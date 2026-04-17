#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// ==================== DEFINISI PIN & KONSTANTA ====================
// Servo
#define SERVO_PIN 9
#define SERVO_OPEN 0
#define SERVO_CLOSE 90

// LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Sensor IR RPM (Interrupt)
#define IR_SENSOR_PIN 2

// Sound Detector KY-037
#define SOUND_PIN A0

// Tombol Mode (aktif HIGH)
#define BUTTON_MODE1_PIN 4
#define BUTTON_MODE2_PIN 5
#define BUTTON_MODE3_PIN 6

Servo katupServo;

// ==================== VARIABEL GLOBAL ====================
// RPM
volatile unsigned int pulseCount = 0;
unsigned long lastRPMCalcTime = 0;
unsigned int currentRPM = 0;
unsigned long lastRPMUpdate = 0;

// Suara
int soundLevel = 0;

// Mode Operasi
enum SystemMode { MODE_IDLE = 1, MODE_NORMAL = 2, MODE_BURU = 3 };
SystemMode currentMode = MODE_IDLE;

// Timing Non-Blocking
unsigned long cycleStartTime = 0;
bool isCycleActive = false;
bool isClosingPhase = false;
unsigned long cycleDuration = 60000;   // 1 menit

// Threshold
const int SOUND_THRESHOLD = 80;
const int RPM_THRESHOLD = 4000;

// RPM Timeout Reset
const unsigned long RPM_TIMEOUT = 10000;
unsigned long rpmZeroStartTime = 0;

// === KHUSUS MODE 1: Jeda 1 detik setelah menutup ===
unsigned long mode1CloseStartTime = 0;
bool mode1CloseTimerActive = false;

// Debounce Tombol
unsigned long lastDebounceTime1 = 0, lastDebounceTime2 = 0, lastDebounceTime3 = 0;
const unsigned long debounceDelay = 50;
bool lastButtonState1 = LOW, lastButtonState2 = LOW, lastButtonState3 = LOW;

// Update LCD
unsigned long lastLCDUpdate = 0;
const unsigned long lcdInterval = 500;

// ==================== INTERRUPT RPM ====================
void countPulse() {
    pulseCount++;
}

// ==================== BACA SUARA ====================
int readSoundLevel() {
    int raw = analogRead(SOUND_PIN);
    return map(raw, 0, 1023, 0, 100);
}

// ==================== KALKULASI RPM ====================
void calculateRPM() {
    unsigned long now = millis();
    unsigned long deltaTime = now - lastRPMCalcTime;
    if (deltaTime >= 1000) {
        noInterrupts();
        unsigned int pulses = pulseCount;
        pulseCount = 0;
        interrupts();
        if (pulses > 0) {
            currentRPM = (pulses * 60000UL) / deltaTime;
        } else {
            currentRPM = 0;
        }
        lastRPMCalcTime = now;
        lastRPMUpdate = now;
    }
}

// ==================== KONTROL SERVO ====================
void setServoPosition(int angle) {
    katupServo.write(angle);
}

// ==================== RESET SIKLUS ====================
void resetCycle() {
    isCycleActive = false;
    isClosingPhase = false;
    setServoPosition(SERVO_OPEN);
}

