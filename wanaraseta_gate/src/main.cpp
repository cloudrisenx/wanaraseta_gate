#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>

WiFiManager wm;

// ==============================================================
// KONFIGURASI GITHUB KAMU (Wajib pakai URL "Raw" dari GitHub)
// ==============================================================
// Contoh format URL raw: https://raw.githubusercontent.com/Username/Repo/main/namafile
const char* urlVersi = "https://raw.githubusercontent.com/UsernameKamu/NamaRepo/main/version.txt";
const char* urlFirmware = "https://raw.githubusercontent.com/UsernameKamu/NamaRepo/main/firmware.bin";

// Versi program saat ini (Ubah angka ini setiap mau upload fitur baru ke GitHub)
String versiSaatIni = "1.0"; 

void cekUpdateGitHub() {
  Serial.println("Mengecek update firmware di GitHub...");
  
  // Karena GitHub pakai HTTPS, kita pakai WiFiClientSecure
  WiFiClientSecure client;
  client.setInsecure(); // Abaikan cek sertifikat SSL agar tidak ribet saat sertifikat GitHub kadaluarsa

  HTTPClient http;
  http.begin(client, urlVersi);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String versiDiGitHub = http.getString();
    versiDiGitHub.trim(); // Bersihkan spasi kosong atau enter

    Serial.println("Versi di ESP32: " + versiSaatIni);
    Serial.println("Versi di GitHub: " + versiDiGitHub);

    // Kalau beda, berarti ada update!
    if (versiDiGitHub != versiSaatIni && versiDiGitHub.length() > 0) {
      Serial.println(">> UPDATE DITEMUKAN! Sedang mendownload firmware dari GitHub...");
      
      // Proses sihirnya ada di satu baris ini:
      t_httpUpdate_return ret = httpUpdate.update(client, urlFirmware);

      switch (ret) {
        case HTTP_UPDATE_FAILED:
          Serial.printf(">> GAGAL UPDATE Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
          break;
        case HTTP_UPDATE_NO_UPDATES:
          Serial.println(">> Tidak ada update.");
          break;
        case HTTP_UPDATE_OK:
          Serial.println(">> UPDATE SUKSES! ESP32 akan restart otomatis.");
          break;
      }
    } else {
      Serial.println(">> Firmware sudah paling baru. Aman.");
    }
  } else {
    Serial.println(">> Gagal mengecek ke GitHub. Cek koneksi atau URL.");
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  
  // Jodohkan Wi-Fi dulu
  wm.autoConnect("Wanara_Gate_Setup", "griyapersada");
  Serial.println("WiFi Terhubung!");

  // Cek update 1 kali setiap ESP32 baru dinyalakan
  cekUpdateGitHub();
}

void loop() {
  // Kalau mau ESP32 ngecek update setiap 1 jam sekali (biar otomatis tanpa direstart)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 3600000) { // 3.600.000 ms = 1 Jam
    cekUpdateGitHub();
    lastCheck = millis();
  }

  // Sisa logika kamu (RFID, Gerbang, MQTT) jalan normal di bawah sini
}