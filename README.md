# Wanara Seta Smart Gate — Panduan Komplit Setup Ekosistem

Dokumen ini menjelaskan arsitektur sistem, alur komunikasi data, dan panduan lengkap langkah-demi-langkah untuk melakukan setup seluruh komponen pada ekosistem **Wanara Seta Smart Gate**.

Ekosistem ini terdiri dari 4 komponen utama yang saling terintegrasi secara real-time:
1. **EMQX MQTT Broker**: Hub komunikasi real-time dan HTTP Webhook router.
2. **Flask Admin Central (`Main/`)**: Server backend utama, pengolah REST API scan, database PostgreSQL, dan panel administrator (Manage Gates, Members, Guests, Log Scan, Top Up).
3. **Next.js Kiosk UI (`kioks_web/`)**: Layar tampilan TV Kios pintar (Slideshow media gambar/video, audio pengiring, dan popup notifikasi scan real-time) serta server admin lokalnya.
4. **ESP32/WT32 Firmware (`wanaraseta_gate/`)**: Program mikrokontroler pintu gerbang berbasis PlatformIO dengan sensor RFID MFRC522, simulator relay gerbang, konektivitas WiFiManager, dan auto-update via GitHub OTA.

---

## 1. Arsitektur & Pemetaan Port

Berikut adalah peta distribusi port dan database untuk masing-masing service di lingkungan pengembangan lokal:

| Service / Komponen | Port | Database | Keterangan |
| :--- | :--- | :--- | :--- |
| **EMQX Broker (TCP)** | `1883` | - | Digunakan oleh Flask Backend & ESP32 |
| **EMQX Broker (WebSockets)** | `8083` | - | Digunakan oleh Next.js Kios di browser |
| **EMQX Admin Dashboard** | `18083` | Built-in | Dashboard pengaturan EMQX |
| **Flask Admin Central (`Main/`)** | `5001` | PostgreSQL (`wanara_gate_db`) | Server API, CRUD panel, & Logika Top Up |
| **Next.js Kiosk UI (`kioks_web/`)** | `3000` | Local Storage | Layar TV Kios (Slideshow & Popup) |
| **Kiosk Admin Backend** | `5000` | SQLite (`kiosk.db`) | Panel edit TV Kios (Upload file & slideshow) |
| **ESP32 Local Dashboard** | `80` | Preferences Flash | Halaman ganti target mesin & trigger OTA manual |

---

## 2. Alur Kerja Komunikasi (Cara Kerja)

Sistem ini menggabungkan protokol **MQTT (Asinkron & Real-time)** untuk komunikasi perangkat keras, dengan **HTTP REST API (Sinkron)** untuk pemrosesan webhook backend.

Berikut adalah diagram urutan alur komunikasi saat kartu RFID ditempelkan pada mesin gate reader:

```mermaid
sequenceDiagram
    autonumber
    participant ESP as ESP32 Gate Reader
    participant EMQX as EMQX MQTT Broker (127.0.0.1 / 192.168.4.50)
    participant Flask as Flask Central Backend (Port 5001)
    participant DB as PostgreSQL (wanara_gate_db)
    participant Kiosk as Kiosk TV Screen (Port 3000)

    ESP->>EMQX: 1. Tempel Kartu: Publish RFID Scan ke "gate/gate_esp32_01/scan/in"<br/>Payload: {"rfid": "C36E09AC", "device_id": "gate_esp32_01"}
    Note over EMQX: 2. Rule Webhook Data Integration Aktif
    EMQX->>Flask: 3. Forward HTTP POST ke "/api/gate/scan"<br/>Payload: {"rfid": "C36E09AC", "device_id": "gate_esp32_01"}
    
    rect rgb(20, 30, 40)
        Note over Flask: 4. Validasi Perangkat (Device ID aktif)<br/>5. Cek & Auto-claim Saldo Tunda (Pending Top Up)<br/>6. Validasi Kartu (Masa berlaku & Saldo)
        Flask->>DB: Query info Device, Member & Pending Top Up
        DB-->>Flask: Kembalikan status data & lakukan debit saldo atomik
        Note over Flask: 7. Catat transaksi di tabel GateLog (Granted/Denied)
        Flask->>DB: Simpan log transaksi
    end
    
    Flask->>EMQX: 8. Publish Keputusan Akses ke "gate/gate_esp32_01/result"<br/>Payload: {"status": "valid", "message": "Access granted", "log_id": 1}
    Flask-->>EMQX: 9. Kembalikan Respon HTTP 200 {"status": "ok"}<br/>(Menyelesaikan Webhook)
    EMQX->>ESP: 10. Forward Respon Otorisasi Akses ke Topic Response
    EMQX->>Kiosk: 11. Broadcast status notifikasi ke Topic Kios TV<br/>(e.g. "gate/lobby/result" untuk pop-up hijau/merah)
    
    Note over ESP: 12. Jika "granted": Aktifkan Relay Pintu (LED menyala 3 detik)<br/>Jika "denied": Bunyikan Alarm Buzzer
    Note over Kiosk: 13. Tampilkan Glassmorphism Pop-up (Valid/Invalid) selama 4 detik
```

