#include <Arduino.h>
#include <Wire.h>

// ─── PIN DEFINITIONS (STRICTLY REQUIRED) ──────────────────────────
#define PIN_JOY_X   PA0
#define PIN_JOY_Y   PA1
#define PIN_JOY_SW  PA2
#define PIN_SHOOT   PA3
#define PIN_RESPAWN PA4

// I2C pins for MPU-6500 (Wire default on STM32 Arduino for I2C1)
// PB6 = SCL, PB7 = SDA

const uint8_t MPU = 0x68;

// ─── TUNING ────────────────────────────────────────────────────────
#define SAMPLE_RATE_HZ   50
#define SAMPLE_PERIOD_MS (1000 / SAMPLE_RATE_HZ)  // 20 ms
#define COMP_ALPHA       0.98f   // complementary filter: trust gyro 98%, accel 2%
#define CALIBRATION_MS   2000    // 2 seconds of startup calibration
#define GYRO_DEADZONE    0.02f   // rad/s — ignore noise below this

// ─── STATE ─────────────────────────────────────────────────────────
float yaw   = 0.0f;   // degrees, integrated from gyro Z
float pitch = 0.0f;   // degrees, fused from gyro X + accel tilt

// Calibration offsets (average gyro bias at rest)
// Factory calibration from 10,000-sample test (converted °/s → rad/s):
//   X: -2.9890 °/s  →  -0.05217 rad/s
//   Y: +1.7953 °/s  →  +0.03133 rad/s
//   Z: +4.4749 °/s  →  +0.07810 rad/s
float gyroBiasX = -0.05217f;
float gyroBiasY =  0.03133f;
float gyroBiasZ =  0.07810f;

bool calibrated = false;
unsigned long calibStart = 0;
int calibSamples = 0;
float calibSumX = 0.0f, calibSumY = 0.0f, calibSumZ = 0.0f;

unsigned long lastTime = 0;
bool mpu_ok = false;
bool hardwareTestMode = false;

void setup() {
    Serial.begin(115200);
    delay(2000); // Give USB CDC time to connect

    // Configure button pins with internal pull-ups
    pinMode(PIN_SHOOT, INPUT_PULLUP);
    pinMode(PIN_RESPAWN, INPUT_PULLUP);
    pinMode(PIN_JOY_SW, INPUT_PULLUP);
    
    // Hold SCOPE (Joystick SW) during boot to enter hardware test mode
    if (digitalRead(PIN_JOY_SW) == LOW) {
        hardwareTestMode = true;
        Serial.println("\n=================================");
        Serial.println("ASTHRA BLUE PILL HARDWARE TEST");
        Serial.println("=================================");
    }

    // Initialize I2C for MPU-6500
    Wire.begin(); 
    
    Wire.beginTransmission(MPU);
    if (Wire.endTransmission() == 0) {
        mpu_ok = true;
        
        // Wake up MPU-6500
        Wire.beginTransmission(MPU);
        Wire.write(0x6B); // PWR_MGMT_1 register
        Wire.write(0x00); // 0 to wake up
        Wire.endTransmission(true);

        // Config Accel to +/- 4g (matches original MPU6050_RANGE_4_G)
        Wire.beginTransmission(MPU);
        Wire.write(0x1C);
        Wire.write(0x08); 
        Wire.endTransmission(true);

        // Config Gyro to +/- 500 deg/s (matches original MPU6050_RANGE_500_DEG)
        Wire.beginTransmission(MPU);
        Wire.write(0x1B);
        Wire.write(0x08);
        Wire.endTransmission(true);

        // Config DLPF to ~20Hz (matches original MPU6050_BAND_21_HZ)
        Wire.beginTransmission(MPU);
        Wire.write(0x1A); // CONFIG register
        Wire.write(0x04); // DLPF_CFG = 4 (~20Hz)
        Wire.endTransmission(true);

        if (hardwareTestMode) {
            Serial.println("MPU-6500: OK");
        }
    } else {
        if (hardwareTestMode) {
            Serial.println("MPU-6500: NOT DETECTED");
        } else {
            Serial.println("{\"E\":\"MPU-6500 not found!\"}");
        }
        while (1) { delay(1000); }
    }

    if (!hardwareTestMode) {
        Serial.println("{\"E\":\"Calibrating... hold still\"}");
    }
    
    calibStart = millis();
    lastTime = micros();
}

