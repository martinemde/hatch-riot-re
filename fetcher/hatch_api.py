"""
Shared Hatch cloud-API client.

Endpoints and request shapes were recovered from the Hatch Sleep Android app
(com.hatchbaby.rest 6.40.1) and cross-checked against the community library
https://github.com/dahlb/hatch_rest_api .

  Base URL        https://data.hatchbaby.com/
  Login           POST /public/v1/login            {"email","password"}  -> {"token": ...}
  Auth header     X-HatchBaby-Auth: <token>
  Devices         GET  /service/app/iotDevice/v2/fetch?iotProducts=...
  Firmware check  GET  /service/app/firmwareVersion/v1/check?platform=Android&product=..&macAddress=..&..

Most service responses use the envelope {"status": "success", "payload": ...}.
Login is the exception: the token is a top-level field.

This talks only to Hatch's own API with your own account credentials. Nothing
here touches the device over Bluetooth or flashes anything; it only reads the
version-check response (which contains the firmware download URL) and fetches
that file over HTTPS.
"""

from __future__ import annotations

import json
import os
import time
from pathlib import Path

import requests

# Base host the maintained community library uses. The app also uses
# prod-sa.hatch.co / prod-sleep.hatchbaby.com; override with HATCH_BASE_URL.
BASE_URL = os.environ.get("HATCH_BASE_URL", "https://data.hatchbaby.com/").rstrip("/") + "/"

# User-Agent the library sends; the API is lenient but we mirror it.
USER_AGENT = os.environ.get("HATCH_USER_AGENT", "hatch_rest_api")

# Products the app asks about (from DeviceService.fetchIoTDevices).
IOT_PRODUCTS = [
    "restMini",
    "restPlus",
    "riot",
    "riotPlus",
    "restBaby",
    "restoreIot",
    "restoreV4",
    "restoreV5",
]

# Where a fetched token is cached between runs.
TOKEN_CACHE = Path(os.environ.get("HATCH_TOKEN_CACHE", Path.home() / ".cache" / "hatch" / "token.json"))


class HatchError(RuntimeError):
    pass


class AuthError(HatchError):
    pass


class HatchClient:
    def __init__(self, token: str | None = None, base_url: str = BASE_URL, timeout: int = 30):
        self.base_url = base_url.rstrip("/") + "/"
        self.timeout = timeout
        self.token = token
        self.session = requests.Session()
        self.session.headers["User-Agent"] = USER_AGENT

    # ---- low level ---------------------------------------------------------
    def _headers(self) -> dict:
        h = {}
        if self.token:
            h["X-HatchBaby-Auth"] = self.token
        return h

    def _check(self, resp: requests.Response) -> dict:
        if resp.status_code == 429:
            raise HatchError("Rate limited (HTTP 429). Wait a bit and retry.")
        try:
            body = resp.json()
        except ValueError:
            raise HatchError(f"Non-JSON response (HTTP {resp.status_code}): {resp.text[:300]}")
        # Service envelope. Login does not carry "status" but has "token".
        if body.get("errorCode") == 1001:
            raise AuthError("Session invalid/expired (errorCode 1001). Re-login.")
        if "status" in body and body["status"] != "success":
            raise HatchError(f"API error: {body.get('message')} (status={body.get('status')})")
        resp.raise_for_status()
        return body

    def get(self, path: str, params: dict | None = None) -> dict:
        resp = self.session.get(
            self.base_url + path.lstrip("/"),
            params=params,
            headers=self._headers(),
            timeout=self.timeout,
        )
        return self._check(resp)

    def post(self, path: str, json_body: dict) -> dict:
        resp = self.session.post(
            self.base_url + path.lstrip("/"),
            json=json_body,
            headers=self._headers(),
            timeout=self.timeout,
        )
        return self._check(resp)

    # ---- auth --------------------------------------------------------------
    def login(self, email: str, password: str) -> str:
        """POST /public/v1/login -> token (also stored on the client)."""
        body = self.post("public/v1/login", {"email": email, "password": password})
        token = body.get("token")
        if not token:
            raise AuthError(f"Login returned no token: {json.dumps(body)[:300]}")
        self.token = token
        return token

    # ---- device / member --------------------------------------------------
    def member(self) -> dict:
        return self.get("service/app/v2/member")["payload"]

    def iot_devices(self, products: list[str] | None = None) -> list[dict]:
        """GET /service/app/iotDevice/v2/fetch -> list of device dicts."""
        params = {"iotProducts": products or IOT_PRODUCTS}
        return self.get("service/app/iotDevice/v2/fetch", params=params)["payload"]

    # ---- firmware ----------------------------------------------------------
    def check_firmware(
        self,
        product: str,
        mac_address: str,
        current_app_version: str = "",
        current_firmware_version: str = "",
        current_hardware_version: str = "",
        mrb: bool = False,
        os_name: str = "Android",
    ) -> dict:
        """
        GET /service/app/firmwareVersion/v1/check

        Argument order mirrors DeviceService.checkFirmwareVersion(
            os, product, macAddress,
            currentAppVersion, currentFirmwareVersion, currentHardwareVersion, mrb)

        The first param's query key is 'os' (value "Android"/"iOS"), not
        "platform" — the server rejects the request without it.

        Returns the raw payload, which deserializes into FirmwareVersionApi:
            { url, <bool>, <bool>, version }   # 'url' is the firmware image URL
        Passing empty/low current versions makes the server report the latest
        available image regardless of what the device currently runs.
        """
        params = {
            "os": os_name,
            "product": product,
            "macAddress": mac_address,
            "currentAppVersion": current_app_version,
            "currentFirmwareVersion": current_firmware_version,
            "currentHardwareVersion": current_hardware_version,
            "mrb": str(bool(mrb)).lower(),
        }
        body = self.get("service/app/firmwareVersion/v1/check", params=params)
        # Service envelope wraps it in payload; be tolerant if it doesn't.
        return body.get("payload", body)


# ---- token cache helpers ---------------------------------------------------
def save_token(token: str, email: str | None = None) -> None:
    TOKEN_CACHE.parent.mkdir(parents=True, exist_ok=True)
    TOKEN_CACHE.write_text(json.dumps({"token": token, "email": email, "ts": int(time.time())}))
    try:
        TOKEN_CACHE.chmod(0o600)
    except OSError:
        pass


def load_token() -> str | None:
    try:
        return json.loads(TOKEN_CACHE.read_text()).get("token")
    except (OSError, ValueError):
        return None
