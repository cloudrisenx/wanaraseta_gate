# Rangkuman Pengembangan Proyek: Smart Gate Universal Kiosk System

Dokumen ini merangkum seluruh arsitektur, fitur yang diimplementasikan, cara kerja, solusi atas kendala teknis, dan petunjuk penggunaan sistem Kiosk Gate. Dokumen ini siap digunakan sebagai referensi atau input konteks untuk sesi chat LLM (seperti Gemini).

---

## 1. Arsitektur & Struktur Proyek
Sistem ini menggunakan arsitektur **Two-Tier** yang memisahkan panel manajemen dan tampilan TV Kiosk:

```
kioks_web/ (Root - Next.js Frontend App Router)
├── admin-server/ (Flask Admin Backend Server)
│   ├── app.py (Entry point Flask, API endpoints & Auth)
│   ├── models.py (Definisi database SQLAlchemy 2.0)
│   └── templates/ (Base, Login, Dashboard, Edit UI)
├── src/
│   └── app/
│       ├── page.tsx (Kiosk Screen Client Logic)
│       └── globals.css (Animasi, Fade, Custom Scrollbar)
└── instance/
    └── kiosk.db (Database SQLite)
```

* **Frontend:** Next.js (Port `3000`) - Bertindak sebagai layar Kiosk TV yang interaktif.
* **Backend:** Flask & SQLAlchemy (Port `5000`) - Mengelola konfigurasi dan upload file media.
* **Database:** SQLite (lokal di folder `instance/`).
* **Protokol Komunikasi:** MQTT WebSockets (via EMQX Broker) untuk menerima data scan kartu RFID secara real-time.

---

## 2. Fitur-Fitur Utama yang Telah Diimplementasikan

### A. Tampilan Kiosk (Frontend - Next.js)
1. **Slideshow Media Pintar (Auto Loop):**
   * Mendukung kombinasi file **Gambar** dan **Video (MP4, WebM, OGG)**.
   * Transisi antarmedia menggunakan efek cross-fade yang mulus (`animate-fade-in`).
2. **Logika Audio Cerdas (Background Music & Video Audio):**
   * Kios dapat memutar **Musik Latar** opsional yang diunggah dari dashboard admin.
   * **Aturan Main Audio:** Musik latar hanya berputar saat slide aktif adalah **Gambar**. Ketika slide aktif berganti menjadi **Video**, musik latar akan di-pause secara otomatis agar suara video tidak bertubrukan, dan dilanjutkan kembali ketika kembali ke slide Gambar.
   * **Unmute Interaction:** Karena pembatasan browser modern terhadap autoplay audio, layar akan meminta interaksi klik pertama (yang juga mengaktifkan mode fullscreen) untuk mengaktifkan/unmute seluruh audio.
3. **Popup Scan Kartu RFID:**
   * Overlay responsif di bagian tengah layar saat ada pesan masuk via MQTT.
   * Desain kartu modern dengan efek *Glassmorphism* dan *Neon-glow glow*:
     * **Akses Diterima (Valid):** Glow hijau neon dengan ikon perisai centang, menampilkan nama member dan masa berlaku.
     * **Akses Ditolak (Invalid):** Glow merah neon dengan ikon peringatan bahaya.
   * Popup otomatis menghilang dalam **4 detik** dan mereset status blur layar latar belakang.
4. **Auto-Hide Status Overlay (Terbaru):**
   * Status TV ID dan Status Broker MQTT di pojok kiri bawah layar hanya ditampilkan selama **3 detik pertama** saat halaman dimuat, lalu menghilang secara perlahan menggunakan transisi opacity dan translate Tailwind CSS (`opacity-0 translate-y-2 pointer-events-none`).
   * Menampilkan host broker MQTT yang terhubung (diambil dari `localStorage` atau default `10.127.10.8:8083`) beserta indikator keamanan status koneksi: `Connected (Aman)` atau `Offline (Tidak Aman)`.
5. **Akses Admin Tersembunyi:**
   * Terdapat *invisible link trigger* di pojok kanan atas layar Kiosk untuk navigasi instan ke halaman login admin.

