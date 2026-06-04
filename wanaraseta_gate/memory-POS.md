# Wanara Seta — Gate Access & RFID E-Wallet System
**PRD v1.1 | Juni 2026 | Status: Aktif**

---

## Keputusan Arsitektur Resmi

> Keputusan berikut telah dikonfirmasi dan dikunci. Update terakhir: 4 Juni 2026.

| # | Topik | Keputusan |
|---|-------|-----------|
| A | Firmware ESP32 | **Dipegang vendor/teknisi hardware** — tim ini hanya mendefinisikan API contract. Tidak ada kode firmware yang ditulis di sini. |
| B | API gate endpoint | **`gate-service` terpisah (port 5004)** — bukan bagian dari `pos-service`. Dipisah karena auth berbeda (device_token, bukan JWT) dan latency kritis untuk hardware. |
| C | Notifikasi admin | **n8n + Telegram** — `gate-service` POST ke webhook n8n, n8n forward ke Telegram bot admin. Flask tidak kirim Telegram langsung. Tabel `admin_notifications` tetap ada sebagai audit trail di DB. |
| D | Database engine | **PostgreSQL** (sesuai SOT) — sintaks MySQL di schema lama sudah dikonversi ke PostgreSQL + Prisma. |

---

## Project Overview

Wanara Seta adalah wahana air yang membutuhkan sistem akses gate modern dan ekosistem pembayaran digital internal. Sistem menggantikan tiket kertas dan transaksi tunai dengan mekanisme akses hybrid: QR harian untuk guest dan RFID gelang untuk member.

---

## Tipe Akses (3 jenis)

### 1. QR Guest (Harian)
- **Media:** Kertas QR code — dicetak massal sebelumnya
- **Aktivasi:** Kasir scan QR saat pengunjung membeli → status `unactivated` → `active`, `valid_date` = hari ini
- **Gate:** Sekali pakai — setelah berhasil membuka gate → status langsung `used`
- **Berlaku:** Hanya pada `valid_date` (tanggal aktivasi kasir)
- **Tidak jadi datang:** HANGUS — tidak ada reschedule, tidak ada refund
- **Transaksi outlet:** ❌ Tidak bisa — tidak punya saldo
- **Identitas:** Anonim

### 2. RFID Member (Permanen)
- **Media:** Gelang RFID — diterbitkan saat pendaftaran member
- **Gate:** Biaya masuk dipotong dari saldo gelang otomatis
- **Saldo:** Tidak hangus — tersimpan permanen hingga digunakan
- **Transaksi outlet:** ✅ Tap gelang di semua outlet dalam wahana
- **Topup:** Bisa di pos utama **maupun di semua outlet** kapan saja
- **Identitas:** Ada profil (nama, HP, email)
- **Gelang hilang:** Admin blokir dari dashboard → saldo pindah ke gelang baru

### 3. RFID Master (Operator/Security)
- **Media:** Kartu atau gelang RFID khusus — dipegang staff terpercaya
- **Fungsi:** Buka gate kapan saja, unlimited, bypass semua validasi
- **Pemegang:** Security, operator gate, teknisi, manajer operasional
- **Offline:** ✅ Tetap bisa buka gate meski server tidak terkoneksi (whitelist lokal ESP32)
- **Log:** Setiap penggunaan dicatat di `gate_logs`
- **Notifikasi:** gate-service POST ke webhook n8n → n8n kirim pesan Telegram ke admin

---

## Prioritas Scan Gate

```
1. RFID Master  →  2. QR Guest  →  3. RFID Member
```

---

## Alur Sistem

### QR Guest
1. Tiket QR dicetak massal → status `unactivated`
2. Pengunjung beli di loket → kasir scan → status `active`, `valid_date` = hari ini
3. Pengunjung tap QR di gate
4. Validasi: `active`? `valid_date` = hari ini? `used_at` = NULL?
5. Jika valid → buka gate → status `used`, `used_at` = NOW()
6. QR tidak bisa digunakan kembali

### RFID Member
1. Daftar member → admin issued gelang RFID
2. Topup saldo di pos utama atau outlet manapun
3. Tap gelang di gate → cek saldo ≥ harga masuk → debit → buka gate
4. Di dalam wahana: tap gelang di outlet → debit saldo → transaksi tercatat
5. Sisa saldo tetap tersimpan untuk kunjungan berikutnya

