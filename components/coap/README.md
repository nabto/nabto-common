# Coap client and server

# Tests

Unit tests for the server live in `test/` and are built by the
Boost.Test project in `ci-build/`. Boost is provided by the vcpkg
submodule in `3rdparty/vcpkg` (manifest in `ci-build/vcpkg.json`):

```
./3rdparty/vcpkg/bootstrap-vcpkg.sh -disableMetrics
cmake -B build -S components/coap/ci-build \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/3rdparty/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --parallel
./build/coap_test
```

The library and the tests are built with AddressSanitizer and
UndefinedBehaviorSanitizer; pass `-DNABTO_COAP_SANITIZE=OFF` to cmake
to disable that. The same steps run in `.github/workflows/coap_ci.yml`.
Further tests are located in the private nabto-common-cpp repository.
