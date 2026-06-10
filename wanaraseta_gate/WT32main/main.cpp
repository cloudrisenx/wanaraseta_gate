#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
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

#define APP_VERSION         "1.2"               // Ganti angka ini setiap ada fitur baru!
#define GITHUB_USER         "cloudrisenx"       // Username GitHub kamu
#define GITHUB_REPO         "wanaraseta_gate"   // Nama Repository kamu

// Interval otomatis cek update ke GitHub (dalam milidetik, 7.200.000 = 2 Jam)
#define UPDATE_INTERVAL_MS  7200000             

#define RELAY1_PIN          2                   // Relay 1 (Gate In) - Sisi J2
#define RELAY2_PIN          5                   // Relay 2 (Gate Out / Alarm) - Sisi J3 (Seberang)
#define GATE_OPEN_MS        3000                // Lama relay aktif (3 detik)

// Watchdog Timer configuration
#define WDT_TIMEOUT_SECONDS 5  // Dibuat lebih waspada: Jika loop() nge-hang > 5 detik, ESP akan restart

// Konfigurasi Barcode Scanner (UART2)
#define BARCODE_RX          39  // Menggunakan pin di sisi J2
#define BARCODE_TX          33  // Tetap di 33 (sisi seberang), tapi biasanya tidak perlu dipasang
#define BARCODE_BAUD        9600  // Sesuaikan dengan baudrate scanner Anda

// Konfigurasi Pin Ethernet WT32-ETH01 (LAN8720)
#define WT32_ETH_ADDR  1
#define WT32_ETH_POWER 16
#define WT32_ETH_MDC   23
#define WT32_ETH_MDIO  18
#define WT32_ETH_TYPE  ETH_PHY_LAN8720
#define WT32_ETH_CLK   ETH_CLOCK_GPIO0_IN

// ========================================================================
// KONFIGURASI RFID & MQTT
// ========================================================================
// Panduan Wiring MFRC522 ke WT32-ETH01 (Satu Sisi J2):
// SDA / SS    -> GPIO 12
// SCK / SCLK  -> GPIO 14
// MOSI        -> GPIO 15
// MISO        -> GPIO 35 (Input Only)
// RST         -> GPIO 4
// LED_PIN     -> GPIO 2 (Built-in LED)

#define SS_PIN              12
#define RST_PIN             4

#define SPI_SCK             14
#define SPI_MISO            35
#define SPI_MOSI            15

// Variabel MQTT (Nilai default, akan ditimpa oleh pengaturan dari Web Dashboard)
String mqtt_server     = "127.0.0.1";
int    mqtt_port       = 1883;
String mqtt_user       = "gate_wt32_01";
String mqtt_pass       = "11223344";
String mqtt_client_id  = "gate_wt32_01";

// ========================================================================

WebServer server(80);
Preferences preferences;

MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String folderAktif = "WT32main"; // Sesuaikan dengan folder mesin ini
bool eth_connected = false;

String rfidStatus = "Mencari Sensor...";
String lastRfidScan = "Belum ada kartu";

