// Rubicon: wiring + UI simulation for Wokwi.
// Playback is faked with sample data. Spotify (cspot + Web API) and the
// Bluetooth link to the earbuds (A2DP) come later, on real hardware.

#include <WiFi.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ---- Pins ----
// Chosen to be safe on a WROVER board: avoids GPIO 6-11 (flash),
// 16/17 (PSRAM), 12 (boot strapping) and 34-39 (no internal pull-ups).
constexpr uint8_t TFT_CS = 5, TFT_DC = 27, TFT_RST = 33;  // SCK 18, MOSI 23, MISO 19
constexpr uint8_t ENC_CLK = 32, ENC_DT = 25, ENC_SW = 26;
constexpr uint8_t BTN_PREV = 4, BTN_PLAY = 13, BTN_NEXT = 14, BTN_LIKE = 22;

// Wokwi's simulated access point; put your own network here on real hardware.
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// ---- Colours (RGB565) ----
constexpr uint16_t C_BG = 0x1082;      // #121212
constexpr uint16_t C_ROW = 0x2945;     // #282828
constexpr uint16_t C_TEXT = 0xFFFF;
constexpr uint16_t C_DIM = 0xB596;     // #B3B3B3
constexpr uint16_t C_ACCENT = 0x1DCA;  // #1DB954
constexpr uint16_t ART_COLORS[] = {0xE8E4, 0x4A7F, 0xFD20, 0x9A7F, 0x07FF};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// ---- Sample library (stands in for the Spotify Web API) ----
struct Track { const char* title; const char* artist; uint16_t lengthSec; bool liked; };
struct Playlist { const char* name; Track* tracks; uint8_t count; };

Track likedSongs[] = {
  {"Midnight Drive", "Neon Harbor", 214, true},
  {"Paper Planes Home", "The Quiet Hours", 187, true},
  {"Static Bloom", "Lumen", 241, true},
  {"Low Tide", "Coastal Ghosts", 199, true},
};
Track chillMix[] = {
  {"Slow Morning", "Velvet Rooms", 232, false},
  {"Rain on Glass", "Hollow Pines", 176, false},
  {"Soft Focus", "Amber Lane", 205, false},
};
Track gymMix[] = {
  {"Overdrive", "Iron Pulse", 168, false},
  {"Breaking Point", "Redline", 194, false},
  {"Last Rep", "Voltage Kids", 181, false},
  {"No Brakes", "Iron Pulse", 159, false},
  {"Sprint Mode", "Hyperlane", 172, false},
};

Playlist playlists[] = {
  {"Liked Songs", likedSongs, COUNT(likedSongs)},
  {"Chill Vibes", chillMix, COUNT(chillMix)},
  {"Gym Session", gymMix, COUNT(gymMix)},
};

// ---- Input ----
struct Button { uint8_t pin; bool stable = HIGH; bool raw = HIGH; uint32_t changedAt = 0; };

Button btnPrev{BTN_PREV}, btnPlay{BTN_PLAY}, btnNext{BTN_NEXT}, btnLike{BTN_LIKE}, encBtn{ENC_SW};

// True once per press, after the contact has settled for 25 ms.
bool pressed(Button& b) {
  bool r = digitalRead(b.pin);
  uint32_t now = millis();
  if (r != b.raw) { b.raw = r; b.changedAt = now; }
  if (now - b.changedAt > 25 && r != b.stable) {
    b.stable = r;
    return r == LOW;
  }
  return false;
}

// +1 clockwise, -1 counter-clockwise, 0 if the knob hasn't moved.
int readEncoder() {
  static int lastClk = HIGH;
  int clk = digitalRead(ENC_CLK);
  if (clk == lastClk) return 0;
  lastClk = clk;
  if (clk != LOW) return 0;
  return digitalRead(ENC_DT) == HIGH ? 1 : -1;
}

// ---- State ----
enum class Screen { Library, Tracks, NowPlaying };

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
Screen screen = Screen::Library;
int cursor = 0;        // highlighted row on a list screen
int openPlaylist = 0;  // playlist shown on the Tracks screen
int playingList = -1;  // -1 = nothing playing yet
int playingTrack = 0;
bool isPlaying = false;
int volume = 60;       // 0-100; becomes the A2DP volume later
uint32_t positionMs = 0, lastTick = 0;
uint32_t drawnSec = UINT32_MAX;  // second last shown on the progress bar
bool wifiUp = false;
bool redraw = true;

Track& currentTrack() { return playlists[playingList].tracks[playingTrack]; }

// ---- Drawing (portrait, 240x320) ----
constexpr int ROW_H = 24, LIST_TOP = 48, VISIBLE_ROWS = 11;
constexpr int CONTROLS_Y = 268;

