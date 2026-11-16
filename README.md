# 1stPlayer® Thunderstorm TS4 360-bk — Native Linux Display Driver

**[English](README.md)** | **[🇷🇴 Română (Ghid Complet)](README.ro.md)** | **[USB HID & IPC Protocol](PROTOCOL.md)**

> 🇷🇴 **Folosești Linux în România?** Vezi secțiunea **[Instalare rapidă în română](#-pentru-utilizatorii-din-românia-română)** mai jos sau ghidul complet în **[README.ro.md](README.ro.md)**.

A clean-room, native Linux C daemon, CLI controller (`stmctl`), `udev` ruleset, and `systemd` service for the **1stPlayer® Thunderstorm TS4 360-bk** IPS USB display (`0145:1001` `HWCX USB Display`) and compatible XKW-family AIO/cooler displays.

Built from a full reverse engineering of the official Windows `SystemTemperatureMonitoring V2.6` driver (`SystemTemperatureMonitoring.exe`, `XKWLib.dll`, `HardwareHelp64.dll`, `TempComm.dll`), providing **1:1 protocol parity** on Linux without Wine, Mono, or Windows services.

---

## Features

- **1:1 Windows HID Protocol Parity**:
  - Exact `0x01` challenge-response handshake (`K1 ^ (K2 + K3)` / `K3 ^ (K1 + K2)` + XOR & ADD checksums).
  - Exact `0x02` 64-byte big-endian telemetry + wall-clock report every `500 ms`.
  - Supports `SetDisplayMode` (`display_mode` / `display_submode`).
- **Native Linux Hardware Telemetry** (zero external runtime dependencies):
  - **CPU**: Temperature (`k10temp`, `zenpower`, `coretemp`, SuperIO fallback), Load (`%` via `/proc/stat`), Max Core Clock (`cpufreq`), Package Power (`intel-rapl:0` / `amd_energy`), Vcore & CPU Fan RPM (`nct67*`, `it87*`, `asus-ec-sensors`).
  - **GPU**: AMD (`amdgpu` sysfs/hwmon), NVIDIA (`nvidia-smi`), and Intel (`i915`/`xe`).
  - **RAM & Storage**: Usage `%` (`/proc/meminfo`), `spd5118`/`jc42` RAM temp, `nvme` temp, and `/proc/diskstats` activity `%` (with stuck-queue USB-SATA bridge protection).
  - **Network**: Upload & Download `Bytes/sec` (`/proc/net/dev`).
- **Ultra-Lightweight**: Pure C daemon (`~1 MB` RAM), `systemd` service (`Restart=always`), `udev` rule with `uaccess` permissions and USB autosuspend disabled (`power/control=on`).
- **Windows-Compatible UDP Server**: Listens on `127.0.0.1:8890` (`FF FE FF FD` framing) supporting `rundata`, `staticdata`, `open`, `stop`, plus `status` and `setmode`.

---

## Supported Devices (`VID:PID`)

| VID | PID | Windows Service Profile | Device |
|---|---|---|---|
| `0x0145` | `0x1001` | `SystemTemperatureMonitoring` | **1stPlayer Thunderstorm TS4 360-bk** / HWCX USB Display |
| `0x12b7` | `0x1001` | `XKWHardwareServices` | XKW Hardware Display |
| `0x1c57` | `0x3ed1` | `ZALMAN-CTM` | ZALMAN CTM Display |
| `0x1c57` | `0x4b8f` | `ZALMAN-CTM` | ZALMAN CTM Display v2 |
| `0x1c57` | `0x3a7f` | `ZALMAN-CTM` | ZALMAN CTM Display v3 |
| `0x1c57` | `0x3ed2` | `ZALMAN-TDM` | ZALMAN TDM Display |
| `0x1c57` | `0x3ed6` | `ZALMAN-TDM` | ZALMAN TDM Display v2 |
| `0x2c65` | `0x1001` | `SLAYERCPULiveTemperature` | SLAYER CPU Live Display |
| `0x2c65` | `0x1000` | `RM-Hardware` | RM Hardware Display |
| `0x0145` | `0x1005` | `LK Digital Display` | LK Digital Display |

> **Note on ARGB Lighting:** The `0145:1001` USB interface controls **only** the IPS LCD display (`0x01` + `0x02`). The cooler's ARGB LEDs connect to your motherboard's 5V 3-pin ARGB header and can be controlled natively on Linux using [OpenRGB](https://openrgb.org/).

---

## Quick Install

### Option A: Download from GitHub Releases (Recommended)

1. Download `1stplayer-ts4-linux-v1.0.0.tar.gz` from the **[Releases](../../releases)** page.
2. Extract and run the native installer:

```bash
tar -xzf 1stplayer-ts4-linux-v1.0.0.tar.gz
cd 1stplayer-ts4-linux-v1.0.0
sudo ./install.sh
```

### Option B: Build & Install from Git

**Prerequisites:** `gcc` and `make` (Arch/CachyOS: `sudo pacman -S --needed base-devel`; Debian/Ubuntu: `sudo apt install build-essential`).

```bash
git clone https://github.com/RaulKong878/1stplayer-ts4-linux.git
cd 1stplayer-ts4-linux
sudo ./install.sh
```

To build and run protocol unit tests without installing system-wide:
```bash
./install.sh --build-only
./build/system-temperature-monitoringd --dry-run
```

---

## CLI Usage (`stmctl`)

```bash
stmctl status                 # Check USB display, udev & live systemd daemon status
stmctl sensors                # Show all auto-detected Linux sensors & live readings
stmctl probe                  # Test 0x01 USB HID challenge-response handshake directly
stmctl send-once              # Send a single 0x02 telemetry frame
stmctl mode 0 0               # Switch display mode/submode (live + config.ini)
stmctl pause                  # Pause screen updates (UDP: stop)
stmctl resume                 # Resume screen updates (UDP: open)
stmctl udp rundata            # Query live JSON telemetry over UDP 8890
stmctl udp staticdata         # Query static system info over UDP 8890
```

---

## Configuration (`/etc/system-temperature-monitoring/config.ini`)

```ini
[config]
fefresh_delay = 500
send_delay = 500
IsLog = false
localPort = 8890
LogPath = /var/log/system-temperature-monitoring/log.txt
IsUdp = true

[linux]
bind_address = 127.0.0.1
display_mode = 1
display_submode = 0
cpu_temp_sensor = auto
cpu_fan_channel = auto
cpu_volt_sensor = auto
hidraw_device = auto
gpu_fan_mode = pwm
strict_windows_fields = false
```

- `cpu_fan_channel`: Set to `fan1`, `fan2`, `fan3`, or an explicit `/sys/class/hwmon/.../fanX_input` path if your motherboard uses a different header for CPU/pump RPM.
- `gpu_fan_mode`: `pwm` (`0–100%`, matching Windows `XKWLib`) or `rpm`.
- `strict_windows_fields`: Set `true` to zero RAM/Disk temp and GPU volt/watt like Windows `XKWLib`, or `false` (default) to populate them from Linux sysfs.

Reload config without restarting:
```bash
sudo systemctl reload system-temperature-monitoring
```

---

## Uninstall

```bash
sudo ./uninstall.sh
```

---

## 🇷🇴 Pentru utilizatorii din România (Română)

Driver nativ pentru Linux (daemon C + `systemd` + `udev` + `stmctl`) pentru ecranul IPS al cooler-ului **1stPlayer® Thunderstorm TS4 360-bk** (`0145:1001` `HWCX USB Display`), construit prin reverse engineering 1:1 după driverul oficial de Windows (`SystemTemperatureMonitoring V2.6`), fără Wine sau Mono.

### Instalare rapidă (din Releases)
1. Descarcă `1stplayer-ts4-linux-v1.0.0.tar.gz` din pagina **[Releases](../../releases)**.
2. Dezarhivează și rulează installer-ul:
```bash
tar -xzf 1stplayer-ts4-linux-v1.0.0.tar.gz
cd 1stplayer-ts4-linux-v1.0.0
sudo ./install.sh
```

### Comenzi utile
```bash
stmctl status      # Verifică ecranul USB și serviciul systemd
stmctl sensors     # Afișează toți senzorii (CPU, GPU, RAM, Disk, Ventilator)
stmctl probe       # Testează handshake-ul USB 0x01
stmctl mode 0 0    # Schimbă modul de afișare (dacă este necesar)
```

> **Important despre ARGB:** Cablul USB (`0145:1001`) controlează **doar ecranul IPS**. LED-urile ARGB se conectează pe header-ul de 5V 3-pin al plăcii de bază și pot fi controlate pe Linux cu **OpenRGB**.

Pentru toate detaliile și configurarea avansată în limba română, vezi **[README.ro.md](README.ro.md)**.
