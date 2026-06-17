#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ── OLED ──────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── RFID ──────────────────────────────────────────────────────────────────
#define SS_PIN  5
#define RST_PIN 4
MFRC522 rfid(SS_PIN, RST_PIN);

// ── Outputs ───────────────────────────────────────────────────────────────
#define GREEN_LED 27
#define RED_LED   25
#define BUZZER    26

// ── Credentials ───────────────────────────────────────────────────────────
const char* ssid     = "POCOF4";
const char* password = "12345678";

const String GOOGLE_SCRIPT_URL =
  "https://script.google.com/macros/s/AKfycbxCWuOx1yX89-TrEjUHaSmu-Qa1waJNEiu1wfQsDW7MIMGvI8TQWBzM51vCto5ne2rQIw/exec";

// ── Cards ─────────────────────────────────────────────────────────────────
byte rohitUID[4] = {0xED, 0x99, 0x31, 0x07};
byte rahulUID[4] = {0xB1, 0xE0, 0xAB, 0x16};

// ── State ─────────────────────────────────────────────────────────────────
bool rohitInside = false;
bool rahulInside = false;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

void beep() {
  digitalWrite(BUZZER, HIGH);
  delay(150);
  digitalWrite(BUZZER, LOW);
}

bool compareUID(byte *uid1, byte *uid2) {
  for (int i = 0; i < 4; i++)
    if (uid1[i] != uid2[i]) return false;
  return true;
}

String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "";
  char buf[24];
  strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &timeinfo);
  return String(buf);
}

// ── Safe screen transition — wipes every pixel before next draw ───────────
void clearScreen() {
  display.clearDisplay();
  display.display();
  delay(50);
}

// ═══════════════════════════════════════════════════════════════════════════
//  GOOGLE SHEETS UPLOAD
//  NOTE: No fillRect() / drawLine() used here — status text only
// ═══════════════════════════════════════════════════════════════════════════

void sendToGoogleSheet(String name, String status, String date, String timeStr) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Sheet] WiFi not connected — skipping upload");
    return;
  }

  // ── Show uploading status — full clear first, then text only ─────
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ATTENDANCE");
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.println(name);
  display.setTextSize(1);
  display.setCursor(0, 34);
  display.println(status);
  display.setCursor(0, 50);
  display.println("Uploading...");
  display.display();

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;

  String url = GOOGLE_SCRIPT_URL
             + "?name="   + name
             + "&status=" + status
             + "&date="   + date
             + "&time="   + timeStr;

  Serial.println("[Sheet] URL: " + url);

  https.begin(client, url);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = https.GET();

  // ── After response — full clear, then redraw with result ─────────
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ATTENDANCE");
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.println(name);
  display.setTextSize(1);
  display.setCursor(0, 34);
  display.println(status);

  if (httpCode > 0) {
    String payload = https.getString();
    Serial.println("[Sheet] HTTP " + String(httpCode) + " -> " + payload);
    display.setCursor(0, 50);
    display.println("Sheet: OK");
  } else {
    Serial.println("[Sheet] Upload failed: " + https.errorToString(httpCode));
    display.setCursor(0, 50);
    display.println("Sheet: FAILED");
  }

  display.display();
  https.end();
}

// ═══════════════════════════════════════════════════════════════════════════
//  DISPLAY FUNCTIONS
//  Every function: clearDisplay() first → draw → display() last
//  No fillRect(), drawLine(), drawFastHLine(), drawRect() anywhere
// ═══════════════════════════════════════════════════════════════════════════

void showReady() {
  display.clearDisplay();                    // wipe entire buffer
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("RFID ATTENDANCE");

  display.setCursor(0, 18);
  display.println("SCAN CARD");

  String ts = getTimestamp();
  if (ts.length() >= 19) {
    display.setCursor(0, 40);
    display.println(ts.substring(0, 10));   // DD/MM/YYYY
    display.setCursor(0, 52);
    display.println(ts.substring(11));      // HH:MM:SS
  }

  display.display();                         // push buffer to screen
}

void showAttendance(String name, String status, String timestamp) {
  display.clearDisplay();                    // wipe entire buffer first
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("ATTENDANCE");

  display.setTextSize(2);
  display.setCursor(0, 12);
  display.println(name);

  display.setTextSize(1);
  display.setCursor(0, 34);
  display.println(status);

  if (timestamp.length() >= 19) {
    display.setCursor(0, 46);
    display.println(timestamp.substring(0, 10));  // DD/MM/YYYY
    display.setCursor(0, 56);
    display.println(timestamp.substring(11));     // HH:MM:SS
  }

  display.display();                         // push buffer to screen
}

