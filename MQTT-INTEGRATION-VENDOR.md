# Panduan Integrasi MQTT — Wanara Seta Gate System
**Hotel Griya Persada Bandungan**  
**Versi:** 1.0 | **Tanggal:** Juni 2026  
**Untuk:** Vendor ESP32 Smart Gate

---

## Gambaran Sistem

```
ESP32 (Smart Gate)
    │
    │  PUBLISH scan
    ▼
EMQX Broker (192.168.4.50:1883)
    │
    │  Forward ke subscriber
    ▼
Gate Service (Python Backend)
    │
    │  Query & validasi
    ▼
PostgreSQL DB (pos_db)
    │
    │  Return hasil
    ▼
Gate Service
    │
    │  PUBLISH hasil validasi
    ▼
EMQX Broker
    │
    ├──▶ ESP32 (Trigger relay palang)
    └──▶ Kiosk UI (Update layar display)
```

---

## Koneksi MQTT Broker

| Parameter | Nilai |
|-----------|-------|
| **Host** | `192.168.4.50` |
| **Port** | `1883` (MQTT standar) |
| **Port WebSocket** | `8083` (untuk Kiosk UI / browser) |
| **TLS** | Tidak (internal network) |
| **Username** | Diberikan terpisah oleh tim IT |
| **Password** | Diberikan terpisah oleh tim IT |

> Setiap device ESP32 harus menggunakan **Client ID unik** dan credentials yang berbeda. Vendor mengatur credentials melalui dashboard EMQX di `http://192.168.4.50:18083`.

---

## MQTT Topics

### Topic yang ESP32 harus PUBLISH

#### 1. Scan Token
```
Topic : wanara/gate/{gate_id}/scan
QoS   : 1
```

**Payload (JSON):**
```json
{
  "token": "ws-062601AB",
  "ts": 1749820800
}
```

| Field | Tipe | Keterangan |
|-------|------|-----------|
| `token` | string | Token dari scan QR barcode atau UID RFID. Case-sensitive. |
| `ts` | integer | Unix timestamp saat scan (epoch detik). Dipakai untuk offline log. |

**`{gate_id}`** adalah nomor gate yang sudah didaftarkan di sistem. Contoh: gate ID 1 → topic `wanara/gate/1/scan`.

---

#### 2. Offline Log (saat reconnect)
```
Topic : wanara/gate/{gate_id}/offline-log
QoS   : 1
```

Dikirim saat ESP32 baru reconnect setelah sempat offline. Berisi array semua scan yang terjadi selama offline.

**Payload (JSON array):**
```json
[
  { "token": "ws-062601AB", "ts": 1749820800, "result": "granted" },
  { "token": "INVALID123",  "ts": 1749820860, "result": "denied"  }
]
```

| Field | Tipe | Keterangan |
|-------|------|-----------|
| `token` | string | Token yang di-scan |
| `ts` | integer | Unix timestamp saat scan |
| `result` | string | Hasil keputusan lokal ESP32: `"granted"` atau `"denied"` |

Setelah sistem menerima offline log, akan dikirim ACK ke `wanara/gate/{gate_id}/command` dengan `action: "ack_offline_log"`. ESP32 boleh menghapus log lokal setelah menerima ACK ini.

---

#### 3. Heartbeat (opsional)
```
Topic : wanara/gate/{gate_id}/heartbeat
QoS   : 0
```

**Payload (JSON):**
```json
{
  "ts": 1749820800,
  "rssi": -65
}
```

Dikirim secara periodik (disarankan setiap 30–60 detik) untuk monitoring status koneksi gate.

---

### Topic yang ESP32 harus SUBSCRIBE

#### 1. Command dari Backend
```
Topic : wanara/gate/{gate_id}/command
QoS   : 1
```

**Payload granted (buka palang):**
```json
{
  "action": "open"
}
```

**Payload denied (tolak):**
```json
{
  "action": "deny",
  "reason": "expired"
}
```

**Payload ACK offline log:**
```json
{
  "action": "ack_offline_log"
}
```

| `action` | Arti |
|----------|------|
| `open` | Buka palang / izinkan masuk |
| `deny` | Tolak — jangan buka palang |
| `ack_offline_log` | Konfirmasi offline log sudah diterima |

