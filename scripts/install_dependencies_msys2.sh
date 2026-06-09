#!/bin/bash

set -e

echo "Installing dependencies..."

pacman -S --needed --noconfirm \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-cmake \
    mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-boost \
    mingw-w64-x86_64-nlohmann-json \
    mingw-w64-x86_64-zlib \
    git

echo ""
echo "Dependencies installed!"
