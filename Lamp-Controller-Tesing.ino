// clasification included in web & buzzer.ino - Complete System dengan RS485 + WebServer + LCD
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <U8g2lib.h>

// === WiFi & mDNS ===
const char* ssid = "D4TE";
const char* password = "12345678";
const char* mdnsName = "wildan";

// === RS485 Pins ===
const int DE_RE = 4;    // RS485 Direction Control
const int RX_PIN = 16;  // RS485 RX
const int TX_PIN = 17;  // RS485 TX
const int IR = 23;

// === Status LEDs & Buzzer ===
const int buzzer = 27;
const int red = 26;
const int green = 25;

// === LED Control Pins ===
const int LED_BLUE = 18;
const int LED_RED = 19;

// === Sensor Configuration ===
Adafruit_ADS1115 ads;
const float R1 = 22000.0;
const float R2 = 2700.0;
const int NUM_SAMPLES = 20;

// === Shared Sensor Data (Protected by Mutex) ===
volatile float V = 0.0;
volatile float A = 0.0;
volatile float P = 0.0;
volatile bool irState = false;
volatile bool ledBlueState = false;
volatile bool ledRedState = false;
volatile bool statusGreen = false;
volatile bool buzzerShouldBeOn = false;
volatile bool klasifikasi = false;  // Variabel klasifikasi baru
SemaphoreHandle_t dataMutex = NULL;

// === Buzzer Control Variables ===
const unsigned long CHANGE_WINDOW_MS = 3500; // 5 detik untuk mendeteksi pola on-off
const int CHANGE_THRESHOLD = 1; // Minimal 3 perubahan dalam window waktu

// === LCD ===
U8G2_ST7920_128X64_F_SW_SPI u8g2(
  U8G2_R0,     // Rotasi layar
  14,          // E (Enable) pin
  13,          // RW (Clock)
  15,          // RS (Data)
  2            // RST
);

// === Web Server ===
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// === Task Handles ===
TaskHandle_t samplingTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;
TaskHandle_t serialTaskHandle = NULL;
TaskHandle_t lcdTaskHandle = NULL;
TaskHandle_t buzzerTaskHandle = NULL;

// ============================================
// TASK 1: SENSOR SAMPLING (Core 0 - Highest Priority)
// ============================================
void samplingTask(void *parameter) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(200); // 5Hz sampling
  
  Serial.println(F("[CORE 0] Sampling Task started"));
  
  for (;;) {
    // Read Voltage from A0
    int16_t adcV = ads.readADC_SingleEnded(0);
    float voltage = ads.computeVolts(adcV);
    float v = voltage * ((R1 + R2) / R2); // Calibration offset

    if (v < 1.4) v = 0;

    // Read Current from A2 (with averaging)
    int32_t sum = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
      sum += ads.readADC_SingleEnded(2);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    int16_t avg = sum / NUM_SAMPLES;
    
    // Linear calibration for ACS712
    const float m = (0.43 - 0.043) / (17650.0 - 14950.0);
    const float b = 0.043 - (m * 14950.0);
    float current = (((m * avg) + b) * 1000) + 91.64; // Offset dalam mA
    if (current < 0) current = 0;

    // Calculate Power in Watts
    float power = v * (current / 1000);

    // Read IR Sensor
    bool ir = false;
    
    // Read LED states
    bool blueState = !digitalRead(LED_BLUE);
    bool redState = !digitalRead(LED_RED);
    
    // Update indicator LEDs based on voltage
    if (v > 1.0) {
      digitalWrite(green, 1);
      digitalWrite(red, 0);
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        statusGreen = true;
        xSemaphoreGive(dataMutex);
      }
    } else {
      digitalWrite(red, 1);
      digitalWrite(green, 0);
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        statusGreen = false;
        xSemaphoreGive(dataMutex);
      }
    }

    // Update shared data with mutex protection
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      V = v;
      A = current;
      P = power;
      irState = ir;
      ledBlueState = blueState;
      ledRedState = redState;
      // Klasifikasi sama dengan status buzzer (ditentukan di task buzzer)
      xSemaphoreGive(dataMutex);
    } else {
      Serial.println(F("[WARNING] Mutex timeout in sampling task"));
    }

    // Precise timing control
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ============================================
// TASK 2: WEB SERVER & WEBSOCKET (Core 1 - Medium Priority)
// ============================================
void broadcastSensorData() {
  float v, a, p;
  bool klas = false;
  
  // Read with mutex protection (data sensor dan klasifikasi)
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    v = V;
    a = A;
    p = P;
    klas = klasifikasi;  // Baca status klasifikasi
    xSemaphoreGive(dataMutex);
  } else {
    v = a = p = -1;
    klas = false;
  }

  // Manual JSON formatting dengan tambahan klasifikasi
  char jsonBuffer[150];
  snprintf(jsonBuffer, sizeof(jsonBuffer), 
           "{\"V\":%.2f,\"A\":%.3f,\"P\":%.2f,\"klasifikasi\":%s}", 
           v, a, p, klas ? "true" : "false");
  
  ws.textAll(jsonBuffer);
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch(type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] Client #%u connected from %s\n", 
                    client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] Client #%u disconnected\n", client->id());
      break;
    case WS_EVT_ERROR:
      Serial.printf("[WS] Client #%u error\n", client->id());
      break;
    default:
      break;
  }
}

