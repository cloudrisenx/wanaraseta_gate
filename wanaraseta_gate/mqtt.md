# Panduan & Alur Kerja MQTT (EMQX) - Wanara Seta Gate

Dokumen ini menjelaskan alur komunikasi MQTT antara perangkat keras (ESP32), MQTT Broker (EMQX), dan Server Backend (Flask) beserta panduan cara setup server MQTT dari nol.

---

## 1. Konsep Dasar MQTT di Sistem Ini

Sistem ini menggunakan arsitektur **MQTT + Webhook**. Mengapa? Agar ESP32 tidak perlu menunggu respons HTTP yang berat dan lama. ESP32 hanya bertugas melempar data (Publish) dan mendengarkan perintah (Subscribe).

Aktor yang terlibat:
1. **Publisher (Pengirim):** ESP32 (mengirim UID RFID) & Flask (mengirim perintah buka gerbang).
2. **Subscriber (Penerima):** ESP32 (mendengarkan perintah buka gerbang).
3. **Broker (Jembatan):** EMQX (menerima lalu meneruskan semua pesan).
4. **Webhook:** Fitur EMQX yang otomatis mengubah pesan MQTT menjadi HTTP POST ke Backend Flask.

---

## 2. Alur Kerja (Skenario Utama Scan RFID)

Berikut adalah perjalanan data saat seseorang menempelkan kartu RFID di gerbang (Misal: Gate 2):

1. **Scan:** ESP32 `Gate_02` membaca UID RFID (Contoh: `A1B2C3D4`).
2. **Publish:** ESP32 mengirim JSON `{"token":"A1B2C3D4", "device_id":"Gate_02"}` ke topik `wanara/gate/scan/request` di EMQX.
3. **Webhook Bridge:** EMQX menerima pesan tersebut. Karena sudah di-setup Webhook, EMQX langsung mengubahnya menjadi Request HTTP POST dan menembaknya ke `http://IP_FLASK:5003/api/gate/scan`.
4. **Validasi Backend:** Flask memproses data, mengecek database (Apakah kartu ini terdaftar? Apakah saldonya cukup?).
5. **Server Publish:** Flask mengambil keputusan (misal: Granted/Diizinkan). Flask mengirim pesan balasan `{"result": "granted"}` ke topik `wanara/gate/Gate_02/response` via MQTT.
6. **ESP32 Subscribe:** Karena `Gate_02` sudah standby (subscribe) di topiknya sendiri, ia akan langsung menerima pesan tersebut.
7. **Aksi Fisik:** ESP32 menyalakan Relay (buka gerbang) dan menghidupkan LED hijau.

---

## 3. Apa Saja yang Perlu Disiapkan?

Untuk menjalankan ekosistem ini, Anda membutuhkan:
1. **PC / Server Local (Ubuntu / Windows)**: Komputer tempat EMQX dan Flask akan berjalan. Disarankan satu jaringan LAN/WiFi dengan ESP32. Pastikan IP-nya Statis (contoh API Contract: `192.168.4.50`).
2. **EMQX Broker**: Server MQTT yang diinstal secara native (tanpa Docker). Kita akan pakai versi 5.0+.
3. **Flask Server**: Backend API (Python) yang berjalan di port `5003`.

---

## 4. Langkah-Langkah Setup EMQX

### A. Menjalankan EMQX (Native)
Karena sistem Anda berjalan secara **native tanpa Docker**, berikut adalah panduan instalasi/run dasarnya:

**Jika menggunakan Ubuntu Server (Sesuai Production API Contract):**
1. Tambahkan repository dan install EMQX:
   ```bash
   curl -s https://assets.emqx.com/scripts/install-emqx-deb.sh | sudo bash
   sudo apt-get install emqx
   ```
2. Jalankan dan aktifkan EMQX agar otomatis menyala saat server restart:
   ```bash
   sudo systemctl start emqx
   sudo systemctl enable emqx
   ```

**Jika menggunakan Windows (Untuk Development Lokal PC Anda):**
1. Unduh versi Windows (.zip) dari situs resmi EMQX.
2. Ekstrak file tersebut (misalnya di `C:\emqx`).
3. Buka CMD/Powershell sebagai Administrator, arahkan ke folder tersebut, dan ketik:
   ```cmd
   bin\emqx start
   ```

*Penjelasan Port Default:*
- `1883`: Port yang dipakai ESP32 untuk komunikasi MQTT.
- `18083`: Port Web Dashboard untuk kita (Admin) mengatur EMQX.

