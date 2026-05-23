/*
  ============================================================
  WIRELESS CAR REMOTE  —  v2  (WiFi stable + dual touch)
  ============================================================
  Board   : ESP32-S3 (2.8" display module)
  Display : ILI9341V via Adafruit_ILI9341
  Touch   : FT6336G I2C  (2-finger multi-touch)
  Role    : WiFi UDP Client + Joystick Touch UI
  ============================================================
  Libraries (Arduino Library Manager):
    - Adafruit ILI9341
    - Adafruit GFX Library
  ============================================================
  PINS:
    TFT CS   IO10    TFT DC   IO46    TFT SCK  IO12
    TFT MOSI IO11    TFT MISO IO13    TFT BL   IO45
    TFT RST  -1 (shared with board RST)
    Touch SDA IO16   Touch SCL IO15   Touch RST IO18
  ============================================================
  WiFi fix: WiFi.setSleep(false) keeps radio always active.
  Auto-reconnect handles drops silently in background.
  UI never redraws on a blip — only status bar dot changes.
  ============================================================
*/

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ── Pins ─────────────────────────────────────────────────────
#define TFT_CS   10
#define TFT_DC   46
#define TFT_RST  -1
#define TFT_SCK  12
#define TFT_MOSI 11
#define TFT_MISO 13
#define TFT_BL   45

#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18

#define SCREEN_W  240
#define SCREEN_H  320
#define FT_ADDR   0x38

// ── WiFi / UDP ───────────────────────────────────────────────
const char* WIFI_SSID = "RCCar_Control";
const char* WIFI_PASS = "rccar1234";
const char* CAR_IP    = "192.168.4.1";
const int   UDP_PORT  = 4210;

// ── Colors ───────────────────────────────────────────────────
#define C_BG      0x0000
#define C_GREEN   0x07E0
#define C_DGREEN  0x03E0
#define C_DDGREEN 0x0320
#define C_DARK    0x0180
#define C_WHITE   0xFFFF
#define C_RED     0xF800
#define C_YELLOW  0xFFE0
#define C_CYAN    0x07FF
#define C_GRAY    0x4208

// ── Structs  (typedef before everything — Arduino preprocessor safe) ──
typedef struct { int x, y; bool pressed; } TouchPoint;
typedef struct {
  TouchPoint p[2];  // p[0] = first finger, p[1] = second finger
  int count;        // 0, 1 or 2
} TouchFrame;
typedef struct { int cx, cy, r, kx, ky; bool active; } Joystick;
typedef struct { int x, y, w, h; const char* lbl; uint16_t col; bool pressed; } Btn;

// ── Objects ──────────────────────────────────────────────────
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
WiFiUDP udp;

// ── Joysticks ────────────────────────────────────────────────
Joystick jsThrottle = { 60,  175, 50, 60,  175, false };  // left  = throttle (vertical)
Joystick jsSteering = { 180, 175, 50, 180, 175, false };  // right = steering (horizontal)

// ── Buttons ──────────────────────────────────────────────────
#define NUM_BTNS 3
Btn btns[NUM_BTNS] = {
  {  4, 288, 72, 28, "HORN",   C_YELLOW, false },
  { 84, 288, 72, 28, "LIGHTS", C_CYAN,   false },
  {164, 288, 72, 28, "STOP",   C_RED,    false },
};

// ── State ────────────────────────────────────────────────────
int  driveSpeed = 200;
bool lightsOn   = false;
bool wifiOk     = false;
char lastDrive  = 0;   // 'F' fwd  'B' back  0 stop
char lastSteer  = 0;   // 'L' left 'R' right 0 center
unsigned long lastSpeedSend = 0;
unsigned long lastStatusBar = 0;