### RFID Master
1. Staff tap kartu/gelang master di gate
2. ESP32 cek whitelist lokal (tidak perlu tunggu server)
3. Jika token ada di whitelist → buka gate langsung
4. ESP32 POST ke `gate-service` (jika online) → insert `gate_logs` + `admin_notifications`
5. gate-service POST ke webhook n8n → n8n kirim notifikasi Telegram ke admin

### Logika Validasi Gate
```
SCAN token
  │
  ├─ Ada di rfid_masters & active?
  │    → GRANTED (bypass semua validasi)
  │    → UPDATE last_used_at = NOW()
  │    → INSERT gate_logs (is_master_used: true)
  │    → INSERT admin_notifications
  │
  ├─ type = qr_guest?
  │    → unactivated?         DENIED 'not_activated'
  │    → used?                DENIED 'already_used'
  │    → expired?             DENIED 'expired'
  │    → valid_date != today? DENIED 'wrong_date'
  │    → GRANTED
  │         UPDATE status='used', used_at=NOW()
  │         INSERT gate_logs
  │
  └─ type = rfid_member?
       → blocked?             DENIED 'blocked'
       → saldo < harga?       DENIED 'insufficient_balance'
       → GRANTED
            UPDATE saldo = saldo - harga_masuk
            INSERT transactions
            INSERT gate_logs
```

---

## Database Schema

### tickets
```sql
CREATE TABLE tickets (
  id             BIGINT AUTO_INCREMENT PRIMARY KEY,
  type           ENUM('qr_guest','rfid_member') NOT NULL,
  token          VARCHAR(255) UNIQUE NOT NULL,

  -- QR Guest
  status         ENUM('unactivated','active','used','expired') DEFAULT 'unactivated',
  valid_date     DATE NULL,
  activated_at   DATETIME NULL,
  activated_by   INT NULL,          -- FK -> users (kasir)
  used_at        DATETIME NULL,
  used_gate_id   INT NULL,          -- FK -> gates

  -- RFID Member
  member_id      INT NULL,          -- FK -> members
  saldo          DECIMAL(10,2) DEFAULT 0,
  rfid_status    ENUM('active','blocked') DEFAULT 'active',
  issued_at      DATETIME NULL
);
```

### rfid_masters
```sql
CREATE TABLE rfid_masters (
  id             INT AUTO_INCREMENT PRIMARY KEY,
  token          VARCHAR(255) UNIQUE NOT NULL,
  label          VARCHAR(100),      -- 'Security-01', 'Manajer Ops'
  assigned_to    INT NULL,          -- FK -> users
  active         BOOLEAN DEFAULT TRUE,
  created_at     DATETIME DEFAULT NOW(),
  last_used_at   DATETIME NULL
);
```

### members
```sql
CREATE TABLE members (
  id             INT AUTO_INCREMENT PRIMARY KEY,
  name           VARCHAR(100) NOT NULL,
  phone          VARCHAR(20),
  email          VARCHAR(100),
  joined_at      DATETIME DEFAULT NOW()
);
```

### gate_logs
```sql
CREATE TABLE gate_logs (
  id             BIGINT AUTO_INCREMENT PRIMARY KEY,
  token          VARCHAR(255) NOT NULL,
  ticket_type    ENUM('qr_guest','rfid_member','rfid_master') NOT NULL,
  ticket_id      BIGINT NULL,       -- NULL jika master
  master_id      INT NULL,          -- FK -> rfid_masters
  gate_id        INT NOT NULL,
  scanned_at     DATETIME DEFAULT NOW(),
  result         ENUM('granted','denied') NOT NULL,
  deny_reason    VARCHAR(100) NULL, -- 'already_used','expired','wrong_date','blocked', dll
  is_master_used BOOLEAN DEFAULT FALSE
);
```

### admin_notifications
```sql
CREATE TABLE admin_notifications (
  id             BIGINT AUTO_INCREMENT PRIMARY KEY,
  type           ENUM('master_used','low_saldo','gate_error') NOT NULL,
  message        TEXT NOT NULL,
  ref_log_id     BIGINT NULL,       -- FK -> gate_logs
  is_read        BOOLEAN DEFAULT FALSE,
  created_at     DATETIME DEFAULT NOW()
);
```

