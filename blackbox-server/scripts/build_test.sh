#!/bin/bash
# Build script for test_nvidia_apis

cd "$(dirname "$0")/.."
mkdir -p build_test
cd build_test

# Use the test CMakeLists
cp ../CMakeLists_test.txt CMakeLists.txt

cmake .
make -j$(nproc)

if [ -f test_nvidia_apis ]; then
    echo "Build successful! Run with: ./build_test/test_nvidia_apis"
else
    echo "Build failed!"
    exit 1
fi