// ════════════════════════════════════════════════════════════
//  TOUCH — FT6336G dual-finger I2C read
// ════════════════════════════════════════════════════════════
TouchFrame getTouchFrame() {
  TouchFrame tf;
  tf.count  = 0;
  tf.p[0]   = {0, 0, false};
  tf.p[1]   = {0, 0, false};

  Wire.beginTransmission(FT_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return tf;

  // 13 bytes: 1 touch-count byte + 6 bytes per point × 2 points
  Wire.requestFrom(FT_ADDR, 13);
  if (Wire.available() < 13) return tf;

  uint8_t d[13];
  for (int i = 0; i < 13; i++) d[i] = Wire.read();

  tf.count = constrain((int)(d[0] & 0x0F), 0, 2);

  int offsets[2] = {1, 7};   // byte offset for point 1 and point 2
  for (int f = 0; f < tf.count; f++) {
    int o   = offsets[f];
    int rx  = ((d[o]   & 0x0F) << 8) | d[o + 1];
    int ry  = ((d[o+2] & 0x0F) << 8) | d[o + 3];
    // setRotation(2) = 180°, so invert both axes
    tf.p[f].x       = constrain(239 - rx, 0, 239);
    tf.p[f].y       = constrain(319 - ry, 0, 319);
    tf.p[f].pressed = true;
  }
  return tf;
}

// Returns whichever finger falls inside a screen zone, or unpressed if none
TouchPoint zoneTouch(TouchFrame& tf, int x0, int x1, int y0, int y1) {
  for (int f = 0; f < tf.count; f++) {
    if (tf.p[f].x >= x0 && tf.p[f].x <= x1 &&
        tf.p[f].y >= y0 && tf.p[f].y <= y1) {
      return tf.p[f];
    }
  }
  return {0, 0, false};
}

// ════════════════════════════════════════════════════════════
//  UDP SEND
// ════════════════════════════════════════════════════════════
void sendCmd(const char* cmd) {
  if (!wifiOk) return;
  udp.beginPacket(CAR_IP, UDP_PORT);
  udp.print(cmd);
  udp.endPacket();
}

// ════════════════════════════════════════════════════════════
//  DRAW HELPERS
// ════════════════════════════════════════════════════════════
void drawCorners(int x, int y, int w, int h, uint16_t c, int l = 10) {
  tft.drawFastHLine(x,     y,     l, c); tft.drawFastVLine(x,     y,     l, c);
  tft.drawFastHLine(x+w-l, y,     l, c); tft.drawFastVLine(x+w-1, y,     l, c);
  tft.drawFastHLine(x,     y+h-1, l, c); tft.drawFastVLine(x,     y+h-l, l, c);
  tft.drawFastHLine(x+w-l, y+h-1, l, c); tft.drawFastVLine(x+w-1, y+h-l, l, c);
}

void drawStatusBar() {
  tft.fillRect(0, 0, SCREEN_W, 22, C_DARK);
  tft.drawFastHLine(0, 22, SCREEN_W, C_GREEN);
  tft.setTextSize(1);

  // WiFi dot
  tft.setTextColor(wifiOk ? C_GREEN : C_RED);
  tft.setCursor(4, 7);
  tft.print(wifiOk ? "WiFi OK" : "No WiFi");

  // Title
  tft.setTextColor(C_DGREEN);
  tft.setCursor(58, 7);
  tft.print("WIRELESS CAR REMOTE");

  // Lights indicator
  tft.setTextColor(lightsOn ? C_YELLOW : C_GRAY);
  tft.setCursor(200, 7);
  tft.print("LIT");
}

void drawJoystick(Joystick& js, bool full) {
  if (full) {
    tft.fillCircle(js.cx, js.cy, js.r + 4, C_DARK);
    tft.drawCircle(js.cx, js.cy, js.r, C_GRAY);
    tft.drawFastVLine(js.cx, js.cy - js.r, js.r * 2, C_DDGREEN);
    tft.drawFastHLine(js.cx - js.r, js.cy, js.r * 2, C_DDGREEN);
  }
  uint16_t kc = js.active ? C_CYAN : C_DGREEN;
  tft.fillCircle(js.kx, js.ky, 14, kc);
  tft.drawCircle(js.kx, js.ky, 14, C_WHITE);
}

void clearKnob(Joystick& js) {
  tft.fillCircle(js.kx, js.ky, 15, C_DARK);
  tft.drawCircle(js.cx, js.cy, js.r, C_GRAY);
  tft.drawFastVLine(js.cx, js.cy - js.r, js.r * 2, C_DDGREEN);
  tft.drawFastHLine(js.cx - js.r, js.cy, js.r * 2, C_DDGREEN);
}

void drawBtn(int i, bool pressed) {
  uint16_t bg = pressed ? btns[i].col : C_DARK;
  uint16_t fg = pressed ? C_BG        : btns[i].col;
  tft.fillRoundRect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, 5, bg);
  tft.drawRoundRect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, 5, btns[i].col);
  tft.setTextSize(1);
  tft.setTextColor(fg);
  int tx = btns[i].x + (btns[i].w / 2) - (strlen(btns[i].lbl) * 3);
  int ty = btns[i].y + (btns[i].h / 2) - 4;
  tft.setCursor(tx, ty);
  tft.print(btns[i].lbl);
}

