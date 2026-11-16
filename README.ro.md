# 1stPlayer® Thunderstorm TS4 360-bk — Driver Nativ Linux pentru Ecran IPS

**[English](README.md)** | **[Română](README.ro.md)** | **[Specificație Protocol USB HID](PROTOCOL.md)**

Daemon C nativ pentru Linux, utilitar CLI (`stmctl`), reguli `udev` și serviciu `systemd` pentru ecranul IPS USB al cooler-ului **1stPlayer® Thunderstorm TS4 360-bk** (`0145:1001` `HWCX USB Display`) și display-urile compatibile din familia XKW.

Construit prin reverse engineering complet al driverului oficial de Windows `SystemTemperatureMonitoring V2.6` (`SystemTemperatureMonitoring.exe`, `XKWLib.dll`, `HardwareHelp64.dll`, `TempComm.dll`), oferind **compatibilitate 1:1** pe Linux fără Wine sau Mono.

---

## Funcționalități

- **Protocol HID 1:1 cu Windows**:
  - Handshake challenge-response `0x01` (`K1 ^ (K2 + K3)` / `K3 ^ (K1 + K2)` + checksum-uri `XOR` și `ADD`).
  - Pachet de telemetrie `0x02` pe 64 bytes (Big-Endian) trimis la fiecare `500 ms` cu toate valorile + data/ora.
  - Suport pentru `SetDisplayMode` (`display_mode` / `display_submode`).
- **Senzori nativi Linux** (fără dependențe externe):
  - **CPU**: Temperatură (`k10temp`, `zenpower`, `coretemp`, fallback SuperIO), Încărcare (`%` din `/proc/stat`), Frecvență maximă (`cpufreq`), Putere în Wați (`intel-rapl:0` / `amd_energy`), Voltaj Vcore și Turație ventilator (`nct67*`, `it87*`, `asus-ec-sensors`).
  - **GPU**: AMD (`amdgpu`), NVIDIA (`nvidia-smi`) și Intel (`i915`/`xe`).
  - **RAM & Disk**: Utilizare RAM `%` (`/proc/meminfo`), temperatură `spd5118`/`jc42`, temperatură `nvme` și activitate Disk `%` (`/proc/diskstats`).
  - **Rețea**: Viteză Upload / Download în `Bytes/sec` (`/proc/net/dev`).
- **Ultra-ușor**: consumă ~1 MB RAM, pornește automat cu `systemd`, include regulă `udev` (`uaccess` + dezactivare USB autosuspend).
- **Server UDP local (`8890`)**: compatibil cu protocolul `XKWLib` (`rundata`, `staticdata`, `open`, `stop`).

> **Notă despre ARGB:** Interfața USB `0145:1001` controlează **doar** ecranul IPS (`0x01` + `0x02`). Iluminarea ARGB se conectează pe header-ul de 5V 3-pin al plăcii de bază și se poate controla nativ pe Linux cu [OpenRGB](https://openrgb.org/).

---

## Instalare rapidă

### Varianta A: Din GitHub Releases (Recomandat)

1. Descarcă `1stplayer-ts4-linux-v1.0.0.tar.gz` din pagina **[Releases](../../releases)**.
2. Dezarhivează și rulează scriptul de instalare:

```bash
tar -xzf 1stplayer-ts4-linux-v1.0.0.tar.gz
cd 1stplayer-ts4-linux-v1.0.0
sudo ./install.sh
```

### Varianta B: Din Git

**Cerințe:** `gcc` și `make` (Arch/CachyOS: `sudo pacman -S --needed base-devel`; Debian/Ubuntu: `sudo apt install build-essential`).

```bash
git clone https://github.com/RaulKong878/1stplayer-ts4-linux.git
cd 1stplayer-ts4-linux
sudo ./install.sh
```

---

## Comenzi CLI (`stmctl`)

```bash
stmctl status                 # Verifică dispozitivul USB și serviciul systemd
stmctl sensors                # Afișează toți senzorii detectați și valorile live
stmctl probe                  # Testează direct handshake-ul USB 0x01
stmctl send-once              # Trimite un singur pachet de test către ecran
stmctl mode 0 0               # Schimbă modul de afișare (live + config.ini)
stmctl pause                  # Pune pauză la actualizări
stmctl resume                 # Reia actualizările
stmctl udp rundata            # Interoghează JSON-ul live pe UDP 8890
```

Fișierul de configurare este `/etc/system-temperature-monitoring/config.ini`. După editare:
```bash
sudo systemctl reload system-temperature-monitoring
```

Dezinstalare:
```bash
sudo ./uninstall.sh
```
