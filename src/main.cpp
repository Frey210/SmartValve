#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ==================== DEFINISI PIN & KONSTANTA ====================
// Servo
#define SERVO_PIN 9
#define SERVO_OPEN 0      // Sudut terbuka penuh (0 derajat)
#define SERVO_CLOSE 90    // Sudut menutup (90 derajat)

// LCD I2C (Alamat 0x27)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Sensor IR (Interrupt)
#define IR_SENSOR_PIN 2   // Hanya Pin 2 atau 3 untuk interrupt Uno

// Sound Detector KY-037
#define SOUND_PIN A0

// Push Button (Mode Switcher)
#define BUTTON_PIN 3

// Servo object
Servo katupServo;

// ==================== VARIABEL GLOBAL ====================
// RPM
volatile unsigned int pulseCount = 0; // Dihitung di ISR
unsigned long lastRPMCalcTime = 0;
unsigned int currentRPM = 0;
unsigned long lastRPMUpdate = 0;       // Untuk mendeteksi RPM = 0 lama

// Suara
int soundLevel = 0;                   // 0-100%

// Mode Operasi
enum SystemMode { MODE_IDLE = 1, MODE_NORMAL = 2, MODE_BURU = 3 };
SystemMode currentMode = MODE_IDLE;
int lastButtonState = HIGH;           // Pull-up internal

// Timing Non-Blocking (menggantikan delay)
unsigned long cycleStartTime = 0;     // Kapan siklus 1 menit dimulai
bool isCycleActive = false;           // Apakah sedang menjalankan siklus 1 menit?
bool isClosingPhase = false;          // Fase menutup atau membuka dalam siklus
unsigned long cycleDuration = 60000;  // 1 menit = 60.000 ms

// Threshold
const int SOUND_THRESHOLD = 80;       // dB (dalam persen 0-100)
const int RPM_THRESHOLD = 4000;       // RPM

// RPM Timeout Reset
const unsigned long RPM_TIMEOUT = 10000; // 10 detik RPM 0 -> Reset ke Mode 1
unsigned long rpmZeroStartTime = 0;

// Debounce Button
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// Update LCD
unsigned long lastLCDUpdate = 0;
const unsigned long lcdInterval = 500; // Update LCD tiap 500ms

// ==================== INTERRUPT SERVICE ROUTINE (RPM) ====================
// Dijalankan setiap kali ada falling edge (atau rising) dari sensor IR
void countPulse() {
    pulseCount++;
}

// ==================== FUNGSI PEMBACAAN SUARA ====================
// Memetakan nilai analog (0-1023) ke persentase (0-100) untuk simulasi dB
int readSoundLevel() {
    int raw = analogRead(SOUND_PIN);
    // KY-037 memiliki output analog. Mapping langsung 0-1023 ke 0-100.
    // Anda bisa menambahkan kalibrasi offset jika perlu.
    return map(raw, 0, 1023, 0, 100);
}

// ==================== FUNGSI KALKULASI RPM ====================
// Dipanggil di loop() setiap interval tertentu
void calculateRPM() {
    // Asumsi: Sensor membaca 1 pulsa per putaran (atau bisa disesuaikan)
    // Jika 1 pulsa = 1 putaran: RPM = (pulseCount / (deltaTime / 60000))
    // deltaTime dalam ms.
    unsigned long now = millis();
    unsigned long deltaTime = now - lastRPMCalcTime;
    
    if (deltaTime >= 1000) { // Hitung tiap 1 detik untuk akurasi
        // Nonaktifkan interrupt sebentar saat membaca pulseCount
        noInterrupts();
        unsigned int pulses = pulseCount;
        pulseCount = 0;
        interrupts();
        
        // Hitung RPM: (pulsa * 60000) / deltaTime
        // Jika 1 putaran = 1 pulsa
        if (pulses > 0) {
            currentRPM = (pulses * 60000UL) / deltaTime;
        } else {
            currentRPM = 0;
        }
        
        lastRPMCalcTime = now;
        lastRPMUpdate = now; // Reset timer update RPM
    }
}

// ==================== FUNGSI KONTROL SERVO ====================
void setServoPosition(int angle) {
    katupServo.write(angle);
}

// ==================== FUNGSI RESET SIKLUS ====================
void resetCycle() {
    isCycleActive = false;
    isClosingPhase = false;
    setServoPosition(SERVO_OPEN); // Default terbuka
}

