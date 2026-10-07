# AI usage limits display

The sample shows two cards, Codex and Claude, each with a short-window slot and a weekly slot. Bars represent **percent remaining**. The protocol keeps the inherited `Daily` field names for compatibility; the UI labels the first slot `Window` because it is not necessarily a calendar day.

## Build and install

```sh
python3 scripts/generate_ota_credentials.py limits
sh scripts/build_firmware.sh limits
```

Upload `build/limits/SDP_limits_ota.bin` using the current firmware's updater. Configure Wi-Fi through the fallback AP if needed. The root page has a manual test form and `/update` link; `GET /state` returns the current metrics.

## Demo without provider access

After installing the limits sample, set the display URL and submit a fictional payload:

```sh
DISPLAY_URL=http://192.168.1.50
curl --fail --data-urlencode updatedAt=Demo \
  --data-urlencode 'codexDailyText=72% left' --data-urlencode codexDailyPercent=72 \
  --data-urlencode 'codexWeeklyText=58% left' --data-urlencode codexWeeklyPercent=58 \
  --data-urlencode 'claudeDailyText=35% left' --data-urlencode claudeDailyPercent=35 \
  --data-urlencode 'claudeWeeklyText=61% left' --data-urlencode claudeWeeklyPercent=61 \
  "$DISPLAY_URL/limits"
```

The equivalent JSON-shaped form fields are in [examples/limits-demo.json](../examples/limits-demo.json). Convert JSON to a form upload; the device does not accept a JSON request body. Omitted metric fields retain prior values, valid percentages are integers 0–100, and text is limited by the 24-byte metric buffer.

## Optional local-session sender

Python 3.9+ and the standard library are sufficient. Copy the template and edit your local endpoint:

```sh
cp senders/ai_limits/config.json.example senders/ai_limits/config.json
python3 senders/ai_limits/ai_limits_sync.py push --config senders/ai_limits/config.json --dry-run
python3 senders/ai_limits/ai_limits_sync.py push --config senders/ai_limits/config.json
```

`esp_url` includes `/limits`. Run the one-shot command periodically with your operating system's scheduler if desired. It reads local Codex session records from `~/.codex/sessions` and Claude transcripts from `~/.claude/projects` by default. Optional `codex_sessions_dir` and `claude_projects_dir` keys override these paths in your ignored local configuration.

Codex data comes from the newest recorded limit event. Claude percentages/reset labels are inferred from transcript text and may be absent or ambiguous. The sender does not sign in to provider accounts, query an authoritative account API, or guarantee current limits. Local log formats can change. Missing data displays `waiting`; old data can remain stale, and this sample has no automatic stale-data warning. Check the provider's own UI before making a usage decision.

`show` prints diagnostics including local file paths, while `push --dry-run` prints the proposed endpoint and small metric payload. Review these locally. Never add actual session records, diagnostic output or personal paths to the repository.
