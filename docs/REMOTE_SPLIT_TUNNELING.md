# Remote split-tunneling lists (experimental personal build)

This patch ports the subscription/update concept from
[tim8es/amnezia-vpn-tunnel-update](https://github.com/tim8es/amnezia-vpn-tunnel-update)
to the Amnezia VPN UI. The upstream Amnezia application is GPL-3.0; keep its
copyright notices and source distribution obligations. The source utility is MIT.

## Behavior

- Opt-in: no subscriptions or network requests until the user clicks **Update now**
  or enables **Automatic updates**.
- Supports the three `lib4u/amnezia-tunneling-ru` release JSON files and a custom
  HTTPS URL with the same JSON schema.
- Auto-checks on application start and every six hours while the application runs.
  There is **no system scheduler when the application is closed**.
- Validates JSON, caps payloads at 8 MiB and 100,000 entries, accepts valid IPv4/CIDR
  and hostnames, checks SHA-256 and HTTP ETag.
- Refuses suspicious >50% shrinkages from the *same source*, as well as empty or
  invalid sources. Explicit switching between presets is allowed.
- Merges `existing - previously managed + downloaded`, keeping unmanaged local
  entries. If a downloaded hostname collides with an unmanaged local hostname,
  the local entry wins and is not claimed by the subscription. Domain-only
  records retain previously resolved IPs for continued managed hostnames.
  Ownership is tracked per hostname, not per IP.
- Each VPN routing mode keeps independent provenance and managed-host lists.
- No live route changes: if connected or connecting, the downloaded update remains
  pending in memory until a user disconnects and presses **Update now**.
  The **Skip** action remembers the ignored SHA-256 per source/mode and offers an
  update again only when that source publishes new content.
- Before every apply, atomically saves the previous rules as
  `remote-split-backup-mode-{1,2}.json` in the app's local data directory. The latest
  backup for the mode replaces the previous backup. Subscription metadata uses
  `remote-split-tunneling.ini` in the same directory.
- No automatic VPN restart or client exit.

## Manual verification before personal use

1. Build against the same `dev` revision as the patch and install in a test user
   profile. Start with an empty test list.
2. Test each preset and a custom HTTPS JSON. Invalid URL, malformed/empty JSON,
   404, timeout, HTTP 304 and >8 MiB downloads must not delete existing rules.
3. Add a local-only site, update the source twice, and verify it survives. Switch
   presets; confirm stale managed sites are removed and local sites remain.
4. Test **Via VPN** and **Directly** separately; check that changes to one mode do
   not affect the other.
5. While the VPN is connected, trigger an update. It must stay pending; manually
   disconnect and press **Update now**, then reconnect.
6. Disable automatic updates and restart. No network request should occur until
   **Update now** is clicked.
7. Test Windows, macOS and Linux. Do not ship before their build/test pipelines pass.

## Known limitations / remaining upstream work

- No background updates while Amnezia VPN is closed. The separate helper can still
  provide that behavior independently if explicitly installed.
- Local edits to IPs for a hostname owned by the subscription can be overwritten
  on the next update. Consider a per-host override model before upstream review.
- There is a small non-atomic window between writing native VPN settings and
  subscription provenance. A backup is preserved for manual recovery, but the
  process might be interrupted before provenance is saved.
- Run the Qt Test target `test_remote_split_tunneling_lists` and the existing
  IP split-tunneling tests. Only release after platform CI and manual VPN checks.

## Verification

The deterministic JSON parsing and merge tests are registered in CTest as
`RemoteSplitTunnelingListsTest`. Platform packaging and end-to-end VPN behavior
still require CI and manual validation before release.

## Source attribution

Ported design and validation rules from the MIT-licensed
`tim8es/amnezia-vpn-tunnel-update` project (Copyright © 2026 tim8es).
List data belongs to its respective upstream sources.