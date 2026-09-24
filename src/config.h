// Which Home Assistant entities the remote shows and controls.
//
// Names marked CHANGE ME depend on your sensors. Once the 433 MHz receiver and
// the weather station are in Home Assistant, find the real names under
// Settings > Devices & services > Entities (or send them to Claude).
// An entity that doesn't exist just shows "--".
#pragma once

// ---------- Weather page (Sainlogic SA68 via the 433 MHz receiver) ----------
const char* E_TEMPERATURE    = "sensor.weather_station_temperature";      // CHANGE ME
const char* E_HUMIDITY       = "sensor.weather_station_humidity";         // CHANGE ME
const char* E_WIND           = "sensor.weather_station_wind_speed";       // CHANGE ME
const char* E_GUST           = "sensor.weather_station_wind_gust";        // CHANGE ME
const char* E_WIND_DIR       = "sensor.weather_station_wind_direction";   // CHANGE ME
const char* E_RAIN_TODAY     = "sensor.rain_today";                       // CHANGE ME (see README)
const char* E_WEATHER        = "weather.kjbr";                            // CHANGE ME (NWS integration)

// ---------- Driveway page ----------
const char* E_DRIVEWAY       = "binary_sensor.driveway_alarm_alert";      // CHANGE ME
const char* E_LIGHTNING_NEAR = "binary_sensor.lightning_detector_lightning_nearby";
const char* E_LIGHTNING_MI   = "sensor.lightning_detector_lightning_distance";
const char* E_STRIKES_HOUR   = "sensor.lightning_detector_strikes_last_hour";

// ---------- Controls page: six buttons ----------
// Anything that can be switched on and off: light., switch., fan., input_boolean...
struct ControlButton {
  const char* label;
  const char* entity;
};
const ControlButton CONTROLS[6] = {
  {"Porch",       "light.porch"},          // CHANGE ME
  {"Garage",      "light.garage"},         // CHANGE ME
  {"Living Room", "light.living_room"},    // CHANGE ME
  {"Kitchen",     "light.kitchen"},        // CHANGE ME
  {"Fan",         "fan.bedroom"},          // CHANGE ME
  {"Outlet",      "switch.outlet"},        // CHANGE ME
};

// ---------- Picture frame ----------
const uint32_t SCREENSAVER_AFTER_S = 5 * 60;  // start the slideshow after 5 min without a touch
const uint32_t PHOTO_SECONDS       = 15;      // how long each picture stays up
const char*    PHOTO_FOLDER        = "/photos";

// ---------- Screen ----------
const uint8_t BRIGHTNESS = 255;   // backlight, 0-255
const char* TIMEZONE = "CST6CDT,M3.2.0,M11.1.0";  // US Central
