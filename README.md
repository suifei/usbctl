# usbctl - USB/IP Web Manager

A lightweight, single-file USB/IP device web manager designed for ARM Linux devices like Raspberry Pi.

## ✨ Features

- **Single executable file** (~300KB static build)
- **Zero dependencies** (all web assets embedded)
- **Real-time updates** via WebSocket
- **ARM Linux optimized** (Pi 3, Pi 4, Pi 5 supported)
- **CLI management** with rich parameters
- **Auto-configuration** with persistence
- **Systemd integration** for service deployment
- **Responsive web UI** with embedded SVG logo

## 🚀 Quick Start

### Build

```bash
# For current architecture
./build.sh native

# For Raspberry Pi 4/5 (ARM64)
./build.sh arm64

# For Raspberry Pi 3/Zero 2 (ARMv7)
./build.sh armv7

# Build all targets
./build.sh all
```

### Run

```bash
# Start web server (default port 5000)
./usbctl

# Custom port and bind address
./usbctl -p 8080 -b 192.168.1.100

# Show available devices
./usbctl --list

# Bind/unbind devices via CLI
./usbctl --bind 1-1.1
./usbctl --unbind 1-1.1
```

### Install as Service

```bash
# Install systemd service (requires root)
sudo ./usbctl --install-service

# Enable and start service
sudo systemctl enable usbctl
sudo systemctl start usbctl

# Check service status
sudo systemctl status usbctl
```

## 🔧 Configuration

Configuration is automatically saved to `~/.config/usbctl/config`:

```ini
port=5000
bind=0.0.0.0
poll_interval=3
```

### CLI Options

```bash
Usage: usbctl [OPTIONS] [COMMANDS]

Web Server Options:
  -p, --port PORT        Server port (default: 5000)
  -b, --bind ADDRESS     Bind address (default: 0.0.0.0)
  -i, --interval SEC     Polling interval (default: 3)
  -c, --config PATH      Configuration file path
  -d, --daemon           Run as daemon (background)

Commands:
  --list                 List USB devices (JSON format)
  --bind BUSID           Bind USB device
  --unbind BUSID         Unbind USB device
  --init-config          Create default configuration
  --print-config         Show current configuration
  --install-service      Install systemd service
  --version              Show version information
  --help                 Show this help
```

## 🌐 Web Interface

Access the web interface at `http://device-ip:5000`

The interface provides:
- **Real-time device list** with auto-refresh
- **One-click bind/unbind** operations
- **Device status indicators** (bound/unbound)
- **Responsive design** for mobile devices
- **WebSocket live updates** (no page refresh needed)

## 🛠 Requirements

### Runtime Requirements
- Linux with USB/IP support (kernel module `usbip-host`)
- `usbip` command-line tools installed
- Root privileges for binding/unbinding devices

### Build Requirements
- GCC with static linking support
- For cross-compilation:
  - `gcc-aarch64-linux-gnu` (ARM64)
  - `gcc-arm-linux-gnueabihf` (ARMv7)

## 📦 Raspberry Pi Installation

### 1. Install USB/IP tools
```bash
sudo apt update
sudo apt install linux-tools-generic usbip
```

### 2. Load kernel module
```bash
sudo modprobe usbip-host
echo 'usbip-host' | sudo tee -a /etc/modules
```

### 3. Deploy usbctl
```bash
# Download or build usbctl for your Pi model
wget https://github.com/your-repo/usbctl/releases/download/v1.0.0/usbctl-arm64

# Make executable and move to system path
chmod +x usbctl-arm64
sudo mv usbctl-arm64 /usr/local/bin/usbctl

# Install as service
sudo usbctl --install-service
sudo systemctl enable usbctl
sudo systemctl start usbctl
```

### 4. Access web interface
Open `http://raspberry-pi-ip:5000` in your browser.

## 🔍 Troubleshooting

### Permission Issues
```bash
# Ensure user has access to USB devices
sudo usermod -a -G dialout $USER

# Or run as root for binding operations
sudo usbctl
```

### Service Not Starting
```bash
# Check service logs
sudo journalctl -u usbctl -f

# Check if usbip tools are installed
usbip version

# Verify kernel module is loaded
lsmod | grep usbip
```

### Web Interface Not Loading
```bash
# Check if port is available
sudo netstat -tlnp | grep 5000

# Test with different port
usbctl -p 8080

# Check firewall settings
sudo ufw allow 5000
```

## 📊 Performance

| Platform | Binary Size | RAM Usage | CPU Usage |
|----------|-------------|-----------|-----------|
| ARM64 | ~280KB | ~4MB | <1% |
| ARMv7 | ~290KB | ~4MB | <1% |
| x86_64 | ~270KB | ~3MB | <1% |

## 🔒 Security Notes

- **Default configuration** binds to all interfaces (0.0.0.0)
- For production use, consider:
  - Binding to specific interface (`-b 192.168.1.100`)
  - Using reverse proxy with HTTPS
  - Implementing authentication (future feature)
  - Firewall rules for port access

## 🚧 Development

### Code Structure
- **Single-file design** for maximum portability
- **Embedded resources** (HTML/CSS/JS as string constants)
- **Minimal HTTP server** with WebSocket support
- **POSIX threading** for concurrent handling
- **No external dependencies** (static linking)

### Extending
The code is designed to be easily extended:
- Add new API endpoints in `handle_client()`
- Modify web UI by editing embedded strings
- Add configuration options in `config_t` structure

## 📝 License

MIT License - see LICENSE file for details.

## 🤝 Contributing

1. Fork the repository
2. Create feature branch
3. Make changes to `usbctl.c`
4. Test on target platforms
5. Submit pull request

## 📋 TODO

- [ ] Add authentication support
- [ ] HTTPS/TLS support
- [ ] Device filtering and grouping
- [ ] Export/import configuration
- [ ] Docker container version
- [ ] Prometheus metrics endpoint