#!/usr/bin/env python3
"""Run on the Synology NAS. Read local capacities/network and SNMP health;
POST full snapshots to the ESP. No Home Assistant or third-party Python packages.
"""
import argparse
import fcntl
import json
import math
import os
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

from snmp_reader import Client, SnmpError

RAID_TABLE = "1.3.6.1.4.1.6574.3.1.1"
DISK_TABLE = "1.3.6.1.4.1.6574.2.1.1"
RAID_STATUS = {1: "normal", 2: "repairing", 3: "migrating", 4: "expanding", 5: "deleting", 6: "creating", 7: "RaidSyncing", 8: "RaidParityChecking", 9: "RaidAssembling", 10: "canceling", 11: "degraded", 12: "crashed / read-only", 13: "DataScrubbing", 14: "RaidDeploying", 15: "RaidUnDeploying", 16: "RaidMountCache", 17: "RaidUnmountCache", 18: "expansion interrupted", 19: "RaidConvertSHRToPool", 20: "RaidMigrateSHR1ToSHR2", 21: "unknown storage status"}
RAID_FAULTS = {11, 12, 18, 21}
DISK_STATUS = {1: "normal", 2: "initialized", 3: "not initialized", 4: "system partition failed", 5: "crashed", 6: "disconnected"}
DISK_HEALTH = {1: "normal", 2: "health warning", 3: "critical", 4: "failing"}


def clean(text):
    return str(text).replace("|", "/").encode("ascii", "replace").decode().strip()[:120]


def rows(table):
    result = {}
    for suffix, value in table.items():
        if len(suffix) == 2:
            column, index = suffix
            result.setdefault(index, {})[column] = value
    return result


def storage_health(raid_table, disk_table, config):
    raids, disks = rows(raid_table), rows(disk_table)
    warnings, temperatures = [], []
    statuses = ["unknown", "unknown"]
    if not raids:
        warnings.append("Storage health unavailable: no RAID table")
    if not disks:
        warnings.append("Drive health unavailable: no disk table")
    names_seen = set()
    for index, raid in sorted(raids.items()):
        name = clean(raid.get(2, "Storage item %s" % index))
        status = raid.get(3)
        label = RAID_STATUS.get(status, "status unavailable" if status is None else "unknown status %s" % status)
        normalized = re.sub(r"[^a-z0-9]", "", name.lower())
        for volume in (1, 2):
            wanted = re.sub(r"[^a-z0-9]", "", str(config.get("volume%d_raid_name" % volume, "volume%d" % volume)).lower())
            if normalized == wanted:
                statuses[volume - 1] = label
                names_seen.add(volume)
        if status in RAID_FAULTS or status not in RAID_STATUS:
            warnings.append("%s: %s" % (name, label))
    for volume in (1, 2):
        if volume not in names_seen:
            warnings.append("Volume %d: health entry missing" % volume)
    has_health_column = any(13 in disk for disk in disks.values())
    expected = {str(name).strip().lower() for name in config.get("expected_drive_names", [])}
    actual = set()
    for index, disk in sorted(disks.items()):
        name = clean(disk.get(12) or disk.get(2) or "Drive %s" % index)
        actual.add(name.lower())
        deployment, health = disk.get(5), disk.get(13)
        # Initialized/uninitialized hot spares and unassigned disks are not
        # failed disks. Unknown codes and failed data-drive status are alerts.
        if deployment not in (1, 2, 3):
            warnings.append("%s: %s" % (name, DISK_STATUS.get(deployment, "status unavailable")))
        if health not in (None, 1):
            warnings.append("%s: %s" % (name, DISK_HEALTH.get(health, "unknown health status")))
        if has_health_column and health is None:
            warnings.append("%s: health unavailable" % name)
        if deployment in (2, 3) and disk.get(7) == "data":
            warnings.append("%s: %s" % (name, DISK_STATUS[deployment]))
        if disk.get(7) == "unknown":
            warnings.append("%s: storage role unknown" % name)
        for column, description in ((9, "bad sectors"), (10, "drive identification failures")):
            if isinstance(disk.get(column), int) and disk[column] > 0:
                warnings.append("%s: %d %s reported" % (name, disk[column], description))
        value = disk.get(6)
        if isinstance(value, (int, float)) and math.isfinite(value) and 0 < value <= 125:
            temperatures.append(float(value))
    for missing in sorted(expected - actual):
        warnings.append("%s: drive missing" % clean(missing))
    if config.get("require_disk_health", True) and disks and not has_health_column:
        warnings.append("Drive health not exposed: DSM 7.1+ required")
    return statuses, temperatures, list(dict.fromkeys(warnings))


