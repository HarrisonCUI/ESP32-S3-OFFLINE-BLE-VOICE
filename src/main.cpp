#include <M5Cardputer.h>
#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID        "19B10000-E8F2-537E-4F6C-D104768A1214"
#define CHARACTERISTIC_UUID "19B10001-E8F2-537E-4F6C-D104768A1214"
#define COMMAND_UUID        "19B10002-E8F2-537E-4F6C-D104768A1214"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
BLECharacteristic* pCommandCharacteristic = NULL;
bool deviceConnected = false;
bool raiseToSpeakEnabled = false; // Default OFF

M5Canvas canvas;

// IMA ADPCM Tables
const int8_t indexTable[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
const int16_t stepsizeTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

int16_t valprev = 0;
int8_t adpcm_index = 0;

uint8_t adpcm_encode(int16_t sample) {
    int32_t diff = sample - valprev;
    uint8_t sign = (diff < 0) ? 8 : 0;
    if (sign) diff = -diff;

    int16_t step = stepsizeTable[adpcm_index];
    uint8_t delta = 0;
    int32_t vpdiff = step >> 3;

    if (diff >= step) { delta |= 4; diff -= step; vpdiff += step; }
    step >>= 1;
    if (diff >= step) { delta |= 2; diff -= step; vpdiff += step; }
    step >>= 1;
    if (diff >= step) { delta |= 1; vpdiff += step; }

    if (sign) valprev -= vpdiff;
    else valprev += vpdiff;

    if (valprev > 32767) valprev = 32767;
    else if (valprev < -32768) valprev = -32768;

    adpcm_index += indexTable[delta];
    if (adpcm_index < 0) adpcm_index = 0;
    else if (adpcm_index > 88) adpcm_index = 88;

    return sign | delta;
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      M5Cardputer.Display.fillScreen(BLACK);
      M5Cardputer.Display.setCursor(0, 0);
      M5Cardputer.Display.println("BLE Connected!");
      M5Cardputer.Display.println("SPACE: Talk");
      M5Cardputer.Display.printf("R: Raise2Speak [%s]\n", raiseToSpeakEnabled ? "ON" : "OFF");
    }
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      M5Cardputer.Display.fillScreen(BLACK);
      M5Cardputer.Display.setCursor(0, 0);
      M5Cardputer.Display.println("BLE Disconnected");
      pServer->startAdvertising();
    }
};

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    Serial.begin(115200);

    // Audio config (16kHz, mono)
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = 16000;
    mic_cfg.stereo = false;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();
    
    // IMU (Accelerometer & Gyroscope) config
    M5.Imu.begin();

    // BLE config
    BLEDevice::init("Cardputer");
    BLEDevice::setMTU(512); // Require large MTU for audio chunks
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ   |
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
    pCharacteristic->addDescriptor(new BLE2902());

    // Add Command Characteristic for remote control
    pCommandCharacteristic = pService->createCharacteristic(
                        COMMAND_UUID,
                        BLECharacteristic::PROPERTY_READ   |
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
    pCommandCharacteristic->addDescriptor(new BLE2902());

    pService->start();

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone/Mac connections issue
    pAdvertising->setMinPreferred(0x12);
    
    // IMPORTANT: Make sure the device name is also advertised in the payload
    BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
    oAdvertisementData.setFlags(0x04); // BR_EDR_NOT_SUPPORTED
    oAdvertisementData.setName("Cardputer");
    pAdvertising->setAdvertisementData(oAdvertisementData);
    
    BLEDevice::startAdvertising();

    M5Cardputer.Display.setTextSize(2);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.println("BLE Audio Ready");
    M5Cardputer.Display.println("Waiting for connect...");
    
    // Initialize waveform canvas
    canvas.setColorDepth(8); // Use 8-bit color for faster performance
    canvas.createSprite(240, 60);
    canvas.fillSprite(BLACK);
    canvas.drawLine(0, 30, 240, 30, DARKGREY);
    canvas.pushSprite(&M5Cardputer.Display, 0, 60);
}

