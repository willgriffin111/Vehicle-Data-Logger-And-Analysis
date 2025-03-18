#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SparkFun_u-blox_GNSS_Arduino_Library.h>
#include "obd.hpp"
#include "server.hpp"

#include <SdFat.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WebServer.h>
   
#define SD_CS_PIN A0   
#define DEBUG true

#define BUTTON_PIN A2      
#define LED_PIN    A1  


// WiFi Access Point Credentials
const char* ssid = "MyESP32AP";
const char* password = "12345678";

// Global variables for file/folder names
char folderName[20];
char fileName[40];

// Module instances
OBD obd;
SFE_UBLOX_GNSS myGNSS;
SdFat SD;
FsFile logFile;

SemaphoreHandle_t sdMutex;

// Global variables for fuel efficiency calculations
float totalSpeedTimeProduct = 0.0;
float totalFuelTimeProduct = 0.0;
unsigned long lastTime = 0;

// Global flags (volatile because they are shared across tasks)
volatile bool isCalibrated = false;
volatile bool loggingActive = false;  // Reflects the state of the button (pressed = logging active)
bool firstLog = true;

// Flags to indicate if the modules have been initialized
bool gnssInitialized = false;
bool obdInitialized = false;

// GNSS Calibration Function (Blocking)
void calibrateGNNS() {
    Serial.println("Starting GNSS Calibration...");
    // Loop until calibration is achieved.
    while (!isCalibrated) {
        if (myGNSS.getEsfInfo()) {
            int fusionMode = myGNSS.packetUBXESFSTATUS->data.fusionMode;
            Serial.print("Fusion Mode: ");
            Serial.println(fusionMode);
            if (fusionMode == 1) {
                Serial.println("Calibrated!");
                isCalibrated = true;
            } else {
                Serial.println("Initialising... Perform calibration maneuvers.");
            }
        } else {
            Serial.println("Failed to retrieve ESF Info. Retrying...");
        }
        delay(1000);
    }
    Serial.println("Calibration Complete!");
}

// Data Acquisition & SD Logging Task (runs on Core 1)
void dataTask(void *pvParameters) {
    (void) pvParameters; // Unused parameter

    // Wait until both GNSS and OBD modules have been initialized.
    while (!gnssInitialized || !obdInitialized) {
         vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Call calibration (blocking) if not already calibrated.
    if (!isCalibrated) {
        calibrateGNNS();
        // Reset the timer after calibration.
        lastTime = millis();
    }
    
    for (;;) {
        unsigned long currentTime = millis();
        float deltaTime = (currentTime - lastTime) / 1000.0; // seconds

        int rpm = 0, speed = 0, throttle = 0;
        float maf = 0.0, mpg = 0.0, avgMPG = 0.0;
        
        // --- OBD-II Data Retrieval ---
        obd.readRPM(rpm);
        obd.readSpeed(speed);
        obd.readMAF(maf);
        obd.readThrottle(throttle);
        
        // --- Fuel Efficiency Calculation ---
        if (speed > 0 && maf > 0) {
            mpg = obd.calculateInstantMPG(speed, maf);
            totalSpeedTimeProduct += (speed * 0.621371) * deltaTime; // Convert to MPH
            totalFuelTimeProduct += (maf * 0.0805) * deltaTime;      // Fuel consumption over time
            avgMPG = (totalFuelTimeProduct > 0) ? (totalSpeedTimeProduct / totalFuelTimeProduct) : 0.0;
        }

        // --- GPS Data Retrieval ---
        byte SIV = myGNSS.getSIV();
        double latitude = myGNSS.getLatitude() / 10000000.0;
        double longitude = myGNSS.getLongitude() / 10000000.0;
        uint8_t hour = myGNSS.getHour();
        uint8_t minute = myGNSS.getMinute();
        uint8_t second = myGNSS.getSecond();
        uint16_t day = myGNSS.getDay();
        uint16_t month = myGNSS.getMonth();
        uint16_t year = myGNSS.getYear();

        char timeStr[10];
        sprintf(timeStr, "%02d:%02d:%02d", hour, minute, second);

        char dateStr[12];
        sprintf(dateStr, "%04d-%02d-%02d", year, month, day);

        // --- IMU Data Retrieval ---
        int accelX = 0, accelY = 0;
        if (myGNSS.getEsfIns()) {
            accelX = myGNSS.packetUBXESFINS->data.xAccel;
            accelY = -myGNSS.packetUBXESFINS->data.yAccel;  // Invert Y-axis 
        }

        // --- Debug Output ---
        if (DEBUG) {
            Serial.printf("\nRPM: %d, Speed (MPH): %.2f, MAF (g/sec): %.2f, Throttle (%%): %d",
                          rpm, speed * 0.621371, maf, throttle);
            Serial.printf("\nInstant MPG: %.2f, Avg MPG: %.2f", mpg, avgMPG);
            Serial.printf("\nTime: %s, Date: %s, Lat: %.7f, Long: %.7f, SIV: %s",
                          timeStr, dateStr, latitude, longitude, (SIV > 0) ? "Valid" : "Dead Reckoning");
            Serial.printf("\nIMU Data: AccelX: %d, AccelY: %d\n", accelX, accelY);
        }

        // --- Logging Data to SD Card (Using Mutex) ---
        // Log only if calibration is done, the button is pressed and a journey is active.
        if (loggingActive) {
            if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000))) { // Protect SD access
                if (firstLog) {
                    sprintf(folderName, "%04d-%02d-%02d", year, month, day);
                    if (!SD.exists(folderName)) SD.mkdir(folderName);
                    sprintf(fileName, "%s/%02d-%02d-%02d.json", folderName, hour, minute, second);
                    logFile = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);
                    if (logFile) {
                        Serial.printf("Log file created: %s\n", fileName);
                        firstLog = false;
                    } else {
                        Serial.println("Failed to create log file.");
                    }
                }

                if (logFile) {
                    StaticJsonDocument<256> jsonDoc;
                    jsonDoc["gps"]["time"] = timeStr;
                    jsonDoc["gps"]["latitude"] = latitude;
                    jsonDoc["gps"]["longitude"] = longitude;
                    jsonDoc["obd"]["rpm"] = rpm;
                    jsonDoc["obd"]["speed"] = speed;
                    jsonDoc["obd"]["maf"] = maf;
                    jsonDoc["obd"]["instant_mpg"] = mpg;
                    jsonDoc["obd"]["throttle"] = throttle;
                    jsonDoc["obd"]["avg_mpg"] = avgMPG;
                    jsonDoc["imu"]["accel_x"] = accelX;
                    jsonDoc["imu"]["accel_y"] = accelY;

                    if (serializeJson(jsonDoc, logFile) == 0) {
                        Serial.println("Failed to serialize JSON.");
                    } else {
                        logFile.println();
                        Serial.println("\nData logged.");
                    }
                    logFile.flush();
                } else {
                    Serial.println("Log file not open. Retrying...");
                    logFile = SD.open(fileName, O_RDWR | O_CREAT | O_AT_END);
                }
                xSemaphoreGive(sdMutex); // Release SD Mutex
            } else {
                Serial.println("SD mutex timeout, skipping write...");
            }
        }

        lastTime = currentTime;
        vTaskDelay(pdMS_TO_TICKS(1000));  // 1-second delay (non-blocking)
    }
}

