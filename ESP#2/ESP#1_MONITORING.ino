/*
 * ============================================================
 *  ALAT TAMBAHAN – ESP32 #2  (v5 - dengan NTP sinkronisasi waktu)
 *  Detektor Aktivitas Tikus via Sensor PIR
 * ============================================================
 *  Library yang dibutuhkan (Arduino Library Manager):
 *    - ArduinoJson  by Benoit Blanchon
 *    - FireTimer    by PowerBroker2
 *    - NTPClient    by Fabrice Weinberg
 * ============================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FireTimer.h>
#include <NTPClient.h>     // Tambahan: sinkronisasi waktu via internet
#include <WiFiUdp.h>       // Tambahan: diperlukan oleh NTPClient

// ===================== KONFIGURASI WiFi =====================
const char* wifi_ssid = "BOLT! SUPER 4G-F3FA";  
const char* wifi_pass = "B6D4F3FA"; 

// ===================== KONFIGURASI APPS SCRIPT =====================
const char* apps_script_url = "https://script.google.com/macros/s/AKfycbxnKxXivcs9P7tWvBVb3Ihma0lnh-KeRrB6VXMgoy08EaGjtYfjvJxg_T4wrU0ypnRQ/exec";

// ===================== KONFIGURASI NTP =====================
// Offset waktu WIB = UTC+7 = 7 * 3600 = 25200 detik
const long  UTC_OFFSET   = 25200;
const int   NTP_INTERVAL = 60000; // update NTP tiap 60 detik

WiFiUDP     ntpUDP;
NTPClient   timeClient(ntpUDP, "pool.ntp.org", UTC_OFFSET, NTP_INTERVAL);

// ===================== KONFIGURASI PIN =====================
const uint8_t PIN_PIR = 13; 

// ===================== KONFIGURASI INTERVAL =====================
const uint32_t INTERVAL_MS = 600000UL;  // 10 menit

// ===================== VARIABEL GLOBAL =====================
String   colab_url = "";
bool     url_ready = false;

uint32_t pir_count        = 0;
uint32_t pir_durasi_detik = 0;
bool     pir_state_lalu   = false;

// Waktu awal interval (dari NTP)
String   iv_start_str = "";  // format "HH:MM"

FireTimer ft_1detik;
FireTimer ft_interval;

// ===================== FUNGSI HELPER WAKTU =====================
// Ambil waktu sekarang dari NTP dalam format "HH:MM"
String get_waktu_sekarang() {
  timeClient.update();
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
  return String(buf);
}

// Ambil waktu sekarang lengkap untuk serial monitor
String get_waktu_lengkap() {
  timeClient.update();
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  int s = timeClient.getSeconds();
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

// ===================== FUNGSI WiFi =====================
void wifi_connect() {
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);

  uint8_t retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi terhubung! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nGagal terhubung ke WiFi!");
  }
}

// ===================== FUNGSI BACA URL DARI APPS SCRIPT =====================
bool baca_url_dari_apps_script() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[URL] WiFi tidak terhubung.");
    return false;
  }

  Serial.println("[URL] Membaca URL Colab dari Apps Script...");

  HTTPClient http;
  http.begin(apps_script_url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  int http_code = http.GET();

  if (http_code != 200) {
    Serial.println("[URL] Gagal. HTTP code: " + String(http_code));
    http.end();
    return false;
  }

  String response = http.getString();
  http.end();

  Serial.println("[URL] Respons: " + response);

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.println("[URL] Gagal parse JSON: " + String(err.c_str()));
    return false;
  }

  String status = doc["status"].as<String>();
  if (status != "ok") {
    Serial.println("[URL] Error: " + doc["message"].as<String>());
    return false;
  }

  String url = doc["url"].as<String>();
  if (url.length() == 0) {
    Serial.println("[URL] URL kosong.");
    return false;
  }

  colab_url = url;
  Serial.println("[URL] URL Colab: " + colab_url);
  return true;
}

