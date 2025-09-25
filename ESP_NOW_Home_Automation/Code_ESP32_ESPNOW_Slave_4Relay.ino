#include <WiFi.h>
#include <esp_now.h>
#include <AceButton.h>

using namespace ace_button;

// Relay Pins (active low, so relay turns ON when pin is LOW)

const int relayPins[] = {23, 19, 18, 5};
const int numRelays = 4;
bool relayStates[numRelays] = {false, false, false, false};

// Push Button Pins
const int buttonPins[] = {13, 12, 14, 27};  // Push-button pins 

// Button Objects
AceButton buttons[numRelays] = {
  AceButton(buttonPins[0]), AceButton(buttonPins[1]),
  AceButton(buttonPins[2]), AceButton(buttonPins[3])
};

// Structure for receiving data from Master
typedef struct {
  int buttonID;
  bool state;
} ButtonData;

ButtonData buttonData;

// Structure for sending feedback to Master
typedef struct {
  bool relayStates[numRelays]; // State of all relays
} FeedbackData;

FeedbackData feedbackData;

// ESP-NOW Connection Status
bool masterConnected = false;

// Master MAC Address (replace with actual Master ESP32 MAC address)
uint8_t masterMAC[] = {0xC0, 0x49, 0xEF, 0xD1, 0x25, 0x4C};  // Replace with actual MAC

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&buttonData, incomingData, sizeof(buttonData));

  // Toggle relay state based on received data
  int relayIndex = buttonData.buttonID;
  if (relayIndex >= 0 && relayIndex < numRelays) {
    relayStates[relayIndex] = buttonData.state;
    // Set the corresponding relay pin state (active low)
    digitalWrite(relayPins[relayIndex], relayStates[relayIndex] ? LOW : HIGH);

    // Send feedback to Master with updated relay states
    sendFeedbackToMaster();
  }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  masterConnected = (status == ESP_NOW_SEND_SUCCESS);
}

void sendFeedbackToMaster() {
  // Copy relay states to feedback data structure
  memcpy(feedbackData.relayStates, relayStates, sizeof(feedbackData.relayStates));

  // Send the relay states as feedback to the master
  esp_now_send(masterMAC, (uint8_t *)&feedbackData, sizeof(feedbackData));

  // Print feedback to serial to verify data is being sent
  Serial.print("Sending feedback to master: ");
  for (int i = 0; i < 4; i++) {
    Serial.print(feedbackData.relayStates[i] ? "ON " : "OFF ");
  }
  Serial.println();
}

void handleButtonPress(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  if (eventType == AceButton::kEventReleased) {
    int buttonIndex = button->getPin();
    
    // Find the corresponding relay index
    for (int i = 0; i < numRelays; i++) {
      if (buttonPins[i] == buttonIndex) {
        // Toggle the relay state
        relayStates[i] = !relayStates[i];
        digitalWrite(relayPins[i], relayStates[i] ? LOW : HIGH);

        // Send feedback to Master (if connected)
        if (masterConnected) {
          sendFeedbackToMaster();
        }
        break;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize relays (active low, so relays are OFF initially)
  for (int i = 0; i < numRelays; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH);  // Set initial state to OFF (active low)
  }

  // Initialize buttons with AceButton
  ButtonConfig *buttonConfig = ButtonConfig::getSystemButtonConfig();
  buttonConfig->setEventHandler(handleButtonPress);

  // Set up the button pins as INPUT_PULLUP
  for (int i = 0; i < numRelays; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    buttons[i].getButtonConfig()->setEventHandler(handleButtonPress);
  }

  // Setup ESP-NOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  // Add Master peer for ESP-NOW communication
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, masterMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
  }
}

void loop() {
  // Check button presses without delay
  for (int i = 0; i < numRelays; i++) {
    buttons[i].check();
  }

  // Periodically send feedback to master every 1000ms (non-blocking)
  static unsigned long lastFeedbackTime = 0;
  if (millis() - lastFeedbackTime >= 2000) {
    lastFeedbackTime = millis();
    sendFeedbackToMaster();  // Send the relay states to master every 2 second
  }
}
