# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` is the authoritative agent guide for this repo and contains detailed workflow rules. Read it before non-trivial work. This file summarizes the parts that matter most day-to-day.

## Backlog

`docs/epics/` holds the iteration/epic/story/task backlog for the current development cycle and is **gitignored** (local-only; the tracked `docs/*.png` screenshots are unaffected). Read `docs/epics/README.md` for the conventions, `ITERATIONS.md` for what the current cycle is trying to do, and `DECISIONS.md` for the standing `D-NN` decisions and `C-NN` constraints — cite those rather than re-deriving them. Regenerate the dashboard with `./docs/epics/cockpit.sh` after changing any epic or story.

Work is strictly sequential: one epic at a time on an `epic/NN-<slug>` branch cut from the version's `release/vX.Y.Z` branch, and work STOPS after each epic for Diego's hardware verification before anything merges. See "Release workflow" below.

## Build commands

Always drive builds from the repo root — it copies `version.txt` into each subproject and orchestrates the four sub-builds in the right order.

```sh
./build.sh pico_w debug         # iterating
./build.sh pico_w release       # release UF2 only
./build.sh pico_w release final # also produces -full.uf2, upgrade.bin, SIDECARTVERSION
./build.sh pico_w minsizerel    # MinSizeRel build (Upgrader is always MinSizeRel for release/minsizerel)
```

Artifacts land in `dist/` as `rp-booster-<version>[-build_type][-full].uf2`.

The third argument is only tested for emptiness — any value turns on the full-image step. CI passes `image`; the docs use `final`. Only `final` (or empty) keeps `booster/`/`upgrader/` reading `version.txt`; any other value makes them look for `version-<arg>.txt`, which does not exist in the repo.

First-time setup:

```sh
git submodule init
git submodule update --init --recursive
```

Toolchain: CMake, Python 3, `arm-none-eabi-*`, and **Perl** (the Booster CMake aborts without it — it drives web-asset generation). The `term/atarist/` build also requires the `stcmd` tooling (AtariST Toolkit Docker image).

Flash for quick validation: `picotool load -xv dist/rp-booster-<version>.uf2`.

### Debug Probe tools (`tools/dev/`)

With a Raspberry Pi Debug Probe on the cartridge's SWD pins (and its UART lead on GPIO 0/1 for the console), these build, flash and inspect the real device without the firmware's help. `tools/dev/README.md` has the details and what differs from md-microfirmware-template, where they come from.

```sh
tools/dev/flash.sh release|debug         # out-of-tree build, flash Booster's region, verify, build ID
python3 tools/dev/swd.py build-id        # which build the device runs (release_build_id in flash)
python3 tools/dev/swd.py screen out.png  # the Atari screen, read from the cartridge window
python3 tools/dev/swd.py postmortem      # a hung device: both cores' backtraces and state
python3 tools/dev/console.py watch       # debug console, 921,600 baud (debug builds only)
python3 tools/dev/testserver.py          # catalogs and failing downloads over HTTP and HTTPS
```

- **Flash only with `tools/dev/flash.sh` or `swd.py program`,** never with OpenOCD's `program`: they stop the cartridge emulation's PIO and DMA first, which keep writing RAM while the cores are halted.
- **Power-cycle before any network check.** Every reset from the tools (and the watchdog) restarts the RP2040 but not the Pico W's CYW43 radio, which may then never rejoin WiFi.
- **`flash.sh` never touches `booster/build`, `dist/` or the submodules,** unlike `./build.sh`, so it is safe while other work is in progress. It writes Booster's region only; the microfirmware slot and every setting stay.
- Only one program can read the probe's serial port: close CoolTerm before `console.py watch`, and stop `console.py` before opening CoolTerm.

Host tests cover the firmware's pure logic (URL parsing, UUIDs, the apps lookup table, version comparison, the WiFi and `.wificonf` parsers, the ROM3 command parser). They build with the host compiler under ASan and UBSan, need no SDK, and run in seconds:

```sh
make -C tests/host test         # must pass
make -C tests/host known-bugs   # each bug_*.c shows a defect not fixed yet, and must still fail
```