// Setup: Initialization & Task Creation
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Initialising System...");

    // --- SD Card Initialization ---
    if (!SD.begin(SD_CS_PIN, 1000000)) {
        Serial.println("SD card initialization failed!");
    } else {
        Serial.println("SD card initialized successfully.");
    }

    // --- WiFi Access Point ---
    WiFi.softAP(ssid, password);
    Serial.println("AP IP address: " + WiFi.softAPIP().toString());

    // --- Setup Web Server ---
    setupServer();

    // --- Initialize LED and Button pins ---
    pinMode(BUTTON_PIN, INPUT_PULLUP); 
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); // Start with LED off

    
    lastTime = millis();

    sdMutex = xSemaphoreCreateMutex();
    if (sdMutex == NULL) {
        Serial.println("Failed to create sdMutex!");
    } else {
        Serial.println("Mutex created successfully.");
    }

    // --- Create Data Task on Core 1 ---
    xTaskCreatePinnedToCore(
        dataTask,     // Task function.
        "Data Task",  // Name of task.
        16384,        // Stack size.
        NULL,         // Parameter.
        1,            // Task priority.
        NULL,         // Task handle.
        1             // Run on Core 1.
    );
}

// Main Loop: Handle Web Server, Button, and LED (runs on Core 0)
void loop() {
    server.handleClient();

    // Check for button press to initialise modules (only once)
    if (digitalRead(BUTTON_PIN) == LOW) {
        if (!gnssInitialized) {
            Wire1.setPins(SDA1, SCL1);
            Wire1.begin();
            if (myGNSS.begin(Wire1)) {
                Serial.println("GPS Module Initialized");
                myGNSS.setI2COutput(COM_TYPE_UBX);
                gnssInitialized = true;
            } else {
                Serial.println("Failed to initialize NEO-M8U Module");
            }
        }
        if (!obdInitialized) {
            if (obd.initialise()) {
                Serial.println("OBD-II Adapter Initialized");
                obdInitialized = true;
            } else {
                Serial.println("Failed to initialise OBD-II Adapter");
            }
        }
    }

    if (!isCalibrated) {
        // Before calibration is complete, blink the LED.
        static unsigned long previousMillis = 0;
        const unsigned long interval = 500; 
        unsigned long currentMillis = millis();
        if (currentMillis - previousMillis >= interval) {
            previousMillis = currentMillis;
            digitalWrite(LED_PIN, !digitalRead(LED_PIN)); 
        }
    } else {
        // Once calibrated, the LED reflects the button state.
        loggingActive = (digitalRead(BUTTON_PIN) == LOW);
        digitalWrite(LED_PIN, loggingActive ? HIGH : LOW);
    }

    // Manages SD log file opening/closing when logging state changes.
    static bool lastLoggingState = false;
    if (loggingActive != lastLoggingState) {
        if (loggingActive) {
            // Logging just activated—prepare a new log file.
            firstLog = true;
            Serial.println("Logging activated.");
        } else {
            // Logging just deactivated—close the current log file if open.
            if (logFile) {
                if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000))) {
                    logFile.close();
                    xSemaphoreGive(sdMutex);
                    Serial.println("Log file closed.");
                }
            }
        }
        lastLoggingState = loggingActive;
    }
    delay(10);  // Short delay 
}
