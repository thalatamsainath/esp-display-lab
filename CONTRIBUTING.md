# Contributing

Read [the development guide](docs/developing.md) and [source attribution](ATTRIBUTION.md) before extending the samples. Keep a new dashboard in its own Arduino sketch folder, document its board assumptions and payload, and add a preview using fictional data.

Use small changes with clear behavior: describe the trigger, the resulting screen or web response, and how you checked it. Keep meaningful parsing, arithmetic and state logic testable on a host. Avoid adding a test that merely duplicates a drawing implementation.

Before committing:

```sh
sh scripts/test.sh
sh scripts/build_firmware.sh synology   # if this sample changed
sh scripts/build_firmware.sh trains     # if this sample changed
python3 scripts/check_public_tree.py
git diff --cached --check
git diff --cached
```

Do not commit credentials, actual sender configuration, prompt/session logs, NAS hostnames, private IPs, device photographs with identifying details, personal firmware images or bundled toolchains. Share anonymized field names and synthetic values when diagnosing a parser issue. A `.gitignore` rule does not remove a secret already in history.

Retain source notices and describe external code origins accurately. No project-wide licence is asserted while inherited-code licensing remains unresolved.
