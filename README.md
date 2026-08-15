<div align="center">
  <img src="https://github.com/user-attachments/assets/0eba90bc-2ff5-40df-88a1-92e23396d1d3" alt="logo" width="100" height="auto" />
  
  <h1>nyanBOX™</h1>
  <p>All-in-One Gadget for BLE and 2.4GHz Networks</p>
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
    <a href="https://shop.nyandevices.com">🛒 Buy nyanBOX</a> ·
    <a href="https://discord.gg/J5A3zDC2y8">💬 Join Discord</a>
  </h3>
</div>

---

## What is nyanBOX?

**nyanBOX** is your pocket-sized 2.4GHz wireless lab. Think of it as a swiss army knife for the entire 2.4GHz spectrum - Bluetooth, BLE, WiFi, and everything in between. Perfect for security researchers, pentesters, hackers, and curious tinkerers who want to understand how wireless protocols really work.

Built around an ESP32 with triple NRF24 modules, a crisp OLED display, and a 2500mAh rechargeable battery, nyanBOX lets you explore the invisible world of 2.4GHz radio anywhere - no cables needed. Scan for hidden Bluetooth devices, detect AirTags tracking you, find credit card skimmers, analyze RF signals, test wireless security, and way more.

**→ [Check out all the features at nyandevices.com](https://nyandevices.com)**

<div align="center">
  <img src="https://github.com/user-attachments/assets/530e5686-09db-4f02-aabe-80a8abcbb036" alt="nyanBOX Interface" width="650" />
</div>

---

## ⚡ Why You'll Love It

- **Plug & Play** – USB-C powered, works right out of the box
- **All-Day Battery** – 2500mAh battery provides up to a full day of portable use
- **Level Up System** – Built-in RPG mechanics track your progress as you explore wireless protocols
- **Complete 2.4GHz Toolkit** – 20+ built-in features for Bluetooth, BLE, WiFi, and RF analysis
- **Pocket-Sized** – Take it anywhere, scan everything
- **Active Updates** – New features added regularly by Nyan Devices

**Ready to dive in? [Purchase nyanBOX at shop.nyandevices.com](https://shop.nyandevices.com)**

---

## 🎯 What Can It Do?

> **⚠️ Note:** Additional advanced tools can be enabled in the Settings menu.

### 📶 WiFi Tools
- **WiFi Scanner** – Detects nearby WiFi access points with full client detection. View connected clients for each network, monitor their signal strength, packet activity, and deauthenticate individual clients.
- **Channel Analyzer** – Visualizes WiFi congestion across all channels with a real-time bar chart to identify the best channel for your network
- **Camera Detector** – Passively scans nearby WiFi traffic to detect and identify wireless security cameras. Fingerprints devices by MAC OUI and frame heuristics to identify Ring, Blink, Nest, Arlo, Wyze, Reolink, Eufy, Hikvision, Dahua, Axis, and 20+ more camera brands without transmitting a single packet. Displays the camera's MAC address, SSID, signal strength, and confidence level. Lock onto a detected camera and use the signal strength meter to physically locate it.
- **Camera Deauther** – Detects and deauthenticates wireless security cameras by OUI fingerprinting and frame heuristics. Targets Ring, Blink, Nest, Arlo, Wyze, Reolink, Eufy, Hikvision, Dahua, Axis, and 20+ more. Lock onto a detected camera and forcibly disconnect it from its network.
- **WiFi Deauther** – Educational tool for testing network security with deauthentication frames on authorized networks
- **Deauth Scanner** – Monitors and analyzes WiFi deauthentication frames in real-time. Displays the source MAC, channel, and live RSSI of the deauthing transmitter. Use it to physically locate the source of a deauth attack.
- **Packet Monitor** – Passive 802.11 packet capture in promiscuous mode with channel hopping, live packet counters by type, and CSV export over Serial.
- **Beacon Spam** – Broadcasts multiple fake WiFi networks for testing. Choose to clone real nearby networks, select specific SSIDs, or use a list of random names.
- **Evil Portal** – Creates a captive portal with multiple realistic templates (Google, Facebook, Apple ID, Microsoft, and Xfinity) that automatically scans nearby networks for realistic SSID spoofing and credential capture.
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
- **FindMy Beeper** – Scans for nearby Apple FindMy devices like AirTags, AirPods, and other FindMy accessories and triggers their built-in speaker. Choose Beep Selected to target a specific device or Beep All to sweep every FindMy device in range automatically.
- **SmartTag Detector** - Scans for and identifies nearby Samsung SmartTag devices.
- **Tile Detector** - Scans for and identifies nearby Tile Tracker devices.
- **RayBan Detector** - Scans for and identifies nearby RayBan Meta smart glasses.
- **iBeacon Detector** – Scans for and decodes Apple iBeacon BLE advertisements in real time. iBeacons are deployed in stores, airports, malls, and stadiums to track your movement and behavior.
- **iBeacon Spoofer** - Detects, clones, and rebroadcasts nearby Apple iBeacon advertisements. Clone individual beacons or all detected beacons simultaneously. By introducing duplicate beacon identifiers into the environment, iBeacon Spoofer can throw off beacon-based positioning systems and reduce the accuracy of indoor location tracking, movement analytics, and location-aware profiling used by retailers, venues, and third-party applications.
- **BLE Spammer** – Broadcasts BLE advertisement packets for testing
- **Swift Pair** - Triggers Windows Swift Pair notifications by broadcasting fake Microsoft device advertisements.
- **Sour Apple** – Mimics Apple Bluetooth signals like AirPods pairing pop-up to test device resilience against protocol exploits.
- **Sour Droid** – Floods nearby Android and Samsung devices with Google FastPair and Samsung EasySetup pairing notifications by cycling through hundreds of device models to test protocol resilience.
- **BLE Spoofer** – Clones and rebroadcasts detected BLE devices with complete 1:1 replication of MAC address, name, advertising data, scan response, and connectable state.

### 📡 Signal & Protocol Tools
- **Drone Detector** – Detects nearby drones broadcasting RemoteID via WiFi and BLE. Displays drone identification, GPS location, altitude, speed, operator information, and flight status. Features a locate mode with real-time RSSI signal strength meter to help pinpoint drone positions.
- **Drone Spoofer** – Broadcasts fake Open Drone ID (ODID) Remote ID packets over BLE and WiFi per the ASTM F3411 spec. Generates randomized drone identities, GPS coordinates, altitudes, speeds, and operator IDs.
- **Flock Detector** - Detects Flock Safety surveillance cameras using dual-mode WiFi and BLE scanning. Identifies devices through SSID patterns, MAC OUI prefixes, and Bluetooth device names. Uses promiscuous frame capture to detect cameras even when powered on and not actively broadcasting. Features real-time signal strength tracking with detailed device info and a locate mode for pinpointing camera positions.
- **LE Gear Detector** - Passively scans WiFi and BLE traffic to detect law enforcement equipment. Identifies body cams, radios, radar, lightbars, and other gear from Axon, Motorola, Hytera, and 40+ more vendors. Displays device category, confidence level, and signal strength, with a locate mode to pinpoint devices.
- **Device Scout** – Wireless device scanner combining Bluetooth and WiFi detection with anti-surveillance capabilities. Discovers nearby devices and ranks by persistence to identify trackers following you.
- **Scanner** – Scans the 2.4GHz frequency band to detect active channels and devices
- **Jam Detector** – Scans the Wi-Fi and Bluetooth band and establishes a baseline of your local wireless environment. Continuously updates the baseline in the background and alerts you when abnormal interference or signal saturation is detected nearby.
- **Analyzer** – Real-time spectrum analyzer with channel filters for targeted RF analysis. Features dynamic display with auto-scaling, peak frequency detection, and instant filter switching via left/right buttons. Analyze WiFi, Bluetooth, or custom frequency bands.

### 🎮 Leveling System
nyanBOX features a built-in RPG-style leveling system that tracks your usage throughout using the device:

- **Level Progression** – Gain XP by using different tools and features
- **Rank System** – Progress through 9 different ranks
- **Usage Tracking** – Different XP rates for scanning, attacks, and utilities/misc
- **Session Bonuses** – Extra XP for extended tool usage
- **Level Display** – Current level shown on main menu, detailed stats accessible via RIGHT arrow
- **Progress Persistence** – Level data saved to EEPROM, survives power cycles
- **XP Reset** – Reset progress via Settings menu if desired
- **Device Networking** – Your level and version are automatically broadcasted to nearby nyanBOX devices for discovery

Hit RIGHT in the main menu to check your stats. Level up by tinkering with RF signals and unlock ranks as you progress. Other nyanBOX users can see your progress when they scan for nearby devices!

---

## 🛠️ Hardware Specs

| Component  | Details                                  |
|-----------:|------------------------------------------|
| Microcontroller      | ESP32 WROOM‑32U (dual‑core, Wi‑Fi + BT)  |
| Wireless Modules     | 3× NRF24 GTmini modules        |
| Display    | 0.96" OLED                               |
| Power      | USB‑C + 2500mAh rechargeable battery     |
| Battery    | Up to a full day typical use             |
| Case       | Protective enclosure included            |
| Debug      | UART                                     |

Get yours: https://shop.nyandevices.com

---

## 🚀 Getting Started

### First Time Setup

Purchase a nyanBOX from **[shop.nyandevices.com](https://shop.nyandevices.com)** and flash the firmware in minutes using our web-based flasher!

### Firmware Installation & Updates

Get up and running or update to the latest features:

#### Easy Mode - Web Flasher (Recommended)
1. Head to **[nyandevices.com/flasher](https://nyandevices.com/flasher)**
2. Plug in your nyanBOX via USB-C
3. Click **Install nyanBOX Firmware**
4. Done!

**Troubleshooting:**
- Can't find the port? Install [CP210x drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- Upload failing? Hold the BOOT button while flashing
- Still stuck? Hit up our [Discord](https://discord.gg/J5A3zDC2y8) - we're here to help!

---

## ⚠️ Legal Disclaimer

**Use nyanBOX responsibly.** It's built for education, security research, and authorized testing only.

- Don’t attack networks you don’t own
- Always obtain permission before testing
- Know your local laws; some features may be restricted
- You’re responsible for how you use it

By using nyanBOX, you agree to use it ethically and legally. We're not liable for misuse.

---

## ❓ FAQ

**Is nyanBOX legal to own?**  
Yes, but some features may be restricted depending on your location and use. Follow local laws and obtain permission.

**How long does the battery last?**  
Up to a full day of typical use. Heavy continuous scanning may reduce runtime; lighter intermittent use can extend it.

**Can I request new tools or features?**  
Yes! Head to our **[Discord](https://discord.gg/J5A3zDC2y8)** and drop your request in. We're actively developing new features and community suggestions drive a lot of what gets added.

**Does it come with firmware pre‑installed?**  
nyanBOX ships ready to flash. Use the web flasher at https://nyandevices.com/flasher to get running in minutes.

---

## 💬 Join the Community

Got questions? Want to show off your device? Need help?

- **[Discord](https://discord.gg/J5A3zDC2y8)** - Questions, bug reports, feature requests, and community
- **[nyandevices.com](https://nyandevices.com)** - Full docs and guides

---

## 💝 Support the Project

Love nyanBOX? Here's how you can help:

- ⭐ Star this repo
- 🛒 **[Buy nyanBOX at shop.nyandevices.com](https://shop.nyandevices.com)**
- ☕ Buy us a coffee:
  - [jbohack's Ko-fi](https://ko-fi.com/jbohack)
  - [zr_crackiin's Ko-fi](https://ko-fi.com/zrcrackiin)
- 🗣️ Spread the word!

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
- [RF24](https://github.com/nRF24/RF24) 
- [Original nRFBOX Project](https://github.com/cifertech/nrfbox)

And thanks to everyone who's contributed code, reported bugs, purchased a device, or just shared the love. You're awesome!

---

---


<div align="center">
  <h3>Ready to explore the 2.4GHz spectrum?</h3>
  <p>
    <a href="https://shop.nyandevices.com"><strong>🛒 Buy nyanBOX Now</strong></a>
  </p>
  <p>#BadgeLife</p>
</div>
