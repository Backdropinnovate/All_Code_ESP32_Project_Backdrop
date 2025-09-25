#include <WiFi.h>
#include <WebServer.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <AceButton.h>

using namespace ace_button;

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LED_BUILTIN 2  // Built-in LED to indicate slave connection status

const char* ssid = "ESP32_Relay";  // WiFi  SSID
const char* password = "12345678";  // WiFi  password

const int buttonPins[] = {13, 12, 14, 27};  // Pin assignments for 4 buttons
const int numButtons = 4;  // Total number of buttons
bool buttonStates[numButtons] = {false, false, false, false};  // Button states

uint8_t slaveMAC[] = {0x24, 0xD7, 0xEB, 0x14, 0xE2, 0xBC};  // MAC address of the slave

// Structure to store button data
struct ButtonData {
  int buttonID;
  bool state;
};
ButtonData buttonData;

// Structure to store feedback data (relay states)
struct FeedbackData {
  bool relayStates[4];
};
FeedbackData feedbackData;

bool slaveConnected = false; // Status of slave connection
unsigned long lastFeedbackTime = 0; // Time of the last feedback received from slave
const unsigned long feedbackTimeout = 4000; // Timeout to consider no feedback

// Initialize the AceButton objects for each button
AceButton buttons[numButtons] = {
  AceButton(buttonPins[0]),
  AceButton(buttonPins[1]),
  AceButton(buttonPins[2]),
  AceButton(buttonPins[3])
};

WebServer server(80); // Create the WebServer object

// Function to generate HTML page for the control interface
String getHTMLPage() {
  String html = "";
  html += "<!DOCTYPE html><html><head><title>Smart Relay Control</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { background: #f2f2f2; font-family: Arial; text-align: center; margin: 0; padding: 20px; }";
  html += ".grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; max-width: 300px; margin: auto; }";
  html += ".button { padding: 15px; font-size: 16px; color: white; border: none; border-radius: 10px; cursor: pointer; }";
  html += ".on { background-color: #4CAF50; }";
  html += ".off { background-color: #f44336; }";
  html += ".turnAllOff { background-color: #FF5722; width: 100%; margin-top: 20px; }";
  html += "h1 { color: #333; margin-bottom: 20px; }";
  html += "#status { margin-bottom: 20px; font-weight: bold; }";
  html += "footer { margin-top: 30px; font-size: 12px; color: #777; }";
  html += "</style></head><body>";

  html += "<h1>Smart Relay Control</h1>";
  html += "<div id='status'>Slave Status: <span id='slaveStatus' style='color: red;'>Disconnected</span></div>";
  html += "<div class='grid' id='buttons'></div>";
  html += "<button class='button turnAllOff' onclick='turnAllOff()'>Turn ALL OFF</button>";
  html += "<footer>Created by Tech StudyCell</footer>";

  html += "<script>\n";
  html += "function fetchRelayStates(){\n";
  html += "  fetch('/relay_states')\n";
  html += "    .then(response => response.json())\n";
  html += "    .then(data => {\n";
  html += "      let buttonsHTML = '';\n";
  html += "      data.relays.forEach((state, i) => {\n";
  html += "        buttonsHTML += `<button class='button ${state ? 'on' : 'off'}' onclick='toggleRelay(${i}, ${state ? 0 : 1})'>Relay ${i+1} ${state ? 'ON' : 'OFF'}</button>`;\n";
  html += "      });\n";
  html += "      document.getElementById('buttons').innerHTML = buttonsHTML;\n";
  html += "      document.getElementById('slaveStatus').innerText = data.slaveConnected ? 'Connected' : 'Disconnected';\n";
  html += "      document.getElementById('slaveStatus').style.color = data.slaveConnected ? 'green' : 'red';\n";
  html += "    });\n";
  html += "}\n";
  html += "function toggleRelay(relay, state){\n";
  html += "  fetch(`/toggle?relay=${relay}&state=${state}`)\n";
  html += "    .then(() => setTimeout(fetchRelayStates, 500));\n";
  html += "}\n";
  html += "function turnAllOff(){\n";
  html += "  fetch('/turn_all_off')\n";
  html += "    .then(() => setTimeout(fetchRelayStates, 500));\n";
  html += "}\n";
  html += "setInterval(fetchRelayStates, 2000);\n";
  html += "window.onload = fetchRelayStates;\n";
  html += "</script>";

  html += "</body></html>";
  return html;
}

// Callback when data is sent to the slave
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  slaveConnected = (status == ESP_NOW_SEND_SUCCESS); // Update slave connection
  digitalWrite(LED_BUILTIN, slaveConnected ? HIGH : LOW); // Turn on LED if slave is connected
}

