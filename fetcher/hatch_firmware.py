#!/usr/bin/env python3
"""
Find and download Hatch device firmware.

For each device on your account (or one you name), this calls the Hatch
firmware-version-check endpoint, prints what the server reports, and — if a
download URL is present — downloads the firmware image.

Flow (recovered from com.hatchbaby.rest 6.40.1):
    1. GET /service/app/iotDevice/v2/fetch                 -> devices (product, mac)
    2. GET /service/app/firmwareVersion/v1/check?...        -> { url, ..., version }
    3. download <url>                                       -> firmware file

Usage:
    ./hatch_auth.py                       # get a token first (cached)
    ./hatch_firmware.py --list            # just list devices + reported firmware
    ./hatch_firmware.py                    # check every device, download any image
    ./hatch_firmware.py --mac AA:BB:CC:DD:EE:FF
    ./hatch_firmware.py --product restoreV5 --mac AA:BB:...
    ./hatch_firmware.py --check-only       # don't download, just show the JSON
    ./hatch_firmware.py --token "$(./hatch_auth.py --quiet)"

Output dir defaults to the repo's firmware/ (git-ignored; override with --out).

Note on current-version args: by default the script sends empty current
firmware/hardware versions so the server returns the latest available image.
Pass --current-fw / --current-hw to ask "is there something newer than X".
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from urllib.parse import urlparse

import requests

from hatch_api import AuthError, HatchClient, HatchError, load_token

# The repo's firmware/ directory, where the analysis tools look for images.
FIRMWARE_DIR = Path(__file__).resolve().parent.parent / "firmware"


def _find_url(obj) -> str | None:
    """Return the first value that looks like a firmware download URL.

    FirmwareVersionApi's first field is `url`, but we scan defensively in case
    the envelope/field names differ, matching any http(s) value whose key
    mentions url/download/location or whose value ends in a firmware-ish ext.
    """
    fw_ext = re.compile(r"\.(zip|bin|hex|dfu|img|ota|fw)(\?|$)", re.I)
    found: list[str] = []

    def walk(node, key=""):
        if isinstance(node, dict):
            for k, v in node.items():
                walk(v, k)
        elif isinstance(node, list):
            for v in node:
                walk(v, key)
        elif isinstance(node, str) and node.startswith(("http://", "https://")):
            keyish = re.search(r"url|download|location|firmware", key, re.I)
            if keyish or fw_ext.search(node):
                found.append(node)

    walk(obj)
    # Prefer a URL that clearly points at a firmware file.
    for u in found:
        if fw_ext.search(u):
            return u
    return found[0] if found else None


def _filename_for(url: str, product: str, mac: str, version: str | None) -> str:
    name = os.path.basename(urlparse(url).path) or "firmware.bin"
    safe_mac = mac.replace(":", "").lower()
    ver = f"_{version}" if version else ""
    # e.g. restoreV5_aabbccddeeff_1.2.3_image.zip
    return f"{product}_{safe_mac}{ver}_{name}"


def download(url: str, dest: Path, timeout: int = 120, verify: bool = True) -> Path:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if not verify:
        # The firmware.hatchbaby.com cert is currently expired; allow opt-out.
        import urllib3
        urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    with requests.get(url, stream=True, timeout=timeout, verify=verify) as r:
        r.raise_for_status()
        total = int(r.headers.get("Content-Length", 0))
        written = 0
        with open(dest, "wb") as f:
            for chunk in r.iter_content(chunk_size=64 * 1024):
                if not chunk:
                    continue
                f.write(chunk)
                written += len(chunk)
                if total:
                    pct = written * 100 // total
                    print(f"\r    downloading {dest.name}: {pct}% ({written}/{total} B)", end="", flush=True)
        print()
    return dest


def process_device(
    client: HatchClient,
    device: dict,
    out_dir: Path,
    current_fw: str,
    current_hw: str,
    current_app: str,
    mrb: bool,
    check_only: bool,
    verify_tls: bool = True,
) -> None:
    product = device.get("product", "")
    mac = device.get("macAddress", "")
    name = device.get("name", "")
    print(f"\n=== {name!r}  product={product}  mac={mac} ===")

    try:
        payload = client.check_firmware(
            product=product,
            mac_address=mac,
            current_app_version=current_app,
            current_firmware_version=current_fw,
            current_hardware_version=current_hw,
            mrb=mrb,
        )
    except HatchError as e:
        print(f"    firmware check failed: {e}", file=sys.stderr)
        return

    print("    server response:")
    print("    " + json.dumps(payload, indent=2).replace("\n", "\n    "))

    url = _find_url(payload)
    version = None
    if isinstance(payload, dict):
        version = payload.get("version") or payload.get("firmwareVersion")

    if not url:
        print("    no firmware download URL in response (device may be up to date).")
        return

    print(f"    firmware URL: {url}")
    if check_only:
        return

    dest = out_dir / _filename_for(url, product, mac, version)
    if dest.exists():
        print(f"    already downloaded: {dest}")
        return
    try:
        download(url, dest, verify=verify_tls)
        print(f"    saved: {dest}  ({dest.stat().st_size} bytes)")
    except requests.RequestException as e:
        print(f"    download failed: {e}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description="Find and download Hatch firmware.")
    ap.add_argument("--token", default=os.environ.get("HATCH_TOKEN") or load_token(),
                    help="auth token (default: HATCH_TOKEN env or cached token from hatch_auth.py)")
    ap.add_argument("--mac", help="only this device MAC address")
    ap.add_argument("--product", help="override/limit product type (e.g. restoreV5)")
    ap.add_argument("--out", default=FIRMWARE_DIR, type=Path, help="download directory (default: the repo's firmware/)")
    ap.add_argument("--list", action="store_true", help="list devices and exit")
    ap.add_argument("--check-only", action="store_true", help="show check response, do not download")
    ap.add_argument("--current-fw", default="", help="current firmware version to report (default: empty -> latest)")
    ap.add_argument("--current-hw", default="", help="current hardware version to report")
    ap.add_argument("--current-app", default="", help="current app version to report")
    ap.add_argument("--mrb", action="store_true", help="set the mrb flag true")
    ap.add_argument("--insecure", action="store_true",
                    help="skip TLS verification when downloading (firmware.hatchbaby.com's cert is expired)")
    args = ap.parse_args()

    if not args.token:
        print("No token. Run ./hatch_auth.py first, or pass --token / set HATCH_TOKEN.", file=sys.stderr)
        return 2

    client = HatchClient(token=args.token)

    try:
        devices = client.iot_devices()
    except AuthError as e:
        print(f"{e}\nRe-run ./hatch_auth.py to refresh the token.", file=sys.stderr)
        return 2
    except HatchError as e:
        print(f"Could not fetch devices: {e}", file=sys.stderr)
        return 1

    if args.mac:
        want = args.mac.lower()
        devices = [d for d in devices if d.get("macAddress", "").lower() == want]
        if not devices:
            print(f"No device with mac {args.mac} on this account.", file=sys.stderr)
            return 1
    if args.product:
        for d in devices:
            d["product"] = args.product  # force the product type for the check

    if args.list:
        print(f"Devices ({len(devices)}):")
        for d in devices:
            print(f"  - {d.get('name')!r}  product={d.get('product')}  mac={d.get('macAddress')}  thing={d.get('thingName')}")
        return 0

    for d in devices:
        process_device(
            client, d, args.out,
            current_fw=args.current_fw,
            current_hw=args.current_hw,
            current_app=args.current_app,
            mrb=args.mrb,
            check_only=args.check_only,
            verify_tls=not args.insecure,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
