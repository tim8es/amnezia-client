# Remote split-tunneling updates

Use **More → Remote list updates** on the existing Split tunneling page.
Select a curated list or a custom HTTPS JSON URL. Refresh manually or
enable a six-hour check while Amnezia is open (disabled by default).

The network layer downloads no more than 8 MiB over HTTPS. The native
`IpSplitTunnelingController::parseSitesFromJson` parser handles import
and remote lists, with additional strict checks for remote input
(up to 100,000 records). Invalid or empty responses do not change rules.

When importing, the updater merges rules already present with the entries
it owns. Existing user entries win collisions; manually modified IP entries
are preserved. A backup of the current native rules is saved before applying
updates, and the managed list snapshot is stored atomically in
`remote-split-managed-mode-<mode>.json`, independently of UI preferences.
The old INI snapshot format is read once for migration.

Hostnames without IP entries are resolved through the existing Qt DNS API
using a maximum of six concurrent lookups and batch persistence. No routes
are changed while VPN is connected. No automatic VPN reconnect, external
scheduler, or daemon changes.

Known limitation: for managed hostname-only entries, automatically resolved
addresses cannot currently be distinguished from manually edited IPs,
so the ownership check treats both as managed. Use locally defined entries
to avoid overrides of custom IP mappings.

## Verification

`RemoteSplitTunnelingListsTest` covers strict parsing, merging, remote
ownership, domain-only updates, manual IP overrides and local collisions.
Build and run CTest plus a real disconnected/connected VPN smoke test;
review invalid, blocked, malformed and oversized responses before merging.
