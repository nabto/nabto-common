# libnn

Common Data Structures

Implemented structures:

  * Vector based on dynamic allications `nn/vector.h`
  * String set based on dynamic allocations `nn/string_set.h`
  * double linked list without allocations `nn/llist.h`
  * string kv map based on dynamic allocations `nn/string_map.h`
  * IP address structure `nn/ip_address.h`


## Run the test suite

With vcpkg
```
cmake -DCMAKE_TOOLCHAIN_FILE=~/sandbox/vcpkg/scripts/buildsystems/vcpkg.cmake ../../components/nn/ci-build/
./nn_test
```

With boost unit test framework installed on the system:
```
cmake ../../components/nn/ci-build/
./nn_test
```