---

## 3. Langkah Setup Detail

### LANGKAH A: Setup EMQX MQTT Broker

PC / Server lokal bertindak sebagai broker MQTT. Gunakan EMQX native (Windows/Ubuntu) untuk menangani WebSockets secara langsung.

1. **Jalankan EMQX**:
   * **Windows**: Unduh EMQX versi `.zip`, ekstrak ke `C:\emqx`, jalankan Powershell sebagai Administrator dan eksekusi:
     ```powershell
     C:\emqx\bin\emqx start
     ```
   * **Ubuntu**:
     ```bash
     curl -s https://assets.emqx.com/scripts/install-emqx-deb.sh | sudo bash
     sudo apt-get install emqx
     sudo systemctl start emqx
     sudo systemctl enable emqx
     ```
2. **Setup Kredensial Koneksi (Otentikasi)**:
   * Buka browser dan arahkan ke dashboard EMQX: `http://localhost:18083` (Default user: `admin` | pass: `public`).
   * Buka menu **Access Control** -> **Authentication**.
   * Klik **+ Create** -> Pilih **Password-Based** -> **Built-in Database** -> Klik **Create**.
   * Klik tombol **Users** pada baris tersebut, lalu tambahkan kredensial untuk perangkat keras gerbang dan backend:
     * **Username:** `gate_esp32_01` | **Password:** `11223344` (Sesuaikan dengan setting di `main.cpp` ESP32)
     * **Username:** `backend_flask` | **Password:** `11223344` (Sesuaikan dengan setting MQTT Flask)
     * **Username:** `kiosk_nextjs` | **Password:** `sandi_next` (Untuk WebSockets Next.js Kios)
3. **Setup Webhook Data Integration (EMQX ke Flask)**:
   * Pada Dashboard EMQX, masuk ke menu **Integration** -> **Data Bridge**.
   * Klik **+ Create**, lalu pilih **HTTP Server**.
   * Isi pengaturan:
     * **Bridge Name:** `Webhook_ke_Flask`
     * **Method:** `POST`
     * **URL:** `http://localhost:5001/api/gate/scan` *(Sesuaikan IP/Port jika Flask berjalan di mesin berbeda)*
     * **Body:** `${payload}` *(Meneruskan isi payload JSON mentah)*
     * Klik **Create**.
   * Sistem akan menawarkan pembuatan **Rule**, klik **Create Rule**.
   * Pada kolom SQL Rule, masukkan:
     ```sql
     SELECT payload FROM "gate/+/scan/in"
     ```
     *(Atau jika menggunakan payload ter-parsing: `SELECT payload.rfid as rfid, payload.device_id as device_id FROM "gate/+/scan/in"`)*
   * Klik **Save**.

---

### LANGKAH B: Setup Flask Admin Central (`Main/`)

Bagian ini mengelola otorisasi scan, sisa saldo member, pendaftaran tamu, dan data transaksi gerbang.

1. **Prasyarat**:
   * Pastikan Python 3.12+ dan PostgreSQL sudah terinstall di PC Anda.
   * Buat database baru di PostgreSQL bernama `wanara_gate_db`.
2. **Instalasi Dependensi**:
   * Buka terminal di folder `Main/`:
     ```bash
     cd Main
     python -m venv venv
     # Aktifkan virtual environment
     # Di Windows (Powershell):
     .\venv\Scripts\Activate.ps1
     # Di Linux/macOS:
     source venv/bin/activate
     
     pip install -r requirements.txt
     ```
