# usbctl - Multi-platform Build System
# =================================

# Project information
PROJECT := usbctl
VERSION := $(shell grep "#define VERSION" usbctl.c | cut -d'"' -f2)
BUILD_TIME := $(shell date '+%Y%m%d%H%M%S')
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")

# Directories
BUILD_DIR := build
DIST_DIR := dist
SRC_FILE := usbctl.c

# Default build flags
CFLAGS := -static -O2 -Wall -Wextra -DVERSION=\"$(VERSION)\" -DBUILD_TIME=\"$(BUILD_TIME)\" -DGIT_COMMIT=\"$(GIT_COMMIT)\"
LDFLAGS := -lpthread

# Build targets - organized by architecture and OS
# =================================================================

# Linux targets with different architectures
LINUX_TARGETS := \
	linux-x86_64 \
	linux-i386 \
	linux-arm64 \
	linux-armv7 \
	linux-mips \
	linux-mipsel \
	linux-mips64 \
	linux-mips64el \
	linux-ppc64le \
	linux-riscv64 \
	linux-s390x

# macOS targets (需要交叉编译工具链)
MACOS_TARGETS := \
	darwin-x86_64 \
	darwin-arm64

# Windows targets (mingw)
WINDOWS_TARGETS := \
	windows-x86_64 \
	windows-i386 \
	windows-arm64

# All targets
ALL_TARGETS := $(LINUX_TARGETS) $(MACOS_TARGETS) $(WINDOWS_TARGETS)

# Cross-compiler configurations
# =================================================================

# Linux cross-compilers
CC_linux-x86_64 := x86_64-linux-gnu-gcc
CC_linux-i386 := i686-linux-gnu-gcc
CC_linux-arm64 := aarch64-linux-gnu-gcc
CC_linux-armv7 := arm-linux-gnueabihf-gcc
CC_linux-mips := mips-linux-gnu-gcc
CC_linux-mipsel := mipsel-linux-gnu-gcc
CC_linux-mips64 := mips64-linux-gnuabi64-gcc
CC_linux-mips64el := mips64el-linux-gnuabi64-gcc
CC_linux-ppc64le := powerpc64le-linux-gnu-gcc
CC_linux-riscv64 := riscv64-linux-gnu-gcc
CC_linux-s390x := s390x-linux-gnu-gcc

# macOS (native compilation only)
# CC_darwin-x86_64 := # Use native compilation on macOS
# CC_darwin-arm64 := # Use native compilation on macOS

# Windows (MinGW)
CC_windows-x86_64 := x86_64-w64-mingw32-gcc
CC_windows-i386 := i686-w64-mingw32-gcc
CC_windows-arm64 := clang --target=aarch64-pc-windows-msvc

# Architecture-specific CFLAGS
CFLAGS_linux-i386 := -m32
CFLAGS_linux-armv7 := -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard

# OS-specific LDFLAGS
LDFLAGS_windows := -static -lpthread -lws2_32
LDFLAGS_darwin := -lpthread

# File extensions by OS
EXT_linux :=
EXT_darwin :=
EXT_windows := .exe

# Build rules
# =================================================================

.PHONY: all clean distclean help install check-deps
.PHONY: $(ALL_TARGETS) native
.PHONY: dist release checksums package
.PHONY: linux macos windows

# Default target - build for current platform
all: native

# Native build (auto-detect current platform)
native:
	@echo "Building native binary..."
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(BUILD_DIR)/$(PROJECT) $(SRC_FILE) $(LDFLAGS)
	@echo "Native build complete: $(BUILD_DIR)/$(PROJECT)"

# Platform groups
linux: $(LINUX_TARGETS)
macos: $(MACOS_TARGETS)
windows: $(WINDOWS_TARGETS)

# Individual target builds
$(LINUX_TARGETS): linux-%:
	@echo "Building $@ ($(CC_$@))..."
	@mkdir -p $(BUILD_DIR)
	@if command -v $(CC_$@) >/dev/null 2>&1; then \
		$(CC_$@) $(CFLAGS) $(CFLAGS_$@) -o $(BUILD_DIR)/$(PROJECT)-$@$(EXT_linux) $(SRC_FILE) $(LDFLAGS) && \
		echo "✓ Built $@: $(BUILD_DIR)/$(PROJECT)-$@$(EXT_linux)"; \
	else \
		echo "✗ Skipping $@: $(CC_$@) not found"; \
	fi

$(MACOS_TARGETS): darwin-%:
	@echo "Building $@ ($(CC_$@))..."
	@mkdir -p $(BUILD_DIR)
	@if command -v $(CC_$@) >/dev/null 2>&1; then \
		$(CC_$@) $(CFLAGS) $(CFLAGS_$@) -o $(BUILD_DIR)/$(PROJECT)-$@$(EXT_darwin) $(SRC_FILE) $(LDFLAGS_darwin) && \
		echo "✓ Built $@: $(BUILD_DIR)/$(PROJECT)-$@$(EXT_darwin)"; \
	else \
		echo "✗ Skipping $@: $(CC_$@) not found"; \
	fi

