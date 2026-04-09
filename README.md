<div align="center">
  <img src="https://github.com/user-attachments/assets/0eba90bc-2ff5-40df-88a1-92e23396d1d3" alt="logo" width="100" height="auto" />
  
  <h1>nyanBOX</h1>
  <p>Integrated Platform for BLE and 2.4GHz Wireless Analysis</p>
  <p>By Nyan Devices | Maintained by jbohack & zr_crackiin</p>

  <!-- Badges -->
  <p>
    <a href="https://github.com/jbohack/nyanBOX" title="GitHub repo">
      <img src="https://img.shields.io/static/v1?label=nyanBOX&message=jbohack&color=purple&logo=github" alt="nyanBOX - jbohack">
    </a>
    <a href="https://github.com/jbohack/nyanBOX">
      <img src="https://img.shields.io/github/stars/jbohack/nyanBOX?style=social" alt="stars - nyanBOX">
    </a>
    <a href="https://github.com/jbohack/nyanBOX">
      <img src="https://img.shields.io/github/forks/jbohack/nyanBOX?style=social" alt="forks - nyanBOX">
    </a>
  </p>

  <h3>
    <a href="https://nyandevices.com">🌐 Learn More</a> ·
    <a href="https://shop.nyandevices.com">🛒 Purchase nyanBOX</a> ·
    <a href="https://discord.gg/J5A3zDC2y8">💬 Join Discord</a>
  </h3>
</div>

---

## What is nyanBOX?

**nyanBOX** is a compact, comprehensive 2.4GHz wireless analysis device designed for professionals working with Bluetooth, BLE, Wi-Fi, and other protocols operating within the 2.4GHz spectrum. It functions as a versatile, portable platform suitable for security researchers, penetration testers, engineers, and technical hobbyists seeking an in-depth understanding of wireless communication and device behavior.

Powered by an ESP32, three NRF24 modules, a high-clarity OLED display, and a 2500mAh rechargeable battery, nyanBOX enables effective wireless scanning, detection, and analysis in the field with no external hardware required. Typical applications include identifying BLE devices, detecting tracking beacons such as AirTags, locating skimming devices, examining RF channel activity, and performing authorized wireless security evaluations.

