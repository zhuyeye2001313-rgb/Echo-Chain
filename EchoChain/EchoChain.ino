// ═══════════════════════════════════════════════════════════════════════
//  EchoChain — final prototype sketch
//  Arduino Nano ESP32 + WS2812B LED strip + passive buzzer + 7 buttons
//  (direct GPIO wiring — no shift register)
//
//  Required libraries (Arduino Library Manager):
//    • WebSockets  by Markus Sattler  (search: "WebSockets", author: Links2004)
//    • FastLED     by Daniel Garcia   (search: "FastLED")
//
//  Wiring summary:
//    LED strip (WS2812B) → DATA → D2 (GPIO5),  5V → VUSB,  GND → GND
//    Buzzer (passive)    → (+)  → 100Ω → D9,   (-) → GND
//    7 buttons (each)    → one leg → assigned pin, other leg → GND
//                          Do=D11  Re=D10  Mi=D8  Fa=D7  Sol=D6  La=D5  Ti=D4
//
//  Scenarios (web UI tabs):
//    pair_web   → Web ↔ Web       (no hardware needed)
//    pair_mixed → Web ↔ Hardware  (this board = hw-A or hw-B paired with browser)
//    pair_hw    → HW  ↔ HW       (hw-A on one board, hw-B on the other)
// ═══════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <FastLED.h>

// ── CONFIGURE THESE ────────────────────────────────────────────────────
#define WIFI_SSID     "dragonfly"
#define WIFI_PASSWORD "Mpa-2093-1027-2"

#define WS_HOST       "echo-chain-relay-7a6752ad0567.herokuapp.com"
#define WS_PORT       443
#define WS_PATH       "/"

#define DEVICE_ID     "hw-A"        // partner board = hw-B
#define PAIR_ID       "pair_mixed"  // solo testing against the web UI

#define OPEN_NETWORK  0  // 1 for open/guest WiFi (no password)
// ───────────────────────────────────────────────────────────────────────

// ── PIN ASSIGNMENTS ─────────────────────────────────────────────────────
#define LED_PIN       5     // board D2 = GPIO5 (raw int — FastLED requires)
#define SPEAKER_PIN   D9

// 7 buttons → Do, Re, Mi, Fa, Sol, La, Ti (in order)
const int BTN_PINS[] = { D11, D10, D8, D7, D6, D5, D4 };
// ───────────────────────────────────────────────────────────────────────

// ── LED STRIP CONFIG ───────────────────────────────────────────────────
//  Two profiles selected by USE_PARTNER_LED_CONFIG below:
//    0 = this board (denser strip) — full strip fills with note color, BRIGHTNESS 200
//    1 = partner's board           — single LED at note's ledIndex,    BRIGHTNESS 50
#define USE_PARTNER_LED_CONFIG  0

#define NUM_LEDS     10        // bump up once you cut the strip for the enclosure
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB

#if USE_PARTNER_LED_CONFIG
  #define BRIGHTNESS  50
#else
  #define BRIGHTNESS  200
#endif

#define NOTE_DUR     500       // ms — blocking tone duration per press
// ───────────────────────────────────────────────────────────────────────

CRGB leds[NUM_LEDS];

// ── Note table (Do → Ti) ───────────────────────────────────────────────
//  ledIndex used only when USE_PARTNER_LED_CONFIG = 1 (single-LED render)
struct Note {
  const char* name;
  int         freq;
  uint8_t     r, g, b;
  int         ledIndex;
};

const Note NOTES[] = {
  { "Do",  262, 255,   0,   0, 0 },  // 0 — Red
  { "Re",  294, 255, 128,   0, 1 },  // 1 — Orange
  { "Mi",  330, 255, 255,   0, 2 },  // 2 — Yellow
  { "Fa",  349,   0, 255,   0, 3 },  // 3 — Green
  { "Sol", 392,   0, 128, 255, 4 },  // 4 — Blue
  { "La",  440, 128,   0, 255, 5 },  // 5 — Purple
  { "Ti",  494, 255,   0, 255, 6 },  // 6 — Magenta
};

const int NUM_NOTES = 7;

