"""Read-only version selection and update-envelope tests; no real BLE."""
import json
import struct
import types
import unittest
from unittest.mock import AsyncMock, Mock, patch
from server import Dashboard, read_device_info, update_request, json_packets, selected_protocol

class ProbeTests(unittest.IsolatedAsyncioTestCase):
    def client(self, firmware="7.1.503", model="RIOT Plus"):
        values={"2a24":model,"2a26":firmware,"2a27":"8.0.1"}
        client=Mock()
        client.read_gatt_char=AsyncMock(side_effect=lambda uuid:values[uuid[4:8]].encode()+b"\0")
        client.write_gatt_char=AsyncMock()
        return client

    async def test_read_only_supported_and_unknown_versions(self):
        for firmware,protocol in [("7.1.503","state_rf"),("7.1.645","wsup"),("7.1.600","unsupported")]:
            client=self.client(firmware)
            info=await read_device_info(client,"second")
            self.assertEqual(info["protocol"],protocol)
            self.assertEqual(info["supported"],protocol!="unsupported")
            client.write_gatt_char.assert_not_called()
        self.assertFalse((await read_device_info(self.client(model="Other"),"second"))["supported"])

    async def test_probe_no_lease_no_update_and_failure_clears_old_probe(self):
        client=self.client()
        context=Mock()
        context.__aenter__=AsyncMock(return_value=client)
        context.__aexit__=AsyncMock(return_value=False)
        app=Dashboard({},"192.0.2.1",8000)
        with patch.dict("sys.modules",{"bleak":types.SimpleNamespace(BleakClient=Mock(return_value=context))}):
            await app.probe({"address":"second"})
        self.assertEqual(app.device_info["protocol"],"state_rf")
        self.assertIsNone(app.lease)
        client.write_gatt_char.assert_not_called()

class RequestTests(unittest.TestCase):
    def payload(self,version="2026.10.0"):
        payload=bytearray(80)
        payload[32:36]=struct.pack("<I",0xABCD5432)
        payload[48:48+len(version)]=version.encode()
        return payload

    def test_protocol_envelopes_and_packet_roundtrip(self):
        url="http://192.0.2.1:8000/fw/test.bin"
        self.assertEqual(update_request("wsup",url,self.payload()),"WSUP "+url)
        request=update_request("state_rf",url,self.payload())
        self.assertEqual(request,{"state":{"rF":{"v":"2026.10.0","i":True,"u":url}}})
        packets=json_packets(request,23)
        self.assertEqual(json.loads(b"".join(p[4:] for p in packets)),request)
        for protocol,url,payload in [("unsupported",url,self.payload()),("state_rf",url,self.payload("1234567890123")),("state_rf",url,bytes(80))]:
            with self.assertRaises(ValueError):
                update_request(protocol,url,payload)

    def test_manual_protocol_for_unknown_and_known_firmware(self):
        unknown = {"protocol": "unsupported"}
        with self.assertRaises(ValueError):
            selected_protocol(unknown, {})
        for protocol in ("wsup", "state_rf"):
            self.assertEqual(selected_protocol(unknown, {"protocol": protocol}), protocol)
            self.assertEqual(selected_protocol({"protocol": "wsup"}, {"protocol": protocol}), protocol)
        self.assertEqual(selected_protocol({"protocol": "state_rf"}, {}), "state_rf")
        with self.assertRaises(ValueError):
            selected_protocol(unknown, {"protocol": "guess"})

    def test_stale_image_confirmation_sends_nothing(self):
        app = Dashboard({}, "192.0.2.1", 8000)
        app.payloads["esphome"] = self.payload()
        app.images["esphome"] = {"sha256": "current", "bytes": 80}
        app.devices = [{"address": "second", "wsup_service": True}]
        app.device_info = {"address": "second", "protocol": "wsup"}
        with self.assertRaisesRegex(ValueError, "Image changed"):
            app.start("flash", {"address": "second", "image": "esphome", "confirm": True,
                                "image_sha256": "old"})
        self.assertFalse(app.busy)
        self.assertIsNone(app.lease)

    def test_flash_requires_matching_probe_and_protocol(self):
        app=Dashboard({},"192.0.2.1",8000)
        app.payloads={"esphome":self.payload()}
        app.devices=[{"address":"second","wsup_service":True}]
        for info in [None,{"address":"other","supported":True},{"address":"second","supported":False,"protocol":"unsupported"}]:
            app.device_info=info
            with self.assertRaises(ValueError):
                app.start("flash",{"address":"second","image":"esphome","confirm":True})
            self.assertFalse(app.busy)
            self.assertIsNone(app.lease)