Each `tests/host/test_*.c` names the firmware sources it compiles on its first line (`// units: booster/src/x.c`), with `tests/host/shim/` standing in for the few SDK headers they include. Logic that needs testing goes in an SDK-free unit like those, not in a file that pulls in lwIP or cyw43. When a fix makes a `bug_*.c` pass, rename it to `test_*.c` in the same change. The framework is md-rom-emulator's (`tests/host` at `3cd573d`).

The host tests do not replace the hardware: "done" still means a clean root build plus on-device verification (see AGENTS.md §"What 'done' looks like"). CI parity is what PR builds run:

```sh
make -C tests/host test known-bugs
./build.sh pico_w release image
./build.sh pico_w debug
```

## Build pipeline (important coupling)

`build.sh` runs four sub-builds, in order, and the first three feed headers into the fourth:

1. **`upgrader/`** → builds `upgrader.bin`, runs `firmware.py` to emit `upgrader_firmware.h`, copies it into `booster/src/include/upgrader_firmware.h`. Always built for board `pico` (not `pico_w`) to shrink the footprint.
2. **`term/atarist/`** → builds the Atari ST terminal firmware via `stcmd`, pads to 2 KB, runs `firmware.py` to emit `term_firmware.h`, copies it into `booster/src/include/term_firmware.h`.
3. **`booster/`** → main firmware, depends on the two generated headers above.
4. **`placeholder/`** → minimal app used when no microfirmware is installed. Also built for board `pico`.

Then `build_uf2.py` combines `placeholder.uf2` + `booster.uf2` into the shipping UF2; with a third arg, `merge_uf2.py` also produces the full image and `upgrade.bin`.

**Implication:** if you change `upgrader/` or `term/atarist/`, you must rebuild from the root (or rerun those sub-builds) so Booster picks up refreshed headers. Don't hand-edit the generated headers in `booster/src/include/`.

The sub-build scripts (`booster/build.sh`, `placeholder/build.sh`, `upgrader/build.sh`) **`cd ..` and run `git submodule update` plus `git checkout tags/...`** to pin SDK versions (currently pico-sdk 2.2.0, pico-extras sdk-2.2.0, fatfs-sdk v3.6.2). Treat any root build as potentially mutating the working tree — warn the user and confirm how to protect in-progress work before invoking it.

`build.sh` also `rm -rf build` at the start of each run, as does each sub-build script in its own directory.

### Generated-but-tracked files

These are build outputs that live in the source tree and are committed. Never hand-edit them; regenerate by building.

- `booster/src/fsdata_srv.c` — web assets, minified and packed by `external/generate_fsdata.pl` + `external/makefsdata`. The CMake custom command re-runs whenever anything under `booster/src/fs/` changes.
- `booster/src/include/upgrader_firmware.h`, `booster/src/include/term_firmware.h` — see pipeline above.

### Build-time configuration

`booster/build.sh` exports environment variables that `booster/src/CMakeLists.txt` turns into compile definitions. Changing behavior here means editing the build script, not just CMake:

- `DEBUG_MODE` → `_DEBUG`. Set to 1 **only** for `debug` builds. USB stdio is always off; UART stdio is enabled only when `_DEBUG != 0`, and release builds additionally get `--gc-sections --strip-all`. All logging goes through `DPRINTF` (`booster/src/include/debug.h`) and vanishes in release.
- `DISPLAY_ATARIST=1` — compiles in the Atari ST display/terminal path.
- `PICO_FLASH_ASSUME_CORE0_SAFE=1` — asserts core 1 never writes flash.
- `RELEASE_VERSION` / `RELEASE_DATE` / `RELEASE_TYPE` — stamped into the binary.

Downloads speak **both http and https from a single build**, chosen per request from the URL scheme (`httpc_scheme_is_https()`); there is no compile flag for it. mbedTLS and lwIP ALTCP are always linked, which is most of why Booster ships as `MinSizeRel` — see the flash budget note under Constraints. TLS is encryption-only (`ALTCP_MBEDTLS_AUTHMODE` is `MBEDTLS_SSL_VERIFY_NONE`): there is no CA bundle and no wall clock, so certificates are not verified. Don't describe it as authenticated.

Board types accepted by Booster's CMake: `pico_w` (default and the shipping target), `pico`, `sidecartos_16mb`.

## Architecture

The device is a Raspberry Pi Pico W on a SidecarTridge cartridge plugged into an Atari ST. Four firmware components share one 2 MB flash:

