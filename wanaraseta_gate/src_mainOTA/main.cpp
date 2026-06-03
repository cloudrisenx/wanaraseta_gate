#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

// ========================================================================
// 1. ZONA KONFIGURASI UTAMA (EDIT PENGATURAN HANYA DI BAGIAN INI)
// ========================================================================

#define APP_VERSION         "1.0"               // Ganti angka ini setiap ada fitur baru!
#define GITHUB_USER         "cloudrisenx"       // Username GitHub kamu
#define GITHUB_REPO         "wanaraseta_gate"   // Nama Repository kamu

// Konfigurasi Hostpot Bawaan ESP32 (Saat pertama kali nyala / gagal konek)
#define DEFAULT_AP_SSID     "Wanara_Gate_Setup" 
#define DEFAULT_AP_PASSWORD "griyapersada"      

// Interval otomatis cek update ke GitHub (dalam milidetik, 7.200.000 = 2 Jam)
#define UPDATE_INTERVAL_MS  7200000             

// ========================================================================

WebServer server(80);
WiFiManager wm;
Preferences preferences;

String folderAktif = "ESP32main";
String savedApSSID = DEFAULT_AP_SSID;

// ========================================================================
// 2. FUNGSI OTA GITHUB
// ========================================================================
String cekUpdateGitHub() {
  String otaLog = "";
  otaLog += "[OTA] Memeriksa update di cabang GitHub: main\n";

  // Rakit nama folder berdasarkan pilihan yang disimpan via Web Dashboard
  String folderPath = String(GITHUB_REPO) + "/" + folderAktif;

  // Rakit URL secara dinamis berdasarkan konfigurasi di atas
  String urlFirmware = String("https://raw.githubusercontent.com/") + GITHUB_USER + "/" + GITHUB_REPO + "/main/" + folderPath + "/firmware.bin";

  WiFiClientSecure client;
  client.setInsecure(); // Bebas SSL

  otaLog += "[OTA] >> Memulai instalasi firmware dari: " + folderAktif + "\n";

  httpUpdate.rebootOnUpdate(false); // Cegah restart otomatis agar log bisa dikirim ke web
  t_httpUpdate_return ret = httpUpdate.update(client, urlFirmware);

  if (ret == HTTP_UPDATE_OK) {
    otaLog += "[OTA] >> INSTALASI SUKSES! ESP32 akan restart otomatis.\n";
  } else {
    otaLog += "[OTA] >> GAGAL INSTALASI: " + httpUpdate.getLastErrorString() + " (" + String(httpUpdate.getLastError()) + ")\n";
  }

  Serial.println(otaLog);
  return otaLog;
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
      select, input[type='submit'] { width: 100%; padding: 12px; margin-top: 10px; border-radius: 6px; font-weight: bold; cursor: pointer; border: none; }
      select { background-color: #252525; color: white; border: 1px solid #444; }
      .btn-orange { background-color: #ff9f43; color: #121212; }
      .btn-blue { background-color: #00adb5; color: #121212; margin-top: 25px; }
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
      <h3>Pengaturan Mesin/Board (Firmware Folder)</h3>
      <form action='/save_folder' method='POST'>
        <select name='folder_name'>
          <option value='ESP32main' {{SEL_ESP32MAIN}}>Mesin ESP32 (ESP32main)</option>
          <option value='ESP32_test' {{SEL_ESP32TEST}}>Mesin ESP32 Test (ESP32_test)</option>
          <option value='WT32main' {{SEL_WT32MAIN}}>Mesin WT32 (WT32main)</option>
          <option value='WT32_test' {{SEL_WT32TEST}}>Mesin WT32 Test (WT32_test)</option>
          <option value='src_mainOTA' {{SEL_SRCMAINOTA}}>Main OTA (src_mainOTA)</option>
        </select>
        <input type='submit' class='btn-orange' value='SIMPAN MESIN & RESTART'>
      </form>

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
    delay(2000);
    ESP.restart();
  }
}

void handleCekUpdate() {
  String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='2;url=/do_update'><style>body{background:#121212;color:#fff;text-align:center;padding:50px;font-family:Arial;} .loader{border:6px solid #1e1e1e;border-top:6px solid #00adb5;border-radius:50%;width:50px;height:50px;animation:spin 1s linear infinite;margin:20px auto;} @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head><body><h2 style='color:#00adb5;'>Menghubungi GitHub...</h2><div class='loader'></div><p>Sistem sedang memproses OTA, mohon tunggu sebentar...</p></body></html>";
  server.send(200, "text/html", html);
}

void handleDoUpdate() {
  String hasilLog = cekUpdateGitHub();
  
  String html = R"rawliteral(
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
      <h2>Hasil Proses Instalasi</h2>
      <pre>)rawliteral" + hasilLog + R"rawliteral(</pre>
      <a href='/' class='btn-orange'>KEMBALI KE DASHBOARD</a>
    </div>
  </body>
  </html>
  )rawliteral";

  server.send(200, "text/html", html);

  if (hasilLog.indexOf("SUKSES") > 0) {
    delay(2000);
    ESP.restart();
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
  
  // Untuk firmware "Installer", kita non-aktifkan pengecekan otomatis saat boot.
  // Instalasi hanya akan berjalan jika tombol di Web Dashboard ditekan.
  // cekUpdateGitHub(); 
}

void loop() {
  server.handleClient(); // Jaga agar portal web tetap responsif

  // Timer: Otomatis cek update setiap beberapa jam (Sesuai konstanta UPDATE_INTERVAL_MS)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > UPDATE_INTERVAL_MS) { 
    cekUpdateGitHub();
    lastCheck = millis();
  }

  // ---------------------------------------------------------
  // MASUKKAN LOGIKA RFID, KONTROL GERBANG & EMQX DI SINI
  // ---------------------------------------------------------
}