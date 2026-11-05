"""Offline provisioning safety tests. No real Bluetooth adapter is used."""
import json
import unittest
import types
from unittest.mock import AsyncMock, Mock, patch

from server import Dashboard


class InlineThread:
    def __init__(self, *, target, daemon):
        self.target = target

    def start(self):
        self.target()


class ProvisioningSafety(unittest.TestCase):
    def setUp(self):
        self.app = Dashboard({}, "192.0.2.1", 8000)
        self.app.devices = [
            {"address": "second-unit", "name": "RIOT Plus", "wsup_service": True},
            {"address": "wrong-service", "name": "Hatch", "wsup_service": False},
        ]

    def test_empty_images_are_provision_only_and_start_nothing(self):
        self.assertFalse(self.app.busy)
        self.assertIsNone(self.app.lease)
        self.assertEqual(self.app.images, {})
        with self.assertRaises(ValueError):
            self.app.start("flash", {"address": "second-unit", "image": "esphome", "confirm": True})

    def test_wifi_actions_require_explicit_confirmed_scanned_target(self):
        for action in ("wifi_scan", "wifi_configure"):
            for address, confirm in (("second-unit", False), ("second-unit", "true"),
                                     ("unknown", True), ("wrong-service", True), (None, True)):
                with self.subTest(action=action, address=address, confirm=confirm):
                    with self.assertRaises(ValueError):
                        self.app.start(action, {"address": address, "confirm": confirm,
                                                "ssid": "Test network", "password": "test-password"})
                    self.assertFalse(self.app.busy)
                    self.assertIsNone(self.app.lease)

    def test_wifi_action_does_not_call_flash(self):
        for action in ("wifi_scan", "wifi_configure"):
            with self.subTest(action=action), patch("server.threading.Thread", InlineThread), \
                    patch.dict("sys.modules", {"bleak": types.SimpleNamespace(BleakClient=Mock(side_effect=RuntimeError("offline test")))}), \
                    patch.object(self.app, "flash", new_callable=AsyncMock) as flash:
                self.app.start(action, {"address": "second-unit", "confirm": True,
                                        "ssid": "Test network", "password": "test-password"})
                flash.assert_not_called()
                self.assertIsNone(self.app.lease)
                self.assertEqual(self.app.transfer["sent"], 0)


class PacketContract(unittest.TestCase):
    def test_command_packet_round_trip_and_att_bounds(self):
        from server import json_packets
        command = "WSS" + "é" * 20 + '"\\network'
        for mtu in (23, 80, 247):
            with self.subTest(mtu=mtu):
                packets = json_packets(command, mtu_size=mtu)
                payload = b""
                for index, packet in enumerate(packets):
                    self.assertLessEqual(len(packet), min(80, mtu - 3))
                    header = int.from_bytes(packet[:4], "little")
                    self.assertEqual(header & 0x7fffffff, index)
                    self.assertEqual(bool(header & 0x80000000), index == len(packets) - 1)
                    payload += packet[4:]
                self.assertEqual(json.loads(payload.rstrip(b"\0")), {"command": command})

    def test_multifragment_notification_reassembly(self):
        from server import JsonReplies
        raw = json.dumps({"wifiNetworks": [{"ssid": "é" * 10, "rssi": -42}]}).encode()
        reader = JsonReplies()
        chunks = [raw[i:i+16] for i in range(0, len(raw), 16)]
        for index, chunk in enumerate(chunks):
            final = index == len(chunks) - 1
            header = index | (0x80000000 if final else 0)
            result = reader.feed(header.to_bytes(4, "little") + chunk + (b"\0" * 3 if final else b""))
            if final:
                self.assertEqual(result, json.loads(raw))
            else:
                self.assertIsNone(result)

    def test_out_of_order_or_malformed_reply_is_not_accepted(self):
        from server import JsonReplies
        for packet in (b"short", (0x80000001).to_bytes(4, "little") + b'{"result":"OK"}',
                       (0x80000000).to_bytes(4, "little") + b"not JSON"):
            with self.subTest(packet=packet):
                reader = JsonReplies()
                with self.assertRaises(ValueError):
                    reader.feed(packet)