// Update the OLED display with current relay states
void updateOLED() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Relay States: ");
  display.println("-----------------");
  for (int i = 0; i < 4; i++) {
    display.print(feedbackData.relayStates[i] ? "ON " : "OFF ");
  }
  display.display();
}

// Callback when data is received from the slave
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  memcpy(&feedbackData, incomingData, sizeof(feedbackData));
  lastFeedbackTime = millis();
  updateOLED();
}

// Handle button presses to toggle relay states
void handleButtonPress(AceButton *button, uint8_t eventType, uint8_t buttonState) {
  if (eventType == AceButton::kEventReleased) {
    int pin = button->getPin();
    for (int i = 0; i < numButtons; i++) {
      if (buttonPins[i] == pin) {
        buttonStates[i] = !buttonStates[i];
        buttonData.buttonID = i;
        buttonData.state = buttonStates[i];
        esp_now_send(slaveMAC, (uint8_t *)&buttonData, sizeof(buttonData));
        break;
      }
    }
  }
}

// Handle toggle requests from the web interface
void handleToggle() {
  if (server.hasArg("relay") && server.hasArg("state")) {
    int relay = server.arg("relay").toInt();
    bool state = server.arg("state").toInt();

    if (relay >= 0 && relay < 4) {
      buttonData.buttonID = relay;
      buttonData.state = state;
      buttonStates[relay] = state;
      esp_now_send(slaveMAC, (uint8_t *)&buttonData, sizeof(buttonData)); // Send toggle command to slave
    }
  }
  server.send(200, "text/html", getHTMLPage()); // Send updated HTML page
}

void handleTurnAllOff() {
  for (int i = 0; i < 4; i++) {
    buttonStates[i] = false;
    buttonData.buttonID = i;
    buttonData.state = false;
    esp_now_send(slaveMAC, (uint8_t *)&buttonData, sizeof(buttonData));
  }
  server.send(200, "text/html", getHTMLPage());
}

// Handle requests for the HTML root page
void handleRoot() {
  server.send(200, "text/html", getHTMLPage());
}

// Handle requests for relay states in JSON format
void handleRelayStates() {
  String json = "{ \"relays\": [";
  for (int i = 0; i < 4; i++) {
    json += feedbackData.relayStates[i] ? "1" : "0";
    if (i < 3) json += ",";
  }
  json += "], \"slaveConnected\": ";
  json += (slaveConnected ? "true" : "false");
  json += " }";

  server.send(200, "application/json", json);  // Send relay states as JSON
}

// Show the connecting animation on OLED display
void showConnectingAnimation() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  for (int i = 0; i < 3; i++) {
    display.setCursor(0, 0);
    display.println("Connecting");
    for (int j = 0; j <= i; j++) {
      display.print(".");
    }
    display.display();
    delay(500);
    display.clearDisplay();
  }
}

// Setup function, runs once when the ESP32 starts
void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true); // Stop execution if OLED initialization fails
  }
  display.setTextSize(1);
  display.setTextColor(WHITE);

  showConnectingAnimation(); // Show connecting animation

  pinMode(LED_BUILTIN, OUTPUT); // Set built-in LED as output
  
  // Initialize button pins and event handlers
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    buttons[i].setEventHandler(handleButtonPress);
  }
  ButtonConfig::getSystemButtonConfig()->setEventHandler(handleButtonPress);

  // Set up Wi-Fi as Access Point
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, password);
  IPAddress IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(IP, gateway, subnet);
  Serial.println("AP Started: 192.168.4.1  (Link: http://192.168.4.1)");
  
  // Set up web server routes
  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/relay_states", handleRelayStates);
  server.on("/turn_all_off", handleTurnAllOff);
  server.begin();  // Start the web server

  // Initialize ESP-NOW communication
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }
  esp_now_register_send_cb(onDataSent);  // Register callback for data sent
  esp_now_register_recv_cb(onDataRecv);  // Register callback for data received

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Waiting feedback...");
  display.display();
}

// Main loop function, runs repeatedly
void loop() {
  server.handleClient();  // Handle incoming web server requests

  for (int i = 0; i < numButtons; i++) {
    buttons[i].check();  // Check for button press events
  }

  if (millis() - lastFeedbackTime >= feedbackTimeout) {
    digitalWrite(LED_BUILTIN, LOW);  // Turn off LED if no feedback
    slaveConnected = false;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("No feedback!\nCheck Slave.");
    display.display();
  } else {
    digitalWrite(LED_BUILTIN, HIGH); // Turn on LED if feedback received
    slaveConnected = true;
  }
}
