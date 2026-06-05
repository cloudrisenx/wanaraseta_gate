# SOURCE OF TRUTH
## Hotel Digital Ecosystem — Griya Persada Group
**Versi:** 1.0 | **Status:** Active | **Terakhir diupdate:** Mei 2026

> Dokumen ini adalah referensi tunggal yang mengikat seluruh keputusan arsitektur, teknis, dan bisnis dari ekosistem digital Hotel Griya Persada Group. Setiap keputusan yang ada di sini adalah hasil diskusi dan telah dikonfirmasi. Tidak ada yang boleh diubah tanpa diskusi ulang.

---

## DAFTAR ISI

1. [Visi & Prinsip](#1-visi--prinsip)
2. [Tech Stack](#2-tech-stack)
3. [Infrastruktur & Server](#3-infrastruktur--server)
4. [Struktur Repository](#4-struktur-repository)
5. [Sistem Autentikasi & Permission](#5-sistem-autentikasi--permission)
6. [Database Architecture](#6-database-architecture)
7. [Modul HRD](#7-modul-hrd)
8. [Modul Sales CRM](#8-modul-sales-crm)
9. [Modul POS](#9-modul-pos)
10. [Keputusan yang Dikunci](#10-keputusan-yang-dikunci)
11. [Status Implementasi](#11-status-implementasi)

---

## 1. Visi & Prinsip

### Visi
Membangun platform digital terpadu yang mengelola seluruh operasional hotel — SDM, penjualan, dan point-of-sale — dalam satu ekosistem terintegrasi dengan satu sistem autentikasi terpusat.

### Prinsip Arsitektur
| Prinsip | Penjelasan |
|---------|-----------|
| **Single Sign-On** | Satu akun untuk semua modul, hak akses dikontrol via permission |
| **Separation of Concerns** | Setiap service punya database dan codebase sendiri |
| **Monorepo** | Satu repository, folder terpisah per service |
| **Shared Library** | Kode auth, utilitas, config dipakai bersama via symlink/PYTHONPATH |
| **Self-hosted** | Semua infrastruktur di Proxmox, tidak ada cloud pihak ketiga untuk data inti |
| **Scalable by design** | Menambah modul baru tidak memerlukan perombakan arsitektur |
| **Single Entry** | Sales input satu kali, data masuk ke semua tempat yang relevan |

---

## 2. Tech Stack

### Per Layer
| Layer | Teknologi | Versi |
|-------|-----------|-------|
| Frontend | Next.js (App Router) + TypeScript + Tailwind CSS | 15.x |
| Backend | Python Flask — REST API | 3.0.x |
| Database | PostgreSQL — self-hosted | 16.14 |
| ORM | Prisma | 0.13.x |
| Auth | JWT (PyJWT) — httpOnly cookie | - |
| Password | bcrypt | 4.x |
| Reverse Proxy | Nginx | 1.24.0 |
| Process Manager | systemctl (Linux service) | - |
| PDF Generator | WeasyPrint (Python) | 62.x |
| Background Job | APScheduler (dalam proses Flask) | 3.10.x |
| Validasi | Marshmallow | 3.21.x |

### Yang TIDAK Digunakan
- ❌ Docker — untuk ekosistem baru (marketing tetap pakai Docker)
- ❌ Supabase / cloud database
- ❌ Celery / Redis — tidak diperlukan di fase MVP
- ❌ FastAPI — dipilih Flask karena lebih simpel untuk tim

### Yang Digunakan TERBATAS
- ⚠️ **n8n** — HANYA untuk routing notifikasi Telegram pada event gate Wanara Seta. gate-service POST ke webhook n8n setelah proses MQTT scan, n8n forward ke Telegram bot. Tidak digunakan di modul lain.
- ⚠️ **EMQX MQTT Broker** — HANYA untuk komunikasi antara gate-service dan ESP32 hardware Wanara Seta. Service lain (hrd-service, sales-service, pos-service) tidak menggunakan MQTT.
- ⚠️ **paho-mqtt** — library Python untuk gate-service subscribe/publish ke EMQX. Ditambahkan ke `requirements.txt` gate-service saja, bukan ke service lain.

### Dependencies Flask (requirements.txt standar)
```
flask==3.0.3
flask-cors==4.0.1
prisma==0.13.1
PyJWT==2.8.0
bcrypt==4.1.3
marshmallow==3.21.3
email-validator==2.2.0
python-dotenv==1.0.1
httpx==0.27.0
weasyprint==62.3
apscheduler==3.10.4
pytest==8.2.2
pytest-flask==1.3.0
```

---

## 3. Infrastruktur & Server

### Server Utama
| Property | Value |
|----------|-------|
| IP | 192.168.4.50 |
| OS | Ubuntu 24.04.4 LTS |
| RAM | 5.8 GB |
| Disk | 97 GB (70 GB tersisa) |
| CPU | 2 cores |
| Hypervisor | Proxmox VM |

### Port Mapping
| Service | Port | Status |
|---------|------|--------|
| marketing_db (Docker PostgreSQL) | 5432 | Existing — jangan disentuh |
| marketing_backend (Docker FastAPI) | 8000 | Existing — jangan disentuh |
| marketing_frontend (Docker Next.js) | 3000 | Existing — jangan disentuh |
| **PostgreSQL ekosistem baru** | **5433** | ✅ Running |
| **hrd-service (Flask)** | **5001** | Belum dijalankan |
| **sales-service (Flask)** | **5002** | Belum dijalankan |
| **pos-service (Flask)** | **5003** | Belum dijalankan |
| **gate-service (Flask)** | **5004** | Belum dijalankan |
| **hrd-frontend (Next.js)** | **3001** | Belum dijalankan |
| **sales-frontend (Next.js)** | **3002** | Belum dijalankan |
| **pos-frontend (Next.js)** | **3003** | Belum dijalankan |
| Nginx (reverse proxy) | 80 / 443 | Terinstall, belum dikonfigurasi aktif |

### Database Server
| Database | Owner | Port | Status |
|----------|-------|------|--------|
| hrd_db | ecosystem_user | 5433 | ✅ Ready |
| sales_db | ecosystem_user | 5433 | ✅ Ready |
| pos_db | ecosystem_user | 5433 | ✅ Ready |

### Keamanan
- UFW aktif — hanya port yang diperlukan terbuka
- fail2ban aktif — proteksi brute force SSH
- Port database (5432, 5433) **tidak dibuka** ke luar — akses internal only
- Akses eksternal masa depan: Cloudflare Tunnel / Zero Trust

### Path Penting di Server
```
/home/griyapersada/projects/
├── marketing/dashboard-marketing/   ← existing, JANGAN DISENTUH
└── hotel-ecosystem/                 ← project baru
    ├── shared/                      ← shared Python library
    ├── services/
    │   ├── hrd-service/
    │   ├── sales-service/
    │   ├── pos-service/
    │   └── gate-service/
    ├── apps/
    │   ├── hrd-frontend/
    │   ├── sales-frontend/
    │   └── pos-frontend/
    ├── infra/
    │   ├── nginx/
    │   ├── postgres/
    │   └── systemd/
    ├── docs/
    ├── .env                         ← JANGAN di-commit
    ├── .gitignore
    └── README.md
```

---

## 4. Struktur Repository

### Standar Internal Setiap Flask Service
```
{nama}-service/
├── app.py                  ← Entry point, register Blueprint
├── config.py               ← Extend shared/config/base_config.py
├── requirements.txt
├── .env                    ← TIDAK di-commit
├── .env.example            ← WAJIB ada di repo
├── venv/                   ← Virtual environment, tidak di-commit
├── modules/
│   └── {nama_modul}/
│       ├── __init__.py
│       ├── routes.py       ← Routing & validasi input ONLY
│       ├── service.py      ← SEMUA business logic di sini
│       ├── schema.py       ← Marshmallow validation
│       └── exceptions.py  ← Custom errors (opsional)
├── db/
│   └── client.py           ← Prisma client singleton
├── prisma/
│   └── schema.prisma       ← Schema database service ini
└── tests/
    ├── conftest.py
    └── {nama_modul}/
        └── test_{nama}.py
```

### Aturan Wajib (TIDAK BOLEH DILANGGAR)
1. `routes.py` — hanya routing, validasi input, panggil service, return response. **Tidak ada business logic.**
2. `service.py` — semua business logic. **Tidak boleh import Flask `request` object langsung.**
3. `shared/` — diakses via PYTHONPATH. **Tidak pernah dicopy ke dalam folder service.**
4. `.env` — **tidak pernah masuk Git.** `.env.example` wajib selalu ada.

### Shared Library
```
shared/
├── __init__.py
├── auth/
│   ├── __init__.py
│   └── jwt_guard.py        ← @require_auth, @require_permission
├── config/
│   ├── __init__.py
│   └── base_config.py      ← BaseConfig class
└── utils/
    ├── __init__.py
    ├── response.py         ← success_response(), error_response(), paginated_response()
    ├── pagination.py       ← get_pagination_params()
    └── date_helper.py      ← now_jakarta(), format_date(), days_between()
```

---

## 5. Sistem Autentikasi & Permission

### Arsitektur
- **Single Sign-On** — satu login di HRD berlaku untuk semua modul
- **HRD Auth Service** — satu-satunya yang menerbitkan JWT
- **JWT payload** membawa semua permission — service lain hanya verifikasi signature
- **Shared secret** — semua service pakai JWT_SECRET yang sama dari `.env`
- **Tidak ada tabel users** di `sales_db` atau `pos_db`

### Struktur JWT Payload
```json
{
  "sub": "42",
  "name": "Budi Santoso",
  "department": "Sales",
  "position_level": 5,
  "permissions": [
    "sales:report:write",
    "sales:plan:write",
    "hrd:profile:read"
  ],
  "exp": 1748000000
}
```

### Format Permission
Pattern: `{modul}:{resource}:{aksi}`

| Permission | Akses |
|-----------|-------|
| `*:*` | Full access (Admin IT) |
| `hrd:employee:write` | CRUD data karyawan |
| `hrd:attendance:monitor` | Lihat kehadiran hari ini (Manager+) |
| `hrd:leave:approve` | Approve pengajuan cuti |
| `sales:report:write` | Input laporan harian |
| `sales:target:write` | Set target (Manager+) |
| `pos:wanara-seta:cashier` | Kasir outlet Wanara Seta |
| `pos:wanara-seta:supervisor` | Supervisor outlet Wanara Seta |

### Tabel Permission (di hrd_db)
```
modules          → id, slug (UNIQUE), name, description, is_active
permissions      → id, module_id (FK), key (UNIQUE), name, description
user_permissions → id, user_id (FK), permission_id (FK), granted_by, granted_at, revoked_at
```

### Logika Approval Cuti
| Level Jabatan | Jabatan | Rute Approval |
|--------------|---------|--------------|
| Level 1 | General Manager | **Eskalasi manual** → HRD notifikasi, Direktur/Owner handle di luar sistem |
| Level 2 | Vice General Manager | **Eskalasi manual** → HRD notifikasi, Direktur/Owner handle di luar sistem |
| Level 3 | Manager | **Executive** → GM atau VGM approve → HRD approve final |
| Level 4 | Asst. Manager | **Executive** → GM atau VGM approve → HRD approve final |
| Level 5+ | Staff, Supervisor, dll | **Standard** → Manager dept → HRD approve final |

---

## 6. Database Architecture

### Strategi
- **Satu instance PostgreSQL** (port 5433) dengan tiga database terpisah
- **Tidak ada foreign key antar database** — isolasi penuh
- **Cross-service link** hanya via `employee_id` (integer dari JWT)
- **Validasi employee_id** dilakukan di Flask service layer, bukan di DB

### Skema Relasi Antar Service
```
HRD (hrd_db)          Sales (sales_db)        POS (pos_db)
──────────────         ──────────────           ──────────────
Employee.id     →      activities.sales_id      transactions.cashier_id
(employee_id)   →      leads.assigned_to        (emp_id dari JWT)
                →      tasks.assigned_to
                →      sales_targets.sales_id

Tidak ada FK fisik — hanya logical reference via employee_id
```

---

## 7. Modul HRD

### Status
Ditulis ulang dari awal dengan arsitektur baru (Flask backend). Logika bisnis mengacu ke codebase lama di laptop.

### Tech Stack HRD
- Frontend: Next.js 15, basePath: `/hrd`, port 3001
- Backend: Flask, port 5001
- Database: `hrd_db` port 5433

### Tabel Existing (dari codebase lama)
`Department`, `Position`, `Shift`, `Employee`, `Child`, `EmergencyContact`, `Attendance`, `EmployeeShift`, `JadwalHarian`, `IzinTerlambat`, `Holiday`, `LeaveBalance`, `LeaveRequest`, `ContractHistory`, `CareerHistory`, `User`

### Update & Penyempurnaan Fitur

| # | Fitur | Type | Status | Catatan Teknis |
|---|-------|------|--------|---------------|
| 1 | Rekap periode + kolom cuti & EO | DB + UI | MVP | JOIN LeaveBalance ke query rekap — tidak ada perubahan skema |
| 2 | Filter tanggal cuti H+1 | Logika + UI | MVP | Frontend minDate = today+1, backend validasi startDate > today |
| 3 | Upload SP & warning advice | DB + Fitur baru | MVP | Tabel baru: `EmployeeDocument` |
| 4 | Approval cuti executive (Manager → GM/VGM) | Logika + DB | MVP | 4 kolom baru di `LeaveRequest` |
| 5 | Halaman monitor kehadiran hari ini | UI baru | MVP | Query Attendance WHERE today, sidebar baru, permission baru |
| 6 | Dokumen & surat otomatis (SK, kontrak) | Fitur baru | V2 | Tabel `DocumentTemplate` + WeasyPrint PDF |
| 7 | Performance management / KPI | Fitur baru | V2 | Tabel `PerformanceReview` |

### Tabel Baru yang Ditambahkan ke hrd_db

**EmployeeDocument**
```
id, employeeId (FK→Employee)
docType (SP1 | SP2 | SP3 | WARNING | OTHER)
title, filePath, fileSize
issuedDate, issuedBy (userId)
notes, createdAt
```

**DocumentTemplate**
```
id, name
docType (enum)
templateHtml (text), variables (json)
isActive, createdBy (userId), createdAt
```

**PerformanceReview**
```
id, employeeId (FK→Employee), reviewerId (userId)
period (bulan), year, periodType (MONTHLY | ANNUAL)
scores (json), totalScore, grade
strengths, improvements, notes
status (DRAFT | SUBMITTED | ACKNOWLEDGED)
createdAt, updatedAt
```

**Perubahan LeaveRequest**
```
+ approvalRoute        (STANDARD | EXECUTIVE | MANUAL_ESCALATION)
+ executiveApproverId  (nullable FK→User)
+ executiveApprovedAt  (nullable DateTime)
+ executiveNotes       (nullable String)
```

---

## 8. Modul Sales CRM

### Status
Service baru — ditulis dari awal.

### Tech Stack Sales
- Frontend: Next.js 15, basePath: `/sales`, port 3002
- Backend: Flask, port 5002
- Database: `sales_db` port 5433

### Paradigma
**CRM aktif** — sistem yang mendorong sales untuk follow-up, bukan hanya mencatat.

### Segmen Customer
`Government`, `BUMN`, `Corporate`, `School`, `University`, `TravelAgent`, `EventOrganizer`, `Community`, `Association`, `ReligiousOrg`, `WeddingOrganizer`

### Tipe Aktivitas
`Telemarketing`, `WhatsApp/Digital`, `SalesCall`, `SalesVisit`, `Email`, `Presentation`, `SiteInspection`, `Negotiation`

### Semua Tabel sales_db

| Tabel | Fungsi |
|-------|--------|
| `customers` | Master data pelanggan — satu owner aktif |
| `contact_persons` | PIC per customer — bisa banyak |
| `leads` | Prospek belum dikualifikasi — source, stage, score |
| `opportunities` | Lead dikualifikasi — revenue breakdown per kategori |
| `activities` | Log aktivitas harian sales |
| `tasks` | Task & follow-up dengan priority dan due date |
| `events` | Pipeline event & MICE |
| `proposals` | Penawaran ke customer |
| `sales_targets` | Target per sales per bulan |
| `weekly_plans` | Rencana mingguan sales |
| `ownership_histories` | Histori transfer kepemilikan customer |
| `follow_up_reminders` | Antrian reminder otomatis |
| `customer_event_patterns` | Pola historis event customer per bulan |
| `segment_lead_times` | Konfigurasi lead time per segmen |
| `loss_records` | Dokumentasi wajib untuk lead yang gagal |
| `notification_queue` | Buffer pengiriman notifikasi |
| `single_entry_config` | Aturan kapan aktivitas otomatis buat lead |
| `vhp_imports` | Data historis VHP — diimport sekali |
| `activity_logs` | Audit trail perubahan data |

### Logika Single Entry
```
Sales input form aktivitas
        ↓
Flask evaluasi single_entry_config
        ↓
Jika memenuhi threshold:
  → Simpan ke activities
  → Auto-create lead di leads
  → Auto-create follow_up_reminders
```

### Empat Trigger Notifikasi

| # | Trigger | Mekanisme | Status |
|---|---------|-----------|--------|
| 1 | Manual follow-up date | Sales isi follow_up_date → reminder H-1 & H-0 | MVP |
| 2 | Pola historis tahunan | Event Sept tahun lalu → reminder Juli tahun ini | V2 |
| 3 | Lead time per segmen | Hitung dari estimated_event_month × konfigurasi segmen | V2 |
| 4 | Dormant customer alert | last_contact_at > threshold → flag + alert | MVP |

### Lead Time Default per Segmen
| Segmen | Lead Time | Reminder Pertama |
|--------|-----------|-----------------|
| Government | 1 bulan | 1 bulan sebelum |
| Corporate Annual | 12 bulan | 12 bulan sebelum |
| Wedding | 6 bulan | 6 bulan sebelum |
| MICE | 3 bulan | 3 bulan sebelum |
| Minimum contact | - | Setiap 6 bulan |

### Aturan Bisnis Wajib
1. Satu customer hanya boleh punya **satu owner aktif** — transfer wajib diapprove Manager
2. Lead **tidak bisa di-close sebagai Lost** tanpa mengisi `loss_records` (alasan + evidence)
3. Setiap customer wajib dikontak **minimal 1x per 6 bulan**
4. Tidak ada tabel users di `sales_db` — semua via `employee_id` dari JWT

### Roadmap

| Fitur | Status |
|-------|--------|
| Customer Database CRUD + ownership | MVP |
| Lead Management (stages, pipeline) | MVP |
| Daily Sales Report + single entry | MVP |
| Manual follow-up reminder | MVP |
| Dormant customer alert | MVP |
| Loss record wajib alasan + evidence | MVP |
| Dashboard KPI dasar C-Level | MVP |
| Sales Funnel visual (Kanban) | V2 |
| Event & MICE Pipeline | V2 |
| Ownership transfer approval flow | V2 |
| Pola historis tahunan (Trigger 2) | V2 |
| Lead time per segmen (Trigger 3) | V2 |
| Excel import/export | V2 |
| Confirmation Letter generator (PDF) | V2 |
| Revenue forecast per kuartal | V2 |
| WhatsApp notification integration | V3 |
| AI lead scoring otomatis | V3 |
| AI cari lead baru (LinkedIn) | V3 |
| BEO generator | V3 |

---

## 9. Modul POS

### Status
Service baru — ditulis dari awal.

### Tech Stack POS

| Komponen | Detail |
|----------|--------|
| Frontend | Next.js 15, basePath: `/pos`, port 3003 — mengelola UI kasir, dashboard admin, manajemen member |
| pos-service | Flask, port 5003 — transaksi outlet, topup, laporan, manajemen tiket & member |
| gate-service | Flask, port 5004 — **KHUSUS** endpoint yang dipanggil ESP32: scan gate, whitelist master, sync offline log |
| Database | `pos_db` port 5433 — diakses oleh kedua service |

### Pemisahan gate-service vs pos-service

| Service | Caller | Tanggung Jawab |
|---------|--------|---------------|
| `gate-service` (5004) | EMQX MQTT broker | Subscribe topic scan ESP32, validasi token, publish command balik ke ESP32 via EMQX, sync offline log, trigger notifikasi n8n |
| `pos-service` (5003) | Web frontend (kasir/admin) | Topup saldo, transaksi outlet, laporan, manajemen member, manajemen tiket, publish whitelist update via EMQX |

**Mengapa dipisah:** gate-service berkomunikasi via MQTT (bukan HTTP), sehingga perlu process terpisah yang terus-menerus subscribe ke EMQX broker. pos-service adalah HTTP Flask biasa untuk web frontend. Menggabungkan keduanya akan memperumit lifecycle management.

### Arsitektur Komunikasi: MQTT via EMQX

ESP32 **tidak pernah** memanggil Flask secara HTTP. Semua komunikasi hardware ↔ server melalui **EMQX MQTT broker**.

```
ESP32 (hardware)
  │  PUBLISH wanara/gate/{id}/scan
  ▼
EMQX Broker (192.168.4.50:1883)
  │  forward ke subscriber
  ▼
gate-service (Flask + paho-mqtt thread)
  │  query pos_db → validasi token
  │  PUBLISH wanara/gate/{id}/command
  ▼
EMQX Broker
  │  forward ke subscriber
  ▼
ESP32 (subscribe topic command) → buka/tutup relay gate
```

**Autentikasi device:** Dihandle oleh EMQX pada level koneksi MQTT. Setiap ESP32 didaftarkan di EMQX dengan `clientId` + `username` + `password` unik. Jika kredensial salah → EMQX tolak koneksi, ESP32 tidak bisa publish/subscribe apapun. **Tidak ada device_token di HTTP header** — konsep ini dihapus.

### Tabel MQTT Topic

| Topic | Publisher | Subscriber | Payload | Keterangan |
|-------|-----------|------------|---------|------------|
| `wanara/gate/{gate_id}/scan` | ESP32 | gate-service | `{token, ts}` | Dipicu setiap kali ada scan di gate |
| `wanara/gate/{gate_id}/command` | gate-service | ESP32 | `{action, reason?}` | `action`: `open` atau `deny` |
| `wanara/gate/{gate_id}/heartbeat` | ESP32 | gate-service | `{ts, rssi}` | Status koneksi gate, dikirim periodik |
| `wanara/gate/{gate_id}/offline-log` | ESP32 | gate-service | `[{token, ts, result}]` | Dikirim saat reconnect setelah offline |
| `wanara/gate/whitelist/sync` | gate-service | Semua ESP32 | `{tokens:[...]}` | Dikirim saat admin ubah RFID Master |

### Arsitektur Multi-Outlet
- Satu `pos-service` mengelola **semua outlet**
- Outlet baru = tambah record di tabel `outlets` (oleh Admin IT)
- Permission baru = tambah key `pos:{slug}:{role}` di `hrd_db`
- **Slug outlet** adalah jembatan antara `pos_db` dan sistem permission

### Semua Tabel pos_db

| Tabel | Fungsi |
|-------|--------|
| `outlets` | Master outlet — slug unik per outlet |
| `categories` | Kategori produk per outlet |
| `products` | Produk/menu per outlet |
| `transactions` | Transaksi kasir (retail/FnB) |
| `transaction_items` | Item per transaksi |
| `stock_movements` | Mutasi stok produk |
| `ticket_batches` | Batch generate QR Guest — tracking stok opname Accounting |
| `tickets` | Tiket QR Guest & RFID Member Wanara Seta |
| `rfid_masters` | Token RFID bypass gate (operator/security) |
| `members` | Profil member pemegang gelang RFID |
| `gates` | Master titik gate Wanara Seta |
| `gate_logs` | Log semua scan gate (granted/denied) |
| `wanara_transactions` | Transaksi e-wallet RFID Member di outlet |
| `admin_notifications` | Notifikasi real-time untuk admin |

### Keputusan Desain Penting
- `cashier_name` disimpan sebagai **snapshot** — laporan historis tetap akurat meski data karyawan berubah
- `price_at_sale` disimpan sebagai **snapshot** — harga transaksi tidak berubah meski harga produk diedit
- `pos_db` **sepenuhnya mandiri** — tidak terhubung ke Sales Dashboard
- **Tidak ada tabel users di pos_db** — identitas kasir/operator via `employee_id` dari JWT

### Outlet Aktif
| Outlet | Slug | Tipe | Status |
|--------|------|------|--------|
| Wanara Seta (Gift Shop) | `wanara-seta` | retail | MVP |

---

### Sub-sistem: Wanara Seta Gate Access & RFID E-Wallet

Wanara Seta adalah wahana air yang membutuhkan sistem akses gate modern dan ekosistem pembayaran digital internal. Sistem menggantikan tiket kertas dan transaksi tunai dengan mekanisme akses hybrid: QR harian untuk guest dan RFID gelang untuk member.

#### Tipe Akses (3 jenis)

**1. QR Guest (Gelang Harian)**
- **Media:** Gelang kertas/tyvek dengan QR code — dicetak massal via dashboard (format token: `ws-mmyyXXNN`)
- **Generate:** Admin generate N token → sistem overlay QR ke template desain marketing → export PDF A3 → cetak → stok di Accounting
- **Aktivasi:** Kasir scan QR gelang dari stok → isi `valid_until` di form → status `unactivated` → `active`
- **valid_until:** Default = hari ini, bisa diisi tanggal lebih lanjut (fleksibel per kebijakan operasional)
- **Gate:** Sekali pakai — setelah berhasil membuka gate → status langsung `used`
- **Berlaku:** Hanya sampai `valid_until` — melewati tanggal ini, APScheduler auto-expire tengah malam
- **Tidak jadi datang:** HANGUS — tidak ada reschedule, tidak ada refund
- **Transaksi outlet:** ❌ Tidak bisa — tidak punya saldo
- **Identitas:** Anonim

**2. RFID Member (Permanen)**
- **Media:** Gelang RFID — diterbitkan saat pendaftaran member
- **Gate:** Biaya masuk dipotong dari saldo gelang otomatis
- **Saldo:** Tidak hangus — tersimpan permanen hingga digunakan
- **Transaksi outlet:** ✅ Tap gelang di semua outlet dalam wahana
- **Topup:** Bisa di pos utama **maupun di semua outlet** kapan saja
- **Identitas:** Ada profil (nama, HP, email)
- **Gelang hilang:** Admin blokir dari dashboard → saldo pindah ke gelang baru

**3. RFID Master (Operator/Security)**
- **Media:** Kartu atau gelang RFID khusus — dipegang staff terpercaya
- **Fungsi:** Buka gate kapan saja, unlimited, bypass semua validasi
- **Pemegang:** Security, operator gate, teknisi, manajer operasional
- **Offline:** ✅ Tetap bisa buka gate meski EMQX tidak terkoneksi (whitelist lokal LittleFS ESP32)
- **Log:** Setiap penggunaan dicatat di `gate_logs` — saat online via MQTT, saat offline di-sync saat reconnect
- **Notifikasi:** gate-service terima log via MQTT → POST webhook n8n → Telegram ke admin

#### Prioritas Scan Gate
```
1. RFID Master  →  2. QR Guest  →  3. RFID Member
```

#### Logika Validasi Gate (dijalankan oleh gate-service setelah terima MQTT scan)

```
ESP32 PUBLISH wanara/gate/{gate_id}/scan
  payload: { token, ts }
  │
  ▼
gate-service (subscriber) menerima pesan
  │
  ├─ Ada di rfid_masters & active?
  │    → UPDATE last_used_at = NOW()
  │    → INSERT gate_logs (is_master_used: true)
  │    → INSERT admin_notifications
  │    → POST webhook n8n (notif Telegram)
  │    → PUBLISH wanara/gate/{gate_id}/command { action: "open" }
  │
  ├─ type = qr_guest?
  │    → unactivated?                  PUBLISH command { action:"deny", reason:"not_activated" }
  │    → used?                         PUBLISH command { action:"deny", reason:"already_used" }
  │    → expired?                      PUBLISH command { action:"deny", reason:"expired" }
  │    → CURRENT_DATE > valid_until?   PUBLISH command { action:"deny", reason:"expired" }
  │    → GRANTED
  │         UPDATE status='used', used_at=NOW()
  │         INSERT gate_logs
  │         PUBLISH command { action:"open" }
  │
  └─ type = rfid_member?
       → blocked?             PUBLISH command { action:"deny", reason:"blocked" }
       → saldo < harga?       PUBLISH command { action:"deny", reason:"insufficient_balance" }
       → GRANTED (dalam 1 DB transaction atomik)
            UPDATE saldo = saldo - harga_masuk
            INSERT wanara_transactions
            INSERT gate_logs
            PUBLISH command { action:"open" }

ESP32 (subscriber wanara/gate/{gate_id}/command) terima → buka/tutup relay gate
```

#### Alur Sistem

**QR Guest**

*Generate & Cetak (Admin — satu kali per batch):*
1. Admin input jumlah token + upload template desain gelang dari marketing + set `valid_until`
2. Sistem generate N token format `ws-mmyyXXNN` → INSERT ke `tickets` status `unactivated` + INSERT `ticket_batches`
3. Sistem overlay QR ke template → susun di A3 landscape → export PDF siap cetak
4. Marketing cetak PDF → gelang fisik diserahkan ke Accounting (stok opname masuk)

*Aktivasi (Accounting/kasir — saat gelang dikeluarkan ke customer):*
5. Kasir scan token QR gelang di dashboard
6. Muncul form aktivasi — kasir **isi `valid_until`** (default: hari ini, bisa diubah ke tanggal lebih lanjut)
7. pos-service UPDATE: status `active`, simpan `valid_until`, `activated_at`, `activated_by`
8. Gelang diserahkan ke customer

*Pakai di gate:*
9. Customer tap gelang di gate → ESP32 baca token
10. ESP32 PUBLISH `wanara/gate/{id}/scan { token, ts }` → EMQX → gate-service
11. gate-service cek: `active`? `CURRENT_DATE <= valid_until`? `used_at IS NULL`?
12. Jika valid → UPDATE `used`, INSERT `gate_logs` → PUBLISH command `{ action:"open" }`
13. ESP32 terima command → buka relay gate

**RFID Member**
1. Daftar member → admin issued gelang RFID di dashboard
2. Topup saldo di pos utama atau outlet manapun (via pos-service, alur web/HTTP biasa)
3. Tap gelang di gate → ESP32 PUBLISH scan → gate-service cek saldo di pos_db
4. Jika saldo cukup → debit atomik (UPDATE saldo + INSERT wanara_transactions + INSERT gate_logs) → PUBLISH command open
5. Jika saldo kurang → PUBLISH command deny reason:insufficient_balance
6. Di dalam wahana: tap gelang di outlet → alur terpisah via pos-service (HTTP, bukan MQTT)
7. Sisa saldo tetap tersimpan untuk kunjungan berikutnya

**RFID Master**
1. Staff tap kartu/gelang master di gate
2. ESP32 cek whitelist lokal (LittleFS) — **tidak perlu tunggu EMQX/server**
3. Token ada di whitelist → ESP32 buka gate langsung (relay aktif)
4. Jika EMQX online: ESP32 PUBLISH scan → gate-service INSERT gate_logs + INSERT admin_notifications + POST webhook n8n → Telegram
5. Jika EMQX offline: log disimpan di memori ESP32, dikirim via topic `offline-log` saat reconnect

#### Offline Mode — RFID Master

| Kondisi | QR Guest | RFID Member | RFID Master |
|---------|----------|-------------|-------------|
| EMQX online | Validasi via MQTT normal | Validasi via MQTT normal | Whitelist lokal → open + log via MQTT + notif Telegram |
| EMQX offline | DITOLAK (fail-secure, tidak bisa publish scan) | DITOLAK (fail-secure) | Whitelist lokal → open, log disimpan di memori ESP32 |
| EMQX kembali online | — | — | ESP32 PUBLISH `offline-log` → gate-service sync ke DB + notif Telegram |

**Mekanisme offline:**
- Token master disimpan di **LittleFS** (flash storage ESP32)
- ESP32 deteksi EMQX offline via MQTT connection lost callback
- Saat offline: QR Guest & RFID Member langsung ditolak (fail-secure — tidak ada validasi lokal)
- RFID Master: cek whitelist LittleFS → buka gate → simpan log di memori ESP32
- Saat reconnect ke EMQX: ESP32 PUBLISH `wanara/gate/{id}/offline-log [{token, ts, result}]`
- gate-service INSERT ke `gate_logs` dengan `offline_mode=true` → POST webhook n8n → Telegram ke admin
- ESP32 terima ACK dari gate-service (via MQTT command topic) → hapus offline logs dari memori
- Update whitelist: setiap ESP32 reconnect → gate-service PUBLISH `wanara/gate/whitelist/sync {tokens:[...]}` → ESP32 update LittleFS

#### Schema Tabel Wanara Seta (PostgreSQL)

```sql
-- Batch generate tiket QR Guest (tracking stok opname)
CREATE TABLE ticket_batches (
  id            SERIAL PRIMARY KEY,
  batch_code    VARCHAR(30) UNIQUE NOT NULL,  -- misal BATCH-0626-001
  quantity      INTEGER NOT NULL,
  valid_until   DATE NOT NULL,                -- default end date untuk batch ini (bisa dioverride saat aktivasi)
  template_path VARCHAR(500),                 -- path file template desain gelang dari marketing
  pdf_path      VARCHAR(500),                 -- path file PDF hasil generate
  generated_by  INTEGER NOT NULL,             -- employee_id admin
  status        VARCHAR(20) DEFAULT 'generated' CHECK (status IN ('generated','printed','distributed','closed')),
  created_at    TIMESTAMP DEFAULT NOW()
);

-- Tiket QR Guest & RFID Member
CREATE TABLE tickets (
  id             BIGSERIAL PRIMARY KEY,
  type           VARCHAR(20) NOT NULL CHECK (type IN ('qr_guest','rfid_member')),
  token          VARCHAR(255) UNIQUE NOT NULL,

  -- QR Guest fields
  batch_id       INTEGER,               -- FK → ticket_batches
  status         VARCHAR(20) DEFAULT 'unactivated' CHECK (status IN ('unactivated','active','used','expired')),
  valid_until    DATE,                  -- diisi saat aktivasi oleh kasir/admin (boleh > hari ini)
  activated_at   TIMESTAMP,            -- kapan diaktivasi
  activated_by   INTEGER,               -- employee_id dari JWT (kasir/admin yang aktivasi)
  used_at        TIMESTAMP,
  used_gate_id   INTEGER,               -- FK → gates

  -- RFID Member fields
  member_id      INTEGER,               -- FK → members
  saldo          DECIMAL(10,2) DEFAULT 0,
  rfid_status    VARCHAR(10) DEFAULT 'active' CHECK (rfid_status IN ('active','blocked')),
  issued_at      TIMESTAMP
);

-- Token RFID bypass gate
CREATE TABLE rfid_masters (
  id             SERIAL PRIMARY KEY,
  token          VARCHAR(255) UNIQUE NOT NULL,
  label          VARCHAR(100),          -- 'Security-01', 'Manajer Ops'
  assigned_to    INTEGER,               -- employee_id dari JWT
  active         BOOLEAN DEFAULT TRUE,
  created_at     TIMESTAMP DEFAULT NOW(),
  last_used_at   TIMESTAMP
);

-- Profil member pemegang gelang
CREATE TABLE members (
  id             SERIAL PRIMARY KEY,
  name           VARCHAR(100) NOT NULL,
  phone          VARCHAR(20),
  email          VARCHAR(100),
  joined_at      TIMESTAMP DEFAULT NOW()
);

-- Master titik gate
CREATE TABLE gates (
  id             SERIAL PRIMARY KEY,
  name           VARCHAR(100) NOT NULL,
  location       VARCHAR(255),
  is_active      BOOLEAN DEFAULT TRUE
);

-- Log semua scan gate
CREATE TABLE gate_logs (
  id             BIGSERIAL PRIMARY KEY,
  token          VARCHAR(255) NOT NULL,
  ticket_type    VARCHAR(20) NOT NULL CHECK (ticket_type IN ('qr_guest','rfid_member','rfid_master')),
  ticket_id      BIGINT,                -- NULL jika rfid_master
  master_id      INTEGER,               -- FK → rfid_masters
  gate_id        INTEGER NOT NULL,      -- FK → gates
  scanned_at     TIMESTAMP DEFAULT NOW(),
  result         VARCHAR(10) NOT NULL CHECK (result IN ('granted','denied')),
  deny_reason    VARCHAR(100),          -- 'already_used','expired','wrong_date','blocked', dll
  is_master_used BOOLEAN DEFAULT FALSE,
  offline_mode   BOOLEAN DEFAULT FALSE  -- true jika dikirim saat ESP32 offline
);

-- Transaksi e-wallet RFID Member (topup & debit outlet)
CREATE TABLE wanara_transactions (
  id             BIGSERIAL PRIMARY KEY,
  ticket_id      BIGINT NOT NULL,       -- FK → tickets (rfid_member only)
  outlet_id      INTEGER NOT NULL,      -- FK → outlets
  type           VARCHAR(10) NOT NULL CHECK (type IN ('topup','debit')),
  amount         DECIMAL(10,2) NOT NULL,
  saldo_before   DECIMAL(10,2) NOT NULL,
  saldo_after    DECIMAL(10,2) NOT NULL,
  cashier_id     INTEGER,               -- employee_id dari JWT (yang input topup/debit)
  created_at     TIMESTAMP DEFAULT NOW()
);

-- Notifikasi real-time admin
CREATE TABLE admin_notifications (
  id             BIGSERIAL PRIMARY KEY,
  type           VARCHAR(30) NOT NULL CHECK (type IN ('master_used','low_saldo','gate_error','gate_offline')),
  message        TEXT NOT NULL,
  ref_log_id     BIGINT,                -- FK → gate_logs
  is_read        BOOLEAN DEFAULT FALSE,
  created_at     TIMESTAMP DEFAULT NOW()
);
```

**Auto-expire QR — via APScheduler (setiap tengah malam):**
```python
# Dijalankan via APScheduler di dalam proses Flask pos-service
UPDATE tickets
SET    status = 'expired'
WHERE  type = 'qr_guest'
  AND  status = 'active'
  AND  valid_until < CURRENT_DATE;
```

#### Ekosistem Outlet & E-Wallet

Gelang RFID Member = dompet digital internal wahana. Satu gelang untuk semua transaksi.

| Outlet | Fungsi RFID |
|--------|-------------|
| Gate Masuk | Debit biaya masuk dari saldo |
| Outlet Makanan & Minuman | Tap & bayar menu |
| Wahana & Atraksi | Tap akses per wahana, harga per wahana |
| Toko Souvenir | Tap & bayar produk |
| Loker & Penyewaan | Tap buka loker, tap sewa alat |
| Foto & Dokumentasi | Tap beli foto, UID link ke foto pengunjung |
| Pos Topup | Isi saldo — tersedia di semua outlet + pos utama |

#### Hardware per Titik

| Titik | Komponen |
|-------|----------|
| Gate Masuk | Tripod Turnstile, RC522 RFID Reader, ESP32, 2-CH Relay, Power Supply 12V, Buzzer + LED |
| Tiap Outlet | RC522 RFID Reader, ESP32 (WiFi), LCD/OLED, Mini Thermal Printer (opsional) |
| Pos Topup | PC/Tablet kasir, RFID USB Reader, Software Kasir Web, Printer struk |
| Server | Server 192.168.4.50 — REST API Flask, PostgreSQL, MQTT Broker, Dashboard web |

#### Dashboard Admin Wanara Seta

- Monitor real-time: pengunjung aktif, total scan, saldo beredar
- Monitoring per outlet: pendapatan, transaksi, topup vs debit
- Manajemen tiket: cari by token/UID, lihat status & riwayat
- Manajemen member: profil, riwayat, saldo, blokir/unblokir gelang
- Manajemen RFID Master: tambah/hapus/nonaktifkan, log penggunaan
- Laporan keuangan: harian/mingguan/bulanan per outlet, export PDF & Excel
- Notifikasi Telegram setiap RFID Master dipakai (via n8n webhook)
- Log akses gate lengkap (granted, denied, alasan)

**Format notifikasi master (dikirim via n8n → Telegram):**
```
⚠️  RFID Master digunakan
Label   : Security-01
Gate    : Gate Utama (ID: 1)
Waktu   : 14:32:05 — Sabtu, 31 Mei 2026
Mode    : Online
```

**Alur notifikasi:**
```
gate-service
  → POST /webhook/n8n/{token}
    payload: { label, gate_name, time, mode }
  → n8n menerima → format pesan → kirim Telegram bot → admin
```

#### Keamanan Gate System

| Aspek | Implementasi |
|-------|-------------|
| Transaksi Atomik | Debit saldo + insert transaksi dalam 1 DB transaction |
| Token per Device | Setiap ESP32 punya device_token unik, request tanpa token ditolak |
| Enkripsi | Komunikasi via HTTPS/TLS, saldo tidak disimpan di device |
| Audit Trail | Setiap scan & transaksi dicatat lengkap, tidak bisa dihapus |
| Blokir Gelang | Bisa dari dashboard, saldo bisa dipindahkan ke gelang baru |
| Offline Fail-Secure | Guest & Member ditolak saat offline, hanya Master yang lewat |
| Whitelist Master | Hanya bisa diupdate oleh server yang authenticated |
| QR Anti-Reuse | Token QR 1x pakai, setelah used tidak bisa diubah kembali |
| QR Anti-Stockpile | QR hanya valid pada valid_date (tanggal aktivasi kasir) |
| Role-based Access | Permission via JWT — `pos:wanara-seta:cashier`, `pos:wanara-seta:supervisor` |

---

### Roadmap POS

| Fitur | Status |
|-------|--------|
| Wanara Seta — kasir + stok + laporan (Gift Shop) | MVP |
| Wanara Seta — Gate QR Guest + RFID Master (Fase 1) | MVP |
| Wanara Seta — RFID Member + e-wallet outlet (Fase 2) | MVP |
| Menu digital dengan gambar | MVP |
| Outlet F&B (Restaurant/Bar) | V2 |
| Order per meja | V2 |
| Multi-gate Wanara Seta | V2 |
| QRIS topup saldo member | V2 |
| KDS (Kitchen Display System) | V3 |
| Split bill + void dengan approval | V3 |
| Aplikasi mobile member (saldo, histori) | V3 |
| Loyalty/poin member | V3 |

---

## 10. Keputusan yang Dikunci

> Keputusan berikut **tidak boleh diubah** tanpa diskusi dan persetujuan ulang.

| # | Keputusan | Yang Disepakati |
|---|-----------|----------------|
| 1 | Backend framework | Python Flask — bukan FastAPI, bukan Node.js |
| 2 | Database | PostgreSQL self-hosted port 5433 — bukan Supabase/cloud |
| 3 | ORM | Prisma — konsisten di semua service |
| 4 | Repository | Monorepo satu folder per service |
| 5 | Auth | JWT terpusat di HRD service — tidak ada auth terpisah |
| 6 | Shared code | Via PYTHONPATH — tidak pernah dicopy ke service |
| 7 | Container ekosistem baru | Tidak pakai Docker — pakai systemctl langsung |
| 8 | Docker marketing | Dibiarkan tetap berjalan — tidak disentuh |
| 9 | Akses eksternal | Cloudflare Tunnel / Zero Trust — tanpa port forwarding |
| 10 | n8n | Digunakan TERBATAS — hanya untuk routing notifikasi Telegram gate Wanara Seta. Flask hit webhook n8n, bukan kirim Telegram langsung. Modul lain tidak boleh pakai n8n. |
| 11 | VHP data | Import sekali di awal — tidak ada sync rutin |
| 12 | POS → Sales | Tidak terhubung — `pos_db` mandiri |
| 13 | Users di sales/pos | Tidak ada — semua via `employee_id` dari JWT |
| 14 | Loss record | Wajib sebelum lead bisa di-close — validasi di Flask |
| 15 | Approval cuti GM/VGM | Eskalasi manual — HRD notifikasi, Direktur handle di luar sistem |
| 16 | PDF generation | WeasyPrint (Python) — tidak ada layanan eksternal |
| 17 | Background job | APScheduler di dalam proses Flask — tidak pakai Celery/Redis di MVP |
| 18 | Confirmation Letter | V2 — WeasyPrint template HTML → PDF |
| 19 | BEO generator | V3 — masih butuh bantuan admin, terlalu kompleks untuk MVP |
| 20 | AI features | V3 — lead scoring, LinkedIn prospecting |
| 21 | Media tiket harian Wanara Seta | Gelang kertas/tyvek dengan QR code — dicetak massal via dashboard |
| 22 | Format token QR | `ws-mmyyXXNN` — prefix ws, bulan-tahun, 2 alfabet random, 2 numerik random. Contoh: `ws-0626AB12` |
| 23 | Generate QR | Admin generate N token via dashboard → overlay ke template desain marketing → export PDF A3 → cetak massal |
| 24 | Template desain gelang | Marketing upload template (PNG/SVG) → sistem overlay QR otomatis di posisi yang ditentukan → PDF siap cetak |
| 25 | Stok opname gelang | Tracking via tabel `ticket_batches` — Accounting update status batch: generated → printed → distributed |
| 26 | Aktivasi QR | Kasir scan gelang dari stok → isi form `valid_until` → status unactivated → active. `valid_until` default hari ini, bisa diubah |
| 27 | Berlaku QR | Sampai `valid_until` — bukan hanya 1 hari, fleksibel per kebijakan. APScheduler auto-expire setiap tengah malam |
| 28 | QR tidak jadi pakai | HANGUS setelah `valid_until` — tidak ada reschedule, tidak ada refund |
| 29 | Penggunaan QR | Sekali pakai — setelah buka gate status = used |
| 30 | Media member Wanara Seta | Gelang RFID permanen dengan saldo (e-wallet) |
| 31 | Topup member | Bisa di pos utama MAUPUN di semua outlet |
| 32 | RFID Master | Ada — dipegang operator/security untuk bypass gate |
| 33 | Log RFID Master | SEMUA penggunaan master dicatat di gate_logs |
| 31 | Notifikasi RFID Master | Admin dapat notifikasi real-time setiap master dipakai |
| 34 | Offline RFID Master | RFID Master tetap bisa buka gate saat EMQX offline (whitelist lokal ESP32) |
| 35 | Offline QR Guest & Member | Ditolak (fail-secure) — butuh koneksi EMQX untuk validasi |
| 36 | Sync offline | Log scan offline dikirim ke server via MQTT topic `offline-log` saat reconnect |
| 37 | Firmware ESP32 | Dipegang vendor/teknisi hardware — tim ini TIDAK menulis firmware. Tim ini hanya mendefinisikan API contract (MQTT topic, payload, auth EMQX). |
| 38 | Protokol komunikasi ESP32 ↔ server | **MQTT via EMQX** — ESP32 tidak pernah hit Flask secara HTTP. Semua komunikasi hardware melalui EMQX broker (192.168.4.50:1883). gate-service subscribe/publish via paho-mqtt thread. |
| 39 | Auth ESP32 ke EMQX | Dihandle EMQX di level koneksi MQTT — setiap ESP32 punya `clientId` + `username` + `password` unik yang didaftarkan di EMQX. Tidak ada device_token di HTTP header. |
| 40 | Notifikasi gate Wanara Seta | Via n8n + Telegram bot — gate-service POST ke webhook n8n setelah insert gate_logs. `admin_notifications` di DB tetap ada sebagai audit trail. |
| 41 | MQTT broker | EMQX — self-hosted di server 192.168.4.50, port 1883 (MQTT) / 8083 (WebSocket opsional). |
| 42 | Topic MQTT gate | Format: `wanara/gate/{gate_id}/{event}` — lihat tabel topic di seksi Modul POS. |
| 43 | Whitelist update ke ESP32 | Via MQTT PUBLISH `wanara/gate/whitelist/sync` — bukan HTTP GET. Dikirim gate-service setiap ada perubahan RFID Master di database. |

---

## 11. Status Implementasi

### ✅ Selesai
- [x] Fase arsitektur — semua keputusan dikunci
- [x] Dokumen arsitektur lengkap (.docx)
- [x] Server setup (Ubuntu 24.04, PostgreSQL, Nginx, UFW, fail2ban)
- [x] Ketiga database dibuat (hrd_db, sales_db, pos_db)
- [x] Struktur monorepo dibuat di server
- [x] Shared library ditulis (jwt_guard, response, pagination, date_helper, base_config)
- [x] hrd-service skeleton (app.py, config.py, db/client.py)
- [x] Git repository diinisialisasi

### 🔄 Sedang Berjalan
- [ ] Setup Python virtual environment di server
- [ ] Install dependencies (pip install -r requirements.txt)

### ⏳ Antrian Berikutnya
- [ ] Prisma schema untuk hrd_db
- [ ] Prisma migrate — create tables
- [ ] Modul auth — endpoint login + issue JWT
- [ ] Modul employees — CRUD karyawan
- [ ] Modul attendance — absensi QR + GPS
- [ ] Modul leaves — pengajuan cuti + approval flow
- [ ] hrd-frontend — Next.js app
- [ ] Nginx konfigurasi aktif
- [ ] systemd unit files per service
- [ ] sales-service (setelah HRD stabil)
- [ ] pos-service (setelah HRD stabil)

---

## Environment Variables Reference

### Root .env (hotel-ecosystem/)
```env
POSTGRES_USER=ecosystem_user
POSTGRES_PASSWORD=<strong_password>
JWT_SECRET=<64_char_hex>
FLASK_ENV=development
TZ=Asia/Jakarta
```

### hrd-service/.env
```env
DATABASE_URL=postgresql://ecosystem_user:<password>@localhost:5433/hrd_db
JWT_SECRET=<same_as_root>
PORT=5001
FLASK_ENV=development
SMTP_HOST=smtp.gmail.com
SMTP_PORT=465
SMTP_USER=<gmail>
SMTP_PASS=<app_password>
GPS_RADIUS_METERS=100
TZ=Asia/Jakarta
```

### sales-service/.env
```env
DATABASE_URL=postgresql://ecosystem_user:<password>@localhost:5433/sales_db
JWT_SECRET=<same_as_root>
PORT=5002
FLASK_ENV=development
TZ=Asia/Jakarta
```

### pos-service/.env
```env
DATABASE_URL=postgresql://ecosystem_user:<password>@localhost:5433/pos_db
JWT_SECRET=<same_as_root>
PORT=5003
FLASK_ENV=development
TZ=Asia/Jakarta
```

### gate-service/.env
```env
DATABASE_URL=postgresql://ecosystem_user:<password>@localhost:5433/pos_db
JWT_SECRET=<same_as_root>
PORT=5004
FLASK_ENV=development
TZ=Asia/Jakarta
MQTT_BROKER_HOST=192.168.4.50
MQTT_BROKER_PORT=1883
MQTT_USERNAME=gate-service
MQTT_PASSWORD=<mqtt_password_untuk_gate_service>
N8N_WEBHOOK_URL=<url_webhook_n8n_untuk_notifikasi_gate>
```

---

*Dokumen ini di-maintain secara aktif selama fase implementasi. Setiap keputusan baru yang disepakati harus ditambahkan ke sini sebelum diimplementasikan.*
