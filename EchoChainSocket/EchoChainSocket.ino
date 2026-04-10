// ═══════════════════════════════════════════════════════════════════════
//  EchoChainSocket — WebSocket relay test sketch
//  One button, one buzzer. Press on Device A → buzzes on Device B.
//
//  Required library (install via Arduino Library Manager):
//    • WebSockets  by Markus Sattler  (search: "WebSockets", author: Links2004)
//
//  Setup:
//    1. Fill in your WiFi credentials below.
//    2. Set DEVICE_ID to "echoA-01" on one board, "echoB-02" on the other.
//    3. Flash both boards, open Serial Monitor at 115200 baud.
// ═══════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebSocketsClient.h>

// ── CONFIGURE THESE ────────────────────────────────────────────────────
#define WIFI_SSID     "dragonfly"
#define WIFI_PASSWORD "Mpa-2093-1027-2"

#define WS_HOST       "echo-chain-relay-7a6752ad0567.herokuapp.com"
#define WS_PORT       443
#define WS_PATH       "/"

#define DEVICE_ID     "echoA-01"   // → "echoB-02" on the other board
#define PAIR_ID       "pair_001"

// Set 1 for open/guest WiFi (no password), 0 if password required
#define OPEN_NETWORK  0
// ───────────────────────────────────────────────────────────────────────

const int BTN_PIN    = 13;
const int BUZZER_PIN = 18;
const int NOTE_ID    = 0;   // single note test (Do / 262 Hz)

WebSocketsClient ws;
bool partnerOnline = false;
bool wsConnected   = false;

// ── Helpers ─────────────────────────────────────────────────────────────
void buzz(int ms) {
  tone(BUZZER_PIN, 262, ms);
  delay(ms + 20);
}

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

void sendNote() {
  String msg = "{\"type\":\"note\",\"note_id\":" + String(NOTE_ID) + "}";
  ws.sendTXT(msg);
  Serial.println("TX: " + msg);
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
      }
      else if (msg.indexOf("\"partner_left\"") > -1) {
        partnerOnline = false;
        Serial.println("[socket] Partner disconnected.");
      }
      else if (msg.indexOf("\"note\"") > -1) {
        Serial.println("[note]   Received from partner — buzzing!");
        buzz(300);
      }
      break;
    }

    default: break;
  }
}

// ── Setup ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

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

  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWSEvent);
  ws.setReconnectInterval(3000);      // auto-reconnect if dropped
  ws.enableHeartbeat(25000, 3000, 2); // ping every 25s — keeps Heroku alive

  Serial.println("[socket] Connecting to wss://" WS_HOST " ...");
}

// ── Loop ─────────────────────────────────────────────────────────────────
void loop() {
  ws.loop();

  if (wsConnected && digitalRead(BTN_PIN) == LOW) {
    buzz(300);

    if (partnerOnline) {
      Serial.println("[btn]    Button pressed — sending note to partner.");
      sendNote();
    } else {
      Serial.println("[btn]    Button pressed — partner not online yet.");
    }

    delay(500);
    while (digitalRead(BTN_PIN) == LOW) delay(10);
  }
}