3. **Konfigurasi Database**:
   * Secara default, koneksi diatur di `run.py` menggunakan PostgreSQL:
     ```python
     "postgresql://wanara_admin:password_kuat_123@localhost:5432/wanara_gate_db"
     ```
   * Anda bisa meng-override setting ini menggunakan environment variable:
     ```bash
     $env:DATABASE_URL="postgresql://username:password@localhost:5432/wanara_gate_db"
     ```
4. **Migrasi Database & Seed Data**:
   * Jalankan script migrasi awal untuk membuat tabel dan men-seed user Administrator superadmin (`admin` / `admin123`):
     ```bash
     python -c "from run import create_app; from models import db; app=create_app(); app.app_context().push(); db.create_all()"
     ```
5. **Menjalankan Server**:
     ```bash
     python run.py
     ```
   * Server admin akan berjalan secara lokal di `http://localhost:5001`.
6. **Menjalankan Pengujian (TDD Unit Tests)**:
   * Eksekusi seluruh 31 test case suite dengan:
     ```bash
     python -m unittest discover -s tests
     ```

---

### LANGKAH C: Setup Next.js Kiosk UI (`kioks_web/`)

Bagian ini menampilkan slideshow media promosi/informasi yang diselingi popup RFID scan secara real-time.

1. **Setup & Jalankan Local Kiosk Admin Backend (Port 5000)**:
   * Buka terminal di folder `kioks_web/admin-server/`:
     ```bash
     cd kioks_web/admin-server
     pip install -r requirements.txt
     python app.py
     ```
   * Server admin lokal akan berjalan di `http://localhost:5000` (Menggunakan SQLite `instance/kiosk.db` untuk menyimpan konfigurasi media & TV ID).
   * **Kredensial Login:** `admin` | `admin123`.
2. **Setup & Jalankan Next.js Frontend (Port 3000)**:
   * Buka terminal baru di root folder `kioks_web/`:
     ```bash
     cd kioks_web
     npm install
     npm run dev
     ```
   * Tampilan Kiosk UI berjalan secara lokal di `http://localhost:3000`.
3. **Koneksi TV ID & Konfigurasi Slideshow**:
   * Masuk ke dashboard admin lokal (`http://localhost:5000/login`).
   * Daftarkan TV baru (contoh: TV ID = `TV-LOBBY`, MQTT Topic = `gate/lobby/result`).
   * Edit TV tersebut, unggah beberapa file gambar/video (MP4/WebM), dan file audio background opsional.
   * Hubungkan browser Kios dengan membuka URL sekali saja menggunakan query parameter:
     `http://localhost:3000?tv_id=TV-LOBBY`
     *(Sistem otomatis menyimpan TV ID ke `localStorage` perangkat dan membersihkan URL)*.
   * Kios akan secara otomatis terhubung ke MQTT Broker EMQX dan memulai putaran slideshow.

---

### LANGKAH D: Setup ESP32/WT32 Firmware (`wanaraseta_gate/`)

Mikrokontroler ESP32 membaca kartu RFID, mengirim data scan ke EMQX, dan menggerakkan simulator relay pintu.

1. **Buka Project di PlatformIO**:
   * Instal extension **PlatformIO IDE** pada VS Code.
   * Buka folder `wanaraseta_gate/` sebagai workspace PlatformIO.
2. **Pilih Target Firmware**:
   * Buka file `platformio.ini`.
   * Pada baris pemilih folder `src_dir`, hapus tanda titik koma (uncomment) pada target yang diinginkan dan comment yang lain:
     ```ini
     [platformio]
     ; Pilih folder program aktif
     src_dir = ESP32main
     ; src_dir = ESP32_test
     ; src_dir = WT32main
     ; src_dir = WT32_test
     ```
3. **Konfigurasi Pin Hardware (di `src_dir/main.cpp`)**:
   * Pin sudah dikonfigurasi per-target. Pastikan sesuai dengan skema hardware Anda:
     * **ESP32main**: Relay1=GPIO2, Relay2=GPIO27, RFID SS=GPIO5, Barcode RX=GPIO16
     * **WT32main**: Relay1=GPIO2, Relay2=GPIO5, Buzzer=GPIO33, RFID SS=GPIO12, Barcode RX=GPIO39
     * Durasi pulse relay saat ini: **200 ms** (`GATE_OPEN_MS`)
     * WDT timeout: **5 detik** (`WDT_TIMEOUT_SECONDS`)
