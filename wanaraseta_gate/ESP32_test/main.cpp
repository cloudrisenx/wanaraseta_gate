#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h> // Untuk Task Watchdog Timer
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> // Tambahkan library ini untuk parsing JSON

// ========================================================================
// 1. ZONA KONFIGURASI UTAMA (EDIT PENGATURAN HANYA DI BAGIAN INI)
// ========================================================================

#define APP_VERSION         "1.1"               // Ganti angka ini setiap ada fitur baru!
#define GITHUB_USER         "cloudrisenx"       // Username GitHub kamu
#define GITHUB_REPO         "wanaraseta_gate"   // Nama Repository kamu

// Konfigurasi Hostpot Bawaan ESP32 (Saat pertama kali nyala / gagal konek)
#define DEFAULT_AP_SSID     "Wanara_Gate_Setup" 
#define DEFAULT_AP_PASSWORD "griyapersada"      

// Interval otomatis cek update ke GitHub (dalam milidetik, 7.200.000 = 2 Jam)
#define UPDATE_INTERVAL_MS  7200000             

#define LED_PIN             2                   // Pin LED Bawaan ESP32 (Build-in LED)
#define GATE_OPEN_MS        3000                // Lama gate terbuka (LED nyala) dalam milidetik (3 detik)

// Watchdog Timer configuration
#define WDT_TIMEOUT_SECONDS 5  // Dibuat lebih waspada: Jika loop() nge-hang > 5 detik, ESP akan restart

// Konfigurasi Barcode Scanner (UART2)
#define BARCODE_RX          16
#define BARCODE_TX          17
#define BARCODE_BAUD        9600  // Sesuaikan dengan baudrate scanner Anda

// ========================================================================
// KONFIGURASI RFID & MQTT
// ========================================================================
// Panduan Wiring MFRC522 ke ESP32 DevKit V1:
// SDA / SS    -> GPIO 21
// SCK / SCLK  -> GPIO 18
// MOSI        -> GPIO 23
// MISO        -> GPIO 19
// IRQ         -> (Tidak perlu disambung)
// RST         -> GPIO 22
// GND         -> GND
// 3.3V        -> 3V3 (PENTING: JANGAN KE 5V/VIN KARENA BISA RUSAK!)

#define SS_PIN              21
#define RST_PIN             22

// Variabel MQTT (Nilai default, akan ditimpa oleh pengaturan dari Web Dashboard)
String mqtt_server     = "127.0.0.1";
int    mqtt_port       = 1883;
String mqtt_user       = "gate_esp32_01";
String mqtt_pass       = "11223344";
String mqtt_client_id  = "gate_esp32_01";

// ========================================================================

WebServer server(80);
WiFiManager wm;
Preferences preferences;

MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String folderAktif = "ESP32_test"; // Folder default untuk OTA, bisa diubah via Web Dashboard
String savedApSSID = DEFAULT_AP_SSID;

