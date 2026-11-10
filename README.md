# Raspberry Pi Pico W Booster Loader  

![Discord](https://img.shields.io/discord/1160868666162823218?style=flat&label=Discord&link=https%3A%2F%2Fdiscord.com%2Finvite%2Fu73QP9MEYC)


This is an advanced second-stage bootloader for the Raspberry Pi Pico W and the SidecarTridge Multidevice board. The current 2.4.x series targets the RP2040-based Pico W and focuses on a web-based Manager, Atari ST terminal integration, OTA updates, and resilient offline-safe boot flows.  

## Key Features  

- **OTA (Over-The-Air) Updates**  
  The Booster app fetches and installs the latest apps or microfirmwares from a web server, making it easy to manage and update multiple solutions on a single device.  

- **Web-Based Manager**  
  Access and manage the device via a web browser. The interface simplifies app installation, updates, and system configuration without the need for additional software.  

- **Micro SD Card Support**  
  Store and manage files on a micro SD card, allowing apps to use external storage or turn the device into a file server.  

- **Catalog Filters in the Apps View**  
  Browse apps as cartridges on shelves by topic, or find one by search, computer, release type, creator or tag. Colours show at a glance which apps are installed and which have an update.  

- **Catalog Channel Selector**  
  Choose the catalog on the Config page: **Stable** for tested releases (the default), **Testing** for unstable releases, **Development** for microfirmware developers, or a custom catalog URL.  

- **WiFi Setup from the microSD Card**  
  A `.wificonf` file sets the WiFi network and, optionally, a static IP address. It is applied at every boot, so it can also change the settings of a device that is already set up. See [Configuring WiFi from a file](#configuring-wifi-from-a-file).  

- **Tools for Microfirmware Developers**  
  With the Development catalog selected and the DEV APP installed, push a microfirmware over WiFi with the [deploy API](docs/DEPLOY-API.md), run one flashed with a debug probe, and restore the DEV APP or reset its settings from the Apps page. See [DEV APP: tools for microfirmware developers](#dev-app-tools-for-microfirmware-developers).  

- **Per-App Version Selector**  
  Pick which version of a microfirmware to install. The latest is selected by default, but when the publisher exposes prior versions in the catalog you can roll back or pin to a known-good build, with a confirmation step for downgrades.  

- **Offline-Capable Manager**  
  If Booster cannot connect to the configured WiFi network after its retry cycle, it stays usable in offline mode so already-downloaded microfirmwares can still be launched from the Atari ST terminal path.  

- **Atari ST Terminal Integration**  
  Use the Atari ST display as the main terminal, inspect connection details directly on screen, and launch installed apps from the keyboard when network features are unavailable or not needed.  

- **Network Diagnostics**  
  The Manager shows the current MAC address and WiFi signal strength on both the Atari ST display and the web UI, and it replies to ICMP ping requests when connected.  

- **Storage Workflows Through Microfirmwares**  
  Booster itself no longer exposes USB mass storage directly. Use dedicated storage-oriented microfirmwares such as File Manager or Multi-drive when you need file-transfer workflows.  

- **Multi-App Support**  
  Install and manage multiple app or microfirmwares simultaneously, ensuring flexible and dynamic usage.  

- **External Display Support**  
  Connect an external screen or run headless. Currently supports **Atari ST high and low resolutions**, with easy expansion to other display types.  

- **Global Settings**  
  Store system-wide configurations in flash memory, accessible via the web interface or terminal.  

## Booster components  

The Booster app is designed to be modular and extendable. The key components include:  

- **Booster Core**  
  The heart of the system, responsible for managing the device, loading/unloading apps, handling the micro SD card, and providing the web interface.  

- **Placeholder App**  
  A temporary app that runs when no other app is installed. It hands control over to the Booster Core and is replaced when an active app is loaded.  

- **Global Settings**  
  Stored in flash memory, these settings apply to the entire device and its apps.  

- **Local Settings**  
  Each app has its own isolated configuration stored in flash memory, ensuring app-specific settings remain independent.

- **Atari ST firmware**  
  The Booster app uses the Atari ST computer as the terminal for screen and keyboard input/output. A small custom firmware is included to handle the Atari ST's unique hardware and protocols.

## Memory Map  

The Pico W and Pico 2W feature a **2MB flash memory**, structured as follows:  

```plaintext
+-----------------------------------------------------+
|                                                     |
|              MICROFIRMWARE APP (1152K)              |  0x10000000
|          Active microfirmware flash space           |
|                                                     |
+-----------------------------------------------------+
|                                                     |
|                  BOOSTER CORE (768K)                |  0x10120000
|  - Manages device, apps, SD card, web interface     |
|  - Loads/unloads apps                               |
|                                                     |
+-----------------------------------------------------+
|                                                     |
|                  CONFIG FLASH (120K)                |  0x101E0000
|             Microfirmwares configurations           |
|                                                     |
+-----------------------------------------------------+
|                                                     |
|              GLOBAL LOOKUP TABLE (4K)               |  0x101FE000
|  Microfirmwares mapping to configuration sectors    |
|                                                     |
+-----------------------------------------------------+
|                                                     |
|              GLOBAL SETTINGS (4K)                   |  0x101FF000
|           Device-wide configurations                |
|                                                     |
+-----------------------------------------------------+
```

The **Booster Core** resides in **flash memory from 0x10120000 (768K)**. This region is only modified when updating the core itself.  

The **Config Flash (120K at 0x101E0000)** stores per-app settings, allowing up to **30 microfirmware apps**, each allocated **4K of flash memory**.  

A **Global Lookup Table (4K at 0x101FE000)** maps microfirmware apps to their configuration sectors using unique UUIDs.  

Finally, the **Global Settings (4K at 0x101FF000)** store system-wide configurations for the device and its microfirmware apps.  

Each **microfirmware app is loaded at 0x10000000** after powering on the device. It can be updated over the air or through the web interface.  

This design ensures efficient memory usage while providing a structured and extensible platform for managing multiple apps and configurations on the Raspberry Pi Pico W.  

## Booster core libraries and components  

### External Libraries  

These libraries are in different repositories and are included as submodules:  

- Pico-SDK: The official Raspberry Pi Pico SDK, providing essential libraries and tools for development.
- Pico-Extras: A collection of additional libraries and utilities for the Pico platform, enhancing functionality and ease of use.
- [no-OS-FatFS-SD-SDIO-SPI-RPi-Pico](https://github.com/carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico): A library for interfacing with micro SD cards using the SPI protocol, enabling file system access and management.

### Internal Libraries  

These libraries are part of the project and are included in the repository:  

- cJSON: A lightweight JSON parsing library for C, used for handling JSON data in the web interface and configuration files.
- dhcpserver: A simple DHCP server implementation for the Pico, allowing the device to assign IP addresses to connected clients.
- dnserver: A DNS server implementation for the Pico, enabling hostname resolution for local network devices.
- httpc: A lightweight HTTP client library for making requests to web servers, used for fetching updates and resources.
- md5: A library for calculating MD5 hashes, used for verifying the integrity of downloaded files and updates.
- qrcodegen: A library for generating QR codes, allowing users to easily share device information and configurations.
- settings: The `rp-settings` library.
- u8g2: A graphics library for rendering text and images on various display types, used for the external display support in the Booster Core.

## Building the Project  

Initialize the submodules first:

```bash
git submodule init
git submodule update --init --recursive
```

Install the required dependencies:

- CMake
- Python 3
- GCC ARM embedded toolchain (`arm-none-eabi-*`)
- Perl, which the Booster build uses to pack the web pages into the firmware. The build stops if it is missing
- `stcmd` tooling for the embedded Atari ST terminal firmware

The root `version.txt` file defines the release string and is copied into the subprojects by the build scripts.

Common root builds:

```bash
./build.sh pico_w debug
./build.sh pico_w release
./build.sh pico_w minsizerel
```

The combined UF2 is written to `dist/`. If you pass any third argument, the root build also creates the full-image artifacts (`*-full.uf2`, `upgrade.bin`, and `SIDECARTVERSION`).

## Downloading the Project

Firmware artifacts are published in the [GitHub Releases section](https://github.com/sidecartridge/rp2-booster-bootloader/releases). If you build locally, use the files generated in `dist/`.

## Installing the project

Go for the option that best suits you. Both options will install the bootloader on your Raspberry Pi Pico W.

###  Option 1: Using the drag-and-drop method
1. Connect the Raspberry Pi Pico W micro USB connector to your computer while holding the **BOOTSEL** button.
2. Your computer should recognize the device as the mass storage device `RPI-RP2`. Now you can release the **BOOTSEL** button.
3. Drag and drop a downloaded `rp-booster-<version>-full.uf2` release asset, or a locally built `dist/rp-booster-<version>-full.uf2`, to the `RPI-RP2` drive.
4. Wait for the file to be copied. When the copy is complete you can disconnect the Raspberry Pi Pico W from your computer.

### Option 2: Using picotool

With this method you must [first install the picotool](https://github.com/raspberrypi/picotool) helper tool, which is a command-line utility for managing Raspberry Pi devices based on the RP2040 microcontroller.

1. Connect the Raspberry Pi Pico W micro USB connector to your computer while holding the **BOOTSEL** button.
2. Your computer should recognize the device as the mass storage device `RPI-RP2`. Now you can release the **BOOTSEL** button.
3. Flash the bootloader on your board:
```bash
picotool load -xv /path/to/rp-booster-<version>.uf2
```


## Booster Quickstart Guide

### Initial Factory Configuration for SidecarTridge Multidevice on Atari ST Series Computers

1. Format a microSD card with either **exFAT** or **FAT32** file system and insert it into the SidecarTridge Multidevice board.

2. Plug the SidecarTridge Multidevice into the **cartridge port** of your Atari ST series computer.

3. Power on the Atari ST computer.

4. The Booster app will automatically start in **Factory mode**, showing the following message on screen:

   ![Booster Factory Mode step 1](/docs/BOOSTER-FABRIC-1.png)

   This screen confirms that the Booster app is running in Factory mode and is ready to be configured.  
   Scan the **QR code** with your smartphone to connect to the WiFi network created by the Booster app, or manually connect via your WiFi settings.  
   The default network details are:
   - **SSID**: `SIDECART`  
   - **Password**: `sidecart`

5. Once connected to the WiFi, the Booster app will detect the connection and show the following screen:

   ![Booster Factory Mode step 2](/docs/BOOSTER-FABRIC-2.png)

   This screen invites you to open a web browser and navigate to either:
   - `http://sidecart.local`  
   - or `http://192.168.4.1` (if `.local` domains aren’t supported)

6. In your web browser, enter the URL shown above to access the web interface.  
   You’ll be prompted to configure your home or office WiFi network.  
   Select your WiFi SSID from the list (you can click **Refresh** if it doesn’t appear), then click it to continue.  
   In this example, the network is `CHISMEROUTER`.

   ![Booster Factory Mode step 3](/docs/BOOSTER-FABRIC-3.png)

7. Enter your WiFi password and click the **Connect** button.

   ![Booster Factory Mode step 4](/docs/BOOSTER-FABRIC-4.png)

8. The Booster app will save the WiFi credentials to flash memory and reboot.  
   It will then connect to your WiFi network and show this message:

   ![Booster Factory Mode step 5](/docs/BOOSTER-FABRIC-5.png)

   Meanwhile, the device will also display a reboot message on the Atari screen:

   ![Booster Factory Mode step 6](/docs/BOOSTER-FABRIC-6.png)

9. After rebooting, the Booster app will attempt to connect to your configured WiFi network:

   ![Booster Manager Mode step 1](/docs/BOOSTER-MANAGER-1.png)

10. Once connected successfully, the screen will show:

    ![Booster Maneger Mode step 2](/docs/BOOSTER-MANAGER-2.png)

    Your WiFi setup is now complete. From now on, the Booster app will boot directly into **Manager mode**.  
    See the next section for details on using Manager mode.

### Configuring WiFi from a file

A text file named `.wificonf` in the root folder of the microSD card sets the WiFi network and, optionally, a static IP address. Use it to:

- set up a device without a phone or a browser,
- bring up a device on a network that has no DHCP server,
- change the network settings of a device that is already set up.

#### 1. Create the file

Create a plain text file named exactly `.wificonf`, with the leading dot and no extension. Some editors add an extension or save formatted text:

- **Windows Notepad**: in the Save dialog set *Save as type* to *All files*, otherwise the file is saved as `.wificonf.txt`.
- **macOS TextEdit**: choose *Make Plain Text* from the *Format* menu before saving. Finder hides names that start with a dot; press `Cmd+Shift+.` to show them.
- **Linux**: any text editor works.

#### 2. Write the settings

For a network with DHCP, three lines are enough:

```
SSID=MyNetwork
PASS=MyPassword
AUTH=5
```

For a static IP address, add the TCP/IP lines:

```
SSID=MyNetwork
PASS=MyPassword
AUTH=5
TCPIP_DHCP=No
TCPIP_ADDRESS=192.168.1.50
TCPIP_NETMASK=255.255.255.0
TCPIP_GATEWAY=192.168.1.1
TCPIP_DNS=192.168.1.1,8.8.8.8
```

| Key | Needed | Value |
| --- | --- | --- |
| `SSID` | For a new device | Network name, exactly as your router shows it. Upper and lower case matter. |
| `PASS` | For a new device | Network password, 8 to 63 characters. Leave the value empty for an open network. |
| `AUTH` | For a new device | Security type as a number: `0` open, `1` WPA TKIP, `5` WPA2 AES, `8` WPA2 mixed. `5` suits a typical WPA2 router. WPA3-only networks cannot be selected. |
| `TCPIP_DHCP` | No | `No` to use the static settings below, `Yes` to use DHCP. `True`/`False`, `1`/`0`, `Y`/`N` and `T`/`F` work too, in any case. |
| `TCPIP_ADDRESS` | When `TCPIP_DHCP` is `No` | The device's IPv4 address, for example `192.168.1.50`. Choose one outside the range your router hands out by DHCP. |
| `TCPIP_NETMASK` | When `TCPIP_DHCP` is `No` | Usually `255.255.255.0`. |
| `TCPIP_GATEWAY` | When `TCPIP_DHCP` is `No` | Your router's address, for example `192.168.1.1`. |
| `TCPIP_DNS` | No | One DNS server, or two separated by a comma. Without it the device keeps the DNS servers it already has, which is `8.8.8.8` on a new device. |

Writing rules:

- One setting per line, as `KEY=value`. Key names work in any case.
- On the `SSID` and `PASS` lines everything after the `=` is used as written, spaces included, so do not put spaces around the `=` there.
- Lines starting with `;` or `#` are comments. Unknown keys are ignored.
- Windows and Unix line endings both work.

#### 3. Boot the device

Copy the file to the root folder of the microSD card, not to the apps folder. Insert the card and power on.

- **A new device** starts in Factory mode, reads the file, saves the settings and reboots straight into Manager mode, without the QR code screens.
- **A device that is already set up** applies the file in Manager mode before it connects, so the new settings take effect on that same boot. Nothing is written to flash when the file matches the stored settings.

Once connected, the Atari screen shows the network name and the device's address. With static settings, that address is the one in `TCPIP_ADDRESS`.

#### How the TCP/IP lines behave

- Without a `TCPIP_DHCP` line, the other `TCPIP_` lines are ignored and the device keeps its current TCP/IP settings. A new device uses DHCP.
- To go back to DHCP, write `TCPIP_DHCP=Yes`. Deleting the `TCPIP_` lines is not enough, because the device keeps the static settings it already has.
- With `TCPIP_DHCP=No`, the static settings are applied only if `TCPIP_ADDRESS`, `TCPIP_NETMASK` and `TCPIP_GATEWAY` are all present and valid, and `TCPIP_DNS` is valid when present. If any of them is missing or malformed, every `TCPIP_` line is ignored and the device keeps its current settings.
- Addresses are checked for format only. An address that is well formed but wrong for your network, such as one on another subnet, is applied. To recover, correct the file on a computer and power-cycle the device.

#### While the file is on the card

The file is applied at every boot, so while it is on the card it decides the WiFi network and the TCP/IP settings:

- On the **Network** page, a banner explains this, and the TCP/IP configuration and the network list are grayed out and read-only. The WiFi settings above them (country, hostname, power and RSSI) stay editable.
- A factory reset does not remove it either: the file is applied again on the next boot.
- To manage the network from the web interface again, delete the file or rename it, for example to `wificonf.bak`, and power-cycle. The device keeps the settings the file last applied.

> **Note**: The file holds your WiFi password in plain text. Anyone with the microSD card can read it.

> **Upgrading from v2.4.1 or earlier**: older firmware read the file only during the first setup. If you set up your device with a `.wificonf` file and left it on the card, from v2.4.2 on it is applied at every boot and replaces any WiFi or TCP/IP settings you changed later on the Network page. Delete or rename the file if you manage WiFi from the web interface.

## Booster Manager Mode

In **Manager mode**, the Booster app tries to connect to your configured WiFi network and then connects to the public repository of microfirmware apps.

If your network supports `.local` name resolution, you can access the web interface using `http://sidecart.local`. Otherwise, use the IP address shown on the Atari ST screen: the one assigned by your DHCP server, or the static address set on the [Network view](#network-view) or in a [`.wificonf` file](#configuring-wifi-from-a-file).

The menu at the top of the web interface opens the [Apps](#apps-view), [Network](#network-view) and [Config](#config-view) views. On a phone it is folded behind a **Menu** button and opens over the page. Its last three items open in a new tab: **Help** goes to the online documentation, **Report Issue** to the page for reporting a problem, and **News** to the [SidecarTridge news channel on Telegram](https://t.me/s/sidecartridgenews). The [Upgrade view](#upgrade-view) is not in the menu: it opens from the banner shown when a new firmware version is available.

If WiFi retries are exhausted, Booster falls back to an **offline Manager mode**. In that state network features are disabled, but the Atari ST terminal remains available so you can still boot already-downloaded microfirmwares manually.

The Atari ST Manager screen and the shared system information area in the web UI show the current MAC address and WiFi signal strength. When connected, Booster also answers ICMP ping requests to simplify network diagnostics.

> **Note**: The device needs a SDHC or SDXC microSD card formatted with **exFAT** or **FAT32** file system to work properly. The app will warn the user if the microSD card is not present or not formatted correctly.

### Apps view

The **Apps view** shows:

- The list of installable apps from the public repository.
- The list of installed apps on the device.
- The list of apps that are not installed but are available on the microSD card.

This view is the default view when you access the web interface. It lists the catalog selected on the [Config view](#config-view). Apps are shown as cartridges on shelves, one shelf per topic. Apps with an update come first, then the other installed apps. Swipe a shelf or use its arrows to see more.

To find an app, type in the search box: it looks in the names, descriptions, tags and creators. The dropdowns narrow the list by computer, release type and creator, and the chips pick one of the tags shared by several apps. While a filter is on, the shelves become a single list of the matching apps, and **Clear filters** brings the shelves back.

![Booster Manager Apps View 1](/docs/BOOSTER-MANAGER-APPS-1.png)

The colour of a cartridge shows the state of the app:
- **Grey**: not installed.
- **Putty, the colour of the Atari ST**: installed.
- **Reddish, with an exclamation mark**: installed, and a newer version is available.

Each cartridge has one main button: **Install**, or **Launch** if the app is installed. Select the cartridge, or its **i** or exclamation mark button, to open the app's panel, which shows:
- **Name**: The name of the app.
- **Version**: The version of the app.
- **Description**: A short description of the app.
- **Features**: A list of features supported by the app as a taxonomy.
- **Computer**: The computers supported by the app as a taxonomy.

If the catalog publishes more than one version of an app, a **Version** dropdown appears in the panel. The latest version is selected by default; older versions, when available, are listed in semantic-version order. The currently installed version is tagged "(current)" when it is not the latest.

The panel has the following buttons:
- **Install vX / Update to vX / Downgrade to vX**: A single context-aware action button that installs the version selected in the dropdown. Its label and color reflect what the action will do relative to what is already installed. The button is hidden when the selected version is the one already installed. Downgrading to an older version requires an extra confirmation.
- **Delete**: Delete the app from the device microSD card. You can download the app again if you want to.

![Booster Manager Apps View 2](/docs/BOOSTER-MANAGER-APPS-2.png)

- **Launch**: Launch the app. This will install the app in the flash memory of the device and after reboot it will be launched automatically. If you want to launch an app that is already installed, you can use the **Launch** button at any time.
- **Restore and launch** and **Restore config only**: Shown only on the DEV APP card. See [DEV APP: tools for microfirmware developers](#dev-app-tools-for-microfirmware-developers).

On Atari ST systems, Manager mode also supports terminal-driven manual boot: press `ESC` to enter the apps workflow, or press any `SHIFT` key to continue booting from GEMDOS. The apps workflow lists the downloaded apps 18 to a page: press `N` and `P` for the next and previous page, and type an app's number to launch it from any page.

![Booster Manager Apps View 3](/docs/BOOSTER-MANAGER-APPS-3.png)

When launching an app, the computer screen will show a message indicating that the app is being launched. The Booster app will make its best effort to also reboot the computer, but this may not always be possible. So assume that the computer is not rebooted and you need to do it manually.

The Booster microfirmware app is now installed in the flash memory of the device, and every time you power on the device, it will automatically launch the app. 

To return to the Booster app, the user needs to use the command menu on the microfirmware app to do it, or press the SELECT button for more than 10 seconds and then power off and power on the device and the computer.

#### Settings of each app

Each app keeps its settings in one of 30 slots in the device's flash memory. An app gets a slot the first time it is launched, not when it is installed, so apps you install but never run do not use one. When all 30 slots are in use, launching another app moves the settings of the app you launched least recently to the microSD card, as `<uuid>.cfg` in the apps folder, and that app gets them back the next time you launch it. So you can install and use any number of apps, and each keeps its settings.

On the Apps page, an app whose settings are on the card says so under its name. The `.cfg` files travel with the card: another device that launches the app restores those settings. The apps folder also holds `.lookup`, a copy of the device's list of slots, which the device uses to repair that list if the power is cut while it is being saved; a copy made by another device is ignored. Deleting an app removes its `.cfg` too, as do **Restore config only** and **Restore and launch** on the DEV APP card. While the **Development** catalog is selected, the DEV APP keeps its slot and its settings are never moved to the card.

#### DEV APP: tools for microfirmware developers

The **DEV APP** is the entry for a microfirmware you are writing. It uses a fixed UUID, `44444444-4444-4444-8444-444444444444`, reserved for development. To use it, select **Development - Local development only** as the apps catalog on the [Config view](#config-view), then install **DEV APP** from the Apps view like any other app. While the Development catalog is selected, the help at the top of the Apps view also describes its restore buttons.

The DEV APP card offers these actions:

- **Launch**: Runs your latest upload from the [developer deploy API](docs/DEPLOY-API.md), if there is one. Without an upload it leaves the microfirmware slot untouched, so a microfirmware you flashed with a debug probe runs as flashed. The binary that installing the DEV APP downloads is a placeholder and is never flashed by **Launch**.
- **Restore and launch**: Starts the DEV APP from a clean slate. It deletes your last upload, erases the DEV APP's saved settings, writes the DEV APP version downloaded from the catalog into the microfirmware slot, and launches it. Today that version is a placeholder that hands control straight back to Booster. It asks for confirmation first and cannot be undone.
- **Restore config only**: Erases the DEV APP's saved settings, so your microfirmware starts with its default settings the next time it runs. The microfirmware in the slot and your upload are kept, and nothing is launched. It asks for confirmation and shows the result in the same dialog.
- **Delete**: Removes the DEV APP from the microSD card, including your upload. Reinstalling the DEV APP from the catalog keeps an existing upload.

The restore buttons are only on the web page. They are not part of the deploy API, which is described in [docs/DEPLOY-API.md](docs/DEPLOY-API.md).

### Network view

The **Network view** shows the list of available WiFi networks and permits some basic network configuration options:

![Booster Manager WiFi View](/docs/BOOSTER-MANAGER-WIFI-1.png)

- **Country**: The country code of the WiFi network. This is used to configure the WiFi module to use the correct channels and frequencies.
- **Hostname**: The hostname of the device. This is used to identify the device on the network. By default is `sidecart`. 
- **Wifi Power**: The power of the WiFi module. From 0 to 4. 
- **Show RSSI**: Show the RSSI of the WiFi module. This is used to show the signal strength of the WiFi network.
- **DHCP Enabled**: Enable or disable DHCP for the device. If disabled, the TCP/IP settings view lets you enter a static IPv4 address, netmask, gateway, and DNS servers (one, or two separated by a comma; optional). They are checked when you click **Save**: if one is not valid, the page names it and nothing is saved.

The system information banner also shows the Pico W MAC address, which is useful when configuring router allow-lists or debugging network issues.

Don't forget to click the **Save** button to save the changes!

> **Note**: While a `.wificonf` file is on the microSD card, a banner at the top of this page says so, and the **TCP/IP configuration** and the **Network list** are grayed out and read-only. The file applies those settings again at every boot, so a change made here would be lost at the next reboot. The WiFi settings (country, hostname, power and RSSI) stay editable. To change the network from this page, delete or rename the file and power-cycle the device. See [Configuring WiFi from a file](#configuring-wifi-from-a-file).

It's also possible to change the WiFi network. To do this, click on the new WiFi network from the list. A new window will open with the WiFi network details. Enter the password and click the **Connect** button.

![Booster Manager WiFi View 2](/docs/BOOSTER-MANAGER-WIFI-2.png)

The Booster app will save the WiFi credentials to flash memory and reboot. It will then connect to your WiFi network and show a message on the computer screen.

> **Note**: If Booster cannot connect to the configured WiFi network, it stays in offline Manager mode. To restore the WiFi factory defaults and configure the network again, hold the SELECT button for more than ten seconds and then power-cycle the device and computer.

### Config view

The **Config view** shows the device information and some basic configuration options:

![Booster Manager Device View](/docs/BOOSTER-MANAGER-CONFIG-1.png)

- **Apps folder**: The folder where the apps are stored. By default is `/apps`. This folder is created on the microSD card when the Booster app is launched for the first time if it does not exist.
- **Apps catalog URL**: The catalog the Apps view loads. Choose **Stable - Tested release** (the default), **Testing - unstable releases**, **Development - Local development only**, or **Custom catalog URL** to enter your own. The three channels are served from `https://md-store.sidecartridge.com/atari-st/`. Microfirmware developers need **Development**: it lists the DEV APP and, once the DEV APP is installed, enables the [deploy API](docs/DEPLOY-API.md).
- **Boot feature**: The UUID of the microfirmware app to load at boot time, or the Booster factory path. If the configured app cannot be found in flash memory, Booster falls back to **Manager mode**.
- **SD card baud rate (KB)**: The baud rate of the SD card. By default is `12500`. It can be safely increased to `25000`. Above this value, the SD card may not work properly or the value will be ignored.

Don't forget to click the **Save** button to save the changes!

It also provides two buttons to reboot the device:

- **Reboot the device**: Reboot the device and load the microfirmware app in flash memory. This is the normal way to reboot the device.
- **Restore to the default fabric settings**: Reboot the device and load the Booster app in **Factory mode**. This is useful to reset the WiFi configuration or return the device to its default setup.

### Upgrade view

The **Upgrade view** installs a new version of the Booster firmware over WiFi. It has no entry in the menu. When Booster starts in Manager mode it checks the latest published version, and if it is newer than the one running, a **Firmware update available** banner appears at the top of the Apps, Network and Config views. The banner shows the new version, a link to the CHANGELOG and an **Upgrade** button that opens this view.

The upgrade has two steps. Keep the page open and do not refresh it until it finishes.

1. **Download.** Click **Continue upgrading**. The device downloads the checksum of the new firmware (`upgrade.md5`) and then the firmware itself (`upgrade.bin`) to the apps folder on the microSD card. A progress bar shows how much has arrived. When the download completes, the device checks the firmware against the checksum. This can take up to two minutes, depending on your network.
2. **Install.** Click **Confirm firmware upgrade**. The device restarts into the installer, which checks the whole file on the microSD card, writes the new firmware to flash, reads it back, and starts it. This takes about a minute. After 60 seconds the page reloads the web interface.

Do not power off the device while it installs. If the power is cut anyway, or the card cannot be read, power the device on again with the same card in: the installer starts over. A file that cannot be read in full is refused before anything is written, and the current firmware keeps running.

The upgrade is refused, and nothing is written to flash, if the checksum file cannot be downloaded or read, or if the downloaded firmware does not match it. The page shows the reason, for example *Checksum mismatch: the download is corrupt or incomplete*, and a **Back to Home** button. The current firmware keeps running, so you can try again later.

After the upgrade:

- The device starts in Manager mode with the new firmware.
- The WiFi and network settings, the Config view settings and the settings saved by each microfirmware are kept.
- The microfirmware that was in the device is replaced by the placeholder that ships with the firmware. The app is still on the microSD card: launch it again from the [Apps view](#apps-view).
- The downloaded `upgrade.bin` is deleted from the microSD card.


## License

The source code of the project is licensed under the GNU General Public License v3.0. The full license is accessible in the [LICENSE](LICENSE) file. 

The project can also be licensed under a commercial license. Please contact the author for more information.