$(WINDOWS_TARGETS): windows-%:
	@echo "Building $@ ($(CC_$@))..."
	@mkdir -p $(BUILD_DIR)
	@if [ "$@" = "windows-arm64" ]; then \
		clang --target=aarch64-pc-windows-msvc $(CFLAGS) $(CFLAGS_$@) -o $(BUILD_DIR)/$(PROJECT)-$@$(EXT_windows) $(SRC_FILE) $(LDFLAGS_windows) && \
		echo "✓ Built $@: $(BUILD_DIR)/$(PROJECT)-$@$(EXT_windows)"; \
	elif command -v $(CC_$@) >/dev/null 2>&1; then \
		$(CC_$@) $(CFLAGS) $(CFLAGS_$@) -o $(BUILD_DIR)/$(PROJECT)-$@$(EXT_windows) $(SRC_FILE) $(LDFLAGS_windows) && \
		echo "✓ Built $@: $(BUILD_DIR)/$(PROJECT)-$@$(EXT_windows)"; \
	else \
		echo "✗ Skipping $@: $(CC_$@) not found"; \
	fi

# Build all available targets
build-all:
	@echo "Building all available targets..."
	@for target in $(ALL_TARGETS); do \
		$(MAKE) $$target || true; \
	done
	@echo "Build summary:"
	@ls -la $(BUILD_DIR)/ || true

# Release and distribution
# =================================================================

