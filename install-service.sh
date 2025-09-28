#!/bin/bash

set -e

### CONFIGURATION ###
APP_NAME="usbctl"
BINARY_PATH="build/usbctl"
INSTALL_PATH="/usr/local/bin/usbctl"
CONFIG_DIR="/etc/usbctl"
LOG_FILE="/var/log/usbctl.log"
SYSTEMD_SERVICE="/etc/systemd/system/${APP_NAME}.service"
USBIPD_SERVICE="/etc/systemd/system/usbipd.service"  # 👈 新增
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

### 创建 usbipd systemd 服务 ###
create_usbipd_service() {
    if [ -f "$USBIPD_SERVICE" ]; then
        log "usbipd.service already exists, skipping creation."
        return
    fi

    # 自动查找 usbipd 路径（优先 /usr/sbin/）
    USBIPD_BIN=""
    for path in /usr/sbin/usbipd /usr/bin/usbipd /sbin/usbipd; do
        if [ -x "$path" ]; then
            USBIPD_BIN="$path"
            break
        fi
    done

    if [ -z "$USBIPD_BIN" ]; then
        error "usbipd executable not found. Please install usbip package."
    fi

    cat > "$USBIPD_SERVICE" <<EOF
[Unit]
Description=USB/IP Daemon
After=network.target systemd-modules-load.service
Before=usbctl.service

[Service]
Type=simple
ExecStart=$USBIPD_BIN
Restart=always
RestartSec=5
User=root

[Install]
WantedBy=multi-user.target
EOF

    systemctl daemon-reload
    systemctl enable --now usbipd
    log "usbipd.service created with ExecStart=$USBIPD_BIN"
}

### 4. Start usbipd → 替换为依赖 systemd 服务 ###
start_usbipd() {
    # 不再手动启动，全部交由 systemd 管理
    log "Ensuring usbipd.service is active..."
    if ! systemctl is-active --quiet usbipd; then
        systemctl start usbipd || error "Failed to start usbipd.service"
    fi
    log "usbipd is running via systemd."
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

### 7. Create systemd service for usbctl → 添加对 usbipd.service 的依赖 ###
create_systemd_service() {
    cat > "$SYSTEMD_SERVICE" <<EOF
[Unit]
Description=USB/IP Device Web Manager (usbctl)
Documentation=https://github.com/suifei/usbctl
After=network.target usbipd.service
Requires=usbipd.service

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

### 9. Uninstall → 新增清理 usbipd.service ###
uninstall() {
    log "Starting usbctl uninstall..."

    # Stop and disable usbctl
    if systemctl is-active --quiet "$APP_NAME"; then
        systemctl stop "$APP_NAME"
    fi
    if systemctl is-enabled --quiet "$APP_NAME"; then
        systemctl disable "$APP_NAME"
    fi
    rm -f "$SYSTEMD_SERVICE"

    # Stop and disable usbipd (only if we created it)
    if [ -f "$USBIPD_SERVICE" ]; then
        if systemctl is-active --quiet usbipd; then
            systemctl stop usbipd
        fi
        if systemctl is-enabled --quiet usbipd; then
            systemctl disable usbipd
        fi
        rm -f "$USBIPD_SERVICE"
        systemctl daemon-reload
        log "Removed custom usbipd.service."
    fi

    rm -f "$INSTALL_PATH"
    # rm -rf "$CONFIG_DIR"  # 保留配置
    rm -f "$LOG_FILE"
    rm -f "$LOGROTATE_FILE"
    rm -f "$MODULES_FILE"

    log "✅ usbctl uninstalled (config files remain in $CONFIG_DIR)"
}

### MAIN FLOW ###
case "${1:-install}" in
    install)
        log "🚀 Starting usbctl deployment..."
        check_port 11980
        install_usbip
        setup_usbip_module
        create_usbipd_service   # 👈 新增：创建服务
        start_usbipd            # 👈 现在只是确保服务运行
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