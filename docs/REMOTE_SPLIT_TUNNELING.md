# Remote split-tunneling list updates

The existing split-tunneling **More** menu now has **Remote list updates**. Select
Domains, IP lite/full or a custom HTTPS URL. Refresh manually or enable a check
every six hours while the app is open (off by default).

The updater reuses `IpSplitTunnelingController::addSites` and the native
route-mode storage. It never modifies routing during a VPN connection.
Before replacing sites it saves a local JSON backup. Entries added or edited
manually are preserved; entries unchanged since the last remote import can be
updated or removed. HTTP response size is limited to 8 MiB and the JSON list
must contain valid entries. Existing local entries win on collisions.

**Limits:** one selected source per routing mode; no background scheduler when
Amnezia is closed. Lists with hostname-only entries may require DNS resolution
by the existing routing stack; verify on a real VPN before relying on them.

## Tests

`RemoteSplitTunnelingListsTest` covers parsing, merging and manual edits.
Test invalid/empty responses, connected state and both routing modes on a
disposable VPN setup before merging into upstream.

Source design: `tim8es/amnezia-vpn-tunnel-update` (MIT); Amnezia is GPL-3.0.