### B. Setup Authentication (Wajib Login)
Secara bawaan, EMQX mengizinkan siapa saja konek tanpa password. Kita harus menutup celah ini.
1. Buka browser, masuk ke Dashboard EMQX: `http://localhost:18083` (atau IP server `http://192.168.4.50:18083`).
2. Login dengan Username `admin` dan Password `public` (Anda akan diminta ganti password). GPbandungan2026
3. Masuk ke menu **Access Control** -> **Authentication**.
4. Klik **+ Create** -> Pilih **Password-Based** -> **Built-in Database** -> Klik **Create**.
5. Setelah terbuat, klik tombol **Users** pada baris tersebut.
6. Tambahkan kredensial untuk setiap Gate (Contoh untuk Gate 2):
   - Username: `Gate_02`
   - Password: `11223344`
   - Klik **Save**.
*Catatan: Pastikan Username dan Password ini SAMA dengan yang tertulis di kode `main.cpp` ESP32.*

### C. Setup Webhook (Dari EMQX ke Flask)
Langkah ini untuk meneruskan data scan dari ESP32 ke Flask.
1. Di Dashboard EMQX, masuk ke menu **Integration** -> **Data Bridge**.
2. Klik **+ Create**, lalu pilih **HTTP Server**.
3. Isi konfigurasi berikut:
   - **Method:** `POST`
   - **URL:** `http://192.168.4.50:5003/api/gate/scan` (Ganti IP sesuai PC Flask Anda).
   - **Body:** Tulis `${payload}` (Ini memastikan isi pesan ESP32 diteruskan mentah-mentah ke Flask).
4. Klik **Create**. Anda akan ditawari untuk membuat *Rule* (Aturan), klik **Create Rule**.
5. Pada kolom **SQL**, masukkan kode ini:
   ```sql
   SELECT payload FROM "wanara/gate/scan/request"
   ```
6. Klik **Save**. Sekarang, setiap pesan yang masuk ke `wanara/gate/scan/request` akan otomatis dikirim ke Flask.

### D. (Opsional tapi Direkomendasikan) Setup Authorization / ACL
Agar `Gate_02` tidak iseng mematikan gerbang `Gate_01`, kita batasi hak akses mereka (Authorization).
1. Masuk ke **Access Control** -> **Authorization**.
2. Klik **+ Create** -> **Built-in Database** -> **Create**.
3. Klik **Permissions** pada Authorization yang baru dibuat, klik **+ Add**.
4. Buat Rule 1 (Hak Publish Scan):
   - Target: `Username`, Value: `Gate_02`
   - Action: `Publish`
   - Topic: `wanara/gate/scan/request`
5. Buat Rule 2 (Hak Subscribe ke diri sendiri):
   - Target: `Username`, Value: `Gate_02`
   - Action: `Subscribe`
   - Topic: `wanara/gate/Gate_02/#`
6. Buat Rule 3 (Hak Subscribe Update Global):
   - Target: `Username`, Value: `Gate_02`
   - Action: `Subscribe`
   - Topic: `wanara/gate/whitelist/update`

Ulangi pembuatan rule di atas untuk setiap gate (misal `Wanara_Gate_01`).

---

## 5. Daftar Topik (Topic Dictionary)

Sebagai referensi cepat, berikut daftar topik yang digunakan di sistem ini berdasarkan API Contract:

| Topik | Publisher | Subscriber | Fungsi |
|-------|-----------|------------|--------|
| `wanara/gate/scan/request` | ESP32 | EMQX (Webhook) | Tempat ESP32 mengirim data UID kartu. |
| `wanara/gate/{device_id}/response` | Server Flask | ESP32 | Jawaban dari server (Granted/Denied) untuk gate spesifik. |
| `wanara/gate/{device_id}/command` | Server Flask | ESP32 | Perintah override (Buka paksa, Restart ESP32). |
| `wanara/gate/whitelist/update` | Server Flask | Semua ESP32 | Sinyal broadcast ke semua gate untuk download ulang data whitelist saat ada perubahan member Master. |

---

## 6. Cara Testing Sistem (Simulasi)

Jika Backend Flask belum siap, tapi Anda ingin mengetes apakah ESP32 bisa membuka gerbang via MQTT:
1. Buka EMQX Dashboard.
2. Masuk ke menu **Diagnose** -> **WebSocket Client**.
3. Buat koneksi (Isi sembarang client ID).
4. Di bagian bawah (Publish), masukkan:
   - **Topic:** `wanara/gate/Gate_02/response`
   - **Payload:** 
     ```json
     {
       "result": "granted",
       "message": "Akses diberikan"
     }
     ```
5. Klik **Publish**.
6. Lihat ke alat ESP32 / Serial Monitor. Jika LED menyala dan ada tulisan *Gerbang Terbuka*, berarti fungsi MQTT Subscribe di ESP32 sudah bekerja dengan sempurna!