def filesystem_capacity(path):
    # Synology volumes are mount points. Do not mistake a missing mount for
    # a healthy directory on the root filesystem.
    if not os.path.ismount(path):
        raise OSError("Volume is not mounted")
    values = os.statvfs(path)
    if values.f_flag & getattr(os, "ST_RDONLY", 1):
        raise OSError("read-only filesystem")
    total = values.f_blocks * values.f_frsize
    used = (values.f_blocks - values.f_bfree) * values.f_frsize
    if total <= 0 or used < 0 or used > total:
        raise OSError("Invalid filesystem capacity")
    # Decimal TB and MB/s, matching the labels on the display.
    return used / 1e12, total / 1e12


def default_interface(route_path="/proc/net/route"):
    for line in Path(route_path).read_text().splitlines()[1:]:
        fields = line.split()
        if len(fields) > 3 and fields[1] == "00000000" and int(fields[3], 16) & 1:
            return fields[0]
    raise OSError("Set network_interfaces in sender-config.json; no default interface found")


def network_counters(interfaces, path="/proc/net/dev"):
    values = {}
    for line in Path(path).read_text().splitlines():
        if ":" not in line:
            continue
        name, numbers = line.split(":", 1)
        counters = numbers.split()
        if len(counters) >= 16:
            values[name.strip()] = (int(counters[0]), int(counters[8]))
    result = {}
    for interface in interfaces:
        if interface not in values:
            raise OSError("Network interface %s unavailable" % interface)
        result[interface] = values[interface]
    return result


def network_rate(before, after, elapsed):
    if elapsed <= 0 or before.keys() != after.keys():
        return None
    rx = tx = 0
    for interface in before:
        old_rx, old_tx = before[interface]
        new_rx, new_tx = after[interface]
        if new_rx < old_rx or new_tx < old_tx:
            return None  # Reboot/interface reset; don't invent a traffic spike.
        rx += new_rx - old_rx
        tx += new_tx - old_tx
    return rx / elapsed / 1e6, tx / elapsed / 1e6


def snapshot(config, client, stats_fn=filesystem_capacity):
    payload = {"nasName": clean(config.get("nas_name", "SYNOLOGY")), "rxMBps": "", "txMBps": "", "driveTemps": "", "storageWarnings": ""}
    warnings = []
    try:
        statuses, temperatures, health_warnings = storage_health(client.walk(RAID_TABLE), client.walk(DISK_TABLE), config)
        warnings.extend(health_warnings)
        payload["driveTemps"] = ",".join("%.2f" % value for value in temperatures)
    except (OSError, ValueError, SnmpError):
        statuses = ["unknown", "unknown"]
        warnings.append("Storage monitoring unavailable - check local SNMP")
    for volume in (1, 2):
        prefix = "volume%d" % volume
        payload[prefix + "Status"] = statuses[volume - 1]
        payload[prefix + "UsedTB"] = payload[prefix + "TotalTB"] = ""
        try:
            used, total = stats_fn(config.get("volume%d_path" % volume, "/volume%d" % volume))
            payload[prefix + "UsedTB"] = "%.6f" % used
            payload[prefix + "TotalTB"] = "%.6f" % total
        except OSError as error:
            warnings.append("Volume %d: %s" % (volume, clean(error)))
    payload["storageWarnings"] = warning_text(warnings)
    return payload