void drawSpeedBar() {
  int bx = 8, by = 260, bw = 220, bh = 14;
  int pct  = map(driveSpeed, 0, 255, 0, 100);
  int fill = (bw * pct) / 100;
  tft.drawRect(bx, by, bw, bh, C_GRAY);
  if (fill > 2)      tft.fillRect(bx + 1,        by + 1, fill - 2,       bh - 2, C_GREEN);
  if (fill < bw - 2) tft.fillRect(bx + 1 + fill, by + 1, bw - fill - 2,  bh - 2, C_DARK);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE);
  tft.setCursor(bx + 2, by + 3);
  tft.print("SPD: "); tft.print(pct); tft.print("%  ");
}

void drawCmdIndicator() {
  tft.fillRect(0, 238, SCREEN_W, 20, C_BG);
  tft.setTextSize(1);

  tft.setTextColor(C_DGREEN); tft.setCursor(8,   243); tft.print("DRIVE:");
  tft.setTextColor(C_GREEN);  tft.setCursor(52,  243);
  if      (lastDrive == 'F') tft.print("FWD  ");
  else if (lastDrive == 'B') tft.print("REV  ");
  else                       tft.print("STOP ");

  tft.setTextColor(C_DGREEN); tft.setCursor(120, 243); tft.print("STEER:");
  tft.setTextColor(C_GREEN);  tft.setCursor(168, 243);
  if      (lastSteer == 'L') tft.print("LEFT ");
  else if (lastSteer == 'R') tft.print("RIGHT");
  else                       tft.print("CTR  ");
}

void drawFullUI() {
  tft.fillScreen(C_BG);
  drawCorners(0, 0, SCREEN_W, SCREEN_H, C_DGREEN, 8);
  drawStatusBar();

  // Zone labels
  tft.setTextSize(1); tft.setTextColor(C_GRAY);
  tft.setCursor(22, 26);  tft.print("THROTTLE");
  tft.setCursor(148, 26); tft.print("STEERING");
  tft.drawFastVLine(120, 24, 234, C_DDGREEN);

  drawJoystick(jsThrottle, true);
  drawJoystick(jsSteering, true);
  drawSpeedBar();
  drawCmdIndicator();
  for (int i = 0; i < NUM_BTNS; i++) drawBtn(i, false);
}

