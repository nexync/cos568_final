#!/bin/bash
# Build the benchmark executable using the minimal CMakeLists.txt

# Make sure we're in the project root directory
if [ ! -f CMakeLists.txt ]; then
    echo "Error: This script must be run from the project root directory"
    exit 1
fi

echo "Creating and entering build directory..."
mkdir -p build
cd build

echo "Configuring build..."
cmake ..
if [ $? -ne 0 ]; then
    echo "Error: CMake configuration failed"
    exit 1
fi

echo "Building benchmark..."
make
BUILD_RESULT=$?

if [ $BUILD_RESULT -ne 0 ]; then
    echo "Error: Build failed"
    exit 1
fi

# Verify that the benchmark binary was created
if [ ! -f benchmark ]; then
    echo "Error: Build completed but benchmark binary was not created"
    exit 1
fi

echo "Build successful! Binary created at: $(pwd)/benchmark"

# Verify the binary doesn't include unwanted index implementations
# Use more specific patterns to avoid false positives
echo "Verifying that the binary contains only DynamicPGM, B+Tree implementations..."
UNWANTED_REFERENCES=$(nm benchmark | grep -E "benchmark_64_rmi|benchmark_64_art|benchmark_64_alex|benchmark_64_mabtree|benchmark_64_wormhole|benchmark_64_fast|benchmark_64_finedex|benchmark_64_xindex")

if [ -z "$UNWANTED_REFERENCES" ]; then
    echo "Verification successful: No unwanted index implementations found."
else
    echo "Warning: Found references to unwanted index implementations:"
    echo "$UNWANTED_REFERENCES"
    exit 1
fi

echo "Minimal benchmark binary is ready for DynamicPGM, B+Tree and LIPP benchmarking!"