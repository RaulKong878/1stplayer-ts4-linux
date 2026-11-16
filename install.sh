#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
FORCE_CONFIG=0
BUILD_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --force-config) FORCE_CONFIG=1 ;;
        --build-only) BUILD_ONLY=1 ;;
        -h|--help)
            cat <<EOF
Usage: ./install.sh [options]
  --build-only     Compile binaries and run protocol unit tests without installing
  --force-config   Overwrite /etc/system-temperature-monitoring/config.ini with default
  -h, --help       Show this help
EOF
            exit 0
            ;;
        *)
            echo "Unknown option: $arg" >&2
            exit 2
            ;;
    esac
done

if ! command -v gcc >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    echo "Error: gcc and make are required. Install base-devel (Arch/CachyOS: sudo pacman -S --needed base-devel)." >&2
    exit 1
fi

echo "==> Building native driver & CLI in $SCRIPT_DIR..."
cd "$SCRIPT_DIR"

OPT_FLAGS="-O3 -march=native"
if ! make clean >/dev/null 2>&1 || ! make CFLAGS="$OPT_FLAGS -Wall -Wextra -Werror -pthread -D_GNU_SOURCE" all test; then
    echo "==> Retrying build with portable -O2..."
    make clean
    make CFLAGS="-O2 -Wall -Wextra -Werror -pthread -D_GNU_SOURCE" all test
fi

if [ "$BUILD_ONLY" -eq 1 ]; then
    echo "==> Build & tests succeeded (--build-only specified)."
    exit 0
fi

if [ "${EUID}" -ne 0 ]; then
    echo "==> Elevating privileges with sudo to install system service & udev rules..."
    exec sudo -- "$0" "$@"
fi

echo "==> Installing binaries to /usr/local/bin..."
install -Dm755 "$SCRIPT_DIR/build/system-temperature-monitoringd" /usr/local/bin/system-temperature-monitoringd
install -Dm755 "$SCRIPT_DIR/build/stmctl" /usr/local/bin/stmctl

echo "==> Installing configuration and log directories..."
install -d -m755 /etc/system-temperature-monitoring /var/log/system-temperature-monitoring
if [ ! -f /etc/system-temperature-monitoring/config.ini ] || [ "$FORCE_CONFIG" -eq 1 ]; then
    install -m644 "$SCRIPT_DIR/config/config.ini" /etc/system-temperature-monitoring/config.ini
    echo "    Installed /etc/system-temperature-monitoring/config.ini"
else
    echo "    Preserved existing /etc/system-temperature-monitoring/config.ini"
fi

echo "==> Installing udev rules..."
install -Dm644 "$SCRIPT_DIR/udev/99-system-temperature-monitoring.rules" /etc/udev/rules.d/99-system-temperature-monitoring.rules

echo "==> Installing systemd service..."
install -Dm644 "$SCRIPT_DIR/systemd/system-temperature-monitoring.service" /etc/systemd/system/system-temperature-monitoring.service

if command -v udevadm >/dev/null 2>&1; then
    echo "==> Reloading udev rules..."
    udevadm control --reload-rules
    udevadm trigger --subsystem-match=hidraw --subsystem-match=usb || true
fi

if command -v systemctl >/dev/null 2>&1; then
    echo "==> Enabling and starting system-temperature-monitoring.service..."
    systemctl daemon-reload
    systemctl enable system-temperature-monitoring.service
    systemctl restart system-temperature-monitoring.service
    sleep 1
    echo ""
    systemctl --no-pager --lines=12 status system-temperature-monitoring.service || true
fi

echo ""
echo "==> Verifying live status via stmctl:"
/usr/local/bin/stmctl status || true
echo ""
echo "Installation complete! Useful commands:"
echo "  stmctl status      # Check USB display and service status"
echo "  stmctl sensors     # Inspect all Linux hardware sensors"
echo "  stmctl probe       # Test 0x01 USB HID handshake directly"
echo "  stmctl mode 0 0    # Switch display mode if needed"
