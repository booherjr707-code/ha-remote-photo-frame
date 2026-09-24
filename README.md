# Home Assistant Touch Remote + Picture Frame

A 4" touchscreen remote for Home Assistant on the Raspberry Pi. It shows the
Sainlogic SA68 weather station, the driveway alarm and the lightning detector, and
has buttons for lights and switches. After 5 minutes without a touch it turns into a
picture frame, rotating through your photos from the SD card. A tap brings the
remote back, and the driveway alarm wakes it up on the Driveway page.

## Parts

- ESP32-WROOM-32 dev board (the same kind as the weather display)
- Hosyond 4.0" 480x320 SPI touch display, ST7796S + XPT2046 touch, with the SD slot built into the back (its 4 SD pins are a separate little row on the same board)
  ([Amazon B0CKRJ81B5](https://www.amazon.com/dp/B0CKRJ81B5))
- microSD card, 32 GB or smaller (FAT32, the way they come from the store)

## Wiring

Open `WIRING.html` for the diagram and a checklist. The display, touch chip and SD
slot share three lines (SCK, MOSI, MISO), so those ESP32 pins get several wires each.

| Display pin | ESP32 pin |
|---|---|
| VCC | VIN (5V) |
| GND | GND |
| CS | D5 |
| RESET | D17 |
| DC/RS | D16 |
| SDI (MOSI) | D23 |
| SCK | D18 |
| LED | D32 |
| SDO (MISO) | not connected |
| T_CLK | D18 |
| T_CS | D21 |
| T_DIN | D23 |
| T_DO | D19 |
| T_IRQ | not connected |
| SD_CS | D22 |
| SD_MOSI | D23 |
| SD_MISO | D19 |
| SD_SCK | D18 |

## Setup

1. Install [PlatformIO](https://platformio.org/).
2. Copy `src/secrets.example.h` to `src/secrets.h` and fill in your Wi-Fi, and a
   Home Assistant long-lived access token (in Home Assistant: click your name at the
   bottom left > Security > Long-lived access tokens > Create token).
3. In `src/config.h`, replace the entity names marked `CHANGE ME` with yours
   (Settings > Devices & services > Entities in Home Assistant). The lightning
   detector's names are already right.
4. Plug in the ESP32 and run `pio run -t upload -t monitor`.
5. The first time, it asks you to tap four arrows in the corners to calibrate
   touch. To do it again later, hold a finger on the screen while it starts up.

## Photos for the picture frame

1. In the **Photos** app, select the pictures you want, then File > Export > Export
   Photos, and save them to the **Photo Frame** folder in your Pictures folder.
2. Put the SD card in your Mac.
3. Double-click **Prepare Photos.command** in this folder. It shrinks every photo to
   the screen size, turns sideways iPhone photos upright, converts HEIC to JPEG and
   copies them to a `photos` folder on the card. The first run takes a minute to set
   itself up. Run it again after adding more; photos already on the card are skipped.
4. Eject the card and put it in the slot on the back of the screen.

Photos show for 15 seconds each in a random order, with the time and outside
temperature in the corner. Change the timing in `src/config.h`.

## Rain today

The weather station reports a running rain total. To get "rain today", add a
Utility Meter helper in Home Assistant (Settings > Devices & services > Helpers >
Create helper > Utility Meter), pick the station's rain sensor and a daily cycle,
and name it `rain today`.

## Credits

Written by [Claude Code](https://claude.com/claude-code), Anthropic's AI coding
assistant, for James Booher's ESP32 weather projects.
