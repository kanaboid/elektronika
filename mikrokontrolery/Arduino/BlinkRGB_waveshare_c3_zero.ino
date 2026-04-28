/*
  BlinkRGB

  Demonstrates usage of onboard RGB LED on some ESP dev boards.

  Calling digitalWrite(RGB_BUILTIN, HIGH) will use hidden RGB driver.
    
  RGBLedWrite demonstrates controll of each channel:
  void neopixelWrite(uint8_t pin, uint8_t red_val, uint8_t green_val, uint8_t blue_val)

  WARNING: After using digitalWrite to drive RGB LED it will be impossible to drive the same pin
    with normal HIGH/LOW level
*/

#include <WiFi.h>

#define WIFI_SSID "Rrr a52s"
#define WIFI_PASSWORD "1234554321"
#define CONTROL_PIN 6
#define RGB_BRIGHTNESS 10 // Change white brightness (max 255)

WiFiServer server(80);

#define STATUS_INTERVAL 5000

struct LedStep {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  unsigned long duration;
};

const LedStep ledSequence[] = {
  {RGB_BRIGHTNESS, RGB_BRIGHTNESS, RGB_BRIGHTNESS, 1000},
  {0, 0, 0, 1000},
  {RGB_BRIGHTNESS, 0, 0, 1000},
  {0, RGB_BRIGHTNESS, 0, 3000},
  {0, 0, RGB_BRIGHTNESS, 1000},
  {0, 0, 0, 1000},
};

const int LED_STEP_COUNT = sizeof(ledSequence) / sizeof(ledSequence[0]);
int currentLedStep = 0;
unsigned long ledStepStart = 0;
unsigned long nextStatusTime = 0;

// the setup function runs once when you press reset or power the board
#ifdef RGB_BUILTIN
#undef RGB_BUILTIN
#endif
#define RGB_BUILTIN 38

void handleClient() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  String request = client.readStringUntil('\r');
  client.readStringUntil('\n'); // skip rest of request line

  if (request.indexOf("GET /pin6/on") >= 0) {
    digitalWrite(CONTROL_PIN, HIGH);
    Serial.println("Web request: PIN 6 ON");
  } else if (request.indexOf("GET /pin6/off") >= 0) {
    digitalWrite(CONTROL_PIN, LOW);
    Serial.println("Web request: PIN 6 OFF");
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println("Connection: close");
  client.println();
  client.println("OK");
  client.stop();
}

void updateLed() {
  unsigned long now = millis();
  if (now - ledStepStart >= ledSequence[currentLedStep].duration) {
    currentLedStep = (currentLedStep + 1) % LED_STEP_COUNT;
    ledStepStart = now;
    neopixelWrite(RGB_BUILTIN,
                 ledSequence[currentLedStep].r,
                 ledSequence[currentLedStep].g,
                 ledSequence[currentLedStep].b);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // czekaj na port szeregowy (opcjonalne na niektórych płytkach)
  }

  Serial.println();
  Serial.println("BlinkRGB WiFi status monitor");

  pinMode(CONTROL_PIN, OUTPUT);
  digitalWrite(CONTROL_PIN, LOW);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Laczenie z siecia WiFi: ");
  Serial.println(WIFI_SSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 15000) {
      Serial.println();
      Serial.println("Nie udalo sie polaczyc z WiFi, proba ponownie...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      start = millis();
    }
  }

  Serial.println();
  Serial.println("Polaczono z WiFi");
  Serial.print("Adres IP: ");
  Serial.println(WiFi.localIP());
  server.begin();
  Serial.println("HTTP server uruchomiony. Uzyj /pin6/on lub /pin6/off");

  ledStepStart = millis();
  neopixelWrite(RGB_BUILTIN,
               ledSequence[currentLedStep].r,
               ledSequence[currentLedStep].g,
               ledSequence[currentLedStep].b);
  nextStatusTime = millis() + STATUS_INTERVAL;
}

// the loop function runs over and over again forever
void loop() {
#ifdef RGB_BUILTIN
  handleClient();
  updateLed();

  unsigned long now = millis();
  if (now >= nextStatusTime) {
    nextStatusTime = now + STATUS_INTERVAL;
    Serial.print("Pin 6: ");
    Serial.println(digitalRead(CONTROL_PIN) == HIGH ? "HIGH" : "LOW");
    Serial.print("WiFi status: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED");
  }
#endif
}
