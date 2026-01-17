#!/bin/bash

# GraphQL Server Setup Script
# This script helps set up the GraphQL server

set -e

echo "=========================================="
echo "GraphQL Server Setup"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Build the GraphQL server
build_server() {
    echo ""
    echo "Building GraphQL server..."
    echo ""
    
    # Determine build system
    if [ -f "CMakeLists.txt" ]; then
        echo "Using CMake build system..."
        mkdir -p build
        cd build
        cmake ..
        make -j$(nproc)
        echo -e "${GREEN}✓ Build complete${NC}"
        echo ""
        echo "Executable: $(pwd)/graphql_server"
    elif [ -f "meson.build" ]; then
        echo "Using Meson build system..."
        cd ../../../..
        meson setup builddir 2>/dev/null || meson configure builddir
        meson compile -C builddir
        echo -e "${GREEN}✓ Build complete${NC}"
        echo ""
        echo "Executable: builddir/examples/graphql_server/graphql_server"
    else
        echo -e "${RED}Error: No build system found${NC}"
        exit 1
    fi
}

# Generate SSL certificates if needed
generate_certs() {
    if [ ! -f "server-cert.pem" ] || [ ! -f "server-key.pem" ]; then
        echo ""
        echo "Generating self-signed SSL certificates..."
        openssl req -x509 -newkey rsa:4096 -keyout server-key.pem -out server-cert.pem \
            -days 365 -nodes -subj "/CN=localhost"
        echo -e "${GREEN}✓ Certificates generated${NC}"
    fi
}

# Main installation flow
main() {
    echo "This script will help you set up the GraphQL server."
    echo ""
    
    # Build the server
    echo ""
    read -p "Would you like to build the GraphQL server now? (y/n) " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_server
        generate_certs
        
        echo ""
        echo "=========================================="
        echo -e "${GREEN}Setup Complete!${NC}"
        echo "=========================================="
        echo ""
        echo "To run the server:"
        echo "  ./graphql_server --port 8443"
        echo ""
        echo "Endpoints:"
        echo "  POST https://localhost:8443/graphql  - GraphQL API"
        echo "  GET  https://localhost:8443/health   - Health check"
        echo "  GET  https://localhost:8443/schema   - Schema documentation"
        echo ""
        echo "Documentation:"
        echo "  README.md            - General documentation"
        echo "  sample_queries.json  - Example queries"
        echo ""
    fi
}

# Run main function
main

# Made with Bob