### B. Panel Admin (Backend - Flask)
1. **Autentikasi Sederhana:**
   * Login session-based (`/login`) dengan kredensial:
     * **Username:** `admin`
     * **Password:** `admin123`
2. **Pengecekan Status MQTT Broker Aktif (Terbaru):**
   * Backend Flask secara berkala melakukan socket-ping ke port TCP MQTT (`10.127.10.8:1883`) saat dashboard admin dimuat.
   * Dashboard menampilkan indikator dinamis: `Online (Aman)` dengan badge hijau jika broker aktif, atau `Offline (Tidak Aman)` dengan badge merah jika broker tidak dapat dijangkau.
3. **Manajemen Kiosk:**
   * Menampilkan daftar Kiosk/TV terdaftar beserta status MQTT Topic.
   * Pendaftaran Kiosk baru berdasarkan **TV ID** dan **MQTT Topic** (MQTT Topic bersifat opsional; Kios tanpa topic hanya akan bertindak sebagai TV Slideshow murni).
4. **Multi-File Upload & Delete:**
   * Mendukung unggah banyak file sekaligus untuk media slideshow.
   * Opsi khusus untuk mengunggah satu file musik latar (Background Music).
   * Fitur hapus file media satu per satu langsung dari halaman edit.

---

## 3. Cara Menjalankan Aplikasi

### A. Menjalankan Flask Admin Server
1. Masuk ke direktori `admin-server/`:
   ```bash
   cd admin-server
   ```
2. Instal dependensi:
   ```bash
   pip install -r requirements.txt
   ```
3. Jalankan server:
   ```bash
   python app.py
   ```
   Server admin akan berjalan di `http://localhost:5000`.

### B. Menjalankan Next.js Kiosk UI
1. Buka terminal baru di root direktori project (`kioks_web/`).
2. Jalankan dev server:
   ```bash
   npm run dev
   ```
   Kiosk UI akan berjalan di `http://localhost:3000`.

### C. Menghubungkan TV Kiosk ke Konfigurasi
Untuk menghubungkan layar browser ke TV ID tertentu, cukup buka URL dengan query parameter:
* `http://localhost:3000?tv_id=TV-PROMO`
Next.js akan menyimpan `tv_id` ke dalam `localStorage` perangkat dan otomatis membersihkan parameter URL agar tampilan tetap bersih.

---

## 4. Solusi Masalah Teknis Penting (Key Fixes)

1. **React Video Muted Gotcha:**
   * Mengubah properti `muted={isMuted}` secara dinamis dalam state React sering kali tidak mengubah properti DOM mentah pada tag `<video>` setelah di-render pertama kali.
   * **Solusi:** Menggunakan hook `useEffect` dengan referensi langsung ke elemen DOM (`videoRefs.current.forEach(video => { video.muted = isMuted })`) untuk memastikan sinkronisasi suara video bekerja sempurna.
2. **SQLAlchemy 2.0 Model Static Analysis & Schema Errors:**
   * Penggunaan declarative mapping modern di SQLAlchemy 2.0 sering kali memicu error analisis tipe statis (`MappedAnnotationError`).
   * **Solusi:** Menambahkan deklarasi `__allow_unmapped__ = True` dan membuat konstruktor manual (`__init__`) di class model di `models.py`.
3. **Dynamic MQTT Imports:**
   * Karena Next.js merender halaman di server-side (SSR) terlebih dahulu secara default, modul WebSocket Client (`mqtt`) yang membutuhkan API client-side `window` akan error jika langsung di-import di tingkat atas file.
   * **Solusi:** Memuat pustaka secara dinamis menggunakan import asinkron dalam block `useEffect` client-side (`const mqtt = await import('mqtt')`).
4. **Transisi Status Overlay:**
   * Menggunakan class utilitas Tailwind:
     ```css
     transition-all duration-1000 ${showOverlay ? 'opacity-100 translate-y-0' : 'opacity-0 translate-y-2 pointer-events-none'}
     ```
     Ini membuat overlay hilang dengan efek memudar (fade-out) dan bergeser perlahan ke bawah tanpa menghalangi klik pointer setelah disembunyikan.
