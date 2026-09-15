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