4. **Compile & Upload**:
   * Hubungkan ESP32 ke PC via USB.
   * Jalankan perintah **Build** kemudian **Upload** pada task bar PlatformIO.
5. **Setup Koneksi WiFi (WiFiManager) — ESP32main**:
   * Saat ESP32 pertama kali dinyalakan (atau gagal terhubung ke WiFi terdaftar), ia akan memancarkan hotspot sendiri.
   * Hubungkan HP/Laptop Anda ke WiFi Hotspot: **`Wanara_Gate_Setup`** (Password: **`griyapersada`**).
   * Halaman login portal WiFiManager akan terbuka secara otomatis di browser HP Anda.
   * Pilih SSID WiFi lokal Anda, masukkan password, lalu klik **Save**.
   * ESP32 akan restart dan mendapatkan alamat IP lokal. Buka IP tersebut di browser untuk mengakses dashboard lokal (Port 80).
   * **Reset WiFiManager**: Sentuh dan tahan **GPIO4** (Touch Pin) selama **10 detik** untuk menghapus kredensial WiFi dan masuk kembali ke mode setup hotspot.

6. **Setup Koneksi Ethernet — WT32main**:
   * WT32-ETH01 menggunakan koneksi **Ethernet LAN8720** sebagai koneksi utama. Cukup sambungkan kabel RJ45 ke router/switch.
   * Saat kabel terhubung dan mendapat IP, board akan otomatis mematikan WiFi untuk hemat daya.
   * Jika kabel Ethernet **terputus**, board otomatis mengaktifkan WiFi AP dengan nama **`WT32_Gate_{client_id}`** (Password: `griyapersada`) sebagai mode debug darurat.
   * Akses dashboard lokal melalui IP Ethernet di browser (Port 80).

---

## 4. Referensi Firmware v3.9 — ESP32main & WT32main

### A. Perbandingan Fitur & Hardware

| Fitur | **ESP32main** (38-Pin Dev Board) | **WT32main** (WT32-ETH01) |
| :--- | :--- | :--- |
| **Koneksi Utama** | WiFi (via WiFiManager) | Ethernet LAN8720 |
| **Koneksi Fallback** | Hotspot `Wanara_Gate_Setup` | WiFi AP `WT32_Gate_{client_id}` (saat Ethernet putus) |
| **Relay 1 (Gate In)** | GPIO 2 (Active-LOW) | GPIO 2 (Active-LOW) |
| **Relay 2 (Alarm/Aux)** | GPIO 27 (Active-LOW) | GPIO 5 (Active-LOW) |
| **Durasi Relay** | 200 ms (pulse) | 200 ms (pulse) |
| **RFID MFRC522** | SS:5, RST:22, SCK:18, MISO:19, MOSI:23 | SS:12, RST:4, SCK:14, MISO:35, MOSI:15 |
| **Barcode UART2** | RX:16, TX:17, Baud:9600 | RX:39, TX:32, Baud:9600 |
| **LED RFID** | GPIO 26 (nyala 200ms saat scan) | — |
| **Buzzer Eksternal** | — | GPIO 33 (non-blocking melody) |
| **Reset WiFi** | Touch GPIO4 (tahan 10 detik) | — |
| **WDT Timeout** | 5 detik | 5 detik |
| **OTA Dashboard** | `/cek_update` → streaming log | `/api/git_update` → JSON response |

### B. Konfigurasi Pin Hardware Detail

**ESP32main:**
```cpp
#define RELAY1_PIN    2     // Gate In / Built-in LED
#define RELAY2_PIN    27    // Gate Out / Alarm
#define RFID_LED_PIN  26    // Indikator LED scan RFID
#define TOUCH_PIN     4     // Reset WiFiManager (tahan 10 dtk)
#define SS_PIN        5     // SPI SS RFID
#define RST_PIN       22    // RFID RST
#define BARCODE_RX    16    // UART2 Barcode Scanner
#define BARCODE_TX    17
```

**WT32main:**
```cpp
#define RELAY1_PIN    2     // Gate In
#define RELAY2_PIN    5     // Gate Out / Alarm
#define BUZZER_PIN    33    // Buzzer Eksternal
#define SS_PIN        12    // SPI SS RFID
#define RST_PIN       4     // RFID RST
#define BARCODE_RX    39    // UART2 Barcode Scanner (input-only pin)
#define BARCODE_TX    32
// Ethernet LAN8720: MDC:23, MDIO:18, PHY_ADDR:1, PWR:16
```

