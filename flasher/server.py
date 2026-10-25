#!/usr/bin/env python3
"""Local WSUP dashboard. No scan or device command happens at startup."""
import argparse
import asyncio
import hashlib
import http.server
import ipaddress
import json
from pathlib import Path
import secrets
import struct
import threading
import time
from urllib.parse import urlsplit

SERVICE = "02340000-5efd-47eb-9c1a-de53f7a2b232"
WRITE = "02340003-5efd-47eb-9c1a-de53f7a2b232"
NOTIFY = "02340004-5efd-47eb-9c1a-de53f7a2b232"

def validate_application(payload):
    """Validate ESP32 app framing, app descriptor, checksum and optional SHA."""
    if len(payload) < 32 or payload[0] != 0xE9:
        raise ValueError("Expected an ESP32 application-only image")
    segments = payload[1]
    if not 1 <= segments <= 16 or struct.unpack_from("<H", payload, 12)[0] != 0:
        raise ValueError("Expected an ESP32 application image")
    offset, checksum = 24, 0xEF
    for index in range(segments):
        if offset + 8 > len(payload):
            raise ValueError("Truncated segment header")
        _, length = struct.unpack_from("<II", payload, offset)
        offset += 8
        if offset + length > len(payload):
            raise ValueError("Truncated application segment")
        data = payload[offset:offset+length]
        if index == 0 and (len(data) < 4 or struct.unpack_from("<I", data)[0] != 0xABCD5432):
            raise ValueError("Missing application descriptor; bootloader/factory images are not accepted")
        for byte in data:
            checksum ^= byte
        offset += length
    checksum_offset = offset | 15
    if checksum_offset >= len(payload) or payload[checksum_offset] != checksum:
        raise ValueError("Invalid application checksum")
    end = checksum_offset + 1
    if payload[23] == 1:
        if payload[end:end+32] != hashlib.sha256(payload[:end]).digest():
            raise ValueError("Invalid application SHA256")
        end += 32
    elif payload[23] != 0:
        raise ValueError("Invalid appended-hash flag")
    if end != len(payload):
        raise ValueError("Trailing flash data/signature: use an unsigned application-only OTA image")

class Dashboard:
    def __init__(self, images, advertise, port):
        self.payloads = {k: Path(v).read_bytes() for k,v in images.items()}
        self.images = {k: {"bytes":len(v), "sha256":hashlib.sha256(v).hexdigest()} for k,v in self.payloads.items()}
        self.advertise, self.port = advertise, port
        self.lock = threading.RLock()
        self.busy = False
        self.devices = []
        self.events = []
        self.operation = "Ready — scan to find the device"
        self.error = None
        self.transfer = {"sent":0,"total":0}
        self.lease = None
    def event(self, message):
        with self.lock:
            self.events.append({"time":time.strftime("%H:%M:%S"),"message":message})
            self.events = self.events[-100:]
    def status(self):
        with self.lock:
            return dict(busy=self.busy,devices=list(self.devices),events=list(self.events),
                        operation=self.operation,error=self.error,transfer=dict(self.transfer),images=self.images)
    def start(self, action, body):
        with self.lock:
            if self.busy:
                raise ValueError("An operation is already running")
            if action == "flash":
                if body.get("confirm") is not True or body.get("image") not in self.payloads:
                    raise ValueError("Explicit image confirmation required")
                if not any(d["address"] == body.get("address") and d["wsup_service"] for d in self.devices):
                    raise ValueError("Select a scanned device advertising the WSUP service")
            self.busy, self.error = True, None
            self.operation = "Scanning" if action == "scan" else "Connecting to selected device"
        def worker():
            try:
                asyncio.run(self.scan() if action == "scan" else self.flash(body))
            except Exception as exc:
                with self.lock:
                    self.error = str(exc)
                    self.operation = "Operation failed"
                self.event(str(exc))
            finally:
                with self.lock:
                    self.busy = False
        threading.Thread(target=worker, daemon=True).start()
    async def scan(self):
        from bleak import BleakScanner
        self.event("Scanning for 15 seconds; no command is sent")
        found = await BleakScanner.discover(timeout=15, return_adv=True)
        devices = [dict(address=d.address,name=ad.local_name or d.name or "Unnamed",rssi=ad.rssi,
                        wsup_service=SERVICE in [s.lower() for s in (ad.service_uuids or [])])
                   for d,ad in found.values()]
        # Hatch can omit its custom UUID from advertisements. Probe candidates
        # read-only before enabling an explicit flash selection.
        from bleak import BleakClient
        devices = [d for d in devices if d["wsup_service"] or d["name"].startswith(("RIOT", "Hatch"))]
        for device in devices:
            if not device["wsup_service"]:
                try:
                    async with BleakClient(device["address"], timeout=10) as client:
                        device["wsup_service"] = client.services.get_service(SERVICE) is not None
                except Exception as exc:
                    self.event(f"Could not inspect {device['name']}: {exc}")
        devices.sort(key=lambda d:(not d["wsup_service"],-d["rssi"]))
        with self.lock:
            self.devices = devices
            self.operation = "Scan finished"
        self.event(f"Found {len(devices)} BLE devices")
    async def flash(self, body):
        from bleak import BleakClient
        address, image = body["address"], body["image"]
        token = secrets.token_urlsafe(24)
        async with BleakClient(address, timeout=20) as client:
            if client.services.get_service(SERVICE) is None:
                raise ValueError("Selected device does not expose the WSUP service; nothing sent")
            payload = self.payloads[image]
            with self.lock:
                self.lease = (token, image, time.monotonic()+180)
                self.transfer = {"sent":0,"total":len(payload)}
            def notify(_handle, data):
                self.event("BLE reply: " + bytes(data[4:]).decode("utf-8",errors="replace")[:500])
            await client.start_notify(NOTIFY, notify)
            url = f"http://{self.advertise}:{self.port}/fw/{token}.bin"
            command = json.dumps({"command":"WSUP " + url}).encode()
            self.event(f"Sending {image} WSUP to selected device {address}")
            for idx,offset in enumerate(range(0,len(command),76)):
                part = command[offset:offset+76]
                header = idx | (0x80000000 if offset+76 >= len(command) else 0)
                await client.write_gatt_char(WRITE,struct.pack("<I",header)+part,response=True)
            with self.lock:
                self.operation = "WSUP sent — waiting for download/replies (40 seconds)"
            await asyncio.sleep(40)
        with self.lock:
            complete = self.transfer["sent"] == self.transfer["total"]
            self.operation = "HTTP payload sent — boot NOT verified" if complete else "Observation ended — download not completed"
        self.event(self.operation)

