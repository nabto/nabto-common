#!/bin/bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

tmp_dir=$(mktemp -d -t ci-XXXXXXXXXX)

BOOST_URL=https://dl.bintray.com/boostorg/release/1.72.0/source/boost_1_72_0.tar.gz

cd $tmp_dir

wget $BOOST_URL

tar xf boost_1_72_0.tar.gz

cd boost_1_72_0

./bootstrap.sh
./b2 -j 8 tools/bcp
BCP_EXE=`find ./bin.v2/tools/bcp/ -name bcp -type f`
$BCP_EXE boost/asio.hpp boost/filesystem.hpp boost/test/unit_test.hpp boost/test/data/test_case.hpp boost/optional.hpp boost/optional/optional_io.hpp boost/algorithm/string.hpp boost/any.hpp boost/program_options.hpp boost/beast.hpp boost/asio/ssl.hpp $DIR/generated/


rm -r $tmp_dir
