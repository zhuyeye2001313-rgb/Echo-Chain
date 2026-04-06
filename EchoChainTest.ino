#include <esp_now.h>
#include <WiFi.h>
#include <FirebaseESP32.h>

// ── CHANGE THESE ────────────────────────────────────────────────────
uint8_t partnerMAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

#define WIFI_SSID     "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"
#define FIREBASE_HOST "your-project-id-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "your_firebase_database_secret"

#define DEVICE_ID     "echoA-01"   // change to "echoB-02" on Device B
#define PAIR_ID       "pair_001"
// ────────────────────────────────────────────────────────────────────

// Button + LED + Buzzer pins
const int BTN_PINS[]  = {13, 12, 14, 27, 26, 25, 33};
const int LED_PINS[]  = {15,  2,  4, 16, 17,  5, 18};
const int BUZZER_PIN  = 19;
const int NUM_BUTTONS = 7;

const char* NOTE_NAMES[] = {"Do","Re","Mi","Fa","Sol","La","Ti"};
const int   FREQS[]      = {262, 294, 330, 349, 392,  440, 494};

// Firebase objects
FirebaseData   fbData;
FirebaseConfig fbConfig;
FirebaseAuth   fbAuth;

// ESP-NOW message
typedef struct {
  int  note_id;
  char sender_id[16];
} NoteMessage;

NoteMessage outMsg;
NoteMessage inMsg;

// ── Log event to Firebase ─────────────────────────────────────────
void logToFirebase(int note_id, const char* triggered_by) {
  if (WiFi.status() != WL_CONNECTED) return;  // skip if offline

  String path = "/echo-chain/events/" + String(PAIR_ID);

  FirebaseJson json;
  json.set("device_id",    DEVICE_ID);
  json.set("pair_id",      PAIR_ID);
  json.set("note_id",      note_id);
  json.set("note_name",    NOTE_NAMES[note_id]);
  json.set("frequency",    FREQS[note_id]);
  json.set("triggered_by", triggered_by); // "self" or "partner"
  json.set("timestamp",    (int)millis());

  // push() creates a new unique entry each time (like an append)
  Firebase.push(fbData, path, json);
}

// ── ESP-NOW receive callback ──────────────────────────────────────
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  memcpy(&inMsg, data, sizeof(inMsg));
  int n = inMsg.note_id;

  // Actuate LED + buzzer
  digitalWrite(LED_PINS[n], HIGH);
  tone(BUZZER_PIN, FREQS[n], 300);
  delay(300);
  digitalWrite(LED_PINS[n], LOW);

  // Log to Firebase (triggered by partner)
  logToFirebase(n, "partner");

  Serial.printf("Received note %s from partner\n", NOTE_NAMES[n]);
}

void onSent(const uint8_t *mac, esp_now_send_status_t status) {
  // optional
}

void setup() {
  Serial.begin(115200);

  // Pins
  for (int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    pinMode(LED_PINS[i], OUTPUT);
  }

  // ── Connect to Wi-Fi ──
  WiFi.mode(WIFI_AP_STA);  // AP_STA mode allows ESP-NOW + Wi-Fi together
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected: " + WiFi.localIP().toString());

  // ── Init Firebase ──
  fbConfig.host = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);

  // ── Init ESP-NOW ──
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  // Register partner as peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, partnerMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  Serial.println("Echo Chain ready!");
}

void loop() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    if (digitalRead(BTN_PINS[i]) == LOW) {

      // Actuate own LED + buzzer
      digitalWrite(LED_PINS[i], HIGH);
      tone(BUZZER_PIN, FREQS[i], 300);

      // Send to partner via ESP-NOW
      outMsg.note_id = i;
      strncpy(outMsg.sender_id, DEVICE_ID, sizeof(outMsg.sender_id));
      esp_now_send(partnerMAC, (uint8_t *)&outMsg, sizeof(outMsg));

      // Log to Firebase (triggered by self)
      logToFirebase(i, "self");

      Serial.printf("Sent note %s\n", NOTE_NAMES[i]);

      delay(300);
      digitalWrite(LED_PINS[i], LOW);
      while (digitalRead(BTN_PINS[i]) == LOW) delay(10);
    }
  }
}