class ProvisioningErrors(unittest.TestCase):
    def test_transport_exception_does_not_expose_credentials(self):
        app = Dashboard({}, "192.0.2.1", 8000)
        app.devices = [{"address": "second-unit", "name": "RIOT Plus", "wsup_service": True}]
        password = "private-password-do-not-log"
        fake_bleak = types.SimpleNamespace(BleakClient=Mock(
            side_effect=RuntimeError("transport echoed " + password)))
        with patch("server.threading.Thread", InlineThread), patch.dict("sys.modules", {"bleak": fake_bleak}), \
                patch.object(app, "flash", new_callable=AsyncMock) as flash:
            app.start("wifi_configure", {"address": "second-unit", "confirm": True,
                                          "ssid": "private-network", "password": password})
            flash.assert_not_called()
        self.assertFalse(app.busy)
        self.assertIsNotNone(app.error)
        self.assertNotIn(password, json.dumps(app.status()))
        self.assertIsNone(app.lease)

    def test_invalid_credentials_are_rejected_without_starting_worker(self):
        for ssid, password in (("", "password"), ("a\0b", "password"),
                               ("valid", "a\0b"), (None, "password"),
                               ("valid", None)):
            app = Dashboard({}, "192.0.2.1", 8000)
            app.devices = [{"address": "second-unit", "name": "RIOT Plus", "wsup_service": True}]
            with self.subTest(ssid=ssid), patch("server.threading.Thread") as thread:
                with self.assertRaises(ValueError):
                    app.start("wifi_configure", {"address": "second-unit", "confirm": True,
                                                  "ssid": ssid, "password": password})
                thread.assert_not_called()
                self.assertFalse(app.busy)


class FakeClient:
    mtu_size = 23

    def __init__(self, ack="OK", missing_characteristic=False):
        from server import JsonReplies
        self.ack = ack
        self.callbacks = {}
        self.commands = []
        self.reader = JsonReplies()
        self.services = types.SimpleNamespace(
            get_service=lambda uuid: object(),
            get_characteristic=lambda uuid: None if missing_characteristic else object())

    async def __aenter__(self):
        return self

    async def __aexit__(self, *args):
        return False

    async def start_notify(self, uuid, callback):
        self.callbacks[uuid] = callback

    def notify_json(self, reply):
        from server import NOTIFY
        raw = json.dumps(reply).encode()
        chunks = [raw[i:i+16] for i in range(0, len(raw), 16)]
        for index, chunk in enumerate(chunks):
            header = index | (0x80000000 if index == len(chunks)-1 else 0)
            self.callbacks[NOTIFY](None, header.to_bytes(4, "little") + chunk)

    async def write_gatt_char(self, uuid, packet, response):
        from server import UART_WRITE, UART_NOTIFY
        if uuid == UART_WRITE:
            command = packet.decode()
            self.commands.append(command)
            self.callbacks[UART_NOTIFY](None, b"OK")
            if command == "WGS":
                self.notify_json({"wifiNetworks": [{"ssid": "Test network", "password": "hidden"}]})
        else:
            reply = self.reader.feed(packet)
            if reply is not None:
                self.commands.append(reply["command"])
                if self.ack is not None:
                    self.notify_json({"result": self.ack, "command": reply["command"]})


