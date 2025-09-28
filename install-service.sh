#!/bin/bash

set -e

### CONFIGURATION ###
APP_NAME="usbctl"
BINARY_PATH="build/usbctl"
INSTALL_PATH="/usr/local/bin/usbctl"
CONFIG_DIR="/etc/usbctl"
LOG_FILE="/var/log/usbctl.log"
SYSTEMD_SERVICE="/etc/systemd/system/${APP_NAME}.service"
MODULES_FILE="/etc/modules-load.d/usbip.conf"
LOGROTATE_FILE="/etc/logrotate.d/usbctl"

FIREWALL_PORTS=("3240" "11980")

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1" >&2; exit 1; }

if [[ $EUID -ne 0 ]]; then
    error "Please run this script with sudo."
fi

### 1. Check port usage ###
check_port() {
    local port=$1
    if ss -tlnp | grep -q ":$port "; then
        warn "Port $port is already in use, trying to kill old process..."
        pkill -f "usbctl" 2>/dev/null || true
        sleep 2
        if ss -tlnp | grep -q ":$port "; then
            error "Port $port is still in use. Please run 'sudo pkill -f usbctl' manually and retry."
        fi
    fi
}

### 2. Install usbip tool ###
install_usbip() {
    if command -v usbip &> /dev/null; then
        log "usbip tool already installed."
        return
    fi

    log "Installing usbip tool..."
    if apt list --installed 2>/dev/null | grep -q "raspberrypi"; then
        if apt install -y usbip; then
        log "✅ Installed via usbip package."
            return
        fi
    fi

    local kernel_pkg="linux-tools-$(uname -r)"
    if apt-cache search "^${kernel_pkg}$" | grep -q linux-tools; then
        log "Trying to install kernel-matched linux-tools: $kernel_pkg"
        apt install -y "$kernel_pkg" && return
    fi

    if apt install -y linux-tools-generic; then
        log "✅ Installed via linux-tools-generic."
        return
    fi

    error "Failed to install usbip tool, please install manually."
}

### 3. Load and persist usbip-host module ###
setup_usbip_module() {
    log "Loading usbip-host kernel module..."
    modprobe usbip-host || error "Failed to load usbip-host module."
    echo "usbip-host" > "$MODULES_FILE"
    log "Written $MODULES_FILE for auto-load on boot."
}

### 4. Start usbipd ###
start_usbipd() {
    if pgrep -x usbipd > /dev/null; then
        log "usbipd is already running."
        return
    fi

    if systemctl list-unit-files | grep -q "^usbipd\.service"; then
        log "Enable and start system usbipd.service"
        systemctl enable --now usbipd
    else
        warn "usbipd.service not found, starting usbipd -D manually."
        usbipd -D
    fi
}

### 5. Firewall configuration ###
configure_firewall() {
    for port in "${FIREWALL_PORTS[@]}"; do
        if command -v ufw &> /dev/null && ufw status | grep -q "Status: active"; then
            ufw allow "$port/tcp" >/dev/null 2>&1 && log "Opened port $port/tcp (UFW)"
        elif command -v iptables &> /dev/null; then
            iptables -C INPUT -p tcp --dport "$port" -j ACCEPT 2>/dev/null || \
            iptables -A INPUT -p tcp --dport "$port" -j ACCEPT
            log "Opened port $port via iptables."
        fi
    done
}

### 6. Install binary and config files ###
install_binary() {
    [[ -f "$BINARY_PATH" ]] || error "Binary file $BINARY_PATH not found."
    install -m 755 "$BINARY_PATH" "$INSTALL_PATH"
    mkdir -p "$CONFIG_DIR"

    if [ ! -f "$CONFIG_DIR/config" ]; then
        cat > "$CONFIG_DIR/config" <<'EOF'
port=11980
bind=0.0.0.0
poll_interval=3
verbose_logging=1
log_file=/var/log/usbctl.log
EOF
        chown root:root "$CONFIG_DIR/config"
        chmod 644 "$CONFIG_DIR/config"
        log "Default config file created: $CONFIG_DIR/config"
    fi
}

### 7. Create systemd service ###
create_systemd_service() {
    cat > "$SYSTEMD_SERVICE" <<EOF
[Unit]
Description=USB/IP Device Web Manager (usbctl)
Documentation=https://github.com/suifei/usbctl
After=network.target

[Service]
Type=simple
User=root
ExecStart=$INSTALL_PATH
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
Environment=PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable "$APP_NAME"
    systemctl start "$APP_NAME"
    log "systemd service enabled and started."
}

### 8. Setup logrotate ###
setup_logrotate() {
    cat > "$LOGROTATE_FILE" <<EOF
/var/log/usbctl.log {
    daily
    missingok
    rotate 7
    compress
    delaycompress
    notifempty
    create 644 root root
}
EOF
    log "Configured logrotate: $LOGROTATE_FILE"
}

### 9. Uninstall ###
uninstall() {
    log "Starting usbctl uninstall..."

    if systemctl is-active --quiet "$APP_NAME"; then
        systemctl stop "$APP_NAME"
    fi
    if systemctl is-enabled --quiet "$APP_NAME"; then
        systemctl disable "$APP_NAME"
    fi
    rm -f "$SYSTEMD_SERVICE"

    rm -f "$INSTALL_PATH"
    # Optionally remove config directory
    # rm -rf "$CONFIG_DIR"
    rm -f "$LOG_FILE"
    rm -f "$LOGROTATE_FILE"
    rm -f "$MODULES_FILE"

    systemctl daemon-reload
    log "✅ usbctl uninstalled (config files remain in $CONFIG_DIR)"
}

### MAIN FLOW ###
case "${1:-install}" in
    install)
    log "🚀 Starting usbctl deployment..."
    check_port 11980
    install_usbip
    setup_usbip_module
    start_usbipd
    configure_firewall
    install_binary
    create_systemd_service
    setup_logrotate
    log "✅ Deployment complete! Visit http://$(hostname -I | awk '{print $1}'):11980"
        ;;
    uninstall)
        uninstall
        ;;
    *)
    echo "Usage: $0 [install|uninstall]"
    exit 1
        ;;
esac