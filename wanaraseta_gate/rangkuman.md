# Rangkuman MQTT — ESP32 & WT32 untuk Backend Web (Flask)

---

## 1. Koneksi MQTT

| Item | ESP32 | WT32-ETH01 |
|------|-------|------------|
| Koneksi | WiFi (WiFiManager) | Ethernet LAN8720 |
| Username MQTT | `esp32` | `wt32` |
| Password MQTT | `11223344` | `11223344` |
| Client ID | `gate_esp32_01_{MAC}` | `gate_wt32_01_{MAC}` |
| Port default | `1883` | `1883` |

> Client ID di-generate otomatis dari 3 byte terakhir MAC address saat pertama boot.  
> Bisa diubah manual lewat web dashboard masing-masing board.

---

## 2. Topic MQTT

Topic prefix ditentukan oleh **Tipe Device** yang diset di web dashboard board:

| Tipe Device | Publish (kirim scan) | Subscribe (terima hasil) |
|-------------|---------------------|--------------------------|
| `gate` | `gate/{client_id}/scan/in` | `gate/{client_id}/result` |
| `kasir` | `kasir/{client_id}/scan/in` | `kasir/{client_id}/result` |

**Contoh** (client_id = `gate_esp32_01_A1B2C3`):
```
Publish  → gate/gate_esp32_01_A1B2C3/scan/in
Subscribe← gate/gate_esp32_01_A1B2C3/result
```

---

## 3. Payload yang Dikirim Device → Flask

### 3a. Scan RFID
```json
{
  "rfid": "A1B2C3D4",
  "device_id": "gate_esp32_01_A1B2C3"
}
```

### 3b. Scan Barcode / QR
```json
{
  "barcode": "1234567890",
  "device_id": "gate_esp32_01_A1B2C3"
}
```

> **Cara membedakan tipe scan:** dari key JSON-nya — `rfid` untuk kartu RFID, `barcode` untuk QR/barcode scanner.  
> `device_id` selalu sama dengan `client_id` device yang mengirim.

---

## 4. Payload yang Dikirim Flask → Device (Result)

Flask publish ke topic `{device_type}/{client_id}/result` dengan format:

### Akses VALID (buka gerbang)
```json
{
  "status": "valid",
  "message": "Selamat datang, Nama Tamu"
}
```

### Akses INVALID (tolak)
```json
{
  "status": "invalid",
  "message": "Tiket tidak ditemukan / sudah digunakan"
}
```

### Khusus WT32 — Tiket Kids (buka gerbang 2x, jeda 10 detik)
```json
{
  "status": "valid",
  "message": "Tiket Kids",
  "ticket_type": "kids"
}
```

> Jika `ticket_type` = `"kids"`, WT32 akan membuka relay 1 dua kali (langsung + 10 detik kemudian).

---

## 5. Command dari Server ke Device (Opsional)

Flask bisa publish ke `{device_type}/{client_id}/command` untuk perintah langsung:

```json
{ "command": "open_gate" }    // Buka relay 1
{ "command": "open_gate_2" }  // Buka relay 2
{ "command": "reboot" }       // Restart board
```

---

## 6. Konfigurasi EMQX

### Authentication — Users yang harus dibuat:

| Username | Password | Digunakan oleh |
|----------|----------|----------------|
| `esp32` | `11223344` | Semua board ESP32 |
| `wt32` | `11223344` | Semua board WT32-ETH01 |
| `backend_flask` | *(bebas)* | Server Flask |

### Authorization — All Users (gunakan `${clientid}`):

| Action | Permission | Topic |
|--------|-----------|-------|
| Publish | Allow | `gate/${clientid}/scan/in` |
| Subscribe | Allow | `gate/${clientid}/result` |
| Publish | Allow | `kasir/${clientid}/scan/in` |
| Subscribe | Allow | `kasir/${clientid}/result` |

### Authorization — User `backend_flask`:

| Action | Permission | Topic |
|--------|-----------|-------|
| Subscribe | Allow | `gate/+/scan/in` |
| Publish | Allow | `gate/+/result` |
| Subscribe | Allow | `kasir/+/scan/in` |
| Publish | Allow | `kasir/+/result` |

---

## 7. Logika Flask (Ringkasan Alur)

```
1. Flask subscribe → gate/+/scan/in  &  kasir/+/scan/in

2. Terima payload scan:
   - Cek key: ada "rfid" atau "barcode"?
   - Ambil nilai scan & device_id

3. Validasi ke database:
   - Cari tiket/kartu sesuai nilai scan
   - Tentukan valid/invalid

4. Publish result ke topic yang sesuai:
   - Ambil device_type dari topic yang masuk (gate / kasir)
   - Publish ke: {device_type}/{device_id}/result

5. Payload result:
   - valid   → {"status": "valid", "message": "..."}
   - invalid → {"status": "invalid", "message": "..."}
   - kids    → {"status": "valid", "message": "...", "ticket_type": "kids"}
```

---

## 8. Pengaturan di Web Dashboard Board

Akses lewat browser ke IP board, lalu ke card **Pengaturan MQTT Broker**:

| Field | Keterangan |
|-------|-----------|
| IP Server Broker | IP EMQX |
| Port | `1883` |
| Username | `esp32` atau `wt32` |
| Password | `11223344` |
| Client ID | Auto-generate, bisa diubah manual |
| Tipe Device | `Gate` atau `Kasir` — menentukan prefix topic |
| Tipe Scanner | `RFID` atau `QR/Barcode` — info tambahan |

> Topic aktif (publish & subscribe) langsung tampil di bawah form setelah disimpan.
