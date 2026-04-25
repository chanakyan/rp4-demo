# rp4

User-space resource managers for Raspberry Pi 4 bare-metal, running on [qnx-micro](https://github.com/chanakyan/qnx-micro-demo).

## What

- 12 resource managers: audio, display, console, Ethernet, eMMC, USB audio, DRM, Bluetooth, HTTP config server, memory filesystem, process monitor, shell
- BCM2711 HAL: typed MMIO for I2S, GPIO, UART, Timer, Mailbox
- QNX Neutrino IPC model: every hardware resource is a server reached via message-passing
- Browser-based config UI (`dist/index.html`)
- Boots on real RPi4 hardware: `kernel8.img` (564 KB)

## Architecture

```
Phone browser → configsrv (HTTP) → PPS → audiomgr → PCM5122 DAC
                                       → displaymgr → HDMI
                                       → genetmgr → Ethernet
```

## Build

Full build requires vendored dependencies (Bionic libc, Poco, libc++) not included in this demo. The code is browsable and the architecture is documented by the source.

Build scripts: `scripts/seed_config.fsx` (tool detection → config.db) and `scripts/gen_config_module.fsx` (config.db → C++26 constexpr module).

## Verification

Z3 certificates in `proof/certs/`:

- **allocator.txt** — 7 assertions UNSAT
- **audio.txt** — 6 assertions UNSAT
- **proc.txt** — 7 assertions UNSAT

## License

BSD-2-Clause on all our code. GPL-3.0-or-later on Circle-derived drivers (`genetmgr.cpp`, `usbaudmgr.cpp`) — process-isolated via IPC, never statically linked with BSD code.

AI use prohibited. See [LICENSE-AI](LICENSE-AI.md).