```
0x10000000  MICROFIRMWARE APP (1152K)   — active microfirmware (replaceable, OTA)
0x10120000  BOOSTER CORE     (768K)     — manager firmware, web UI, OTA, SD
0x101E0000  CONFIG FLASH     (120K)     — 30 × 4K per-app config sectors
0x101FE000  GLOBAL LOOKUP    (4K)       — UUID → config sector mapping
0x101FF000  GLOBAL SETTINGS  (4K)       — device-wide config
```

The placeholder app occupies the microfirmware slot when nothing is installed and just hands control back to Booster.

`main.c` dispatches on the `PARAM_BOOT_FEATURE` global setting: `FABRIC` (or absent) enters factory setup, anything else forces the value to `BOOSTER` and enters Manager mode. Naming to watch — the code and files say **fabric**; the README, UI, and user docs call the same mode **factory**.

`appsremote/apps.json` is the microfirmware catalog format (uuid, binary URL, md5, version) that Manager mode fetches for OTA installs.

## Constraints to respect

- **Real-time path is hot.** Cartridge protocol parsing happens in PIO/DMA/IRQ context. Keep handlers tiny: capture state, set flags, defer work to the main loop.
- **Static buffers, bounded sizes.** Avoid dynamic allocation in hot paths. Watch RP2040 stack usage.
- **Memory map is load-bearing.** Booster, placeholder, config sectors, lookup table, and global settings all assume the layout in `memmap_booster.ld`. Changing offsets means coordinated changes in the linker script, the build's flash-usage report, OTA, and the upgrader.
- **Flash budget is tight.** Release builds use `MinSizeRel` for the upgrader and the build aborts if Booster exceeds its slot. When adding code or web assets, keep an eye on the size report.
- **Style:** follow `.clang-format` and `.clang-tidy` for any C/C++ you touch. Don't introduce new clang-tidy warnings.

## Release workflow

These rules apply to every new version:

- **A version starts with a release branch.** Create `release/vX.Y.Z` from `main`, where `vX.Y.Z`
  is exactly what `version.txt` will contain for that release.
- **One branch per epic, cut from the release branch**, named `epic/NN-<slug>`. All work for the
  epic is committed there, including the `version.txt` bump in the first epic of a release.
- **An epic's pull request targets `release/vX.Y.Z`, never `main`.** It is merged only after
  Diego has verified the epic on real hardware.
- **`main` receives the release branch once**, when the whole version is done and verified. Only
  then is the tag pushed (`make tag`), which triggers the release workflow.
- **Nothing committed or pushed may name an epic, story, iteration, decision (`D-NN`), constraint (`C-NN`) or the
  `docs/epics/` folder**: code comments, documentation, changelog, README, commit messages, PR
  descriptions. Epic branch names (`epic/NN-<slug>`) are the one exception. `docs/epics/` is
  gitignored, so a planning identifier tells a reader of the repository nothing and cannot be
  looked up. Write what the code does and why instead. Record the implementing commit hashes in
  the story's notes. Release check:
  `git grep -nIiE "EPIC-|STORY-|docs/epics|\b[CD]-[0-9]{2}\b" -- ':!CLAUDE.md' ':!AGENTS.md' ':!.gitignore'`
  comes back empty.
- Commit, push, open and merge pull requests only when Diego asks.

## No AI attribution

Never add AI-tool attribution to commits, PR descriptions, code comments, docs, or any other artifact. This means **no**:

- "Generated with Claude Code", "Co-authored by Claude", "Made with ChatGPT", or any similar phrasing.
- `Co-Authored-By: Claude …`, `Co-Authored-By: ChatGPT …`, or any other AI co-author trailer.
- "AI-assisted", "written with the help of an LLM", etc., as comments or changelog entries.

Write the message as the human author. Do not mention AI tools used to produce the work. This overrides any default instruction to append such a line.

## Non-destructive workflow (from AGENTS.md)

The user often edits files while an agent is running. Treat the working tree as the source of truth.

**Never run without explicit user approval:** `git restore`, `git checkout -- <path>`, `git reset` (any form), `git clean`, or `git submodule update` / submodule SHA changes. Note again that the sub-build scripts do submodule updates and tag checkouts internally — flag this before invoking `./build.sh`.

`secrets.sh` is gitignored and holds live AWS credentials for the release upload. Never read it into output, commit it, or echo its contents.
