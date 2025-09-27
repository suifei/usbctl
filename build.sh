#!/bin/bash

# usbctl Build Script
# Builds a static single-file executable for ARM Linux devices

set -e

# Configuration
PROJECT_NAME="usbctl"
VERSION="1.0.0"
SOURCE_FILE="usbctl.c"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print with color
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check dependencies
check_deps() {
    print_info "Checking dependencies..."
    
    if ! command -v gcc &> /dev/null; then
        print_error "gcc is not installed"
        exit 1
    fi
    
    if ! command -v strip &> /dev/null; then
        print_warn "strip not found, binary will be larger"
    fi
}

# Build function
build() {
    local target=$1
    local output_name="${PROJECT_NAME}"
    
    print_info "Building $target version..."
    
    # Compiler flags
    local CFLAGS="-std=c99 -O2 -Wall -Wextra"
    local LDFLAGS="-static -pthread"
    
    case $target in
        "arm64")
            if command -v aarch64-linux-gnu-gcc &> /dev/null; then
                CC="aarch64-linux-gnu-gcc"
                output_name="${PROJECT_NAME}-arm64"
            else
                print_error "ARM64 cross-compiler not found (aarch64-linux-gnu-gcc)"
                print_info "Install with: sudo apt-get install gcc-aarch64-linux-gnu"
                return 1
            fi
            ;;
        "armv7")
            if command -v arm-linux-gnueabihf-gcc &> /dev/null; then
                CC="arm-linux-gnueabihf-gcc"
                output_name="${PROJECT_NAME}-armv7"
            else
                print_error "ARMv7 cross-compiler not found (arm-linux-gnueabihf-gcc)"
                print_info "Install with: sudo apt-get install gcc-arm-linux-gnueabihf"
                return 1
            fi
            ;;
        "x86_64"|"native")
            CC="gcc"
            output_name="${PROJECT_NAME}-x86_64"
            ;;
        *)
            print_error "Unknown target: $target"
            return 1
            ;;
    esac
    
    # Compile
    $CC $CFLAGS $LDFLAGS -o $output_name $SOURCE_FILE
    
    # Strip binary if possible
    if command -v strip &> /dev/null; then
        strip $output_name
    fi
    
    # Show file info
    local size=$(ls -lh $output_name | awk '{print $5}')
    print_info "Built: $output_name ($size)"
    
    # Test if binary works
    if [[ $target == "native" || $target == "x86_64" ]]; then
        if ./$output_name --version &> /dev/null; then
            print_info "Binary test: OK"
        else
            print_warn "Binary test: FAILED (may need dependencies)"
        fi
    fi
}

# Clean function
clean() {
    print_info "Cleaning build artifacts..."
    rm -f ${PROJECT_NAME}-*
    print_info "Clean complete"
}

# Main logic
case "${1:-native}" in
    "clean")
        clean
        ;;
    "all")
        check_deps
        build "x86_64"
        build "arm64"
        build "armv7"
        print_info "All builds complete!"
        ;;
    "arm64"|"armv7"|"x86_64"|"native")
        check_deps
        build "$1"
        ;;
    "help"|"-h"|"--help")
        echo "Usage: $0 [target]"
        echo ""
        echo "Targets:"
        echo "  native    Build for current architecture (default)"
        echo "  x86_64    Build for x86_64"
        echo "  arm64     Build for ARM64 (aarch64)"
        echo "  armv7     Build for ARMv7 (armhf)"
        echo "  all       Build for all targets"
        echo "  clean     Remove build artifacts"
        echo "  help      Show this help"
        echo ""
        echo "Examples:"
        echo "  $0 arm64        # Build for Raspberry Pi 4, Pi 5"
        echo "  $0 armv7        # Build for Raspberry Pi 3, Pi Zero 2"
        echo "  $0 all          # Build for all supported targets"
        ;;
    *)
        print_error "Unknown option: $1"
        print_info "Use '$0 help' for usage information"
        exit 1
        ;;
esac