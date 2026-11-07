# Sniffer IPv4/IPv6 TCP and UDP record/replay gateway

`gateway_tcp_rr` is the independently buildable Linux gateway entry point for
packet-derived record/replay. It does not depend on the legacy WAMR socket
control protocol or libcrafter. Both builds enter through `gateway/main.cpp`, which dispatches record/replay
commands before opening any legacy listeners. `gateway/tcp_rr.cpp` implements
the shared engine. The legacy `gateway` target enables
`MVVM_GATEWAY_WITH_LEGACY`; with no command it runs the original migration
proxy. The independent `gateway_tcp_rr` target compiles the same main without
the legacy dependencies. Unknown commands fail.

The selected semantics are **new kernel connections from the recorded SYN**:
replay-client calls connect, replay-server listens/accepts. Linux performs a
new SYN/SYN-ACK/ACK handshake; TCP_INFO must confirm a completed connection (ESTABLISHED, or CLOSE_WAIT
when the peer has already half-closed) before data is replayed. Old sequence numbers, congestion state and unacknowledged kernel
queues are not transplanted into a live remote connection. No TCP_REPAIR or
raw SYN injection is used. The destination endpoint is explicit, so testing
can use an isolated service instead of re-executing requests on production.

## Build and use

Requires GCC 16, C++26, CMake, Ninja and libpcap-dev. No fmt/Yalantinglibs.

```sh
cmake -S . -B build-gateway -G Ninja \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DCMAKE_BUILD_TYPE=Release -DMVVM_ENABLE_DWARF_RUNTIME=OFF \
  -DMVVM_BUILD_TCP_GATEWAY=ON
cmake --build build-gateway
# Start before opening the application connection. Record one complete flow.
sudo build-gateway/gateway_tcp_rr record eth0 192.0.2.10 8080 session.pcap 30
build-gateway/gateway_tcp_rr inspect session.pcap 192.0.2.10 8080
# Original endpoint identifies direction; last endpoint is the new target.
build-gateway/gateway_tcp_rr replay-client session.pcap 192.0.2.10 8080 127.0.0.1 9090
# Or emulate the recorded server for a real client connecting to this listener:
build-gateway/gateway_tcp_rr replay-server session.pcap 192.0.2.10 8080 127.0.0.1 9090
```

Record uses libpcap with an endpoint BPF filter, full snaplen, immediate mode
and capture-drop checks. It writes ordinary PCAP packets, not recv() buffers.
Files are created under a private umask. Wait for `CAPTURING` before starting
the application; a nonzero record exit means the capture is unusable.
The time-bounded recorder stops even if no packets arrive. It does not alter
host firewall rules or forwarding settings.

Replay validates the complete capture before opening a network connection.
It reconstructs both byte streams using TCP sequence offsets from SYN,
removes matching retransmissions and buffers out-of-order segments. Conflicting
overlaps, missing data, truncation, absent handshake/FIN and RST are rejected.
Payload and FIN events are replayed in the order they become contiguous.
Reads compare exact bytes regardless of recv() segmentation, sends handle
partial writes, and FIN maps to shutdown(SHUT_WR)/observed EOF. Mismatches and
five-second socket inactivity fail explicitly. Replay-server verifies client
requests and emits the recorded responses; replay-client sends recorded
requests and verifies responses from the live target.

## Scope

One IPv4 or IPv6 flow per capture, Ethernet (including VLAN),
Linux SLL and SLL2 framing. IP fragmentation, IPv6 scope IDs/routing/security headers, TCP Fast Open, resets,
multiple flows, live-tail replay, original packet timing, TCP-option identity,
and continuing a still-live old connection are not implemented. Kernel socket
options/ISNs are freshly negotiated. Capture has a 256 MiB limit, streams and
pending reassembly each have a 64 MiB limit, and replay accepts at most one
million packets. Capture-drop counters describe this recorder, not arbitrary
external PCAPs. Payload checksums are not verified because host captures may
contain checksum-offload placeholders. TLS or protocols containing random
nonces will not generally reproduce identical bytes against a fresh server.
A failure partway through replay can occur after the target has processed
requests; replay is not transactional and does not suppress application side
effects. This CLI does not yet share a checkpoint boundary with the DWARF
runner or provide crash-consistent application-plus-network snapshots.

## Native RISC-V validation

Run the integration test in a new network namespace on lanxin2:

```sh
sudo unshare -n python3 test/test_tcp_rr.py \
  /tmp/mvvm-build-gateway-rr/gateway_tcp_rr /tmp/mvvm-gateway-results
```

The test brings up only its own loopback, captures a real 262,144-byte binary
request and 262,148-byte response, and tests client/server replay, FIN
half-close, duplicate and reordered segments. Negative cases include response
mismatch, missing SYN, missing FIN, data gap, conflicting retransmission and
truncated PCAP. Synthetic impairment cases are edits to the real capture;
they do not claim packet loss or reordering was induced in the network.
`--interface-ready` uses an already-active loopback in the CI container.
The Linux Debug/Release CI builds and runs this integration test alongside
the existing native DWARF checks. The archived JSON records exact commands,
exit codes, byte counts and capture SHA256.

## IPv6 and UDP

IPv6 literals are accepted in all endpoint arguments (for example `::1`).
TCP uses the same SYN-based reconstruction on both address families.
IPv6 hop-by-hop and destination-options headers are parsed in offline captures;
the live BPF capture filter targets ordinary directly encapsulated TCP/UDP.
Routing, fragment, security extension headers and jumbograms fail explicitly.

Append `udp` to the record command to select UDP (`tcp` is the default):

```sh
sudo build-gateway/gateway_tcp_rr record lo ::1 9090 udp6.pcap 10 udp
build-gateway/gateway_tcp_rr replay-client udp6.pcap ::1 9090 ::1 9091
build-gateway/gateway_tcp_rr replay-server udp6.pcap ::1 9090 ::1 9092
```

Offline inspection/replay detects TCP versus UDP from the capture and rejects
mixed protocols/flows. UDP has no SYN or kernel ESTABLISHED state: replay
creates a fresh datagram socket, preserves each packet boundary (including
empty and duplicate datagrams), and checks both length and payload. Server
replay learns the first client with recvfrom/MSG_PEEK, then connects the socket
to that peer. The first recorded packet must originate at the client. UDP does
not have sequence numbers or FIN; missing/reordered UDP packets cannot be
inferred from PCAP alone. Recorded order is retained, with no deduplication,
retransmission, loss recovery or packet-timing guarantee. End-of-log is the
UDP replay boundary and does not imply that all future traffic was captured.

Native lanxin2 tests cover TCP4/TCP6 with 256 KiB requests, and UDP4/UDP6 with
six request/response datagrams (0, 1, 8, 8, 1280, 32000 request bytes). Both
client and server roles pass, including zero-length and duplicate UDP payloads;
a mismatched UDP response is rejected. These are isolated loopback tests, not
physical-network throughput or loss measurements.
