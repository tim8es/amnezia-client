# Remote split-tunneling list updates

The existing split-tunneling **More** menu provides **Remote list updates**.
Choose a preset or custom HTTPS JSON source, then refresh manually or enable a
six-hour check while the app is running (disabled by default).

The feature is isolated in `RemoteSplitTunnelingUpdater`. The original
`IpSplitTunnelingController`, manual import, ordinary DNS resolution, routing
and repository behavior remain unchanged. Remote JSON validation enforces an
8 MiB limit, bounded entries and IPv4/CIDR checks before using the existing
`addSites` method to apply the resulting rules.

For hostname-only remote entries, the updater resolves IPv4 addresses
asynchronously with at most eight concurrent lookups, then updates the rules
in one batch. Temporary resolution failures preserve previously managed
addresses when available. Up to 5,000 hostname lookups are accepted per list.

Only previously owned and unchanged entries may be replaced or removed;
local entries and manually modified records are preserved. Existing rules are
backed up before modification. The per-routing-mode list of owned entries is
saved atomically, separately from the small UI preferences. Existing snapshot
format is read for migration.

An update is not applied while VPN is connected or its state changes.
No daemon, platform scheduler, VPN profiles or connection configuration is
modified. Existing native import and add-site behavior is unaffected.

Tests cover parsing, local collisions, owned entry replacement, manual edits,
and conservative handling of old domain-only snapshots. Build and test with
a disconnected/connected test VPN before an upstream merge.
