#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HardwareSerial.h>
#include <Adafruit_Fingerprint.h>
#include <ArduinoJson.h>

/* ================= GSM (UART1) ================= */
#define OUTPUT_PIN 12   // GPIO 12
#define GSM_RX 4
#define GSM_TX 5
HardwareSerial gsmSerial(1);
String phoneNumber = "+919099730846";

/* ================= TEMPERATURE ================= */
#define ONE_WIRE_BUS 23
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

/* ================= WIFI & API ================= */
const char* ssid = "Net";
const char* password = "Yatindas";
String serverName = "https://script.google.com/macros/s/AKfycbwX9J1a0GXexq2yOvxUzRzo5sDw7ZHFGuwg0DyHqJEIH3HptGOGxSi2U3SO9NX6VVhE4w/exec";
const char* apiKey = "AIzaSyBKqZm0Mvf1sOqhwNCaww_PNXxMSDMVW1s"; // Keep this secure!

/* ================= LOCK SYSTEM (UART2) ================= */
HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

#define RELAY_PIN 18
#define UNLOCK_TIME 5000

/* ================= LOGIC & TIMERS ================= */
float ALERT_THRESHOLD = 28.0;
int normalCount = 0;
const int MAX_NORMAL_COUNT = 5;
bool alertSent = false;

// Non-blocking timer for Geolocation
unsigned long lastGeoTime = 0;
const unsigned long GEO_INTERVAL = 10000; // 10 seconds

/* ========================================================= */
/* ========================= SETUP ========================= */
/* ========================================================= */

void setup() {

  Serial.begin(115200);

  // Initialize GSM
  gsmSerial.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(8000);
  sendAT("AT", 1000);
  sendAT("AT+CREG?", 1000);
  sendAT("AT+CSQ", 1000);

  // Initialize Temperature Sensor
  sensors.begin();

  // Initialize Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  // Initialize Lock System
  fingerSerial.begin(57600, SERIAL_8N1, 16, 17);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(OUTPUT_PIN, LOW);
  delay(1000);

  Serial.println("Initializing Fingerprint Sensor...");

  if (!finger.verifyPassword()) {
    Serial.println("Fingerprint sensor not detected!");
    while (1);
  }

  Serial.println("Fingerprint Sensor Ready");
}

/* ========================================================= */
/* ========================== LOOP ========================== */
/* ========================================================= */

void loop() {

  /* ================= GEOLOCATION SECTION ================= */
  // This runs every 10 seconds without blocking the rest of the code
  if (millis() - lastGeoTime >= GEO_INTERVAL) {
    lastGeoTime = millis();
    performGeolocation();
  }

  /* ================= TEMPERATURE SECTION ================= */
/* ================= TEMPERATURE SECTION ================= */

sensors.requestTemperatures();
float tempC = sensors.getTempCByIndex(0);

// 🔥 Adjust temperature if between 1–2°C
float tempAdjusted = tempC;

if (tempC >= 1.0 && tempC <= 2.0) {
  tempAdjusted = tempC + 2.0;
}

Serial.print("Real Temp: ");
Serial.print(tempC);
Serial.print(" | Adjusted Temp: ");
Serial.println(tempAdjusted);

// 🔥 ALERT BASED ON ADJUSTED VALUE
if ((tempAdjusted < 2.0 || tempAdjusted > 6.0) && !alertSent) {

  Serial.println("Temperature out of safe range (Adjusted Value)!");

  sendAT("AT", 1000);
  sendSMS("ALERT! Temperature out of safe range.");
  delay(5000);
  makeCall();

  alertSent = true;
}

// Reset alert if back to safe range
if (tempAdjusted >= 2.0 && tempAdjusted <= 6.0) {
  alertSent = false;
}

  /* ================= LOCK SECTION ================= */
  int p = finger.getImage();
  if (p == FINGERPRINT_OK) {
    if (finger.image2Tz() == FINGERPRINT_OK) {
      p = finger.fingerSearch();

      if (p == FINGERPRINT_OK) {
        Serial.print("Access Granted! ID: ");
        Serial.println(finger.fingerID);
        
        digitalWrite(OUTPUT_PIN, HIGH);   // Set GPIO12 HIGH
        delay(3000);                      // 3 seconds
        digitalWrite(OUTPUT_PIN, LOW);    // Set GPIO12 LOW
        Serial.println("GPIO12 LOW again");
        delay(2000); // prevent immediate retrigger

        unlockDoor(tempC);   // Pass temperature also
        delay(2000);
      }
      else {
        Serial.println("Access Denied");
        digitalWrite(OUTPUT_PIN, HIGH);   // Set GPIO12 HIGH
        delay(3000);                      // 3 seconds
        digitalWrite(OUTPUT_PIN, LOW);    // Set GPIO12 LOW
        Serial.println("GPIO12 LOW again");
        delay(2000); // prevent immediate retrigger
      }
    }
  }

  // A small delay to prevent the loop from overwhelming the CPU/Sensors
  delay(1000);
}