### transactions
```sql
CREATE TABLE transactions (
  id             BIGINT AUTO_INCREMENT PRIMARY KEY,
  ticket_id      BIGINT NOT NULL,   -- FK -> tickets (rfid_member only)
  outlet_id      INT NOT NULL,      -- FK -> outlets
  type           ENUM('topup','debit') NOT NULL,
  amount         DECIMAL(10,2) NOT NULL,
  saldo_before   DECIMAL(10,2) NOT NULL,
  saldo_after    DECIMAL(10,2) NOT NULL,
  created_at     DATETIME DEFAULT NOW()
);
```

### users
```sql
CREATE TABLE users (
  id             INT AUTO_INCREMENT PRIMARY KEY,
  name           VARCHAR(100),
  role           ENUM('admin','kasir','operator') NOT NULL,
  username       VARCHAR(50) UNIQUE,
  password_hash  VARCHAR(255),
  active         BOOLEAN DEFAULT TRUE
);
```

### Cron Job — Auto Expire QR (setiap tengah malam)
```sql
UPDATE tickets
SET    status = 'expired'
WHERE  type = 'qr_guest'
  AND  status = 'active'
  AND  valid_date < CURDATE();
```

---

## Offline Mode — RFID Master

### Mekanisme
- Token master disimpan di **LittleFS** (flash storage ESP32)
- Saat scan: ESP32 cek whitelist lokal — tidak perlu tunggu server
- Timeout koneksi server: 2–3 detik
- Log scan offline disimpan di memori ESP32 sementara

### Perilaku per Kondisi
| Kondisi | QR Guest | RFID Member | RFID Master |
|---|---|---|---|
| Online normal | Validasi normal | Validasi normal | Granted + notif admin |
| Offline (server tidak respond) | DITOLAK | DITOLAK | Cek whitelist lokal → Granted |
| Kembali online | — | — | Sync offline log ke server |

### Sinkronisasi Setelah Online
1. ESP32 reconnect → kirim semua offline logs ke server
2. Server insert ke `gate_logs` dengan flag `offline_mode = true`
3. Server kirim notifikasi admin (gate sempat offline + jumlah scan)
4. ESP32 hapus offline logs setelah sync berhasil

### Update Whitelist dari Server
- Setiap ESP32 online → `GET /api/master-whitelist`
- Server kirim daftar token master yang `active`
- ESP32 simpan ke LittleFS (replace file lama)
- Jika admin tambah/blokir master → server push via MQTT/WebSocket → ESP32 update lokal

---

## Ekosistem Outlet & E-Wallet

Gelang RFID Member = dompet digital internal wahana. Satu gelang untuk semua transaksi.

| Outlet | Fungsi RFID |
|---|---|
| Gate Masuk | Debit biaya masuk dari saldo |
| Outlet Makanan & Minuman | Tap & bayar menu |
| Wahana & Atraksi | Tap akses per wahana, harga per wahana |
| Toko Souvenir | Tap & bayar produk |
| Loker & Penyewaan | Tap buka loker, tap sewa alat |
| Foto & Dokumentasi | Tap beli foto, UID link ke foto pengunjung |
| Pos Topup | Isi saldo — tersedia di semua outlet + pos utama |

---

## Hardware per Titik

| Titik | Komponen |
|---|---|
| Gate Masuk | Tripod Turnstile, RC522 RFID Reader, ESP32, 2-CH Relay, Power Supply 12V, Buzzer + LED |
| Tiap Outlet | RC522 RFID Reader, ESP32 (WiFi), LCD/OLED, Mini Thermal Printer (opsional) |
| Pos Topup | PC/Tablet kasir, RFID USB Reader, Software Kasir Web, Printer struk |
| Server | Server 192.168.4.50 — REST API Flask (pos-service port 5003, gate-service port 5004), PostgreSQL port 5433, n8n (notifikasi), Dashboard web |

---

## Dashboard Admin

- Monitor real-time: pengunjung aktif, total scan, saldo beredar
- Monitoring per outlet: pendapatan, transaksi, topup vs debit
- Manajemen tiket: cari by token/UID, lihat status & riwayat
- Manajemen member: profil, riwayat, saldo, blokir/unblokir gelang
- Manajemen RFID Master: tambah/hapus/nonaktifkan, log penggunaan
- Laporan keuangan: harian/mingguan/bulanan per outlet, export PDF & Excel
- Alert kapasitas wahana
- Notifikasi real-time setiap RFID Master dipakai
- Log akses gate lengkap (granted, denied, alasan)

