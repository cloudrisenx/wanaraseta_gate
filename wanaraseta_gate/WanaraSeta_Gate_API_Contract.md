# Wanara Seta — Gate API Contract
**Versi:** 1.0 | **Status:** Draft Final | **Tanggal:** Mei 2026

> Dokumen ini adalah kontrak komunikasi antara **ESP32 (hardware/firmware team)** dan **Flask Server (backend team)**.
> Kedua tim bisa bekerja paralel selama mengikuti kontrak ini.
> Setiap perubahan endpoint atau payload **wajib dikoordinasikan ke kedua tim** sebelum diimplementasikan.

---

## Daftar Isi

1. [Prinsip Komunikasi](#1-prinsip-komunikasi)
2. [Autentikasi Device](#2-autentikasi-device)
3. [ESP32 → Server (HTTP REST)](#3-esp32--server-http-rest)
   - [POST /gate/scan (Via Webhook)](#31-post-gatescan-via-emqx-webhook)
   - [GET /gate/master-whitelist](#32-get-gatemaster-whitelist)
   - [POST /gate/sync-offline](#33-post-gatesync-offline)
   - [GET /gate/health](#34-get-gatehealth)
4. [Server → ESP32 (MQTT)](#4-server--esp32-mqtt)
   - [Topic: Scan Response](#41-topic-wanaragatdevice_idresponse)
   - [Topic: whitelist/update](#42-topic-wanaragatewhitelistupdate)
   - [Topic: gate/command](#43-topic-wanaragatdevice_idcommand)
5. [Kode Status & Error](#5-kode-status--error)
6. [Perilaku ESP32 per Skenario](#6-perilaku-esp32-per-skenario)
7. [Checklist Integrasi](#7-checklist-integrasi)

---

## 1. Prinsip Komunikasi

| Aspek | Keputusan |
|-------|-----------|
| Protokol utama device | MQTT murni via EMQX Broker di Ubuntu Server (port 1883) |
| Protokol integrasi server | EMQX Webhook (HTTP Callback POST) menembak Flask (port 5003) |
| Protokol push | MQTT Publish (Server → ESP32/WT32-ETH01) |
| Format payload | JSON |
| Timeout request | **2500ms** — jika tidak ada respons, ESP32 anggap offline |
| Offline fallback | Hanya RFID Master yang bisa lewat saat offline |
| Enkripsi | HTTPS (TLS) untuk produksi, HTTP boleh untuk development/testing |
| Base URL dev | `http://192.168.4.50:5003/api` |
| Base URL prod | `https://pos.griyapersada.local/api` (via Nginx) |

---

## 2. Autentikasi Device

Setiap ESP32 memiliki **device token unik** yang di-hardcode ke firmware saat provisioning.
Token ini dikirim di setiap request sebagai HTTP header.

```
X-Device-Token: esp32-gate-001-a3f9c2
```

**Aturan:**
- Server **tolak semua request** tanpa header `X-Device-Token` yang valid → `401`
- Token didaftarkan Admin IT ke database sebelum ESP32 diaktifkan
- Satu token = satu device — tidak boleh dipakai di dua ESP32 sekaligus
- Jika token dicabut (device rusak/hilang) → server tolak → ESP32 tidak bisa scan

---

## 3. ESP32 → Server (HTTP REST)

### 3.1 POST /gate/scan (Via EMQX Webhook)

**PENTING:** Endpoint ini **TIDAK** ditembak langsung oleh ESP32 atau WT32-ETH01. 
Data scan dikirim oleh device ke EMQX via MQTT. EMQX Webhook (HTTP Callback Data Integration) yang akan meneruskan data ke endpoint ini.

**Request (EMQX Webhook → Flask)**
```
POST /api/gate/scan
Content-Type: application/json
```

```json
{
  "token": "MASTER-A1B2C3D4",
  "device_id": "WT32-01"
}
```

| Field | Tipe | Wajib | Keterangan |
|-------|------|-------|-----------|
| `token` | string | ✅ | Nilai token yang dibaca (QR string atau UID RFID) |
| `device_id` | string | ✅ | ID perangkat hardware pengirim data |

---

**Response (Flask → EMQX Webhook)**

Karena ini adalah integrasi Webhook, Flask **hanya perlu membalas HTTP Status `200 OK`** (dengan body kosong atau JSON sederhana `{"status": "ok"}`) ke EMQX untuk menandakan pesan berhasil diterima.

> **Catatan Alur:** Keputusan akses (`granted`/`denied`) **TIDAK** dikirimkan via response HTTP ini. Flask akan memvalidasi ke PostgreSQL, lalu melakukan **MQTT Publish secara asynchronous** ke topik respons device untuk memerintahkan hardware. (Lihat Bab 4.1).

---

### 3.2 GET /gate/master-whitelist

Digunakan ESP32 untuk mengambil daftar token RFID Master yang aktif.
ESP32 menyimpan hasil ini ke LittleFS sebagai whitelist lokal untuk mode offline.

**Kapan dipanggil:**
- Saat ESP32 pertama kali boot / reconnect ke WiFi
- Setelah menerima perintah `refresh_whitelist` via MQTT

**Request**
```
GET /api/gate/master-whitelist
X-Device-Token: esp32-gate-001-a3f9c2
```

**Response**
```json
{
  "tokens": [
    "MASTER-A1B2C3D4",
    "MASTER-E5F6G7H8",
    "MASTER-I9J0K1L2"
  ],
  "total": 3,
  "generated_at": "2026-05-31T14:00:00+07:00"
}
```

> ESP32 **replace seluruh file whitelist lokal** dengan daftar ini — bukan append.

---

### 3.3 POST /gate/sync-offline

Digunakan ESP32 untuk mengirim log scan yang terjadi saat server tidak bisa dihubungi.
Dipanggil sesaat setelah koneksi ke server kembali tersedia.

**Request**
```
POST /api/gate/sync-offline
X-Device-Token: esp32-gate-001-a3f9c2
Content-Type: application/json
```

```json
{
  "gate_id": 1,
  "logs": [
    {
      "token": "MASTER-A1B2C3D4",
      "scanned_at": "2026-05-31T13:15:22+07:00",
      "local_result": "granted"
    },
    {
      "token": "MASTER-A1B2C3D4",
      "scanned_at": "2026-05-31T13:47:05+07:00",
      "local_result": "granted"
    }
  ]
}
```

| Field | Tipe | Keterangan |
|-------|------|-----------|
| `gate_id` | integer | ID gate pengirim log |
| `logs` | array | Daftar scan yang terjadi saat offline |
| `logs[].token` | string | Token yang di-scan |
| `logs[].scanned_at` | string ISO 8601 | Waktu scan di device |
| `logs[].local_result` | string | Hasil yang diberikan ESP32 (`granted` / `denied`) |

**Response**
```json
{
  "synced": 2,
  "failed": 0,
  "message": "Sync berhasil"
}
```

> Setelah mendapat response `synced` berhasil, **ESP32 wajib hapus log offline dari LittleFS**.
> Jika response gagal / timeout, ESP32 simpan log dan coba lagi nanti.

---

### 3.4 GET /gate/health

Digunakan ESP32 untuk mengecek apakah server bisa dihubungi sebelum mengirim scan.
Bisa juga dipakai untuk polling koneksi saat offline.

**Request**
```
GET /api/gate/health
X-Device-Token: esp32-gate-001-a3f9c2
```

**Response**
```json
{
  "status": "ok",
  "server_time": "2026-05-31T14:32:00+07:00"
}
```

> Timeout endpoint ini sama: **2500ms**. Tidak ada respons = server offline.

---

## 4. Server → ESP32 (MQTT)

Server mengirim perintah ke ESP32 via MQTT broker yang berjalan di server yang sama.

| Property | Value |
|----------|-------|
| Broker | `mqtt://192.168.4.50:1883` |
| Auth | Username + password per device |
| QoS | 1 (at least once) |

---

### 4.1 Topic: `wanara/gate/{device_id}/response`

Topik ini digunakan oleh Flask untuk merespons hasil keputusan akses kembali ke hardware spesifik setelah selesai memproses webhook dari EMQX. Hardware wajib *subscribe* ke topik ini untuk mengontrol relay (gerbang) dan layar display.

**Payload: GRANTED**
```json
{
  "result": "granted",
  "ticket_type": "qr_guest",
  "message": "Akses diberikan",
  "log_id": 5821
}
```

**Payload: GRANTED (RFID Member)**
```json
{
  "result": "granted",
  "ticket_type": "rfid_member",
  "message": "Akses diberikan",
  "saldo_remaining": 75000.00,
  "log_id": 5822
}
```

**Payload: GRANTED (RFID Master)**
```json
{
  "result": "granted",
  "ticket_type": "rfid_master",
  "message": "Master access granted",
  "label": "Security-01",
  "log_id": 5823
}
```

**Payload: DENIED**
```json
{
  "result": "denied",
  "ticket_type": "qr_guest",
  "reason": "already_used",
  "message": "Tiket sudah digunakan",
  "log_id": 5824
}
```

| `reason` | Arti | Tipe Akses |
|----------|------|------------|
| `not_activated` | QR belum diaktivasi kasir | qr_guest |
| `already_used` | QR sudah pernah dipakai | qr_guest |
| `expired` | QR sudah kadaluarsa | qr_guest |
| `wrong_date` | QR valid_date bukan hari ini | qr_guest |
| `blocked` | Gelang RFID diblokir admin | rfid_member |
| `insufficient_balance` | Saldo tidak cukup untuk biaya masuk | rfid_member |
| `unknown_token` | Token tidak dikenali sistem | semua |

---

### 4.2 Topic: `wanara/gate/whitelist/update`

Server publish ke topic ini ketika ada perubahan daftar RFID Master (tambah, nonaktifkan, hapus).
Semua ESP32 gate yang subscribe akan menerima perintah untuk refresh whitelist lokal.

**Payload**
```json
{
  "action": "refresh_whitelist",
  "reason": "master_added",
  "triggered_at": "2026-05-31T14:30:00+07:00"
}
```

**Tindakan ESP32:** Setelah menerima pesan ini → langsung panggil `GET /api/gate/master-whitelist` → update LittleFS.

---

### 4.3 Topic: `wanara/gate/{device_id}/command`

Server kirim perintah spesifik ke satu gate tertentu.

**Payload**
```json
{
  "command": "open_gate",
  "reason": "manual_override",
  "operator_id": 7,
  "triggered_at": "2026-05-31T14:35:00+07:00"
}
```

| `command` | Tindakan ESP32 |
|-----------|----------------|
| `open_gate` | Buka gate sekali (manual override dari dashboard) |
| `refresh_whitelist` | Fetch ulang whitelist dari server |
| `reboot` | Restart ESP32 (untuk maintenance remote) |

---

## 5. Kode Status & Error

### Format Error Response (semua endpoint)
```json
{
  "error": true,
  "code": "INVALID_DEVICE_TOKEN",
  "message": "Device token tidak dikenali"
}
```

| `code` | HTTP Status | Penyebab |
|--------|-------------|---------|
| `INVALID_DEVICE_TOKEN` | 401 | Header X-Device-Token tidak ada atau tidak valid |
| `MISSING_FIELD` | 422 | Field wajib tidak ada di payload |
| `INVALID_GATE_ID` | 422 | gate_id tidak ditemukan di database |
| `SERVER_ERROR` | 500 | Error internal Flask/database |

---

## 6. Perilaku ESP32 per Skenario

### Skenario A — Online Normal
```
ESP32 boot
  → GET /gate/health (cek koneksi)
  → GET /gate/master-whitelist (update whitelist lokal)
  → Subscribe MQTT topics (termasuk wanara/gate/{device_id}/response)
  → Siap menerima scan

Ada scan masuk
  → Device Publish MQTT ke topik `wanara/gate/scan/request`
  → EMQX meneruskan via Webhook (POST /api/gate/scan) ke Flask
  → Flask memproses ke PostgreSQL (validasi & insert log)
  → Flask melakukan MQTT Publish balik ke topik `wanara/gate/{device_id}/response`
  → Device menerima MQTT Payload
  → Jika result == "granted" → buka relay → buzzer OK → LED hijau
  → Jika result == "denied"  → jangan buka relay → buzzer gagal → LED merah
```

### Skenario B — Server Offline Saat Scan
```
Ada scan masuk
  → POST /gate/scan (timeout 2500ms)
  → Tidak ada response / connection refused

  ├─ Cek token di whitelist lokal LittleFS
  │    Token ADA → buka gate → simpan log ke offline_logs.json → LED hijau
  │    Token TIDAK ADA → tolak → LED merah
  │
  └─ (QR Guest & RFID Member selalu ditolak saat offline)
```

### Skenario C — Kembali Online
```
ESP32 deteksi koneksi kembali (polling GET /gate/health setiap 10 detik)
  → POST /gate/sync-offline (kirim semua log di offline_logs.json)
  → Response "synced" berhasil → hapus offline_logs.json
  → GET /gate/master-whitelist (refresh whitelist)
```

### Skenario D — Admin Update Whitelist
```
Admin tambah/nonaktifkan RFID Master di dashboard
  → Server publish MQTT ke wanara/gate/whitelist/update
  → ESP32 terima pesan
  → GET /gate/master-whitelist
  → Replace file whitelist di LittleFS
```

---

## 7. Checklist Integrasi

### Tim Backend (Flask)
- [ ] Implement `POST /api/gate/scan` dengan logika validasi lengkap
- [ ] Implement `GET /api/gate/master-whitelist`
- [ ] Implement `POST /api/gate/sync-offline`
- [ ] Implement `GET /api/gate/health`
- [ ] Validasi `X-Device-Token` di semua endpoint gate
- [ ] Setup MQTT broker di server
- [ ] Publish ke `wanara/gate/whitelist/update` saat ada perubahan master
- [ ] Publish ke `wanara/gate/{gate_id}/command` untuk manual override
- [ ] Pastikan semua response mengikuti format di dokumen ini

### Tim Firmware (ESP32)
- [ ] Hardcode `device_token` dan `gate_id` per unit sebelum deploy
- [ ] Kirim header `X-Device-Token` di setiap request
- [ ] Implementasi timeout 2500ms — jika tidak respons, jalankan offline logic
- [ ] Simpan whitelist ke LittleFS — replace penuh setiap update
- [ ] Simpan offline logs ke LittleFS — hapus setelah sync berhasil
- [ ] Subscribe MQTT topic `wanara/gate/whitelist/update` dan `wanara/gate/{gate_id}/command`
- [ ] Polling `GET /gate/health` setiap 10 detik saat offline untuk deteksi reconnect
- [ ] Gunakan `scanned_at` waktu device (bukan waktu server)

---

## Catatan Penting untuk Kedua Tim

> **Jangan buka gate** berdasarkan HTTP status code. Selalu baca field `"result"` di body response.

> **QR Guest dan RFID Member SELALU ditolak saat offline** — ini keputusan bisnis yang sudah dikunci, bukan bug.

> **Semua perubahan pada kontrak ini** (field baru, endpoint baru, perubahan format) harus dikomunikasikan ke kedua tim dan versi dokumen di-update sebelum diimplementasikan.

---

*Dokumen ini adalah bagian dari ekosistem digital Hotel Griya Persada Group.*
*Referensi lengkap sistem: SOURCE-OF-TRUTH.md | Detail bisnis: memory-POS.md*
