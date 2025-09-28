English | [中文](README.md)

# usbctl - USB/IP Device Web Manager

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
├── build/           # Build output directory, contains usbctl executable
│   └── usbctl       # Main program, compiled (~944KB)
├── LICENSE          # License
├── Makefile         # Build script, multi-platform support
├── usbctl.c         # Main source code
├── usbctl.jpg       # UI screenshot
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

## Recommended Windows Client

Use Microsoft WHLK-certified usbip-win2 project:

- https://github.com/vadimgrn/usbip-win2

Or official usbipd-win (WSL support):

- https://github.com/dorssel/usbipd-win

## Additional Notes

- Supports systemd service install: `./build/usbctl --install-service`
- Default config path: `~/.config/usbctl/config`
- Default log path: `/var/log/usbctl.log`
- More command-line options: `./build/usbctl --help`

## License

MIT License, see [`LICENSE`](LICENSE).

---

## 中文版

请参考 `README.md`。

---

For more help or feedback, visit [github.com/suifei/usbctl](https://github.com/suifei/usbctl).