# Panduan Menjalankan Sistem Kiosk Smart Gate (Wanara Seta)

Sistem ini terdiri dari dua bagian yang saling terhubung:
1. **Next.js Frontend (Kiosk UI)** - Berjalan pada Port 3000.
2. **Python Flask Backend (Admin Server & API)** - Berjalan pada Port 5000.

---

## 1. Setup & Menjalankan Flask Admin Server (Port 5000)

Server Flask bertindak sebagai panel kontrol admin dan menyediakan API konfigurasi Kiosk.

### Prasyarat
- Python 3.8 atau lebih baru.

### Langkah-langkah
1. Buka terminal baru dan masuk ke folder `admin-server`:
   ```bash
   cd admin-server
   ```
2. Instal semua dependensi yang diperlukan:
   ```bash
   pip install -r requirements.txt
   ```
3. Jalankan server Flask:
   ```bash
   python app.py
   ```
   Server akan berjalan secara lokal di `http://localhost:5000`.

### Kredensial Login
- **Username:** `admin`
- **Password:** `admin123`

---

## 2. Setup & Menjalankan Next.js Kiosk UI (Port 3000)

Layar Kiosk UI yang akan terpasang di monitor fisik untuk menampilkan slideshow media dan notifikasi scan kartu RFID.

### Prasyarat
- Node.js v18 atau lebih baru.

### Langkah-langkah
1. Buka terminal baru di root direktori project (`kioks_web`).
2. Jalankan development server Next.js:
   ```bash
   npm run dev
   ```
   Kiosk UI akan berjalan secara lokal di `http://localhost:3000`.

---

## 3. Konfigurasi Awal & Cara Kerja Integrasi

Kiosk UI memisahkan identitas layar fisik (**TV ID**) dari saluran komunikasi scan (**MQTT Topic**).

### Langkah Konfigurasi Kios:
1. Masuk ke dashboard admin di `http://localhost:5000/login` menggunakan kredensial admin.
2. Daftarkan Kiosk baru, contoh:
   - **TV ID:** `TV-LOBBY`
   - **MQTT Topic:** `gate/lobby/result`
3. Unggah file gambar atau video (bisa multi-file sekaligus) pada halaman edit Kiosk untuk mengatur slideshow latar belakang.
4. Buka monitor/browser Kiosk Anda. Konfigurasikan TV ID-nya dengan salah satu cara berikut:
   - **Cara A (Query Parameter):** Buka URL `http://localhost:3000?tv_id=TV-LOBBY` sekali saja. Sistem akan mendeteksi parameter tersebut, menyimpannya ke `localStorage`, dan membersihkan URL secara otomatis.
   - **Cara B (Console Browser):** Buka `http://localhost:3000`, tekan F12 untuk membuka konsol pengembang, jalankan perintah berikut, lalu muat ulang halaman:
     ```javascript
     localStorage.setItem('tv_id', 'TV-LOBBY'); location.reload();
     ```
5. Kiosk akan memuat data slideshow dan mulai mendengarkan scan kartu pada MQTT broker.

---

## 4. Simulasi / Uji Coba MQTT Scan Kartu

Untuk melakukan simulasi RFID Scan, kirimkan (Publish) pesan JSON ke MQTT Broker EMQX Anda (misal `ws://10.127.10.8:8083/mqtt` atau broker lokal yang Anda arahkan) ke topik yang telah dikonfigurasi (e.g. `gate/lobby/result`).

### Payload 1: Akses Diterima (Green/Neon Lime Accent)
```json
{
  "status": "valid",
  "name": "Rifandi",
  "message": "Berlaku s/d Juni 2027"
}
```

### Payload 2: Akses Ditolak (Red Accent)
```json
{
  "status": "invalid",
  "name": "Member Tidak Dikenal",
  "message": "Kartu tidak aktif atau kadaluwarsa"
}
```

*Catatan: Popup notifikasi aktif akan muncul selama 4 detik di layar Kiosk sebelum kembali otomatis ke slideshow latar belakang.*