// ===================== FUNGSI KIRIM KE COLAB =====================
void kirim_ke_colab(uint32_t count, uint32_t durasi) {
  if (!url_ready) {
    Serial.println("[HTTP] URL Colab belum siap, skip kirim.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] WiFi tidak terhubung, skip kirim.");
    return;
  }

  // Ambil waktu akhir interval dari NTP
  String t_end = get_waktu_sekarang();

  StaticJsonDocument<256> doc;
  doc["t_start"] = iv_start_str;  // waktu awal interval (dari NTP)
  doc["t_end"]   = t_end;         // waktu akhir interval (dari NTP)
  doc["count"]   = count;
  doc["durasi"]  = durasi;

  String payload;
  serializeJson(doc, payload);

  Serial.println("[HTTP] Kirim ke Colab: " + payload);

  HTTPClient http;
  http.begin(colab_url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);

  int http_code = http.POST(payload);

  if (http_code > 0) {
    String response = http.getString();
    Serial.println("[HTTP] Respons (" + String(http_code) + "): " + response);
  } else {
    Serial.println("[HTTP] Gagal kirim: " + String(http.errorToString(http_code)));
    Serial.println("[HTTP] Coba baca ulang URL dari Apps Script...");
    url_ready = baca_url_dari_apps_script();
  }

  http.end();
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("=== ALAT TAMBAHAN – Detektor Tikus v5 ===");

  pinMode(PIN_PIR, INPUT);

  wifi_connect();

  // Inisialisasi NTP — sinkronisasi waktu dari internet
  timeClient.begin();
  timeClient.update();
  Serial.println("[NTP] Waktu sekarang: " + get_waktu_lengkap());

  // Baca URL Colab dari Apps Script saat boot
  uint8_t attempt = 0;
  while (!url_ready && attempt < 5) {
    attempt++;
    Serial.println("[BOOT] Percobaan baca URL ke-" + String(attempt));
    url_ready = baca_url_dari_apps_script();
    if (!url_ready) delay(3000);
  }

  if (url_ready) {
    Serial.println("[BOOT] Sistem siap. URL: " + colab_url);
  } else {
    Serial.println("[BOOT] Gagal baca URL setelah 5 percobaan.");
    Serial.println("[BOOT] Sistem tetap berjalan, coba lagi saat interval berikutnya.");
  }

  ft_1detik.begin(1000UL);
  ft_interval.begin(INTERVAL_MS);

  // Catat waktu awal interval pertama dari NTP
  iv_start_str = get_waktu_sekarang();
  Serial.println("[BOOT] Interval pertama mulai: " + iv_start_str);
}

// ===================== LOOP =====================
void loop() {

  // Tick 1 detik: update NTP & baca PIR
  if (ft_1detik.fire()) {
    timeClient.update();

    bool pir_now = digitalRead(PIN_PIR) == HIGH;

    if (pir_now && !pir_state_lalu) pir_count++;
    if (pir_now) pir_durasi_detik++;

    pir_state_lalu = pir_now;

    // Serial monitor dengan waktu NTP
    Serial.print(get_waktu_lengkap());
    Serial.print("  PIR:");    Serial.print(pir_now ? "HIGH" : "LOW");
    Serial.print("  Count:");  Serial.print(pir_count);
    Serial.print("  Durasi:"); Serial.print(pir_durasi_detik);
    Serial.println(" detik");
  }

  // Tick interval (10 menit): kirim data ke Colab
  if (ft_interval.fire()) {
    uint32_t snapshot_count  = pir_count;
    uint32_t snapshot_durasi = pir_durasi_detik;

    Serial.println("========== INTERVAL SELESAI ==========");
    Serial.println("Mulai    : " + iv_start_str);
    Serial.println("Selesai  : " + get_waktu_sekarang());
    Serial.println("Count    : " + String(snapshot_count));
    Serial.println("Durasi   : " + String(snapshot_durasi) + " detik");
    Serial.println("======================================");

    if (!url_ready) {
      Serial.println("[INTERVAL] URL belum siap, coba baca dari Apps Script...");
      url_ready = baca_url_dari_apps_script();
    }

    kirim_ke_colab(snapshot_count, snapshot_durasi);

    // Reset untuk interval berikutnya
    pir_count        = 0;
    pir_durasi_detik = 0;
    iv_start_str     = get_waktu_sekarang();  // catat waktu mulai interval baru dari NTP
  }

  // Reconnect WiFi otomatis jika putus
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Koneksi putus, reconnect...");
    wifi_connect();
  }
}