void webTask(void *parameter) {
  Serial.println(F("[CORE 1] Web Task started"));
  
  // Setup WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve HTML from LittleFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
    r->send(LittleFS, "/index.html", "text/html");
  });

  // Serve logo.svg (optional)
  server.on("/logo.svg", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (LittleFS.exists("/logo.svg")) {
      r->send(LittleFS, "/logo.svg", "image/svg+xml");
    } else {
      r->send(200, "image/svg+xml", 
        "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64' viewBox='0 0 24 24' fill='none' stroke='#3B82F6' stroke-width='2'>"
        "<rect x='5' y='5' width='14' height='14' rx='2'/>"
        "<rect x='9' y='9' width='6' height='6'/>"
        "</svg>");
    }
  });

  //Serve logo.svg (optional)
  server.on("/Logo-SEI.svg", HTTP_GET, [](AsyncWebServerRequest *r) {
    if (LittleFS.exists("/Logo-SEI.svg")) {
      r->send(LittleFS, "/Logo-SEI.svg", "image/svg+xml");
    } else {
      r->send(200, "image/svg+xml", 
        "<svg xmlns='http://www.w3.org/2000/svg' width='64' height='64' viewBox='0 0 24 24' fill='none' stroke='#3B82F6' stroke-width='2'>"
        "<rect x='5' y='5' width='14' height='14' rx='2'/>"
        "<rect x='9' y='9' width='6' height='6'/>"
        "</svg>");
    }
  });

  server.onNotFound([](AsyncWebServerRequest *r) {
    r->send(404, "text/plain", "Not Found");
  });

  server.begin();
  Serial.println(F("[WEB] Server started on port 80"));

  // Main loop: Broadcast sensor data
  for (;;) {
    broadcastSensorData();
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(500)); // Broadcast every 500ms
  }
}

// ============================================
// TASK 3: RS485 SERIAL COMMUNICATION (Core 1 - Low Priority)
// ============================================
void sendDataRS485(float voltage, float current, float power, bool klas) {
  // Prepare data strings
  String Vstr = String(voltage, 2);
  String Astr = String(current, 2); // 3 decimal untuk mA precision
  String Pstr = String(power, 2);
  String Kstr = klas ? "PASS" : "FAIL";
  
  // Enable RS485 transmitter
  digitalWrite(DE_RE, HIGH);
  vTaskDelay(pdMS_TO_TICKS(5));
  
  // Send data via RS485
  Serial2.print(F("V = "));
  Serial2.println(Vstr);
  
  Serial2.print(F("A = "));
  Serial2.println(Astr);
  
  Serial2.print(F("P = "));
  Serial2.println(Pstr);
  
  Serial2.print(F("Klasifikasi = "));
  Serial2.println(Kstr);
  
  // Wait for transmission complete
  Serial2.flush();
  vTaskDelay(pdMS_TO_TICKS(5));
  
  // Back to receive mode
  digitalWrite(DE_RE, LOW);
}

