"""Local image loading only: these tests never access BLE or a device."""
import hashlib
import http.client
import http.server
import json
import socket
import threading
import unittest
from unittest.mock import patch

from server import Dashboard, Handler, MAX_IMAGE_BYTES
from test_server import app as application


class ImageUploads(unittest.TestCase):
    def setUp(self):
        self.app = Dashboard({}, "192.0.2.7", 0)
        self.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.app = self.app
        self.host = f"127.0.0.1:{self.server.server_port}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()

    def upload(self, payload, *, headers=None, path="/api/image/esphome", truncate=False):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=3)
        request_headers = {"Host": self.host, "Origin": "http://" + self.host,
                           "Content-Type": "application/octet-stream"}
        request_headers.update(headers or {})
        connection.request("POST", path, body=payload, headers=request_headers)
        if truncate:
            connection.sock.shutdown(socket.SHUT_WR)
        response = connection.getresponse()
        result = response.status, json.loads(response.read())
        connection.close()
        return result

    def test_load_and_replace_without_operation_or_disk_write(self):
        self.app.payloads["stock"] = b"unchanged-stock"
        self.app.images["stock"] = {"bytes": 15, "sha256": "stock-hash"}
        image = bytes(application())
        with patch.object(self.app, "start") as start:
            status, body = self.upload(image)
            start.assert_not_called()
        self.assertEqual(status, 200)
        self.assertEqual(body["images"]["esphome"], {
            "bytes": len(image), "sha256": hashlib.sha256(image).hexdigest()})
        self.assertEqual(self.app.payloads["esphome"], image)
        self.assertFalse(body["busy"])
        self.assertEqual(body["events"], [])
        self.assertNotIn("\u0000", json.dumps(body))
        self.app.lease = ("old-token", "esphome", 0)
        status, body = self.upload(image)
        self.assertEqual(status, 200)
        self.assertIsNone(self.app.lease)
        self.assertEqual(self.app.payloads["stock"], b"unchanged-stock")
        self.assertEqual(body["images"]["stock"]["sha256"], "stock-hash")

    def test_invalid_images_leave_previous_image_unchanged(self):
        valid = bytes(application())
        self.upload(valid)
        corrupt = bytearray(valid)
        corrupt[40] ^= 1
        for image in (b"secret-invalid-image", valid[:-1], bytes(corrupt), bytes(4096) + valid):
            with self.subTest(size=len(image)):
                status, body = self.upload(image)
                self.assertEqual(status, 400)
                self.assertEqual(self.app.payloads["esphome"], valid)
                self.assertNotIn("secret-invalid-image", json.dumps(body))

    def test_filename_and_safety_capability(self):
        image = bytes(application())
        status, body = self.upload(image, headers={"X-Image-Filename": "Cardiff%20Rest.bin"})
        self.assertEqual(status, 200)
        self.assertEqual(body["firmware_image_safety"], 1)
        self.assertEqual(body["images"]["esphome"]["filename"], "Cardiff Rest.bin")
        status, body = self.upload(image, headers={"X-Image-Filename": "%00bad.bin"})
        self.assertEqual(status, 400)
        self.assertEqual(self.app.images["esphome"]["filename"], "Cardiff Rest.bin")

    def test_busy_rejects_change(self):
        self.app.busy = True
        status, _ = self.upload(bytes(application()))
        self.assertEqual(status, 400)
        self.assertEqual(self.app.payloads, {})

    def test_live_download_lease_rejects_replacement_even_when_idle(self):
        previous = b"previous-image"
        self.app.payloads["esphome"] = previous
        self.app.lease = ("active-token", "esphome", 10**20)
        status, _ = self.upload(bytes(application()))
        self.assertEqual(status, 400)
        self.assertEqual(self.app.payloads["esphome"], previous)
        self.assertEqual(self.app.lease[0], "active-token")

    def test_size_and_transport_guards(self):
        for headers in ({"Content-Length": str(MAX_IMAGE_BYTES + 1)},
                        {"Content-Length": "0"}, {"Content-Length": "-1"},
                        {"Content-Length": "invalid"},
                        {"Content-Type": "application/json"},
                        {"Transfer-Encoding": "chunked"}):
            with self.subTest(headers=headers):
                status, _ = self.upload(b"", headers=headers)
                self.assertEqual(status, 400)
        status, body = self.upload(b"short", headers={"Content-Length": "100"}, truncate=True)
        self.assertEqual(status, 400)
        self.assertIn("Truncated", body["error"])
        self.assertEqual(self.app.payloads, {})

    def test_origin_host_and_local_address_guards(self):
        for headers in ({"Origin": "http://evil.example"},
                        {"Origin": "https://" + self.host},
                        {"Host": "localhost:" + str(self.server.server_port)},
                        {"Origin": ""}):
            with self.subTest(headers=headers):
                status, _ = self.upload(bytes(application()), headers=headers)
                self.assertEqual(status, 403)
        with patch.object(Handler, "local", return_value=False):
            status, _ = self.upload(bytes(application()))
        self.assertEqual(status, 403)
        self.assertEqual(self.app.payloads, {})

    def test_only_fixed_esphome_slot_is_available(self):
        for path in ("/api/image/stock", "/api/image/esphome?path=/tmp/image.bin"):
            status, _ = self.upload(bytes(application()), path=path)
            self.assertEqual(status, 400)
        self.assertEqual(self.app.payloads, {})


if __name__ == "__main__":
    unittest.main()