// ── State ───────────────────────────────────────────────────────────────
WebSocketsClient ws;
bool wsConnected   = false;
bool partnerOnline = false;
bool lastBtn[NUM_NOTES] = { HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH };

// ── LED helpers ─────────────────────────────────────────────────────────
void ledOff() {
  FastLED.clear();
  FastLED.show();
}

// Render the note — either fills the whole strip (this board)
// or lights a single LED at the note's ledIndex (partner's board)
void ledShowNote(int noteId) {
  if (noteId < 0 || noteId >= NUM_NOTES) return;
  const Note& n = NOTES[noteId];
  FastLED.clear();
  #if USE_PARTNER_LED_CONFIG
    if (n.ledIndex >= 0 && n.ledIndex < NUM_LEDS) {
      leds[n.ledIndex] = CRGB(n.r, n.g, n.b);
    }
  #else
    fill_solid(leds, NUM_LEDS, CRGB(n.r, n.g, n.b));
  #endif
  FastLED.show();
}

// ── Speaker helper (blocking square wave) ──────────────────────────────
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

// ── Play a note: LED + tone (blocks for NOTE_DUR) ──────────────────────
void fireNote(int noteId) {
  if (noteId < 0 || noteId >= NUM_NOTES) return;
  ledShowNote(noteId);
  buzzerTone(NOTES[noteId].freq, NOTE_DUR);
  ledOff();
}

// ── Boot melody: Do → Re → Mi  (pause)  Sol → La → Ti ──────────────────
void bootMelody() {
  const int BOOT_NOTE_DUR   = 220;
  const int BOOT_GAP        = 60;
  const int BOOT_PHRASE_GAP = 180;
  const int phraseA[] = { 0, 1, 2 };  // Do, Re, Mi
  const int phraseB[] = { 4, 5, 6 };  // Sol, La, Ti

  for (int i = 0; i < 3; i++) {
    ledShowNote(phraseA[i]);
    buzzerTone(NOTES[phraseA[i]].freq, BOOT_NOTE_DUR);
    ledOff();
    delay(BOOT_GAP);
  }
  delay(BOOT_PHRASE_GAP);
  for (int i = 0; i < 3; i++) {
    ledShowNote(phraseB[i]);
    buzzerTone(NOTES[phraseB[i]].freq, BOOT_NOTE_DUR);
    ledOff();
    delay(BOOT_GAP);
  }
}

// ── WebSocket helpers ──────────────────────────────────────────────────
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
        Serial.println("[socket] Partner online — ready to send notes.");
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
        delay(250);
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
  Serial.println("  EchoChain — booting...");
  Serial.println("========================================");

  // Buttons
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

  // Boot melody — coordinated LED + tone
  Serial.println("[boot]   Playing boot melody...");
  bootMelody();
  ledOff();
  Serial.println("[boot]   Boot melody done.");

  // Dim blue while connecting WiFi
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

  // WebSocket (SSL → Heroku relay)
  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWSEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(25000, 3000, 2);

  Serial.println("[socket] Connecting to wss://" WS_HOST " ...");
}

// ── Loop ─────────────────────────────────────────────────────────────────
void loop() {
  ws.loop();

  // Scan all 7 buttons; rising-edge detect (HIGH → LOW) = just pressed
  for (int i = 0; i < NUM_NOTES; i++) {
    bool btn = digitalRead(BTN_PINS[i]);

    if (btn == LOW && lastBtn[i] == HIGH) {
      Serial.println("[btn]    " + String(NOTES[i].name) + " pressed!");

      // ⭐ Latency improvement: send network frame FIRST so the partner /
      //    web UI doesn't wait on our local 500ms blocking playback.
      if (wsConnected && partnerOnline) {
        sendNote(i);
      } else if (wsConnected) {
        Serial.println("[btn]    Partner not online — playing locally only.");
      }

      // Then local LED + tone (this blocks for NOTE_DUR)
      fireNote(i);

      // Wait for release + debounce
      while (digitalRead(BTN_PINS[i]) == LOW) {
        ws.loop();
        delay(10);
      }
      delay(50);
    }

    lastBtn[i] = btn;
  }
}
