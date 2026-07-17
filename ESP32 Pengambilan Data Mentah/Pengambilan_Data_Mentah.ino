/*
 * =====================================================
 *  ESP32 PIR Sensor → Google Spreadsheet (Real-Time)
 * =====================================================
 * 
 * Komponen yang dibutuhkan:
 *  - ESP32 Development Board
 *  - Sensor PIR (HC-SR501 atau sejenisnya)
 *  - Kabel jumper
 * 
 * Wiring:
 *  PIR VCC  → ESP32 3.3V atau 5V
 *  PIR GND  → ESP32 GND
 *  PIR OUT  → ESP32 GPIO 14 (bisa diubah)
 * 
 * Library yang diperlukan (install via Library Manager):
 *  - WiFi (built-in ESP32)
 *  - HTTPClient (built-in ESP32)
 *  - WiFiClientSecure (built-in ESP32)
 *  - ArduinoJson (versi 6.x) oleh Benoit Blanchon
 *  - NTPClient oleh Fabrice Weinberg
 *  - WiFiUdp (built-in ESP32)
 * 
 * Cara penggunaan:
 *  1. Isi WIFI_SSID dan WIFI_PASSWORD
 *  2. Isi GOOGLE_SCRIPT_ID dari deployment Google Apps Script
 *  3. Upload ke ESP32
 *  4. Buka Serial Monitor (115200 baud) untuk melihat log
 * =====================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// =====================================================
//  KONFIGURASI - UBAH SESUAI KEBUTUHAN
// =====================================================

// Konfigurasi WiFi
const char* WIFI_SSID     = "BOLT! SUPER 4G-F3FA";
const char* WIFI_PASSWORD = "B6D4F3FA";

// Google Apps Script Web App ID
const char* GOOGLE_SCRIPT_ID = "AKfycbwm_nXtujdkseLRJltNmfWkc7Z-xhudD2cG9K7e1tMQc8NEeu68nDj3-utL98AqKRkIog";

// Nama lokasi / perangkat (untuk identifikasi di spreadsheet)
const char* DEVICE_NAME   = "ESP32-PIR-01";
const char* LOCATION_NAME = "Prototipe sawah";

// Pin sensor PIR
#define PIR_PIN 13

// Konfigurasi deteksi
#define MOTION_COOLDOWN_MS  8000    // Jeda antar deteksi (ms)
#define WIFI_RETRY_MAX      20      // Maksimum percobaan koneksi WiFi
#define HTTP_TIMEOUT_MS     10000   // Timeout HTTP request (ms)

// Zona waktu UTC+7 (WIB)
const long  GMT_OFFSET_SEC  = 7 * 3600;
const int   DAYLIGHT_OFFSET = 0;

// =====================================================
//  VARIABEL GLOBAL
// =====================================================

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", GMT_OFFSET_SEC, 60000);

#define INTERVAL_MS         600000  // 10 menit dalam ms
#define MOTION_END_DELAY_MS 2000    // 2 detik tunggu selesai

// [FIX] Re-sync NTP setiap 30 menit agar waktu tidak hilang
#define NTP_RESYNC_INTERVAL_MS (30UL * 60UL * 1000UL)
unsigned long lastNtpSync = 0;
bool          timeSynced  = false;  // [FIX] Flag: apakah waktu sudah tersinkron

unsigned long intervalStart     = 0;
int           intervalCount     = 0;
float         intervalDuration  = 0.0;

unsigned long motionStartTime   = 0;
unsigned long lastHighTime      = 0;
bool          isMotionActive    = false;
bool          motionDetected    = false;
bool          lastMotionState   = false;

int           totalDetectionCount = 0;
bool          wifiConnected       = false;
unsigned long lastPeriodicSend    = 0;

String scriptURL;
String intervalStartTimestamp = "";

// =====================================================
//  FUNGSI KONEKSI WIFI
// =====================================================

bool connectWiFi() {
  Serial.print("\n[WiFi] Menghubungkan ke: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED && retryCount < WIFI_RETRY_MAX) {
    delay(500);
    Serial.print(".");
    retryCount++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] ✓ Terhubung!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    return true;
  } else {
    Serial.println("\n[WiFi] ✗ Gagal terhubung!");
    return false;
  }
}

void checkAndReconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Koneksi terputus, mencoba reconnect...");
    wifiConnected = connectWiFi();

    // [FIX] Jika berhasil reconnect, langsung re-sync NTP
    if (wifiConnected) {
      Serial.println("[WiFi] Reconnect berhasil, re-sync NTP...");
      syncTime();
    }
  }
}

// =====================================================
//  FUNGSI SINKRONISASI WAKTU (DIPERBAIKI)
// =====================================================

// [FIX] syncTime() sekarang mengembalikan bool (berhasil/tidak)
//       dan dicoba hingga MAX_NTP_RETRY kali
#define MAX_NTP_RETRY 5

bool syncTime() {
  Serial.print("[NTP] Sinkronisasi waktu...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET,
             "pool.ntp.org", "time.nist.gov", "time.google.com");

  struct tm timeinfo;

  // [FIX] Coba beberapa kali dengan jeda lebih panjang
  for (int attempt = 1; attempt <= MAX_NTP_RETRY; attempt++) {
    int retry = 0;
    while (!getLocalTime(&timeinfo) && retry < 20) {
      delay(500);
      Serial.print(".");
      retry++;
    }

    if (getLocalTime(&timeinfo)) {
      char timeStr[30];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
      Serial.println("\n[NTP] ✓ Waktu tersinkron: " + String(timeStr));
      lastNtpSync = millis();
      timeSynced  = true;
      return true;
    }

    if (attempt < MAX_NTP_RETRY) {
      Serial.println("\n[NTP] Percobaan " + String(attempt) + " gagal, coba lagi...");
      delay(2000);
    }
  }

  Serial.println("\n[NTP] ✗ Gagal sinkron setelah " + String(MAX_NTP_RETRY) + " percobaan.");
  timeSynced = false;
  return false;
}

// [FIX] Fungsi baru: re-sync NTP secara berkala dari loop()
void checkAndResyncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;

  bool perluSync = !timeSynced ||
                   (millis() - lastNtpSync >= NTP_RESYNC_INTERVAL_MS);

  if (perluSync) {
    Serial.println("[NTP] Melakukan re-sinkronisasi waktu...");
    syncTime();
  }
}

// =====================================================
//  FUNGSI AMBIL TIMESTAMP
// =====================================================

String getTimestamp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[30];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeStr);
  }
  // Fallback: gunakan millis jika NTP benar-benar gagal
  unsigned long ms = millis();
  return "T+" + String(ms / 1000) + "s";
}

String getDateOnly() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char dateStr[12];
    strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
    return String(dateStr);
  }
  return "Unknown";
}

String getTimeOnly() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[10];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    return String(timeStr);
  }
  return "Unknown";
}

// =====================================================
//  FUNGSI KIRIM DATA KE GOOGLE SHEETS
// =====================================================

String urlEncode(String str) {
  String encoded = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (c == ' ')       encoded += "%20";
    else if (c == ':')  encoded += "%3A";
    else if (c == '-')  encoded += "-";
    else if (c == '_')  encoded += "_";
    else if (isAlphaNumeric(c)) encoded += c;
    else {
      char buf[4];
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += String(buf);
    }
  }
  return encoded;
}

bool sendToGoogleSheets(String timestamp, String date, String time,
                         String status, int count, int rssi, float duration = 0.0) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Tidak ada koneksi WiFi!");
    return false;
  }

  String params = "?device="    + urlEncode(String(DEVICE_NAME))
                + "&location="  + urlEncode(String(LOCATION_NAME))
                + "&timestamp=" + urlEncode(timestamp)
                + "&date="      + urlEncode(date)
                + "&time="      + urlEncode(time)
                + "&status="    + urlEncode(status)
                + "&count="     + String(count)
                + "&duration="  + String(duration, 3)
                + "&rssi="      + String(rssi);

  String fullURL = scriptURL + params;
  Serial.println("[HTTP] URL: " + fullURL);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, fullURL);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  String response = http.getString();
  http.end();

  Serial.println("[HTTP] Status: " + String(httpCode));
  Serial.println("[HTTP] Response: " + response);

  if (httpCode == 200) {
    Serial.println("[HTTP] ✓ Data berhasil dikirim!");
    return true;
  }

  Serial.println("[HTTP] ✗ Gagal, code: " + String(httpCode));
  return false;
}

bool sendIntervalData(String tsStart, String tsEnd, String date,
                      int count, float duration, int rssi) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Tidak ada koneksi WiFi!");
    return false;
  }

  String params = "?device="    + urlEncode(String(DEVICE_NAME))
                + "&location="  + urlEncode(String(LOCATION_NAME))
                + "&tsStart="   + urlEncode(tsStart)
                + "&tsEnd="     + urlEncode(tsEnd)
                + "&date="      + urlEncode(date)
                + "&count="     + String(count)
                + "&duration="  + String(duration, 3)
                + "&rssi="      + String(rssi);

  String fullURL = scriptURL + params;
  Serial.println("[HTTP] Mengirim: " + fullURL);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, fullURL);
  http.setTimeout(10000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  String response = http.getString();
  http.end();

  Serial.println("[HTTP] Status  : " + String(httpCode));
  Serial.println("[HTTP] Response: " + response);

  if (httpCode == 200) {
    Serial.println("[HTTP] ✓ Interval berhasil dikirim!");
    return true;
  }

  Serial.println("[HTTP] ✗ Gagal, code: " + String(httpCode));
  return false;
}

// =====================================================
//  SETUP
// =====================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println("  ESP32 PIR Sensor → Google Sheets");
  Serial.println("  Sistem Deteksi Gerakan Real-Time");
  Serial.println("========================================");
  Serial.println("  Device  : " + String(DEVICE_NAME));
  Serial.println("  Lokasi  : " + String(LOCATION_NAME));
  Serial.println("  PIR Pin : GPIO " + String(PIR_PIN));
  Serial.println("========================================\n");

  pinMode(PIR_PIN, INPUT);
  Serial.println("[PIR] Pin " + String(PIR_PIN) + " dikonfigurasi sebagai INPUT");

  scriptURL = "https://script.google.com/macros/s/" +
              String(GOOGLE_SCRIPT_ID) + "/exec";
  Serial.println("[Info] Script URL: " + scriptURL);

  wifiConnected = connectWiFi();
  
  if (wifiConnected) {
    syncTime();

    String ts = getTimestamp();
    String dt = getDateOnly();
    String tm = getTimeOnly();
    sendToGoogleSheets(ts, dt, tm, "SYSTEM_START", 0, WiFi.RSSI());
  }

  Serial.println("\n[PIR] Menunggu sensor stabil (30 detik)...");
  for (int i = 30; i > 0; i--) {
    Serial.print("\r[PIR] Warm-up: " + String(i) + " detik  ");

    // [FIX] Selama warm-up, coba sync NTP jika belum berhasil
    if (!timeSynced && WiFi.status() == WL_CONNECTED) {
      syncTime();
    }

    delay(1000);
  }
  Serial.println("\n[PIR] ✓ Sensor siap digunakan!\n");
  Serial.println("========================================");
  Serial.println("  SISTEM AKTIF - Memantau Gerakan...");
  Serial.println("========================================\n");

  intervalStart          = millis();
  intervalStartTimestamp = getTimestamp();  // [FIX] Dijamin valid karena NTP sudah dicoba berulang
  intervalCount          = 0;
  intervalDuration       = 0.0;
}

// =====================================================
//  LOOP UTAMA
// =====================================================

void loop() {
  checkAndReconnectWiFi();

  // [FIX] Re-sync NTP berkala agar waktu tidak hilang
  checkAndResyncNTP();

  int pirValue   = digitalRead(PIR_PIN);
  motionDetected = (pirValue == HIGH);
  unsigned long now = millis();

  // Gerakan MULAI
  if (motionDetected && !isMotionActive) {
    motionStartTime = now;
    lastHighTime    = now;
    isMotionActive  = true;
    Serial.println("[PIR] Gerakan dimulai...");
  }

  // Update lastHighTime selama masih HIGH
  if (motionDetected && isMotionActive) {
    lastHighTime = now;
  }

  // Gerakan SELESAI
  if (isMotionActive && !motionDetected) {
    if (now - lastHighTime >= MOTION_END_DELAY_MS) {
      float duration = (lastHighTime - motionStartTime) / 1000.0;
      isMotionActive = false;
      intervalCount++;
      intervalDuration += duration;

      Serial.println("[PIR] Gerakan selesai!");
      Serial.println("[PIR] Durasi   : " + String(duration, 3) + " detik");
      Serial.println("[PIR] Count    : " + String(intervalCount));
      Serial.println("[PIR] Total dur: " + String(intervalDuration, 3) + " detik");
    }
  }

  // Kirim data per interval 10 menit
  if (now - intervalStart >= INTERVAL_MS) {
    String tsStart = intervalStartTimestamp;
    String tsEnd   = getTimestamp();
    String dt      = getDateOnly();
    int rssi       = WiFi.RSSI();

    Serial.println("\n[INTERVAL] Mengirim ringkasan interval...");
    Serial.println("[INTERVAL] Periode  : " + tsStart + " — " + tsEnd);
    Serial.println("[INTERVAL] Count    : " + String(intervalCount));
    Serial.println("[INTERVAL] Duration : " + String(intervalDuration, 3) + " detik");

    sendIntervalData(tsStart, tsEnd, dt, intervalCount, intervalDuration, rssi);

    // Reset untuk interval berikutnya
    intervalCount          = 0;
    intervalDuration       = 0.0;
    intervalStart          = now;
    intervalStartTimestamp = getTimestamp();
  }

  lastMotionState = motionDetected;
  delay(100);
}
