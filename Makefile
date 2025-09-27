# Makefile for usbctl - USB/IP Web Manager

PROJECT = usbctl
VERSION = 1.0.0
SOURCE = usbctl.c

# Compiler and flags
CC = gcc
CFLAGS = -std=c99 -O2 -Wall -Wextra -DVERSION=\"$(VERSION)\"

# Platform-specific flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS = -static -pthread
else ifeq ($(UNAME_S),Darwin)
    LDFLAGS = -pthread
    CFLAGS += -Wno-unused-parameter
else
    LDFLAGS = -static -pthread
endif

# Cross-compilation toolchains
CC_ARM64 = aarch64-linux-gnu-gcc
CC_ARMV7 = arm-linux-gnueabihf-gcc
CC_X86_64 = gcc

# Output binaries
BIN_NATIVE = $(PROJECT)
BIN_ARM64 = $(PROJECT)-arm64
BIN_ARMV7 = $(PROJECT)-armv7
BIN_X86_64 = $(PROJECT)-x86_64

# Default target
.PHONY: all native arm64 armv7 x86_64 clean install uninstall test help available

# Build only what's available by default
all: native available

# Try to build all cross-compile targets (may fail if toolchains not installed)
all-force: native arm64 armv7 x86_64

# Build available cross-compilation targets
available:
	@echo "Building available cross-compilation targets..."
	@$(MAKE) -s arm64 2>/dev/null || echo "  → ARM64 toolchain not available"
	@$(MAKE) -s armv7 2>/dev/null || echo "  → ARMv7 toolchain not available" 
	@$(MAKE) -s x86_64 2>/dev/null || echo "  → x86_64 build skipped (same as native)"

# Native build (current architecture)
native: $(BIN_NATIVE)

$(BIN_NATIVE): $(SOURCE)
	@echo "Building native binary..."
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@if command -v strip >/dev/null 2>&1; then strip $@; fi
	@echo "Built: $@ ($$(ls -lh $@ | awk '{print $$5}'))"

# ARM64 build (Raspberry Pi 4/5)
arm64: $(BIN_ARM64)

$(BIN_ARM64): $(SOURCE)
	@echo "Building ARM64 binary..."
	@if ! command -v $(CC_ARM64) >/dev/null 2>&1; then \
		echo "  → $(CC_ARM64) not found. Skipping ARM64 build."; \
		echo "  → To install on Ubuntu/Debian: sudo apt-get install gcc-aarch64-linux-gnu"; \
		echo "  → To install on macOS: brew install aarch64-elf-gcc"; \
		exit 1; \
	fi
	$(CC_ARM64) $(CFLAGS) -static -pthread -o $@ $<
	@if command -v aarch64-linux-gnu-strip >/dev/null 2>&1; then aarch64-linux-gnu-strip $@; fi
	@echo "Built: $@ ($$(ls -lh $@ | awk '{print $$5}'))"

# ARMv7 build (Raspberry Pi 3, Zero 2)
armv7: $(BIN_ARMV7)

$(BIN_ARMV7): $(SOURCE)
	@echo "Building ARMv7 binary..."
	@if ! command -v $(CC_ARMV7) >/dev/null 2>&1; then \
		echo "  → $(CC_ARMV7) not found. Skipping ARMv7 build."; \
		echo "  → To install on Ubuntu/Debian: sudo apt-get install gcc-arm-linux-gnueabihf"; \
		echo "  → To install on macOS: brew install arm-none-eabi-gcc"; \
		exit 1; \
	fi
	$(CC_ARMV7) $(CFLAGS) -static -pthread -o $@ $<
	@if command -v arm-linux-gnueabihf-strip >/dev/null 2>&1; then arm-linux-gnueabihf-strip $@; fi
	@echo "Built: $@ ($$(ls -lh $@ | awk '{print $$5}'))"

# x86_64 build
x86_64: $(BIN_X86_64)

$(BIN_X86_64): $(SOURCE)
	@echo "Building x86_64 binary..."
	@if [ "$(UNAME_S)" = "Darwin" ] && [ "$$(uname -m)" = "x86_64" ]; then \
		echo "  → Skipping x86_64 build (same as native on this platform)"; \
		exit 1; \
	fi
	$(CC_X86_64) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@if command -v strip >/dev/null 2>&1; then strip $@; fi
	@echo "Built: $@ ($$(ls -lh $@ | awk '{print $$5}'))"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(BIN_NATIVE) $(BIN_ARM64) $(BIN_ARMV7) $(BIN_X86_64)
	@echo "Clean complete"

# Install native binary to system
install: $(BIN_NATIVE)
	@echo "Installing $(PROJECT) to /usr/local/bin..."
	@if [ "$$(id -u)" != "0" ]; then \
		echo "Error: Installation requires root privileges"; \
		echo "Run: sudo make install"; \
		exit 1; \
	fi
	install -m 755 $(BIN_NATIVE) /usr/local/bin/$(PROJECT)
	@echo "Installation complete"
	@echo "Run '$(PROJECT) --install-service' to install systemd service"

# Uninstall from system
uninstall:
	@echo "Uninstalling $(PROJECT)..."
	@if [ "$$(id -u)" != "0" ]; then \
		echo "Error: Uninstallation requires root privileges"; \
		echo "Run: sudo make uninstall"; \
		exit 1; \
	fi
	rm -f /usr/local/bin/$(PROJECT)
	rm -f /etc/systemd/system/$(PROJECT).service
	systemctl daemon-reload 2>/dev/null || true
	@echo "Uninstallation complete"

# Test native binary
test: $(BIN_NATIVE)
	@echo "Testing $(PROJECT) binary..."
	@./$(BIN_NATIVE) --version
	@./$(BIN_NATIVE) --help | head -3
	@echo "Basic tests passed"

# Development server (runs in foreground)
dev: $(BIN_NATIVE)
	@echo "Starting development server..."
	./$(BIN_NATIVE) -p 11980

# Show help
help:
	@echo "$(PROJECT) v$(VERSION) - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build native + available cross-compilation targets"
	@echo "  all-force - Build for all architectures (may fail)"
	@echo "  available - Build all available cross-compilation targets"
	@echo "  native    - Build for current architecture (default)"
	@echo "  arm64     - Build for ARM64 (Raspberry Pi 4/5)"
	@echo "  armv7     - Build for ARMv7 (Raspberry Pi 3, Zero 2)"
	@echo "  x86_64    - Build for x86_64"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install native binary to system (requires root)"
	@echo "  uninstall - Remove from system (requires root)"
	@echo "  test      - Test native binary"
	@echo "  dev       - Start development server"
	@echo "  info      - Show build configuration"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Examples:"
	@echo "  make arm64        # Build for Raspberry Pi"
	@echo "  make && make test # Build and test"
	@echo "  sudo make install # Install to system"

# Build info
info:
	@echo "Project: $(PROJECT) v$(VERSION)"
	@echo "Source: $(SOURCE)"
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS) $(LDFLAGS)"
	@echo ""
	@echo "Available cross-compilers:"
	@command -v $(CC_ARM64) >/dev/null 2>&1 && echo "  ✓ $(CC_ARM64)" || echo "  ✗ $(CC_ARM64) (not installed)"
	@command -v $(CC_ARMV7) >/dev/null 2>&1 && echo "  ✓ $(CC_ARMV7)" || echo "  ✗ $(CC_ARMV7) (not installed)"
	@command -v $(CC_X86_64) >/dev/null 2>&1 && echo "  ✓ $(CC_X86_64)" || echo "  ✗ $(CC_X86_64) (not installed)"