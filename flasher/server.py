#!/usr/bin/env python3
"""Local stock BLE flasher. No scan or device command happens at startup."""
import argparse
import asyncio
import hashlib
import http.server
import ipaddress
import json
from pathlib import Path
import re
import secrets
import socket
import struct
import threading
import time
from urllib.parse import unquote, urlsplit

SERVICE = "02340000-5efd-47eb-9c1a-de53f7a2b232"
WRITE = "02340003-5efd-47eb-9c1a-de53f7a2b232"
NOTIFY = "02340004-5efd-47eb-9c1a-de53f7a2b232"

UART_WRITE = "02340001-5efd-47eb-9c1a-de53f7a2b232"
UART_NOTIFY = "02340002-5efd-47eb-9c1a-de53f7a2b232"
ACK_TIMEOUT = 15
WIFI_SCAN_TIMEOUT = 20
WIFI_STATUS_TIMEOUT = 10
MAX_IMAGE_BYTES = 8 * 1024 * 1024


def json_packets(command, mtu_size=23):
    """Stock-compatible little-endian framing, bounded by negotiated ATT MTU."""
    capacity = min(80, mtu_size - 3) - 4
    if capacity < 1:
        raise ValueError("BLE MTU is too small")
    raw = json.dumps(command if isinstance(command, dict) else {"command": command},
                     ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return [struct.pack("<I", index | (0x80000000 if offset + capacity >= len(raw) else 0))
            + raw[offset:offset + capacity]
            for index, offset in enumerate(range(0, len(raw), capacity))]


# Both routes verified by completed ESPHome installs; unknown revisions stay blocked.
FLASH_OBSERVE_SECONDS = 180
FIRMWARE_URL_SECONDS = 300
UPDATE_PROTOCOLS = {"7.1.503": "state_rf", "7.1.645": "wsup"}


async def read_device_info(client, address):
    fields = {"model": "2a24", "firmware": "2a26", "hardware": "2a27"}
    info = {"address": address}
    for name, short in fields.items():
        value = await client.read_gatt_char(f"0000{short}-0000-1000-8000-00805f9b34fb")
        info[name] = bytes(value).rstrip(b"\0").decode("utf-8")
    protocol = UPDATE_PROTOCOLS.get(info["firmware"]) if info["model"] == "RIOT Plus" else None
    info.update(protocol=protocol or "unsupported", supported=protocol is not None)
    return info


def selected_protocol(info, body):
    override = body.get("protocol")
    if override is not None and override not in ("wsup", "state_rf"):
        raise ValueError("Choose WSUP or legacy state.rF")
    protocol = override or info["protocol"]
    if protocol not in ("wsup", "state_rf"):
        raise ValueError("Unknown firmware: explicitly choose an update protocol")
    return protocol


def update_request(protocol, url, payload):
    if protocol == "wsup":
        return "WSUP " + url
    if protocol == "state_rf":
        # ESP app descriptor magic and version field, not a fabricated stock version.
        if payload[32:36] != struct.pack("<I", 0xABCD5432):
            raise ValueError("Application descriptor missing")
        version = payload[48:80].split(b"\0", 1)[0].decode("ascii")
        if not version or len(version.encode()) > 12 or len(url.encode()) > 256:
            raise ValueError("Image version or URL exceeds stock state.rF limits")
        return {"state": {"rF": {"v": version, "i": True, "u": url}}}
    raise ValueError("Unsupported stock firmware; no update request sent")


class JsonReplies:
    """Reassemble stock little-endian JSON notifications; never retain raw replies."""
    def __init__(self):
        self.buffer = bytearray()
        self.index = 0

    def feed(self, data):
        if len(data) <= 4:
            raise ValueError("Invalid BLE JSON framing")
        header = struct.unpack("<I", bytes(data[:4]))[0]
        index, final = header & 0x7fffffff, bool(header & 0x80000000)
        if index == 0:
            self.buffer.clear()
            self.index = 0
        if index != self.index:
            self.buffer.clear()
            self.index = 0
            raise ValueError("Out-of-order BLE JSON reply")
        self.buffer.extend(data[4:])
        self.index += 1
        if len(self.buffer) > 65536:
            self.buffer.clear()
            self.index = 0
            raise ValueError("BLE JSON reply too large")
        if not final:
            return None
        raw = bytes(self.buffer).rstrip(b"\0")
        self.buffer.clear()
        self.index = 0
        try:
            reply = json.loads(raw.decode("utf-8"))
        except (ValueError, UnicodeError):
            raise ValueError("Invalid BLE JSON reply") from None
        if not isinstance(reply, dict):
            raise ValueError("Invalid BLE JSON reply")
        return reply


def validate_credentials(body):
    # Conservative supported subset from stock analysis, not universal Wi-Fi limits.
    for field, limit in (("ssid", 31), ("password", 63)):
        value = body.get(field)
        if not isinstance(value, str) or not value or "\0" in value:
            raise ValueError(f"{field} must be nonempty text without NUL")
        try:
            length = len(value.encode("utf-8"))
        except UnicodeError:
            raise ValueError(f"{field} must be valid UTF-8") from None
        if length > limit:
            raise ValueError(f"{field} exceeds conservative supported {limit}-byte limit")


class WifiError(ValueError):
    """Only fixed, credential-free messages may use this exception."""


class WifiSession:
    def __init__(self, client, dashboard):
        self.client, self.dashboard = client, dashboard
        self.json_queue, self.uart_queue = asyncio.Queue(), asyncio.Queue()
        self.reader = JsonReplies()
        self.networks_received = asyncio.Event()

    async def subscribe(self):
        for uuid in (WRITE, NOTIFY, UART_WRITE, UART_NOTIFY):
            if self.client.services.get_characteristic(uuid) is None:
                raise WifiError("Selected device lacks Wi-Fi setup characteristics; nothing sent")
        await self.client.start_notify(NOTIFY, self.json_notify)
        await self.client.start_notify(UART_NOTIFY, self.uart_notify)

    def json_notify(self, _handle, data):
        try:
            reply = self.reader.feed(data)
            if reply is None:
                return
            self.dashboard.wifi_reply(reply)
            if isinstance(reply.get("wifiNetworks"), list):
                self.networks_received.set()
            if "result" in reply:
                self.json_queue.put_nowait(reply)
        except ValueError:
            self.json_queue.put_nowait({"result": "INVALID"})

    def uart_notify(self, _handle, data):
        # UART responses are unframed. Do not expose them in logs/status.
        self.uart_queue.put_nowait(bytes(data).strip(b"\0\r\n "))

    async def command(self, command, uart=False):
        queue = self.uart_queue if uart else self.json_queue
        while not queue.empty():
            queue.get_nowait()
        if uart:
            await self.client.write_gatt_char(UART_WRITE, command.encode("utf-8"), response=True)
        else:
            mtu = getattr(self.client, "mtu_size", 23)
            for packet in json_packets(command, mtu):
                await self.client.write_gatt_char(WRITE, packet, response=True)
        deadline = asyncio.get_running_loop().time() + ACK_TIMEOUT
        while True:
            try:
                reply = await asyncio.wait_for(queue.get(), max(0, deadline - asyncio.get_running_loop().time()))
            except asyncio.TimeoutError:
                raise WifiError("Wi-Fi command acknowledgement timed out") from None
            if uart:
                if reply != b"OK":
                    raise WifiError("Wi-Fi UART command was not accepted")
                return
            # Some revisions may echo the command. Never accept a mismatched echo.
            if "command" in reply and reply["command"] != command:
                continue
            if reply.get("result") != "OK":
                raise WifiError("Wi-Fi command was not accepted")
            return

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
        self.images = {k: {"filename":str(images[k]), "bytes":len(v), "sha256":hashlib.sha256(v).hexdigest()} for k,v in self.payloads.items()}
        self.advertise, self.port = advertise, port
        self.lock = threading.RLock()
        self.busy = False
        self.devices = []
        self.events = []
        self.operation = "Ready — scan to find the device"
        self.error = None
        self.transfer = {"sent":0,"total":0}
        self.lease = None
        self.device_info = None
        self.wifi = dict(address=None, networks=[], status=None, reason=None, reasonCode=None,
                         credentials_accepted=False, connectivity_verified=False)
    def event(self, message):
        with self.lock:
            self.events.append({"time":time.strftime("%H:%M:%S"),"message":message})
            self.events = self.events[-100:]
    def status(self):
        with self.lock:
            return dict(firmware_image_safety=1, busy=self.busy,devices=list(self.devices),events=list(self.events),
                        operation=self.operation,error=self.error,transfer=dict(self.transfer),images=self.images,
                        advertise=self.advertise,port=self.port, device_info=self.device_info,
                        wifi={**self.wifi, "networks":list(self.wifi["networks"])})
    def load_esphome(self, payload, filename=None):
        with self.lock:
            if self.busy:
                raise ValueError("An operation is already running")
            if self.lease and time.monotonic() <= self.lease[2]:
                raise ValueError("A firmware download URL is still active; wait for it to expire")
            if not 0 < len(payload) <= MAX_IMAGE_BYTES:
                raise ValueError("Image must be between 1 byte and 8 MiB")
            if filename is not None:
                filename = filename.replace("\\", "/").rsplit("/", 1)[-1]
                if not filename or len(filename) > 255 or any(ord(c) < 32 for c in filename):
                    raise ValueError("Invalid image filename")
            validate_application(payload)
            self.payloads["esphome"] = payload
            self.images = {**self.images, "esphome": {
                "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
                **({"filename": filename} if filename else {})}}
            # A previous download URL must never serve a newly loaded image.
            self.lease = None
            return self.status()

    def check_advertise_address(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
                probe.bind((self.advertise, 0))
        except OSError:
            raise ValueError(
                "Cannot bind the advertised LAN address. Restart the flasher with "
                "--advertise set to this Mac's current LAN IPv4 address. No WSUP sent."
            ) from None

    def start(self, action, body):
        body = dict(body)
        with self.lock:
            if self.busy:
                raise ValueError("An operation is already running")
            if action not in ("scan", "probe", "flash", "wifi_scan", "wifi_configure"):
                raise ValueError("Invalid operation")
            if action != "scan":
                if body.get("confirm") is not True:
                    raise ValueError("Explicit target confirmation required")
                if not any(d["address"] == body.get("address") and d["wsup_service"] for d in self.devices):
                    raise ValueError("Select a scanned device advertising the stock BLE control service")
            if action == "flash" and body.get("image") not in self.payloads:
                raise ValueError("Explicit available image confirmation required")
            if action == "flash":
                if not self.device_info or self.device_info["address"] != body.get("address"):
                    raise ValueError("Probe the selected device before flashing")
                selected_protocol(self.device_info, body)
                if body.get("image_sha256") != self.images[body["image"]]["sha256"]:
                    raise ValueError("Image changed or hash not confirmed; review the image and confirm again")
                self.check_advertise_address()
            if action in ("scan", "probe"):
                self.device_info = None
            if action == "wifi_configure":
                validate_credentials(body)
            if action.startswith("wifi_"):
                self.wifi = dict(address=body["address"], networks=[], status=None, reason=None, reasonCode=None,
                                 credentials_accepted=False, connectivity_verified=False)
            self.busy, self.error = True, None
            self.operation = "Scanning" if action == "scan" else "Connecting to selected device"
        def worker():
            try:
                method = getattr(self, action)
                asyncio.run(method() if action == "scan" else method(body))
            except Exception as exc:
                # BLE/backend exception text can contain transmitted credentials.
                message = (str(exc) if isinstance(exc, WifiError) else
                           "Wi-Fi operation failed; no flash was started") if action.startswith("wifi_") else str(exc)
                with self.lock:
                    self.error = message
                    self.operation = "Operation failed"
                self.event(message)
            finally:
                body.pop("password", None)
                with self.lock:
                    self.busy = False
        threading.Thread(target=worker, daemon=True).start()

    def wifi_reply(self, reply):
        # Keep only display-safe fields, never raw responses or command echoes.
        with self.lock:
            networks = reply.get("wifiNetworks")
            if isinstance(networks, list):
                self.wifi["networks"] = [
                    {"ssid": n if isinstance(n, str) else n["ssid"]}
                    for n in networks[:200]
                    if isinstance(n, str) or (isinstance(n, dict) and isinstance(n.get("ssid"), str))]
            status = reply.get("wifiStatus")
            if isinstance(status, dict):
                value = status.get("status")
                if value in ("WIFI_READY", "SCAN_DONE", "STA_START", "STA_STOP", "STA_CONNECTED",
                             "STA_DISCONNECTED", "STA_AUTHMODE_CHANGE", "STA_GOT_IP", "STA_LOST_IP",
                             "NO_INTERNET", "UNKNOWN"):
                    self.wifi["status"] = value
                reason = status.get("reason")
                if reason in ("ZERO", "UNSPECIFIED", "AUTH_EXPIRE", "AUTH_LEAVE", "ASSOC_EXPIRE",
                              "ASSOC_TOOMANY", "NOT_AUTHED", "NOT_ASSOCED", "ASSOC_LEAVE",
                              "ASSOC_NOT_AUTHED", "DISASSOC_PWRCAP_BAD", "DISASSOC_SUPCHAN_BAD",
                              "IE_INVALID", "MIC_FAILURE", "GROUP_KEY_UPDATE_TIMEOUT",
                              "IE_IN_4WAY_DIFFERS", "GROUP_CIPHER_INVALID", "PAIRWISE_CIPHER_INVALID",
                              "AKMP_INVALID", "UNSUPP_RSN_IE_VERSION", "INVALID_RSN_IE_CAP",
                              "CIPHER_SUITE_REJECTED", "BEACON_TIMEOUT", "NO_AP_FOUND", "AUTH_FAIL",
                              "ASSOC_FAIL", "HANDSHAKE_TIMEOUT", "UNKNOWN"):
                    self.wifi["reason"] = reason
                code = status.get("reasonCode")
                if isinstance(code, int) and not isinstance(code, bool):
                    self.wifi["reasonCode"] = code

    async def wifi_scan(self, body):
        await self._wifi_operation(body, configure=False)

    async def wifi_configure(self, body):
        validate_credentials(body)
        await self._wifi_operation(body, configure=True)

    async def _wifi_operation(self, body, configure):
        from bleak import BleakClient
        async with BleakClient(body["address"], timeout=20) as client:
            if client.services.get_service(SERVICE) is None:
                raise WifiError("Selected device lacks the setup service; nothing sent")
            session = WifiSession(client, self)
            await session.subscribe()
            if configure:
                self.event("Sending Wi-Fi credentials to the explicitly selected device")
                await session.command("WSIW", uart=True)
                await session.command("WSS" + body["ssid"])
                await asyncio.sleep(0.5)
                await session.command("WSs" + body["password"])
                with self.lock:
                    self.wifi["credentials_accepted"] = True
                    self.operation = "Credentials accepted — observing Wi-Fi status; connectivity NOT verified"
                await asyncio.sleep(WIFI_STATUS_TIMEOUT)
                message = "Credentials accepted — connectivity NOT verified; flash requires a separate action"
            else:
                self.event("Requesting Wi-Fi networks from selected device")
                await session.command("WGS", uart=True)
                try:
                    await asyncio.wait_for(session.networks_received.wait(), WIFI_SCAN_TIMEOUT)
                except asyncio.TimeoutError:
                    raise WifiError("Wi-Fi network scan timed out") from None
                message = "Wi-Fi scan finished — no credentials sent"
        with self.lock:
            self.operation = message
        self.event(message)

    async def probe(self, body):
        from bleak import BleakClient
        async with BleakClient(body["address"], timeout=20) as client:
            info = await read_device_info(client, body["address"])
        with self.lock:
            self.device_info = info
            self.operation = "Device probe finished — no update request sent"
        self.event(f"Stock firmware {info['firmware']}, hardware {info['hardware']}: {info['protocol']}")

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
    def flash_reply(self, reply):
        """Display selected diagnostic fields only; never raw JSON or command echoes."""
        fields = {}
        result = reply.get("result")
        if isinstance(result, str):
            fields["result"] = result if result in ("OK", "ERROR", "FAIL", "E01", "E11") else "UNRECOGNIZED"
        if isinstance(reply.get("wifiStatus"), dict):
            # Reuse the provisioning allowlists, then report only newly supplied fields.
            with self.lock:
                previous = self.wifi
                self.wifi = {**previous, "status": None, "reason": None, "reasonCode": None}
                self.wifi_reply({"wifiStatus": reply["wifiStatus"]})
                safe = {k: self.wifi[k] for k in ("status", "reason", "reasonCode") if self.wifi[k] is not None}
                self.wifi = {**previous, **safe}
            if safe:
                fields["wifiStatus"] = safe
        iot = reply.get("iotStatus")
        if isinstance(iot, dict):
            safe = {}
            if iot.get("status") in (
                "TIME_FETCH_START", "TIME_FETCH_PASS", "TIME_FETCH_FAIL", "CERTS_GOT_CERTS", "CERTS_FAIL",
                "SHADOW_INIT_PASS", "SHADOW_INIT_FAIL", "SHADOW_CONNECT_PASS", "SHADOW_CONNECT_FAIL",
                "SHADOW_RECONNECT_SET_PASS", "SHADOW_RECONNECT_SET_FAIL", "SHADOW_DELTA_REGISTER_PASS",
                "SHADOW_DELTA_REGISTER_FAIL", "SHADOW_RECONNECTING", "SHADOW_RECONNECTED",
                "SHADOW_DISCONNECTED", "SHADOW_UPDATE", "SHADOW_GOT_DELTA", "UNKNOWN"
            ):
                safe["status"] = iot["status"]
            if isinstance(iot.get("isFail"), bool):
                safe["isFail"] = iot["isFail"]
            if type(iot.get("reason")) is int and 0 <= iot["reason"] <= 0xffffffffffffffff:
                safe["reason"] = iot["reason"]
            if safe:
                fields["iotStatus"] = safe
        self.event("BLE diagnostic: " + json.dumps(fields, sort_keys=True) if fields else
                   "BLE JSON reply received; no recognized diagnostic fields")

    async def flash(self, body):
        self.check_advertise_address()
        from bleak import BleakClient
        address, image = body["address"], body["image"]
        token = secrets.token_urlsafe(12)
        async with BleakClient(address, timeout=20) as client:
            if client.services.get_service(SERVICE) is None:
                raise ValueError("Selected device does not expose the stock BLE control service; nothing sent")
            info = await read_device_info(client, address)
            with self.lock:
                previous = self.device_info
                self.device_info = info
            if info != previous:
                raise ValueError("Device identity/version changed; review the probe and confirm again")
            protocol = selected_protocol(info, body)
            rejected = asyncio.Event()
            rejection = None
            payload = self.payloads[image]
            replies = JsonReplies()
            def notify(_handle, data):
                try:
                    reply = replies.feed(data)
                except ValueError:
                    self.event("BLE JSON reply could not be decoded; raw content omitted")
                    return
                if reply is not None:
                    self.flash_reply(reply)
                    result = reply.get("result")
                    if isinstance(result, str) and result != "OK":
                        nonlocal rejection
                        code = result if re.fullmatch(r"E[0-9]{2}|ERROR|FAIL", result) else "unrecognized result"
                        rejection = f"{protocol} update request rejected: {code}. No automatic retry."
                        if result == "E01":
                            rejection += " E01 may mean unsupported command, denied access, or invalid parameters; it does not identify firmware."
                        rejected.set()
            await client.start_notify(NOTIFY, notify)
            self.check_advertise_address()
            with self.lock:
                self.lease = (token, image, time.monotonic()+FIRMWARE_URL_SECONDS)
                self.transfer = {"sent":0,"total":len(payload)}
            url = f"http://{self.advertise}:{self.port}/fw/{token}.bin"
            request = update_request(protocol, url, payload)
            self.event(f"Sending {image} via {protocol} to selected device {address}")
            mtu = getattr(client, "mtu_size", 23)
            packets = json_packets(request, mtu)
            self.event(f"BLE transport: MTU {mtu}, {len(packets)} update packet(s)")
            for packet in packets:
                await client.write_gatt_char(WRITE, packet, response=True)
            with self.lock:
                self.operation = f"Update request sent — observing download/replies ({FLASH_OBSERVE_SECONDS} seconds)"
            try:
                await asyncio.wait_for(rejected.wait(), FLASH_OBSERVE_SECONDS)
            except asyncio.TimeoutError:
                pass
            if rejection:
                with self.lock:
                    self.lease = None
                raise ValueError(rejection)
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
            if self.path == "/api/image/esphome":
                if self.headers.get_content_type() != "application/octet-stream" or self.headers.get("Transfer-Encoding"):
                    raise ValueError("Send a raw application/octet-stream image with Content-Length")
                if not 0 < length <= MAX_IMAGE_BYTES:
                    raise ValueError("Image must be between 1 byte and 8 MiB")
                payload = self.rfile.read(length)
                if len(payload) != length:
                    raise ValueError("Truncated image upload")
                filename = self.headers.get("X-Image-Filename")
                return self.reply(200, self.server.app.load_esphome(
                    payload, unquote(filename) if filename is not None else None))
            if not 0 < length <= 4096:
                raise ValueError("Invalid request size")
            body = json.loads(self.rfile.read(length))
            action = {"/api/scan":"scan", "/api/flash":"flash",
                      "/api/probe":"probe", "/api/wifi_scan":"wifi_scan", "/api/wifi_configure":"wifi_configure"}.get(self.path)
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
    parser.add_argument("--esphome",type=Path,help="Optional application image; omit for Wi-Fi provisioning only")
    parser.add_argument("--stock",type=Path,help="Optional archived stock application; not recovery from ESPHome")
    parser.add_argument("--advertise",required=True,help="Mac LAN IPv4 reachable by Hatch")
    parser.add_argument("--port",type=int,default=8000)
    args = parser.parse_args()
    ipaddress.IPv4Address(args.advertise)
    images = {"esphome":args.esphome} if args.esphome else {}
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