// ==================== LOGIKA MODE ====================
void processMode() {
    // Cek timeout RPM = 0
    if (currentRPM == 0) {
        if (rpmZeroStartTime == 0) {
            rpmZeroStartTime = millis();
        } else if (millis() - rpmZeroStartTime > RPM_TIMEOUT) {
            currentMode = MODE_IDLE;
            resetCycle();
            rpmZeroStartTime = 0;
            mode1CloseTimerActive = false; // Reset timer mode1
        }
    } else {
        rpmZeroStartTime = 0;
    }

    bool soundTrigger = (soundLevel > SOUND_THRESHOLD);
    bool rpmTrigger = (currentRPM > RPM_THRESHOLD);

    // --- STATE MACHINE ---
    switch (currentMode) {
        case MODE_IDLE: {
            // Mode 1: Suara > 80dB → servo menutup, lalu tunggu 1 detik sebelum boleh buka kembali
            if (soundTrigger) {
                // Selama timer jeda 1 detik aktif, servo tetap menutup
                if (!mode1CloseTimerActive) {
                    // Baru terpicu suara, mulai tutup dan aktifkan timer
                    setServoPosition(SERVO_CLOSE);
                    mode1CloseTimerActive = true;
                    mode1CloseStartTime = millis();
                }
            } else {
                // Suara di bawah threshold
                if (mode1CloseTimerActive) {
                    // Cek apakah jeda 1 detik sudah lewat
                    if (millis() - mode1CloseStartTime >= 1000) {
                        setServoPosition(SERVO_OPEN);
                        mode1CloseTimerActive = false;
                    } else {
                        // Masih dalam masa jeda, servo tetap tutup
                        setServoPosition(SERVO_CLOSE);
                    }
                } else {
                    // Normal: suara rendah dan timer tidak aktif → buka
                    setServoPosition(SERVO_OPEN);
                }
            }
            isCycleActive = false;
            break;
        }

        case MODE_NORMAL:
            if (soundTrigger && rpmTrigger) {
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

        case MODE_BURU:
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

    // Proses siklus 1 menit (Mode 2 dan 3)
    if (isCycleActive) {
        unsigned long elapsed = millis() - cycleStartTime;
        if (elapsed >= cycleDuration) {
            isClosingPhase = !isClosingPhase;
            cycleStartTime = millis();
            if (isClosingPhase) {
                setServoPosition(SERVO_CLOSE);
            } else {
                setServoPosition(SERVO_OPEN);
            }
        }
    }
}

// ==================== UPDATE LCD ====================
void updateLCD() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("M:");
    lcd.print(currentMode);
    lcd.print(" RPM:");
    lcd.print(currentRPM);
    lcd.setCursor(0, 1);
    lcd.print("S:");
    lcd.print(soundLevel);
    lcd.print("% V:");
    if (katupServo.read() == SERVO_CLOSE) {
        lcd.print("CLOSE");
    } else {
        lcd.print("OPEN ");
    }
}

// ==================== PEMBACAAN TOMBOL (HIGH AKTIF) ====================
void checkButtons() {
    // Tombol Mode 1
    bool reading1 = digitalRead(BUTTON_MODE1_PIN);
    if (reading1 != lastButtonState1) {
        lastDebounceTime1 = millis();
    }
    if ((millis() - lastDebounceTime1) > debounceDelay) {
        if (reading1 == HIGH) {
            currentMode = MODE_IDLE;
            resetCycle();
            mode1CloseTimerActive = false; // Reset timer
            rpmZeroStartTime = 0;
            while(digitalRead(BUTTON_MODE1_PIN) == HIGH); // tunggu lepas
        }
    }
    lastButtonState1 = reading1;

    // Tombol Mode 2
    bool reading2 = digitalRead(BUTTON_MODE2_PIN);
    if (reading2 != lastButtonState2) {
        lastDebounceTime2 = millis();
    }
    if ((millis() - lastDebounceTime2) > debounceDelay) {
        if (reading2 == HIGH) {
            currentMode = MODE_NORMAL;
            resetCycle();
            mode1CloseTimerActive = false;
            rpmZeroStartTime = 0;
            while(digitalRead(BUTTON_MODE2_PIN) == HIGH);
        }
    }
    lastButtonState2 = reading2;

    // Tombol Mode 3
    bool reading3 = digitalRead(BUTTON_MODE3_PIN);
    if (reading3 != lastButtonState3) {
        lastDebounceTime3 = millis();
    }
    if ((millis() - lastDebounceTime3) > debounceDelay) {
        if (reading3 == HIGH) {
            currentMode = MODE_BURU;
            resetCycle();
            mode1CloseTimerActive = false;
            rpmZeroStartTime = 0;
            while(digitalRead(BUTTON_MODE3_PIN) == HIGH);
        }
    }
    lastButtonState3 = reading3;
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(9600);

    katupServo.attach(SERVO_PIN);
    katupServo.write(SERVO_OPEN);

    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("3-Button Mode");
    delay(1000);

    pinMode(BUTTON_MODE1_PIN, INPUT);
    pinMode(BUTTON_MODE2_PIN, INPUT);
    pinMode(BUTTON_MODE3_PIN, INPUT);

    pinMode(IR_SENSOR_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(IR_SENSOR_PIN), countPulse, FALLING);

    lastRPMCalcTime = millis();
    lastLCDUpdate = millis();
}

// ==================== LOOP UTAMA ====================
void loop() {
    soundLevel = readSoundLevel();
    checkButtons();
    calculateRPM();
    processMode();

    if (millis() - lastLCDUpdate >= lcdInterval) {
        updateLCD();
        lastLCDUpdate = millis();

        // Debug Serial
        Serial.print("Mode: ");
        Serial.print(currentMode);
        Serial.print(" RPM: ");
        Serial.print(currentRPM);
        Serial.print(" Sound: ");
        Serial.print(soundLevel);
        Serial.print("% Servo: ");
        Serial.println(katupServo.read());
    }
}