// ════════════════════════════════════════════════════════════
//  WIFI CONNECT SCREEN  (called once at boot only)
// ════════════════════════════════════════════════════════════
void connectWiFi() {
  tft.fillScreen(C_BG);
  drawCorners(2, 2, SCREEN_W - 4, SCREEN_H - 4, C_GREEN, 12);

  tft.setTextSize(2); tft.setTextColor(C_GREEN);
  tft.setCursor(6, 40);  tft.print("WIRELESS CAR");
  tft.setCursor(6, 64);  tft.print("REMOTE");

  tft.setTextSize(1); tft.setTextColor(C_DGREEN);
  tft.setCursor(10, 100); tft.print("Connecting to:");
  tft.setTextColor(C_CYAN);
  tft.setCursor(10, 116); tft.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);          // ← disable radio power-save, prevents drops
  WiFi.setAutoReconnect(true);   // reconnect silently in background if link blips
  WiFi.persistent(true);         // store credentials so reconnect needs no begin()
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int dots = 0, tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 50) {
    delay(300); tries++;
    tft.setTextColor(C_DGREEN);
    tft.setCursor(10 + dots * 8, 140); tft.print(".");
    dots++;
    if (dots > 26) { dots = 0; tft.fillRect(10, 140, 220, 12, C_BG); }
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiOk = true;
    tft.setTextColor(C_GREEN);
    tft.setCursor(10, 166); tft.print("CONNECTED!");
    tft.setTextColor(C_DGREEN);
    tft.setCursor(10, 182); tft.print(WiFi.localIP().toString());
    delay(1000);
  } else {
    // Not fatal — autoReconnect will keep trying in background
    tft.setTextColor(C_RED);
    tft.setCursor(10, 166); tft.print("Car not found yet.");
    tft.setTextColor(C_DGREEN);
    tft.setCursor(10, 182); tft.print("Will retry automatically.");
    delay(1500);
  }
}

// ════════════════════════════════════════════════════════════
//  JOYSTICK PROCESSING
// ════════════════════════════════════════════════════════════
void processThrottle(TouchPoint tp) {
  bool hit = tp.pressed;

  if (hit) {
    int ny = constrain(tp.y, jsThrottle.cy - jsThrottle.r, jsThrottle.cy + jsThrottle.r);
    clearKnob(jsThrottle);
    jsThrottle.kx     = jsThrottle.cx;
    jsThrottle.ky     = ny;
    jsThrottle.active = true;
    drawJoystick(jsThrottle, false);

    int off = jsThrottle.cy - ny;  // positive = up = forward
    if (off > 8) {
      driveSpeed = constrain(map(abs(off), 8, jsThrottle.r, 80, 255), 80, 255);
      if (lastDrive != 'F') { sendCmd("forward");   lastDrive = 'F'; drawCmdIndicator(); }
    } else if (off < -8) {
      driveSpeed = constrain(map(abs(off), 8, jsThrottle.r, 80, 255), 80, 255);
      if (lastDrive != 'B') { sendCmd("backward");  lastDrive = 'B'; drawCmdIndicator(); }
    } else {
      if (lastDrive != 0)   { sendCmd("stop_drive"); lastDrive = 0;  drawCmdIndicator(); }
    }
  } else if (jsThrottle.active) {
    clearKnob(jsThrottle);
    jsThrottle.kx = jsThrottle.cx; jsThrottle.ky = jsThrottle.cy;
    jsThrottle.active = false;
    drawJoystick(jsThrottle, false);
    if (lastDrive != 0) { sendCmd("stop_drive"); lastDrive = 0; drawCmdIndicator(); }
  }
}

void processSteering(TouchPoint tp) {
  bool hit = tp.pressed;

  if (hit) {
    int nx = constrain(tp.x, jsSteering.cx - jsSteering.r, jsSteering.cx + jsSteering.r);
    clearKnob(jsSteering);
    jsSteering.kx     = nx;
    jsSteering.ky     = jsSteering.cy;
    jsSteering.active = true;
    drawJoystick(jsSteering, false);

    int off = nx - jsSteering.cx;  // positive = right
    if (off > 8) {
      if (lastSteer != 'R') { sendCmd("right"); lastSteer = 'R'; drawCmdIndicator(); }
    } else if (off < -8) {
      if (lastSteer != 'L') { sendCmd("left");  lastSteer = 'L'; drawCmdIndicator(); }
    } else {
      if (lastSteer != 0)   { sendCmd("stop_steer"); lastSteer = 0; drawCmdIndicator(); }
    }
  } else if (jsSteering.active) {
    clearKnob(jsSteering);
    jsSteering.kx = jsSteering.cx; jsSteering.ky = jsSteering.cy;
    jsSteering.active = false;
    drawJoystick(jsSteering, false);
    if (lastSteer != 0) { sendCmd("stop_steer"); lastSteer = 0; drawCmdIndicator(); }
  }
}

