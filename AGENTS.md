# AGENTS.md

This repository contains the **RP2 Booster Bootloader** for the Raspberry Pi Pico W / SidecarTridge Multidevice. It builds:
- the Booster core firmware (web UI, OTA, SD, multi-app management)
- a placeholder app used when no app is installed
- an upgrader firmware embedded into Booster
- an Atari ST terminal firmware embedded into Booster

See `README.md` for user-facing behavior, installation steps, and device flow.

---

## Project shape (important)

Key folders and build flow:

- `booster/`  
  Main firmware (Booster core). Produces `booster-<board>.uf2`.

- `placeholder/`  
  Minimal app used when no app is installed. Produces `placeholder-<board>.uf2`.

- `upgrader/`  
  Upgrader firmware. Produces `upgrader-<board>.uf2` and embeds a header into Booster.

- `term/atarist/`  
  Atari ST terminal firmware. Produces `term_firmware.h` embedded into Booster.

- Root scripts:  
  - `build.sh` orchestrates all builds and creates the combined UF2.  
  - `build_uf2.py`, `merge_uf2.py`, `show_uf2.py` for UF2 manipulation.

- Submodules at repo root (do not “vendor” these):
  - `pico-sdk/`
  - `pico-extras/`
  - `fatfs-sdk/`

- Tooling/config:
  - `.clang-format`, `.clang-tidy`, `.clang-tidy-ignore`
  - `.vscode/` (debugging presets)

**Key coupling:**  
- `upgrader/build.sh` generates `upgrader_firmware.h` and copies it to `booster/src/include/upgrader_firmware.h`.  
- `term/atarist/build.sh` generates `term_firmware.h` and copies it to `booster/src/include/term_firmware.h`.  
If you change `upgrader/` or `term/atarist/`, you must rebuild so Booster picks up the refreshed headers.

---

## Build prerequisites

### 1) Initialize submodules
```sh
git submodule init
git submodule update --init --recursive
```

### 2) Environment variables

Build scripts set these internally, but if you build manually you’ll need:

* `PICO_SDK_PATH` (e.g. `$(pwd)/pico-sdk`)
* `PICO_EXTRAS_PATH` (e.g. `$(pwd)/pico-extras`)
* `FATFS_SDK_PATH` (e.g. `$(pwd)/fatfs-sdk`)

### 3) Toolchain

You need an ARM embedded toolchain (`arm-none-eabi-*`) and CMake. Python is required for the UF2 merge tools.  
`term/atarist/` builds use `stcmd` tooling.

A Pico debug probe (picoprobe / Raspberry Pi Debug Probe) is strongly recommended for real debugging.

---

## Build commands (what agents should run)

Use the repo root `build.sh`:

### Debug build (recommended while iterating)

```sh
./build.sh pico_w debug
```

### Release build

```sh
./build.sh pico_w release
```

Notes:

* If you pass a third arg (release type), `build.sh` creates an additional “full” UF2 and `upgrade.bin`.
* `build.sh` deletes the root `build/` directory each run.

### Expected artifacts

After a successful build, `dist/` should contain:

* `rp-booster-<version>.uf2` (or `-debug`)
* If third arg provided: `rp-booster-<version>-full.uf2`, `upgrade.bin`, `SIDECARTVERSION`

---

## Flash / deploy (for quick validation)

You can load the UF2 with `picotool`:

```sh
picotool load -xv dist/rp-booster-<version>.uf2
```

For serious work, prefer debugging with a probe + VS Code (see `.vscode/` and the SidecarTridge docs).

---

## Agent workflow rules (read carefully)

### Non-destructive workflow (must follow)

This repo is often edited interactively by a human while the agent is running. Treat the current filesystem state as the source of truth.

**Never discard or overwrite local changes without explicit user approval.** In particular, do not run any of these unless the user explicitly says to:

* `git restore` / `git checkout -- <path>` / `git reset` (any form that reverts files)
* `git clean` (any form)
* `git submodule update` / “pinning” submodules / changing submodule SHAs

Build scripts in `booster/`, `placeholder/`, and `upgrader/` run `git submodule update` and `git checkout tags/...` to pin SDK versions. Treat that as destructive without approval.

Before running build scripts (especially root `./build.sh`), the agent must:

1. Warn that the build may touch multiple files/submodules and can create a lot of diffs.
2. Ask the user how to protect in-progress work: commit, stash, or proceed without protection.
3. After the build, show `git status --porcelain` and ask before cleaning up any diffs.

### Always keep embedded firmware headers in sync

If you:

* change `upgrader/` firmware behavior
* change `term/atarist/` firmware behavior

…you must rebuild so the generated headers are refreshed in `booster/src/include/`.

### Don’t do heavy work in IRQ/PIO/DMA handlers

Protocol parsing/lookup may happen in an interrupt context, but command handling should be pushed to the main loop. Keep interrupt handlers fast: capture state, set flags, return.

### Respect flash/RAM and “old computer” constraints

* Prefer static buffers and bounded sizes.
* Avoid dynamic allocation in hot paths.
* Be careful with stack usage (RP2040).
* Keep memory map assumptions stable (Booster + placeholder layout).

### Formatting / linting

* Follow `.clang-format` and `.clang-tidy` configurations in the repo root.
* If you touch C/C++ files, format them and avoid introducing new clang-tidy warnings.

### Release workflow

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

### Submodules

Do not change submodule pins unless the change is explicitly required. If you do, call it out clearly in the PR/commit message and note compatibility risk.

### No secrets

Do not add tokens/keys/credentials to the repo. Keep config local.

---

## Where to make common changes

### Booster behavior / UI

Usually in `booster/src/` (HTTP handlers, OTA, SD operations, settings).

### Placeholder behavior

In `placeholder/src/`.

### Upgrader behavior

In `upgrader/src/`, then rebuild to refresh `booster/src/include/upgrader_firmware.h`.

### Atari ST terminal behavior

In `term/atarist/`, then rebuild to refresh `booster/src/include/term_firmware.h`.

---

## What “done” looks like for a change

Before considering a change complete:

1. Host tests pass, and every known bug still fails (a fixed one is renamed from `bug_*.c` to `test_*.c`):

   ```sh
   make -C tests/host test known-bugs
   ```
2. Root build succeeds:

   ```sh
   ./build.sh pico_w debug
   ```
3. `dist/` contains `rp-booster-<version>*.uf2`.
4. If `upgrader/` or `term/atarist/` changed: confirm the embedded headers were regenerated.
5. No obvious style regressions (clang-format) in files you touched.
6. Behavior matches README expectations.

---

## Pointers (authoritative docs)

If anything here conflicts with official documentation, the docs win:

* SidecarTridge Multi-device Programming Guide (microfirmware architecture, build, protocol).
