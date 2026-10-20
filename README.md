# Rubicon

A pocket Spotify player built on an ESP32. It streams music over Wi-Fi without
a phone and plays it through Bluetooth earbuds.

> **Status: early.** The wiring and interface run in a browser simulator with
> sample data. Spotify streaming and Bluetooth audio come next, on real hardware.

## How it works

```mermaid
flowchart LR
    spotify[(Spotify)] -- audio stream --> cspot
    api[Spotify Web API] -- playlists, likes, search --> ui
    subgraph ESP32
        ui[Screen, knob, buttons] -- play / skip / volume --> cspot[cspot<br/>Spotify Connect]
        cspot -- PCM audio --> a2dp[A2DP source]
    end
    a2dp -- Bluetooth --> buds((Earbuds))
```

Two pieces do the work:

- **[cspot](https://github.com/feelfreelinux/cspot)** turns the ESP32 into a
  Spotify Connect device. It shows up in Spotify's device list, like a smart
  speaker, and pulls audio straight from Spotify's servers.
- **The Spotify Web API** handles browsing: your playlists, Liked Songs, search,
  the queue, liking the current song. It can also start playback *on the ESP32
  itself*, so after a one-time login the phone isn't needed.

The decoded audio goes out over Bluetooth Classic (A2DP) to the earbuds.

## What it can do

| Works | Doesn't |
|---|---|
| Play, pause, skip, seek, volume, shuffle, repeat | Jam |
| Your playlists: play, create, edit | AI DJ, Smart Shuffle, Blend, daylist |
| Liked Songs: play, like, unlike | Lyrics |
| Queue, search, recently played | Offline downloads |
| Album art on the screen | Listing Spotify-made playlists (Discover Weekly, Daily Mix) |

Spotify only offers the right-hand column inside its own apps.

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | Must be an original ESP32. The S3, C3 and C6 have no Bluetooth Classic, so they can't send audio to earbuds. A **WROVER** module (with PSRAM) is needed for Spotify streaming. |
| ILI9341 TFT, 2.4" or 2.8" | SPI, 240 × 320 |
| KY-040 rotary encoder | Scrolling and volume |
| 4 tactile buttons | Previous, play/pause, next, like |
| Bluetooth earbuds | Any A2DP earbuds. Developed against Realme Buds. |
| Spotify Premium | Required for Spotify Connect playback |

<details>
<summary>Does my board have PSRAM?</summary>

Look at the metal shield on the module: **ESP32-WROVER** has PSRAM, while
**ESP32-WROOM-32** almost never does. Or flash this and open the Serial Monitor
at 115200 baud:

```cpp
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.printf("Chip: %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("PSRAM: %u bytes\n", ESP.getPsramSize());
}
void loop() {}
```

`PSRAM: 0 bytes` means there isn't any.
</details>

## Wiring

| Part | Pin | ESP32 GPIO |
|---|---|---|
| TFT | VCC, LED | 3V3 |
| | GND | GND |
| | CS | 5 |
| | RST | 33 |
| | D/C | 27 |
| | MOSI | 23 |
| | SCK | 18 |
| | MISO | 19 |
| Encoder | CLK | 32 |
| | DT | 25 |
| | SW | 26 |
| | + / GND | 3V3 / GND |
| Buttons | PREV | 4 |
| | PLAY | 13 |
| | NEXT | 14 |
| | LIKE | 22 |

Each button's other leg goes to GND. The firmware uses the internal pull-ups.

The pins avoid GPIO 6–11 (flash), 16/17 (PSRAM on WROVER), 12 (boot strapping)
and 34–39 (input-only, no pull-ups), so the same wiring works on WROOM and WROVER.

## Try it in the simulator

No hardware needed. [`sim/sketch/`](sim/sketch) is a [Wokwi](https://wokwi.com)
project with everything above wired up. Wokwi can't simulate Bluetooth audio or
Spotify, so the songs are sample data and playback is a timer.

1. Open <https://wokwi.com/projects/new/esp32>.
2. Replace `sketch.ino` and `diagram.json` with the files from `sim/sketch/`.
3. Add a `libraries.txt` file with the same contents as the one here.
4. Press play.

| Input | Lists | Now Playing |
|---|---|---|
| Turn knob | Scroll | Volume |
| Press knob | Open, play, or go back | Back to library |
| PLAY | Play / pause | Play / pause |
| PREV, NEXT | Previous / next track | Previous / next track |
| LIKE | Like the current track | Like the current track |

## Roadmap

- [x] Wiring and interface in the simulator
- [ ] Confirm the board has PSRAM
- [ ] Bluetooth audio to the earbuds: a test tone first, then MP3s from an SD card
- [ ] Spotify Connect playback with cspot
- [ ] Web API: playlists, Liked Songs, the like button
- [ ] Album art on the screen
- [ ] Battery and enclosure

## Caveats

- cspot is an unofficial, reverse-engineered client. A change on Spotify's side
  can break it until the project catches up. This is a personal project, not a
  product.
- Wi-Fi and Bluetooth share the ESP32's single 2.4 GHz radio. Streaming in and
  sending to the earbuds at the same time takes careful buffering to avoid
  dropouts.
