#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h> // Untuk Task Watchdog Timer
#include <Preferences.h>

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
#define TOUCH_PIN           4                   // Pin Touch Sensor (T0)
#define TOUCH_THRESHOLD     30                  // Batas deteksi sentuhan (biasanya < 40 jika disentuh)
#define GATE_OPEN_MS        3000                // Lama gate terbuka (LED nyala) dalam milidetik (3 detik)

// Watchdog Timer configuration
#define WDT_TIMEOUT_SECONDS 5  // Dibuat lebih waspada: Jika loop() nge-hang > 5 detik, ESP akan restart

// ========================================================================

WebServer server(80);
WiFiManager wm;
Preferences preferences;

String folderAktif = "ESP32main";
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
    esp_task_wdt_delete(NULL); // Matikan WDT sementara
    t_httpUpdate_return ret = httpUpdate.update(client, urlFirmware);
    esp_task_wdt_add(NULL); // Hidupkan WDT kembali
    
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
      <h3>Pengaturan Mesin/Board (Firmware Folder)</h3>
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
    delay(2000);
    ESP.restart();
  }
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
// 4. SETUP & MAIN LOOP
// ========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Ambil data folder aktif dan nama AP WiFi dari memori internal
  preferences.begin("gate_config", true);
  folderAktif = preferences.getString("ota_folder", "ESP32main");
  savedApSSID = preferences.getString("ap_ssid", DEFAULT_AP_SSID); // Mengambil dari const di atas
  preferences.end();

  // Memulai WiFi Manager (Jika gagal konek, panggil AP dan Password dari const di atas)
  Serial.println("\n[WIFI] Mencoba terhubung ke jaringan...");
  wm.autoConnect(savedApSSID.c_str(), DEFAULT_AP_PASSWORD);
  Serial.println("[WIFI] Sukses Terhubung! IP Web Dashboard: " + WiFi.localIP().toString());

  // Routing Halaman Web
  server.on("/", handleRoot);
  server.on("/save_folder", HTTP_POST, handleSaveFolder);
  server.on("/cek_update", HTTP_GET, handleCekUpdate);
  server.on("/do_update", HTTP_GET, handleDoUpdate);
  server.begin();

  // Aktifkan Task Watchdog Timer SETELAH WiFi Terhubung (Mencegah restart saat loading WiFi)
  // Ini akan mereset ESP32 jika loop() (atau fungsi yang dipanggilnya) nge-hang
  esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true); 
  esp_task_wdt_add(NULL); 

  // Otomatis cek pembaruan firmware 1x saat alat baru menyala
  cekUpdateGitHub(); 
  // (Proses download dan restart akan dieksekusi otomatis oleh loop() jika ada update)

  // Setup Pin LED sebagai Output
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  server.handleClient(); // Jaga agar server web tetap responsif

  // Beri makan Task Watchdog Timer agar tidak reset
  esp_task_wdt_reset();

  // Timer: Otomatis cek update setiap beberapa jam (Sesuai konstanta UPDATE_INTERVAL_MS)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > UPDATE_INTERVAL_MS) { 
    cekUpdateGitHub();
    lastCheck = millis();
  }

  // ---------------------------------------------------------
  // MASUKKAN LOGIKA RFID, KONTROL GERBANG & EMQX DI SINI
  // ---------------------------------------------------------

  // Logika Simulasi Gerbang via Touch Sensor
  static unsigned long gateOpenTime = 0;
  static bool isGateOpen = false;

  // Baca sensor sentuh (Touch0 ada di GPIO 4)
  int touchValue = touchRead(TOUCH_PIN);

  // Jika disentuh (nilai drop di bawah threshold) dan gerbang sedang tertutup
  if (touchValue < TOUCH_THRESHOLD && !isGateOpen) {
    Serial.printf("[SIMULASI] Sensor disentuh! Value: %d. Membuka gerbang...\n", touchValue);
    isGateOpen = true;
    gateOpenTime = millis();
    digitalWrite(LED_PIN, HIGH); // LED Nyala (Gerbang Terbuka)
  }

  // Jika gerbang sedang terbuka dan waktunya (3 detik) sudah habis
  if (isGateOpen && (millis() - gateOpenTime > GATE_OPEN_MS)) {
    Serial.println("[SIMULASI] Waktu habis. Menutup gerbang...");
    isGateOpen = false;
    digitalWrite(LED_PIN, LOW); // LED Mati (Gerbang Tertutup)
  }
}