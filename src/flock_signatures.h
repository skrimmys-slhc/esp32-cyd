#pragma once
// ---------------------------------------------------------------------------
// Heuristic signatures for Flock Safety ALPR cameras and their accessories.
//
// These come from public, community-sourced research (e.g. the open-source
// "flock-you" detector and DeFlock mapping project). They are NOT an official
// list: expect false positives (shared Wi-Fi module vendors) and misses
// (firmware changes). Add your own via "flock_extra_ssids" / "flock_extra_ouis"
// in /config.json on the SD card.
// ---------------------------------------------------------------------------

// Case-insensitive substrings matched against Wi-Fi SSIDs (beacons, probe
// requests/responses) and BLE advertised names.
static const char* const FLOCK_NAME_PATTERNS[] = {
    "flock",
    "fs ext battery",
    "penguin",
    "pigvision",
};

// MAC address prefixes (OUI) seen on Flock hardware. Matched against Wi-Fi
// transmitter addresses and BLE public addresses.
static const char* const FLOCK_OUIS[] = {
    "58:8E:81", "CC:CC:CC", "EC:1B:BD", "90:35:EA", "04:0D:84", "F0:82:C0", "1C:34:F1",
    "38:5B:44", "94:34:69", "B4:E3:F9", "70:C9:4E", "3C:91:80", "D8:F3:BC", "80:30:49",
    "14:5A:FC", "74:4C:A1", "08:3A:88", "9C:2F:9D", "94:08:53", "E4:AA:EA",
};

// Bluetooth SIG company identifiers seen in Flock BLE manufacturer data.
static const uint16_t FLOCK_BLE_COMPANY_IDS[] = {
    0x09C8,  // XUNTONG
};
