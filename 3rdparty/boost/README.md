## boost source

Source in the generated folder is created by the boost bcp utility.

coammands to generate the source in boost 1.65
```
./boostrap.sh
./b2 tools/bcp
./bin.v2/tools/bcp/gcc-8.2.0/release/link-static/bcp  <headers.... see generate script> ~/sandbox/nabto-common-cpp/3rdparty/boost/generated/
```
