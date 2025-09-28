
English | [中文](README.md)

# usbctl - USB/IP Device Web Manager

Architecture

![architecture](./architecture.svg)

![usbctl responsive UI screenshot](usbctl.jpg)

## Project Overview

**usbctl** is a lightweight, cross-platform USB/IP device management web interface, supporting both PC and mobile (responsive design). It integrates device listing, bind/unbind operations, real-time status push (SSE), operation logs, and is suitable for Linux (including Raspberry Pi), Windows (WSL/usbipd-win), and macOS.

- Single-file C implementation
- Embedded HTML/CSS/JS resources, no external dependencies
- Persistent configuration, auto-recover bindings
- Real-time device status push, AJAX operations
- ARM Linux optimized, ideal for Raspberry Pi and embedded devices

## Directory Structure

```
usbctl/
├── build/             # Build output directory, contains usbctl executable
│   └── usbctl         # Main program, compiled (~944KB)
├── LICENSE            # License
├── Makefile           # Build script, multi-platform support
├── usbctl.c           # Main source code
├── usbctl.jpg         # UI screenshot
├── install-service.sh # Automated deployment script (recommended)
```

## Build Instructions

Recommended to use Makefile:

```sh
make           # Build static binary for local platform
# Or manual build
gcc -static -O2 -o build/usbctl usbctl.c -lpthread
```

Cross-compilation for multiple platforms is supported, see Makefile.

## Deployment & Setup (Tested on Raspberry Pi OS arm64)

### 1. Run usbctl to check dependencies

```sh
./build/usbctl
# If usbip not found, install usbip tool
```

### 2. Install usbip tool

For Debian/Ubuntu/Raspberry Pi OS:

```sh
sudo apt update
sudo apt install usbip
```

### 3. Verify usbip tool

```sh
usbip
# If help message appears, installation is successful
```

### 4. Check USB devices

```sh
lsusb -t
# View bus and device info
```

### 5. Load kernel driver

```sh
sudo modprobe usbip-host
lsmod | grep usbip
# Should show usbip_host and usbip_core modules loaded
```

### 6. Start usbipd service

```sh
sudo usbipd -D
ps -Al | grep usb
sudo netstat -tlnp | grep :3240
# Confirm usbipd is listening on port 3240
```

### 7. Open firewall port (if needed)

```sh
sudo ufw allow 3240/tcp
# Or check iptables
sudo iptables -L
```

### 8. Start usbctl web service

```sh
sudo ./build/usbctl
# Default port is 11980, customizable
```

Access via browser:

- http://localhost:11980
- http://<raspberrypi_ip>:11980

---

## 🚀 Automated Deployment Script (Recommended)

To simplify deployment, the project provides a **one-click install/uninstall script** `install-service.sh`, suitable for **Debian / Ubuntu / Raspberry Pi OS (ARM64)** systems.

This script will automatically perform the following operations:

- ✅ Detect and install the `usbip` tool (compatible with Raspberry Pi OS and generic Debian)
- ✅ Automatically load and persist the `usbip-host` kernel module (auto-load on boot)
- ✅ Start the `usbipd -D` daemon (compatible with systems without systemd service)
- ✅ Open firewall ports `3240/tcp` (usbipd) and `11980/tcp` (usbctl)
- ✅ Install `usbctl` to `/usr/local/bin/usbctl`
- ✅ Create a standardized config file: `/etc/usbctl/config`
- ✅ Configure log rotation (`/var/log/usbctl.log`)
- ✅ Install and enable **systemd service** for auto-start on boot
- ✅ Automatically detect and clean up port conflicts (avoid "Address already in use")

### Usage Steps

#### 1. Get the deployment script

Save the following script as `install-service.sh` in the project root directory ([Click here for script content](#) or download from the repository).

#### 2. Make it executable

```bash
chmod +x install-service.sh
```

#### 3. Run installation

```bash
sudo ./install-service.sh install
```

Sample output:
```
[INFO] 🚀 Starting usbctl deployment...
[INFO] usbip installed
[INFO] Written /etc/modules-load.d/usbip.conf for auto-load on boot
[WARN] usbipd.service not found, starting usbipd -D manually.
[INFO] Opened port 3240/tcp (UFW)
[INFO] Opened port 11980/tcp (UFW)
[INFO] Default config file created: /etc/usbctl/config
[INFO] systemd service enabled and started.
[INFO] Configured logrotate: /etc/logrotate.d/usbctl
[INFO] ✅ Deployment complete! Visit http://192.168.x.x:11980
```

#### 4. Verify service status

```bash
# Check service status
systemctl status usbctl

# View real-time logs
journalctl -u usbctl -f

# Check port listening
ss -tlnp | grep -E ':(3240|11980)'
```

#### 5. Uninstall (optional)

```bash
sudo ./install-service.sh uninstall
```

> ⚠️ Uninstalling will keep the `/etc/usbctl/config` config file to avoid deleting user custom settings. To completely remove, please delete this directory manually.

### Config File

After installation, the config file is located at:

```ini
/etc/usbctl/config
```

Default content:
```ini
port=11980
bind=0.0.0.0
poll_interval=3
verbose_logging=1
log_file=/var/log/usbctl.log
```

Restart the service after modification:
```bash
sudo systemctl restart usbctl
```

### Troubleshooting

| Issue | Solution |
|-------|----------|
| `Bind failed: Address already in use` | The script auto-detects and cleans up. If still occurs, run `sudo pkill -f usbctl` manually |
| `usbip: command not found` | The script will auto-install. If it fails, run `sudo apt install usbip` manually |
| Cannot access web UI | Check firewall: `sudo ufw status`, ensure port 11980 is open |
| Device not shown | Ensure USB device is plugged in and `usbip list -l` can list devices |

> ✅ **Recommended**: On Raspberry Pi and embedded devices, use this script for deployment to avoid missing manual steps.

---

## Recommended Windows Client

Use Microsoft WHLK-certified usbip-win2 project:

- https://github.com/vadimgrn/usbip-win2  

Or official usbipd-win (WSL support):

- https://github.com/dorssel/usbipd-win  

## Additional Notes

- Supports systemd service install: `./build/usbctl --install-service`
- Default config path: `~/.config/usbctl/config` (**after script deployment, changed to `/etc/usbctl/config`**)
- Default log path: `/var/log/usbctl.log`
- More command-line options: `./build/usbctl --help`

## License

MIT License, see [`LICENSE`](LICENSE).

---

For more help or feedback, visit [github.com/suifei/usbctl](https://github.com/suifei/usbctl).
