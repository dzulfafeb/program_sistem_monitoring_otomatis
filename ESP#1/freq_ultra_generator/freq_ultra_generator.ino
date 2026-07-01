#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <FireTimer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>  // Install via Library Manager: ArduinoJson by Benoit Blanchon

// ===================== KONFIGURASI WiFi AP =====================
const char* wifi_ap_ssid = "PulseGen";
const char* wifi_ap_pass = "";

// ===================== KONFIGURASI WiFi STA =====================
const char* wifi_sta_ssid = "BOLT! SUPER 4G-F3FA";   // <-- ganti sesuai WiFi
const char* wifi_sta_pass = "B6D4F3FA";               // <-- ganti sesuai WiFi

// ===================== KONFIGURASI MQTT EMQX =====================
const char* mqtt_broker      = "broker.emqx.io";
const int   mqtt_port        = 1883;
const char* mqtt_topic       = "pulsegen/frequency";      // publish log alat induk
const char* mqtt_topic_ai    = "sawah/cerdas/perintah";   // subscribe perintah AI
const char* mqtt_client_id   = "ESP32_PulseGen";
const char* mqtt_user        = "";
const char* mqtt_pass_broker = "";

// ===================== KONFIGURASI FREKUENSI =====================
const uint32_t init_freq  = 20000;
const uint8_t  pin_freq   = 13;
const uint8_t  resolution = 8;
const uint32_t duty_cycle = 128;

uint32_t freq_min = 20000UL;
uint32_t freq_max = 20167UL;
uint8_t  config   = 5;
uint8_t  state    = 0;

uint8_t hour = 0, minute = 0, second = 0;

// ===================== VARIABEL MODE CERDAS (config=8) =====================
// Rentang frekuensi per klasifikasi sesuai Tabel 7
const uint32_t AI_FREQ_MIN_RENDAH = 20000UL;
const uint32_t AI_FREQ_MAX_RENDAH = 20167UL;
const uint32_t AI_FREQ_MIN_SEDANG = 20167UL;
const uint32_t AI_FREQ_MAX_SEDANG = 20333UL;
const uint32_t AI_FREQ_MIN_TINGGI = 20333UL;
const uint32_t AI_FREQ_MAX_TINGGI = 20500UL;

String   ai_klasifikasi  = "rendah";
String   ai_prediksi     = "-";
uint32_t ai_freq_min     = AI_FREQ_MIN_RENDAH;
uint32_t ai_freq_max     = AI_FREQ_MAX_RENDAH;
bool     ai_pernah_terima = false;  // true setelah pesan AI pertama diterima

// ===================== OBJEK =====================
WebServer    server(80);
DNSServer    dnsServer;
IPAddress    apIP;
FireTimer    ft_1detik;
WiFiClient   espClient;
PubSubClient mqttClient(espClient);

// ===================== HELPER: generator on/off =====================
void generator_on(uint32_t freq) {
  ledcChangeFrequency(pin_freq, freq, resolution);
  ledcAttach(pin_freq, freq, resolution);
  ledcWrite(pin_freq, duty_cycle);
}

void generator_off() {
  ledcDetach(pin_freq);
  pinMode(pin_freq, OUTPUT);
  digitalWrite(pin_freq, LOW);
}

// ===================== CALLBACK MQTT =====================
// Dipanggil otomatis saat ada pesan masuk di topic yang disubscribe.
// Format payload JSON dari Colab:
// {"klasifikasi":"tinggi","freq_min":20333,"freq_max":20500,"prediksi_1jam":"sedang"}
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  if (String(topic) != String(mqtt_topic_ai)) return;

  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.println("[AI] Pesan diterima: " + msg);

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, msg);
  if (err) {
    Serial.println("[AI] Gagal parse JSON: " + String(err.c_str()));
    return;
  }

  // Ambil klasifikasi → terapkan default range
  if (doc.containsKey("klasifikasi")) {
    ai_klasifikasi = doc["klasifikasi"].as<String>();
    if      (ai_klasifikasi == "rendah") { ai_freq_min = AI_FREQ_MIN_RENDAH; ai_freq_max = AI_FREQ_MAX_RENDAH; }
    else if (ai_klasifikasi == "sedang") { ai_freq_min = AI_FREQ_MIN_SEDANG; ai_freq_max = AI_FREQ_MAX_SEDANG; }
    else if (ai_klasifikasi == "tinggi") { ai_freq_min = AI_FREQ_MIN_TINGGI; ai_freq_max = AI_FREQ_MAX_TINGGI; }
  }

  // Override dengan nilai eksplisit dari payload jika ada
  if (doc.containsKey("freq_min"))     ai_freq_min  = doc["freq_min"].as<uint32_t>();
  if (doc.containsKey("freq_max"))     ai_freq_max  = doc["freq_max"].as<uint32_t>();
  if (doc.containsKey("prediksi_1jam")) ai_prediksi = doc["prediksi_1jam"].as<String>();

  // Langsung terapkan ke freq aktif jika mode cerdas sedang berjalan
  if (config == 8) {
    freq_min = ai_freq_min;
    freq_max = ai_freq_max;
  }

  ai_pernah_terima = true;

  Serial.println("[AI] Klasifikasi: " + ai_klasifikasi
    + " | Range: " + String(ai_freq_min) + "-" + String(ai_freq_max)
    + " Hz | Prediksi: " + ai_prediksi);
}