void printClipped(const char* s, int x, int y, uint8_t size, int maxChars) {
  tft.setTextSize(size);
  tft.setCursor(x, y);
  if ((int)strlen(s) <= maxChars) { tft.print(s); return; }
  for (int i = 0; i < maxChars - 2; i++) tft.print(s[i]);
  tft.print("..");
}

void fmtTime(uint32_t sec, char* buf) { snprintf(buf, 8, "%u:%02u", unsigned(sec / 60), unsigned(sec % 60)); }

void drawStatusBar() {
  tft.fillRect(0, 0, 240, 20, C_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_ACCENT);
  tft.setCursor(6, 6);
  tft.print("RUBICON");
  tft.setTextColor(wifiUp ? C_ACCENT : C_DIM);
  tft.setCursor(118, 6);
  tft.print(wifiUp ? "WiFi OK" : "WiFi...");
  tft.setTextColor(C_DIM);
  tft.setCursor(178, 6);
  tft.print("Buds:sim");
  tft.drawFastHLine(0, 20, 240, C_ROW);
}

// Library row 0 jumps back to Now Playing; Tracks row 0 goes back.
int listCount() {
  return screen == Screen::Library ? COUNT(playlists) + 1 : playlists[openPlaylist].count + 1;
}

const char* listLabel(int i) {
  if (screen == Screen::Library) return i == 0 ? "> Now Playing" : playlists[i - 1].name;
  return i == 0 ? "< Back" : playlists[openPlaylist].tracks[i - 1].title;
}

void drawList() {
  tft.fillRect(0, 21, 240, 299, C_BG);
  tft.setTextColor(C_TEXT);
  printClipped(screen == Screen::Library ? "Your Library" : playlists[openPlaylist].name, 8, 26, 2, 19);
  int first = max(0, cursor - VISIBLE_ROWS + 1);
  for (int row = 0; row < VISIBLE_ROWS && first + row < listCount(); row++) {
    int i = first + row;
    int y = LIST_TOP + row * ROW_H;
    if (i == cursor) tft.fillRect(0, y, 240, ROW_H, C_ROW);
    tft.setTextColor(i == cursor ? C_ACCENT : C_TEXT);
    printClipped(listLabel(i), 12, y + 4, 2, 18);
  }
}

void drawHeart(int x, int y, bool liked) {
  tft.fillRect(x - 10, y - 6, 20, 18, C_BG);
  uint16_t c = liked ? C_ACCENT : C_DIM;
  tft.fillCircle(x - 4, y, 4, c);
  tft.fillCircle(x + 4, y, 4, c);
  tft.fillTriangle(x - 8, y + 2, x + 8, y + 2, x, y + 10, c);
}

void drawControls() {
  int cy = CONTROLS_Y;
  tft.fillRect(70, cy - 16, 100, 33, C_BG);
  tft.fillRect(76, cy - 8, 3, 17, C_TEXT);  // previous
  tft.fillTriangle(80, cy, 90, cy - 8, 90, cy + 8, C_TEXT);
  tft.fillTriangle(160, cy, 150, cy - 8, 150, cy + 8, C_TEXT);  // next
  tft.fillRect(161, cy - 8, 3, 17, C_TEXT);
  tft.fillCircle(120, cy, 15, C_TEXT);
  if (isPlaying) {
    tft.fillRect(114, cy - 7, 4, 15, C_BG);
    tft.fillRect(122, cy - 7, 4, 15, C_BG);
  } else {
    tft.fillTriangle(115, cy - 8, 115, cy + 8, 128, cy, C_BG);
  }
}

void drawVolume() {
  constexpr int x = 44, y = 300, w = 180;
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(16, y - 2);
  tft.print("VOL");
  int filled = volume * w / 100;
  tft.fillRect(x, y, filled, 4, C_TEXT);
  tft.fillRect(x + filled, y, w - filled, 4, C_ROW);
}

void drawProgress() {
  constexpr int x = 16, y = 220, w = 208;
  Track& t = currentTrack();
  int filled = (uint64_t)positionMs * w / (t.lengthSec * 1000UL);
  tft.fillRect(x, y, filled, 4, C_ACCENT);
  tft.fillRect(x + filled, y, w - filled, 4, C_ROW);
  char buf[8];
  tft.setTextSize(1);
  tft.setTextColor(C_DIM, C_BG);
  fmtTime(positionMs / 1000, buf);
  tft.setCursor(x, y + 10);
  tft.print(buf);
  fmtTime(t.lengthSec, buf);
  tft.setCursor(x + w - 6 * strlen(buf), y + 10);
  tft.print(buf);
}

