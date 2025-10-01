# usbctl Makefile
# Professional build system for cross-platform compilation and release

# Project metadata
BINARY_NAME := usbctl
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
BUILD_TIME := $(shell date -u '+%Y-%m-%d_%H:%M:%S')
GIT_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")

# Directories
BUILD_DIR := build
DIST_DIR := dist
STATIC_DIR := static

# Go build settings
GO := go
GOFLAGS := -trimpath
LDFLAGS := -s -w \
	-X 'main.Version=$(VERSION)' \
	-X 'main.BuildTime=$(BUILD_TIME)' \
	-X 'main.GitCommit=$(GIT_COMMIT)'

# Platforms to build
PLATFORMS := \
	linux/amd64 \
	linux/arm64 \
	linux/arm \
	windows/amd64 \
	windows/arm64 \
	darwin/amd64 \
	darwin/arm64

# Colors for output
COLOR_RESET := \033[0m
COLOR_BOLD := \033[1m
COLOR_GREEN := \033[32m
COLOR_YELLOW := \033[33m
COLOR_BLUE := \033[34m

.PHONY: all clean build release test lint fmt check help

# Default target
all: clean build

# Help target
help:
	@echo "$(COLOR_BOLD)usbctl Build System$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_GREEN)Available targets:$(COLOR_RESET)"
	@echo "  $(COLOR_BOLD)make all$(COLOR_RESET)        - Clean and build for all platforms"
	@echo "  $(COLOR_BOLD)make build$(COLOR_RESET)      - Build for all platforms"
	@echo "  $(COLOR_BOLD)make release$(COLOR_RESET)    - Create release archives with checksums"
	@echo "  $(COLOR_BOLD)make test$(COLOR_RESET)       - Run tests"
	@echo "  $(COLOR_BOLD)make lint$(COLOR_RESET)       - Run linters"
	@echo "  $(COLOR_BOLD)make fmt$(COLOR_RESET)        - Format code"
	@echo "  $(COLOR_BOLD)make check$(COLOR_RESET)      - Run all checks (fmt, lint, test)"
	@echo "  $(COLOR_BOLD)make clean$(COLOR_RESET)      - Clean build artifacts"
	@echo "  $(COLOR_BOLD)make install$(COLOR_RESET)    - Install locally"
	@echo "  $(COLOR_BOLD)make help$(COLOR_RESET)       - Show this help"
	@echo ""
	@echo "$(COLOR_YELLOW)Examples:$(COLOR_RESET)"
	@echo "  make release             - Build and package all platforms"
	@echo "  make PLATFORMS=linux/amd64 build  - Build only for Linux AMD64"

# Clean build artifacts
clean:
	@echo "$(COLOR_BLUE)Cleaning build artifacts...$(COLOR_RESET)"
	@rm -rf $(BUILD_DIR) $(DIST_DIR)
	@echo "$(COLOR_GREEN)Clean complete$(COLOR_RESET)"

# Ensure directories exist
$(BUILD_DIR) $(DIST_DIR):
	@mkdir -p $@

# Build for all platforms
build: $(BUILD_DIR)
	@echo "$(COLOR_BLUE)Building version $(VERSION)...$(COLOR_RESET)"
	@$(foreach platform,$(PLATFORMS), \
		$(call build_platform,$(platform)))
	@echo "$(COLOR_GREEN)Build complete$(COLOR_RESET)"

# Build function for a single platform
define build_platform
	$(eval OS := $(word 1,$(subst /, ,$(1))))
	$(eval ARCH := $(word 2,$(subst /, ,$(1))))
	$(eval OUTPUT := $(BUILD_DIR)/$(BINARY_NAME)-$(OS)-$(ARCH)$(if $(filter windows,$(OS)),.exe,))
	@echo "  Building $(OS)/$(ARCH)..."
	@GOOS=$(OS) GOARCH=$(ARCH) CGO_ENABLED=0 \
		$(GO) build $(GOFLAGS) -ldflags="$(LDFLAGS)" \
		-o $(OUTPUT) $(BINARY_NAME).go
endef

# Create release archives
release: build $(DIST_DIR)
	@echo "$(COLOR_BLUE)Creating release packages...$(COLOR_RESET)"
	@$(foreach platform,$(PLATFORMS), \
		$(call package_platform,$(platform)))
	@cd $(DIST_DIR) && sha256sum * > checksums.txt
	@echo "$(COLOR_GREEN)Release packages created in $(DIST_DIR)$(COLOR_RESET)"
	@echo "$(COLOR_YELLOW)Checksums:$(COLOR_RESET)"
	@cat $(DIST_DIR)/checksums.txt

# Package function for a single platform
define package_platform
	$(eval OS := $(word 1,$(subst /, ,$(1))))
	$(eval ARCH := $(word 2,$(subst /, ,$(1))))
	$(eval BINARY := $(BINARY_NAME)-$(OS)-$(ARCH)$(if $(filter windows,$(OS)),.exe,))
	$(eval ARCHIVE := $(BINARY_NAME)-$(VERSION)-$(OS)-$(ARCH))
	@echo "  Packaging $(OS)/$(ARCH)..."
	@mkdir -p $(DIST_DIR)/$(ARCHIVE)
	@cp $(BUILD_DIR)/$(BINARY) $(DIST_DIR)/$(ARCHIVE)/$(BINARY_NAME)$(if $(filter windows,$(OS)),.exe,)
	@cp -r $(STATIC_DIR) $(DIST_DIR)/$(ARCHIVE)/ 2>/dev/null || true
	@cp README.md $(DIST_DIR)/$(ARCHIVE)/ 2>/dev/null || echo "README not found"
	@cp LICENSE $(DIST_DIR)/$(ARCHIVE)/ 2>/dev/null || echo "LICENSE not found"
	@cd $(DIST_DIR) && \
		$(if $(filter windows,$(OS)), \
			zip -q -r $(ARCHIVE).zip $(ARCHIVE), \
			tar czf $(ARCHIVE).tar.gz $(ARCHIVE))
	@rm -rf $(DIST_DIR)/$(ARCHIVE)
endef

# Run tests
test:
	@echo "$(COLOR_BLUE)Running tests...$(COLOR_RESET)"
	@$(GO) test -v -race -cover ./...
	@echo "$(COLOR_GREEN)Tests passed$(COLOR_RESET)"

# Run linters
lint:
	@echo "$(COLOR_BLUE)Running linters...$(COLOR_RESET)"
	@if command -v golangci-lint >/dev/null 2>&1; then \
		golangci-lint run; \
	else \
		echo "$(COLOR_YELLOW)golangci-lint not found, skipping$(COLOR_RESET)"; \
	fi
	@$(GO) vet ./...
	@echo "$(COLOR_GREEN)Linting complete$(COLOR_RESET)"

# Format code
fmt:
	@echo "$(COLOR_BLUE)Formatting code...$(COLOR_RESET)"
	@$(GO) fmt ./...
	@if command -v goimports >/dev/null 2>&1; then \
		goimports -w .; \
	fi
	@echo "$(COLOR_GREEN)Formatting complete$(COLOR_RESET)"

# Run all checks
check: fmt lint test
	@echo "$(COLOR_GREEN)All checks passed$(COLOR_RESET)"

# Install locally
install:
	@echo "$(COLOR_BLUE)Installing $(BINARY_NAME)...$(COLOR_RESET)"
	@$(GO) install $(GOFLAGS) -ldflags="$(LDFLAGS)" $(BINARY_NAME).go
	@echo "$(COLOR_GREEN)Installed to $(shell $(GO) env GOPATH)/bin/$(BINARY_NAME)$(COLOR_RESET)"

# Development build (current platform only)
dev:
	@echo "$(COLOR_BLUE)Building for development...$(COLOR_RESET)"
	@$(GO) build -o $(BINARY_NAME) $(BINARY_NAME).go
	@echo "$(COLOR_GREEN)Development build complete: ./$(BINARY_NAME)$(COLOR_RESET)"

# Quick build (no optimizations)
quick:
	@$(GO) build -o $(BINARY_NAME) $(BINARY_NAME).go

# Show version info
version:
	@echo "Version:    $(VERSION)"
	@echo "Build Time: $(BUILD_TIME)"
	@echo "Git Commit: $(GIT_COMMIT)"

# Download dependencies
deps:
	@echo "$(COLOR_BLUE)Downloading dependencies...$(COLOR_RESET)"
	@$(GO) mod download
	@$(GO) mod tidy
	@echo "$(COLOR_GREEN)Dependencies updated$(COLOR_RESET)"

# Verify dependencies
verify:
	@echo "$(COLOR_BLUE)Verifying dependencies...$(COLOR_RESET)"
	@$(GO) mod verify
	@echo "$(COLOR_GREEN)Dependencies verified$(COLOR_RESET)"

# Generate embedded files info
info:
	@echo "$(COLOR_BOLD)Project Information$(COLOR_RESET)"
	@echo "Binary Name:    $(BINARY_NAME)"
	@echo "Version:        $(VERSION)"
	@echo "Build Time:     $(BUILD_TIME)"
	@echo "Git Commit:     $(GIT_COMMIT)"
	@echo "Build Dir:      $(BUILD_DIR)"
	@echo "Dist Dir:       $(DIST_DIR)"
	@echo ""
	@echo "$(COLOR_BOLD)Build Platforms:$(COLOR_RESET)"
	@$(foreach platform,$(PLATFORMS),echo "  - $(platform)";)