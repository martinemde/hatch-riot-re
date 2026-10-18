#!/usr/bin/env python3
"""
Get a Hatch API auth token.

Usage:
    # credentials from env (recommended)
    export HATCH_EMAIL=you@example.com
    export HATCH_PASSWORD='...'
    ./hatch_auth.py

    # or prompt interactively
    ./hatch_auth.py --email you@example.com

    # print just the token (for scripting)
    ./hatch_auth.py --quiet

The token is cached at ~/.cache/hatch/token.json (override: HATCH_TOKEN_CACHE)
and reused by hatch_firmware.py. Tokens are account-scoped; treat them like a
password.
"""

from __future__ import annotations

import argparse
import getpass
import json
import os
import sys

from hatch_api import AuthError, HatchClient, HatchError, save_token


def main() -> int:
    ap = argparse.ArgumentParser(description="Obtain a Hatch API auth token.")
    ap.add_argument("--email", default=os.environ.get("HATCH_EMAIL"))
    ap.add_argument("--password", default=os.environ.get("HATCH_PASSWORD"))
    ap.add_argument("--quiet", action="store_true", help="print only the token")
    ap.add_argument("--no-cache", action="store_true", help="do not write the token cache")
    args = ap.parse_args()

    email = args.email or input("Hatch email: ").strip()
    password = args.password or getpass.getpass("Hatch password: ")

    client = HatchClient()
    try:
        token = client.login(email, password)
    except AuthError as e:
        print(f"Authentication failed: {e}", file=sys.stderr)
        return 2
    except HatchError as e:
        print(f"API error: {e}", file=sys.stderr)
        return 1

    if not args.no_cache:
        save_token(token, email)

    if args.quiet:
        print(token)
    else:
        print("Login OK.")
        print(f"Token: {token}")
        if not args.no_cache:
            print("Cached for hatch_firmware.py.")
        # Friendly sanity check: list the account's devices.
        try:
            devices = client.iot_devices()
            print(f"\nDevices on this account ({len(devices)}):")
            for d in devices:
                print(f"  - {d.get('name')!r}  product={d.get('product')}  mac={d.get('macAddress')}")
        except HatchError as e:
            print(f"(could not list devices: {e})", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
