// ═══════════════════════════════════════════════════════════════════════
//  EchoChainSocket — LED Strip + 7 Buttons
//  Arduino Nano ESP32 + WS2812B strip + passive buzzer + 7 buttons
//
//  Required libraries:
//    • WebSockets  by Markus Sattler (Links2004)
//    • FastLED
// ═══════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <FastLED.h>

// ── CONFIGURE THESE ────────────────────────────────────────────────────
#define WIFI_SSID     "USC Guest Wireless"
#define WIFI_PASSWORD ""

#define WS_HOST       "echo-chain-relay-7a6752ad0567.herokuapp.com"
#define WS_PORT       443
#define WS_PATH       "/"

#define DEVICE_ID     "hw-B"       // hw-A on the other board
#define PAIR_ID       "pair_hw"    // match the scenario tab on the web UI

#define OPEN_NETWORK  1
// ───────────────────────────────────────────────────────────────────────

// ── PIN ASSIGNMENTS ────────────────────────────────────────────────────
#define LED_PIN       5       // 板子上的 D2
#define SPEAKER_PIN   D9

// 7 个按钮引脚（顺序对应 Do Re Mi Fa Sol La Ti）
const int BTN_PINS[] = { D11, D10, D8, D7, D6, D5, D4 };

// ── LED STRIP CONFIG ───────────────────────────────────────────────────
#define NUM_LEDS      10
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
#define BRIGHTNESS    50

#define NOTE_DUR      500   // ms
// ───────────────────────────────────────────────────────────────────────

CRGB leds[NUM_LEDS];

// ── Note table (Do → Ti) ───────────────────────────────────────────────
struct Note {
  const char* name;
  int freq;
  uint8_t r, g, b;
  int ledIndex;
};

const Note NOTES[] = {
  { "Do",  262, 255,   0,   0,  0 },  // 0 — Red
  { "Re",  294, 255, 128,   0,  1 },  // 1 — Orange
  { "Mi",  330, 255, 255,   0,  2 },  // 2 — Yellow
  { "Fa",  349,   0, 255,   0,  3 },  // 3 — Green
  { "Sol", 392,   0, 128, 255,  4 },  // 4 — Blue
  { "La",  440, 128,   0, 255,  5 },  // 5 — Purple
  { "Ti",  494, 255,   0, 255,  6 },  // 6 — Magenta
};

const int NUM_NOTES = 7;

WebSocketsClient ws;
bool partnerOnline = false;
bool wsConnected   = false;

// 按钮边沿检测（7 个按钮的上一次状态）
bool lastBtn[7] = { HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH };

// ── LED helpers ─────────────────────────────────────────────────────────
void ledOff() {
  FastLED.clear();
  FastLED.show();
}

void ledShowNote(int noteId) {
  if (noteId < 0 || noteId >= NUM_NOTES) return;
  const Note& n = NOTES[noteId];
  FastLED.clear();
  leds[n.ledIndex] = CRGB(n.r, n.g, n.b);
  FastLED.show();
}

// ── Speaker helper (software square wave) ──────────────────────────────
void buzzerTone(int freq, int duration) {
  unsigned long endTime = millis() + duration;
  int halfPeriod = 500000 / freq;
  while (millis() < endTime) {
    digitalWrite(SPEAKER_PIN, HIGH);
    delayMicroseconds(halfPeriod);
    digitalWrite(SPEAKER_PIN, LOW);
    delayMicroseconds(halfPeriod);
  }
}

// ── Play a note: LED + buzzer ──────────────────────────────────────────
void fireNote(int noteId) {
  if (noteId < 0 || noteId >= NUM_NOTES) return;
  ledShowNote(noteId);
  buzzerTone(NOTES[noteId].freq, NOTE_DUR);
  ledOff();
}

// ── WebSocket send helpers ──────────────────────────────────────────────
void sendJSON(const char* payload) {
  ws.sendTXT(payload);
  Serial.println("TX: " + String(payload));
}

void joinPair() {
  sendJSON(
    "{\"type\":\"join\","
    "\"pair_id\":\"" PAIR_ID "\","
    "\"device_id\":\"" DEVICE_ID "\","
    "\"client_type\":\"device\"}"
  );
}

