# Sniffer TCP record/replay gateway

`gateway_tcp_rr` is the independently buildable Linux gateway entry point for
packet-derived record/replay. It does not depend on the legacy WAMR socket
control protocol or libcrafter. Existing `gateway/main.cpp` remains the legacy
migration proxy and is not wired to this new replay CLI.

The selected semantics are **new kernel connections from the recorded SYN**:
replay-client calls connect, replay-server listens/accepts. Linux performs a
new SYN/SYN-ACK/ACK handshake; TCP_INFO must report ESTABLISHED before data is
replayed. Old sequence numbers, congestion state and unacknowledged kernel
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

One complete IPv4 TCP connection per capture, Ethernet (including VLAN),
Linux SLL and SLL2 framing. IPv6, IP fragmentation, TCP Fast Open, resets,
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