void loop() {
    unsigned long now = micros();
    float dt = (now - lastTime) / 1000000.0f;  // seconds
    lastTime = now;

    // Clamp dt to avoid spikes
    if (dt <= 0.0f || dt > 0.1f) dt = (float)SAMPLE_PERIOD_MS / 1000.0f;

    // Read MPU-6500 raw data
    Wire.beginTransmission(MPU);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(MPU, (uint8_t)14, (uint8_t)true); 

    int16_t AcX = Wire.read()<<8 | Wire.read(); 
    int16_t AcY = Wire.read()<<8 | Wire.read(); 
    int16_t AcZ = Wire.read()<<8 | Wire.read(); 
    Wire.read(); Wire.read(); // Skip temp
    int16_t GyX = Wire.read()<<8 | Wire.read(); 
    int16_t GyY = Wire.read()<<8 | Wire.read(); 
    int16_t GyZ = Wire.read()<<8 | Wire.read(); 

    // Convert raw to physical (Accel 4g = 8192 LSB/g, Gyro 500dps = 65.5 LSB/dps)
    float ax = AcX / 8192.0f;
    float ay = AcY / 8192.0f;
    float az = AcZ / 8192.0f;
    
    // Gyro in rad/s (500 dps scale = 65.5 LSB/deg/s. 1 deg = 0.0174533 rad)
    float gx = (GyX / 65.5f) * 0.0174533f;
    float gy = (GyY / 65.5f) * 0.0174533f;
    float gz = (GyZ / 65.5f) * 0.0174533f;

    // ─── READ BUTTONS ──────────────────────────────────────────────
    int shoot  = (digitalRead(PIN_SHOOT)  == LOW) ? 1 : 0;
    int reload = (digitalRead(PIN_RESPAWN) == LOW) ? 1 : 0;
    int scope  = (digitalRead(PIN_JOY_SW)   == LOW) ? 1 : 0;
    int pauseBtn = 0; // Hardcoded to 0 as there is no physical pause button

    // ─── READ JOYSTICK ANALOG ──────────────────────────────────────
    int joyX = analogRead(PIN_JOY_X);
    int joyY = analogRead(PIN_JOY_Y);

    if (hardwareTestMode) {
        Serial.print("\033[2J\033[H");
        Serial.println("=================================");
        Serial.println("ASTHRA BLUE PILL HARDWARE TEST");
        Serial.println("=================================");
        Serial.println(mpu_ok ? "MPU-6500: OK\n" : "MPU-6500: NOT DETECTED\n");
        
        Serial.println("ACC:");
        Serial.print("X="); Serial.println(ax, 2);
        Serial.print("Y="); Serial.println(ay, 2);
        Serial.print("Z="); Serial.println(az, 2);
        Serial.println();
        
        Serial.println("GYRO:");
        Serial.print("X="); Serial.println(gx * 57.2958f, 2); // print deg/s
        Serial.print("Y="); Serial.println(gy * 57.2958f, 2);
        Serial.print("Z="); Serial.println(gz * 57.2958f, 2);
        Serial.println();
        
        Serial.println("JOYSTICK:");
        Serial.print("X="); Serial.println(joyX);
        Serial.print("Y="); Serial.println(joyY);
        Serial.println();
        
        Serial.println("BUTTONS:");
        Serial.print("SCOPE="); Serial.println(scope ? "PRESSED" : "RELEASED");
        Serial.print("SHOOT="); Serial.println(shoot ? "PRESSED" : "RELEASED");
        Serial.print("RESPAWN="); Serial.println(reload ? "PRESSED" : "RELEASED");
        
        delay(100);
        return;
    }

    // ─── CALIBRATION PHASE ─────────────────────────────────────────
    if (!calibrated) {
        calibSumX += gx;
        calibSumY += gy;
        calibSumZ += gz;
        calibSamples++;

        if (millis() - calibStart >= CALIBRATION_MS) {
            gyroBiasX = calibSumX / calibSamples;
            gyroBiasY = calibSumY / calibSamples;
            gyroBiasZ = calibSumZ / calibSamples;
            calibrated = true;
            yaw = 0.0f;
            pitch = 0.0f;
            Serial.println("{\"E\":\"Calibration done\"}");
        }
        delay(SAMPLE_PERIOD_MS);
        return;
    }

    // ─── REMOVE BIAS ───────────────────────────────────────────────
    gx -= gyroBiasX;
    gy -= gyroBiasY;
    gz -= gyroBiasZ;

    // ─── DEADZONE FILTER ───────────────────────────────────────────
    if (fabsf(gx) < GYRO_DEADZONE) gx = 0.0f;
    if (fabsf(gy) < GYRO_DEADZONE) gy = 0.0f;
    if (fabsf(gz) < GYRO_DEADZONE) gz = 0.0f;

    // ─── PITCH: COMPLEMENTARY FILTER (gyro + accel) ────────────────
    float accelPitch = atan2f(-ax, sqrtf(ay * ay + az * az));
    float accelPitchDeg = accelPitch * 57.2958f;  // RAD_TO_DEG

    float gyroPitchDelta = gy * dt * 57.2958f;  // GyroY is pitch rotation

    pitch = COMP_ALPHA * (pitch + gyroPitchDelta) + (1.0f - COMP_ALPHA) * accelPitchDeg;

    // ─── YAW: GYRO-ONLY INTEGRATION ───────────────────────────────
    float gyroYawDelta = gz * dt * 57.2958f;  // GyroZ is yaw rotation
    yaw += gyroYawDelta;

    // ─── SEND JSON ─────────────────────────────────────────────────
    Serial.print("{\"Y\":");
    Serial.print(yaw, 2);
    Serial.print(",\"P\":");
    Serial.print(pitch, 2);
    Serial.print(",\"GX\":");
    Serial.print(gx, 3);
    Serial.print(",\"GY\":");
    Serial.print(gy, 3);
    Serial.print(",\"GZ\":");
    Serial.print(gz, 3);
    Serial.print(",\"S\":");
    Serial.print(shoot);
    Serial.print(",\"R\":");
    Serial.print(reload);
    Serial.print(",\"SC\":");
    Serial.print(scope);
    Serial.print(",\"JX\":");
    Serial.print(joyX);
    Serial.print(",\"JY\":");
    Serial.print(joyY);
    Serial.print(",\"PA\":");
    Serial.print(pauseBtn);
    Serial.println("}");

    delay(SAMPLE_PERIOD_MS);
}