void sendNote(int noteId) {
  String msg = "{\"type\":\"note\",\"note_id\":" + String(noteId) + "}";
  ws.sendTXT(msg);
  Serial.println("TX: " + msg);
}

// ── Parse note_id from incoming JSON ────────────────────────────────────
int parseNoteId(const String& msg) {
  int idx = msg.indexOf("\"note_id\":");
  if (idx == -1) return 0;
  idx += 10;
  while (idx < (int)msg.length() && msg[idx] == ' ') idx++;
  return msg.substring(idx).toInt();
}

// ── WebSocket event handler ─────────────────────────────────────────────
void onWSEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {

    case WStype_CONNECTED:
      wsConnected = true;
      Serial.println("[socket] Connected — joining pair...");
      joinPair();
      break;

    case WStype_DISCONNECTED:
      wsConnected   = false;
      partnerOnline = false;
      Serial.println("[socket] Disconnected.");
      break;

    case WStype_TEXT: {
      String msg = String((char*)payload);
      Serial.println("RX: " + msg);

      if (msg.indexOf("\"joined\"") > -1) {
        Serial.println("[socket] Joined relay as " DEVICE_ID);
      }
      else if (msg.indexOf("\"partner_joined\"") > -1) {
        partnerOnline = true;
        Serial.println("[socket] Partner is online!");
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        delay(200);
        ledOff();
      }
      else if (msg.indexOf("\"partner_left\"") > -1) {
        partnerOnline = false;
        Serial.println("[socket] Partner disconnected.");
      }
      else if (msg.indexOf("\"note\"") > -1) {
        int noteId = parseNoteId(msg);
        if (noteId >= 0 && noteId < NUM_NOTES) {
          Serial.println("[note]   Received " + String(NOTES[noteId].name) +
                         " (" + String(NOTES[noteId].freq) + " Hz) from partner!");
          fireNote(noteId);
        }
      }
      break;
    }

    default: break;
  }
}

// ── Setup ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("========================================");
  Serial.println("  EchoChain 7-Note Version — booting...");
  Serial.println("========================================");

  // Button pins (7 个)
  for (int i = 0; i < NUM_NOTES; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }

  // Speaker
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  // LED strip
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  ledOff();

  // Boot test: 走一圈 7 色
  Serial.println("[led]    Boot test: Do → Ti");
  for (int i = 0; i < NUM_NOTES; i++) {
    ledShowNote(i);
    delay(200);
  }
  ledOff();
  Serial.println("[led]    Boot test done.");

  // 蓝色表示正在连 WiFi
  fill_solid(leds, NUM_LEDS, CRGB(0, 0, 40));
  FastLED.show();

  // WiFi
  Serial.print("[wifi]   Connecting to " WIFI_SSID);
  #if OPEN_NETWORK
    WiFi.begin(WIFI_SSID);
  #else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  #endif

  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\n[wifi]   Connected — IP: " + WiFi.localIP().toString());
  ledOff();

  // WebSocket (SSL)
  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWSEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(25000, 3000, 2);

  Serial.println("[socket] Connecting to wss://" WS_HOST " ...");
}

// ── Loop ─────────────────────────────────────────────────────────────────
void loop() {
  ws.loop();

  // 扫描所有 7 个按钮
  for (int i = 0; i < NUM_NOTES; i++) {
    bool btn = digitalRead(BTN_PINS[i]);

    // 边沿检测：从 HIGH 跳到 LOW = 刚按下
    if (btn == LOW && lastBtn[i] == HIGH) {
      Serial.println("[btn" + String(i + 1) + "]   " +
                     String(NOTES[i].name) + " pressed!");
      

      if (wsConnected && partnerOnline) {
        sendNote(i);
      } else if (wsConnected) {
        Serial.println("[btn" + String(i + 1) + "]   Partner not online — local only.");
      }

      fireNote(i);

      // 等松开 + 消抖
      while (digitalRead(BTN_PINS[i]) == LOW) {
        ws.loop();
        delay(10);
      }
      delay(50);
    }

    lastBtn[i] = btn;
  }
}