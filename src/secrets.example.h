// Copy this file to secrets.h (same folder) and fill in your Wi-Fi and Home
// Assistant details. secrets.h is git-ignored so it never gets uploaded.
// The ESP32 only works on 2.4 GHz Wi-Fi.
#pragma once

const char* WIFI_SSID     = "your-wifi-name";
const char* WIFI_PASSWORD = "your-wifi-password";


// Home Assistant on the Raspberry Pi. Use its IP address if homeassistant.local
// doesn't work (Settings > System > Network shows it).
const char* HA_HOST  = "homeassistant.local";
const int   HA_PORT  = 8123;
// Long-lived access token: in Home Assistant click your name (bottom left) >
// Security > Long-lived access tokens > Create token. Paste it here.
const char* HA_TOKEN = "";
