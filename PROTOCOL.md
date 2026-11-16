# Reverse-Engineered USB HID & IPC Protocol (`0145:1001` HWCX USB Display)

Extracted and verified from `SystemTemperatureMonitoring-English-V2.6-Setup.zip`:
- `SystemTemperatureMonitoring.exe` (.NET Windows Service, port `8890`)
- `XKWLib.dll` (.NET service/telemetry orchestration + UDP server + VID/PID table)
- `HardwareHelp64.dll` (.NET CPUID SDK / WMI / memory collector)
- `TempComm.dll` (Native x86-64 USB HID communication library)

---

## 1. USB Device Descriptor (`0145:1001`)

- **Vendor / Product**: `0x0145:0x1001` (`HWCX` / `USB Display`)
- **USB Class**: HID (`bInterfaceClass = 3`, `bInterfaceSubClass = 0`, `bInterfaceProtocol = 0`)
- **Endpoints**:
  - `EP 1 IN` (`0x81`, Interrupt, 64 bytes)
  - `EP 3 OUT` (`0x03`, Interrupt, 64 bytes)
- **HID Report Descriptor** (31 bytes):
  `05 01 09 00 a1 01 15 00 25 ff 19 01 29 08 95 40 75 08 81 02 19 01 29 08 95 40 75 08 91 02 c0`
  - Unnumbered 64-byte Input Report and 64-byte Output Report (`Report ID 0`).
  - Writing to Linux `/dev/hidrawX` sends 65 bytes (`0x00` report ID + 64-byte payload).
  - Reading from Linux `/dev/hidrawX` yields 64 bytes (`payload[0..63]`).

### Supported VID/PID Profiles (`XKWLib.PIDVID`)

| VID | PID | Windows Service Profile |
|---|---|---|
| `0x0145` | `0x1001` | `SystemTemperatureMonitoring` (1stPlayer Thunderstorm TS4 360-bk) |
| `0x12b7` | `0x1001` | `XKWHardwareServices` |
| `0x1c57` | `0x3ed1` | `ZALMAN-CTM` |
| `0x1c57` | `0x4b8f` | `ZALMAN-CTM` |
| `0x1c57` | `0x3a7f` | `ZALMAN-CTM` |
| `0x1c57` | `0x3ed2` | `ZALMAN-TDM` |
| `0x1c57` | `0x3ed6` | `ZALMAN-TDM` |
| `0x2c65` | `0x1001` | `SLAYERCPULiveTemperature` |
| `0x2c65` | `0x1000` | `RM-Hardware` |
| `0x0145` | `0x1005` | `LK Digital Display` |

---

## 2. Command `0x01`: Challenge-Response Handshake (`TempComm.dll @ 0x1800010f0`)

Performed when opening a device before sending telemetry.

### Request (64-byte payload `p[0..63]`)
- `p[0..63]` initialized with random bytes.
- `p[0] = 0x01`, `p[1] = 0x00`, `p[2] = 0x00`, `p[3] = 0x00`
- Three 16-bit big-endian challenges from random bytes:
  - `K1 = (p[4] << 8) | p[5]`
  - `K2 = (p[6] << 8) | p[7]`
  - `K3 = (p[8] << 8) | p[9]`
- Outgoing auth token (`p[10..11]`, big-endian):
  - `AuthOut = K1 ^ ((K2 + K3) & 0xFFFF)`
- Checksums:
  - `p[62] = XOR_{i=0..59} ((i * p[i]) & 0xFF)`
  - `p[63] = SUM_{i=0..62} ((i * p[i]) & 0xFF) & 0xFF` (includes `p[62]`)

### Reply (64-byte payload `r[0..63]`)
- Expected reply auth token at `(r[4] << 8) | r[5]`:
  - `AuthIn = K3 ^ ((K1 + K2) & 0xFFFF)`
- Checksums `r[62]` and `r[63]` must satisfy the exact same XOR and ADD formulas.

---

## 3. Command `0x02`: Telemetry + Timestamp (`TempComm.dll @ 0x1800019dc`)

Sent every `send_delay` ms (default `500 ms`) with no reply read. All multi-byte integers are **Big-Endian**.

| Payload Offset | Size | Field | Encoding / Units |
|---|---|---|---|
| `0` | 1 | Command ID | `0x02` |
| `1` | 1 | CPU Temperature | `uint8` (°C) |
| `2` | 1 | GPU Temperature | `uint8` (°C) |
| `3` | 1 | Hour | `0..23` |
| `4` | 1 | Minute | `0..59` |
| `5` | 1 | Second | `0..59` |
| `6` | 1 | Centisecond | `ms / 10` (`0..99`) |
| `7` | 1 | Year High | `year / 100` (e.g. `20`) |
| `8` | 1 | Year Low | `year % 100` (e.g. `26`) |
| `9` | 1 | Month | `1..12` |
| `10` | 1 | Day | `1..31` |
| `11` | 1 | DayOfWeek | `0=Sun..6=Sat` (.NET `DayOfWeek`) |
| `12` | 1 | CPU Load | `uint8` (`0..100` %) |
| `13..14` | 2 | CPU Clock | `uint16 BE` (MHz) |
| `15` | 1 | GPU Load | `uint8` (`0..100` %) |
| `16..17` | 2 | GPU Clock | `uint16 BE` (MHz) |
| `18` | 1 | RAM Temperature | `uint8` (°C; `0` in Windows `XKWLib`) |
| `19` | 1 | RAM Usage | `uint8` (`0..100` %, `100 - avail_pct`) |
| `20..21` | 2 | RAM Clock | `uint16 BE` (MHz; `0` in Windows `XKWLib`) |
| `22` | 1 | Disk Temperature | `uint8` (°C; `0` in Windows `XKWLib`) |
| `23` | 1 | Disk Load | `uint8` (`0..100` %) |
| `24..25` | 2 | CPU Fan Speed | `uint16 BE` (RPM) |
| `26` | 1 | CPU Voltage (Int) | `uint8` (`int(V)`) |
| `27` | 1 | CPU Voltage (Frac) | `uint8` (`int(V * 100) % 100`) |
| `28..29` | 2 | CPU Power | `uint16 BE` (tenths of a Watt, `W * 10`) |
| `30..31` | 2 | GPU Fan | `uint16 BE` (PWM % in `XKWLib`) |
| `32` | 1 | GPU Voltage (Int) | `uint8` (`0` in Windows `XKWLib`) |
| `33` | 1 | GPU Voltage (Frac) | `uint8` (`0` in Windows `XKWLib`) |
| `34..35` | 2 | GPU Power | `uint16 BE` (tenths of a Watt; `0` in `XKWLib`) |
| `36..39` | 4 | Reserved | `0x00000000` |
| `40..43` | 4 | Net Upload Speed | `uint32 BE` (Bytes/sec) |
| `44..47` | 4 | Net Download Speed | `uint32 BE` (Bytes/sec) |
| `48` | 1 | Display Mode | `SetDisplayMode(a, b)`: `(a==0) ? (b \| 0x80) : b` |
| `49..63` | 15 | Padding | `0x00` |

---

## 4. UDP Local Control / Telemetry Server (`port 8890`, `XKWLib.UdpCommand`)

Packet framing:
`[FF FE FF FD] [cmdType:1B] [cmdNo:2B BE] [UTF-8 "cmd|data"] [FF FE FF FD]`

- Requests (`cmdType=0`): `"rundata"`, `"staticdata"`, `"open"`, `"stop"` (+ Linux extensions `"status"`, `"setmode"`, `"reload"`).
- Responses (`cmdType=1`): same `cmdNo`, JSON payload after `|`.