class ProvisioningFlow(unittest.IsolatedAsyncioTestCase):
    async def test_success_sequence_and_no_connectivity_claim(self):
        app = Dashboard({}, "192.0.2.1", 8000)
        client = FakeClient()
        sleeper = AsyncMock()
        with patch.dict("sys.modules", {"bleak": types.SimpleNamespace(BleakClient=lambda *a, **kw: client)}), \
                patch("server.asyncio.sleep", sleeper), patch.object(app, "flash", new_callable=AsyncMock) as flash:
            await app.wifi_configure({"address": "second-unit", "ssid": "Test network", "password": "test-password"})
        self.assertEqual(client.commands, ["WSIW", "WSSTest network", "WSstest-password"])
        self.assertEqual(sleeper.await_args_list[0].args, (0.5,))
        self.assertTrue(app.wifi["credentials_accepted"])
        self.assertFalse(app.wifi["connectivity_verified"])
        self.assertIsNone(app.lease)
        flash.assert_not_called()
        self.assertNotIn("test-password", json.dumps(app.status()))

    async def test_rejected_or_missing_ack_stops_before_password(self):
        for ack in ("ERROR", None):
            app = Dashboard({}, "192.0.2.1", 8000)
            client = FakeClient(ack=ack)
            with self.subTest(ack=ack), patch.dict("sys.modules", {"bleak": types.SimpleNamespace(BleakClient=lambda *a, **kw: client)}), \
                    patch("server.ACK_TIMEOUT", 0.01):
                with self.assertRaises(ValueError):
                    await app.wifi_configure({"address": "second-unit", "ssid": "Test network", "password": "test-password"})
            self.assertEqual(client.commands, ["WSIW", "WSSTest network"])
            self.assertFalse(app.wifi["credentials_accepted"])
            self.assertIsNone(app.lease)

    async def test_scan_uses_uart_and_keeps_only_network_names(self):
        app = Dashboard({}, "192.0.2.1", 8000)
        client = FakeClient()
        with patch.dict("sys.modules", {"bleak": types.SimpleNamespace(BleakClient=lambda *a, **kw: client)}):
            await app.wifi_scan({"address": "second-unit"})
        self.assertEqual(client.commands, ["WGS"])
        self.assertEqual(app.wifi["networks"], [{"ssid": "Test network"}])
        self.assertNotIn("hidden", json.dumps(app.status()))
        self.assertFalse(app.wifi["credentials_accepted"])

    async def test_missing_setup_characteristic_sends_nothing(self):
        app = Dashboard({}, "192.0.2.1", 8000)
        client = FakeClient(missing_characteristic=True)
        with patch.dict("sys.modules", {"bleak": types.SimpleNamespace(BleakClient=lambda *a, **kw: client)}):
            with self.assertRaises(ValueError):
                await app.wifi_scan({"address": "second-unit"})
        self.assertEqual(client.commands, [])

    async def test_status_drops_arbitrary_device_echoes(self):
        app = Dashboard({}, "192.0.2.1", 8000)
        app.wifi_reply({"result": "private-secret", "command": "private-secret",
                        "wifiStatus": {"status": "private-secret", "reason": "private-secret", "reasonCode": 202}})
        self.assertNotIn("private-secret", json.dumps(app.status()))
        self.assertEqual(app.wifi["reasonCode"], 202)


class AdvertisedAddressSafety(unittest.IsolatedAsyncioTestCase):
    @staticmethod
    def socket_mock(failure=None):
        fake_socket = Mock()
        fake_socket.__enter__ = Mock(return_value=fake_socket)
        fake_socket.__exit__ = Mock(return_value=False)
        fake_socket.bind.side_effect = failure
        return fake_socket

    async def test_stale_address_stops_before_ble_command_or_url_lease(self):
        app = Dashboard({}, "192.0.2.99", 8000)
        app.payloads["esphome"] = b"private-firmware-credentials"
        constructor = Mock()
        fake_socket = self.socket_mock(OSError("private-system-detail"))
        with patch("server.socket.socket", return_value=fake_socket), \
                patch.dict("sys.modules", {"bleak": types.SimpleNamespace(BleakClient=constructor)}):
            with self.assertRaises(ValueError) as error:
                await app.flash({"address": "second-unit", "image": "esphome"})
        constructor.assert_not_called()
        self.assertIsNone(app.lease)
        self.assertEqual(app.transfer["sent"], 0)
        self.assertNotIn("private-system-detail", str(error.exception))
        self.assertNotIn("private-firmware-credentials", json.dumps(app.status()))

    async def test_stale_address_rejected_before_worker_starts(self):
        app = Dashboard({}, "192.0.2.99", 8000)
        app.payloads["esphome"] = b"private-firmware-credentials"
        app.devices = [{"address": "second-unit", "wsup_service": True}]
        with patch("server.socket.socket", return_value=self.socket_mock(OSError("private-detail"))), \
                patch("server.threading.Thread") as thread:
            with self.assertRaises(ValueError):
                app.start("flash", {"address": "second-unit", "image": "esphome", "confirm": True})
            thread.assert_not_called()
        self.assertFalse(app.busy)
        self.assertIsNone(app.lease)

    async def test_local_address_bind_is_accepted_and_shown_in_status(self):
        app = Dashboard({}, "192.0.2.7", 8123)
        fake_socket = self.socket_mock()
        with patch("server.socket.socket", return_value=fake_socket):
            app.check_advertise_address()
        fake_socket.bind.assert_called_once_with(("192.0.2.7", 0))
        self.assertEqual(app.status()["advertise"], "192.0.2.7")
        self.assertEqual(app.status()["port"], 8123)


