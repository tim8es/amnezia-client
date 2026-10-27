# On-demand server ICMP checks

The **Servers** page provides a **Ping all** action and a per-server **Ping**
button on desktop platforms. Up to three independent probes run concurrently.
Each server gets one IPv4 ICMP echo with an OS-level timeout; incomplete
processes are killed after 3.5 seconds. No configuration changes, connections,
credentials or background monitoring are involved.

## What the result means

- **ICMP: N ms**: host replied to an ICMP echo, with the RTT parsed from ping.
- **ICMP: reachable**: echo succeeded but localized output had no parseable RTT.
- **No ICMP reply**: echo did not complete successfully; firewall, blocked ICMP,
  DNS, routing or process permissions may be responsible.
- **ICMP: unsupported address**: entry does not contain a suitable IPv4 or DNS
  hostname. IPv6-only addresses are not probed in this first version.

**ICMP is not a VPN handshake.** A server can respond to ping while its VPN
service is unavailable, or successfully serve VPN traffic while blocking ping.
Do not label unresponsive hosts as "offline" or auto-select servers based on
these results. A protocol-specific connectivity/handshake check would be a
different feature.

On Android and iOS, process-based ICMP checks are deliberately disabled.

## Implementation notes

- Uses \`QProcess\` with discrete arguments, not a shell.
- Hostnames are validated before launching a probe (no URL, port, options).
- Results exist in memory only and are cleared on server list model reset.
- One-shot, user-triggered checks only; no telemetry or periodic network traffic.
- Native output parsing supports common English and Russian RTT formats, with
  a safe "reachable" fallback when output cannot be parsed.

## Verification

1. Run \`ServerPingControllerTest\` through CTest (input and output parsing).
2. Exercise Ping / Ping all from the desktop Servers page.
3. Verify a responding host shows RTT and a nonresponding host shows
   "No ICMP reply" (not "VPN offline").
4. Verify DNS errors, invalid target, missing ping executable, and timeouts do
   not freeze the interface.
5. Verify server rename/delete/model refresh clears stale results.
6. Test Windows/macOS/Linux build and operation before release.