class Handler(http.server.BaseHTTPRequestHandler):
    def local(self):
        return ipaddress.ip_address(self.client_address[0]).is_loopback
    def reply(self, status, data, content_type="application/json"):
        raw = json.dumps(data).encode() if content_type == "application/json" else data
        self.send_response(status)
        self.send_header("Content-Type",content_type)
        self.send_header("Content-Length",str(len(raw)))
        self.send_header("Cache-Control","no-store")
        self.send_header("X-Content-Type-Options","nosniff")
        self.end_headers()
        self.wfile.write(raw)
    def do_GET(self):
        app = self.server.app
        if self.path.startswith("/fw/"):
            with app.lock:
                lease = app.lease
                if not lease or time.monotonic() > lease[2] or not self.path.startswith(f"/fw/{lease[0]}.bin"):
                    return self.reply(404,{"error":"No active firmware URL"})
                payload = app.payloads[lease[1]]
            self.send_response(200)
            self.send_header("Content-Type","application/octet-stream")
            self.send_header("Content-Length",str(len(payload)))
            self.end_headers()
            app.event("Device requested firmware; sending HTTP payload")
            try:
                for start in range(0,len(payload),16384):
                    chunk = payload[start:start+16384]
                    self.wfile.write(chunk)
                    self.wfile.flush()
                    with app.lock:
                        app.transfer["sent"] = start+len(chunk)
                app.event("Full HTTP payload sent; this does NOT verify flash or boot")
            except (BrokenPipeError,ConnectionResetError):
                app.event("Firmware HTTP connection interrupted")
            return
        if not self.local():
            return self.reply(403,{"error":"Dashboard is localhost-only"})
        if self.path == "/api/status":
            return self.reply(200,app.status())
        if self.path == "/":
            return self.reply(200,Path(__file__).with_name("dashboard.html").read_bytes(),"text/html; charset=utf-8")
        self.reply(404,{"error":"Not found"})
    def do_POST(self):
        origin = urlsplit(self.headers.get("Origin", ""))
        if not self.local() or self.headers.get("Host") != f"127.0.0.1:{self.server.server_port}" or origin.netloc != self.headers.get("Host") or origin.scheme != "http":
            return self.reply(403,{"error":"Use the local dashboard"})
        try:
            length = int(self.headers.get("Content-Length",0))
            if not 0 < length <= 4096:
                raise ValueError("Invalid request size")
            body = json.loads(self.rfile.read(length))
            action = {"/api/scan":"scan","/api/flash":"flash"}.get(self.path)
            if not action or not isinstance(body,dict):
                raise ValueError("Invalid operation")
            self.server.app.start(action,body)
            self.reply(202,{"ok":True})
        except (ValueError,TypeError) as exc:
            self.reply(400,{"error":str(exc)})
    def log_message(self,*args):
        pass

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--esphome",required=True,type=Path)
    parser.add_argument("--stock",type=Path,help="Optional archived stock application; not recovery from ESPHome")
    parser.add_argument("--advertise",required=True,help="Mac LAN IPv4 reachable by Hatch")
    parser.add_argument("--port",type=int,default=8000)
    args = parser.parse_args()
    ipaddress.IPv4Address(args.advertise)
    images = {"esphome":args.esphome}
    if args.stock:
        images["stock"] = args.stock
    for path in images.values():
        try:
            validate_application(path.read_bytes())
        except ValueError as exc:
            parser.error(f"{path.name}: {exc}")
    server = http.server.ThreadingHTTPServer(("0.0.0.0",args.port),Handler)
    server.app = Dashboard(images,args.advertise,args.port)
    print(f"Dashboard http://127.0.0.1:{args.port}; only temporary firmware URLs are accessible over LAN",flush=True)
    server.serve_forever()

if __name__ == "__main__":
    main()
