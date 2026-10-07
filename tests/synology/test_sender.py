import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "senders/synology"))
import snmp_reader as snmp
import synology_sync as sync


def table(rows):
    return {(column, index): value for index, row in rows.items() for column, value in row.items()}


RAIDS = table({0: {2: "volume1", 3: 1}, 1: {2: "volume2", 3: 1}, 2: {2: "Storage Pool 1", 3: 1}})
DISKS = table({0: {2: "Disk 1", 12: "sata1", 5: 1, 6: 38, 7: "data", 13: 1}, 1: {2: "Disk 2", 12: "sata2", 5: 1, 6: 36, 7: "data", 13: 1}})
CONFIG = {"expected_drive_names": ["sata1", "sata2"]}


class SenderTests(unittest.TestCase):
    def test_healthy_and_temperature_individual_drives(self):
        status, temps, warnings = sync.storage_health(RAIDS, DISKS, CONFIG)
        self.assertEqual(status, ["normal", "normal"])
        self.assertEqual(temps, [38, 36])
        self.assertFalse(warnings)

    def test_pool_fault_even_when_both_volumes_normal(self):
        raids = dict(RAIDS, **{})
        raids[(3, 2)] = 11
        status, _, warnings = sync.storage_health(raids, DISKS, CONFIG)
        self.assertEqual(status, ["normal", "normal"])
        self.assertIn("Storage Pool 1: degraded", warnings)

    def test_failed_disk_and_health_both_visible(self):
        disks = dict(DISKS)
        disks[(5, 0)], disks[(13, 0)] = 5, 4
        _, _, warnings = sync.storage_health(RAIDS, disks, CONFIG)
        self.assertIn("sata1: crashed", warnings)
        self.assertIn("sata1: failing", warnings)

    def test_missing_drive_is_not_a_healthy_snapshot(self):
        disks = {key: value for key, value in DISKS.items() if key[1] == 0}
        self.assertIn("sata2: drive missing", sync.storage_health(RAIDS, disks, CONFIG)[2])

    def test_missing_volume_table_and_unknown_status(self):
        raids = {key: value for key, value in RAIDS.items() if key[1] != 1}
        raids[(3, 0)] = 98
        warnings = sync.storage_health(raids, DISKS, CONFIG)[2]
        self.assertIn("Volume 2: health entry missing", warnings)
        self.assertIn("volume1: unknown status 98", warnings)

    def test_maintenance_is_not_a_failure(self):
        raids = dict(RAIDS)
        for status in [2, 7, 8, 13, 20]:
            raids[(3, 0)] = status
            self.assertFalse(sync.storage_health(raids, DISKS, CONFIG)[2])
        raids[(3, 0)] = 18
        self.assertTrue(sync.storage_health(raids, DISKS, CONFIG)[2])

    def test_temperature_zero_missing_and_bad_sectors(self):
        disks = dict(DISKS)
        disks[(6, 0)] = 0
        disks[(9, 1)] = 4
        _, temps, warnings = sync.storage_health(RAIDS, disks, CONFIG)
        self.assertEqual(temps, [36])
        self.assertIn("sata2: 4 bad sectors reported", warnings)

    def test_missing_health_oid_is_monitoring_warning(self):
        disks = {key: value for key, value in DISKS.items() if key[0] != 13}
        self.assertTrue(sync.storage_health(RAIDS, disks, CONFIG)[2])

    def test_network_units_and_counter_reset(self):
        before = {"bond0": (100, 200)}
        after = {"bond0": (24800100, 3600200)}
        self.assertEqual(sync.network_rate(before, after, 2), (12.4, 1.8))
        self.assertIsNone(sync.network_rate(after, before, 2))

    def test_network_interface_selects_one_avoiding_bond_double_count(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "dev"
            path.write_text("bond0: 100 0 0 0 0 0 0 0 200 0 0 0 0 0 0 0\neth0: 100 0 0 0 0 0 0 0 200 0 0 0 0 0 0 0\n")
            self.assertEqual(sync.network_counters(["bond0"], path), {"bond0": (100, 200)})

    def test_snapshot_snmp_failure_still_sends_warning(self):
        class Failed:
            def walk(self, _):
                raise snmp.SnmpError("timeout")
        payload = sync.snapshot(CONFIG, Failed(), lambda _: (1, 4))
        self.assertIn("Storage monitoring unavailable", payload["storageWarnings"])
        self.assertEqual(payload["volume1Status"], "unknown")
        self.assertEqual(payload["volume2TotalTB"], "4.000000")

    def test_readonly_volume_reports_reason(self):
        class Good:
            def walk(self, oid):
                return RAIDS if oid == sync.RAID_TABLE else DISKS
        def stats(path):
            if path == "/volume2":
                raise OSError("read-only filesystem")
            return 1, 4
        payload = sync.snapshot(CONFIG, Good(), stats)
        self.assertIn("Volume 2: read-only filesystem", payload["storageWarnings"])
        self.assertEqual(payload["volume2TotalTB"], "")

    def test_many_warnings_preserve_reasons_and_explicit_overflow(self):
        text = sync.warning_text(["Fault %d: %s" % (i, "x" * 80) for i in range(40)])
        self.assertLessEqual(len(text), 1024)
        self.assertIn("More faults: inspect DSM", text)
        self.assertIn("Fault 0:", text)


class SnmpTests(unittest.TestCase):
    def test_oid_multibyte_first_subidentifier(self):
        for oid in ["1.3.6.1.4.1.6574.2.1.1.13.0", "2.999.123456.0"]:
            _, raw, _ = snmp.read_tlv(snmp.encode_oid(oid))
            self.assertEqual(snmp.decode_oid(raw), tuple(map(int, oid.split("."))))

    def test_signed_and_counter64_response(self):
        oid = "1.3.6.1.4.1.6574.3.1.1.5.0"
        bindings = snmp.tlv(0x30, snmp.encode_oid(oid) + snmp.tlv(0x46, (9 * 10**12).to_bytes(8, "big")))
        pdu = snmp.tlv(0xA2, snmp.integer(123) + snmp.integer(0) + snmp.integer(0) + snmp.tlv(0x30, bindings))
        response = snmp.tlv(0x30, snmp.integer(1) + snmp.tlv(4, b"example") + pdu)
        self.assertEqual(snmp.decode_response(response, 123, b"example")[0][1], 9 * 10**12)
        with self.assertRaises(snmp.SnmpError):
            snmp.decode_response(response, 124, b"example")
        with self.assertRaises(snmp.SnmpError):
            snmp.decode_response(response, 123, b"wrong")

    def test_truncated_length_is_rejected(self):
        for raw in [b"", b"\x30\x82\x01", b"\x30\x05abc", b"\x30\x80"]:
            with self.assertRaises(snmp.SnmpError):
                snmp.read_tlv(raw)

    def test_nonadvancing_walk_is_rejected(self):
        client = snmp.Client("127.0.0.1", "example")
        with patch.object(client, "request", return_value=((1, 3, 6), 1)):
            with self.assertRaises(snmp.SnmpError):
                client.walk("1.3.6")


if __name__ == "__main__":
    unittest.main()
