/*
  ============================================================
  ESP32 RC CAR BRAIN  —  v2  (WiFi stable)
  ============================================================
  Board   : ESP32 (standard)
  Role    : WiFi AP + UDP Server + Motor Controller
  AP SSID : RCCar_Control   Pass: rccar1234
  AP IP   : 192.168.4.1     UDP Port: 4210
  ============================================================
  WIRING:
    L293D IN1  → GPIO 26  (Drive Forward)
    L293D IN2  → GPIO 25  (Drive Backward)
    L293D ENB  → GPIO 5   (Drive Speed PWM)
    L293D IN3  → GPIO 27  (Steer Left)
    L293D IN4  → GPIO 14  (Steer Right)
    L293D ENA  → GPIO 18  (Steer Speed PWM)
    Buzzer +   → GPIO 23
    LED        → GPIO 22
  ============================================================
*/

#include <WiFi.h>
#include <WiFiUdp.h>

// ── WiFi ─────────────────────────────────────────────────────
const char* AP_SSID  = "RCCar_Control";
const char* AP_PASS  = "rccar1234";
const int   UDP_PORT = 4210;

// ── Motor Pins ───────────────────────────────────────────────
#define DRIVE_IN1  26
#define DRIVE_IN2  25
#define DRIVE_ENB   5

#define STEER_IN3  27
#define STEER_IN4  14
#define STEER_ENA  18

#define BUZZER_PIN 23
#define LED_PIN    22

// ── PWM (ESP32 Arduino core 3.x) ────────────────────────────
#define PWM_FREQ 1000
#define PWM_RES  8       // 0–255

// ── State ────────────────────────────────────────────────────
WiFiUDP udp;
char    pkt[64];
int     driveSpeed = 200;
bool    lightsOn   = false;

// ── Motor Helpers ────────────────────────────────────────────
void driveForward(int s)  { ledcWrite(DRIVE_ENB, s); digitalWrite(DRIVE_IN1, HIGH); digitalWrite(DRIVE_IN2, LOW);  }
void driveBackward(int s) { ledcWrite(DRIVE_ENB, s); digitalWrite(DRIVE_IN1, LOW);  digitalWrite(DRIVE_IN2, HIGH); }
void driveStop()          { ledcWrite(DRIVE_ENB, 0); digitalWrite(DRIVE_IN1, LOW);  digitalWrite(DRIVE_IN2, LOW);  }
void steerLeft(int s)     { ledcWrite(STEER_ENA, s); digitalWrite(STEER_IN3, HIGH); digitalWrite(STEER_IN4, LOW);  }
void steerRight(int s)    { ledcWrite(STEER_ENA, s); digitalWrite(STEER_IN3, LOW);  digitalWrite(STEER_IN4, HIGH); }
void steerStop()          { ledcWrite(STEER_ENA, 0); digitalWrite(STEER_IN3, LOW);  digitalWrite(STEER_IN4, LOW);  }

// ── Command Parser ───────────────────────────────────────────
void handleCmd(String cmd) {
  cmd.trim();
  Serial.println("[CMD] " + cmd);

  if      (cmd == "forward")       driveForward(driveSpeed);
  else if (cmd == "backward")      driveBackward(driveSpeed);
  else if (cmd == "stop_drive")    driveStop();
  else if (cmd == "left")          steerLeft(180);
  else if (cmd == "right")         steerRight(180);
  else if (cmd == "stop_steer")    steerStop();
  else if (cmd == "stop_all")      { driveStop(); steerStop(); }
  else if (cmd == "horn_on")       digitalWrite(BUZZER_PIN, HIGH);
  else if (cmd == "horn_off")      digitalWrite(BUZZER_PIN, LOW);
  else if (cmd == "lights_on")     { lightsOn = true;  digitalWrite(LED_PIN, HIGH); }
  else if (cmd == "lights_off")    { lightsOn = false; digitalWrite(LED_PIN, LOW);  }
  else if (cmd == "lights_toggle") { lightsOn = !lightsOn; digitalWrite(LED_PIN, lightsOn); }
  else if (cmd == "ping") {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("pong");
    udp.endPacket();
  }
  else if (cmd.startsWith("speed=")) {
    driveSpeed = constrain(cmd.substring(6).toInt(), 0, 255);
    Serial.println("Speed: " + String(driveSpeed));
  }
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 RC CAR BRAIN v2 ===");

  // Motor direction pins
  pinMode(DRIVE_IN1, OUTPUT); pinMode(DRIVE_IN2, OUTPUT);
  pinMode(STEER_IN3, OUTPUT); pinMode(STEER_IN4, OUTPUT);

  // Peripherals
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  pinMode(LED_PIN,    OUTPUT); digitalWrite(LED_PIN,    LOW);

  // PWM enable pins — core 3.x API
  ledcAttach(DRIVE_ENB, PWM_FREQ, PWM_RES);
  ledcAttach(STEER_ENA, PWM_FREQ, PWM_RES);
  driveStop();
  steerStop();

  // WiFi AP — disable sleep so radio stays fully active under motor load
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleep(false);  // ← KEY: prevents radio duty-cycling that causes drops
  delay(500);

  Serial.println("AP IP : " + WiFi.softAPIP().toString());
  Serial.println("SSID  : " + String(AP_SSID));

  udp.begin(UDP_PORT);
  Serial.println("UDP   : port " + String(UDP_PORT));

  // Ready double-beep
  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(80);
    digitalWrite(BUZZER_PIN, LOW);  delay(80);
  }
  Serial.println("Ready!");
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
  int sz = udp.parsePacket();
  if (sz > 0) {
    int len = udp.read(pkt, sizeof(pkt) - 1);
    if (len > 0) {
      pkt[len] = '\0';
      handleCmd(String(pkt));
    }
  }
  // No delay — keep UDP polling as fast as possible
}
