/*
 * TRMNL 7.5" E-Ink MQTT Dashboard v2
 * 
 * Connects to WiFi and subscribes to an MQTT topic.
 * When a JSON message arrives, it renders a headline + body text
 * on the 800x480 e-ink display.
 * 
 * MQTT JSON format:
 * {
 *   "headline": "Your Headline Here",
 *   "body": "Your body text here. Can be multiple sentences.",
 *   "footer": "Optional footer text"
 * }
 * 
 * Hardware: TRMNL 7.5" (OG) DIY Kit (XIAO ESP32-S3 Plus)
 * Libraries: Seeed_GFX, PubSubClient, ArduinoJson
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============================================================
// CONFIGURATION - Edit these to match your setup
// ============================================================

const char* WIFI_SSID     = "";
const char* WIFI_PASSWORD = "";

const char* MQTT_SERVER   = "10.0.0.246";  // your Pi's IP
const int   MQTT_PORT     = 1883;
const char* MQTT_TOPIC    = "dashboard/epaper";

const char* MQTT_USER     = "";
const char* MQTT_PASS     = "";

// ============================================================
// DISPLAY LAYOUT
// ============================================================

const int SCREEN_W = 800;
const int SCREEN_H = 480;
const int MARGIN_LEFT   = 20;
const int MARGIN_RIGHT  = 20;
const int MARGIN_TOP    = 20;
const int HEADLINE_Y       = MARGIN_TOP + 34;  // offset by font ascent height (baseline positioning)
const int SEP_Y_OFFSET     = 18;       // gap below last headline line before separator
const int BODY_Y_OFFSET    = 38;       // gap below separator before body text (accounts for ascent)
const int FOOTER_Y         = SCREEN_H - 20;
const int CONTENT_W = SCREEN_W - MARGIN_LEFT - MARGIN_RIGHT;

// Font selections (FreeFont smooth fonts)
// Available bold sans: FreeSansBold9pt7b, 12pt, 18pt, 24pt
// Available regular sans: FreeSans9pt7b, 12pt, 18pt, 24pt
#define HEADLINE_FONT  &FreeSansBold24pt7b   // large bold headline
#define BODY_FONT      &FreeSans18pt7b       // readable body text
#define FOOTER_FONT    &FreeSans12pt7b       // smaller footer

const int HEADLINE_LINE_H = 42;   // line spacing for 24pt bold
const int BODY_LINE_H     = 32;   // line spacing for 18pt
const int FOOTER_LINE_H   = 22;   // line spacing for 12pt

// ============================================================
// GLOBALS
// ============================================================

EPaper epaper = EPaper();

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

char currentHeadline[256] = "";
char currentBody[1024]    = "";
char currentFooter[128]   = "";
bool newDataAvailable      = false;
bool displayReady          = false;

// ============================================================
// WORD-WRAP TEXT DRAWING (FreeFont / proportional)
// ============================================================

// Draws text with word-wrap using the currently set FreeFont.
// Uses textWidth() for accurate proportional character measurement.
// Returns the Y position after the last line drawn.
int drawWrappedText(const char* text, int x, int y, int maxWidth, const GFXfont* font, int lineHeight) {
  epaper.setFreeFont(font);
  epaper.setTextSize(1);  // must be 1 when using FreeFonts

  int len = strlen(text);
  int pos = 0;
  int currentY = y;

  while (pos < len) {
    // Build a line word by word, measuring pixel width as we go
    int lineStart = pos;
    int lastGoodBreak = pos;  // last position where we can break

    while (pos < len) {
      // Find end of next word
      int wordEnd = pos;
      while (wordEnd < len && text[wordEnd] != ' ' && text[wordEnd] != '\n') {
        wordEnd++;
      }

      // Measure width from lineStart to wordEnd
      char testBuf[256];
      int testLen = wordEnd - lineStart;
      if (testLen > 255) testLen = 255;
      strncpy(testBuf, &text[lineStart], testLen);
      testBuf[testLen] = '\0';

      int pixelWidth = epaper.textWidth(testBuf);

      if (pixelWidth > maxWidth && lastGoodBreak > lineStart) {
        // This word overflows — break at the last good position
        break;
      }

      lastGoodBreak = wordEnd;

      if (wordEnd < len && text[wordEnd] == '\n') {
        lastGoodBreak = wordEnd;
        break;
      }

      // Skip the space after the word
      pos = wordEnd;
      if (pos < len && text[pos] == ' ') {
        pos++;
      }
    }

    // Print the line from lineStart to lastGoodBreak
    char lineBuf[256];
    int lineLen = lastGoodBreak - lineStart;
    if (lineLen > 255) lineLen = 255;
    if (lineLen <= 0) {
      // Safety: skip a character to avoid infinite loop
      pos = lineStart + 1;
      continue;
    }
    strncpy(lineBuf, &text[lineStart], lineLen);
    lineBuf[lineLen] = '\0';

    epaper.setCursor(x, currentY);
    epaper.print(lineBuf);
    currentY += lineHeight;

    // Advance past the break point
    pos = lastGoodBreak;
    if (pos < len && (text[pos] == ' ' || text[pos] == '\n')) {
      pos++;
    }

    // Don't draw past the screen
    if (currentY > SCREEN_H - 40) break;
  }

  return currentY;
}

// ============================================================
// RENDER
// ============================================================

void renderDashboard() {
  Serial.println("[DISPLAY] renderDashboard() called");

  if (!displayReady) {
    Serial.println("[DISPLAY] Display not ready, skipping.");
    return;
  }

  epaper.fillScreen(TFT_WHITE);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);

  int cursorY = HEADLINE_Y;

  // Headline
  if (strlen(currentHeadline) > 0) {
    Serial.print("[DISPLAY] Drawing headline: ");
    Serial.println(currentHeadline);
    cursorY = drawWrappedText(currentHeadline, MARGIN_LEFT, cursorY, CONTENT_W, HEADLINE_FONT, HEADLINE_LINE_H);
    cursorY += SEP_Y_OFFSET;
  }

  // Separator
  epaper.drawLine(MARGIN_LEFT, cursorY, SCREEN_W - MARGIN_RIGHT, cursorY, TFT_BLACK);
  epaper.drawLine(MARGIN_LEFT, cursorY + 1, SCREEN_W - MARGIN_RIGHT, cursorY + 1, TFT_BLACK);
  cursorY += BODY_Y_OFFSET;

  // Body
  if (strlen(currentBody) > 0) {
    Serial.println("[DISPLAY] Drawing body text...");
    cursorY = drawWrappedText(currentBody, MARGIN_LEFT, cursorY, CONTENT_W, BODY_FONT, BODY_LINE_H);
  }

  // Footer
  if (strlen(currentFooter) > 0) {
    epaper.setFreeFont(FOOTER_FONT);
    epaper.setTextSize(1);
    epaper.setCursor(MARGIN_LEFT, FOOTER_Y);
    epaper.print(currentFooter);
  }

  // Push to display
  Serial.println("[DISPLAY] Calling epaper.update()...");
  delay(100);
  epaper.update();
  Serial.println("[DISPLAY] Screen updated successfully!");
}

// ============================================================
// MQTT CALLBACK
// ============================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("[MQTT] Message on topic: ");
  Serial.println(topic);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("[MQTT] JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  const char* headline = doc["headline"] | "";
  const char* body     = doc["body"]     | "";
  const char* footer   = doc["footer"]   | "";

  strncpy(currentHeadline, headline, sizeof(currentHeadline) - 1);
  currentHeadline[sizeof(currentHeadline) - 1] = '\0';

  strncpy(currentBody, body, sizeof(currentBody) - 1);
  currentBody[sizeof(currentBody) - 1] = '\0';

  strncpy(currentFooter, footer, sizeof(currentFooter) - 1);
  currentFooter[sizeof(currentFooter) - 1] = '\0';

  Serial.print("[MQTT] Headline: ");
  Serial.println(currentHeadline);
  Serial.print("[MQTT] Body length: ");
  Serial.println(strlen(currentBody));

  newDataAvailable = true;
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi() {
  Serial.print("[WIFI] Connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("[WIFI] Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("[WIFI] Failed to connect!");
  }
}

// ============================================================
// MQTT
// ============================================================

void connectMQTT() {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(2048);

  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Connecting to broker...");

    String clientId = "TRMNL-" + String(WiFi.macAddress());
    bool connected;

    if (strlen(MQTT_USER) > 0) {
      connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    } else {
      connected = mqttClient.connect(clientId.c_str());
    }

    if (connected) {
      Serial.println(" connected!");
      mqttClient.subscribe(MQTT_TOPIC);
      Serial.print("[MQTT] Subscribed to: ");
      Serial.println(MQTT_TOPIC);
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" - retrying in 5 seconds");
      delay(5000);
    }
  }
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("================================");
  Serial.println("TRMNL E-Ink MQTT Dashboard v2");
  Serial.println("================================");

  // Step 1: Connect WiFi first
  connectWiFi();

  // Step 2: Connect MQTT
  connectMQTT();

  // Step 3: Init display AFTER WiFi/MQTT are up
  Serial.println("[DISPLAY] Initializing ePaper...");
  epaper.begin();
  epaper.setRotation(0);
  displayReady = true;
  Serial.println("[DISPLAY] ePaper initialized.");

  // Step 4: Show ready screen
  epaper.fillScreen(TFT_WHITE);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);

  epaper.setFreeFont(HEADLINE_FONT);
  epaper.setTextSize(1);
  epaper.setCursor(MARGIN_LEFT, 50);
  epaper.println("MQTT Dashboard Ready");

  epaper.setFreeFont(BODY_FONT);
  epaper.setTextSize(1);
  epaper.setCursor(MARGIN_LEFT, 110);
  epaper.print("WiFi: ");
  epaper.println(WiFi.localIP().toString());

  epaper.setCursor(MARGIN_LEFT, 150);
  epaper.print("Broker: ");
  epaper.println(MQTT_SERVER);

  epaper.setCursor(MARGIN_LEFT, 190);
  epaper.print("Topic: ");
  epaper.println(MQTT_TOPIC);

  epaper.setCursor(MARGIN_LEFT, 260);
  epaper.println("Waiting for messages...");

  Serial.println("[DISPLAY] Calling epaper.update() for ready screen...");
  delay(100);
  epaper.update();
  Serial.println("[DISPLAY] Ready screen displayed.");
}

// ============================================================
// LOOP
// ============================================================

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Connection lost, reconnecting...");
    connectWiFi();
  }

  if (!mqttClient.connected()) {
    Serial.println("[MQTT] Connection lost, reconnecting...");
    connectMQTT();
  }

  mqttClient.loop();

  if (newDataAvailable) {
    Serial.println("[LOOP] New data detected, updating display...");
    newDataAvailable = false;
    renderDashboard();
  }
}
