#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>      // Untuk Task Watchdog Timer
#include <Preferences.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>       // Library untuk parsing JSON
#include <time.h>              // Untuk timestamp
#include "soc/soc.h"           // Untuk kontrol Brownout
#include "soc/rtc_cntl_reg.h"  // Untuk kontrol Brownout

// ========================================================================
// 1. ZONA KONFIGURASI UTAMA
// ========================================================================

#define APP_VERSION         "3.11"               // Versi Firmware ESP32 WiFi
#define GITHUB_USER         "cloudrisenx"       // Username GitHub
#define GITHUB_REPO         "wanaraseta_gate"   // Nama Repository

#define DEFAULT_AP_SSID     "Wanara_Gate_Setup" 
#define DEFAULT_AP_PASSWORD "griyapersada"

#define RELAY1_PIN          2                   // Relay 1 (Gate In) / Built-in LED
#define RELAY2_PIN          27                  // Relay 2 (Gate Out / Alarm)
#define GATE_OPEN_MS        200                 // Durasi gerbang terbuka (200 ms)

// Konfigurasi Level Logika Relay (Active-LOW)
#define RELAY_ACTIVE        LOW
#define RELAY_DEACTIVE      HIGH

#define RFID_LED_PIN        26                  // LED Indikator saat RFID terbaca
#define RFID_LED_ON_MS      200                 // Durasi LED menyala saat scan (200 ms)

// Konfigurasi Watchdog Timer
#define WDT_TIMEOUT_SECONDS 5                   // Timeout WDT 5 detik

// Konfigurasi Barcode Scanner (UART2 default ESP32)
#define BARCODE_RX          16                  // RX2 (default ESP32)
#define BARCODE_TX          17                  // TX2 (default ESP32)
#define BARCODE_BAUD        9600

// Konfigurasi Pin SPI & RFID MFRC522 (VSPI default ESP32)
#define SS_PIN              5                   // VSPI SS
#define RST_PIN             22                  // RFID RST Pin
#define SPI_SCK             18                  // VSPI SCK
#define SPI_MISO            19                  // VSPI MISO
#define SPI_MOSI            23                  // VSPI MOSI

// Cooldown Pembacaan RFID (Milidetik)
#define COOLDOWN_SAME_CARD_MS    1000           // Cooldown untuk kartu yang sama
#define COOLDOWN_ANY_CARD_MS     0              // Tanpa jeda untuk kartu yang berbeda

// Cooldown Pembacaan Barcode (Milidetik)
#define COOLDOWN_SAME_BARCODE_MS 1000           // Fallback cooldown, scanner hardware sudah handle 3 detik

// Touch Pin untuk Reset WiFi Manager (tahan 10 detik)
#define TOUCH_PIN           4                   // GPIO4 / T0 - Capacitive Touch Pin
#define TOUCH_THRESHOLD     35                  // Nilai di bawah ini = pin disentuh

// ========================================================================
// 2. VARIABEL GLOBAL & INSTANSIASI
// ========================================================================

WebServer server(80);
Preferences preferences;

MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Konfigurasi MQTT Default (Akan ditimpa oleh preferensi yang tersimpan)
String mqtt_server     = "127.0.0.1";
int    mqtt_port       = 1883;
String mqtt_user       = "esp32";
String mqtt_pass       = "11223344";
String mqtt_client_id  = "gate_esp32_01";

// Tipe Device (Tersimpan di NVS)
String device_type     = "gate";               // "gate" atau "kasir"

// Touch Pin - Deteksi tahan untuk reset WiFi
unsigned long touchStartTime = 0;
bool isTouching = false;

WiFiManager wm;
String savedApSSID     = DEFAULT_AP_SSID;

String folderAktif     = "ESP32main";

// Penundaan restart untuk respon HTTP bersih
bool pendingRestart = false;
unsigned long restartTime = 0;

String rfidStatus      = "Mencari Sensor...";
String lastRfidScan    = "";
String lastBarcodeScan = "";
String rfid_master     = "A166C820";
String lastScannedUid     = "";
unsigned long lastScannedTime  = 0;
String lastScannedBarcode = "";
unsigned long lastBarcodeTime  = 0;

// Status & Timer Relay
unsigned long relay1ActiveTime = 0;
bool isRelay1Active = false;
unsigned long relay2ActiveTime = 0;
bool isRelay2Active = false;
bool pendingRfidReinit = false;
unsigned long pendingRfidReinitTime = 0;

// Status & Timer LED RFID
unsigned long rfidLedActiveTime = 0;
bool isRfidLedActive = false;

unsigned long lastMqttReconnectAttempt = 0;

// Forward Declarations untuk menghindari error undeclared scope
void prepareForOTA();
void restoreAfterOTA();
void cekUpdateGitHub(bool fromWeb = false);
void triggerRelay(int relayNum);
void handleGate();
void reinitRFID();

// ========================================================================
// KONTROL RESET & RE-INISIALISASI RFID
// ========================================================================
void reinitRFID() {
  Serial.println("[RFID] Re-inisialisasi sensor RFID...");
  
  // Hardware Reset Pulsa via RST Pin
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(10);
  digitalWrite(RST_PIN, HIGH);
  delay(50);

  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  delay(10);
}

// ========================================================================
// 3. KONTROL POWER-SAVING & ANTI-BROWNOUT
// ========================================================================
void prepareForOTA() {
  Serial.println("[OTA] Menyiapkan sistem untuk update (Mode Hemat Daya & Anti-Brownout)...");
  
  // 1. Matikan relay agar tidak membebani daya
  digitalWrite(RELAY1_PIN, RELAY_DEACTIVE);
  digitalWrite(RELAY2_PIN, RELAY_DEACTIVE);
  isRelay1Active = false;
  isRelay2Active = false;
  
  // 2. Putuskan MQTT dan bersihkan koneksi socket untuk membebaskan RAM
  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  espClient.stop();
  delay(100);
  
  // 3. Matikan antena RFID untuk memotong konsumsi arus
  mfrc522.PCD_AntennaOff();
  delay(50);
  
  // 4. Nonaktifkan detektor Brownout secara software
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  delay(50);
  
  // 5. Nonaktifkan Watchdog Timer (WDT) agar tidak reset saat flashing
  esp_task_wdt_delete(NULL); 
  yield();
}

void restoreAfterOTA() {
  Serial.println("[OTA] Mengembalikan konfigurasi daya & WDT...");
  
  // 1. Aktifkan kembali detektor Brownout
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);
  delay(10);
  
  // 2. Aktifkan kembali Watchdog Timer (WDT)
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
  
  // 3. Inisialisasi ulang SPI & RFID
  SPI.end();
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  mfrc522.PCD_Init();
  delay(10);
  mfrc522.PCD_AntennaOn();
}

// ========================================================================
// 4. OTA UPDATE GITHUB (HTTPS DENGAN OPTIMASI MEMORI)
// ========================================================================
void cekUpdateGitHub(bool fromWeb) {
  auto logMsg = [&](String msg) {
    Serial.print(msg);
    if (fromWeb) server.sendContent(msg);
  };

  logMsg("[OTA] Memeriksa update di cabang GitHub: main\n");
  String folderPath = folderAktif;
  String urlVersi = String("https://raw.githubusercontent.com/") + GITHUB_USER + "/" + GITHUB_REPO + "/main/wanaraseta_gate/" + folderPath + "/version.txt";
  String urlFirmware = String("https://raw.githubusercontent.com/") + GITHUB_USER + "/" + GITHUB_REPO + "/main/wanaraseta_gate/" + folderPath + "/firmware.bin";

  bool doUpdate = false;
  String versiDiGitHub = "";

  // Step 1: Cek Versi (Gunakan scope agar memory client secure dibebaskan setelah cek)
  {
    WiFiClientSecure client;
    client.setInsecure(); // Bypass SSL verification untuk menghemat RAM

    HTTPClient http;
    http.begin(client, urlVersi);
    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      versiDiGitHub = http.getString();
      versiDiGitHub.trim();

      logMsg("[OTA] Versi ESP32   : " + String(APP_VERSION) + "\n");
      logMsg("[OTA] Versi GitHub  : " + versiDiGitHub + "\n");

      if (versiDiGitHub != APP_VERSION && versiDiGitHub.length() > 0) {
        logMsg("[OTA] >> UPDATE DITEMUKAN! File terbaru tersedia.\n");
        logMsg("[OTA] >> Menyiapkan sistem untuk mengunduh firmware...\n");
        doUpdate = true;
      } else {
        logMsg("[OTA] >> Firmware sudah paling baru. Aman terkendali.\n");
      }
    } else {
      logMsg("[OTA] >> Gagal menghubungi GitHub (HTTP: " + String(httpCode) + "). Periksa koneksi internet.\n");
    }
    http.end();
  }
  
  if (doUpdate) {
    // Jalankan shutdown peripheral & brownout
    prepareForOTA();
    delay(100);

    WiFiClientSecure updateClient;
    updateClient.setInsecure();

    httpUpdate.rebootOnUpdate(true); // Otomatis restart setelah update sukses
    
    t_httpUpdate_return ret = httpUpdate.update(updateClient, urlFirmware);
    
    // Jika baris di bawah tereksekusi, berarti update GAGAL
    restoreAfterOTA();

    if (ret == HTTP_UPDATE_OK) {
      logMsg("\n[OTA] >> UPDATE SUKSES! Board akan restart otomatis.\n");
    } else {
      logMsg("\n[OTA] >> GAGAL UPDATE: " + httpUpdate.getLastErrorString() + " (Code: " + String(httpUpdate.getLastError()) + ")\n");
    }
  }
}