// ========================================================================
// 2. FUNGSI OTA GITHUB
// ========================================================================
void cekUpdateGitHub(bool fromWeb = false) {
  auto logMsg = [&](String msg) {
    Serial.print(msg);
    if (fromWeb) server.sendContent(msg);
  };

  logMsg("[OTA] Memeriksa update di cabang GitHub: main\n");

  // Rakit nama folder berdasarkan pilihan yang disimpan via Web Dashboard
  String folderPath = folderAktif;

  // Rakit URL secara dinamis berdasarkan konfigurasi di atas
  String urlVersi = String("https://raw.githubusercontent.com/") + GITHUB_USER + "/" + GITHUB_REPO + "/main/" + folderPath + "/version.txt";
  String urlFirmware = String("https://raw.githubusercontent.com/") + GITHUB_USER + "/" + GITHUB_REPO + "/main/" + folderPath + "/firmware.bin";

  WiFiClientSecure client;
  client.setInsecure(); // Bebas SSL

  HTTPClient http;
  http.begin(client, urlVersi);
  int httpCode = http.GET();

  bool doUpdate = false;

  if (httpCode == HTTP_CODE_OK) {
    String versiDiGitHub = http.getString();
    versiDiGitHub.trim();

    logMsg("[OTA] Versi ESP32   : " + String(APP_VERSION) + "\n");
    logMsg("[OTA] Versi GitHub  : " + versiDiGitHub + "\n");

    if (versiDiGitHub != APP_VERSION && versiDiGitHub.length() > 0) {
      logMsg("[OTA] >> UPDATE DITEMUKAN! File terbaru tersedia.\n");
      logMsg("[OTA] >> Sedang menyedot firmware...\n(Proses memakan waktu 1-2 menit, JANGAN TUTUP HALAMAN INI)\n");
      doUpdate = true;
    } else {
      logMsg("[OTA] >> Firmware sudah paling baru. Aman terkendali.\n");
    }
  } else {
    logMsg("[OTA] >> Gagal menghubungi GitHub (HTTP: " + String(httpCode) + "). Cek koneksi internet atau path folder.\n");
  }
  http.end();
  
  if (doUpdate) {
    httpUpdate.rebootOnUpdate(false); // Atur manual restart
    
    // Cara aman mematikan WDT di berbagai versi Core
    esp_task_wdt_delete(NULL); 

    t_httpUpdate_return ret = httpUpdate.update(client, urlFirmware);
    
    esp_task_wdt_add(NULL); 

    if (ret == HTTP_UPDATE_OK) {
      logMsg("\n[OTA] >> UPDATE SUKSES! ESP32 akan restart otomatis.\n");
      if (fromWeb) {
        server.sendContent("</pre><br><a href='/' class='btn-orange'>KEMBALI</a></div></body></html>");
        server.sendContent(""); // Sinyal end of chunk
      }
      delay(1000);
      ESP.restart();
    } else {
      logMsg("\n[OTA] >> GAGAL UPDATE: " + httpUpdate.getLastErrorString() + " (" + String(httpUpdate.getLastError()) + ")\n");
    }
  }
}