// ════════════════════════════════════════════════════════════
//  BUTTON PROCESSING
// ════════════════════════════════════════════════════════════
void processButtons(TouchPoint tp) {
  for (int i = 0; i < NUM_BTNS; i++) {
    bool hit = tp.pressed &&
               tp.x >= btns[i].x && tp.x <= btns[i].x + btns[i].w &&
               tp.y >= btns[i].y && tp.y <= btns[i].y + btns[i].h;

    if (hit && !btns[i].pressed) {
      btns[i].pressed = true;
      drawBtn(i, true);
      if (i == 0) sendCmd("horn_on");
      if (i == 1) {
        lightsOn = !lightsOn;
        sendCmd(lightsOn ? "lights_on" : "lights_off");
        drawStatusBar();
      }
      if (i == 2) {
        sendCmd("stop_all");
        lastDrive = 0; lastSteer = 0;
        clearKnob(jsThrottle);
        jsThrottle.kx = jsThrottle.cx; jsThrottle.ky = jsThrottle.cy;
        jsThrottle.active = false; drawJoystick(jsThrottle, false);
        clearKnob(jsSteering);
        jsSteering.kx = jsSteering.cx; jsSteering.ky = jsSteering.cy;
        jsSteering.active = false; drawJoystick(jsSteering, false);
        drawCmdIndicator();
      }
    }
    if (!hit && btns[i].pressed) {
      btns[i].pressed = false;
      drawBtn(i, false);
      if (i == 0) sendCmd("horn_off");
    }
  }
}

// ════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(300);

  // Backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Touch I2C
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);  // 400kHz fast mode for snappier touch reads
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);  delay(10);
  digitalWrite(TOUCH_RST, HIGH); delay(100);

  // Display — same init pattern as JARVIS
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI);
  tft.begin(40000000);
  tft.invertDisplay(true);
  tft.setRotation(2);
  tft.fillScreen(C_BG);

  // Connect WiFi — only called once at boot
  connectWiFi();

  // UDP socket
  udp.begin(UDP_PORT);

  // Draw main UI
  drawFullUI();
}

// ════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════
void loop() {
  // ── WiFi status — autoReconnect handles the actual reconnect.
  //    We only flip the status bar colour. Never wipe screen or call begin() again.
  bool nowOk = (WiFi.status() == WL_CONNECTED);
  if (nowOk != wifiOk) {
    wifiOk = nowOk;
    drawStatusBar();
  }

  // ── Read both touch fingers and route each to its screen zone ──
  TouchFrame tf = getTouchFrame();

  processThrottle(zoneTouch(tf,   0, 117,  24, 239));   // left half  = throttle
  processSteering(zoneTouch(tf, 122, 239,  24, 239));   // right half = steering
  processButtons( zoneTouch(tf,   0, 239, 280, 319));   // bottom row = buttons

  // ── Speed packet — sent every 100ms while driving ──
  if (lastDrive != 0 && millis() - lastSpeedSend > 100) {
    char buf[16];
    snprintf(buf, sizeof(buf), "speed=%d", driveSpeed);
    sendCmd(buf);
    drawSpeedBar();
    lastSpeedSend = millis();
  }

  // ── Status bar refresh every 1s ──
  if (millis() - lastStatusBar > 1000) {
    drawStatusBar();
    lastStatusBar = millis();
  }

  delay(20);  // ~50 Hz touch polling
}
