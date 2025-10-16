// SIM card PIN (leave empty, if not defined)
const char simPIN[]   = "";

// Your phone number to send SMS: + (plus sign) and country code, for Portugal +351, followed by phone number
// SMS_TARGET Example for Portugal +351XXXXXXXXX
#define SMS_TARGET  "+918908945726"

// Define your temperature Threshold (in this case it's 28.0 degrees Celsius)
float temperatureThreshold = 28.0;
// Flag variable to keep track if alert SMS was sent or not
bool smsSent = false;

// Configure TinyGSM library
#define TINY_GSM_MODEM_SIM800      // Modem is SIM800
#define TINY_GSM_RX_BUFFER   1024  // Set RX buffer to 1Kb

#include <Wire.h>
#include <TinyGsmClient.h>
#include <DHT.h>

// DHT22 Pin Setup
#define DHTPIN 13          // Pin connected to DHT22 sensor data pin
#define DHTTYPE DHT22      // Define the sensor type as DHT22

DHT dht(DHTPIN, DHTTYPE);    // Initialize DHT sensor

// TTGO T-Call pins
#define MODEM_RST            5
#define MODEM_PWKEY          4
#define MODEM_POWER_ON       23
#define MODEM_TX             27
#define MODEM_RX             26
#define I2C_SDA              21
#define I2C_SCL              22

// Set serial for debug console (to Serial Monitor, default speed 115200)
#define SerialMon Serial
// Set serial for AT commands (to SIM800 module)
#define SerialAT  Serial1

// Define the serial console for debug prints, if needed
//#define DUMP_AT_COMMANDS

#ifdef DUMP_AT_COMMANDS
  #include <StreamDebugger.h>
  StreamDebugger debugger(SerialAT, SerialMon);
  TinyGsm modem(debugger);
#else
  TinyGsm modem(SerialAT);
#endif

#define IP5306_ADDR          0x75
#define IP5306_REG_SYS_CTL0  0x00

bool setPowerBoostKeepOn(int en){
  Wire.beginTransmission(IP5306_ADDR);
  Wire.write(IP5306_REG_SYS_CTL0);
  if (en) {
    Wire.write(0x37); // Set bit1: 1 enable 0 disable boost keep on
  } else {
    Wire.write(0x35); // 0x37 is default reg value
  }
  return Wire.endTransmission() == 0;
}

void setup() {
  // Set console baud rate
  SerialMon.begin(115200);

  // Keep power when running from battery
  Wire.begin(I2C_SDA, I2C_SCL);
  bool isOk = setPowerBoostKeepOn(1);
  SerialMon.println(String("IP5306 KeepOn ") + (isOk ? "OK" : "FAIL"));

  // Set modem reset, enable, power pins
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER_ON, OUTPUT);
  digitalWrite(MODEM_PWKEY, LOW);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_POWER_ON, HIGH);

  // Set GSM module baud rate and UART pins
  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  // Restart SIM800 module, it takes quite some time
  // To skip it, call init() instead of restart()
  SerialMon.println("Initializing modem...");
  modem.restart();
  // use modem.init() if you don't need the complete restart

  // Unlock your SIM card with a PIN if needed
  if (strlen(simPIN) && modem.getSimStatus() != 3 ) {
    modem.simUnlock(simPIN);
  }
  
  // Start the DHT22 sensor
  dht.begin();
}

void loop() {
  // Read temperature and humidity from the DHT22 sensor
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Check if any reading failed and return if so
  if (isnan(temperature) || isnan(humidity)) {
    SerialMon.println("Failed to read from DHT sensor!");
    return;
  }

  SerialMon.print("Temperature: ");
  SerialMon.print(temperature);
  SerialMon.print("*C, Humidity: ");
  SerialMon.print(humidity);
  SerialMon.println("%");

  // Check if temperature is above threshold and if it needs to send the SMS alert
  if((temperature > temperatureThreshold) && !smsSent){
    String smsMessage = String("Temperature above threshold: ") + 
           String(temperature) + String("C, Humidity: ") + String(humidity) + String("%");
    if(modem.sendSMS(SMS_TARGET, smsMessage)){
      SerialMon.println(smsMessage);
      smsSent = true;
    }
    else{
      SerialMon.println("SMS failed to send");
    }    
  }
  // Check if temperature is below threshold and if it needs to send the SMS alert
  else if((temperature < temperatureThreshold) && smsSent){
    String smsMessage = String("Temperature below threshold: ") + 
           String(temperature) + String("C, Humidity: ") + String(humidity) + String("%");
    if(modem.sendSMS(SMS_TARGET, smsMessage)){
      SerialMon.println(smsMessage);
      smsSent = false;
    }
    else{
      SerialMon.println("SMS failed to send");
    }
  }

  // Wait 5 seconds before reading again
  delay(5000);
}
