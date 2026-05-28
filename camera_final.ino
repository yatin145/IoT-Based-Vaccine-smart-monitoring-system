#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ================= WIFI =================
const char* ssid = "Net";
const char* password = "Yatindas";

// ================= TELEGRAM =================
const char* BOT_TOKEN = "7868908930:AAGc2GqFGZmkNgSnH3dYOaZ0EK7R4vzhNYU";
const char* CHAT_ID  = "1958225824";

// ================= GPIO =================
#define TRIGGER_PIN 13   // GPIO that triggers image
#define FLASH_PIN 4

// ============ AI THINKER ESP32-CAM PINS ============
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

bool lastState = LOW;

// =================================================
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32-CAM Telegram Trigger");
  pinMode(FLASH_PIN, OUTPUT);
digitalWrite(FLASH_PIN, LOW);  // flash OFF initially


  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);

  // -------- CAMERA CONFIG --------
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;
  config.jpeg_quality = 12;
  config.fb_count     = 1;

  // Init camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }

  // -------- WIFI --------
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

// =================================================
void loop() {
  bool currentState = digitalRead(TRIGGER_PIN);

  if (currentState == HIGH && lastState == LOW) {
    Serial.println("GPIO HIGH detected → Capturing image");
    sendPhotoTelegram();
    delay(2000); // debounce
  }

  lastState = currentState;
}

// =================================================
void sendPhotoTelegram() {

  digitalWrite(FLASH_PIN, HIGH);   // Flash ON
  //delay(500);                      // small delay for brightness

  camera_fb_t * fb = esp_camera_fb_get();

  

  //camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  String boundary = "----ESP32CAM";
  String head =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    String(CHAT_ID) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"image.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  uint32_t contentLength = head.length() + fb->len + tail.length();

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Telegram connection failed");
    esp_camera_fb_return(fb);
    return;
  }
  digitalWrite(FLASH_PIN, LOW);    // Flash OFF after capture
  client.println("POST /bot" + String(BOT_TOKEN) + "/sendPhoto HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(contentLength));
  client.println();
  client.print(head);
  client.write(fb->buf, fb->len);
  client.print(tail);

  esp_camera_fb_return(fb);
  Serial.println("Image sent to Telegram");
}