void showUnknown(String datePart, String timePart) {
  display.clearDisplay();                    // wipe entire buffer first
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 8);
  display.println("UNKNOWN CARD");

  display.setCursor(0, 26);
  display.println("ACCESS DENIED");

  display.setCursor(0, 44);
  display.println(datePart);

  display.setCursor(0, 54);
  display.println(timePart);

  display.display();                         // push buffer to screen
}

// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED,   OUTPUT);
  pinMode(BUZZER,    OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED,   LOW);
  digitalWrite(BUZZER,    LOW);

  Wire.begin(21, 22);

  // ── OLED init ────────────────────────────────────────────────────
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed");
    while (1);
  }
  // Clear Adafruit splash screen immediately
  display.clearDisplay();
  display.display();

  // ── WiFi ─────────────────────────────────────────────────────────
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected — IP: " + WiFi.localIP().toString());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.setCursor(0, 16);
  display.println(WiFi.localIP().toString());
  display.display();
  delay(1000);

  // ── NTP ──────────────────────────────────────────────────────────
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Syncing time...");
  display.display();

  struct tm timeinfo;
  int attempts = 0;
  while (attempts < 20) {
    if (getLocalTime(&timeinfo) && timeinfo.tm_year > 120) break;
    delay(500);
    attempts++;
  }

  display.clearDisplay();
  if (timeinfo.tm_year > 120) {
    String ts = getTimestamp();
    Serial.println("Time synced: " + ts);
    display.setCursor(0, 0);
    display.println("Time Synced!");
    display.setCursor(0, 20);
    display.println(ts.substring(0, 10));
    display.setCursor(0, 35);
    display.println(ts.substring(11));
  } else {
    Serial.println("Time sync FAILED");
    display.setCursor(0, 0);
    display.println("Time sync FAILED");
    display.setCursor(0, 16);
    display.println("Check internet!");
  }
  display.display();
  delay(2000);

  // ── RFID ─────────────────────────────────────────────────────────
  SPI.begin();
  rfid.PCD_Init();

  Serial.println("System Ready");
  clearScreen();   // clean slate before first showReady()
  showReady();
}

// ═══════════════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  // Refresh clock every 1 second
  static unsigned long lastClockUpdate = 0;
  if (millis() - lastClockUpdate > 1000) {
    showReady();
    lastClockUpdate = millis();
  }

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial())   return;

  Serial.print("UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.printf("%02X ", rfid.uid.uidByte[i]);
  }
  Serial.println();

  beep();

  String ts = getTimestamp();
  if (ts.length() == 0) ts = "00/00/0000 00:00:00";
  String datePart = ts.substring(0, 10);
  String timePart = ts.substring(11);

  // ── Rohit ────────────────────────────────────────────────────────
  if (compareUID(rfid.uid.uidByte, rohitUID)) {
    if (!rohitInside) {
      rohitInside = true;
      digitalWrite(GREEN_LED, HIGH);
      showAttendance("Rohit", "ENTRY", ts);
      Serial.println("Rohit ENTRY  | " + ts);
      sendToGoogleSheet("Rohit", "ENTRY", datePart, timePart);
      delay(2000);
      digitalWrite(GREEN_LED, LOW);
    } else {
      rohitInside = false;
      digitalWrite(RED_LED, HIGH);
      showAttendance("Rohit", "EXIT", ts);
      Serial.println("Rohit EXIT   | " + ts);
      sendToGoogleSheet("Rohit", "EXIT", datePart, timePart);
      delay(2000);
      digitalWrite(RED_LED, LOW);
    }
  }

  // ── Rahul ────────────────────────────────────────────────────────
  else if (compareUID(rfid.uid.uidByte, rahulUID)) {
    if (!rahulInside) {
      rahulInside = true;
      digitalWrite(GREEN_LED, HIGH);
      showAttendance("Rahul", "ENTRY", ts);
      Serial.println("Rahul ENTRY  | " + ts);
      sendToGoogleSheet("Rahul", "ENTRY", datePart, timePart);
      delay(2000);
      digitalWrite(GREEN_LED, LOW);
    } else {
      rahulInside = false;
      digitalWrite(RED_LED, HIGH);
      showAttendance("Rahul", "EXIT", ts);
      Serial.println("Rahul EXIT   | " + ts);
      sendToGoogleSheet("Rahul", "EXIT", datePart, timePart);
      delay(2000);
      digitalWrite(RED_LED, LOW);
    }
  }

  // ── Unknown card ─────────────────────────────────────────────────
  else {
    showUnknown(datePart, timePart);
    Serial.println("Unknown Card | " + ts);
    delay(2000);
  }

  // ── Clean transition back to ready screen ────────────────────────
  clearScreen();   // wipe attendance screen fully before ready screen
  showReady();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}