### C. Tipe Device & Scanner (Dikonfigurasi via Web Dashboard)

Device ini mendukung dua mode operasi yang dapat diganti dari dashboard lokal (Port 80):

| Setting | Nilai | Keterangan |
| :--- | :--- | :--- |
| **Tipe Device** | `gate` | Gerbang — trigger Relay 1 saat akses valid |
| **Tipe Device** | `kasir` | Kasir — tidak ada trigger relay fisik |
| **Tipe Scanner** | `rfid` | Baca dari sensor MFRC522 |
| **Tipe Scanner** | `qr` | Baca dari Barcode Scanner UART2 |

### D. Format MQTT Topic Dinamis

Topic MQTT dibentuk otomatis dari konfigurasi **Tipe Device** dan **Client ID**:

```
📤 Publish  : {device_type}/{mqtt_client_id}/scan/in
📥 Subscribe: {device_type}/{mqtt_client_id}/result
              {device_type}/{mqtt_client_id}/command
```

**Contoh** (device_type=`gate`, client_id=`gate_esp32_01`):
```
📤 gate/gate_esp32_01/scan/in
📥 gate/gate_esp32_01/result
📥 gate/gate_esp32_01/command
```

### E. Perintah Remote via MQTT Command Topic

Server backend dapat mengirim perintah langsung ke perangkat melalui topic `/command`:

```json
{ "command": "open_gate" }    // Trigger Relay 1 (Gerbang Utama)
{ "command": "open_gate_2" }  // Trigger Relay 2 (Alarm/Aux)
{ "command": "reboot" }       // Restart board dari jarak jauh
```

### F. Buzzer Nada WT32main

WT32main memiliki sistem buzzer non-blocking dengan 3 pilihan nada yang dapat diatur dari dashboard tanpa restart:

| Mode | Pilihan Nada |
| :--- | :--- |
| **Akses Diterima (Allow)** | `0` Happy Chime (C-E-G naik) · `1` Beep Tunggal Tinggi · `2` Beep Ganda Cepat |
| **Akses Ditolak (Deny)** | `0` Sad Tone (beep panjang rendah) · `1` Beep Rendah Ganda · `2` Alarm Cepat Berulang |

---

## 5. Format Pengujian Mandiri & Troubleshooting

### A. Simulasi Scan Kartu via MQTTX (Tanpa Hardware)
Jika Anda belum merakit perangkat keras ESP32 namun ingin menguji fungsionalitas visual pop-up Kiosk Next.js:
1. Hubungkan MQTTX Client ke broker EMQX Anda di port WebSockets (`ws://localhost:8083/mqtt`).
2. Gunakan kredensial: Username: `kiosk_nextjs` | Password: `sandi_next`.
3. Lakukan publish JSON ke topik Kios yang Anda daftarkan (misalnya `gate/lobby/result`):
   * **Payload Akses Diterima (Valid):**
     ```json
     {
       "status": "valid",
       "name": "Budi Santoso",
       "message": "Saldo terpotong: Rp 5.000"
     }
     ```
   * **Payload Akses Ditolak (Invalid):**
     ```json
     {
       "status": "invalid",
       "name": "Kartu Tidak Dikenal",
       "message": "Saldo tidak cukup atau kartu diblokir"
     }
     ```

### B. Masalah Umum & Solusi
* **Notifikasi "Offline (Tidak Aman)" di Pojok Kios**:
  Pastikan EMQX broker Anda aktif dan port WebSockets `8083` tidak terblokir oleh firewall Windows. Browser **tidak bisa** terhubung ke port TCP biasa (`1883`).
* **Database SQLAlchemy / PostgreSQL Static Type Error**:
  Saat pertama kali menjalankan `run.py`, jika database PostgreSQL tidak dapat dihubungkan, pastikan konfigurasi host, port, username, dan password di `DATABASE_URL` sudah benar dan service PostgreSQL Windows/Linux sudah berstatus *Running*.
* **OTA Update Gagal pada ESP32**:
  Pastikan repositori GitHub Anda berstatus publik agar file raw URL (`https://raw.githubusercontent.com/...`) dapat diakses secara bebas oleh klien HTTPClientSecure ESP32 tanpa proses autentikasi akun GitHub.