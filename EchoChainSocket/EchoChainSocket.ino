// ═══════════════════════════════════════════════════════════════════════
//  EchoChainSocket — WebSocket relay test sketch
//  One button, one buzzer. Press on Device A → buzzes on Device B.
//
//  Required libraries (install via Arduino Library Manager):
//    • ArduinoWebsockets  by Gil Mishne
//
//  Setup:
//    1. Fill in your WiFi credentials below.
//    2. Set DEVICE_ID to "echoA-01" on one board, "echoB-02" on the other.
//    3. For LOCAL testing: set WS_URL to ws://<your-computer-IP>:3000
//       For HEROKU testing: set WS_URL to wss://<your-app>.herokuapp.com
//    4. Flash both boards, open Serial Monitor at 115200 baud.
// ═══════════════════════════════════════════════════════════════════════

#include <WiFi.h>
#include <ArduinoWebsockets.h>
using namespace websockets;

// ── CONFIGURE THESE ────────────────────────────────────────────────────
#define WIFI_SSID     "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"

// Local test:  "ws://192.168.x.x:3000"
// Heroku:      "wss://echo-chain-relay-7a6752ad0567.herokuapp.com"
#define WS_URL        "wss://echo-chain-relay-7a6752ad0567.herokuapp.com"

#define DEVICE_ID     "echoA-01"   // → "echoB-02" on the other board
#define PAIR_ID       "pair_001"

// ── OPEN NETWORK (no password) ─────────────────────────────────────────
// Campus / guest WiFi with no password — set this to 1.
// If your network requires a password, set to 0 and fill in WIFI_PASSWORD.
#define OPEN_NETWORK  1
// ───────────────────────────────────────────────────────────────────────

const int BTN_PIN    = 13;
const int BUZZER_PIN = 18;
const int NOTE_ID    = 0;   // single note for this test (Do / 262 Hz)

WebsocketsClient ws;
bool partnerOnline = false;

// ── Helpers ─────────────────────────────────────────────────────────────
void buzz(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void sendJSON(String payload) {
  ws.send(payload);
  Serial.println("TX: " + payload);
}

void joinPair() {
  sendJSON(
    "{\"type\":\"join\","
    "\"pair_id\":\"" + String(PAIR_ID) + "\","
    "\"device_id\":\"" + String(DEVICE_ID) + "\"}"
  );
}

void sendNote() {
  sendJSON(
    "{\"type\":\"note\","
    "\"note_id\":" + String(NOTE_ID) + "}"
  );
}

// ── WebSocket message handler ───────────────────────────────────────────
void onMessage(WebsocketsMessage msg) {
  String data = msg.data();
  Serial.println("RX: " + data);

  if (data.indexOf("\"joined\"") > -1) {
    Serial.println("[socket] Connected to relay as " DEVICE_ID);
  }
  else if (data.indexOf("\"partner_joined\"") > -1) {
    partnerOnline = true;
    Serial.println("[socket] Partner is online — ready to send notes.");
  }
  else if (data.indexOf("\"partner_left\"") > -1) {
    partnerOnline = false;
    Serial.println("[socket] Partner disconnected.");
  }
  else if (data.indexOf("\"note\"") > -1) {
    Serial.println("[note]   Received from partner — buzzing!");
    buzz(300);
  }
}

// ── WebSocket event handler ─────────────────────────────────────────────
void onEvent(WebsocketsEvent event, String data) {
  if (event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("[socket] Connection opened — joining pair...");
    joinPair();
  }
  else if (event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("[socket] Connection closed — reconnecting in 3s...");
    partnerOnline = false;
  }
  else if (event == WebsocketsEvent::GotPing) {
    ws.pong();
  }
}

// ── Connect / reconnect ─────────────────────────────────────────────────
void connectWS() {
  Serial.println("[socket] Connecting to " WS_URL " ...");
  ws.onMessage(onMessage);
  ws.onEvent(onEvent);
  bool ok = ws.connect(WS_URL);
  if (!ok) Serial.println("[socket] Connection failed — will retry in loop.");
}

// ── Setup ───────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN,    INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  // Connect to WiFi
  Serial.print("[wifi] Connecting to " WIFI_SSID);
  #if OPEN_NETWORK
    WiFi.begin(WIFI_SSID);           // open network — no password
  #else
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  #endif
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\n[wifi] Connected — IP: " + WiFi.localIP().toString());

  connectWS();
}

// ── Loop ────────────────────────────────────────────────────────────────
void loop() {
  // Keep WebSocket alive; reconnect if dropped
  if (!ws.available()) {
    delay(3000);
    connectWS();
    return;
  }
  ws.poll();

  // Button press — send note to partner
  if (digitalRead(BTN_PIN) == LOW) {
    buzz(300);  // local feedback

    if (partnerOnline) {
      Serial.println("[btn]    Button pressed — sending note to partner.");
      sendNote();
    } else {
      Serial.println("[btn]    Button pressed — partner not online yet.");
    }

    delay(500);  // debounce
    while (digitalRead(BTN_PIN) == LOW) delay(10);  // wait for release
  }
}