void drawNowPlaying() {
  tft.fillRect(0, 21, 240, 299, C_BG);
  drawnSec = UINT32_MAX;
  if (playingList < 0) {
    tft.setTextColor(C_DIM);
    printClipped("Nothing playing", 30, 140, 2, 18);
    printClipped("Press knob for library", 54, 166, 1, 30);
    return;
  }
  Track& t = currentTrack();
  // Album art placeholder; real art comes from the track's image URL later.
  tft.fillRoundRect(60, 32, 120, 120, 8, ART_COLORS[t.title[0] % COUNT(ART_COLORS)]);
  tft.setTextColor(C_BG);
  tft.setTextSize(6);
  tft.setCursor(102, 68);
  tft.print(t.title[0]);

  tft.setTextColor(C_TEXT);
  printClipped(t.title, 16, 164, 2, 15);
  tft.setTextColor(C_DIM);
  printClipped(t.artist, 16, 186, 1, 30);
  printClipped(playlists[playingList].name, 16, 198, 1, 30);
  drawHeart(214, 170, t.liked);
  drawControls();
  drawVolume();
}

// ---- Fake playback (stands in for cspot) ----
void startTrack(int list, int index) {
  playingList = list;
  playingTrack = index;
  positionMs = 0;
  isPlaying = true;
  Serial.printf("Play: %s - %s\n", currentTrack().title, currentTrack().artist);
  if (screen == Screen::NowPlaying) redraw = true;
}

void skip(int dir) {
  if (playingList < 0) return;
  // Like Spotify, "previous" restarts the song if it's more than 3 s in.
  if (dir < 0 && positionMs > 3000) { positionMs = 0; return; }
  int n = playlists[playingList].count;
  startTrack(playingList, (playingTrack + dir + n) % n);
}

void onSelect() {
  switch (screen) {
    case Screen::Library:
      if (cursor == 0) {
        screen = Screen::NowPlaying;
      } else {
        openPlaylist = cursor - 1;
        screen = Screen::Tracks;
        cursor = 1;
      }
      break;
    case Screen::Tracks:
      if (cursor == 0) {
        screen = Screen::Library;
        cursor = openPlaylist + 1;
      } else {
        screen = Screen::NowPlaying;
        startTrack(openPlaylist, cursor - 1);
      }
      break;
    case Screen::NowPlaying:
      screen = Screen::Library;
      cursor = 0;
      break;
  }
  redraw = true;
}

void setup() {
  Serial.begin(115200);
  for (uint8_t p : {ENC_CLK, ENC_DT, ENC_SW, BTN_PREV, BTN_PLAY, BTN_NEXT, BTN_LIKE}) {
    pinMode(p, INPUT_PULLUP);
  }
  tft.begin();
  tft.fillScreen(C_BG);
  drawStatusBar();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("Rubicon sim started");
  lastTick = millis();
}

void loop() {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up != wifiUp) {
    wifiUp = up;
    drawStatusBar();
    if (up) Serial.printf("Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
  }

  // Knob: scrolls lists, or sets volume on the Now Playing screen.
  int step = readEncoder();
  if (step && screen == Screen::NowPlaying) {
    volume = constrain(volume + step * 5, 0, 100);
    if (playingList >= 0) drawVolume();
  } else if (step) {
    cursor = constrain(cursor + step, 0, listCount() - 1);
    redraw = true;
  }
  if (pressed(encBtn)) onSelect();

  if (pressed(btnPlay) && playingList >= 0) {
    isPlaying = !isPlaying;
    Serial.println(isPlaying ? "Resume" : "Pause");
    if (screen == Screen::NowPlaying) drawControls();
  }
  if (pressed(btnNext)) skip(1);
  if (pressed(btnPrev)) skip(-1);
  if (pressed(btnLike) && playingList >= 0) {
    Track& t = currentTrack();
    t.liked = !t.liked;
    Serial.printf("%s: %s\n", t.liked ? "Liked" : "Unliked", t.title);
    if (screen == Screen::NowPlaying) drawHeart(214, 170, t.liked);
  }

  uint32_t now = millis();
  if (isPlaying) {
    positionMs += now - lastTick;
    if (positionMs >= currentTrack().lengthSec * 1000UL) skip(1);
  }
  lastTick = now;

  if (redraw) {
    redraw = false;
    if (screen == Screen::NowPlaying) drawNowPlaying();
    else drawList();
  }
  if (screen == Screen::NowPlaying && playingList >= 0 && positionMs / 1000 != drawnSec) {
    drawnSec = positionMs / 1000;
    drawProgress();
  }
}
