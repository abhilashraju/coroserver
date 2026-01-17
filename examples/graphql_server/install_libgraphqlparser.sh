#!/bin/bash

# Script to install libgraphqlparser
# This script supports Ubuntu/Debian and builds from source

set -e

echo "==================================="
echo "libgraphqlparser Installation Script"
echo "==================================="
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then 
    SUDO=""
else
    SUDO="sudo"
fi

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
else
    echo "Cannot detect OS. Please install manually."
    exit 1
fi

echo "Detected OS: $OS"
echo ""

# Try package manager first
case "$OS" in
    ubuntu|debian)
        echo "Attempting to install via apt..."
        if $SUDO apt-get update && $SUDO apt-get install -y libgraphqlparser-dev; then
            echo "✓ libgraphqlparser installed successfully via apt"
            exit 0
        else
            echo "Package not available via apt, will build from source..."
        fi
        ;;
    fedora|rhel|centos)
        echo "Attempting to install via dnf/yum..."
        if $SUDO dnf install -y libgraphqlparser-devel 2>/dev/null || \
           $SUDO yum install -y libgraphqlparser-devel 2>/dev/null; then
            echo "✓ libgraphqlparser installed successfully"
            exit 0
        else
            echo "Package not available, will build from source..."
        fi
        ;;
esac

# Build from source
echo ""
echo "Building libgraphqlparser from source..."
echo ""

# Install build dependencies
case "$OS" in
    ubuntu|debian)
        echo "Installing build dependencies..."
        $SUDO apt-get install -y \
            git \
            cmake \
            build-essential \
            python3 \
            flex \
            bison
        ;;
    fedora|rhel|centos)
        echo "Installing build dependencies..."
        $SUDO dnf install -y \
            git \
            cmake \
            gcc-c++ \
            python3 \
            flex \
            bison 2>/dev/null || \
        $SUDO yum install -y \
            git \
            cmake \
            gcc-c++ \
            python3 \
            flex \
            bison
        ;;
    *)
        echo "Please install the following manually:"
        echo "  - git"
        echo "  - cmake"
        echo "  - C++ compiler"
        echo "  - python3"
        echo "  - flex"
        echo "  - bison"
        read -p "Press Enter when ready to continue..."
        ;;
esac

# Create temporary directory
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

echo ""
echo "Cloning libgraphqlparser repository..."
git clone https://github.com/graphql/libgraphqlparser.git
cd libgraphqlparser

echo ""
echo "Building libgraphqlparser..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      .

make -j$(nproc)

echo ""
echo "Installing libgraphqlparser..."
$SUDO make install

# Update library cache
echo ""
echo "Updating library cache..."
$SUDO ldconfig

# Cleanup
cd /
rm -rf "$TEMP_DIR"

echo ""
echo "==================================="
echo "✓ Installation Complete!"
echo "==================================="
echo ""
echo "Verify installation:"
echo "  pkg-config --exists libgraphqlparser && echo 'Found' || echo 'Not found'"
echo ""
echo "You can now build the GraphQL server with libgraphqlparser support:"
echo "  cd public/sources/coroserver"
echo "  meson setup build"
echo "  meson compile -C build"
echo ""

# Made with Bob
