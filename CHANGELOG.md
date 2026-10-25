# Changelog

## v2.4.3 (unreleased)

_In development. Sections below are filled in as epics land and finalised at tag time._

### New features
- **News in the web interface menu.** The menu at the top of the Apps, Network and Config pages now ends with **News**, to the right of **Report Issue**. It opens the [SidecarTridge news channel on Telegram](https://t.me/s/sidecartridgenews) in a new tab.

---

## v2.4.2 (2026-09-30) - release

Firmware only. The `.wificonf` file on the microSD card can now set a static IP address, and it is applied at every boot, so it can also change the network settings of a device that is already set up. For microfirmware developers, launching the DEV APP works again with a debug probe, and new buttons restore it from a clean slate or reset only its settings.

### New features
- **Static IP address from the `.wificonf` file.** Add `TCPIP_DHCP=No` with `TCPIP_ADDRESS`, `TCPIP_NETMASK` and `TCPIP_GATEWAY`, and optionally `TCPIP_DNS` with one or two servers. `TCPIP_DHCP=Yes` switches back to DHCP. This is the way to set up a device on a network without a DHCP server. The format, with examples, is in [Configuring WiFi from a file](README.md#configuring-wifi-from-a-file).
- **Malformed static settings are ignored, not applied.** The static settings are used only when address, netmask and gateway are all present and valid, and the DNS line is valid when present. Otherwise every `TCPIP_` line is ignored and the device keeps the settings it has.
- **Restore and launch for the DEV APP.** A new button on the DEV APP card in the Apps page starts it again from a clean slate. It replaces the microfirmware in the device with the version downloaded from the catalog, erases the DEV APP's saved settings and deletes the last upload, then launches it. Use it when a deploy from your development environment leaves the device in a bad state. It asks for confirmation, and it is only available from the web page. The DEV APP card and its buttons are described in [DEV APP: tools for microfirmware developers](README.md#dev-app-tools-for-microfirmware-developers).
- **Restore config only for the DEV APP.** A second new button on the DEV APP card erases only the DEV APP's saved settings, so it starts with its defaults the next time it runs. The microfirmware in the device and the last upload are kept, and nothing is launched. The help for both restore buttons appears at the top of the Apps page only while the Development catalog is selected.

### Changes
- **The `.wificonf` file is applied at every boot, not only during the first setup.** Edit the file and power-cycle to change the network settings of a configured device. Flash is written only when the file differs from the stored settings.
- **Check your microSD card before upgrading if you set up with a `.wificonf` file.** If the file is still on the card, it now replaces at every boot any WiFi or TCP/IP settings you changed later on the Network page. Delete or rename the file if you manage WiFi from the web interface.
- While a `.wificonf` file is on the card, the TCP/IP configuration and the network list on the Network page are grayed out and read-only, with a banner explaining why. The device refuses changes to those settings as well, so nothing can be saved there that the file would undo at the next boot.
- The file is read more leniently. Key names work in any case, and lines starting with `#` are comments as well as lines starting with `;`.
- The first setup page lists the `AUTH` values and shows the optional TCP/IP lines in its example file.
- Deleting the DEV APP also deletes the microfirmware uploaded for it with the [developer deploy API](docs/DEPLOY-API.md).

### Fixes
- **WiFi passwords longer than 8 characters now work in the `.wificonf` file.** The file kept only the first 8 characters of the password, so any longer password failed to connect. Every firmware version that reads the file was affected, since v2.0.6beta.
- **Launching the DEV APP no longer overwrites a microfirmware flashed with a debug probe.** Since v2.3.0, launching the DEV APP after installing it from the Development catalog wrote the catalog's placeholder into the microfirmware slot, and the device came straight back to Booster. The placeholder is never flashed now. Uploads from the [developer deploy API](docs/DEPLOY-API.md) are kept in their own file on the microSD card, so upload your microfirmware again after updating: an upload made with an older version is not launched.
- Reinstalling the DEV APP from the catalog no longer replaces a microfirmware uploaded with the developer deploy API. Both used to be stored in the same file.

---

## v2.4.1 (2026-08-30) - release

Firmware only. Fixes a crash on boot with an SD card that has no apps folder, a misleading message on the Apps page, and two problems with the firmware update process.

### Fixes
- **The device no longer crashes on boot when the SD card has no apps folder.** Creating the folder asked the filesystem for a buffer as large as 32 KB, and a failed allocation stopped the device with "Out of memory" instead of falling back to a smaller one. A fresh or newly formatted card is the usual way to hit this.
- The Apps page no longer shows "No apps match the selected filters." while apps are listed. The message now appears only when a filter really does exclude everything.
- **A firmware update can no longer install the wrong image.** The verified download only got its final name when you pressed Confirm, so anything downloaded in between, such as installing a microfirmware, was installed instead. The download is now checked and named in one step, as soon as it finishes.
- The firmware image is deleted from the SD card once the new firmware has booted. It was left behind after every update, around 1.4 MB each time. It is kept until the new firmware is confirmed running, so an interrupted update can still retry from it.

---

## v2.4.0 (2026-08-17) - release

Firmware only. Installs and firmware updates are far more reliable, show real progress while they run, and the firmware update now checks the download before installing it.

### New features
- **You can see what an install is doing.** A progress bar with a percentage and size, the stage it has reached, and the name of the microfirmware being installed. It returns to the Apps page on its own when finished.
- **Failed installs tell you why, and can be retried.** The page shows the reason and offers Retry or Cancel instead of a generic error page.
- **Firmware updates are checked before installing.** The downloaded firmware is verified against a checksum published with it, and the update is refused if it does not match. It is also refused if the checksum is missing, so a damaged or incomplete download can never be flashed.
- The firmware update page shows the same live progress, instead of a fixed two minute wait.

### Fixes
- Microfirmwares hosted on GitHub releases and similar services now install correctly. Downloads follow redirects, and an error page can never be saved as a microfirmware.
- Installs no longer fail at random. A download that used to fail once and work on a later attempt now works first time.
- The device no longer freezes while installing.
- Browsing the web interface during a download can no longer corrupt the SD card.
- A dropped connection is retried instead of failing the whole install.
- Several installs in a row no longer slow down or stall.
- A failed firmware update used to report "No error". It now shows the real reason.
- The progress page no longer shows the size of the previous download.
- The banners at the top of a page no longer all flash into view while it loads.
- Copyright notices read 2024-2026 and match on every page.

### Build
- Releases publish `upgrade.md5` alongside `upgrade.bin`.
- The `fatfs-sdk` and `pico-extras` references now match the versions the build scripts use.

---

## v2.3.0 (2026-08-02) - release

This release moves downloads to HTTPS and the apps catalog to the new SidecarTridge store. The Apps page gains filters for who made a microfirmware and how tested it is, and developers get a way to deploy a microfirmware over WiFi without a debug probe.

### New features
- Downloads now work over both `http://` and `https://` from a single firmware image, chosen per request from the URL scheme. Microfirmware installs and the firmware OTA both use it.
- The version check and the firmware OTA now use `https://` by default.
- The apps catalog has moved to `https://md-store.sidecartridge.com/atari-st/apps.json`.
- Each microfirmware on the Apps page now shows who made it, and a **Creator** filter sits alongside Platform and Features. Creators who publish a website get a small link icon on their filter chip. Microfirmwares published without creator information are shown as SidecarTridge.
- New **Release type** filter on the Apps page: **All**, **Stable** and **Beta**. Stable hides beta versions, Beta shows only beta versions, and All shows everything. It filters the list you already have, and does not change which catalog is loaded.
- **Deploy API for microfirmware developers.** A `.uf2` can be pushed to the device over WiFi and launched with two `curl` calls, instead of needing a debug probe or the USB/BOOTSEL dance. It is off unless you install the DEV APP and stay on the Development channel, and a red banner appears on every page while it is on. Documented in [`docs/DEPLOY-API.md`](docs/DEPLOY-API.md), including a Makefile target. See the security note below before enabling it.

### Changes
- Booster is built as `MinSizeRel` for release flows. Linking TLS costs about 121 KB of flash, and `-O3` no longer fits the 768 KB slot.
- The Stable and Beta buttons on the Apps page used to switch catalog and reload the page, quietly rewriting your saved catalog setting. They now only filter, and the Config page is the single place that chooses the catalog.
- The catalog options on the Config page say what each one is for: `Stable - Tested release (default)`, `Testing - unstable releases`, `Development - Local development only`.
- Official creators are listed first in the **Creator** filter.
- The build scripts now stop at the first failure. A failed compile or link used to be stepped over, so a build could report success while packaging firmware left over from an earlier run, or produce no firmware at all.
- Debug builds are compiled with the same size optimisation as release builds and differ only in their debug logging. A debug build previously overflowed the flash slot and never linked at all.
- The Atari ST terminal firmware is rebuilt on every build, including in CI, where it was being skipped without any error.
- Devices already using one of the standard catalog channels are switched to the new store automatically on upgrade. A custom catalog URL is never modified.
- The Development channel now points at the new store and works. The Beta channel points there too but the store does not publish that catalog yet, so selecting it shows an empty list until it does.

### Fixes
- The **WiFi power** setting now defaults to **No Powersave**, an always-on radio. It previously defaulted to Disabled PM, which also disables power saving but leaves the radio's listen intervals in place. Existing devices keep whatever they have configured; this only changes new devices and factory resets.

### Security
- **HTTPS on the device is encrypted but not authenticated.** Certificates are not verified: the device has no CA bundle and no real-time clock, so certificate validity cannot be checked. Downloads it performs itself — microfirmware binaries and the firmware OTA — are protected against passive eavesdropping on the network, but **not** against an active man-in-the-middle who can substitute content. Do not treat an `https://` binary URL as proof of origin. (The apps catalog is fetched by your browser rather than the device, so it does get normal certificate verification.)
- The MD5 check on a downloaded microfirmware is an integrity check, not a signature. It confirms the stored bytes match what the catalog said to expect, catching truncation and corruption. Because the expected hash arrives from the same catalog over the same connection as the binary, it does not establish authenticity.
- **The developer deploy API is not authenticated.** While it is enabled, anyone who can reach the device on your network can install and run code on it, and that code runs on a cartridge attached to your Atari's bus. The web UI is served over plain `http`, so the connection is not protected either. Requiring the DEV APP to be installed and the Development channel to be selected makes switching it on deliberate and visible; it is not a security boundary. Enable it only on a network you trust, and switch it off when you are done. An uploaded binary is not hash-checked, because the catalog's MD5 describes the placeholder it replaced.
- Verified certificates remain planned but are not in this release.

---

## v2.2.0 (2026-04-20) - release

This release lets users pick which version of a microfirmware to install — including older ones when the publisher keeps them online — and switch between Stable and Beta catalog channels from the Apps page.

### Changes
- Replace the Download/Update buttons on the Apps page with a single context-aware action button (Install / Update / Downgrade).
- Replace the Apps catalog URL text input on the Config page with a channel dropdown (Stable / Beta / Development / Custom).
- Show a loading indicator while the apps catalog is being fetched.

### New features
- Add an optional `previous_versions` array to entries in `apps.json`, backwards compatible with existing catalogs.
- Add a per-app version selector defaulting to the latest, with downgrades requiring an explicit confirmation.
- Add a Stable/Beta channel switcher on the Apps page that updates the catalog URL and reloads on click.

### Fixes
- Compare versions using a semantic-version comparator instead of string equality.

---

## v2.1.0 (2026-03-24) - release

This release delivers a smoother overall user experience, with a cleaner Manager interface on both the Atari ST screen and the web UI, better behavior when WiFi is unavailable, fixes for some network issues in weak WiFi environments, and major flash-usage optimizations behind the scenes.

### Changes
- Reduced flash usage across Booster and the embedded Upgrader with targeted code-size optimizations, smaller display primitives, and streamlined helper code.
- Minify the Manager web assets before generating `fsdata_srv.c`, and refactor repeated page content to reduce the embedded web payload size.
- Add automatic flash usage reporting to the Booster build and stop the build if the generated image exceeds the available flash slot.
- Accept `MinSizeRel` explicitly in the build scripts, and always build the Upgrader in `MinSizeRel` for `release` and `minsizerel` flows.

### New features
- Add an offline Manager mode: if WiFi connection retries are exhausted, Booster keeps running without network features and still allows manual boot of already-downloaded microfirmwares from the Atari ST terminal path.
- Allow launching installed apps directly from the Atari ST terminal, including the ESC-to-apps and SHIFT-to-GEMDOS workflow for manual booting.
- Show signal strength and MAC address information in both the Atari ST Manager screen and the shared system information panel in the web UI.
- Re-enable ICMP ping replies for easier network diagnostics and compatibility checks.
- Add responsive Platform and Features filters to the Apps page so users can narrow the available microfirmwares dynamically from the downloaded catalog.
- Update the integrated Atari ST terminal firmware and embedded Upgrader image used by Booster.

### Fixes
- Improve SELECT-button debouncing for long presses so factory reset is more reliable.
- Fix several WiFi stability issues, including reconnect memory pressure, polling behavior, multi-connection handling, and weaker-signal network environments.
- Harden MD5 parsing for downloaded app metadata by validating the input length before converting it from JSON.
- Polish Manager copy, status messaging, and minor header consistency issues found during PR review.

---

## v2.0.9 (2025-12-20) - release

This release now shows the MAC address to help users to configure their home router filters.

### Changes
- Show the WiFi countries as a drop down list instead of a text input, to avoid invalid country codes.
- Show the available power WiFi network options as a drop down list instead of a text input, to avoid invalid power values.
- Show the range of SD baud rate values (KB/s) as a drop down list instead of a text input, to avoid invalid speed values.
- Shows the MAC address always on the Atari ST connection screen, even if the device is not connected to a WiFi network.

### New features
- Shows the MAC address of the Pico W in the Atari ST screen and system information banner at the bottom of the web pages.
- Added AGENTS.md file with instructions for AI agentic development. 

### Fixes
- The DMA memory copy to ROM in RAM gets the number of bytes to copy instead of words. For consistency with the microfirmwares.

---

## v2.0.8 (2025-10-16) - First stable release

This is the first stable release of the 2.0.x series. The code has been tested and is considered stable. 

### Changes
No changes in this release.

### New features
No new features in this release.

### Fixes
No fixes in this release.

---

## v2.0.7beta (2025-10-07) - Beta release

This is the second beta release. It includes all the new features and improvements for the version 2.0.x, and it will not include any more new features but only fixes and small improvements. The code is still in development and may contain bugs, but it is stable and ready to use for all users.

### Changes
- **Link to the Report Issues page**: A link to the Report Issues page has been added to the web app at the bottom of the page. This makes it easier for users to report any issues they encounter while using the app.

### New features
- **New ./notreboot file**: A new `./notreboot` file has been introduced. If this file is present on the microSD card, the device will not reboot automatically if the user performs a reset to the factory settings. This feature is useful during the development and testing phases, because combined with the `./wificonf` file, it allows to test the device without the need to reconfigure the WiFi settings after each reset.

### Fixes
No fixes in this release.

---

## v2.0.6beta (2025-08-14) - Beta release

This is the first beta release. It includes all the new features and improvements for the version 2.0.x, and it will not include any more new features. The code is still in development and may contain bugs, but it is more stable than previous alpha releases and ready to use for all users.

### Changes
- **Show information in the web app when launched app**: The web app will now display relevant information and tips when launched, helping users to get started quickly and easily.
- **Removed USB Mass Storage support**: The USB Mass Storage feature has been removed. Now, to use the Mass storage mode the user has to launch the File Manager or Multi-drive microfirmwares.
- **Disable ICMP ping responses**: ICMP ping responses have been disabled for improved reliability.
- **Better network deinitialization**: The network deinitialization process has been improved to avoid the network to stuck when changing from the "Factory" mode to the "Booster" mode.

### New features
- **Firmware update notifications**: The web app will now notify users when a new firmware update is available.
- **Firmware upgrade process**: The firmware upgrade process has been streamlined for easier updates. Now the users can initiate the upgrade process directly from the web app, without needing to manually download and install firmware files.
- **mDNS support**: The web app now supports mDNS (Multicast DNS), allowing for easier discovery and connection to devices on the local network. Now the site "sidecart.local" is accessible via mDNS. Previously it was only through DHCP plus DNS, which small routers from telecom providers often struggle with.

### Fixes
- Multiple small bug fixes and improvements.

## v2.0.5alpha (2025-07-02) - Alpha release

This is a rolling alpha release. It may include new features, bugs, and issues. Bug and issue tracking is not yet available as this is not a public release. This is a development version.

### Changes
- **Highlight microfirmware updates available**: The system will now highlight available microfirmware updates, making it easier for users to identify and apply important updates.

- **Remove Safe SELECT reboot option**: The Safe SELECT reboot option has been removed from the configuration settings. Not needed anymore.

### New features
No new features in this release.

### Fixes
- **Display tooltips for configuration options**: Tooltips did not display for various configuration options, providing users with helpful information and guidance when making changes.

## v2.0.4alpha (2025-06-13) - Alpha release

This is a rolling alpha release. It may include new features, bugs, and issues. Bug and issue tracking is not yet available as this is not a public release. This is a development version.

### Changes
- **Disabled USB Mass storage in Booster**: The USB Mass Storage feature has been disabled in the Booster firmware to prevent potential issues with device stability and performance.
- **Display HTML description**: Now the description accepts HTML tags, allowing for more flexible and rich text formatting in the device description field.
- **Stay in Booster mode**: The device will now remain in Booster mode after a reboot, ensuring that users can continue to use the Booster features without needing to re-enter the mode.

### New features
No new features in this release.

### Fixes
No fixes in this release.

## v2.0.3alpha (2025-06-13) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **Better retry logic for Wifi connection**: The retry logic for WiFi connections has been improved to handle connection failures more gracefully. This should reduce the number of failed connections and improve overall reliability.
- **Better error messages**: Error messages have been enhanced to provide more detailed information about issues.

### New features
- **New .wificonf file**: A new `.wificonf` file has been introduced to allow users to configure WiFi settings right from the microSD card. This file can be used to set up WiFi credentials, making it easier to connect to WiFi networks without needing to access the web interface.

### Fixes
- **URL decoding**: Fixed an issue where the decoding of the password sent from the web interface to the device was not working correctly. This could lead to connection issues if special characters were included in the password.

## v2.0.1alpha (2025-06-06) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **Slow USB Mass Storage**: Trying to fix the issues when mounting USB Mass Storage mode that hangs the system. A slower USB Mass Storage mode is now enabled by default.
- **Automatic build of the full image**: The full image is now automatically built and uploaded to the repository. This ensures that the latest version is always available for users with the drag and drop feature, not just for the `picotool` command line tool users.

### New features
- **New HELP link**: A new HELP link has been added to the Management page. This link provides users with quick access to documentation and support resources, enhancing the user experience.

### Fixes
Everything is a massive and ongoing fix...

## v2.0.0alpha (2025-06-05) - Alpha release
- First version
