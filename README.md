# CYD 4.0" Multitool firmware

Touchscreen firmware for the **4.0" ESP32-32E "Cheap Yellow Display"**: ST7796 320×480 LCD, XPT2046 resistive touch, micro-SD, speaker amp, RGB LED and a CH340C USB port (LCDWiki E32R40T / E32N40T).

| App | What it does |
|---|---|
| **Wi-Fi** | Scans nearby networks and shows SSID, BSSID, channel, security and RSSI. Has a 2.4 GHz channel-usage graph and a details page, and connects to a network using an on-screen keyboard. |
| **Bluetooth** | Live BLE scanner, sorted by signal. Shows vendor decoding and flags trackers (AirTag/Find My, Samsung SmartTag, Tile, Fast Pair…). The details page shows services and manufacturer data, plus a live RSSI meter you can use to find a device. |
| **Smart Home** | **Home Assistant**: lights (on/off, brightness, colour), switches, fans, thermostats, locks, covers, scenes, scripts, media players and **camera/doorbell snapshots**. **Google Nest** (direct, via Google's Smart Device Management API): thermostat setpoint and mode, plus doorbell/camera status. |
| **Music** | Plays MP3/WAV/AAC from the SD card. Has a library browser, shuffle, a **5-band EQ** with presets, and **visualisers**: oscilloscope, spectrum bars and scrolling spectrogram. Music keeps playing in the background while you use other apps. |
| **Flock Detect** | Passive detector for Flock Safety ALPR cameras. It sniffs Wi-Fi management frames on channels 1–13 and listens to BLE adverts, then matches them against community-published signatures. A match turns the RGB LED red, beeps, and is logged to `/flock_log.csv`. |
| **Settings** | Brightness, volume, colour fixes (invert / swap R-B), touch calibration, reloading the SD config, forgetting Wi-Fi, system info and reboot. |

The **BOOT** button works as "back" everywhere.

---

## 1. Flash it

### Option A: prebuilt image (fastest)
`firmware/cyd-multitool-merged.bin` is a full image that goes at offset **0x0**.

* **Browser:** open <https://espressif.github.io/esptool-js/> in Chrome or Edge, click Connect, add the file at address `0x0`, then Program.
* **esptool:** `esptool.py --chip esp32 --baud 921600 write_flash 0x0 firmware/cyd-multitool-merged.bin`

If the board won't connect, hold **BOOT**, tap **RESET**, then release BOOT.

### Option B: build from source (PlatformIO)
```bash
pip install platformio
pio run -t upload          # build + flash
pio device monitor         # serial log at 115200
```
GitHub Actions builds the firmware on every push. You can download the firmware from the run's **Artifacts**.

## 2. Prepare the SD card (FAT32)
```
/config.json        <- copy sdcard/config.example.json, fill in your details
/music/...          <- your .mp3 / .wav / .aac files (subfolders OK)
```
The firmware reads `config.json` at boot and copies it into flash, so you can remove the card afterwards (except for music). After editing it, use **Settings → Reload SD cfg**.

You can also set up Wi-Fi without the SD card: **Wi-Fi app → tap a network → Connect**.

`timezone` takes a POSIX TZ string, e.g. `EST5EDT,M3.2.0,M11.1.0`, `PST8PDT,M3.2.0,M11.1.0`, `GMT0BST,M3.5.0/1,M10.5.0` or `CET-1CEST,M3.5.0,M10.5.0/3`.

## 3. First boot
On first boot you're asked to **calibrate the touchscreen**: tap the tip of each arrow. To recalibrate later, touch the screen (or hold BOOT) during the splash screen, or use Settings → Calibrate.

If colours look wrong (negative, or blue and red swapped), toggle **Invert colors** or **Swap R/B** in Settings. The setting is saved.

## 4. Smart-home setup

### Home Assistant (recommended, covers almost everything)
Google Home has no public API for controlling arbitrary lights or plugs. The reliable way to control *"Google Home stuff"* from a gadget like this is to bring those devices into [Home Assistant](https://www.home-assistant.io/). HA has integrations for Google Nest, Philips Hue, TP-Link Kasa/Tapo, WiZ, LIFX, Govee, Ecobee, Ring, etc. This firmware then talks to HA's REST API.

1. In HA: **Profile → Security → Long-lived access tokens → Create token**.
2. In `config.json`, set `ha_url` (e.g. `http://192.168.1.10:8123`) and `ha_token`.
3. Optional: the `ha_entities` list (e.g. `["light.kitchen", "climate.hallway", "camera.front_door"]`) limits what's shown. If it's left empty, all lights, switches, fans, climate, locks, covers, cameras, scenes, scripts, input booleans and media players are listed.

Doorbell: add the Nest doorbell (or Ring, Reolink, …) to HA. Its `camera.*` entity then shows a snapshot that refreshes every 5 s. Tap the image to pause.

### Google Nest direct (thermostat), optional
This uses Google's official [Smart Device Management API](https://developers.google.com/nest/device-access/get-started):
1. Register for **Device Access** (one-time US$5) and create a project. Note its **Project ID**.
2. In Google Cloud Console, create an **OAuth client (Web application)** with redirect URI `https://www.google.com`, and enable the *Smart Device Management API*.
3. Open the authorization URL in a browser, approve, and copy the `code=` value from the redirect:
   `https://nestservices.google.com/partnerconnections/PROJECT_ID/auth?redirect_uri=https://www.google.com&access_type=offline&prompt=consent&client_id=CLIENT_ID&response_type=code&scope=https://www.googleapis.com/auth/sdm.service`
4. Exchange the code for a refresh token:
   ```bash
   curl -X POST https://oauth2.googleapis.com/token \
     -d client_id=CLIENT_ID -d client_secret=CLIENT_SECRET -d code=CODE \
     -d grant_type=authorization_code -d redirect_uri=https://www.google.com
   ```
5. Put `nest_project_id`, `nest_client_id`, `nest_client_secret` and `nest_refresh_token` into `config.json`.

The SDM API doesn't provide on-demand snapshots for doorbells or cameras. Live video is WebRTC, which is out of reach for an ESP32. Use the HA route for doorbell images.

## 5. Flock detector: how it works and its limits
* **Wi-Fi:** the radio enters promiscuous mode, hops channels 1–13 every 250 ms, and checks beacons, probe requests and probe responses. A frame matches if its SSID contains `flock`, `fs ext battery`, `penguin` or `pigvision`, or its transmitter MAC has a known Flock OUI.
* **BLE:** a passive scan checks advertised names (same patterns), public-address OUIs, and manufacturer company ID `0x09C8`.
* The signatures in `src/flock_signatures.h` come from public community research (e.g. the open-source *flock-you* project and DeFlock). **They are heuristics:** expect some false positives and misses. Confirm a hit by looking for the camera. You can add patterns without reflashing using `flock_extra_ssids` and `flock_extra_ouis` in `config.json`.
* While the detector runs, Wi-Fi disconnects from your network, and it reconnects when you leave the app. It is receive-only: nothing is transmitted.
* The log is `/flock_log.csv`: time, uptime, source, MAC, RSSI, channel, label, reason and count. Times are real clock times if NTP synced before you started.

## 6. Hardware notes / pin map
All pins are in [`include/pins.h`](include/pins.h):

| Function | GPIO |
|---|---|
| LCD SPI (SCK/MOSI/MISO/CS/DC/BL) | 14 / 13 / 12 / 15 / 2 / 27 |
| Touch XPT2046 (CS / IRQ) | 33 / 36 |
| SD card (SCK/MISO/MOSI/CS) | 18 / 19 / 23 / 5 |
| RGB LED (R/G/B, active-low) | 22 / 16 / 17 |
| Speaker: DAC out / amp enable (active-low) | 26 / 4 |
| Free: I2C header SDA/SCL, inputs | 32 / 25, 35 / 39 |

Audio goes through the ESP32's **8-bit internal DAC** into the on-board mono amp. It's fine for a small speaker, but don't expect hi-fi. With the EQ, the volume and the visualisers, 128–192 kbps MP3s at 44.1 kHz work well.

## 7. Troubleshooting
| Symptom | Fix |
|---|---|
| White or blank screen | Check `PIN_LCD_*` in `pins.h`. Some clones use a different backlight pin. |
| Touch is offset or mirrored | Settings → Calibrate. |
| No sound | Flip `AUDIO_EN_LEVEL` in `pins.h` (some revisions use active-high amp enable). Check the speaker is on the 2-pin "SPEAKER" JST. |
| "Can't reach Home Assistant" | Use the IP address instead of `.local`. Check the token, and that the board is on the same network. |
| Nest error 401/403 | The refresh token was revoked, or the account isn't linked in Device Access. Redo steps 3–4. |
| Camera "can't decode" | The camera produces progressive JPEG, or the image is >90 KB. Lower the camera's snapshot resolution in HA. |

## 8. Security note
HTTPS requests (Google APIs and an `https://` Home Assistant) are encrypted but **don't verify the server certificate**, to keep the firmware small. Tokens are stored in the ESP32's NVS flash. Use a plain-HTTP HA URL only on your own LAN, and treat the device as holding your HA token.

## Project layout
```
include/pins.h          board pin map
src/main.cpp            launcher, boot, touch calibration
src/ui.*                widgets, list view, on-screen keyboard
src/settings.*          NVS + SD config.json
src/net.*               Wi-Fi, NTP, HTTP helpers
src/app_wifi.cpp        Wi-Fi scanner
src/app_ble.cpp         BLE scanner (+ ble_scan.*)
src/app_home.cpp        Home Assistant + Google Nest
src/app_music.cpp       player UI, EQ screen, visualisers
src/audio_engine.*      decoder chain, EQ DSP, DAC output (+ dsp.h)
src/app_flock.cpp       Flock detector (+ flock_signatures.h)
src/app_settings.cpp    settings
```