// ========================================================================
// 5. DASHBOARD WEB & API (HTML DENGAN GLASSMORPHISM & AJAX)
// ========================================================================

// Template Web Dashboard di Flash (PROGMEM)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Wanara Seta - Gate Portal</title>
  <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;700&family=JetBrains+Mono:wght@400;700&display=swap" rel="stylesheet">
  <style>
    :root {
      --bg-color: #0d0e15;
      --card-bg: rgba(22, 25, 41, 0.75);
      --border-color: rgba(255, 255, 255, 0.08);
      --text-main: #f3f4f6;
      --text-muted: #9ca3af;
      --primary: #6366f1;
      --primary-hover: #4f46e5;
      --success: #10b981;
      --warning: #f59e0b;
      --danger: #ef4444;
    }
    body {
      margin: 0;
      padding: 0;
      background-color: var(--bg-color);
      background-image: 
        radial-gradient(circle at top right, rgba(99, 102, 241, 0.12), transparent 45%),
        radial-gradient(circle at bottom left, rgba(16, 185, 129, 0.06), transparent 45%);
      color: var(--text-main);
      font-family: 'Outfit', sans-serif;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: flex-start;
    }
    .app-container {
      width: 100%;
      max-width: 1100px;
      padding: 24px;
      box-sizing: border-box;
    }
    header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 24px;
      border-bottom: 1px solid var(--border-color);
      padding-bottom: 16px;
    }
    .brand h1 {
      margin: 0;
      font-size: 22px;
      font-weight: 700;
      letter-spacing: 0.5px;
      background: linear-gradient(135deg, #ffffff 40%, #a5b4fc 100%);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
    }
    .brand p {
      margin: 4px 0 0 0;
      font-size: 13px;
      color: var(--text-muted);
    }
    .badge-group {
      display: flex;
      gap: 8px;
    }
    .badge {
      padding: 4px 10px;
      border-radius: 9999px;
      font-size: 11px;
      font-weight: 600;
      border: 1px solid rgba(255, 255, 255, 0.1);
      background: rgba(255, 255, 255, 0.05);
    }
    .badge-primary {
      background: rgba(99, 102, 241, 0.15);
      border-color: rgba(99, 102, 241, 0.3);
      color: #a5b4fc;
    }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
      gap: 20px;
      margin-bottom: 20px;
    }
    .card {
      background: var(--card-bg);
      backdrop-filter: blur(12px);
      -webkit-backdrop-filter: blur(12px);
      border: 1px solid var(--border-color);
      border-radius: 16px;
      padding: 20px;
      box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.37);
      transition: transform 0.2s, box-shadow 0.2s;
    }
    .card:hover {
      transform: translateY(-2px);
      box-shadow: 0 12px 40px 0 rgba(0, 0, 0, 0.5);
    }
    .card-title {
      margin: 0 0 16px 0;
      font-size: 15px;
      font-weight: 600;
      display: flex;
      align-items: center;
      gap: 8px;
      color: #ffffff;
      border-bottom: 1px solid rgba(255, 255, 255, 0.06);
      padding-bottom: 8px;
    }
    .card-title svg {
      width: 18px;
      height: 18px;
      color: var(--primary);
      flex-shrink: 0;
    }
    .status-list {
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .status-item {
      display: flex;
      justify-content: space-between;
      align-items: center;
      font-size: 13.5px;
    }
    .status-label {
      color: var(--text-muted);
    }
    .status-value {
      font-weight: 600;
      display: flex;
      align-items: center;
      gap: 6px;
    }
    .status-indicator {
      width: 7px;
      height: 7px;
      border-radius: 50%;
      display: inline-block;
    }
    .indicator-green { background-color: var(--success); box-shadow: 0 0 8px var(--success); }
    .indicator-orange { background-color: var(--warning); box-shadow: 0 0 8px var(--warning); }
    .indicator-red { background-color: var(--danger); box-shadow: 0 0 8px var(--danger); }
    .text-green { color: var(--success); }
    .text-orange { color: var(--warning); }
    .text-red { color: var(--danger); }
    .text-muted { color: var(--text-muted); }
    .mono {
      font-family: 'JetBrains Mono', monospace;
      font-size: 12.5px;
      background: rgba(0,0,0,0.3);
      padding: 2px 6px;
      border-radius: 4px;
      color: #e0e7ff;
    }
    .btn {
      background: linear-gradient(135deg, var(--primary), var(--primary-hover));
      color: #ffffff;
      border: none;
      padding: 11px 18px;
      border-radius: 8px;
      font-weight: 600;
      font-size: 13.5px;
      cursor: pointer;
      width: 100%;
      transition: opacity 0.2s, transform 0.1s;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      box-sizing: border-box;
      font-family: inherit;
    }
    .btn:hover { opacity: 0.9; }
    .btn:active { transform: scale(0.98); }
    .btn-secondary {
      background: rgba(255, 255, 255, 0.06);
      border: 1px solid rgba(255, 255, 255, 0.08);
      color: var(--text-main);
    }
    .btn-secondary:hover { background: rgba(255, 255, 255, 0.1); }
    .btn-danger { background: linear-gradient(135deg, var(--danger), #be123c); }
    .control-row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      margin-top: 12px;
    }
    .form-group {
      margin-bottom: 12px;
    }
    .form-group label {
      display: block;
      font-size: 11px;
      color: var(--text-muted);
      margin-bottom: 5px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .form-control {
      width: 100%;
      padding: 9px 12px;
      border-radius: 6px;
      background: rgba(0, 0, 0, 0.3);
      border: 1px solid var(--border-color);
      color: #ffffff;
      font-family: inherit;
      font-size: 13px;
      box-sizing: border-box;
    }
    .form-control:focus { outline: none; border-color: var(--primary); }
    select.form-control {
      appearance: none;
      background-image: url("data:image/svg+xml;utf8,<svg fill='white' height='18' viewBox='0 0 24 24' width='18' xmlns='http://www.w3.org/2000/svg'><path d='M7 10l5 5 5-5z'/></svg>");
      background-repeat: no-repeat;
      background-position: right 8px center;
      padding-right: 24px;
    }
    .file-input-wrapper {
      position: relative;
      border: 2px dashed rgba(255, 255, 255, 0.12);
      border-radius: 8px;
      padding: 18px;
      text-align: center;
      background: rgba(0, 0, 0, 0.2);
      cursor: pointer;
    }
    .file-input-wrapper input[type="file"] {
      position: absolute;
      top: 0; left: 0; width: 100%; height: 100%; opacity: 0; cursor: pointer;
    }
    .file-input-text { font-size: 12px; color: var(--text-muted); }
    .progress-container { margin-top: 12px; display: none; }
    .progress-track { background: rgba(0, 0, 0, 0.4); height: 8px; border-radius: 4px; overflow: hidden; border: 1px solid rgba(255,255,255,0.03); }
    .progress-bar { background: linear-gradient(90deg, var(--primary), var(--success)); width: 0%; height: 100%; transition: width 0.1s ease-out; }
    .progress-info { display: flex; justify-content: space-between; font-size: 11px; margin-top: 5px; color: var(--text-muted); }
    
    .overlay {
      position: fixed;
      top: 0; left: 0; width: 100%; height: 100%;
      background: rgba(11, 12, 17, 0.96);
      z-index: 9999;
      display: flex; flex-direction: column; justify-content: center; align-items: center;
      display: none;
    }
    .spinner {
      width: 44px; height: 44px;
      border: 3px solid rgba(255, 255, 255, 0.08);
      border-top: 3px solid var(--primary);
      border-radius: 50%;
      animation: spin 1s linear infinite;
      margin-bottom: 16px;
    }
    @keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    .overlay h2 { margin: 0 0 6px 0; font-size: 18px; font-weight: 600; }
    .overlay p { margin: 0; color: var(--text-muted); font-size: 13px; }
    
    .toast {
      position: fixed;
      bottom: 20px; right: 20px;
      background: rgba(22, 25, 41, 0.96);
      border: 1px solid var(--success);
      border-radius: 8px;
      padding: 10px 20px;
      color: #ffffff;
      box-shadow: 0 8px 24px rgba(0,0,0,0.5);
      display: flex; align-items: center; gap: 8px;
      transform: translateY(100px); opacity: 0;
      transition: transform 0.3s, opacity 0.3s;
      font-size: 13px;
      z-index: 999;
    }
    .toast.show { transform: translateY(0); opacity: 1; }

    /* ── Landscape Mobile (tinggi layar < 500px) ── */
    @media (max-height: 500px) and (orientation: landscape) {
      .app-container { padding: 10px 14px; }
      header { margin-bottom: 10px; padding-bottom: 8px; }
      .brand h1 { font-size: 15px; }
      .brand p { display: none; }
      .badge { padding: 3px 8px; font-size: 10px; }
      .grid { gap: 10px; margin-bottom: 10px; }
      .card { padding: 12px 14px; border-radius: 10px; }
      .card:hover { transform: none; box-shadow: none; }
      .card-title { font-size: 12.5px; margin-bottom: 8px; padding-bottom: 6px; gap: 6px; }
      .card-title svg { width: 14px; height: 14px; }
      .status-list { gap: 7px; }
      .status-item { font-size: 12px; }
      .mono { font-size: 11px; padding: 1px 5px; }
      .btn { padding: 7px 12px; font-size: 12px; }
      .form-group { margin-bottom: 8px; }
      .form-group label { font-size: 10px; margin-bottom: 3px; }
      .form-control { padding: 7px 10px; font-size: 12px; }
      .control-row { gap: 8px; }
    }

    /* ── Portrait Mobile (lebar < 480px) ── */
    @media (max-width: 480px) {
      .app-container { padding: 14px; }
      header { flex-direction: column; align-items: flex-start; gap: 8px; }
      .grid { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="app-container">
    <header>
      <div class="brand">
        <h1>🏢 Wanara Seta Gate Portal</h1>
        <p>Sistem Gate Controller ESP32 S4 38-Pin</p>
      </div>
      <div class="badge-group">
        <span class="badge badge-primary">Firmware v<span id="version-text">{{VERSION}}</span></span>
        <span class="badge" id="folder-badge">{{FOLDER}}</span>
      </div>
    </header>

    <div class="grid">
      <!-- Card Status Hardware & Sistem -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M9 3v2m6-2v2M9 19v2m6-2v2M5 9H3m2 6H3m18-6h-2m2 6h-2M7 19h10a2 2 0 002-2V7a2 2 0 00-2-2H7a2 2 0 00-2 2v10a2 2 0 002 2zM9 9h6v6H9V9z"></path></svg>
          Status Hardware & Sistem
        </h3>
        <div class="status-list">
          <div class="status-item">
            <span class="status-label">Uptime</span>
            <span class="status-value mono" id="uptime">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">Free Heap RAM</span>
            <span class="status-value mono" id="free-heap">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">Min Free Heap</span>
            <span class="status-value mono" id="min-free-heap">Loading...</span>
          </div>
          <div class="status-item" style="margin-top: 6px;">
            <button class="btn btn-secondary btn-danger" onclick="rebootDevice()">
              <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg" style="width:14px;height:14px;"><path stroke-linecap="round" stroke-linejoin="round" d="M11.25 11.25l.041-.02a.75.75 0 111.086 1.086L10.5 14.25h3.75a4.5 4.5 0 010 9H8.25m.75-12h-.008v-.008H9v.008z"></path></svg>
              Reboot Board
            </button>
          </div>
        </div>
      </div>

      <!-- Card RFID Scanner -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M15 7a2 2 0 012 2m4 0a6 6 0 01-7.743 5.743L11 17H9v2H7v2H4a1 1 0 01-1-1v-2.586a1 1 0 01.293-.707l5.964-5.964A6 6 0 1121 9z"></path></svg>
          RFID Scanner (MFRC522)
        </h3>
        <div class="status-list">
          <div class="status-item">
            <span class="status-label">Status Koneksi</span>
            <span class="status-value text-green" id="rfid-status-val">
              <span class="status-indicator indicator-green" id="rfid-status-ind"></span>
              Loading...
            </span>
          </div>
          <div class="status-item">
            <span class="status-label">Versi Register PCD</span>
            <span class="status-value mono" id="rfid-version">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">Scan UID Terakhir</span>
            <span class="status-value mono" id="last-rfid" style="color: #6366f1;">Belum ada kartu</span>
          </div>
        </div>
      </div>

      <!-- Card Barcode Scanner -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M3 5v14M7 5v14M11 5v14M15 5v14M17 5v14M21 5v14"></path></svg>
          Barcode Scanner (UART2)
        </h3>
        <div class="status-list">
          <div class="status-item">
            <span class="status-label">Status Koneksi</span>
            <span class="status-value text-green" id="barcode-status-val">
              <span class="status-indicator indicator-green" id="barcode-status-ind"></span>
              ✅ AKTIF
            </span>
          </div>
          <div class="status-item">
            <span class="status-label">Scan Barcode Terakhir</span>
            <span class="status-value mono" id="last-barcode" style="color: #10b981;">Belum ada barcode</span>
          </div>
        </div>
      </div>

      <!-- Card Jaringan WiFi & MQTT -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M3.055 11H5a2 2 0 012 2v1a2 2 0 002 2 2 2 0 012 2v2.945M8 3.935V5.5A2.5 2.5 0 0010.5 8h.5a2 2 0 012 2 2 2 0 002 2h2m-4-3h1.286a2 2 0 00.707-.13l1.581-.79a2 2 0 00.41-.318l1.361-1.36a2 2 0 00.318-.41L21 6.5"></path></svg>
          Koneksi & MQTT Broker
        </h3>
        <div class="status-list">
          <div class="status-item">
            <span class="status-label">Koneksi WiFi</span>
            <span class="status-value text-green" id="wifi-status-val">
              <span class="status-indicator indicator-green" id="wifi-status-ind"></span>
              Loading...
            </span>
          </div>
          <div class="status-item">
            <span class="status-label">SSID WiFi</span>
            <span class="status-value mono" id="wifi-ssid">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">Kekuatan Sinyal</span>
            <span class="status-value mono" id="wifi-rssi">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">Alamat IP Board</span>
            <span class="status-value mono" id="wifi-ip">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">EMQX MQTT Broker</span>
            <span class="status-value text-green" id="mqtt-status-val">
              <span class="status-indicator indicator-green" id="mqtt-status-ind"></span>
              Loading...
            </span>
          </div>
          <div class="status-item">
            <span class="status-label">Alamat Broker</span>
            <span class="status-value mono" id="mqtt-broker">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">MQTT Client ID</span>
            <span class="status-value mono" id="mqtt-client-id">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">Tipe Device</span>
            <span class="status-value mono" id="device-type-val">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">📤 Publish Topic</span>
            <span class="status-value mono" id="topic-pub-val" style="color:#10b981;font-size:11px;">Loading...</span>
          </div>
          <div class="status-item">
            <span class="status-label">📥 Subscribe Topic</span>
            <span class="status-value mono" id="topic-sub-val" style="color:#a5b4fc;font-size:11px;">Loading...</span>
          </div>
          <div class="control-row" style="margin-top: 16px;">
            <button class="btn btn-secondary" onclick="reconnectWifi()" style="font-size: 12px; padding: 6px;">🔄 Reconnect WiFi</button>
            <button class="btn btn-secondary btn-danger" onclick="forgetWifi()" style="font-size: 12px; padding: 6px;">🗑️ Lupakan WiFi</button>
          </div>
          <div class="status-item" style="margin-top:10px;border-top:1px solid rgba(255,255,255,0.05);padding-top:10px;">
            <span class="status-label">Reset WiFi Manager</span>
            <span class="status-value mono text-orange" id="touch-status-val" style="font-size:11px;">GPIO4 — tahan 10 dtk</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Card Output / Relay & Manual Trigger -->
    <div class="card" style="margin-bottom: 24px;">
      <h3 class="card-title">
        <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M8 11V7a4 4 0 118 0m-4 8v2m-6 4h12a2 2 0 002-2v-6a2 2 0 00-2-2H6a2 2 0 00-2 2v6a2 2 0 002 2z"></path></svg>
        Kontrol Manual Relay Gerbang
      </h3>
      <div class="grid" style="grid-template-columns: 1fr 1fr; margin-bottom: 0;">
        <div class="status-list" style="justify-content: center;">
          <div class="status-item">
            <span class="status-label">Gerbang 1 (Relay 1 - GPIO 2)</span>
            <span class="status-value text-muted" id="r1-status">⚡ STANDBY</span>
          </div>
          <div class="status-item">
            <span class="status-label">Gerbang 2 (Relay 2 - GPIO 27)</span>
            <span class="status-value text-muted" id="r2-status">⚡ STANDBY</span>
          </div>
        </div>
        <div class="control-row" style="margin-top: 0; display: flex; gap: 10px; justify-content: flex-start; align-items: center; align-content: center; flex-wrap: wrap; width: 100%;">
          <button class="btn" onclick="triggerRelay(1)" style="width: auto; padding: 8px 16px; font-size: 13px;">
            🔓 Buka Gerbang 1 (R1)
          </button>
          <button class="btn btn-secondary" onclick="triggerRelay(2)" style="width: auto; padding: 8px 16px; font-size: 13px;">
            🔓 Buka Gerbang 2 (R2)
          </button>
        </div>
      </div>
    </div>

    <div class="grid">
      <!-- Card Konfigurasi Target Firmware & Update -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M7 16a4 4 0 01-.88-7.903A5 5 0 1115.9 6L16 6a5 5 0 011 9.9M9 19l3 3m0 0l3-3m-3 3V10"></path></svg>
          GitHub OTA & Target Mesin
        </h3>
        <form id="form-folder">
          <div class="form-group">
            <label>Pilih Target Board (Folder GitHub)</label>
            <select name="folder_name" class="form-control">
              <option value="ESP32main" {{SEL_ESP32MAIN}}>Mesin ESP32 (ESP32main)</option>
              <option value="ESP32_test" {{SEL_ESP32TEST}}>Mesin ESP32 Test (ESP32_test)</option>
              <option value="WT32main" {{SEL_WT32MAIN}}>Mesin WT32 (WT32main)</option>
              <option value="WT32_test" {{SEL_WT32TEST}}>Mesin WT32 Test (WT32_test)</option>
              <option value="src_mainOTA" {{SEL_SRCMAINOTA}}>Main OTA (src_mainOTA)</option>
            </select>
          </div>
          <button type="submit" class="btn" style="margin-bottom: 12px;">Simpan Target & Restart</button>
        </form>
        <button class="btn btn-secondary" onclick="window.location.href='/cek_update'">
          📥 Cek Update GitHub Sekarang
        </button>
      </div>

      <!-- Card Konfigurasi MQTT -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z"></path><path stroke-linecap="round" stroke-linejoin="round" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z"></path></svg>
          Pengaturan MQTT Broker
        </h3>
        <form id="form-mqtt">
          <div class="form-group">
            <label>IP Server Broker</label>
            <input type="text" name="mqtt_server" class="form-control" placeholder="192.168.1.50" value="{{MQTT_SERVER}}" required>
          </div>
          <div class="form-group">
            <label>Port Broker</label>
            <input type="number" name="mqtt_port" class="form-control" placeholder="1883" value="{{MQTT_PORT}}" required>
          </div>
          <div class="form-group">
            <label>Username MQTT</label>
            <input type="text" name="mqtt_user" class="form-control" value="{{MQTT_USER}}">
          </div>
          <div class="form-group">
            <label>Password MQTT</label>
            <input type="password" name="mqtt_pass" class="form-control" value="{{MQTT_PASS}}">
          </div>
          <div class="form-group">
            <label>Client ID (Device ID)</label>
            <input type="text" name="mqtt_client_id" class="form-control" value="{{MQTT_CLIENT_ID}}" required>
          </div>
          <div class="form-group">
            <label>Tipe Device</label>
            <select name="device_type" class="form-control" id="sel-device-type">
              <option value="gate" {{SEL_DEVICE_GATE}}>Gate (Gerbang)</option>
              <option value="kasir" {{SEL_DEVICE_KASIR}}>Kasir</option>
            </select>
          </div>

          <div class="form-group">
            <label>Topic MQTT Aktif</label>
            <div style="background:rgba(0,0,0,0.3);border:1px solid var(--border-color);border-radius:6px;padding:8px 10px;">
              <div style="font-size:11.5px;margin-bottom:4px;"><span style="color:#9ca3af;">📤 Publish:</span> <span class="mono" style="color:#10b981;">{{TOPIC_PUB}}</span></div>
              <div style="font-size:11.5px;"><span style="color:#9ca3af;">📥 Subscribe:</span> <span class="mono" style="color:#a5b4fc;">{{TOPIC_SUB}}</span></div>
            </div>
          </div>
          <button type="submit" class="btn">Simpan MQTT & Restart</button>
        </form>
      </div>

      <!-- Card Konfigurasi Kartu Master -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M15 7a2 2 0 012 2m4 0a6 6 0 01-7.743 5.743L11 17H9v2H7v2H4a1 1 0 01-1-1v-2.586a1 1 0 01.293-.707l5.964-5.964A6 6 0 1121 9z"></path></svg>
          Pengaturan Kartu Master (Bypass)
        </h3>
        <form id="form-master">
          <div class="form-group">
            <label>UID Kartu Master</label>
            <input type="text" name="rfid_master" class="form-control" placeholder="A166C820" value="{{RFID_MASTER}}" required>
          </div>
          <button type="submit" class="btn">Simpan Master & Restart</button>
        </form>
      </div>

      <!-- Card Upload Firmware Manual -->
      <div class="card">
        <h3 class="card-title">
          <svg fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><path stroke-linecap="round" stroke-linejoin="round" d="M4 16v1a3 3 0 003 3h10a3 3 0 003-3v-1m-4-8l-4-4m0 0L8 8m4-4v12"></path></svg>
          Upload Firmware Manual (.bin)
        </h3>
        <form id="form-upload">
          <div class="form-group">
            <label>Pilih File Firmware .bin</label>
            <div class="file-input-wrapper">
              <input type="file" name="update" id="file-input" accept=".bin">
              <span class="file-input-text" id="file-name-display">Seret file ke sini atau klik untuk memilih</span>
            </div>
          </div>
          <button type="submit" class="btn">Upload & Flash</button>
        </form>

        <div class="progress-container" id="progress-container">
          <div class="progress-track">
            <div class="progress-bar" id="progress-bar"></div>
          </div>
          <div class="progress-info">
            <span id="progress-status">Menyiapkan...</span>
            <span id="progress-percent">0%</span>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- Loading Overlay -->
  <div class="overlay" id="overlay">
    <div class="spinner"></div>
    <h2 id="overlay-title">Memproses...</h2>
    <p id="overlay-subtitle">Menghubungi board ESP32 kembali dalam <span id="overlay-countdown">--</span></p>
  </div>

  <!-- Toast -->
  <div class="toast" id="toast">Gerbang Terbuka!</div>

  <script>
    function updateUI(data) {
      document.getElementById('version-text').innerText = data.version;
      document.getElementById('folder-badge').innerText = data.folder;
      document.getElementById('free-heap').innerText = data.free_heap.toLocaleString() + ' Bytes';
      document.getElementById('min-free-heap').innerText = data.min_free_heap.toLocaleString() + ' Bytes';

      // Format Uptime
      let uptimeSec = data.uptime;
      let hrs = Math.floor(uptimeSec / 3600);
      let mins = Math.floor((uptimeSec % 3600) / 60);
      let secs = uptimeSec % 60;
      document.getElementById('uptime').innerText = (hrs > 0 ? hrs + 'j ' : '') + mins + 'm ' + secs + 's';

      // RFID Status UI
      let rfidVal = document.getElementById('rfid-status-val');
      let rfidInd = document.getElementById('rfid-status-ind');
      rfidVal.innerText = data.rfid_status;
      if (data.rfid_status.indexOf('✅') !== -1) {
        rfidVal.className = 'status-value text-green';
        rfidInd.className = 'status-indicator indicator-green';
      } else {
        rfidVal.className = 'status-value text-red';
        rfidInd.className = 'status-indicator indicator-red';
      }
      document.getElementById('rfid-version').innerText = data.rfid_version;
      document.getElementById('last-rfid').innerText = data.last_rfid || 'Belum ada kartu';
      document.getElementById('last-barcode').innerText = data.last_barcode || 'Belum ada barcode';

      // WiFi UI
      let wifiVal = document.getElementById('wifi-status-val');
      let wifiInd = document.getElementById('wifi-status-ind');
      let wifiCon = (data.wifi_status === 'Connected');
      wifiVal.innerText = wifiCon ? '✅ Terhubung' : '❌ Terputus';
      if (wifiCon) {
        wifiVal.className = 'status-value text-green';
        wifiInd.className = 'status-indicator indicator-green';
      } else {
        wifiVal.className = 'status-value text-red';
        wifiInd.className = 'status-indicator indicator-red';
      }
      document.getElementById('wifi-ssid').innerText = data.wifi_ssid || '-';
      document.getElementById('wifi-rssi').innerText = data.wifi_rssi ? data.wifi_rssi + ' dBm' : '-';
      document.getElementById('wifi-ip').innerText = data.wifi_ip;

      // MQTT UI
      let mqttVal = document.getElementById('mqtt-status-val');
      let mqttInd = document.getElementById('mqtt-status-ind');
      let mqttCon = (data.mqtt_status === 'Connected');
      mqttVal.innerText = mqttCon ? '✅ Terhubung' : '⚠️ Offline';
      if (mqttCon) {
        mqttVal.className = 'status-value text-green';
        mqttInd.className = 'status-indicator indicator-green';
      } else {
        mqttVal.className = 'status-value text-orange';
        mqttInd.className = 'status-indicator indicator-orange';
      }
      document.getElementById('mqtt-broker').innerText = data.mqtt_broker + ':' + data.mqtt_port;
      document.getElementById('mqtt-client-id').innerText = data.mqtt_client_id;
      document.getElementById('device-type-val').innerText = (data.device_type || '-').toUpperCase();
      document.getElementById('topic-pub-val').innerText = data.mqtt_topic_pub || '-';
      document.getElementById('topic-sub-val').innerText = data.mqtt_topic_sub || '-';
      let touchEl = document.getElementById('touch-status-val');
      if (data.touch_active) {
        touchEl.innerText = '⚠️ DISENTUH — segera reset!';
        touchEl.className = 'status-value mono text-orange';
      } else {
        touchEl.innerText = 'GPIO4 — tahan 10 dtk';
        touchEl.className = 'status-value mono text-muted';
      }

      // Relays UI
      let r1 = document.getElementById('r1-status');
      r1.innerText = data.relay1 ? '✅ AKTIF (Pulse)' : '⚡ STANDBY';
      r1.className = data.relay1 ? 'status-value text-green' : 'status-value text-muted';

      let r2 = document.getElementById('r2-status');
      r2.innerText = data.relay2 ? '✅ AKTIF (Pulse)' : '⚡ STANDBY';
      r2.className = data.relay2 ? 'status-value text-green' : 'status-value text-muted';
    }

    function fetchStatus() {
      fetch('/status_data?t=' + Date.now())
        .then(res => res.json())
        .then(data => updateUI(data))
        .catch(err => console.error('Error fetching status data:', err));
    }

    setInterval(fetchStatus, 1500);
    fetchStatus();

    function showToast(message, isSuccess = true) {
      let toast = document.getElementById('toast');
      toast.innerText = message;
      toast.style.borderColor = isSuccess ? 'var(--success)' : 'var(--danger)';
      toast.className = 'toast show';
      setTimeout(() => { toast.className = 'toast'; }, 3000);
    }

    function showOverlay(title, subtitle, seconds) {
      let overlay = document.getElementById('overlay');
      let titleEl = document.getElementById('overlay-title');
      let subtitleEl = document.getElementById('overlay-subtitle');
      let countdown = document.getElementById('overlay-countdown');

      titleEl.innerText = title;
      subtitleEl.innerHTML = subtitle + ' kembali dalam <span id="overlay-countdown"></span>';
      
      overlay.style.display = 'flex';
      let rem = seconds;
      
      let el = document.getElementById('overlay-countdown');
      el.innerText = rem + ' detik...';

      let iv = setInterval(() => {
        rem--;
        if (rem <= 0) {
          clearInterval(iv);
          window.location.href = '/';
        } else {
          el.innerText = rem + ' detik...';
        }
      }, 1000);
    }

    function reconnectWifi() {
      fetch('/reconnect_wifi')
        .then(() => showToast('Mencoba menghubungkan ulang WiFi...'))
        .catch(() => showToast('Gagal mengirim perintah', false));
    }

    function forgetWifi() {
      if (confirm('Yakin ingin menghapus WiFi? Alat akan restart ke mode setup Hotspot.')) {
        fetch('/forget_wifi')
          .then(() => showOverlay('Menghapus WiFi', 'Alat akan restart dan masuk ke mode Setup (Wanara_Gate_Setup)', 15))
          .catch(() => showToast('Gagal mereset WiFi', false));
      }
    }

    function triggerRelay(num) {
      let r = document.getElementById('r' + num + '-status');
      if (r) {
        r.innerText = '⏳ MENGIRIM...';
        r.className = 'status-value text-orange';
      }
      fetch('/trigger_relay?relay=' + num + '&t=' + Date.now())
        .then(res => {
          if (res.ok) {
            showToast('Relay ' + num + ' Berhasil Dipicu!');
            if (r) {
              r.innerText = num === 1 ? '✅ DIPICU' : '🔔 DIPICU';
              r.className = num === 1 ? 'status-value text-green' : 'status-value text-orange';
            }
          } else {
            showToast('Gagal memicu relay', false);
            if (r) {
              r.innerText = '❌ GAGAL';
              r.className = 'status-value text-red';
            }
          }
        })
        .catch(() => showToast('Error komunikasi', false));
    }

    function rebootDevice() {
      if (confirm('Merestart board ESP32?')) {
        fetch('/reboot')
          .then(() => showOverlay('Rebooting Device', 'Mencoba menghubungi board', 10))
          .catch(() => showToast('Gagal memproses reboot', false));
      }
    }

    document.getElementById('form-folder').addEventListener('submit', function(e) {
      e.preventDefault();
      fetch('/save_folder', {
        method: 'POST',
        body: new URLSearchParams(new FormData(this))
      })
      .then(() => showOverlay('Mengubah Target Folder', 'Merestart board untuk memuat setelan', 8))
      .catch(() => showToast('Gagal merubah folder', false));
    });

    document.getElementById('form-mqtt').addEventListener('submit', function(e) {
      e.preventDefault();
      fetch('/save_mqtt', {
        method: 'POST',
        body: new URLSearchParams(new FormData(this))
      })
      .then(() => showOverlay('Menyimpan MQTT Broker', 'Menerapkan setelan broker baru', 8))
      .catch(() => showToast('Gagal menyimpan MQTT', false));
    });

    document.getElementById('form-master').addEventListener('submit', function(e) {
      e.preventDefault();
      fetch('/save_master', {
        method: 'POST',
        body: new URLSearchParams(new FormData(this))
      })
      .then(() => showOverlay('Menyimpan Kartu Master', 'Menerapkan UID kartu master baru', 8))
      .catch(() => showToast('Gagal menyimpan kartu master', false));
    });



    document.getElementById('file-input').addEventListener('change', function() {
      let label = this.files.length > 0 ? this.files[0].name : 'Seret file ke sini atau klik untuk memilih';
      document.getElementById('file-name-display').innerText = label;
    });

    document.getElementById('form-upload').addEventListener('submit', function(e) {
      e.preventDefault();
      let fileInput = document.getElementById('file-input');
      if (fileInput.files.length === 0) {
        showToast('Pilih file .bin terlebih dahulu!', false);
        return;
      }
      if (!confirm('Apakah Anda yakin ingin meng-upload dan men-flash file ini secara manual?')) {
        return;
      }

      let file = fileInput.files[0];
      let formData = new FormData();
      formData.append('update', file);

      let xhr = new XMLHttpRequest();
      xhr.open('POST', '/update', true);

      let container = document.getElementById('progress-container');
      let bar = document.getElementById('progress-bar');
      let percentText = document.getElementById('progress-percent');
      let statusText = document.getElementById('progress-status');

      container.style.display = 'block';
      statusText.innerText = 'Mengunggah file...';
      bar.style.width = '0%';
      percentText.innerText = '0%';

      xhr.upload.addEventListener('progress', function(event) {
        if (event.lengthComputable) {
          let percent = Math.round((event.loaded / event.total) * 100);
          bar.style.width = percent + '%';
          percentText.innerText = percent + '%';
          if (percent === 100) {
            statusText.innerText = 'Menulis ke Flash... (Jangan Matikan Board)';
          }
        }
      });

      xhr.onreadystatechange = function() {
        if (xhr.readyState === 4) {
          if (xhr.status === 200) {
            statusText.innerText = 'Flash Berhasil! Board merestart...';
            showOverlay('Update Manual Berhasil', 'Firmware baru terpasang, board sedang reboot', 10);
          } else {
            statusText.innerText = 'Gagal: ' + xhr.responseText;
            showToast('Gagal meng-upload firmware: ' + xhr.responseText, false);
          }
        }
      };
      xhr.send(formData);
    });
  </script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  String html = String(INDEX_HTML);
  
  // Replace string template saat dimuat pertama kali
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
  html.replace("{{RFID_MASTER}}", rfid_master);
  html.replace("{{SEL_DEVICE_GATE}}", device_type == "gate" ? "selected" : "");
  html.replace("{{SEL_DEVICE_KASIR}}", device_type == "kasir" ? "selected" : "");

  html.replace("{{TOPIC_PUB}}", device_type + "/" + mqtt_client_id + "/scan/in");
  html.replace("{{TOPIC_SUB}}", device_type + "/" + mqtt_client_id + "/result");

  server.send(200, "text/html", html);
}

void handleStatusData() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  JsonDocument doc;
  doc["version"]       = APP_VERSION;
  doc["folder"]        = folderAktif;
  doc["free_heap"]     = ESP.getFreeHeap();
  doc["min_free_heap"] = ESP.getMinFreeHeap();
  doc["uptime"]        = millis() / 1000;
  doc["rfid_status"]   = rfidStatus;

  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  char verStr[10];
  sprintf(verStr, "0x%02X", v);
  doc["rfid_version"]  = verStr;
  doc["last_rfid"]     = lastRfidScan;
  doc["last_barcode"]  = lastBarcodeScan;

  bool wifiCon = (WiFi.status() == WL_CONNECTED);
  doc["wifi_status"]   = wifiCon ? "Connected" : "Disconnected";
  doc["wifi_ssid"]     = wifiCon ? WiFi.SSID() : (wm.getWiFiIsSaved() ? WiFi.SSID() : "-");
  doc["wifi_rssi"]     = wifiCon ? String(WiFi.RSSI()) : "0";
  doc["wifi_ip"]       = WiFi.localIP().toString();

  doc["mqtt_status"]   = mqttClient.connected() ? "Connected" : "Disconnected";
  doc["mqtt_broker"]   = mqtt_server;
  doc["mqtt_port"]     = mqtt_port;
  doc["mqtt_client_id"]= mqtt_client_id;
  doc["relay1"]        = isRelay1Active;
  doc["relay2"]        = isRelay2Active;
  doc["device_type"]   = device_type;
  doc["mqtt_topic_pub"]= device_type + "/" + mqtt_client_id + "/scan/in";
  doc["mqtt_topic_sub"]= device_type + "/" + mqtt_client_id + "/result";
  doc["touch_active"]  = isTouching;

  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleReconnectWifi() {
  server.send(200, "text/plain", "Reconnecting");
  if (wm.getWiFiIsSaved()) {
    Serial.println("[WIFI] Perintah reconnect manual via Web...");
    WiFi.disconnect();
    WiFi.begin(); // Reconnect pakai kredensial tersimpan
  }
}

void handleForgetWifi() {
  server.send(200, "text/plain", "OK");
  Serial.println("[WIFI] Perintah Forget WiFi diterima. Menghapus kredensial...");
  delay(1000);
  wm.resetSettings(); // Menghapus SSID & Password dari NVS ESP32
  esp_task_wdt_delete(NULL); 
  ESP.restart();
}

void handleSaveFolder() {
  if (server.hasArg("folder_name")) {
    String newFolder = server.arg("folder_name");
    preferences.begin("gate_config", false);
    preferences.putString("ota_folder", newFolder);
    preferences.end();

    server.send(200, "text/plain", "OK");
    pendingRestart = true;
    restartTime = millis();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSaveMqtt() {
  if (server.hasArg("mqtt_server")) {
    mqtt_server    = server.arg("mqtt_server");
    mqtt_port      = server.arg("mqtt_port").toInt();
    mqtt_user      = server.arg("mqtt_user");
    mqtt_pass      = server.arg("mqtt_pass");
    mqtt_client_id = server.arg("mqtt_client_id");
    if (server.hasArg("device_type"))  device_type  = server.arg("device_type");

    preferences.begin("gate_config", false);
    preferences.putString("mqtt_server", mqtt_server);
    preferences.putInt("mqtt_port", mqtt_port);
    preferences.putString("mqtt_user", mqtt_user);
    preferences.putString("mqtt_pass", mqtt_pass);
    preferences.putString("mqtt_client_id", mqtt_client_id);
    preferences.putString("device_type", device_type);
    preferences.end();

    server.send(200, "text/plain", "OK");
    pendingRestart = true;
    restartTime = millis();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleSaveMaster() {
  if (server.hasArg("rfid_master")) {
    rfid_master = server.arg("rfid_master");
    rfid_master.toUpperCase();
    rfid_master.trim();

    preferences.begin("gate_config", false);
    preferences.putString("rfid_master", rfid_master);
    preferences.end();

    server.send(200, "text/plain", "OK");
    pendingRestart = true;
    restartTime = millis();
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleTriggerRelay() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");

  if (server.hasArg("relay")) {
    int rNum = server.arg("relay").toInt();
    if (rNum == 1 || rNum == 2) {
      triggerRelay(rNum);
      server.send(200, "text/plain", "Relay " + String(rNum) + " triggered");
    } else {
      server.send(400, "text/plain", "Invalid relay number");
    }
  } else {
    server.send(400, "text/plain", "Missing parameter");
  }
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting");
  pendingRestart = true;
  restartTime = millis();
}

void handleCekUpdate() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='2;url=/do_update'><style>body{background:#0b0c10;color:#f3f4f6;text-align:center;padding:50px;font-family:sans-serif;} .loader{border:3px solid rgba(255,255,255,0.08);border-top:3px solid #6366f1;border-radius:50%;width:40px;height:40px;animation:spin 1s linear infinite;margin:20px auto;} @keyframes spin{0%{transform:rotate(0deg);}100%{transform:rotate(360deg);}}</style></head><body><h2>Menghubungi GitHub...</h2><div class='loader'></div><p style='color:#9ca3af;'>Mengunduh manifest pembaruan secara secure...</p></body></html>";
  server.send(200, "text/html", html);
}

void handleDoUpdate() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", ""); // Live streaming log

  String htmlStart = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>OTA - Wanara Seta</title>
    <link href="https://fonts.googleapis.com/css2?family=Outfit:wght@400;600&family=JetBrains+Mono&display=swap" rel="stylesheet">
    <style>
      body { background: #0b0c10; color: #f3f4f6; font-family: 'Outfit', sans-serif; text-align: center; padding: 40px 20px; margin: 0; }
      .box { max-width: 600px; margin: 0 auto; padding: 30px; background: rgba(22, 25, 41, 0.75); border: 1px solid rgba(255,255,255,0.08); border-radius: 16px; }
      h2 { margin-top: 0; margin-bottom: 20px; font-weight: 600; color: #fff; }
      pre { background: #000; padding: 20px; border: 1px solid rgba(255, 255, 255, 0.05); border-radius: 8px; text-align: left; overflow-x: auto; color: #10b981; font-family: 'JetBrains Mono', monospace; font-size: 12.5px; white-space: pre-wrap; word-wrap: break-word; line-height: 1.5; }
      .btn { display: inline-block; background: linear-gradient(135deg, #6366f1, #4f46e5); color: #fff; padding: 11px 22px; text-decoration: none; border-radius: 6px; font-weight: 600; margin-top: 20px; }
    </style>
  </head>
  <body>
    <div class='box'>
      <h2>Hasil Update OTA GitHub</h2>
      <pre>)rawliteral";

  server.sendContent(htmlStart);
  cekUpdateGitHub(true);
  server.sendContent("</pre><a href='/' class='btn'>KEMBALI KE DASHBOARD</a></div></body></html>");
  server.sendContent("");
}

// Handler POST dari Manual Upload
void handleManualUpdate() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(500, "text/plain", String("Upload Error: ") + Update.errorString());
  } else {
    server.send(200, "text/plain", "OK");
    delay(1000);
    ESP.restart();
  }
}

// Handler penulisan chunk file Manual Upload
void handleManualUpload() {
  HTTPUpload& upload = server.upload();
  static bool hasError = false;
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[UPLOAD] Mulai upload manual: %s\n", upload.filename.c_str());
    hasError = false;
    
    // Nonaktifkan peripheral dan brownout detector sebelum flashing manual
    prepareForOTA();
    
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      hasError = true;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!hasError) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
        hasError = true;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!hasError) {
      if (Update.end(true)) {
        Serial.printf("[UPLOAD] Selesai! Ukuran: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
        hasError = true;
      }
    }
    // Kembalikan konfigurasi jika update gagal (jika sukses board langsung restart)
    if (hasError) {
      restoreAfterOTA();
    }
  }
}

// ========================================================================
// 6. KONTROL GERBANG & TIMER RELAY
// ========================================================================
void triggerRelay(int relayNum) {
  if (relayNum == 1) {
    isRelay1Active = true;
    relay1ActiveTime = millis();
    digitalWrite(RELAY1_PIN, RELAY_ACTIVE);
    Serial.println("[GATE] Relay 1 Aktif (Gerbang Terbuka)");
  } else if (relayNum == 2) {
    isRelay2Active = true;
    relay2ActiveTime = millis();
    digitalWrite(RELAY2_PIN, RELAY_ACTIVE);
    Serial.println("[GATE] Relay 2 Aktif (Alarm/Aux Terbuka)");
  }
  
  // Jadwalkan soft-reinit RFID Non-Blocking untuk mengatasi noise EMI
  pendingRfidReinit = true;
  pendingRfidReinitTime = millis();
}

void handleGate() {
  bool relayJustClosed = false;

  if (isRelay1Active && (millis() - relay1ActiveTime > GATE_OPEN_MS)) {
    isRelay1Active = false;
    digitalWrite(RELAY1_PIN, RELAY_DEACTIVE);
    Serial.println("[GATE] Relay 1 Mati (Gerbang Tertutup)");
    relayJustClosed = true;
  }
  if (isRelay2Active && (millis() - relay2ActiveTime > GATE_OPEN_MS)) {
    isRelay2Active = false;
    digitalWrite(RELAY2_PIN, RELAY_DEACTIVE);
    Serial.println("[GATE] Relay 2 Mati (Alarm/Aux Tertutup)");
    relayJustClosed = true;
  }

  if (relayJustClosed) {
    // Jadwalkan soft-reinit RFID Non-Blocking untuk mengatasi noise EMI
    pendingRfidReinit = true;
    pendingRfidReinitTime = millis();
  }
}

// ========================================================================
// 7. CALLBACK & KONEKSI MQTT
// ========================================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("\n[MQTT] Pesan masuk di topik: %s\n", topic);
  Serial.println("[MQTT] Payload: " + message);

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) {
    Serial.println("[JSON] Gagal mem-parsing payload MQTT.");
    return;
  }

  String topicStr = String(topic);
  String resTopic = device_type + "/" + mqtt_client_id + "/result";
  String cmdTopic = device_type + "/" + mqtt_client_id + "/command";

  if (topicStr == resTopic) {
    String status = doc["status"];
    if (status == "valid") {
      Serial.println("[AKSES] Valid! Pesan: " + doc["message"].as<String>());
      triggerRelay(1);
    } else if (status == "invalid") {
      Serial.println("[AKSES] Ditolak! Alasan: " + doc["message"].as<String>());
    }
  } 
  else if (topicStr == cmdTopic) {
    String cmd = doc["command"];
    if (cmd == "open_gate") {
      Serial.println("[COMMAND] Perintah Buka Gerbang Utama!");
      triggerRelay(1);
    } else if (cmd == "open_gate_2") {
      Serial.println("[COMMAND] Perintah Buka Relay 2 / Alarm!");
      triggerRelay(2);
    } else if (cmd == "reboot") {
      Serial.println("[COMMAND] Perintah Reboot dari server. Restarting...");
      esp_task_wdt_delete(NULL); 
      delay(1000);
      ESP.restart();
    }
  }
}

void reconnectMQTT() {
  Serial.printf("[MQTT] Melakukan koneksi ke broker EMQX %s:%d...\n", mqtt_server.c_str(), mqtt_port);
  
  // TCP Ping Cepat (Timeout maksimal 1 detik)
  // Mencegah fungsi koneksi mem-blokir program selama 15 detik jika IP/Server sedang mati
  WiFiClient pingClient;
  if (!pingClient.connect(mqtt_server.c_str(), mqtt_port, 1000)) {
    Serial.println("[MQTT] ERROR: Broker tidak merespon / IP tidak dapat dijangkau.");
    return; // Langsung keluar agar sistem & WDT tetap berjalan normal
  }
  pingClient.stop(); // Port terbuka! Tutup ping socket

  // Set timeout (3 detik) pada client agar reconnect tidak memblokir WDT
  espClient.setTimeout(3000);

  // Beri makan WDT sebelum proses koneksi yang memakan waktu (blocking)
  esp_task_wdt_reset();

  bool connected = false;
  if (mqtt_user.length() > 0) {
    connected = mqttClient.connect(mqtt_client_id.c_str(), mqtt_user.c_str(), mqtt_pass.c_str());
  } else {
    connected = mqttClient.connect(mqtt_client_id.c_str());
  }

  // Beri makan WDT setelah selesai proses koneksi
  esp_task_wdt_reset();

  if (connected) {
    Serial.println("[MQTT] Terhubung!");
    
    String resTopic = device_type + "/" + mqtt_client_id + "/result";
    String cmdTopic = device_type + "/" + mqtt_client_id + "/command";
    
    mqttClient.subscribe(resTopic.c_str());
    mqttClient.subscribe(cmdTopic.c_str());
    
    Serial.println("[MQTT] Berhasil subscribe ke topik result & command.");
  } else {
    Serial.print("[MQTT] Gagal terhubung ke broker, rc=");
    Serial.println(mqttClient.state());
  }
}

// ========================================================================
// 8. SETUP & LOOP
// ========================================================================
void setup() {
  // Nonaktifkan brownout detector agar tidak reset saat WiFi radio aktif
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);

  // Ambil data konfigurasi dari memori internal (NVS)
  preferences.begin("gate_config", true);
  folderAktif    = preferences.getString("ota_folder", "ESP32main");
  savedApSSID    = preferences.getString("ap_ssid", DEFAULT_AP_SSID);
  mqtt_server    = preferences.getString("mqtt_server", mqtt_server);
  mqtt_port      = preferences.getInt("mqtt_port", mqtt_port);
  mqtt_user      = preferences.getString("mqtt_user", mqtt_user);
  mqtt_pass      = preferences.getString("mqtt_pass", mqtt_pass);
  mqtt_client_id = preferences.getString("mqtt_client_id", mqtt_client_id);
  rfid_master    = preferences.getString("rfid_master", rfid_master);
  device_type    = preferences.getString("device_type", "gate");
  preferences.end();

  // Pastikan Client ID unik dengan menambahkan suffix MAC address jika masih default atau kosong
  if (mqtt_client_id == "gate_esp32_01" || mqtt_client_id.length() == 0) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macSuffix[16];
    sprintf(macSuffix, "_%02X%02X%02X", mac[3], mac[4], mac[5]);
    mqtt_client_id = "gate_esp32_01" + String(macSuffix);
    Serial.println("[MQTT] Client ID default terdeteksi. Menggunakan ID unik: " + mqtt_client_id);
  }

  // Konfigurasi WiFiManager
  wm.setConnectTimeout(15);       // Coba konek ke router maksimal 15 detik
  wm.setConfigPortalTimeout(180); // Portal AP otomatis tutup setelah 3 menit jika tidak diisi
  wm.setConnectRetries(3);        // Coba reconnect 3x sebelum menyerah

  Serial.println("\n[WIFI] Mencoba terhubung ke jaringan...");
  bool wifiOk = wm.autoConnect(savedApSSID.c_str(), DEFAULT_AP_PASSWORD);

  if (!wifiOk) {
    // Gagal konek dan portal timeout — buka ulang AP untuk debug
    Serial.println("[WIFI] autoConnect gagal / timeout. Membuka Hotspot debug...");
    wm.startConfigPortal(savedApSSID.c_str(), DEFAULT_AP_PASSWORD);
    Serial.println("[WIFI] Mode Hotspot aktif. Hubungkan ke '" + savedApSSID + "' untuk konfigurasi.");
  } else {
    Serial.println("[WIFI] Sukses Terhubung! IP Web Dashboard: " + WiFi.localIP().toString());
  }

  // Routing Halaman Web
  server.on("/", handleRoot);
  server.on("/status_data", HTTP_GET, handleStatusData);
  server.on("/reconnect_wifi", HTTP_GET, handleReconnectWifi);
  server.on("/forget_wifi", HTTP_GET, handleForgetWifi);
  server.on("/save_folder", HTTP_POST, handleSaveFolder);
  server.on("/save_mqtt", HTTP_POST, handleSaveMqtt);
  server.on("/save_master", HTTP_POST, handleSaveMaster);
  server.on("/trigger_relay", HTTP_GET, handleTriggerRelay);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.on("/cek_update", HTTP_GET, handleCekUpdate);
  server.on("/do_update", HTTP_GET, handleDoUpdate);
  
  // Handler Upload Manual
  server.on("/update", HTTP_POST, handleManualUpdate, handleManualUpload);

  server.begin();

  // Hardware Reset Pulsa via RST Pin sebelum inisialisasi SPI / PCD
  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, LOW);
  delay(20);
  digitalWrite(RST_PIN, HIGH);
  delay(50);

  // Inisialisasi SPI & RFID MFRC522 (VSPI default, gunakan begin tanpa SS_PIN agar CS dikendalikan manual oleh library)
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI); 
  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);

  // Cek versi sensor saat awal boot
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  Serial.printf("[RFID] Version Register: 0x%02X\n", v);
  if (v == 0x91 || v == 0x92 || v == 0x88 || v == 0x82 || v == 0x12) {
    rfidStatus = "✅ AKTIF";
    Serial.println("[RFID] Sensor MFRC522 terdeteksi dan siap.");
  } else {
    rfidStatus = "❌ TIDAK MERESPON (Cek Pin/Kabel)";
    Serial.println("[RFID] PERHATIAN: Sensor MFRC522 tidak terdeteksi!");
  }

  // Setup Barcode Scanner (UART2)
  Serial2.begin(BARCODE_BAUD, SERIAL_8N1, BARCODE_RX, BARCODE_TX);
  Serial.println("[BARCODE] Scanner UART2 siap.");

  // Setup MQTT Client
  mqttClient.setServer(mqtt_server.c_str(), mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);        // Buffer JSON MQTT diperbesar

  // Aktifkan Task Watchdog Timer
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

  // Setup Pin Relay sebagai Output
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY1_PIN, RELAY_DEACTIVE);
  digitalWrite(RELAY2_PIN, RELAY_DEACTIVE);

  // Setup Pin LED RFID sebagai Output
  pinMode(RFID_LED_PIN, OUTPUT);
  digitalWrite(RFID_LED_PIN, LOW); // Default mati (asumsi Active-HIGH)
}

