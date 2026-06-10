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

String branchAktif = "main";
String savedApSSID = DEFAULT_AP_SSID;

// ========================================================================
// 2. FUNGSI OTA GITHUB
// ========================================================================
void cekUpdateGitHub() {
  Serial.println("\n[OTA] Memeriksa update di cabang GitHub: " + branchAktif);
  
  // Rakit URL secara dinamis berdasarkan konfigurasi di atas
  String urlVersi = String("https://raw.githubusercontent.com/") + GITHUB_USER + "/" + GITHUB_REPO + "/" + branchAktif + "/version.txt";
  String urlFirmware = String("https://raw.githubusercontent.com/") + GITHUB_USER + "/" + GITHUB_REPO + "/" + branchAktif + "/firmware.bin";

  WiFiClientSecure client;
  client.setInsecure(); // Bebas SSL

  HTTPClient http;
  http.begin(client, urlVersi);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String versiDiGitHub = http.getString();
    versiDiGitHub.trim(); 

    Serial.printf("[OTA] Versi ESP32   : %s\n", APP_VERSION);
    Serial.printf("[OTA] Versi GitHub  : %s\n", versiDiGitHub.c_str());

    if (versiDiGitHub != APP_VERSION && versiDiGitHub.length() > 0) {
      Serial.println("[OTA] >> UPDATE DITEMUKAN! Sedang menyedot firmware...");
      
      t_httpUpdate_return ret = httpUpdate.update(client, urlFirmware);

      if (ret == HTTP_UPDATE_OK) {
        Serial.println("[OTA] >> UPDATE SUKSES! ESP32 akan restart otomatis.");
      } else {
        Serial.printf("[OTA] >> GAGAL UPDATE: %s\n", httpUpdate.getLastErrorString().c_str());
      }
    } else {
      Serial.println("[OTA] >> Firmware sudah paling baru. Aman terkendali.");
    }
  } else {
    Serial.println("[OTA] >> Gagal menghubungi GitHub. Cek koneksi internet.");
  }
  http.end();
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
        <p>Jalur Update   : <strong style='color:#fff;'>{{BRANCH}}</strong></p>
      </div>
      
      <hr>
      <h3>Pengaturan Jalur Update OTA</h3>
      <form action='/save_branch' method='POST'>
        <select name='branch_name'>
          <option value='main' {{SEL_MAIN}}>MAIN (Jalur Stabil)</option>
          <option value='testing' {{SEL_TEST}}>TESTING (Jalur Eksperimen)</option>
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
  html.replace("{{BRANCH}}", branchAktif);
  html.replace("{{SEL_MAIN}}", branchAktif == "main" ? "selected" : "");
  html.replace("{{SEL_TEST}}", branchAktif == "testing" ? "selected" : "");

  server.send(200, "text/html", html);
}

void handleSaveBranch() {
  if (server.hasArg("branch_name")) {
    String newBranch = server.arg("branch_name");
    preferences.begin("gate_config", false);
    preferences.putString("ota_branch", newBranch);
    preferences.end();
    
    server.send(200, "text/html", "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='5;url=/'><style>body{background:#000;color:#fff;text-align:center;padding:50px;font-family:sans-serif;} a{background:#222;color:#fff;padding:12px 20px;text-decoration:none;border:1px solid #444;display:inline-block;margin-top:20px;font-weight:bold;}</style></head><body><h2>Jalur berhasil diubah ke: " + newBranch + "</h2><p style='color:#aaa;'>Sistem sedang di-restart...</p></body></html>");
    delay(2000);
    ESP.restart();
  }
}

void handleCekUpdate() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='2;url=/do_update'><style>body{background:#000;color:#fff;text-align:center;padding:50px;font-family:sans-serif;} .loader{border:4px solid #222;border-top:4px solid #fff;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;} @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head><body><h2>Menghubungi GitHub...</h2><div class='loader'></div><p style='color:#aaa;'>Sistem sedang memproses OTA, mohon tunggu sebentar...</p></body></html>";
  server.send(200, "text/html", html);
  delay(1000);
  cekUpdateGitHub();
}

// ========================================================================
// 4. SETUP & MAIN LOOP
// ========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Ambil data cabang (branch) dan nama AP WiFi dari memori internal
  preferences.begin("gate_config", true);
  branchAktif = preferences.getString("ota_branch", "main"); 
  savedApSSID = preferences.getString("ap_ssid", DEFAULT_AP_SSID); // Mengambil dari const di atas
  preferences.end();

  // Memulai WiFi Manager (Jika gagal konek, panggil AP dan Password dari const di atas)
  Serial.println("\n[WIFI] Mencoba terhubung ke jaringan...");
  wm.autoConnect(savedApSSID.c_str(), DEFAULT_AP_PASSWORD);
  Serial.println("[WIFI] Sukses Terhubung! IP Web Dashboard: " + WiFi.localIP().toString());

  // Routing Halaman Web
  server.on("/", handleRoot);
  server.on("/save_branch", HTTP_POST, handleSaveBranch);
  server.on("/cek_update", HTTP_GET, handleCekUpdate);
  server.begin();

  // Otomatis cek pembaruan firmware 1x saat alat baru menyala
  cekUpdateGitHub();
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