def warning_text(warnings):
    warnings = list(dict.fromkeys(clean(warning) for warning in warnings if warning))
    text = "|".join(warnings)
    if len(text) <= 1024:
        return text
    # Keep complete reasons, flag that additional faults exist. The web state
    # and console keep diagnosis visible instead of silently cutting a sentence.
    selected = []
    for warning in warnings:
        if len("|".join(selected + [warning, "More faults: inspect DSM Storage Manager"])) > 1024:
            break
        selected.append(warning)
    return "|".join(selected + ["More faults: inspect DSM Storage Manager"])


def add_network(payload, config, sample_seconds=2):
    try:
        interfaces = config.get("network_interfaces") or [default_interface()]
        before = network_counters(interfaces)
        started = time.monotonic()
        time.sleep(sample_seconds)
        after = network_counters(interfaces)
        rates = network_rate(before, after, time.monotonic() - started)
        if rates is None:
            raise OSError("Counters reset")
        payload["rxMBps"], payload["txMBps"] = ("%.4f" % rate for rate in rates)
    except (OSError, ValueError):
        existing = payload["storageWarnings"].split("|") if payload["storageWarnings"] else []
        payload["storageWarnings"] = warning_text(existing + ["Network readings unavailable"])


def send(config, payload):
    display_url = config.get("display_url", "").strip()
    if not display_url:
        raise ValueError("Set display_url in sender-config.json")
    url = display_url.rstrip("/") + "/nas"
    request = urllib.request.Request(url, data=urllib.parse.urlencode(payload).encode(), headers={"Content-Type": "application/x-www-form-urlencoded"}, method="POST")
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=Path(__file__).with_name("sender-config.json"))
    parser.add_argument("--once", action="store_true", help="Send once, for DSM's every-minute scheduled task")
    parser.add_argument("--inspect", action="store_true", help="Print local storage table/name readings without sending to ESP")
    parser.add_argument("--learn-drives", action="store_true", help="Save the current healthy drive inventory for missing-drive detection")
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    community = os.environ.get("NAS_SNMP_COMMUNITY") or config.get("snmp_community", "")
    if not community or community == "CHANGE_ME":
        parser.error("Set snmp_community in the NAS-local configuration")
    if config.get("snmp_host", "127.0.0.1") not in ("127.0.0.1", "localhost", "::1"):
        parser.error("This sender runs on the NAS and queries local SNMP only")
    client = Client(config.get("snmp_host", "127.0.0.1"), community, config.get("snmp_port", 161))
    interval = max(10, float(config.get("interval_seconds", 30)))
    # Avoid overlapping DSM Task Scheduler jobs without requiring root.
    lock_path = args.config.with_suffix(".lock")
    with lock_path.open("a") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return 0
        if args.learn_drives:
            raids, disks = client.walk(RAID_TABLE), client.walk(DISK_TABLE)
            _, _, warnings = storage_health(raids, disks, {**config, "expected_drive_names": []})
            if warnings:
                parser.error("Resolve storage/monitoring warnings before learning drives: " + "; ".join(warnings))
            config["expected_drive_names"] = [clean(disk.get(12) or disk.get(2) or "Drive %s" % index) for index, disk in sorted(rows(disks).items())]
            args.config.write_text(json.dumps(config, indent=2) + "\n")
            print("Saved %d expected drive names; missing drives will trigger warnings." % len(config["expected_drive_names"]))
            return 0
        if not config.get("expected_drive_names") and not args.inspect:
            parser.error("Run --inspect, then --learn-drives once while the NAS is healthy before starting the sender")
        if args.inspect:
            output = {"raidRows": rows(client.walk(RAID_TABLE)), "diskRows": rows(client.walk(DISK_TABLE)), "defaultInterface": default_interface()}
            print(json.dumps(output, indent=2)); return 0
        while True:
            started = time.monotonic()
            payload = snapshot(config, client)
            add_network(payload, config)
            try:
                response = send(config, payload)
                print("NAS display updated; %s" % (response.get("warnings") or "storage healthy"), flush=True)
            except (OSError, ValueError) as error:
                print("Display upload failed: %s" % error, file=sys.stderr, flush=True)
                if args.once:
                    return 1
            if args.once:
                return 0
            time.sleep(max(1, interval - (time.monotonic() - started)))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
