// ═══════════════════════════════════════════════════════════════════════
//  EchoChainSocket — LED Strip + 7 Buttons
// ═══════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <FastLED.h>

// ── CONFIGURE THESE ────────────────────────────────────────────────────
#define WIFI_SSID     "1338 W 35th Resident"
#define WIFI_PASSWORD "jbrliyiz"

#define WS_HOST       "echo-chain-relay-7a6752ad0567.herokuapp.com"
#define WS_PORT       443
#define WS_PATH       "/"

#define DEVICE_ID     "hw-B"
#define PAIR_ID       "pair_mixed"

#define OPEN_NETWORK  0
// ───────────────────────────────────────────────────────────────────────

// ── PIN ASSIGNMENTS ────────────────────────────────────────────────────
#define LED_PIN       5
#define SPEAKER_PIN   D9

const int BTN_PINS[] = { D11, D10, D8, D7, D6, D5, D4 };

// ── LED STRIP CONFIG ───────────────────────────────────────────────────
#define NUM_LEDS      10
#define LED_TYPE      WS2812B
#define COLOR_ORDER   GRB
#define BRIGHTNESS    50
// ───────────────────────────────────────────────────────────────────────

CRGB leds[NUM_LEDS];

// ── NOTE（只改颜色） ───────────────────────────────────────────────
struct Note {
  const char* name;
  int freq;
  uint8_t r, g, b;
  int ledIndex;
};

const Note NOTES[] = {
  { "Do",  262, 255,   0,   0,  0 },
  { "Re",  294, 255, 100,   0,  1 },
  { "Mi",  330, 255, 255,   0,  2 },
  { "Fa",  349,   0, 255,   0,  3 },
  { "Sol", 392,   0, 255, 255,  4 },
  { "La",  440,   0,   0, 255,  5 },
  { "Ti",  494, 255,   0, 255,  6 },
};

const int NUM_NOTES = 7;

WebSocketsClient ws;
bool partnerOnline = false;
bool wsConnected   = false;

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

// ── WebSocket helpers（完全不动） ───────────────────────────────────────
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

int parseNoteId(const String& msg) {
  int idx = msg.indexOf("\"note_id\":");
  if (idx == -1) return 0;
  idx += 10;
  while (idx < (int)msg.length() && msg[idx] == ' ') idx++;
  return msg.substring(idx).toInt();
}

// ── WebSocket event（完全不动） ───────────────────────────────────────
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
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        delay(200);
        ledOff();
      }
      else if (msg.indexOf("\"partner_left\"") > -1) {
        partnerOnline = false;
      }
      else if (msg.indexOf("\"note\"") > -1) {
        int noteId = parseNoteId(msg);
        if (noteId >= 0 && noteId < NUM_NOTES) {
          ledShowNote(noteId);

          // ⭐ 长按持续发声版本
          int freq = NOTES[noteId].freq;
          int halfPeriod = 500000 / freq;

          for (int t = 0; t < 100; t++) {
            digitalWrite(SPEAKER_PIN, HIGH);
            delayMicroseconds(halfPeriod);
            digitalWrite(SPEAKER_PIN, LOW);
            delayMicroseconds(halfPeriod);
          }

          ledOff();
        }
      }
      break;
    }

    default: break;
  }
}

// ── Setup（完全不动） ───────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  for (int i = 0; i < NUM_NOTES; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }

  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  ledOff();

  // Boot test
  for (int i = 0; i < NUM_NOTES; i++) {
    ledShowNote(i);
    delay(200);
  }
  ledOff();

  fill_solid(leds, NUM_LEDS, CRGB(0, 0, 40));
  FastLED.show();

  #if OPEN_NETWORK
    WiFi.begin(WIFI_SSID);
  #else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  #endif

  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
  }

  ledOff();

  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWSEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(25000, 3000, 2);
}

// ── LOOP（只改这里实现长按） ───────────────────────────────────────
void loop() {
  ws.loop();

  for (int i = 0; i < NUM_NOTES; i++) {

    bool btn = digitalRead(BTN_PINS[i]);

    // ⭐ 长按持续发声
    if (btn == LOW) {

      if (lastBtn[i] == HIGH) {
        if (wsConnected && partnerOnline) {
          sendNote(i);
        }
      }

      ledShowNote(i);

      int freq = NOTES[i].freq;
      int halfPeriod = 500000 / freq;

      digitalWrite(SPEAKER_PIN, HIGH);
      delayMicroseconds(halfPeriod);
      digitalWrite(SPEAKER_PIN, LOW);
      delayMicroseconds(halfPeriod);
    }

    else {
      if (lastBtn[i] == LOW) {
        ledOff();
        digitalWrite(SPEAKER_PIN, LOW);
      }
    }

    lastBtn[i] = btn;
  }
}