| `reason` (saat deny) | Arti |
|----------------------|------|
| `invalid_token` | Token tidak dikenal di database |
| `not_activated` | Tiket QR belum diaktivasi kasir |
| `already_used` | Tiket QR sudah dipakai |
| `expired` | Tiket sudah kadaluarsa |
| `blocked` | Gelang RFID diblokir |
| `insufficient_balance` | Saldo RFID tidak cukup |
| `server_error` | Error internal sistem |

---

#### 2. Whitelist RFID Master (Sync)
```
Topic  : wanara/gate/whitelist/sync
QoS    : 1
Retain : true
```

**Payload:**
```json
{
  "tokens": ["MASTER001", "MASTER002", "MASTER003"]
}
```

Topic ini menggunakan **retain flag** — ESP32 akan langsung menerima whitelist terbaru saat pertama kali connect, tanpa menunggu publish berikutnya.

Simpan daftar ini di **LittleFS / SPIFFS** ESP32 untuk dipakai saat mode offline. Saat offline, ESP32 membandingkan UID RFID yang di-scan dengan daftar ini:
- UID ada di whitelist → buka palang langsung (tanpa konfirmasi server)
- UID tidak ada → tolak

---

## Jenis Token yang Dikenali Sistem

| Jenis | Format | Sumber |
|-------|--------|--------|
| **QR Guest (tiket)** | `ws-MMYYNNCCC` (contoh: `ws-0626AB01`) | Di-print pada gelang kertas/plastik saat beli tiket di kasir |
| **RFID Member** | UID chip RFID (hex, contoh: `A1B2C3D4`) | Gelang RFID yang sudah didaftarkan ke sistem |
| **RFID Master** | UID chip RFID khusus | Kartu/gelang master yang dipegang petugas |

> **Catatan penting:** Sistem membaca field `token` dari payload JSON — bukan raw MQTT payload. Pastikan ESP32 mengirim dalam format JSON, bukan plain string.

---

## Logika Validasi Backend (Urutan Prioritas)

```
Token diterima
    │
    ├─ Cek RFID Master → jika ada & aktif → OPEN + catat log + notifikasi Telegram
    │
    ├─ Cek QR Guest
    │      ├─ Belum aktivasi → DENY (not_activated)
    │      ├─ Sudah dipakai → DENY (already_used)
    │      ├─ Kadaluarsa → DENY (expired)
    │      └─ Valid → OPEN + update scan_count + jika scan terakhir tandai "used"
    │
    ├─ Cek RFID Member
    │      ├─ Diblokir → DENY (blocked)
    │      ├─ Saldo < harga masuk → DENY (insufficient_balance)
    │      └─ Valid → OPEN + debit saldo
    │
    └─ Tidak ditemukan → DENY (invalid_token)
```

---

## Kiosk UI (Layar Display)

Kiosk UI menerima hasil validasi via **MQTT over WebSocket** (port 8083 EMQX) dengan subscribe ke topic:

```
wanara/gate/{gate_id}/command
```

Payload sama dengan yang diterima ESP32. Kiosk UI membaca field `action` dan `reason` untuk menampilkan informasi ke layar.

> **Catatan:** Kiosk UI adalah komponen yang sedang dikembangkan oleh tim IT hotel. Vendor tidak perlu mengimplementasikan ini.

---

## Skenario Offline ESP32

Saat koneksi MQTT terputus:
1. ESP32 tetap bisa baca RFID Master dari whitelist lokal (LittleFS)
2. Scan QR Guest dan RFID Member: ESP32 memutuskan sendiri berdasarkan logika lokal (atau tolak semua — sesuai konfigurasi ESP32)
3. Semua scan selama offline disimpan di LittleFS sebagai log
4. Saat reconnect: kirim offline log ke `wanara/gate/{gate_id}/offline-log`
5. Tunggu ACK dari backend, lalu hapus log lokal

---

## Informasi Gate ID

Gate ID didaftarkan di sistem oleh tim IT hotel. Vendor akan diberitahu Gate ID yang harus digunakan untuk setiap perangkat ESP32 sebelum deployment.

Saat ini Gate ID yang tersedia:

| Gate ID | Lokasi |
|---------|--------|
| `1` | (dikonfirmasi saat deployment) |

---

## Pertanyaan Teknis

Hubungi tim IT Hotel Griya Persada:
- **Email:** it.bandungan@griyapersadahotel.com

---

*Dokumen ini dapat berubah. Versi terbaru selalu dikomunikasikan melalui email sebelum deployment.*