// ===================== FUNGSI MQTT (non-blocking) =====================
// Tidak pakai while/delay agar WiFi AP tetap responsif untuk HP
void mqtt_connect() {
  if (mqttClient.connected()) return;

  static uint32_t last_attempt = 0;
  uint32_t now = millis();
  if (now - last_attempt < 5000) return;  // coba tiap 5 detik
  last_attempt = now;

  Serial.print("Menghubungkan ke MQTT broker...");
  bool connected = (strlen(mqtt_user) > 0)
    ? mqttClient.connect(mqtt_client_id, mqtt_user, mqtt_pass_broker)
    : mqttClient.connect(mqtt_client_id);

  if (connected) {
    Serial.println(" Terhubung!");
    mqttClient.subscribe(mqtt_topic_ai);
    Serial.println("[MQTT] Subscribe: " + String(mqtt_topic_ai));
  } else {
    Serial.print(" Gagal, rc=");
    Serial.println(mqttClient.state());
    Serial.println("Coba lagi 5 detik...");
  }
}

void mqtt_publish_frequency(uint32_t freq) {
  if (!mqttClient.connected()) return;

  String payload = "{";
  payload += "\"frequency\":\"" + (state == 2 ? String("-") : String(freq)) + "\",";
  payload += "\"fmin\":"       + String(freq_min) + ",";
  payload += "\"fmax\":"       + String(freq_max) + ",";
  payload += "\"state\":"      + String(state) + ",";
  payload += "\"config\":"     + String(config) + ",";
  payload += "\"ai_klas\":\"" + (config == 8 ? ai_klasifikasi : "-") + "\",";
  payload += "\"ai_pred\":\"" + (config == 8 ? ai_prediksi    : "-") + "\",";
  payload += "\"time\":\""    + String(hour) + ":" + String(minute) + ":" + String(second) + "\"";
  payload += "}";

  mqttClient.publish(mqtt_topic, payload.c_str());
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(wifi_ap_ssid, wifi_ap_pass);
  Serial.println("AP aktif. IP: " + WiFi.softAPIP().toString());

  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(wifi_sta_ssid);
  WiFi.begin(wifi_sta_ssid, wifi_sta_pass);

  uint8_t retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi STA terhubung! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nGagal terhubung ke WiFi. Lanjut tanpa internet.");
  }

  mqttClient.setServer(mqtt_broker, mqtt_port);
  mqttClient.setCallback(mqtt_callback);

  webserver_setup();
  ft_1detik.begin(1000UL);

  ledcAttach(pin_freq, init_freq, resolution);
  ledcWrite(pin_freq, duty_cycle);
}

// ===================== LOOP =====================
void loop() {
  webserver_loop();

  if (WiFi.status() == WL_CONNECTED) {
    mqtt_connect();
    mqttClient.loop();
  }

  if (ft_1detik.fire()) {
    second++;
    if (second > 59) { second = 0; minute++;
      if (minute > 59) { minute = 0; hour++;
        if (hour > 23) { hour = 0; }
      }
    }

    uint32_t random_frequency = random(freq_min, freq_max + 1);

    // STATE 1: Manual On
    if (state == 1) {
      generator_on(random_frequency);
    }
    // STATE 2: Manual Off
    else if (state == 2) {
      generator_off();
    }
    // STATE 0: Ikuti Config (Mode)
    else if (state == 0) {

      // Config 5 – Mode 1: 5 menit on / 5 menit off sepanjang hari
      if (config == 5) {
        if (minute % 10 < 5) generator_on(random_frequency);
        else                  generator_off();
      }

      // Config 6 – Mode 2: aktif 16:00–23:59
      else if (config == 6) {
        if (hour >= 16 && hour <= 23) {
          if (minute % 10 < 5) generator_on(random_frequency);
          else                  generator_off();
        } else {
          generator_off();
        }
      }

      // Config 7 – Mode 3: aktif 00:00–08:00
      else if (config == 7) {
        if (hour >= 0 && hour <= 8) {
          if (minute % 10 < 5) generator_on(random_frequency);
          else                  generator_off();
        } else {
          generator_off();
        }
      }

      // Config 8 – Mode 4 (Mode Cerdas): dikontrol AI dari Colab via MQTT
      // - Hanya bisa diganti dari HP via WiFi AP
      // - Tidak ada fallback otomatis ke mode lain
      // - Saat Colab mati: tetap pakai frekuensi AI terakhir
      else if (config == 8) {
        // Belum pernah dapat pesan AI → standby
        if (!ai_pernah_terima) {
          Serial.println("[AI] Standby, menunggu perintah alat tambahan...");
          generator_off();
        }
        // Sudah dapat pesan AI → jalankan dengan frekuensi terakhir dari AI
        else {
          if (minute % 10 < 5) generator_on(random_frequency);
          else                  generator_off();
        }
      }
    }

    // Serial monitor
    Serial.print(hour);   Serial.print(":");
    Serial.print(minute); Serial.print(":");
    Serial.print(second);
    Serial.print("  Fmin:"); Serial.print(freq_min);
    Serial.print(" Fmax:");  Serial.print(freq_max);
    Serial.print(" Stat:");  Serial.print(state);
    Serial.print(" Freq:");
    if (state == 2) {
      Serial.print("-");
      } else {
        Serial.print(random_frequency);
        }
    Serial.print(" C:");     Serial.print(config);
    if (config == 8) {
      Serial.print(" [AI:" + ai_klasifikasi + "|pred:" + ai_prediksi + "]");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      mqtt_publish_frequency(random_frequency);
    }
  }
}