// ========================================================================
// 3. FUNGSI WEB DASHBOARD (HTML DENGAN RAW STRING LITERAL)
// ========================================================================
void handleRoot() {
  // HTML ditulis utuh dan rapi di dalam R"rawliteral(...)rawliteral"
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
      body { background: #000; color: #fff; font-family: sans-serif; text-align: center; padding: 20px; }
      .box { max-width: 420px; margin: 0 auto; padding: 20px; background: #111; border: 1px solid #333; }
      h2 { margin-bottom: 10px; font-weight: normal; }
      h3 { border-bottom: 1px solid #333; padding-bottom: 5px; font-size: 16px; margin-top: 20px; font-weight: normal; }
      .info-text { color: #aaa; font-size: 14px; margin-bottom: 25px; }
      input, select { width: 100%; padding: 12px; margin-top: 10px; box-sizing: border-box; background: #000; color: #fff; border: 1px solid #444; }
      input[type='submit'] { background: #222; cursor: pointer; font-weight: bold; margin-top: 15px; }
      input[type='submit']:hover { background: #333; }
      hr { border: 0; border-top: 1px solid #222; margin: 20px 0; }
    </style>
  </head>
  <body>
    <div class='box'>
      <h2>Wanara Seta - Gate Portal</h2>
      <div class='info-text'>
        <p>Versi Firmware : <strong style='color:#fff;'>{{VERSION}}</strong></p>
        <p>Target Mesin   : <strong style='color:#fff;'>{{FOLDER}}</strong></p>
      </div>

      <hr>
      <h3>Pengaturan Mesin/Board</h3>
      <form action='/save_folder' method='POST'>
        <select name='folder_name'>
          <option value='ESP32main' {{SEL_ESP32MAIN}}>Mesin ESP32 (ESP32main)</option>
          <option value='ESP32_test' {{SEL_ESP32TEST}}>Mesin ESP32 Test (ESP32_test)</option>
          <option value='WT32main' {{SEL_WT32MAIN}}>Mesin WT32 (WT32main)</option>
          <option value='WT32_test' {{SEL_WT32TEST}}>Mesin WT32 Test (WT32_test)</option>
          <option value='src_mainOTA' {{SEL_SRCMAINOTA}}>Main OTA (src_mainOTA)</option>
        </select>
        <input type='submit' value='SIMPAN PENGATURAN & RESTART'>
      </form>

      <hr>
      <h3>Pengaturan MQTT Broker</h3>
      <form action='/save_mqtt' method='POST'>
        <input type='text' name='mqtt_server' placeholder='IP Server (contoh: 192.168.4.50)' value='{{MQTT_SERVER}}' required>
        <input type='number' name='mqtt_port' placeholder='Port (contoh: 1883)' value='{{MQTT_PORT}}' required>
        <input type='text' name='mqtt_user' placeholder='Username MQTT' value='{{MQTT_USER}}' required>
        <input type='password' name='mqtt_pass' placeholder='Password MQTT' value='{{MQTT_PASS}}'>
        <input type='text' name='mqtt_client_id' placeholder='Client ID (contoh: Gate_02)' value='{{MQTT_CLIENT_ID}}' required>
        <input type='submit' value='SIMPAN MQTT & RESTART'>
      </form>

      <hr>
      <h3>Sistem</h3>
      <form action='/forget_wifi' method='POST' onsubmit="return confirm('Yakin ingin menghapus WiFi? Alat akan restart ke mode setup.')">
        <input type='submit' value='LUPAKAN WIFI (RESET KONEKSI)'>
      </form>

      <hr>
      <form action='/cek_update' method='GET'>
        <input type='submit' value='CEK UPDATE GITHUB SEKARANG'>
      </form>
    </div>
  </body>
  </html>
  )rawliteral";

  // Me-replace template teks dengan variabel C++ agar dinamis
  html.replace("{{VERSION}}", APP_VERSION);
  html.replace("{{FOLDER}}", folderAktif);
  html.replace("{{SEL_ESP32MAIN}}", folderAktif == "ESP32main" ? "selected" : "");
  html.replace("{{SEL_ESP32TEST}}", folderAktif == "ESP32_test" ? "selected" : "");
  html.replace("{{SEL_WT32MAIN}}", folderAktif == "WT32main" ? "selected" : "");
  html.replace("{{SEL_WT32TEST}}", folderAktif == "WT32_test" ? "selected" : "");
  html.replace("{{SEL_SRCMAINOTA}}", folderAktif == "src_mainOTA" ? "selected" : "");
  
  html.replace("{{MQTT_SERVER}}", mqtt_server);
  html.replace("{{MQTT_PORT}}", String(mqtt_port));
  html.replace("{{MQTT_USER}}", mqtt_user);
  html.replace("{{MQTT_PASS}}", mqtt_pass);
  html.replace("{{MQTT_CLIENT_ID}}", mqtt_client_id);

  server.send(200, "text/html", html);
}

void handleSaveFolder() {
  if (server.hasArg("folder_name")) {
    String newFolder = server.arg("folder_name");
    preferences.begin("gate_config", false);
    preferences.putString("ota_folder", newFolder);
    preferences.end();

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='5;url=/'><style>body{background:#000;color:#fff;text-align:center;padding:50px;font-family:sans-serif;} a{background:#222;color:#fff;padding:12px 20px;text-decoration:none;border:1px solid #444;display:inline-block;margin-top:20px;font-weight:bold;}</style></head><body><h2>Target mesin diubah ke: " + newFolder + "</h2><p style='color:#aaa;'>Sistem sedang di-restart (Otomatis kembali dalam 5 detik)...</p><a href='/'>KEMBALI KE DASHBOARD</a></body></html>";
    server.send(200, "text/html", html);
    esp_task_wdt_delete(NULL); 
    delay(2000);
    ESP.restart();
  }
}

void handleSaveMqtt() {
  if (server.hasArg("mqtt_server")) {
    mqtt_server = server.arg("mqtt_server");
    mqtt_port = server.arg("mqtt_port").toInt();
    mqtt_user = server.arg("mqtt_user");
    mqtt_pass = server.arg("mqtt_pass");
    mqtt_client_id = server.arg("mqtt_client_id");

    preferences.begin("gate_config", false);
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putInt("mqtt_port", mqtt_port);
    preferences.putString("mqtt_user", mqtt_user);
    preferences.putString("mqtt_pass", mqtt_pass);
    preferences.putString("mqtt_client_id", mqtt_client_id);
    preferences.end();

    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='5;url=/'><style>body{background:#000;color:#fff;text-align:center;padding:50px;font-family:sans-serif;} a{background:#222;color:#fff;padding:12px 20px;text-decoration:none;border:1px solid #444;display:inline-block;margin-top:20px;font-weight:bold;}</style></head><body><h2>Pengaturan MQTT Disimpan!</h2><p style='color:#aaa;'>Sistem sedang di-restart (Otomatis kembali dalam 5 detik)...</p><a href='/'>KEMBALI KE DASHBOARD</a></body></html>";
    server.send(200, "text/html", html);
    esp_task_wdt_delete(NULL); 
    delay(2000);
    ESP.restart();
  }
}

void handleForgetWifi() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#000;color:#fff;text-align:center;padding:50px;font-family:sans-serif;}</style></head><body><h2>WiFi Telah Dilupakan!</h2><p style='color:#aaa;'>ESP32 akan restart dan masuk ke mode Hotspot Setup (Wanara_Gate_Setup).</p><p style='color:#aaa;'>Silakan hubungkan HP Anda ke WiFi tersebut untuk mengatur koneksi baru.</p></body></html>";
  server.send(200, "text/html", html);
  
  Serial.println("[WIFI] Perintah Forget WiFi diterima. Menghapus kredensial...");
  delay(2000);
  wm.resetSettings(); // Menghapus SSID & Password dari memori NVS
  esp_task_wdt_delete(NULL); 
  ESP.restart();
}

void handleCekUpdate() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='2;url=/do_update'><style>body{background:#000;color:#fff;text-align:center;padding:50px;font-family:sans-serif;} .loader{border:4px solid #222;border-top:4px solid #fff;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;} @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head><body><h2>Menghubungi GitHub...</h2><div class='loader'></div><p style='color:#aaa;'>Sistem sedang memproses OTA, mohon tunggu sebentar...</p></body></html>";
  server.send(200, "text/html", html);
}

void handleDoUpdate() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); // Mulai mode chunked live streaming

  String htmlStart = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
      body { background: #000; color: #fff; font-family: sans-serif; text-align: center; padding: 20px; }
      .box { max-width: 500px; margin: 0 auto; padding: 20px; background: #111; border: 1px solid #333; }
      h2 { margin-bottom: 20px; font-weight: normal; }
      pre { background: #000; padding: 15px; border: 1px solid #222; text-align: left; overflow-x: auto; color: #0f0; font-size: 13px; white-space: pre-wrap; word-wrap: break-word; }
      a { background: #222; color: #fff; width: 100%; padding: 12px; border: 1px solid #444; text-decoration: none; display: inline-block; margin-top: 20px; box-sizing: border-box; font-weight: bold; }
      a:hover { background: #333; }
    </style>
  </head>
  <body>
    <div class='box'>
      <h2>Hasil Proses OTA</h2>
      <pre>)rawliteral";

  server.sendContent(htmlStart);
  
  // Eksekusi update dan streaming log ke web perlahan-lahan
  cekUpdateGitHub(true);
  
  // Jika gagal/tidak butuh update, tutup tag HTML-nya
  server.sendContent("</pre><a href='/'>KEMBALI KE DASHBOARD</a></div></body></html>");
  server.sendContent(""); // Sinyal selesai HTTP
}

// ========================================================================
// FUNGSI KONTROL GERBANG
// ========================================================================
unsigned long gateOpenTime = 0;
bool isGateOpen = false;

void openGate() {
  isGateOpen = true;
  gateOpenTime = millis();
  digitalWrite(LED_PIN, HIGH); // Nyalakan LED (Atau trigger RELAY untuk buka gerbang)
  Serial.println("[GATE] Gerbang Terbuka (Relay ON)");
}

void handleGate() {
  // Jika gerbang sedang terbuka dan waktunya (3 detik) sudah habis
  if (isGateOpen && (millis() - gateOpenTime > GATE_OPEN_MS)) {
    isGateOpen = false;
    digitalWrite(LED_PIN, LOW); // Matikan LED (Tutup RELAY)
    Serial.println("[GATE] Waktu habis. Gerbang Tertutup (Relay OFF)");
  }
}

// ========================================================================
// CALLBACK MQTT (Saat menerima pesan dari Server)
// ========================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("\n[MQTT] Pesan masuk di topik: %s\n", topic);
  Serial.println("[MQTT] Payload: " + message);

  JsonDocument doc; // Menggunakan ArduinoJson v7 (Jika v6, gunakan DynamicJsonDocument doc(512);)
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.println("[JSON] Gagal mem-parsing payload dari server.");
    return;
  }

  String topicStr = String(topic);
  String resTopic = "gate/" + mqtt_client_id + "/result";
  String cmdTopic = "gate/" + mqtt_client_id + "/command";

  // Skenario 1: Cek status validitas kartu
  if (topicStr == resTopic) {
    String status = doc["status"];
    if (status == "valid") {
      Serial.println("[AKSES] Diberikan! Pesan: " + doc["message"].as<String>());
      openGate(); // Buka gerbang
    } else if (status == "invalid") {
      Serial.println("[AKSES] Ditolak! Alasan: " + doc["message"].as<String>());
    }
  } 
  // Skenario 2: Command Manual
  else if (topicStr == cmdTopic) {
    String cmd = doc["command"];
    if (cmd == "open_gate") {
      Serial.println("[COMMAND] Perintah Override: Buka Gerbang!");
      openGate();
    } else if (cmd == "reboot") {
      Serial.println("[COMMAND] Perintah Reboot dari server. Restarting...");
      esp_task_wdt_delete(NULL); 
      delay(1000);
      ESP.restart();
    }
  }
}

// ========================================================================
// FUNGSI MQTT
// ========================================================================
unsigned long lastMqttReconnectAttempt = 0;

void reconnectMQTT() {
  Serial.printf("[MQTT] Melakukan TCP Ping ke %s:%d...\n", mqtt_server.c_str(), mqtt_port);
  
  // Cek apakah server dan port 1883 bisa dijangkau
  if (!espClient.connect(mqtt_server.c_str(), mqtt_port)) {
    Serial.println("[MQTT] ERROR: Ping Gagal! Server tidak dapat dijangkau.");
    Serial.println("[MQTT] Saran: Cek IP, apakah EMQX berjalan, atau UFW/Firewall memblokir port 1883.");
    return; // Langsung keluar agar Web Dashboard tetap responsif
  }
  espClient.stop(); // Tutup koneksi TCP sementara karena ping sukses
  Serial.println("[MQTT] Ping Sukses! Server aktif.");

  Serial.print("[MQTT] Melakukan Autentikasi ke EMQX Broker...");
  if (mqttClient.connect(mqtt_client_id.c_str(), mqtt_user.c_str(), mqtt_pass.c_str())) {
    Serial.println(" Terhubung!");
    
    String resTopic = "gate/" + mqtt_client_id + "/result";
    String cmdTopic = "gate/" + mqtt_client_id + "/command";
    
    mqttClient.subscribe(resTopic.c_str());
    mqttClient.subscribe(cmdTopic.c_str());
    
    Serial.println("[MQTT] Berhasil subscribe ke topik result & command.");
  } else {
    Serial.print(" Gagal, rc=");
    Serial.println(mqttClient.state());
  }
}

// ========================================================================
// 4. SETUP & MAIN LOOP
// ========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Ambil data folder aktif dan nama AP WiFi dari memori internal
  preferences.begin("gate_config", true);
  folderAktif = preferences.getString("ota_folder", "ESP32main");
  savedApSSID = preferences.getString("ap_ssid", DEFAULT_AP_SSID); // Mengambil dari const di atas

  // Load konfigurasi MQTT (Jika tidak ada, fallback ke nilai variabel global)
  mqtt_server = preferences.getString("mqtt_server", mqtt_server);
  mqtt_port = preferences.getInt("mqtt_port", mqtt_port);
  mqtt_user = preferences.getString("mqtt_user", mqtt_user);
  mqtt_pass = preferences.getString("mqtt_pass", mqtt_pass);
  mqtt_client_id = preferences.getString("mqtt_client_id", mqtt_client_id);
  preferences.end();

  // Memulai WiFi Manager (Jika gagal konek, panggil AP dan Password dari const di atas)
  Serial.println("\n[WIFI] Mencoba terhubung ke jaringan...");
  wm.autoConnect(savedApSSID.c_str(), DEFAULT_AP_PASSWORD);
  Serial.println("[WIFI] Sukses Terhubung! IP Web Dashboard: " + WiFi.localIP().toString());

  // Routing Halaman Web
  server.on("/", handleRoot);
  server.on("/save_folder", HTTP_POST, handleSaveFolder);
  server.on("/save_mqtt", HTTP_POST, handleSaveMqtt);
  server.on("/cek_update", HTTP_GET, handleCekUpdate);
  server.on("/forget_wifi", HTTP_POST, handleForgetWifi);
  server.on("/do_update", HTTP_GET, handleDoUpdate);
  server.begin();

  // Setup SPI & RFID
  SPI.begin(); // Otomatis menggunakan VSPI ESP32 (SCK=18, MISO=19, MOSI=23)
  mfrc522.PCD_Init();
  Serial.println("[RFID] Sensor MFRC522 Siap.");

  // Setup Barcode Scanner (UART2)
  Serial2.begin(BARCODE_BAUD, SERIAL_8N1, BARCODE_RX, BARCODE_TX);
  Serial.println("[BARCODE] Scanner UART2 Siap (GPIO 16/17).");

  // Setup MQTT Broker
  mqttClient.setServer(mqtt_server.c_str(), mqtt_port);
  mqttClient.setCallback(mqttCallback); // Daftarkan fungsi callback
  mqttClient.setBufferSize(512);        // Perbesar buffer agar JSON aman

  // Aktifkan Task Watchdog Timer SETELAH WiFi Terhubung (Mencegah restart saat loading WiFi)
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0,
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&twdt_config);
  #else
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true); 
  #endif
  
  esp_task_wdt_add(NULL); 

  // Auto cek pembaruan saat boot dimatikan sesuai permintaan
  // cekUpdateGitHub(); 
  // (Proses download dan restart akan dieksekusi otomatis oleh loop() jika ada update)

  // Setup Pin LED sebagai Output
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  server.handleClient(); // Jaga agar server web tetap responsif

  // Beri makan Task Watchdog Timer agar tidak reset
  esp_task_wdt_reset();

  // Jaga koneksi MQTT tetap hidup
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      // Coba sambung ulang setiap 5 detik tanpa memblokir sistem
      if (millis() - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = millis();
        reconnectMQTT();
      }
    } else {
      mqttClient.loop();
    }
  }

  // Timer OTA otomatis dimatikan: update HANYA saat diklik dari Web Dashboard
  // static unsigned long lastCheck = 0;
  // if (millis() - lastCheck > UPDATE_INTERVAL_MS) { 
  //   cekUpdateGitHub();
  //   lastCheck = millis();
  // }

  // ---------------------------------------------------------
  // MASUKKAN LOGIKA RFID, KONTROL GERBANG & EMQX DI SINI
  // ---------------------------------------------------------

  // Logika Pembacaan Barcode (UART2)
  if (Serial2.available()) {
    String barcodeData = Serial2.readStringUntil('\r'); // Biasanya scanner mengirim CR atau LF di akhir
    barcodeData.trim();
    
    if (barcodeData.length() > 0) {
      Serial.println("[BARCODE] Terdeteksi! Data: " + barcodeData);
      
      if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
        // Kirim Payload ke EMQX (Gunakan format JSON yang sama agar Backend mudah mengolahnya)
        String payload = "{\"barcode\":\"" + barcodeData + "\", \"device_id\":\"" + mqtt_client_id + "\"}";
        String pubTopic = "gate/" + mqtt_client_id + "/scan/in";
        
        if (mqttClient.publish(pubTopic.c_str(), payload.c_str())) {
          Serial.println("[MQTT] Barcode SUKSES Terkirim.");
        } else {
          Serial.println("[MQTT] Barcode GAGAL Mengirim!");
        }
      } else {
        Serial.println("[MQTT] Offline, Barcode tidak terkirim.");
      }
    }
  }

  // Logika Pembacaan RFID
  static unsigned long lastRFIDReadTime = 0;

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    if (millis() - lastRFIDReadTime > 1000) { // Cooldown 1 detik tanpa memblokir sistem
      String rfidUid = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        rfidUid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        rfidUid += String(mfrc522.uid.uidByte[i], HEX);
      }
      rfidUid.toUpperCase();
      Serial.println("[RFID] Kartu Terdeteksi! UID: " + rfidUid);

      // Cek apakah ini Kartu Master Hardcode (Bypass akses offline)
      if (rfidUid == "A166C820") {
        Serial.println("[MASTER] Kartu Master dikenali! Buka gerbang langsung tanpa MQTT.");
        openGate();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      } else {
        // Kirim Payload ke EMQX Broker (Sesuai Requirement 1 & 2)
        String payload = "{\"rfid\":\"" + rfidUid + "\", \"device_id\":\"" + mqtt_client_id + "\"}";
        String pubTopic = "gate/" + mqtt_client_id + "/scan/in";
        
        if (mqttClient.publish(pubTopic.c_str(), payload.c_str())) {
          Serial.println("[MQTT] SUKSES Terkirim ke " + pubTopic);
        } else {
          Serial.println("[MQTT] GAGAL Mengirim! Cek koneksi atau ukuran buffer.");
        }
        
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }

      lastRFIDReadTime = millis();
    }
  }

  // Tangani timer penutupan gerbang otomatis
  handleGate();
}