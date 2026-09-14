# Mdns server component

This component takes a mdns request packet and can create a mdns
response packet.

# Tests

Unit tests for the packet handling live in `test/` and are built by
the Boost.Test project in `ci-build/`. Boost is provided by the vcpkg
submodule in `3rdparty/vcpkg` (manifest in `ci-build/vcpkg.json`):

```
./3rdparty/vcpkg/bootstrap-vcpkg.sh -disableMetrics
cmake -B build -S components/mdns/ci-build \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/3rdparty/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --parallel
./build/mdns_test
```

The same steps run in `.github/workflows/mdns_ci.yml`. Socket level
integration tests are located in the private nabto-common-cpp
repository.