**→ [Learn more at nyandevices.com](https://nyandevices.com)**

<div align="center">
  <img src="https://github.com/user-attachments/assets/530e5686-09db-4f02-aabe-80a8abcbb036" alt="nyanBOX Interface" width="650" />
</div>

---

## ⚡ Key Advantages

- **Plug & Play Operation** – USB-C powered and ready for immediate use  
- **Extended Battery Life** – 2500mAh battery supports up to a full day of typical operation  
- **Progress Monitoring System** – Integrated leveling system providing usage insights  
- **Open-Source Firmware** – Fully customizable with active community contributions  
- **Comprehensive 2.4GHz Suite** – Over 20 integrated tools for BLE, Bluetooth, Wi-Fi, and RF diagnostics  
- **Portable Design** – Compact form factor optimized for field work  
- **Regular Updates** – Ongoing feature additions and improvements

**Interested? [Purchase nyanBOX at shop.nyandevices.com](https://shop.nyandevices.com)**

---

## 🎯 Features & Capabilities

> **⚠️ Note:** Some advanced tools may require activation through the Settings menu.

### 📶 WiFi Tools
- **WiFi Scanner** – Detects nearby WiFi access points with full client detection. View connected clients for each network, monitor their signal strength, packet activity, and deauthenticate individual clients.
- **Channel Analyzer** – Visualizes WiFi congestion across all channels with a real-time bar chart to identify the best channel for your network
- **WiFi Deauther** – Educational tool for testing network security with deauthentication frames on authorized networks
- **Deauth Scanner** – Monitors and analyzes WiFi deauthentication frames in real-time. Displays the source MAC, channel, and live RSSI of the deauthing transmitter. Use it to physically locate the source of a deauth attack.
- **Beacon Spam** – Broadcasts multiple fake WiFi networks for testing. Choose to clone real nearby networks, select specific SSIDs, or use a list of random names.
- **Evil Portal** – Creates captive portal with multiple templates (Generic, Facebook, Google) that automatically scans nearby networks for realistic SSID spoofing and credential capture.
- **Pineapple Detector** – Detect and identify nearby Pineapple devices
- **Pwnagotchi Detector** – Detects nearby Pwnagotchi devices and displays their information
- **Pwnagotchi Spam** - Pwnagotchi grid flooding tool that generates fake beacon frames with randomized identities, faces, names, and versions (contains optional DoS mode).

### 🔵 Bluetooth (BLE) Tools
- **BLE Scanner** – Detects nearby BLE devices
- **BLE Inspector** – Decodes raw BLE advertising packets from nearby devices, displaying service UUIDs, manufacturer data, TX power, flags, and raw payloads.
- **nyanBOX Detector** – Discovers nearby nyanBOX devices and displays their information including level, version, and signal strength.
- **Flipper Scanner** – Detects nearby Flipper Zero devices
- **Axon Detector** – Detects nearby Axon devices (body cameras, tasers, and other law enforcement equipment)
- **Meshtastic Detector** - Detects nearby devices running Meshtastic firmware
- **MeshCore Detector** - Detects nearby devices running MeshCore firmware
- **Skimmer Detector** – Detects HC-03, HC-05, and HC-06 Bluetooth modules commonly used in credit card skimming devices.
- **AirTag Detector** – Scans for and identifies nearby Apple AirTag devices.
- **AirTag Spoofer** – Clones and rebroadcasts detected Apple AirTag devices for selective or bulk spoofing.
- **SmartTag Detector** - Scans for and identifies nearby Samsung SmartTag devices.
- **Tile Detector** - Scans for and identifies nearby Tile Tracker devices.
- **RayBan Detector** - Scans for and identifies nearby RayBan Meta smart glasses.
- **BLE Spammer** – Broadcasts BLE advertisement packets for testing
- **Swift Pair** - Triggers Windows Swift Pair notifications by broadcasting fake Microsoft device advertisements.
- **Sour Apple** – Mimics Apple Bluetooth signals like AirPods pairing pop-up to test device resilience against protocol exploits.
- **Sour Droid** – Floods nearby Android and Samsung devices with Google FastPair and Samsung EasySetup pairing notifications by cycling through hundreds of device models to test protocol resilience.
- **BLE Spoofer** – Clones and rebroadcasts detected BLE devices with complete 1:1 replication of MAC address, name, advertising data, scan response, and connectable state.

### 📡 Signal & Protocol Tools
- **Drone Detector** – Detects nearby drones broadcasting RemoteID via WiFi and BLE. Displays drone identification, GPS location, altitude, speed, operator information, and flight status. Features a locate mode with real-time RSSI signal strength meter to help pinpoint drone positions.
- **Drone Spoofer** – Broadcasts fake Open Drone ID (ODID) Remote ID packets over BLE and WiFi per the ASTM F3411 spec. Generates randomized drone identities, GPS coordinates, altitudes, speeds, and operator IDs.
- **Flock Detector** - Detects Flock Safety surveillance cameras using dual-mode WiFi and BLE scanning. Identifies devices through SSID patterns, MAC OUI prefixes, and Bluetooth device names. Features real-time signal strength tracking with detailed device info and a locate mode for pinpointing camera positions.
- **Device Scout** – Wireless device scanner combining Bluetooth and WiFi detection with anti-surveillance capabilities. Discovers nearby devices and ranks by persistence to identify trackers following you.
- **Scanner** – Scans the 2.4GHz frequency band to detect active channels and devices
- **Analyzer** – Real-time spectrum analyzer with channel filters for targeted RF analysis. Features dynamic display with auto-scaling, peak frequency detection, and instant filter switching via left/right buttons. Analyze WiFi, Bluetooth, or custom frequency bands.

---

## 🎮 Leveling System

The integrated leveling system offers structured, persistent feedback on device usage:

- **Progress Tracking** – XP earned by using various tool categories  
- **Rank Advancement** – Nine rank tiers available  
- **Usage Analysis** – Tools award XP at varying rates  
- **Session Bonuses** – Extended use yields additional progression  
- **Persistent Storage** – Data stored in EEPROM across power cycles  
- **Reset Option** – Users may reset progress via the Settings menu  
- **Device Networking** – Level and version broadcast for discovery by other nyanBOX units  

Access detailed statistics via the RIGHT directional button in the main menu.

---

## 🛠️ Hardware Specifications

| Component        | Details                                      |
|-----------------:|----------------------------------------------|
| Microcontroller  | ESP32 WROOM-32U (dual-core, Wi-Fi + BLE)     |
| Wireless Modules | 3× NRF24 GTmini modules                      |
| Display          | 0.96" OLED                                   |
| Power            | USB-C + 2500mAh rechargeable battery         |
| Battery Life     | Up to one full day of typical usage          |
| Case             | Protective enclosure included                |
| Debug Interface  | UART                                          |

Purchase: https://shop.nyandevices.com

---

## 🚀 Getting Started

### First-Time Setup

Purchase a nyanBOX from **[shop.nyandevices.com](https://shop.nyandevices.com)** and install firmware within minutes using the web-based flashing tool.

### Firmware Installation & Updates

#### Recommended: Web Flasher
1. Visit **[nyandevices.com/flasher](https://nyandevices.com/flasher)**  
2. Connect the device via USB-C  
3. Select **Install nyanBOX Firmware**  
4. The installation completes automatically  

#### Advanced: PlatformIO
1. Install [VS Code](https://code.visualstudio.com/) and [PlatformIO](https://platformio.org/install/ide?install=vscode)  
2. Clone or download the repository  
3. Open the project in VS Code  
4. Select Upload in PlatformIO  
5. The device will flash and restart  

**Troubleshooting:**  
- Port unavailable: install [CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)  
- Upload failure: hold the BOOT button during flashing  
- Need assistance? Join the [Discord community](https://discord.gg/J5A3zDC2y8)

---

## ⚠️ Legal Disclaimer

**Use nyanBOX responsibly and ethically.**  
It is designed strictly for authorized testing, research, and educational purposes.

- Do not perform unauthorized network attacks  
- Obtain explicit permission prior to conducting assessments  
- Comply with all applicable local laws and regulations  
- Users are fully responsible for their actions  

By using nyanBOX, you agree to adhere to lawful and ethical usage practices.

---

## ❓ FAQ

**Is nyanBOX legal to own?**  
Yes. Ownership is legal, though specific features may be subject to local restrictions.

**How long does the battery last?**  
Up to one full day during typical operation. Continuous intensive scanning may reduce runtime.

**Can I develop my own tools?**  
Yes. The firmware is open source and supports extensive customization.

**Does it come with firmware pre-installed?**  
Devices ship ready for flashing. The web flasher enables installation in minutes.

---

## 💬 Join the Community

Have questions or need assistance?

- **[Discord](https://discord.gg/J5A3zDC2y8)** – Primary community hub  
- **[GitHub Issues](https://github.com/jbohack/nyanBOX/issues)** – Bug reports and feature requests  
- **[nyandevices.com](https://nyandevices.com)** – Documentation and guides  

---

## 💝 Support the Project

If you find nyanBOX valuable, consider supporting development:

- ⭐ Star this repository  
- 🛒 **[Purchase at shop.nyandevices.com](https://shop.nyandevices.com)**  
- ☕ Support the developers:  
  - [jbohack on Ko-fi](https://ko-fi.com/jbohack)  
  - [zr_crackiin on Ko-fi](https://ko-fi.com/zrcrackiin)  
- 🗣️ Share with others interested in wireless research  

### Built By
- [jbohack](https://github.com/jbohack)
- [zr_crackiin](https://github.com/zRCrackiiN)

---

## 🙏 Thanks To

- [Poor Man's 2.4 GHz Scanner](https://forum.arduino.cc/t/poor-mans-2-4-ghz-scanner/54846)
- [arduino_oled_menu](https://github.com/upiir/arduino_oled_menu)
- [Universal-RC-system](https://github.com/alexbeliaev/Universal-RC-system)
- [AppleJuice](https://github.com/ECTO-1A/AppleJuice)
- [ESP32-Sour-Apple](https://github.com/RapierXbox/ESP32-Sour-Apple)
- [PwnGridSpam](https://github.com/7h30th3r0n3/PwnGridSpam)
- [ESP32-AirTag-Scanner](https://github.com/MatthewKuKanich/ESP32-AirTag-Scanner)
- [BLE Spam Flipper Application](https://github.com/Next-Flip/Momentum-Apps/tree/c470da2d792fc8c4f165ae2906d79250c33a823c/ble_spam)
- [opendroneid-core-c](https://github.com/opendroneid/opendroneid-core-c)
- [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
- [Flock You](https://github.com/colonelpanichacks/flock-you)
- [Original nRFBOX Project](https://github.com/cifertech/nrfbox)

Thank you to all contributors, testers, supporters, and community members.

---

## 📜 License

MIT License – see [LICENSE](LICENSE) for details.

---

<div align="center">
  <h3>Ready to explore the 2.4GHz spectrum?</h3>
  <p>
    <a href="https://shop.nyandevices.com"><strong>🛒 Purchase nyanBOX</strong></a>
  </p>
  <p>#BadgeLife</p>
</div>
