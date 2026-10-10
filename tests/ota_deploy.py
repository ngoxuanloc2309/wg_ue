#!/usr/bin/env python3
"""Deploy the freshly built firmware to the gateway over its own OTA API.

Mirrors the dashboard's XHR contract:
  POST /api/firmware_update
  headers: X-Firmware-Filename, X-Firmware-SHA256, Basic auth
After a 2xx the device marks the slot pending-boot and resets; we poll
/api/status until it returns with a fresh uptime, then report.
"""

import argparse
import hashlib
import sys
import time

import requests


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.1.101")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--password", default="admin")
    ap.add_argument("--bin", default="build/GatewayV2.bin")
    args = ap.parse_args()

    base = f"http://{args.host}"
    image = open(args.bin, "rb").read()
    sha = hashlib.sha256(image).hexdigest()
    print(f"image: {args.bin} ({len(image)} bytes) sha256={sha[:16]}...")

    r = requests.post(
        f"{base}/api/firmware_update",
        data=image,
        headers={
            "Content-Type": "application/octet-stream",
            "X-Firmware-Filename": "GatewayV2.bin",
            "X-Firmware-SHA256": sha,
        },
        auth=(args.user, args.password),
        timeout=120,
    )
    print("upload ->", r.status_code, r.text[:200])
    if r.status_code not in (200, 201, 202):
        return 1

    print("waiting for reboot...")
    deadline = time.time() + 90
    while time.time() < deadline:
        time.sleep(3)
        try:
            st = requests.get(f"{base}/api/status", timeout=5).json()
        except Exception:
            print("  ...device offline (rebooting)")
            continue
        up = int(st.get("uptime", 9999))
        if up <= 45:
            print(f"  back online: uptime={up}s state={st.get('state')}")
            # give ota_health_task (~15 s after boot) a moment to confirm
            time.sleep(16)
            try:
                st = requests.get(f"{base}/api/status", timeout=5).json()
                print(f"  post-health: state={st.get('state')} uptime={st.get('uptime')}")
            except Exception:
                pass
            print("OTA DEPLOY OK")
            return 0
        print(f"  ...old image still up (uptime={up}s), waiting for reset")

    print("OTA DEPLOY TIMEOUT: device did not come back with fresh uptime")
    return 1


if __name__ == "__main__":
    sys.exit(main())