void serialTask(void *parameter) {
  const uint16_t BUFFER_SIZE = 64;
  char buffer[BUFFER_SIZE];
  
  Serial.println(F("[CORE 1] Serial Task started"));
  
  for (;;) {
    // Check USB Serial for restart command
    if (Serial.available()) {
      size_t len = Serial.readBytesUntil('\n', buffer, BUFFER_SIZE - 1);
      buffer[len] = '\0';
      
      String input = String(buffer);
      input.trim();
      
      if (input.equalsIgnoreCase("r")) {
        Serial.println(F("\n[SYSTEM] Restarting ESP32..."));
        Serial.flush();
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP.restart();
      }
    }
    
    // Check RS485 Serial for data request
    if (Serial2.available()) {
      size_t len = Serial2.readBytesUntil('\n', buffer, BUFFER_SIZE - 1);
      buffer[len] = '\0';
      
      String input = String(buffer);
      input.trim();
      
      if (input.equalsIgnoreCase("rs")) {
        Serial.println(F("\n[RS485] Command 'rs' received"));
        
        // Read sensor data with mutex
        float localV, localA, localP;
        bool localKlas = false;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          localV = V;
          localA = A;
          localP = P;
          localKlas = klasifikasi;  // Ambil status klasifikasi
          xSemaphoreGive(dataMutex);
        } else {
          Serial.println(F("[ERROR] Mutex timeout in serial task"));
          continue;
        }
        
        // Display on Serial Monitor
        Serial.println(F("[DATA] Current Readings:"));
        Serial.print(F("  Voltage     : ")); Serial.print(localV, 2); Serial.println(F(" V"));
        Serial.print(F("  Current     : ")); Serial.print(localA, 3); Serial.println(F(" mA"));
        Serial.print(F("  Power       : ")); Serial.print(localP, 2); Serial.println(F(" W"));
        Serial.print(F("  Klasifikasi : ")); Serial.println(localKlas ? "OK" : "FAIL");
        
        // Send via RS485
        sendDataRS485(localV, localA, localP, localKlas);
        Serial.println(F("[RS485] Data sent!\n"));
        
        // Blink green LED
        digitalWrite(green, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(green, HIGH);
      }
    }
    
    // Prevent task starvation
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ============================================
// TASK 4: LCD DISPLAY (Core 0 - Medium Priority)
// ============================================
void lcdTask(void *parameter) {
  Serial.println(F("[CORE 0] LCD Task started"));
  
  // Initialize LCD
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_mr);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
  
  // Show initial message
  u8g2.clearBuffer();
  u8g2.drawStr(10, 20, "CONTROLLER TESTING");
  u8g2.drawStr(10, 35, "System Starting...");
  u8g2.sendBuffer();
  
  vTaskDelay(pdMS_TO_TICKS(2000)); // Show startup message for 2 seconds
  
  char voltageStr[8];
  char ampereStr[8];
  char powerStr[8];
  char statusBlue[5];
  char statusRed[5];
  char irStatus[4];
  char klasStatus[5];
  
  for (;;) {
    // Read data with mutex protection
    float localV, localA, localP;
    bool localIR, localBlue, localRed, localKlas;
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      localV = V;
      localA = A;
      localP = P;
      localIR = irState;
      localBlue = ledBlueState;
      localRed = ledRedState;
      localKlas = klasifikasi;  // Ambil status klasifikasi
      xSemaphoreGive(dataMutex);
    } else {
      // Use default values if mutex fails
      localV = 0.0;
      localA = 0.0;
      localP = 0.0;
      localIR = false;
      localBlue = false;
      localRed = false;
      localKlas = false;
    }
    
    // Convert values to strings
    dtostrf(localV, 5, 2, voltageStr);
    dtostrf(localA, 5, 2, ampereStr);
    dtostrf(localP, 5, 2, powerStr);
    
    // Set status strings
    strcpy(statusBlue, localBlue ? "ON" : "OFF");
    strcpy(statusRed, localRed ? "ON" : "OFF");
    strcpy(klasStatus, localKlas ? "PASS" : "FAIL");
    strcpy(irStatus, localIR ? "OK" : "NG");
    
    if (localV > 24.0) {
      strcpy(irStatus, "ON");
    } else {
      strcpy(irStatus, "OFF");
    }

    // Update LCD display
    u8g2.clearBuffer();
    
    u8g2.setFont(u8g2_font_6x10_mr);
    u8g2.drawStr(12, 0, "CONTROLLER TESTING");
    u8g2.drawHLine(0, 9, 128);
    
    // Line 1: Voltage
    u8g2.drawStr(0, 10, "VOLTAGE : ");
    u8g2.drawStr(55, 10, voltageStr);
    u8g2.drawStr(105, 10, "V");
    
    // Line 2: Current
    u8g2.drawStr(0, 19, "CURRENT : ");
    u8g2.drawStr(55, 19, ampereStr);
    u8g2.drawStr(105, 19, "mA");
    
    // Line 3: Power
    u8g2.drawStr(0, 28, "POWER   : ");
    u8g2.drawStr(55, 28, powerStr);
    u8g2.drawStr(105, 28, "W");
    
    // Line 4: LED Blue
    u8g2.drawStr(0, 37, "LED BLUE: ");
    u8g2.drawStr(60, 37, statusBlue);
    
    // Line 5: LED Red
    u8g2.drawStr(0, 46, "LED RED : ");
    u8g2.drawStr(60, 46, statusRed);
    
    // Line 6: Klasifikasi
    u8g2.drawStr(0, 55, "IR      : ");
    u8g2.drawStr(60, 55, irStatus);
    
    u8g2.sendBuffer();
    
    // Update every 300ms
    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

// ============================================
// TASK 5: BUZZER CONTROL (Core 0 - Medium Priority)
// ============================================
void buzzerTask(void *parameter) {
  Serial.println(F("[CORE 0] Buzzer Task started"));
  
  // Variabel untuk menyimpan status klasifikasi
  bool lastKlasifikasi = false;
  
  // Variabel untuk tracking kondisi (untuk mendeteksi apakah pernah mencapai threshold)
  bool pernahVoltageAbove20 = false;
  bool pernahCurrentAbove90 = false;
  bool pernahBlueOn = false;
  bool pernahRedOn = false;
  bool pernahIrOn = false;
  
  // Variabel untuk PULSE DETECTION - sangat penting!
  // Menyimpan waktu terakhir LED dalam state tertentu
  unsigned long lastBlueHighTime = 0;
  unsigned long lastRedHighTime = 0;
  unsigned long lastBlueLowTime = 0;
  unsigned long lastRedLowTime = 0;
  
  // Minimum pulse width untuk deteksi (dalam ms)
  const unsigned long MIN_PULSE_WIDTH = 10; // Hanya butuh 10ms pulse!
  
  // Variabel untuk menyimpan state sebelumnya
  bool lastBlueState = false;
  bool lastRedState = false;
  
  // Variabel untuk delay awal 5 detik
  bool waitingForInitialDelay = false;
  unsigned long startInitialWaitTime = 0;
  const unsigned long INITIAL_WAIT_DURATION = 30000; // 5 detik
  
  // Variabel untuk timer buzzer 5 detik
  bool buzzerActive = false;
  unsigned long buzzerStartTime = 0;
  const unsigned long BUZZER_DURATION = 5000; // Buzzer menyala 5 detik
  
  // Flag untuk menunjukkan sedang dalam proses testing
  bool testingActive = false;
  
  // Variabel untuk debounce dan timing
  unsigned long lastUpdateTime = 0;
  const unsigned long UPDATE_INTERVAL = 5; // SANGAT CEPAT! 5ms untuk menangkap pulse pendek
  
  // Flag untuk mencegah multiple buzzer activation
  bool buzzerAlreadyActivated = false;
  
  // Pulse detection flags
  bool bluePulseDetected = false;
  bool redPulseDetected = false;
  
  for (;;) {
    unsigned long currentTime = millis();
    
    if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
      lastUpdateTime = currentTime;
      
      // Baca semua data dengan mutex protection
      bool localGreenStatus = false;
      bool localBlueState = false;
      bool localRedState = false;
      bool localIrState = false;
      float localV = 0.0;
      float localA = 0.0;
      
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        localGreenStatus = statusGreen;
        localBlueState = ledBlueState;
        localRedState = ledRedState;
        localIrState = irState;
        localV = V;
        localA = A;
        xSemaphoreGive(dataMutex);
      }
      
      // Cek apakah sistem dalam mode testing (greenStatus = true)
      if (localGreenStatus) {
        if (!testingActive && !waitingForInitialDelay) {
          // Mulai delay awal 5 detik
          waitingForInitialDelay = true;
          startInitialWaitTime = currentTime;
          testingActive = true;
          buzzerAlreadyActivated = false;
          
          // Reset semua tracking variabel
          pernahVoltageAbove20 = false;
          pernahCurrentAbove90 = false;
          pernahBlueOn = false;
          pernahRedOn = false;
          pernahIrOn = false;
          
          // Reset pulse detection
          bluePulseDetected = false;
          redPulseDetected = false;
          lastBlueState = false;
          lastRedState = false;
          
          Serial.println(F("[BUZZER] Green status ON - Mulai delay 30 detik..."));
          Serial.println(F("[BUZZER] Pulse detection aktif!"));
        }
        
        // Jika sedang menunggu delay awal 5 detik
        if (waitingForInitialDelay) {
          unsigned long elapsed = currentTime - startInitialWaitTime;
          
          // Tampilkan countdown
          if (elapsed % 1000 == 0 && elapsed <= 30000) {
            int secondsRemaining = (30000 - elapsed) / 1000;
            Serial.print(F("[BUZZER] Menunggu: "));
            Serial.print(secondsRemaining);
            Serial.println(F(" detik lagi..."));
          }
          
          // SELAMA MENUNGGU 5 DETIK, kita tetap monitor LED untuk PULSE!
          // Ini penting karena LED mungkin ON sebelum monitoring resmi dimulai
          
          // PULSE DETECTION untuk LED Biru
          if (localBlueState != lastBlueState) {
            if (localBlueState) { // Rising edge (OFF -> ON)
              lastBlueHighTime = currentTime;
              Serial.println(F("[PULSE] LED Biru: Rising edge terdeteksi"));
            } else { // Falling edge (ON -> OFF)
              lastBlueLowTime = currentTime;
              unsigned long pulseWidth = currentTime - lastBlueHighTime;
              
              if (pulseWidth >= MIN_PULSE_WIDTH && !bluePulseDetected) {
                bluePulseDetected = true;
                pernahBlueOn = true; // SET SEKARANG JUGA!
                Serial.print(F("[PULSE] ✓ LED Biru pulse terdeteksi! Lebar: "));
                Serial.print(pulseWidth);
                Serial.println(F(" ms"));
              }
            }
            lastBlueState = localBlueState;
          }
          
          // PULSE DETECTION untuk LED Merah
          if (localRedState != lastRedState) {
            if (localRedState) { // Rising edge (OFF -> ON)
              lastRedHighTime = currentTime;
              Serial.println(F("[PULSE] LED Merah: Rising edge terdeteksi"));
            } else { // Falling edge (ON -> OFF)
              lastRedLowTime = currentTime;
              unsigned long pulseWidth = currentTime - lastRedHighTime;
              
              if (pulseWidth >= MIN_PULSE_WIDTH && !redPulseDetected) {
                redPulseDetected = true;
                pernahRedOn = true; // SET SEKARANG JUGA!
                Serial.print(F("[PULSE] ✓ LED Merah pulse terdeteksi! Lebar: "));
                Serial.print(pulseWidth);
                Serial.println(F(" ms"));
              }
            }
            lastRedState = localRedState;
          }
          
          // Cek apakah delay sudah selesai
          if (elapsed >= INITIAL_WAIT_DURATION) {
            waitingForInitialDelay = false;
            Serial.println(F("\n[BUZZER] Delay 30 detik selesai - Mulai monitoring klasifikasi!"));
            
            // Tampilkan status pulse detection
            Serial.println(F("[PULSE] Summary setelah delay:"));
            Serial.print(F("  LED Biru pulse: ")); 
            Serial.println(bluePulseDetected ? "TERDETEKSI ✓" : "BELUM");
            Serial.print(F("  LED Merah pulse: ")); 
            Serial.println(redPulseDetected ? "TERDETEKSI ✓" : "BELUM");
          }
        } 
        // Jika delay awal sudah selesai, mulai monitoring untuk klasifikasi
        else {
          // 1. Tracking Voltage > 20V
          if (localV > 24.0) {
            if (!pernahVoltageAbove20) {
              pernahVoltageAbove20 = true;
              Serial.println(F("[BUZZER] ✓ Tegangan > 20V terdeteksi"));
            }
          }
          
          // 2. Tracking Current > 90mA
          if (localA > 90.0) {
            if (!pernahCurrentAbove90) {
              pernahCurrentAbove90 = true;
              Serial.println(F("[BUZZER] ✓ Arus > 90mA terdeteksi"));
            }
          }
          
          // 3. CONTINUOUS PULSE DETECTION untuk LED Biru (selama monitoring)
          if (localBlueState != lastBlueState) {
            if (localBlueState) { // Rising edge
              lastBlueHighTime = currentTime;
              if (!bluePulseDetected) {
                Serial.println(F("[PULSE] LED Biru: Rising edge (masih cari pulse)"));
              }
            } else { // Falling edge
              lastBlueLowTime = currentTime;
              unsigned long pulseWidth = currentTime - lastBlueHighTime;
              
              if (pulseWidth >= MIN_PULSE_WIDTH && !bluePulseDetected) {
                bluePulseDetected = true;
                pernahBlueOn = true;
                Serial.print(F("[PULSE] ✓ LED Biru pulse terdeteksi selama monitoring! Lebar: "));
                Serial.print(pulseWidth);
                Serial.println(F(" ms"));
              }
            }
            lastBlueState = localBlueState;
          }
          
          // 4. CONTINUOUS PULSE DETECTION untuk LED Merah (selama monitoring)
          if (localRedState != lastRedState) {
            if (localRedState) { // Rising edge
              lastRedHighTime = currentTime;
              if (!redPulseDetected) {
                Serial.println(F("[PULSE] LED Merah: Rising edge (masih cari pulse)"));
              }
            } else { // Falling edge
              lastRedLowTime = currentTime;
              unsigned long pulseWidth = currentTime - lastRedHighTime;
              
              if (pulseWidth >= MIN_PULSE_WIDTH && !redPulseDetected) {
                redPulseDetected = true;
                pernahRedOn = true;
                Serial.print(F("[PULSE] ✓ LED Merah pulse terdeteksi selama monitoring! Lebar: "));
                Serial.print(pulseWidth);
                Serial.println(F(" ms"));
              }
            }
            lastRedState = localRedState;
          }
          
          // 5. Tracking IR
          if (localIrState || localV > 20.0) {
            if (!pernahIrOn) {
              pernahIrOn = true;
              Serial.println(F("[BUZZER] ✓ IR ON terdeteksi"));
            }
          }
          
          // LOGIKA KLASIFIKASI:
          bool newKlasifikasi = false;
          
          // Gunakan pulseDetected flags atau pernahBlueOn/RedOn
          bool blueOK = pernahBlueOn || bluePulseDetected;
          bool redOK = pernahRedOn || redPulseDetected;
          
          if (pernahVoltageAbove20 && 
              pernahCurrentAbove90 && 
              blueOK && 
              redOK && 
              pernahIrOn) {
            newKlasifikasi = true; // PASS
          } else {
            newKlasifikasi = false; // FAIL
          }
          
          // Update variabel klasifikasi di shared memory
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            klasifikasi = newKlasifikasi;
            xSemaphoreGive(dataMutex);
          }
          
          // DEBUG: Tampilkan status setiap 2 detik selama monitoring
          static unsigned long lastStatusTime = 0;
          if (currentTime - lastStatusTime >= 2000) {
            lastStatusTime = currentTime;
            Serial.println(F("[STATUS] Monitoring progress:"));
            Serial.print(F("  Voltage>20: ")); Serial.println(pernahVoltageAbove20 ? "✓" : "✗");
            Serial.print(F("  Current>90: ")); Serial.println(pernahCurrentAbove90 ? "✓" : "✗");
            Serial.print(F("  Blue Pulse: ")); Serial.println(bluePulseDetected ? "✓" : "✗");
            Serial.print(F("  Red Pulse: ")); Serial.println(redPulseDetected ? "✓" : "✗");
            Serial.print(F("  IR: ")); Serial.println(pernahIrOn ? "✓" : "✗");
            Serial.print(F("  Klasifikasi: ")); Serial.println(newKlasifikasi ? "PASS" : "FAIL");
            Serial.println(F("----------------------"));
          }
          
          // Kontrol buzzer berdasarkan klasifikasi
          if (newKlasifikasi) {
            // PASS: Buzzer MATI dan reset flag
            digitalWrite(buzzer, LOW);
            buzzerActive = false;
            buzzerAlreadyActivated = false;
            
            if (lastKlasifikasi != newKlasifikasi) {
              Serial.println(F("\n[BUZZER] ===================================="));
              Serial.println(F("[BUZZER] STATUS: PASS - Produk LOLOS uji!"));
              Serial.println(F("[BUZZER] ====================================\n"));
              
              // Detail kondisi
              Serial.println(F("[DETAIL] Semua kondisi terpenuhi:"));
              Serial.println(F("  1. ✓ Tegangan > 20V"));
              Serial.println(F("  2. ✓ Arus > 90mA"));
              Serial.println(F("  3. ✓ LED Biru pulse terdeteksi"));
              Serial.println(F("  4. ✓ LED Merah pulse terdeteksi"));
              Serial.println(F("  5. ✓ IR terdeteksi"));
            }
          } else {
            // FAIL: Cek apakah buzzer perlu diaktifkan
            if (!buzzerAlreadyActivated) {
              // Tunggu 3 detik setelah monitoring dimulai baru bunyikan buzzer
              if ((currentTime - startInitialWaitTime) > (INITIAL_WAIT_DURATION + 3000)) {
                buzzerActive = true;
                buzzerStartTime = currentTime;
                buzzerAlreadyActivated = true;
                digitalWrite(buzzer, HIGH);
                
                Serial.println(F("\n[BUZZER] ===================================="));
                Serial.println(F("[BUZZER] STATUS: FAIL - Buzzer ON (5 detik)"));
                Serial.println(F("[BUZZER] ====================================\n"));
                
                // Tampilkan kondisi yang belum terpenuhi
                Serial.println(F("[DETAIL] Kondisi yang BELUM terpenuhi:"));
                if (!pernahVoltageAbove20) Serial.println(F("  - ✗ Tegangan belum > 20V"));
                if (!pernahCurrentAbove90) Serial.println(F("  - ✗ Arus belum > 90mA"));
                if (!bluePulseDetected) Serial.println(F("  - ✗ LED Biru belum pulse terdeteksi"));
                if (!redPulseDetected) Serial.println(F("  - ✗ LED Merah belum pulse terdeteksi"));
                if (!pernahIrOn) Serial.println(F("  - ✗ IR belum terdeteksi"));
              }
            }
            
            // Cek apakah buzzer sudah menyala selama 5 detik
            if (buzzerActive && (currentTime - buzzerStartTime >= BUZZER_DURATION)) {
              // Matikan buzzer setelah 5 detik
              buzzerActive = false;
              digitalWrite(buzzer, LOW);
              Serial.println(F("[BUZZER] Buzzer dimatikan setelah 5 detik"));
            }
          }
          
          lastKlasifikasi = newKlasifikasi;
        }
      } else {
        // greenStatus = false (tidak dalam mode testing)
        
        // Reset semua variabel dan status
        if (testingActive || waitingForInitialDelay || buzzerActive) {
          pernahVoltageAbove20 = false;
          pernahCurrentAbove90 = false;
          pernahBlueOn = false;
          pernahRedOn = false;
          pernahIrOn = false;
          waitingForInitialDelay = false;
          testingActive = false;
          buzzerActive = false;
          buzzerAlreadyActivated = false;
          bluePulseDetected = false;
          redPulseDetected = false;
          
          // Matikan buzzer
          digitalWrite(buzzer, LOW);
          
          // Reset klasifikasi
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            klasifikasi = false;
            xSemaphoreGive(dataMutex);
          }
          
          if (lastKlasifikasi != false) {
            lastKlasifikasi = false;
          }
          
          Serial.println(F("\n[BUZZER] Green status OFF - Reset semua status testing\n"));
        }
      }
      
      // Update status buzzer jika sedang aktif
      if (buzzerActive) {
        digitalWrite(buzzer, HIGH);
      }
    }
    
    // Delay kecil untuk prevent CPU overload
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize Serial ports
  Serial.begin(115200);
  while (!Serial && millis() < 3000);
  
  Serial2.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // Configure GPIO
  pinMode(DE_RE, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(red, OUTPUT);
  pinMode(green, OUTPUT);
  pinMode(IR, OUTPUT);
  pinMode(LED_BLUE, INPUT);
  pinMode(LED_RED, INPUT);
  
  digitalWrite(DE_RE, LOW);   // RS485 receive mode
  digitalWrite(buzzer, LOW);
  digitalWrite(red, LOW);
  digitalWrite(green, LOW);
  digitalWrite(IR, LOW);
  
  Serial.println(F("\n================================"));
  Serial.println(F("  LED Tester Full System v2.0"));
  Serial.println(F("  WebServer + RS485 + LCD + RTOS"));
  Serial.println(F("================================\n"));
  
  // Initialize I2C & ADS1115
  Wire.begin();
  Serial.print(F("ADS1115... "));
  if (!ads.begin()) {
    Serial.println(F("FAILED!"));
    while (1) {
      digitalWrite(red, !digitalRead(red));
      delay(200);
    }
  }
  Serial.println(F("OK"));
  ads.setGain(GAIN_TWOTHIRDS);
  
  // Initialize LittleFS
  Serial.print(F("LittleFS... "));
  if (!LittleFS.begin(true)) {
    Serial.println(F("FAILED!"));
    while (1) delay(1000);
  }
  Serial.println(F("OK"));
  
  // List files
  Serial.println(F("\nFiles:"));
  File root = LittleFS.open("/");
  File file = root.openNextFile();
  while(file) {
    Serial.print(F("  - "));
    Serial.print(file.name());
    Serial.print(F(" ("));
    Serial.print(file.size());
    Serial.println(F(" bytes)"));
    file = root.openNextFile();
  }
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print(F("\nWiFi"));
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("\nFAILED!"));
    while (1) {
      digitalWrite(red, !digitalRead(red));
      delay(500);
    }
  }
  
  Serial.println(F(" OK"));
  Serial.print(F("IP: ")); Serial.println(WiFi.localIP());
  
  // Initialize mDNS
  if (MDNS.begin(mdnsName)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print(F("mDNS: http://"));
    Serial.print(mdnsName);
    Serial.println(F(".local"));
  }
  
  // Create mutex
  dataMutex = xSemaphoreCreateMutex();
  if (dataMutex == NULL) {
    Serial.println(F("Mutex FAILED!"));
    while (1);
  }
  
  // Create RTOS Tasks
  
  // Task 1: Sampling (Core 0, Priority 3 - Highest)
  xTaskCreatePinnedToCore(
    samplingTask,
    "Sampling",
    4096,
    NULL,
    3,
    &samplingTaskHandle,
    0
  );
  
  // Task 5: Buzzer Control (Core 0, Priority 2)
  xTaskCreatePinnedToCore(
    buzzerTask,
    "BuzzerCtrl",
    4096,
    NULL,
    2,
    &buzzerTaskHandle,
    0
  );
  
  // Task 4: LCD Display (Core 0, Priority 2)
  xTaskCreatePinnedToCore(
    lcdTask,
    "LCD",
    4096,
    NULL,
    2,
    &lcdTaskHandle,
    0
  );
  
  // Task 2: Web Server (Core 1, Priority 2)
  xTaskCreatePinnedToCore(
    webTask,
    "WebServer",
    8192,
    NULL,
    2,
    &webTaskHandle,
    1
  );
  
  // Task 3: RS485 Serial (Core 1, Priority 1 - Lowest)
  xTaskCreatePinnedToCore(
    serialTask,
    "RS485",
    4096,
    NULL,
    1,
    &serialTaskHandle,
    1
  );
  
  Serial.println(F("\n================================"));
  Serial.println(F("  System Ready!"));
  Serial.println(F("  Core 0: Sensor + LCD + Buzzer"));
  Serial.println(F("  Core 1: WebServer + RS485"));
  Serial.println(F("================================\n"));
  
  // Indicate ready
  digitalWrite(green, HIGH);
}

void loop() {
  // Empty - all handled by RTOS tasks
  vTaskDelay(portMAX_DELAY);
}