#include <WiFi.h>
#include <WebSocketsClient.h>
#include <FastLED.h>

// ── WIFI ─────────────────────────────
#define WIFI_SSID     "1338 W 35th Resident"
#define WIFI_PASSWORD "jbrliyiz"

// ── WebSocket ────────────────────────
#define WS_HOST "echo-chain-relay-7a6752ad0567.herokuapp.com"
#define WS_PORT 443
#define WS_PATH "/"

#define DEVICE_ID "hw-B"
#define PAIR_ID   "pair_mixed"

// ── PIN ─────────────────────────────
#define LED_PIN       5
#define SPEAKER_PIN   D9

const int BTN_PINS[] = { D11, D10, D8, D7, D6, D5, D4 };

// ── LED ─────────────────────────────
#define NUM_LEDS 10
#define BRIGHTNESS 120

CRGB leds[NUM_LEDS];

// ── NOTE ────────────────────────────
struct Note {
  int freq;
  uint8_t r, g, b;
  int ledIndex;
};

const Note NOTES[] = {
  {262, 255,   0,   0, 0}, // 红
  {294, 255, 100,   0, 1}, // 橙（偏橘）
  {330, 255, 255,   0, 2}, // 黄
  {349,   0, 255,   0, 3}, // 绿
  {392,   0, 255, 255, 4}, // 青
  {440,   0,   0, 255, 5}, // 蓝
  {494, 255,   0, 255, 6}  // 紫
};

#define NUM_NOTES 7

// ── GLOBAL ──────────────────────────
WebSocketsClient ws;

bool wsConnected = false;
bool partnerOnline = false;

bool lastBtn[7] = {HIGH,HIGH,HIGH,HIGH,HIGH,HIGH,HIGH};

int currentNote = -1;
int currentFreq = 0;

// ⭐ 防止网页卡音
unsigned long lastNoteTime = 0;
const int NOTE_TIMEOUT = 300;

// ── LED ─────────────────────────────
void ledOff() {
  FastLED.clear();
  FastLED.show();
}

void ledShowOne(int noteId) {
  FastLED.clear();
  leds[NOTES[noteId].ledIndex] = CRGB(
    NOTES[noteId].r,
    NOTES[noteId].g,
    NOTES[noteId].b
  );
  FastLED.show();
}

// ── SOUND ───────────────────────────
void startTone(int freq) {
  currentFreq = freq;
}

void stopTone() {
  currentFreq = 0;
  digitalWrite(SPEAKER_PIN, LOW);
}

// ── NETWORK ─────────────────────────
void sendNote(int noteId) {
  String msg = "{\"type\":\"note\",\"note_id\":" + String(noteId) + "}";
  ws.sendTXT(msg);
}

int parseNoteId(String msg) {
  int idx = msg.indexOf("note_id");
  if (idx == -1) return -1;
  return msg.substring(idx + 9).toInt();
}

// ── WebSocket ───────────────────────
void onWSEvent(WStype_t type, uint8_t* payload, size_t length) {

  if (type == WStype_CONNECTED) {
    wsConnected = true;

    String joinMsg =
      "{\"type\":\"join\",\"pair_id\":\"" PAIR_ID "\",\"device_id\":\"" DEVICE_ID "\"}";
    ws.sendTXT(joinMsg);
  }

  else if (type == WStype_DISCONNECTED) {
    wsConnected = false;
    partnerOnline = false;
  }

  else if (type == WStype_TEXT) {
    String msg = String((char*)payload);

    if (msg.indexOf("partner_joined") > -1) {
      partnerOnline = true;
    }

    else if (msg.indexOf("note") > -1) {
      int id = parseNoteId(msg);

      if (id >= 0 && id < NUM_NOTES) {
        currentNote = id;
        startTone(NOTES[id].freq);
        ledShowOne(id);

        lastNoteTime = millis(); // ⭐关键
      }
    }
  }
}

// ── SETUP ───────────────────────────
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_NOTES; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }

  pinMode(SPEAKER_PIN, OUTPUT);

  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  ledOff();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  ws.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  ws.onEvent(onWSEvent);
}

// ── LOOP ────────────────────────────
void loop() {

  ws.loop();

  // 🎵 持续发声
  if (currentFreq > 0) {
    int halfPeriod = 500000 / currentFreq;
    digitalWrite(SPEAKER_PIN, HIGH);
    delayMicroseconds(halfPeriod);
    digitalWrite(SPEAKER_PIN, LOW);
    delayMicroseconds(halfPeriod);
  }

  // 🔘 硬件按钮
  for (int i = 0; i < NUM_NOTES; i++) {

    bool btn = digitalRead(BTN_PINS[i]);

    // 按住
    if (btn == LOW) {

      currentNote = i;
      startTone(NOTES[i].freq);
      ledShowOne(i);

      lastNoteTime = millis();  // ⭐关键修复（不被timeout杀掉）

      if (lastBtn[i] == HIGH && wsConnected && partnerOnline) {
        sendNote(i);
      }
    }

    // 松开
    else {
      if (lastBtn[i] == LOW && currentNote == i) {
        stopTone();
        ledOff();
        currentNote = -1;
      }
    }

    lastBtn[i] = btn;
  }

  // ⭐ 自动停止（网页防卡）
  if (currentFreq > 0 && millis() - lastNoteTime > NOTE_TIMEOUT) {
    stopTone();
    ledOff();
    currentNote = -1;
  }
}