// ==================== FUNGSI LOGIKA MODE ====================
void processMode() {
    // Cek kondisi RPM = 0 timeout
    if (currentRPM == 0) {
        if (rpmZeroStartTime == 0) {
            rpmZeroStartTime = millis();
        } else if (millis() - rpmZeroStartTime > RPM_TIMEOUT) {
            // Reset ke Mode 1 jika RPM 0 lebih dari 10 detik
            currentMode = MODE_IDLE;
            resetCycle();
            rpmZeroStartTime = 0; // Reset timer
        }
    } else {
        rpmZeroStartTime = 0; // Reset timer jika ada RPM
    }

    // Ambil data sensor
    bool soundTrigger = (soundLevel > SOUND_THRESHOLD);
    bool rpmTrigger = (currentRPM > RPM_THRESHOLD);

    // --- STATE MACHINE PER MODE ---
    switch (currentMode) {
        case MODE_IDLE:
            // Mode 1: Jika Suara > 80dB, servo Menutup. Selain itu Terbuka.
            if (soundTrigger) {
                setServoPosition(SERVO_CLOSE);
            } else {
                setServoPosition(SERVO_OPEN);
            }
            // Tidak ada siklus di mode ini
            isCycleActive = false;
            break;

        case MODE_NORMAL:
            // Mode 2: Jika Suara > 80dB DAN RPM > 4000, jalankan siklus 1 menit.
            if (soundTrigger && rpmTrigger) {
                // Syarat terpenuhi, aktifkan siklus jika belum aktif
                if (!isCycleActive) {
                    // Mulai siklus baru: Menutup dulu
                    isCycleActive = true;
                    isClosingPhase = true;
                    cycleStartTime = millis();
                    setServoPosition(SERVO_CLOSE);
                }
            } else {
                // Syarat tidak terpenuhi: Matikan siklus, servo terbuka
                if (isCycleActive) {
                    resetCycle();
                } else {
                    setServoPosition(SERVO_OPEN);
                }
            }
            break;

        case MODE_BURU:
            // Mode 3: Jika RPM < 4000, jalankan siklus. Jika > 4000, servo terbuka.
            if (currentRPM < RPM_THRESHOLD) {
                if (!isCycleActive) {
                    isCycleActive = true;
                    isClosingPhase = true;
                    cycleStartTime = millis();
                    setServoPosition(SERVO_CLOSE);
                }
            } else {
                if (isCycleActive) {
                    resetCycle();
                } else {
                    setServoPosition(SERVO_OPEN);
                }
            }
            break;
    }

    // --- PROSES SIKLUS 1 MENIT (Jika Aktif) ---
    if (isCycleActive) {
        unsigned long elapsed = millis() - cycleStartTime;
        
        if (elapsed >= cycleDuration) {
            // 1 menit telah berlalu, tukar fase
            isClosingPhase = !isClosingPhase;
            cycleStartTime = millis(); // Reset timer untuk fase berikutnya
            
            if (isClosingPhase) {
                setServoPosition(SERVO_CLOSE);
            } else {
                setServoPosition(SERVO_OPEN);
            }
        }
    }
}

// ==================== FUNGSI UPDATE LCD ====================
void updateLCD() {
    lcd.clear();
    
    // Baris 1: Mode dan RPM
    lcd.setCursor(0, 0);
    lcd.print("M:");
    lcd.print(currentMode);
    lcd.print(" RPM:");
    lcd.print(currentRPM);
    lcd.print("   "); // Clear trailing chars
    
    // Baris 2: Suara dan Status Servo
    lcd.setCursor(0, 1);
    lcd.print("S:");
    lcd.print(soundLevel);
    lcd.print("% ");
    
    // Status servo
    lcd.print("V:");
    if (katupServo.read() == SERVO_CLOSE) {
        lcd.print("CLOSE");
    } else {
        lcd.print("OPEN ");
    }
}

// ==================== FUNGSI BACA BUTTON ====================
void checkButton() {
    int reading = digitalRead(BUTTON_PIN);
    
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
        // Jika tombol ditekan (LOW karena pull-up)
        if (reading == LOW) {
            // Ganti mode
            switch (currentMode) {
                case MODE_IDLE:
                    currentMode = MODE_NORMAL;
                    break;
                case MODE_NORMAL:
                    currentMode = MODE_BURU;
                    break;
                case MODE_BURU:
                    currentMode = MODE_IDLE;
                    break;
            }
            // Reset siklus saat ganti mode
            resetCycle();
            rpmZeroStartTime = 0;
            
            // Tunggu tombol dilepas untuk mencegah multiple trigger
            while(digitalRead(BUTTON_PIN) == LOW);
            delay(10);
        }
    }
    lastButtonState = reading;
}

// ==================== SETUP ====================
void setup() {
    // Inisialisasi Serial (Opsional, untuk debugging)
    Serial.begin(9600);
    
    // Servo
    katupServo.attach(SERVO_PIN);
    katupServo.write(SERVO_OPEN); // Mulai terbuka
    
    // LCD
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("System Starting");
    delay(1000);
    
    // Button
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    // Sensor IR dengan Interrupt
    pinMode(IR_SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(IR_SENSOR_PIN), countPulse, FALLING); // Sesuaikan RISING/FALLING dengan sensor Anda
    
    // Inisialisasi timer
    lastRPMCalcTime = millis();
    lastLCDUpdate = millis();
}

// ==================== LOOP UTAMA ====================
void loop() {
    // 1. Baca sensor suara (terus menerus, cepat)
    soundLevel = readSoundLevel();
    
    // 2. Cek tombol mode (debounced)
    checkButton();
    
    // 3. Hitung RPM setiap 1 detik
    calculateRPM();
    
    // 4. Proses logika kontrol utama (State Machine)
    processMode();
    
    // 5. Update LCD secara periodik (500ms) agar tidak flicker dan beban CPU ringan
    if (millis() - lastLCDUpdate >= lcdInterval) {
        updateLCD();
        lastLCDUpdate = millis();
        
        // Optional: Debug Serial
        Serial.print("Mode: ");
        Serial.print(currentMode);
        Serial.print(" RPM: ");
        Serial.print(currentRPM);
        Serial.print(" Sound: ");
        Serial.print(soundLevel);
        Serial.print("% Servo: ");
        Serial.println(katupServo.read());
    }
    
    // Loop berjalan sangat cepat, semua delay diganti millis()
}
