# libnn

Common Data Structures

Implemented structures:

  * Vector based on dynamic allications `nn/vector.h`
  * String set based on dynamic allocations `nn/string_set.h`
  * double linked list without allocations `nn/llist.h`
  * string kv map based on dynamic allocations `nn/string_map.h`


## Run the test suite

```
cmake -DNN_BUILD_TESTS=ON <path to nn>
./nn_test
```