### Format Notifikasi Master
```
⚠️  RFID Master digunakan
Label   : Security-01
Gate    : Gate Utama (ID: 1)
Waktu   : 14:32:05 — Sabtu, 31 Mei 2026
Mode    : Online
```

---

## Keamanan

| Aspek | Implementasi |
|---|---|
| Transaksi Atomik | Debit saldo + insert transaksi dalam 1 DB transaction |
| Token per Device | Setiap ESP32 punya device_token unik, request tanpa token ditolak |
| Enkripsi | Komunikasi via HTTPS/TLS, saldo tidak disimpan di device |
| Audit Trail | Setiap scan & transaksi dicatat lengkap, tidak bisa dihapus |
| Blokir Gelang | Bisa dari dashboard, saldo bisa dipindahkan ke gelang baru |
| Offline Fail-Secure | Guest & Member ditolak saat offline, hanya Master yang lewat |
| Whitelist Master | Hanya bisa diupdate oleh server yang authenticated |
| QR Anti-Reuse | Token QR 1x pakai, setelah used tidak bisa diubah kembali |
| QR Anti-Stockpile | QR hanya valid pada valid_date (tanggal aktivasi kasir) |
| Role-based Access | Admin, kasir, operator punya akses dashboard berbeda |

---

## Estimasi Biaya

| Komponen | Satuan | Estimasi Harga |
|---|---|---|
| Tripod Turnstile Gate | 1 unit/gate | Rp 3.000.000 – 6.000.000 |
| ESP32 + RC522 + Relay (gate) | 1 set/gate | Rp 150.000 – 250.000 |
| ESP32 + RC522 (outlet) | per outlet | Rp 80.000 – 150.000 |
| RFID Wristband Member | per pcs | Rp 14.000 |
| Kertas QR Cetak | per lembar | Rp 500 – 2.000 |
| Server / VPS | per bulan | Rp 50.000 – 200.000 |
| Software & Dashboard | 1x develop | Rp 5.000.000 – 15.000.000 |
| Kabel + Instalasi | per titik | Rp 100.000 – 300.000 |
| RFID Master Card | per kartu | Rp 5.000 – 15.000 |

**Total estimasi awal (1 gate + 4 outlet): ± Rp 10.000.000 – Rp 25.000.000**

---

## Roadmap

| Fase | Timeline | Deliverable |
|---|---|---|
| Fase 1 — Foundation | Bulan 1–2 | Setup gate, server+DB+API, QR Guest aktif, RFID Master + offline mode, uji internal |
| Fase 2 — Outlet Integration | Bulan 3–4 | RFID Member aktif, semua outlet integrated, dashboard live, laporan harian, training |
| Fase 3 — Expansion | Bulan 5–6 | Multi-gate, QRIS topup, analitik, soft-launch resmi |
| Fase 4 — Enhancement | Bulan 7+ | Aplikasi mobile, loyalty/poin, booking online, ekspansi outlet |

---

## Ringkasan Keputusan Resmi Meeting

| # | Topik | Keputusan |
|---|---|---|
| 1 | Media tiket harian | QR code dicetak di kertas |
| 2 | Cetak QR | Dicetak massal sebelumnya, diaktivasi kasir saat terjual |
| 3 | Aktivasi QR | Kasir wajib scan saat jual → status unactivated → active |
| 4 | Berlaku QR | Hanya valid pada tanggal aktivasi (valid_date) |
| 5 | Tidak jadi datang | HANGUS — tidak ada reschedule, tidak ada refund |
| 6 | Penggunaan QR | Sekali pakai — setelah buka gate status = used |
| 7 | Media member | Gelang RFID permanen dengan saldo (e-wallet) |
| 8 | Topup member | Bisa di pos utama MAUPUN di semua outlet |
| 9 | RFID Master | Ada — dipegang operator/security untuk bypass gate |
| 10 | Log master | SEMUA penggunaan master dicatat di gate_logs |
| 11 | Notifikasi master | Admin dapat notifikasi real-time setiap master dipakai |
| 12 | Offline master | RFID Master tetap bisa buka gate saat server offline |
| 13 | Offline guest/member | Ditolak — butuh validasi server |
| 14 | Sync offline | Log scan offline dikirim ke server saat koneksi kembali |

---

*Dokumen ini merupakan acuan resmi untuk development sistem Wanara Seta.*
*Setiap perubahan keputusan harus diupdate di dokumen ini dan dikomunikasikan ke seluruh tim.*
