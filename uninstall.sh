#!/usr/bin/env bash
set -euo pipefail

if [ "${EUID}" -ne 0 ]; then
    exec sudo -- "$0" "$@"
fi

echo "==> Stopping and disabling system-temperature-monitoring.service..."
if command -v systemctl >/dev/null 2>&1; then
    systemctl stop system-temperature-monitoring.service 2>/dev/null || true
    systemctl disable system-temperature-monitoring.service 2>/dev/null || true
fi

echo "==> Removing installed files..."
rm -f /etc/systemd/system/system-temperature-monitoring.service
rm -f /etc/udev/rules.d/99-system-temperature-monitoring.rules
rm -f /usr/local/bin/system-temperature-monitoringd
rm -f /usr/local/bin/stmctl

if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload-rules || true
    udevadm trigger --subsystem-match=hidraw || true
fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl daemon-reload || true
fi

echo "Uninstalled binaries, udev rules, and systemd unit."
echo "Note: /etc/system-temperature-monitoring and /var/log/system-temperature-monitoring were preserved."
