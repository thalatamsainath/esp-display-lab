# Security and private configuration

The sample firmware is intended for a trusted local network. Telemetry, status, brightness and setup pages use HTTP. Firmware updates require Basic Authentication, which does not encrypt traffic. Do not forward these ports to the internet.

Updater credentials are generated locally into ignored `ota_credentials.h` files. They are embedded in every resulting firmware image. Publishing that header or a personalized binary reveals the updater password. Keep actual NAS SNMP community names in ignored local configuration; the NAS sender queries loopback and does not require a DSM administrator password.

Before sharing diagnostics, replace hostnames, device IPs, local user paths and session/provider data with fictional values. The public-tree script flags common patterns, but manual review is still required. No personal logs or photographs are included in the sample showcase.

If a credential is exposed, remove it from the public material and history where appropriate, rotate it on the running device/service, and rebuild affected firmware. Removing a file alone does not revoke a credential.

For a discovered issue, describe it without live secrets. If this project is published, use a repository's private vulnerability-reporting channel when one is enabled; do not include sensitive values in a public issue.