class FlashDiagnostics(unittest.IsolatedAsyncioTestCase):
    async def test_recognized_diagnostics_keep_only_safe_fields(self):
        app = Dashboard({}, "192.0.2.7", 8000)
        secret = "private-auth-password-url"
        app.flash_reply({"result": "E01", "command": secret, "auth": secret,
                         "wifiStatus": {"status": "STA_DISCONNECTED", "reasonCode": 202, "password": secret},
                         "iotStatus": {"status": "SHADOW_CONNECT_FAIL", "isFail": True, "reason": 42, "auth": secret},
                         "otaStatus": {"status": "FAILED", "progress": 12, "errorCode": 3, "url": secret}})
        serialized = json.dumps(app.status())
        self.assertNotIn(secret, serialized)
        event = app.events[-1]["message"]
        fields = json.loads(event.split(": ", 1)[1])
        self.assertEqual(fields["result"], "E01")
        self.assertEqual(fields["wifiStatus"]["reasonCode"], 202)
        self.assertEqual(fields["iotStatus"], {"status": "SHADOW_CONNECT_FAIL", "isFail": True, "reason": 42})
        self.assertNotIn("otaStatus", fields)
        self.assertEqual(app.wifi["status"], "STA_DISCONNECTED")
        self.assertFalse(app.wifi["connectivity_verified"])

    async def test_unknown_text_and_invalid_numbers_are_omitted(self):
        app = Dashboard({}, "192.0.2.7", 8000)
        secret = "private-response-detail"
        app.flash_reply({"result": secret, "wifiStatus": {"status": secret, "reason": secret},
                         "iotStatus": {"status": secret, "reason": -1, "isFail": secret},
                         "updateStatus": {"status": secret, "bytes": True, "total": 2**64, "reasonCode": -1}})
        serialized = json.dumps(app.status())
        self.assertNotIn(secret, serialized)
        fields = json.loads(app.events[-1]["message"].split(": ", 1)[1])
        self.assertEqual(fields, {"result": "UNRECOGNIZED"})

    async def test_flash_callback_reassembles_and_sanitizes_multipart_replies(self):
        from server import NOTIFY
        app = Dashboard({}, "192.0.2.7", 8000)
        app.payloads["esphome"] = b"fake-private-firmware"
        app.device_info = {"address": "second-unit", "model": "RIOT Plus", "firmware": "7.1.645",
                           "hardware": "8.0.2", "protocol": "wsup", "supported": True}
        client = FakeClient()
        client.read_gatt_char = AsyncMock(side_effect=lambda uuid: {
            "2a24": b"RIOT Plus", "2a26": b"7.1.645", "2a27": b"8.0.2"}[uuid[4:8]])
        secret = "private-command-echo"
        callbacks_observed = []

        async def diagnostic_wait(awaitable, seconds):
            awaitable.close()
            from server import FLASH_OBSERVE_SECONDS, FIRMWARE_URL_SECONDS
            self.assertEqual(seconds, FLASH_OBSERVE_SECONDS)
            self.assertGreater(FIRMWARE_URL_SECONDS, FLASH_OBSERVE_SECONDS)
            callback = client.callbacks[NOTIFY]
            raw = json.dumps({"result": "E01", "command": secret,
                              "iotStatus": {"status": "TIME_FETCH_FAIL", "isFail": True, "reason": 7}}).encode()
            split = len(raw) // 2
            before = len(app.events)
            callback(None, (0).to_bytes(4, "little") + raw[:split])
            self.assertEqual(len(app.events), before)
            callback(None, (0x80000001).to_bytes(4, "little") + raw[split:] + b"\0\0")
            callbacks_observed.append(app.events[-1]["message"])
            callback(None, (0x80000000).to_bytes(4, "little") + b"not-json-" + secret.encode())

        with patch.object(app, "check_advertise_address"), \
                patch.dict("sys.modules", {"bleak": types.SimpleNamespace(BleakClient=lambda *a, **kw: client)}), \
                patch("server.asyncio.wait_for", side_effect=diagnostic_wait):
            with self.assertRaisesRegex(ValueError, "wsup update request rejected: E01"):
                await app.flash({"address": "second-unit", "image": "esphome"})
        self.assertIsNone(app.lease)
        self.assertEqual(len(callbacks_observed), 1)
        self.assertIn('"result": "E01"', callbacks_observed[0])
        self.assertNotIn(secret, json.dumps(app.status()))
        self.assertNotIn("fake-private-firmware", json.dumps(app.status()))
        self.assertIn("raw content omitted", app.events[-1]["message"])
        self.assertTrue(client.commands[0].startswith("WSUP http://192.0.2.7:8000/fw/"))


if __name__ == "__main__":
    unittest.main()