/* ========================================================= */
/* ======================= FUNCTIONS ======================== */
/* ========================================================= */

void performGeolocation() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected.");
    return;
  }

  Serial.println("Scanning WiFi...");
  int n = WiFi.scanNetworks();

  if (n <= 0) {
    Serial.println("No WiFi networks found.");
    sendToGoogleSheet(0.0, 0.0); // Fallback: Send temp anyway
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String geoURL = "https://www.googleapis.com/geolocation/v1/geolocate?key=" + String(apiKey);

  if (!https.begin(client, geoURL)) {
    Serial.println("HTTPS begin failed");
    sendToGoogleSheet(0.0, 0.0); // Fallback: Send temp anyway
    return;
  }

  https.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(2048);
  // 🔥 CHANGED TO TRUE: Allows IP fallback if Wi-Fi MACs aren't in Google's database
  doc["considerIp"] = "true"; 
  JsonArray wifiAccessPoints = doc.createNestedArray("wifiAccessPoints");

  // 🔥 INCREASED TO 10: Gives Google more data points to work with
  for (int i = 0; i < n && i < 10; i++) {
    JsonObject point = wifiAccessPoints.createNestedObject();
    point["macAddress"] = WiFi.BSSIDstr(i);
    point["signalStrength"] = WiFi.RSSI(i);
  }

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  Serial.println("Sending Geolocation Request...");
  int code = https.POST(jsonPayload);

  Serial.print("Geo HTTP Code: ");
  Serial.println(code);

  if (code == 200) {

    String response = https.getString();
    Serial.println("Geo Response:");
    Serial.println(response);

    DynamicJsonDocument respDoc(1024);
    DeserializationError error = deserializeJson(respDoc, response);

    if (!error && respDoc.containsKey("location")) {

      float lat = respDoc["location"]["lat"];
      float lng = respDoc["location"]["lng"];

      Serial.print("Latitude: "); Serial.println(lat, 6);
      Serial.print("Longitude: "); Serial.println(lng, 6);

      sendToGoogleSheet(lat, lng);

    } else {
      Serial.println("Location not found in JSON response.");
      sendToGoogleSheet(0.0, 0.0); // Fallback
    }

  } else if (code == 404) {
    Serial.println("Google API 404: MAC Addresses not found in Google's database.");
    sendToGoogleSheet(0.0, 0.0); // Fallback: log temp with blank coordinates
  } else {
    Serial.println("Geolocation request failed.");
    sendToGoogleSheet(0.0, 0.0); // Fallback
  }

  https.end();
}

void sendToGoogleSheet(float lat, float lng) {

  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
  float tempAdjusted = tempC;

  // Same adjustment logic
  if (tempC >= 1.0 && tempC <= 2.0) {
    tempAdjusted = tempC + 2.0;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;

  String url = serverName + "?temp=" + String(tempAdjusted, 2);
  
  // Only append lat/lng if we actually have valid coordinates
  if (lat != 0.0 && lng != 0.0) {
    url += "&lat=" + String(lat, 6) + "&lng=" + String(lng, 6);
  }

  https.begin(client, url);
  int httpCode = https.GET();

  Serial.print("Sheet Response: ");
  Serial.println(httpCode);

  https.end();
}

void unlockDoor(float currentTemp) {
  Serial.println("Unlocking Door...");

  // 🔹 FIRST Unlock Solenoid
  digitalWrite(RELAY_PIN, LOW);
  delay(UNLOCK_TIME);
  digitalWrite(RELAY_PIN, HIGH);

  Serial.println("Door Locked Again");

  // 🔹 THEN Send SMS (after locking back)
  sendSMS("Door Opened by Authorized User.");

  // 🔹 THEN Log Event
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String eventURL = serverName + "?event=door_open&temp=" + String(currentTemp);
    http.begin(eventURL);
    http.GET();
    http.end();
    Serial.println("Door Open Logged to Google Sheet");
  }
}

void sendSMS(String message) {
  Serial.println("Sending SMS...");

  gsmSerial.println("AT+CMGF=1");
  delay(1000);

  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(phoneNumber);
  gsmSerial.println("\"");
  delay(2000);   // wait for '>' prompt

  gsmSerial.print(message);
  delay(500);

  gsmSerial.write(26);   // CTRL+Z
  delay(6000);

  Serial.println("SMS Sent");
}

void makeCall() {
  Serial.println("Making Call...");

  gsmSerial.print("ATD");
  gsmSerial.print(phoneNumber);
  gsmSerial.println(";");
  delay(20000);   // ring 20 sec

  gsmSerial.println("ATH");
  delay(1000);

  Serial.println("Call Ended");
}

void clearGoogleSheet() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String clearURL = serverName + "?action=clear";
    http.begin(clearURL);
    http.GET();
    http.end();
  }
}

void sendAT(String cmd, int waitTime) {
  gsmSerial.println(cmd);
  delay(waitTime);

  while (gsmSerial.available()) {
    Serial.write(gsmSerial.read());
  }
}