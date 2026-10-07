import importlib.util
from pathlib import Path
import sys
import unittest

path = Path(__file__).resolve().parents[2] / "senders/ai_limits/ai_limits_sync.py"
spec = importlib.util.spec_from_file_location("limits_sync", path)
sync = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = sync
spec.loader.exec_module(sync)


class LimitsTests(unittest.TestCase):
    def test_used_becomes_remaining(self):
        metric = sync.build_metric(28, None, 300)
        self.assertEqual(metric.left_percent, 72)
        self.assertEqual(metric.text, "72% left")

    def test_invalid_numbers_do_not_become_fake_usage(self):
        for value in [None, "unknown", float("nan"), float("inf")]:
            self.assertIsNone(sync.clamp_percent(value))

    def test_percent_bounds(self):
        self.assertEqual(sync.clamp_percent(-20), 0)
        self.assertEqual(sync.clamp_percent(140), 100)

    def test_missing_provider_is_waiting(self):
        fields = sync.build_provider_fields("codex", None)
        self.assertEqual(fields["codexDailyText"], "waiting")
        self.assertEqual(fields["codexWeeklyPercent"], "0")

    def test_explicit_weekly_text(self):
        slot, metric = sync.classify_claude_text_metric("Current week: 40% used")
        self.assertEqual(slot, "weekly")
        self.assertEqual(metric.left_percent, 60)

    def test_unrelated_transcript_text_not_a_usage_reading(self):
        slot, metric = sync.classify_claude_text_metric("Build completed successfully")
        self.assertIsNone(slot)
        self.assertIsNone(metric)


if __name__ == "__main__":
    unittest.main()
