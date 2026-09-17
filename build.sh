#!/bin/bash
set -e

echo "==> Cleaning old build objects"
rm -f mopl_backend.o graphics_backend.o mopl

SDK="$(xcrun --sdk macosx --show-sdk-path)"

echo "==> Compiling native graphics backend"
clang -arch arm64 \
    -isysroot "$SDK" \
    -Wall -Wextra -Werror \
    -Wno-deprecated-declarations \
    -fobjc-arc \
    -c graphics_backend.m \
    -o graphics_backend.o

echo "==> Compiling MOPL interpreter"
clang -arch arm64 \
    -isysroot "$SDK" \
    -Wall -Wextra -Werror \
    -x objective-c \
    -fobjc-arc \
    -c mopl_backend.c \
    -o mopl_backend.o

echo "==> Linking MOPL"
clang -arch arm64 \
    -isysroot "$SDK" \
    mopl_backend.o \
    graphics_backend.o \
    -framework Cocoa \
    -framework CoreGraphics \
    -lobjc \
    -o mopl

echo "==> Build successful!"
echo "==> Run with: ./mopl"
