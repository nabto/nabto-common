# Streaming module.

The streaming module can create a reliable stream over an unrealiable
connection.


## Protocol documentation

The protocol documentation is found in a private repository
`protocol/streaming.md`

## Receive window

A stream accepts at most `NABTO_STREAM_MAX_RECV_SEGMENTS` segments
above the highest in-order segment it has received (the sender's
maximum flight) and advertises that window in its acks; the window is
closed while a recv segment cannot be allocated. Data outside the
window is dropped and acked. The segment allocator in the module
remains the overall memory bound.

## Security model

Stream packets are carried inside a DTLS connection, so they cannot be
forged or modified. DTLS rejects a record it has already received, but
not one it has never seen: an adversary can drop a record from the
network and deliver it later, and DTLS accepts it once as long as it
is still inside the replay protection window. Since the stream layer
retransmits lost packets in new DTLS records, the withheld record
arrives as a replay of a stream packet which has already been handled,
possibly after the stream it belonged to has been closed.

The initiator never reuses a stream id, so a replayed packet from an
old stream cannot reach a new stream on the initiator, and a replayed
rst only reaches a stream that is already closed.

On the responder a replayed syn would open a new stream. To prevent a
replayed syn and the acks that followed it from establishing that
stream, the responder sends a nonce in its syn|ack
(`nabto_stream_init_responder`) and drops every ack until one carries
a matching nonce response. The initiator signals support with the
nonce capability extension in its syn; a syn without it, as sent by
older initiators, disables replay protection for that stream. A
responder ignores syn|ack packets, so the peer cannot replace the nonce
or turn replay protection off once the stream exists.

The nonce must be fresh and random for each stream; generating it is
the responsibility of the caller of `nabto_stream_init_responder`.

## Tests

Unit tests live in `test/` and are built by the Boost.Test project in
`ci-build/`. Boost is provided by the vcpkg submodule in
`3rdparty/vcpkg` (manifest in `ci-build/vcpkg.json`):

```
./3rdparty/vcpkg/bootstrap-vcpkg.sh -disableMetrics
cmake -B build -S components/streaming/ci-build \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/3rdparty/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --parallel
./build/streaming_test
```

The library and the tests are built with AddressSanitizer and
UndefinedBehaviorSanitizer; pass `-DNABTO_STREAMING_SANITIZE=OFF` to
cmake to disable that. The same steps run in
`.github/workflows/streaming_ci.yml`. Explorative tests are located in
the private nabto-common-cpp repository.
