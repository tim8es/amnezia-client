# Minimal on-demand ICMP latency checks

The existing server rows retain their navigation and show a small refresh action
and optional `ICMP: N ms` text. The existing subscription country picker gains
the same action for known locations, plus a bulk check in its toolbar.

Only known addresses are probed:
- Standalone servers: `ServersModel` hostname.
- Subscription: the current selected location's hostname, or `hostName`
  explicitly supplied for a country in the read-only catalog.
  Other countries display `Address unavailable`. No new config is requested or
  issued merely to discover endpoints, avoiding unexpected key rotation.

One ICMP echo per host, at most three parallel processes, with a hard timeout.
An echo reply is not proof that the VPN protocol works; an ICMP timeout is not
proof that the VPN is offline. Desktop only.

`ServerPingControllerTest` covers target validation, localized output parsing
and non-leakage of the current endpoint to other subscription countries.
Actual catalog endpoint coverage and network behavior need device testing.
