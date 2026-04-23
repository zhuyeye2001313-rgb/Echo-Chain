// ═══════════════════════════════════════════════════════════════════════
//  EchoChainSocket — v1 Prototype (7 notes: Do → Ti)
//  Arduino Nano ESP32 + passive buzzer + RGB LED + button
//
//  Required library (install via Arduino Library Manager):
//    • WebSockets  by Markus Sattler  (search: "WebSockets", author: Links2004)
//
//  Wiring:
//    Buzzer (passive)   → (+) → D7,  (-) → GND
//    Button (4-prong)   → One side → A7,  Other side → GND
//    RGB LED module     → (-)  → GND
//                         (R)  → A0
//                         (G)  → A1
//                         (B)  → A2
//
//  Setup:
//    1. Fill in WiFi credentials below
//    2. Set PAIR_ID and DEVICE_ID to match the web UI scenario tab
//    3. Flash, open Serial Monitor at 115200
// ═══════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebSocketsClient.h>

// ── CONFIGURE THESE ────────────────────────────────────────────────────
#define WIFI_SSID     "dragonfly"
#define WIFI_PASSWORD "Mpa-2093-1027-2"

#define WS_HOST       "echo-chain-relay-7a6752ad0567.herokuapp.com"
#define WS_PORT       443
#define WS_PATH       "/"

//  Pair IDs match the web UI scenario tabs:
//    pair_web   → Web ↔ Web       (no hardware needed)
//    pair_mixed → Web ↔ Hardware  (this board = hw-B, browser = web-A)
//    pair_hw    → HW  ↔ HW       (hw-A on one board, hw-B on the other)
#define DEVICE_ID     "hw-B"       // hw-A on the other board
#define PAIR_ID       "pair_mixed" // match the scenario tab on the web UI

#define OPEN_NETWORK  0  // 1 for open/guest WiFi (no password)
// ───────────────────────────────────────────────────────────────────────

// ── PIN ASSIGNMENTS (Arduino Nano ESP32) ───────────────────────────────
#define BTN_PIN      A7   // Button → GND
#define SPEAKER_PIN  D7   // Buzzer positive wire
#define LED_R        A0   // RGB module R
#define LED_G        A1   // RGB module G
#define LED_B        A2   // RGB module B
// ───────────────────────────────────────────────────────────────────────

#define NOTE_DUR  1000  // ms — duration for all notes

// ── Note table (Do → Ti) ───────────────────────────────────────────────
//  With 3 LED channels (R/G/B on/off), we get 7 distinct colors:
//    Do  = Red        Re = Yellow (R+G)   Mi  = Green
//    Fa  = Cyan (G+B) Sol = Blue          La  = Magenta (R+B)
//    Ti  = White (R+G+B)
struct Note {
  const char* name;
  int freq;
  bool r, g, b;
};

const Note NOTES[] = {
  { "Do",  262, 1, 0, 0 },  // 0 — Red
  { "Re",  294, 1, 1, 0 },  // 1 — Yellow
  { "Mi",  330, 0, 1, 0 },  // 2 — Green
  { "Fa",  349, 0, 1, 1 },  // 3 — Cyan
  { "Sol", 392, 0, 0, 1 },  // 4 — Blue
  { "La",  440, 1, 0, 1 },  // 5 — Magenta
  { "Ti",  494, 1, 1, 1 },  // 6 — White
};

const int NUM_NOTES = 7;

WebSocketsClient ws;
bool partnerOnline = false;
bool wsConnected   = false;

// ── LED helpers ─────────────────────────────────────────────────────────
void ledOff() {
  digitalWrite(LED_R, LOW);
  digitalWrite(LED_G, LOW);
  digitalWrite(LED_B, LOW);
}

void ledColor(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? HIGH : LOW);
  digitalWrite(LED_G, g ? HIGH : LOW);
  digitalWrite(LED_B, b ? HIGH : LOW);
}

// ── Speaker helper (software square wave — no LEDC, no conflicts) ──────
void buzzerTone(int freq, int duration) {
  unsigned long endTime = millis() + duration;
  int halfPeriod = 500000 / freq;  // microseconds
  while (millis() < endTime) {
    digitalWrite(SPEAKER_PIN, HIGH);
    delayMicroseconds(halfPeriod);
    digitalWrite(SPEAKER_PIN, LOW);
    delayMicroseconds(halfPeriod);
  }
}

// ── Play a note by ID: LED color + buzzer tone ─────────────────────────
void fireNote(int noteId) {
  if (noteId < 0 || noteId >= NUM_NOTES) return;
  const Note& n = NOTES[noteId];
  ledColor(n.r, n.g, n.b);
  buzzerTone(n.freq, NOTE_DUR);
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
  if (idx == -1) return 0;  // default to Do
  idx += 10;  // skip past "note_id":
  while (idx < (int)msg.length() && msg[idx] == ' ') idx++;  // skip spaces
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

      if (msg.indexOf("\"joined\"") > -1)
        Serial.println("[socket] Joined relay as " DEVICE_ID);

      else if (msg.indexOf("\"partner_joined\"") > -1) {
        partnerOnline = true;
        Serial.println("[socket] Partner is online — ready to send notes.");
        // Quick green flash to confirm pairing
        ledColor(false, true, false);
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
  Serial.println("  EchoChain v1 — booting...");
  Serial.println("========================================");

  // Pin setup
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  ledOff();

  // Boot LED test: R → G → B, 200ms each
  Serial.println("[led]    Boot test: R...");
  ledColor(true, false, false); delay(200);
  Serial.println("[led]    Boot test: G...");
  ledColor(false, true, false); delay(200);
  Serial.println("[led]    Boot test: B...");
  ledColor(false, false, true); delay(200);
  ledOff();
  Serial.println("[led]    Boot test done.");

  // Blue LED while connecting WiFi
  ledColor(false, false, true);

  // Connect to WiFi
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

  // Connect WebSocket (SSL to Heroku)
  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWSEvent);
  ws.setReconnectInterval(3000);
  ws.enableHeartbeat(25000, 3000, 2);

  Serial.println("[socket] Connecting to wss://" WS_HOST " ...");
}

// ── Loop ─────────────────────────────────────────────────────────────────
void loop() {
  ws.loop();

  // Physical button = Do (note 0) — more buttons can be added later
  if (wsConnected && digitalRead(BTN_PIN) == LOW) {
    Serial.println("[btn]    Do pressed!");
    fireNote(0);  // local feedback

    if (partnerOnline) {
      sendNote(0);
    } else {
      Serial.println("[btn]    Partner not online — note played locally only.");
    }

    // Debounce: wait for release
    while (digitalRead(BTN_PIN) == LOW) {
      ws.loop();  // keep WebSocket alive during hold
      delay(10);
    }
    delay(50);
  }
}