// Callback untuk memantau status Ethernet
void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.println("[ETH] Terhubung! IP: " + ETH.localIP().toString());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("[ETH] Kabel Terputus!");
      eth_connected = false;
      break;
    default:
      break;
  }
}

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
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle()); 

    t_httpUpdate_return ret = httpUpdate.update(client, urlFirmware);
    
    esp_task_wdt_add(xTaskGetCurrentTaskHandle()); 

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
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
      body { background-color: #121212; color: #ffffff; font-family: Arial, sans-serif; text-align: center; padding: 20px; }
      .box { max-width: 420px; margin: 0 auto; padding: 25px; background-color: #1e1e1e; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
      h2 { color: #00adb5; margin-bottom: 5px; }
      .info-text { color: #aaaaaa; font-size: 14px; margin-bottom: 25px; }
      select, input[type='submit'], input[type='text'], input[type='number'], input[type='password'] { width: 100%; padding: 12px; margin-top: 10px; border-radius: 6px; font-weight: bold; cursor: pointer; border: none; box-sizing: border-box; }
      select, input[type='text'], input[type='number'], input[type='password'] { background-color: #252525; color: white; border: 1px solid #444; font-weight: normal; cursor: auto; margin-top: 5px; }
      .btn-orange { background-color: #ff9f43; color: #121212; }
      .btn-blue { background-color: #00adb5; color: #121212; margin-top: 25px; }
      .btn-red { background-color: #ff5252; color: #ffffff; }
      hr { border-color: #333; margin: 20px 0; }
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
      <h3>Status Hardware</h3>
      <div class='info-text' style='text-align:left; background:#252525; padding:10px; border-radius:6px;'>
        <p>📡 RFID Sensor: <strong style='color:{{RFID_COLOR}};'>{{RFID_STATUS}}</strong></p>
        <p>💳 Scan Terakhir: <strong style='color:#00ff00;'>{{LAST_RFID}}</strong></p>
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
        <input type='submit' class='btn-orange' value='SIMPAN PENGATURAN & RESTART'>
      </form>

      <hr>
      <h3>Pengaturan MQTT Broker</h3>
      <form action='/save_mqtt' method='POST'>
        <input type='text' name='mqtt_server' placeholder='IP Server (contoh: 192.168.4.50)' value='{{MQTT_SERVER}}' required>
        <input type='number' name='mqtt_port' placeholder='Port (contoh: 1883)' value='{{MQTT_PORT}}' required>
        <input type='text' name='mqtt_user' placeholder='Username MQTT' value='{{MQTT_USER}}' required>
        <input type='password' name='mqtt_pass' placeholder='Password MQTT' value='{{MQTT_PASS}}'>
        <input type='text' name='mqtt_client_id' placeholder='Client ID (contoh: Gate_02)' value='{{MQTT_CLIENT_ID}}' required>
        <input type='submit' class='btn-orange' value='SIMPAN MQTT & RESTART'>
      </form>

      <hr>
      <form action='/cek_update' method='GET'>
        <input type='submit' class='btn-blue' value='CEK UPDATE GITHUB SEKARANG'>
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
  
  html.replace("{{RFID_STATUS}}", rfidStatus);
  html.replace("{{RFID_COLOR}}", rfidStatus == "AKTIF" ? "#00ff00" : "#ff5252");
  html.replace("{{LAST_RFID}}", lastRfidScan);

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

    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='5;url=/'><style>body{background:#121212;color:#fff;text-align:center;padding:50px;font-family:Arial;} .btn-orange{background:#ff9f43;color:#121212;padding:12px 20px;text-decoration:none;border-radius:6px;font-weight:bold;display:inline-block;margin-top:20px;}</style></head><body><h2 style='color:#00adb5;'>Target mesin diubah ke: " + newFolder + "</h2><p>Sistem sedang di-restart (Otomatis kembali dalam 5 detik)...</p><a href='/' class='btn-orange'>KEMBALI KE DASHBOARD</a></body></html>";
    server.send(200, "text/html", html);
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle()); 
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

    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='5;url=/'><style>body{background:#121212;color:#fff;text-align:center;padding:50px;font-family:Arial;} .btn-orange{background:#ff9f43;color:#121212;padding:12px 20px;text-decoration:none;border-radius:6px;font-weight:bold;display:inline-block;margin-top:20px;}</style></head><body><h2 style='color:#00adb5;'>Pengaturan MQTT Disimpan!</h2><p>Sistem sedang di-restart (Otomatis kembali dalam 5 detik)...</p><a href='/' class='btn-orange'>KEMBALI KE DASHBOARD</a></body></html>";
    server.send(200, "text/html", html);
    esp_task_wdt_delete(xTaskGetCurrentTaskHandle()); 
    delay(2000);
    ESP.restart();
  }
}

void handleCekUpdate() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='2;url=/do_update'><style>body{background:#121212;color:#fff;text-align:center;padding:50px;font-family:Arial;} .loader{border:6px solid #1e1e1e;border-top:6px solid #00adb5;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:20px auto;} @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head><body><h2 style='color:#00adb5;'>Menghubungi GitHub...</h2><div class='loader'></div><p>Sistem sedang memproses OTA, mohon tunggu sebentar...</p></body></html>";
  server.send(200, "text/html", html);
}

void handleDoUpdate() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); // Mulai mode chunked live streaming

  String htmlStart = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <style>
      body { background-color: #121212; color: #ffffff; font-family: Arial, sans-serif; text-align: center; padding: 20px; }
      .box { max-width: 500px; margin: 0 auto; padding: 25px; background-color: #1e1e1e; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
      h2 { color: #00adb5; margin-bottom: 20px; }
      pre { background-color: #000; padding: 15px; border-radius: 8px; text-align: left; overflow-x: auto; color: #00ff00; font-size: 14px; white-space: pre-wrap; word-wrap: break-word; }
      .btn-orange { background-color: #ff9f43; color: #121212; width: 100%; padding: 12px; border-radius: 6px; font-weight: bold; cursor: pointer; border: none; text-decoration: none; display: inline-block; margin-top: 20px; box-sizing: border-box;}
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
  server.sendContent("</pre><a href='/' class='btn-orange'>KEMBALI KE DASHBOARD</a></div></body></html>");
  server.sendContent(""); // Sinyal selesai HTTP
}

// ========================================================================
// FUNGSI KONTROL GERBANG
// ========================================================================
unsigned long relay1ActiveTime = 0;
bool isRelay1Active = false;
unsigned long relay2ActiveTime = 0;
bool isRelay2Active = false;

void triggerRelay(int relayNum) {
  if (relayNum == 1) {
    isRelay1Active = true;
    relay1ActiveTime = millis();
    digitalWrite(RELAY1_PIN, HIGH);
    Serial.println("[GATE] Relay 1 Aktif");
  } else if (relayNum == 2) {
    isRelay2Active = true;
    relay2ActiveTime = millis();
    digitalWrite(RELAY2_PIN, HIGH);
    Serial.println("[GATE] Relay 2 Aktif");
  }
}

void handleGate() {
  if (isRelay1Active && (millis() - relay1ActiveTime > GATE_OPEN_MS)) {
    isRelay1Active = false;
    digitalWrite(RELAY1_PIN, LOW);
    Serial.println("[GATE] Relay 1 Mati");
  }
  if (isRelay2Active && (millis() - relay2ActiveTime > GATE_OPEN_MS)) {
    isRelay2Active = false;
    digitalWrite(RELAY2_PIN, LOW);
    Serial.println("[GATE] Relay 2 Mati");
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
      triggerRelay(1); // Default buka Relay 1
    } else if (status == "invalid") {
      Serial.println("[AKSES] Ditolak! Alasan: " + doc["message"].as<String>());
    }
  } 
  // Skenario 2: Command Manual
  else if (topicStr == cmdTopic) {
    String cmd = doc["command"];
    if (cmd == "open_gate") {
      Serial.println("[COMMAND] Perintah Override: Buka Gerbang!");
      triggerRelay(1);
    } else if (cmd == "open_gate_2") {
      Serial.println("[COMMAND] Perintah Override: Buka Relay 2!");
      triggerRelay(2);
    }else if (cmd == "reboot") {
      Serial.println("[COMMAND] Perintah Reboot dari server. Restarting...");
      esp_task_wdt_delete(xTaskGetCurrentTaskHandle()); 
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
  folderAktif = preferences.getString("ota_folder", "WT32main");

  // Load konfigurasi MQTT (Jika tidak ada, fallback ke nilai variabel global)
  mqtt_server = preferences.getString("mqtt_server", mqtt_server);
  mqtt_port = preferences.getInt("mqtt_port", mqtt_port);
  mqtt_user = preferences.getString("mqtt_user", mqtt_user);
  mqtt_pass = preferences.getString("mqtt_pass", mqtt_pass);
  mqtt_client_id = preferences.getString("mqtt_client_id", mqtt_client_id);
  preferences.end();

  // Inisialisasi Ethernet WT32-ETH01
  WiFi.onEvent(WiFiEvent);
  ETH.begin(WT32_ETH_ADDR, WT32_ETH_POWER, WT32_ETH_MDC, WT32_ETH_MDIO, WT32_ETH_TYPE, WT32_ETH_CLK);

  Serial.println("\n[ETH] Mencari koneksi LAN (DHCP)...");

  // Routing Halaman Web
  server.on("/", handleRoot);
  server.on("/save_folder", HTTP_POST, handleSaveFolder);
  server.on("/save_mqtt", HTTP_POST, handleSaveMqtt);
  server.on("/cek_update", HTTP_GET, handleCekUpdate);
  server.on("/do_update", HTTP_GET, handleDoUpdate);
  server.begin();

  // Setup SPI & RFID
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SS_PIN); 
  mfrc522.PCD_Init();

  // Cek apakah RFID terdeteksi dengan membaca register versi chip
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (v == 0x91 || v == 0x92) {
    rfidStatus = "AKTIF";
  } else {
    rfidStatus = "TIDAK TERDETEKSI (Cek Kabel)";
  }
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

  // Setup Pin Relay sebagai Output
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
}

void loop() {
  server.handleClient(); // Jaga agar server web tetap responsif

  // Beri makan Task Watchdog Timer agar tidak reset
  esp_task_wdt_reset();

  // Jaga koneksi MQTT tetap hidup
  if (eth_connected) {
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
      
      if (eth_connected && mqttClient.connected()) {
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
      lastRfidScan = rfidUid; // Simpan untuk ditampilkan di Web

      // Cek apakah ini Kartu Master Hardcode (Bypass akses offline)
      if (rfidUid == "A166C820") {
        Serial.println("[MASTER] Kartu Master dikenali! Buka gerbang langsung tanpa MQTT.");
        triggerRelay(1);
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