# Create distribution package
dist: build-all
	@echo "Creating distribution package..."
	@mkdir -p $(DIST_DIR)
	@rm -f $(DIST_DIR)/*
	@# Copy all built binaries to dist
	@for file in $(BUILD_DIR)/*; do \
		if [ -f "$$file" ] && [ -x "$$file" ]; then \
			cp "$$file" $(DIST_DIR)/; \
		fi; \
	done
	@# Copy additional files
	@cp LICENSE $(DIST_DIR)/ 2>/dev/null || true
	@cp README.md $(DIST_DIR)/ 2>/dev/null || true
	@echo "Distribution created in $(DIST_DIR)/"
	@ls -la $(DIST_DIR)/

# Generate checksums
checksums: dist
	@echo "Generating checksums..."
	@cd $(DIST_DIR) && \
		sha256sum $(PROJECT)-* > SHA256SUMS 2>/dev/null || \
		shasum -a 256 $(PROJECT)-* > SHA256SUMS
	@echo "Checksums generated: $(DIST_DIR)/SHA256SUMS"

# Create release packages
package: checksums
	@echo "Creating release packages..."
	@cd $(DIST_DIR) && \
		tar -czf $(PROJECT)-$(VERSION)-linux.tar.gz $(PROJECT)-linux-* LICENSE README.md SHA256SUMS 2>/dev/null || true
	@cd $(DIST_DIR) && \
		tar -czf $(PROJECT)-$(VERSION)-darwin.tar.gz $(PROJECT)-darwin-* LICENSE README.md SHA256SUMS 2>/dev/null || true
	@cd $(DIST_DIR) && \
		zip -q $(PROJECT)-$(VERSION)-windows.zip $(PROJECT)-windows-* LICENSE README.md SHA256SUMS 2>/dev/null || true
	@echo "Release packages created:"
	@ls -la $(DIST_DIR)/*.tar.gz $(DIST_DIR)/*.zip 2>/dev/null || true

# Complete release process
release: clean dist checksums package
	@echo ""
	@echo "==================================================================="
	@echo "Release $(VERSION) completed!"
	@echo "Build time: $(BUILD_TIME)"
	@echo "Git commit: $(GIT_COMMIT)"
	@echo "==================================================================="
	@echo "Files in $(DIST_DIR):"
	@ls -la $(DIST_DIR)/
	@echo ""
	@echo "Binary sizes:"
	@cd $(DIST_DIR) && ls -lh $(PROJECT)-* 2>/dev/null || true

# Development and maintenance
# =================================================================

# Check dependencies
check-deps:
	@echo "Checking build dependencies..."
	@echo "Available cross-compilers:"
	@command -v $(CC_linux-x86_64) >/dev/null 2>&1 && echo "  ✓ linux-x86_64: $(CC_linux-x86_64)" || echo "  ✗ linux-x86_64: $(CC_linux-x86_64) (not found)"
	@command -v $(CC_linux-i386) >/dev/null 2>&1 && echo "  ✓ linux-i386: $(CC_linux-i386)" || echo "  ✗ linux-i386: $(CC_linux-i386) (not found)"
	@command -v $(CC_linux-arm64) >/dev/null 2>&1 && echo "  ✓ linux-arm64: $(CC_linux-arm64)" || echo "  ✗ linux-arm64: $(CC_linux-arm64) (not found)"
	@command -v $(CC_linux-armv7) >/dev/null 2>&1 && echo "  ✓ linux-armv7: $(CC_linux-armv7)" || echo "  ✗ linux-armv7: $(CC_linux-armv7) (not found)"
	@command -v $(CC_linux-mips) >/dev/null 2>&1 && echo "  ✓ linux-mips: $(CC_linux-mips)" || echo "  ✗ linux-mips: $(CC_linux-mips) (not found)"
	@command -v $(CC_linux-mipsel) >/dev/null 2>&1 && echo "  ✓ linux-mipsel: $(CC_linux-mipsel)" || echo "  ✗ linux-mipsel: $(CC_linux-mipsel) (not found)"
	@command -v $(CC_linux-mips64) >/dev/null 2>&1 && echo "  ✓ linux-mips64: $(CC_linux-mips64)" || echo "  ✗ linux-mips64: $(CC_linux-mips64) (not found)"
	@command -v $(CC_linux-mips64el) >/dev/null 2>&1 && echo "  ✓ linux-mips64el: $(CC_linux-mips64el)" || echo "  ✗ linux-mips64el: $(CC_linux-mips64el) (not found)"
	@command -v $(CC_linux-ppc64le) >/dev/null 2>&1 && echo "  ✓ linux-ppc64le: $(CC_linux-ppc64le)" || echo "  ✗ linux-ppc64le: $(CC_linux-ppc64le) (not found)"
	@command -v $(CC_linux-riscv64) >/dev/null 2>&1 && echo "  ✓ linux-riscv64: $(CC_linux-riscv64)" || echo "  ✗ linux-riscv64: $(CC_linux-riscv64) (not found)"
	@command -v $(CC_linux-s390x) >/dev/null 2>&1 && echo "  ✓ linux-s390x: $(CC_linux-s390x)" || echo "  ✗ linux-s390x: $(CC_linux-s390x) (not found)"
	@echo "  → macOS: Use native compilation on macOS system"
	@command -v $(CC_windows-x86_64) >/dev/null 2>&1 && echo "  ✓ windows-x86_64: $(CC_windows-x86_64)" || echo "  ✗ windows-x86_64: $(CC_windows-x86_64) (not found)"
	@command -v $(CC_windows-i386) >/dev/null 2>&1 && echo "  ✓ windows-i386: $(CC_windows-i386)" || echo "  ✗ windows-i386: $(CC_windows-i386) (not found)"
	@command -v clang >/dev/null 2>&1 && echo "  ✓ windows-arm64: clang (with Windows ARM64 target)" || echo "  ✗ windows-arm64: clang (not found)"

# Install native binary
install: native
	@echo "Installing $(PROJECT)..."
	@install -m 755 $(BUILD_DIR)/$(PROJECT) /usr/local/bin/
	@echo "$(PROJECT) installed to /usr/local/bin/"

# Clean build artifacts
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)
	@echo "Build directory cleaned."

# Clean everything including dist
distclean: clean
	@echo "Cleaning distribution directory..."
	@rm -rf $(DIST_DIR)
	@echo "Distribution directory cleaned."

# Show build information
info:
	@echo "Project: $(PROJECT)"
	@echo "Version: $(VERSION)"
	@echo "Build time: $(BUILD_TIME)"
	@echo "Git commit: $(GIT_COMMIT)"
	@echo "Source file: $(SRC_FILE)"
	@echo "Build directory: $(BUILD_DIR)"
	@echo "Distribution directory: $(DIST_DIR)"
	@echo ""
	@echo "Available targets:"
	@echo "  Linux: $(LINUX_TARGETS)"
	@echo "  macOS: $(MACOS_TARGETS)"
	@echo "  Windows: $(WINDOWS_TARGETS)"

# Help
help:
	@echo "usbctl Build System"
	@echo "==================="
	@echo ""
	@echo "Targets:"
	@echo "  all, native    - Build for current platform"
	@echo "  build-all      - Build all available targets"
	@echo "  linux          - Build all Linux targets"
	@echo "  macos          - Build all macOS targets"
	@echo "  windows        - Build all Windows targets"
	@echo ""
	@echo "Individual targets:"
	@echo "  $(LINUX_TARGETS)"
	@echo "  $(MACOS_TARGETS)"
	@echo "  $(WINDOWS_TARGETS)"
	@echo ""
	@echo "Distribution:"
	@echo "  dist           - Create distribution with all binaries"
	@echo "  checksums      - Generate SHA256 checksums"
	@echo "  package        - Create release packages (tar.gz, zip)"
	@echo "  release        - Complete release process"
	@echo ""
	@echo "Utilities:"
	@echo "  check-deps     - Check available cross-compilers"
	@echo "  install        - Install native binary to /usr/local/bin"
	@echo "  clean          - Remove build directory"
	@echo "  distclean      - Remove build and dist directories"
	@echo "  info           - Show project information"
	@echo "  help           - Show this help"
	@echo ""
	@echo "Examples:"
	@echo "  make                    # Build native binary"
	@echo "  make linux-arm64        # Build ARM64 Linux binary"
	@echo "  make linux              # Build all Linux targets"
	@echo "  make build-all          # Build all available targets"
	@echo "  make release            # Complete release process"
	@echo "  make check-deps         # Check available compilers"
