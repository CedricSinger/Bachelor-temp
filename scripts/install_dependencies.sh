#!/bin/bash

set -e

echo "Installing dependencies..."

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    nlohmann-json3-dev \
    zlib1g-dev

echo ""
echo "Dependencies installed!"
