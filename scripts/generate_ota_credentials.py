#!/usr/bin/env python3
"""Generate a local updater password for one sample; never replace an existing one."""
import argparse
import os
from pathlib import Path
import secrets

SKETCHES = {"synology": "esp_synology_display", "limits": "esp_ai_limits"}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sample", choices=SKETCHES)
    args = parser.parse_args()
    path = Path(__file__).resolve().parents[1] / "firmware" / SKETCHES[args.sample] / "ota_credentials.h"
    header = '#pragma once\n#define OTA_USERNAME "admin"\n#define OTA_PASSWORD "' + secrets.token_hex(16) + '"\n'
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise SystemExit("OTA credentials already exist; existing credentials were preserved.")
    with os.fdopen(fd, "w") as output:
        output.write(header)
    print("Created local credentials in %s" % path)
    print("Read that private file for the updater login. Do not publish it or personal firmware builds.")

if __name__ == "__main__":
    main()