void loop() {
  server.handleClient(); // Jaga agar server web tetap responsif

  // Touch Pin - Deteksi tahan 10 detik untuk masuk ulang ke WiFi Manager
  {
    uint16_t touchVal = touchRead(TOUCH_PIN);
    if (touchVal < TOUCH_THRESHOLD) {
      if (!isTouching) {
        isTouching = true;
        touchStartTime = millis();
        Serial.println("[TOUCH] GPIO4 disentuh! Tahan 10 detik untuk reset WiFi Manager...");
      } else if (millis() - touchStartTime >= 10000) {
        Serial.println("[TOUCH] 10 detik tercapai! Mereset WiFi Manager dan restart...");
        isTouching = false;
        wm.resetSettings();
        esp_task_wdt_delete(NULL);
        delay(500);
        ESP.restart();
      }
    } else {
      if (isTouching && (millis() - touchStartTime > 500)) {
        Serial.printf("[TOUCH] Dilepas setelah %lums (butuh 10000ms untuk reset)\n", millis() - touchStartTime);
      }
      isTouching = false;
      touchStartTime = 0;
    }
  }

  // Tangani restart tertunda agar respon HTTP terkirim bersih ke browser
  if (pendingRestart && (millis() - restartTime > 2000)) {
    ESP.restart();
  }

  // Status Koneksi
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  bool mqttConnected = mqttClient.connected();

  static int wifiReconnectAttempts = 0;
  static unsigned long lastWifiCheck = 0;
  static unsigned long timeAfterFiveAttempts = 0;

  if (wifiConnected) {
    wifiReconnectAttempts = 0;
    timeAfterFiveAttempts = 0;
  }

  // --- LOGIKA RECONNECT WIFI ---
  if (!wifiConnected) {
    // Coba reconnect setiap 10 detik maksimal 5 kali percobaan
    if (millis() - lastWifiCheck > 10000 && wifiReconnectAttempts < 5) {
      lastWifiCheck = millis();
      if (wm.getWiFiIsSaved()) {
        wifiReconnectAttempts++;
        Serial.printf("[WIFI] Terputus! Mencoba menghubungkan ulang ke router... (Percobaan %d/5)\n", wifiReconnectAttempts);
        WiFi.disconnect();
        WiFi.begin(); 
        
        if (wifiReconnectAttempts == 5) {
          timeAfterFiveAttempts = millis(); // Catat waktu setelah percobaan ke-5 selesai
        }
      }
    }
  }

  // --- LOGIKA WATCHDOG TIMER (WDT) ---
  bool feedWDT = true;

  if (wm.getWiFiIsSaved()) {
    // Jika WiFi terputus, sudah 5 kali percobaan, biarkan timeout 1 menit sebelum WDT reset board
    if (!wifiConnected && wifiReconnectAttempts >= 5 && timeAfterFiveAttempts > 0) {
      if (millis() - timeAfterFiveAttempts > 60000) {
        feedWDT = false;
      }
    }
  }

  if (feedWDT) {
    esp_task_wdt_reset(); // Beri makan watchdog
  }

  // --- LOGIKA RECONNECT MQTT ---
  if (wifiConnected) {
    if (!mqttConnected) {
      // Coba reconnect tiap 5 detik tanpa peduli WDT
      if (millis() - lastMqttReconnectAttempt > 5000) {
        lastMqttReconnectAttempt = millis();
        reconnectMQTT();
      }
    } else {
      mqttClient.loop();
    }
  }

  // Logika Pembacaan Barcode (UART2)
  if (Serial2.available()) {
    String barcodeData = Serial2.readStringUntil('\r');
    barcodeData.trim();
    if (barcodeData.length() > 0) {
      Serial.println("[BARCODE] Terdeteksi: " + barcodeData);

      unsigned long nowBarcode       = millis();
      bool isSameBarcode             = (barcodeData == lastScannedBarcode);
      bool withinBarcodeCooldown     = (nowBarcode - lastBarcodeTime < COOLDOWN_SAME_BARCODE_MS);

      if (isSameBarcode && withinBarcodeCooldown) {
        Serial.println("[BARCODE] Cooldown aktif, scan diabaikan.");
      } else {
        lastScannedBarcode = barcodeData;
        lastBarcodeTime    = nowBarcode;
        lastBarcodeScan    = barcodeData;

        if (wifiConnected && mqttClient.connected()) {
          String payload = "{\"barcode\":\"" + barcodeData + "\", \"device_id\":\"" + mqtt_client_id + "\"}";
          String pubTopic = device_type + "/" + mqtt_client_id + "/scan/in";
          if (mqttClient.publish(pubTopic.c_str(), payload.c_str())) {
            Serial.println("[MQTT] Barcode sukses terkirim.");
          } else {
            Serial.println("[MQTT] Barcode gagal terkirim!");
          }
        }
      }
    }
  }

  // Tangani Re-inisialisasi RFID Non-Blocking akibat EMI Relay
  if (pendingRfidReinit && (millis() - pendingRfidReinitTime > 50)) {
    pendingRfidReinit = false;
    mfrc522.PCD_Init();
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  }

  // Logika Pembacaan & Health-Check Sensor RFID (dilewati jika scanner_type = "qr")
  static unsigned long lastRFIDReadTime = 0;
  static unsigned long lastRfidHealthCheck = 0;
  static unsigned long lastPeriodicRfidReinit = 0;

  // Health-check berkala setiap 5 detik
  if (millis() - lastRfidHealthCheck > 5000) {
    lastRfidHealthCheck = millis();
    byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    if (v == 0x00 || v == 0xFF) {
      Serial.println("[RFID] Sensor hang/terputus! Melakukan hard reset...");
      
      // Hardware Reset Pulsa via RST Pin
      digitalWrite(RST_PIN, LOW);
      pinMode(RST_PIN, OUTPUT);
      delay(20);
      digitalWrite(RST_PIN, HIGH);
      delay(50);
      
      // Reset SPI bus
      SPI.end();
      SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
      
      mfrc522.PCD_Init();
      mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
      delay(50);
      
      v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
      if (v != 0x00 && v != 0xFF) {
        rfidStatus = "✅ AKTIF (Dipulihkan)";
        Serial.printf("[RFID] Sensor pulih! Versi: 0x%02X\n", v);
      } else {
        rfidStatus = "❌ TIDAK MERESPON (Hang)";
        Serial.println("[RFID] Pemulihan gagal. Cek kabel.");
      }
    } else {
      if (rfidStatus.indexOf("✅") == -1) {
        rfidStatus = "✅ AKTIF";
      }
    }
  }

  // Soft-reset register berkala setiap 30 detik untuk memulihkan register yang ter-corrupt noise
  if (millis() - lastPeriodicRfidReinit > 30000) {
    lastPeriodicRfidReinit = millis();
    mfrc522.PCD_Init();
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  }

  // Proses scan kartu RFID
  if (mfrc522.PICC_IsNewCardPresent()) {
    if (mfrc522.PICC_ReadCardSerial()) {
      String rfidUid = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        rfidUid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        rfidUid += String(mfrc522.uid.uidByte[i], HEX);
      }
      rfidUid.toUpperCase();

      unsigned long now = millis();
      
      // Cooldown logic cerdas
      bool isSameCard = (rfidUid == lastScannedUid);
      bool withinSameCardCooldown = (now - lastScannedTime < COOLDOWN_SAME_CARD_MS);
      bool withinAnyCardCooldown = (now - lastScannedTime < COOLDOWN_ANY_CARD_MS);

      if (isSameCard && withinSameCardCooldown) {
        // Abaikan agar tidak terjadi double-scan kartu yang sama
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      } else if (withinAnyCardCooldown) {
        // Abaikan karena terlalu cepat dari scan sebelumnya
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      } else {
        // Proses Validasi Kartu
        lastScannedUid  = rfidUid;
        lastScannedTime = now;
        lastRfidScan    = rfidUid;
        Serial.println("[RFID] Kartu Terdeteksi: " + rfidUid);

        // Nyalakan LED Indikator
        isRfidLedActive = true;
        rfidLedActiveTime = millis();
        digitalWrite(RFID_LED_PIN, HIGH);

        // Bypass jika master card
        if (rfidUid == rfid_master) {
          Serial.println("[MASTER] Master Card terdeteksi! Bypass buka gerbang.");
          triggerRelay(1);
        } else {
          // Kirim data scan ke MQTT Broker
          if (wifiConnected && mqttClient.connected()) {
            String payload = "{\"rfid\":\"" + rfidUid + "\", \"device_id\":\"" + mqtt_client_id + "\"}";
            String pubTopic = device_type + "/" + mqtt_client_id + "/scan/in";
            if (mqttClient.publish(pubTopic.c_str(), payload.c_str())) {
              Serial.println("[MQTT] Data scan terkirim ke EMQX.");
            } else {
              Serial.println("[MQTT] Gagal mengirim data scan!");
            }
          } else {
            Serial.println("[MQTT] Offline, data scan tidak dikirim.");
          }
        }

        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
      }
    } else {
      // Gagal membaca serial kartu (misalnya karena terganggu noise relay).
      // Bersihkan state kartu dan soft reset reader agar tidak stuck.
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      mfrc522.PCD_Init();
      mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
    }
  }

  // Tangani timer matikan LED Indikator RFID otomatis
  if (isRfidLedActive && (millis() - rfidLedActiveTime > RFID_LED_ON_MS)) {
    isRfidLedActive = false;
    digitalWrite(RFID_LED_PIN, LOW);
  }

  // Tangani timer penutupan gerbang otomatis
  handleGate();
}