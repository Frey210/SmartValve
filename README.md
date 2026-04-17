# Sistem Kontrol Katup Servo SG90 - Arduino Uno R3

![PlatformIO](https://img.shields.io/badge/PlatformIO-Project-orange?logo=platformio)
![Board](https://img.shields.io/badge/Board-Arduino%20Uno-blue)
![Status](https://img.shields.io/badge/Status-Active-success)

Sistem kontrol katup otomatis menggunakan **Servo SG90** dengan 3 mode operasi berbasis sensor suara (KY-037) dan RPM (IR Speed Sensor).

---

## 📋 Spesifikasi Hardware

| Komponen               | Pin Arduino | Keterangan                 |
|-----------------------|------------|----------------------------|
| Servo SG90            | D9 (PWM)   | Kontrol sudut katup        |
| LCD I2C 16x2          | SDA, SCL   | Alamat 0x27                |
| IR Speed Sensor       | D2         | Interrupt untuk hitung RPM |
| Sound Detector KY-037 | A0         | Input analog suara         |
| Push Button           | D3         | Mode switch (pull-up)      |

---

## 🔌 Wiring Diagram


( Tambahkan wiring diagram di sini jika diperlukan )


---

## 🔄 Diagram State Machine

```mermaid
stateDiagram-v2
    [*] --> Mode1_Idle

    state Mode1_Idle {
        [*] --> Idle_Check
        Idle_Check --> Servo_Open : Suara ≤ 80
        Idle_Check --> Servo_Close : Suara > 80
        Servo_Open --> Idle_Check
        Servo_Close --> Idle_Check
    }

    state Mode2_Normal {
        [*] --> Normal_Check
        Normal_Check --> Servo_Open : Suara ≤ 80 ATAU RPM ≤ 4000
        Normal_Check --> Cycle_Active : Suara > 80 DAN RPM > 4000

        state Cycle_Active {
            [*] --> Close_1min
            Close_1min --> Open_1min : 1 menit
            Open_1min --> Close_1min : 1 menit
        }

        Cycle_Active --> Normal_Check
        Servo_Open --> Normal_Check
    }

    state Mode3_Buru {
        [*] --> Buru_Check
        Buru_Check --> Servo_Open : RPM > 4000
        Buru_Check --> Cycle_Active2 : RPM < 4000

        state Cycle_Active2 {
            [*] --> Close_1min2
            Close_1min2 --> Open_1min2 : 1 menit
            Open_1min2 --> Close_1min2 : 1 menit
        }

        Cycle_Active2 --> Buru_Check
        Servo_Open --> Buru_Check
    }

    Mode1_Idle --> Mode2_Normal : Tombol
    Mode2_Normal --> Mode3_Buru : Tombol
    Mode3_Buru --> Mode1_Idle : Tombol

    Mode1_Idle --> Mode1_Idle : RPM=0 >10s
    Mode2_Normal --> Mode1_Idle : RPM=0 >10s
    Mode3_Buru --> Mode1_Idle : RPM=0 >10s

```

📊 Flowchart Program
```mermaid
flowchart TD
    A[Loop Start] --> B[Baca Sensor Suara]
    B --> C[Cek Tombol]
    C --> D[Hitung RPM]
    D --> E[Process Mode]
    E --> F{Mode}

    F -->|1| G[Suara > 80?]
    G -->|Ya| H[Servo CLOSE]
    G -->|Tidak| I[Servo OPEN]

    F -->|2| J[Suara > 80 & RPM > 4000?]
    J -->|Ya| K[Siklus 1 menit]
    J -->|Tidak| L[Servo OPEN]

    F -->|3| M[RPM < 4000?]
    M -->|Ya| N[Siklus 1 menit]
    M -->|Tidak| O[Servo OPEN]

    H --> P[Update LCD]
    I --> P
    K --> P
    L --> P
    N --> P
    O --> P

    P --> Q{RPM = 0 > 10s?}
    Q -->|Ya| R[Reset Mode 1]
    Q -->|Tidak| A
    R --> A

```

📈 Diagram Timing

```mermaid
gantt
    title Siklus Servo 1 Menit
    dateFormat X

    section Close
    Servo Close :a1, 0, 60

    section Open
    Servo Open :a2, 60, 60

    section Loop
    Repeat :a3, 120, 60

```

🖥️ Tampilan LCD
```
┌──────────────────┐
│ M:1 RPM:4000     │
│ S:85% V:CLOSE    │
└──────────────────┘

```

🔧 Mode Operasi

🟢 Mode 1 - Idle
```
Kondisi| Aksi Servo
Suara > 80| CLOSE
Suara ≤ 80| OPEN

```

🟡 Mode 2 - Normal
```
Kondisi| Aksi
Suara > 80 & RPM > 4000| Siklus 1 menit
Tidak terpenuhi| OPEN
```

🔴 Mode 3 - Buru
```
Kondisi| Aksi
RPM < 4000| Siklus
RPM ≥ 4000| OPEN

```

⚡ Auto Reset
```
- RPM = 0 selama 10 detik → kembali ke Mode 1

```

🚀 Upload (PlatformIO)
```
pio run
pio run --target upload
pio device monitor

```

📁 Struktur Project
```
servo-katup-kontrol/
├── platformio.ini
├── README.md
├── src/
│   └── main.cpp

```

📜 Lisensi

Open Source

---

Versi: 1.0.0
Board: Arduino Uno R3
