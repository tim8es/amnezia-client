# On-demand ICMP latency

Small ping actions reuse the existing Servers and subscription country
screens. Results appear inline. No change to the VPN daemon, key material,
routing, or connection configuration.

The client knows the hostname of each installed server. For a subscription
it knows only the currently selected country server's endpoint. Other
countries currently show **Address unavailable** when checked: the country
catalog does not provide verified read-only network targets, and the client
must **not** issue new configurations or silently switch countries to obtain
them. Bulk country ping checks only discoverable endpoints. Full per-country
latency requires a documented read-only source of distinct endpoints.

OS `ping` runs with discrete arguments, validated IPv4/DNS targets, three
concurrent processes and a hard 3.5 s timeout. Results are coalesced for
efficient QML updates. Desktop only; mobile remains unsupported. ICMP
reachability is not a VPN handshake, and no ICMP reply does not prove
a VPN server is offline.

Run `ServerPingControllerTest` and manual UI/locale/network smoke tests
before merging.
