# Debug Probe tools

Tools that build, flash, read and drive a real SidecarTridge running Booster through the
Raspberry Pi Debug Probe (SWD and its UART bridge), and a test server for Booster's downloads.
They never use the firmware's own services, so they work on a hung device too.

Requirements: Python 3.10 or later (standard library only), the Arm toolchain on `PATH`
(`arm-none-eabi-gcc`, `-nm`, `-objdump`, `-objcopy`, `-addr2line`, and `-gdb` for `postmortem`),
OpenOCD with the RP2040 target (`$OPENOCD`, `openocd` on `PATH`, or `../pico/openocd/src/openocd`
next to this repository), and a Debug Probe wired to the Pico W's SWD pins and to GPIO 0/1 for the
console.

## The tools

| Tool | What it does |
| --- | --- |
| `flash.sh release\|debug` | Builds Booster out of tree into `builds/<type>`, flashes `booster.elf` over the probe and checks over SWD that the RP runs it, that flash matches the ELF and that it carries the new build ID. `--build-only` stops after the build. |
| `swd.py` | Reads and drives the running RP: `running`, `verify`, `build-id`, `read`, `program`, `reset`, `resume`, `heap`, `crash`, `postmortem`, `screen`, `text`, `select`. `python3 tools/dev/swd.py --help` and the top of the file explain each. |
| `console.py` | Captures the debug console (debug builds only, 921,600 baud) to `logs/console.log`: `watch` in one terminal, then `tail`, `since-boot`, `grep` and `wait` from anywhere. |
| `testserver.py` | Catalogs and downloads over HTTP and HTTPS with every failure Booster must survive: errors, truncation, stalls, redirect loops, slow downloads, a wrong MD5, and `many-N` catalogs of N apps. |
| `stackdepth.py` | Worst-case stack depth from GCC's call graph (`.ci` files from `measure_builds.sh`). |
| `measure_builds.sh` | Out-of-tree MinSizeRel builds, release and debug, with section sizes, the flash report, the heap size and the call-graph files. |

A typical change:

```sh
tools/dev/flash.sh debug                 # build, flash, verify, build ID
python3 tools/dev/console.py watch &     # console of the debug build
python3 tools/dev/swd.py screen /tmp/s.png   # what the Atari shows
python3 tools/dev/swd.py postmortem      # if it hangs: both cores' backtraces
```

Build output, cached ELFs (`builds/elf/<type>-<build id>.elf`, which `swd.py` matches against the
device's build ID) and logs stay out of git.

## Rules

- **Flash with `flash.sh` or `swd.py program`, not OpenOCD's `program`.** They halt both cores and
  stop every PIO state machine and DMA channel before writing: the cartridge emulation's DMA keeps
  writing RAM while the cores are halted, and a flash write staged in that RAM programs bus
  samples instead of code. Only Booster's region, from `0x10120000`, is written; the
  microfirmware slot and all settings stay as they are.
- **Power-cycle before any network check.** `program` and `reset` restart the RP2040 through the
  watchdog, which does not reset the Pico W's CYW43 radio. WiFi may not come back until the whole
  system is power-cycled. The tools print a reminder.
- **`postmortem` and `read` halt or touch the bus.** `postmortem` stops both cores for a few seconds,
  so the ST sees a dead cartridge meanwhile; everything else reads while the RP runs.
- **`select long --force` is a factory reset.**

## Differences from the template

The tools are kept as close to their sources as Booster allows, so a fix travels both ways with a
plain diff:

- Paths: `booster/src`, `booster.elf`, Booster's `build_id.cmake`.
- OpenOCD's work area is SCRATCH_Y (`0x20041000`), not SCRATCH_X: Booster runs core 1 (the SELECT
  watcher in `select.c`) with its stack in SCRATCH_X, and leaves SCRATCH_Y unused.
- `crash` and `postmortem` resolve code addresses across the whole 2 MB flash: Booster runs from
  `0x10120000`, above the template's range.
- `postmortem` starts OpenOCD's GDB server without its memory map, md-rom-emulator's fix: building
  the map probes the flash bank and left the flash out of XIP, which stopped the firmware.
- `postmortem` prints Booster's state: the terminal's last command, and the download, launch and
  firmware-update state.
- `running` cannot tell Booster from the placeholder, which boots first and keeps its vector table
  at the same RAM address. `flash.sh` therefore also checks `verify` and `build-id`.
- `flash.sh` always flashes over the probe (picotool cannot see Booster, which has no USB stdio),
  and builds MinSizeRel, the type Booster ships; Release does not fit its slot.
- `console.py` recognises Booster's banner, `SidecarTridge Booster. v2.5.0 (date). DEBUG mode.`
  Booster's debug builds use the microfirmwares' 921,600 baud, as console.py expects.
- Dropped, because Booster has none of what they read: `counters` and `ring` (the template's ROM3
  capture ring), `shared` (its shared-variable block), and `key`, `app` and `inject` (its debug
  mailbox).
- `testserver.py` keeps the routes and the TLS set-up and replaces the ROM catalog with Booster's
  JSON catalog.

## Sources

| File | Source | Commit |
| --- | --- | --- |
| `console.py`, `flash.sh`, `swd.py`, `stackdepth.py`, `measure_builds.sh` | md-microfirmware-template `tools/dev` | `cd3f930` (v1.3.0) |
| `swd.py`'s `postmortem` memory-map fix | md-rom-emulator `tools/dev/swd.py` | `3cd573d` |
| `testserver.py` (routes, TLS) | md-rom-emulator `tools/dev/testserver.py`, from md-browser's redirect and TLS test server | `3cd573d` |
| `../../booster/src/build_id.cmake` | md-microfirmware-template `rp/src/build_id.cmake` | `cd3f930` (v1.3.0) |
| `../../tests/host/` (framework and shims) | md-rom-emulator `tests/host` | `3cd573d` |