#define CHUNK_SIZE 480 // 480 samples = 30ms @ 16kHz
int16_t mic_buf[CHUNK_SIZE];
uint8_t ble_buf[CHUNK_SIZE / 2];
bool isRecording = false;
bool wasXPressed = false;

void loop() {
    M5Cardputer.update();
    
    if (deviceConnected) {
        // --- Toggle Raise to Speak Logic ---
        static bool wasRPressed = false;
        bool isRPressed = M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R');
        if (isRPressed && !wasRPressed) {
            raiseToSpeakEnabled = !raiseToSpeakEnabled;
            // Clear line 3 (y=32, h=16) and print status
            M5Cardputer.Display.fillRect(0, 32, 240, 16, BLACK);
            M5Cardputer.Display.setCursor(0, 32);
            M5Cardputer.Display.printf("R: Raise2Speak [%s]", raiseToSpeakEnabled ? "ON" : "OFF");
        }
        wasRPressed = isRPressed;

        // --- Command Logic: Remote Controls ---
        bool isXPressed = M5Cardputer.Keyboard.isKeyPressed('x') || M5Cardputer.Keyboard.isKeyPressed('X');
        if (isXPressed && !wasXPressed) {
            uint8_t cmd = 0x01; // 0x01 = Open Terminal Command
            pCommandCharacteristic->setValue(&cmd, 1);
            pCommandCharacteristic->notify();
            M5Cardputer.Display.fillCircle(220, 20, 10, BLUE); // Blue indicator for command
        } else if (!isXPressed && wasXPressed) {
            M5Cardputer.Display.fillCircle(220, 20, 10, BLACK); // Clear command indicator
        }
        wasXPressed = isXPressed;

        static bool wasLeftPressed = false;
        static bool wasRightPressed = false;
        
        // Use ',' (has '<' printed) and '.' (has '>' printed) for switching macOS spaces
        bool isLeftPressed = M5Cardputer.Keyboard.isKeyPressed(',');
        bool isRightPressed = M5Cardputer.Keyboard.isKeyPressed('.');
        
        if (isLeftPressed && !wasLeftPressed) {
            uint8_t cmd = 0x02; // 0x02 = Switch Left Space
            pCommandCharacteristic->setValue(&cmd, 1);
            pCommandCharacteristic->notify();
            M5Cardputer.Display.fillCircle(220, 20, 10, BLUE);
        } else if (!isLeftPressed && wasLeftPressed) {
            M5Cardputer.Display.fillCircle(220, 20, 10, BLACK);
        }
        wasLeftPressed = isLeftPressed;

        if (isRightPressed && !wasRightPressed) {
            uint8_t cmd = 0x03; // 0x03 = Switch Right Space
            pCommandCharacteristic->setValue(&cmd, 1);
            pCommandCharacteristic->notify();
            M5Cardputer.Display.fillCircle(220, 20, 10, BLUE);
        } else if (!isRightPressed && wasRightPressed) {
            M5Cardputer.Display.fillCircle(220, 20, 10, BLACK);
        }
        wasRightPressed = isRightPressed;

        // --- Raise to Speak Logic ---
        // Get IMU data to detect tilt angle
        float ax = 0.0F, ay = 0.0F, az = 0.0F;
        M5.Imu.getAccelData(&ax, &ay, &az);
        
        // --- Gesture Screen Switch Logic ---
        static unsigned long lastGestureTime = 0;
        // 1000ms cooldown to prevent rapid switching
        if (millis() - lastGestureTime > 1000) {
            if (ax < -0.6F) { // Tilt Left
                uint8_t cmd = 0x02; // Switch Left
                pCommandCharacteristic->setValue(&cmd, 1);
                pCommandCharacteristic->notify();
                M5Cardputer.Display.fillCircle(220, 20, 10, BLUE);
                lastGestureTime = millis();
            } else if (ax > 0.6F) { // Tilt Right
                uint8_t cmd = 0x03; // Switch Right
                pCommandCharacteristic->setValue(&cmd, 1);
                pCommandCharacteristic->notify();
                M5Cardputer.Display.fillCircle(220, 20, 10, BLUE);
                lastGestureTime = millis();
            }
        } else if (millis() - lastGestureTime > 500 && millis() - lastGestureTime < 1000) {
            // Clear the blue circle indicator shortly after switching
            M5Cardputer.Display.fillCircle(220, 20, 10, BLACK);
        }

        // When holding the Cardputer like a microphone, the top edge tilts up.
        // This corresponds to ay becoming significantly negative (e.g. < -0.5G).
        // Flat on table means ay is around 0.0G.
        bool isRaised = raiseToSpeakEnabled && (ay < -0.5F);
        
        // Support both SPACE key OR Raise gesture
        bool shouldRecord = M5Cardputer.Keyboard.isKeyPressed(' ') || isRaised;

        // --- Audio Logic ---
        if (shouldRecord) {
            if (!isRecording) {
                // Draw indicator once when pressed/raised
                M5Cardputer.Display.fillCircle(200, 20, 10, RED);
                isRecording = true;
            }
            
            if (M5.Mic.record(mic_buf, CHUNK_SIZE, 16000)) {
                // Pre-Compression Digital Gain (Boost Volume)
                // This is CRITICAL for ADPCM: Low volume signals get destroyed by quantization noise.
                // Multiplying by 4.0 ensures we use the full dynamic range of the 4-bit compression.
                for (int i = 0; i < CHUNK_SIZE; i++) {
                    int32_t boosted = mic_buf[i] * 4; 
                    if (boosted > 32767) boosted = 32767;
                    if (boosted < -32768) boosted = -32768;
                    mic_buf[i] = (int16_t)boosted;
                }

                // Compress and pack 2 samples into 1 byte (4-bit ADPCM)
                for (int i = 0; i < CHUNK_SIZE; i += 2) {
                    uint8_t nibble1 = adpcm_encode(mic_buf[i]);
                    uint8_t nibble2 = adpcm_encode(mic_buf[i+1]);
                    ble_buf[i/2] = (nibble1 << 4) | nibble2;
                }
                
                // Send via BLE notification in safe 20-byte chunks (MTU fallback safe)
                for (int i = 0; i < CHUNK_SIZE / 2; i += 20) {
                    int send_len = ((CHUNK_SIZE / 2) - i > 20) ? 20 : ((CHUNK_SIZE / 2) - i);
                    pCharacteristic->setValue(ble_buf + i, send_len);
                    pCharacteristic->notify();
                    delay(2); // Small delay to prevent BLE buffer overflow
                }
                
                // Draw Waveform
                canvas.fillSprite(BLACK);
                int prev_y = 30;
                for (int i = 0; i < 240; i++) {
                    // Average 2 samples for each pixel on X axis
                    int32_t avg = (mic_buf[i * 2] + mic_buf[i * 2 + 1]) / 2;
                    // Map amplitude (-4000 to 4000) to sprite height (60 to 0)
                    int y = 30 - (avg * 30 / 4000);
                    if (y < 0) y = 0;
                    if (y > 59) y = 59;
                    if (i > 0) {
                        canvas.drawLine(i - 1, prev_y, i, y, GREEN);
                    }
                    prev_y = y;
                }
                canvas.pushSprite(&M5Cardputer.Display, 0, 60);
            }
        } else {
            if (isRecording) {
                // Clear indicator once when released
                M5Cardputer.Display.fillCircle(200, 20, 10, BLACK);
                
                // Clear waveform to idle line
                canvas.fillSprite(BLACK);
                canvas.drawLine(0, 30, 240, 30, DARKGREY);
                canvas.pushSprite(&M5Cardputer.Display, 0, 60);
                
                isRecording = false;
            }
        }
    }
    
    delay(2);
}
