// ???????????????????????????????????????????????????? //

// Why store audio on SD and motor events in RAM?
//      Can they both be in SD? 
//      Can they both be 'chunked' into RAM and played from there? 

//  Need to ENFORCE Audio from webpage is:
//            16-bit PCM  <-- ?
//            44.1kHz   <-- Maybe change later?
//            mono
// 
//  MAX_EVENTS --> Should it be higher? How much higher can it be?
// 
//  "uint8_t type = data[0]" is unused in MotorCallback()
//        Do I need it?



//////      IMPORTANT CHANGE      //////
//    live playback in sinkCallback()  DISABLED
//    Can ONLY be used later if I want fallback


// ???????????????????????????????????????????????????? //

// Billy Bass Sat Nav / "BLE Bass" control code
// by Ian Renton, 2024. CC Zero / Public Domain
// Based on examples from https://github.com/tierneytim/btAudio & https://github.com/kosme/arduinoFFT
// Libraries used with many thanks.

//      INCLUDES      //
// Basic setup
#include <Arduino.h>
#include <arduinoFFT.h>
// // Setup Bluetooth stuff for receiving AUDIO only
// #include <btAudio.h>
// Setup BLE stuff for receiving motor data
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLECharacteristic.h>
//  Stuff for MicroSD card reader
#include <SPI.h>
#include <SD.h>
// Native I2S driver to send audio from SD card to amplifier
#include <driver/i2s.h>

// Bluetooth device name
#define DEVICE_NAME "BillyBass"
#define SERVICE_UUID "B160BA55-AAAA-0117-3650-005006019920"
#define CHARACTERISTIC_UUID "0ABC1230-0021-0021-0021-333444455555"

#define BLE_ADVERTISE_UUID_SHORT  0xBA55   // arbitrary 16-bit alias, just for discovery

// Timing
#define MOUTH_FLAP_INTERVAL_MILLIS 100 // Rate of mouth movement. Lower number = faster
#define SERIAL_BAUD 115200

// FFT parameters. Hopefully these match the sampling frequency and number of samples per packet
// coming from your device. This is what works with my phone!
#define SAMPLING_FREQUENCY 16000
// #define SAMPLING_FREQUENCY 44100
#define SAMPLES_PER_FFT 2048

// Frequency range to trigger fish movement. Described as a voice frequency range but really
// a lot of instrumental music will fall in this range too
#define VOICE_MIN_FREQ_HZ 300
#define VOICE_MAX_FREQ_HZ 3000

//  AMPLIFIER  I2S pins
#define I2S_WS_PIN 21     // <-- Connects to amp "LRCLK" pin
#define I2S_DOUT_PIN 22   // <-- Connects to amp "DIN" pin
#define I2S_BCLK_PIN 26   // 
#define I2S_SD_PIN  27 // amp Shut Down pin. HIGH=on, LOW=off    ******

//  MOTOR  control pins
  // On WROVER modules, pins 16 & 17 are usually connected internally to the PSRAM chip.
    // If your motor controller is hooked to 16 or 17, Billy Bass might start twitching randomly
    // or the ESP32 will crash on boot!
#define HEADTAIL_MOTOR_PIN_1 4//12  <-- Motor B
#define HEADTAIL_MOTOR_PIN_2 14 // Was 17
#define HEADTAIL_MOTOR_PWM_PIN 13//13
#define MOUTH_MOTOR_PIN_1 25//27    <-- Motor A
#define MOUTH_MOTOR_PIN_2 33//26
#define MOUTH_MOTOR_PWM_PIN 32//25
// #define MOTOTR_STBY_PIN 14 //
// #define HEADs_or_TAILs 12 // Was 16

// SD CARD READER
#define MicroSD_READER_CS 5 //
#define MicroSD_READER_CLK 18 //
#define MicroSD_READER_DI 23 //
#define MicroSD_READER_DO 19 //
// #define MicroSD_READER_CD  // Currently unused

// Misc pins
#define LED_PIN 2
// #define BUTTON_PIN 4

// Motor PWM settings
#define PWM_FREQUENCY 1000
#define PWM_RESOLUTION 8
#define HEADTAIL_MOTOR_PWM_CHANNEL 0
#define MOUTH_MOTOR_PWM_CHANNEL 1
#define HEADTAIL_MOTOR_PWM_DUTY_CYCLE 255 // Proxy for motor speed, up to 2^resolution
#define MOUTH_MOTOR_PWM_DUTY_CYCLE 255    // Proxy for motor speed, up to 2^resolution

////////////////////////////////////////////////
//      Stuff For Custom Controls     //
File audioFile;
volatile bool packetReady = false;
volatile bool playbackStarted = false;
volatile bool isPlayingAudio = false;
#define AUDIO_FILE_PATH "/audio.raw"
#define AUDIO_BUFFER_SIZE 512
uint8_t audioBuffer[AUDIO_BUFFER_SIZE];

static uint32_t startTime = 0;
volatile bool useBLE = false;
uint32_t lastBLEtime = 0;
#define BLE_TIMEOUT_MS 2000

// Motor event struct
struct MotorEvent {
    uint32_t t;
    uint8_t  motor;
    uint8_t  state;
    uint8_t  pwm;
};
////////////////////////////////////////////////

// Function defs
void setup();
void i2sbullshit();
void loop();
void sinkCallback(const uint8_t *data, uint32_t len);
void calcFFT(void *pvParameters);

void pushEvent(MotorEvent e);
bool popEvent(MotorEvent &e);
void playAudioFromSD();

void headOut();
void tailOut();
void headBack();
void tailBack();
void headTailRest();
void flapMouth();
void mouthOpen();
void mouthClose();
void mouthRest();

// Data storage for FFT, FFT object & task
double vReal[SAMPLES_PER_FFT];
double vImag[SAMPLES_PER_FFT];
ArduinoFFT<double> fft = ArduinoFFT<double>(vReal, vImag, SAMPLES_PER_FFT, SAMPLING_FREQUENCY);

// Data storage & mutex for passing audio samples between CPUs
double audioTransferBuffer[SAMPLES_PER_FFT];
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

// // Create Bluetooth audio receiver
// btAudio audio = btAudio(DEVICE_NAME);
// 
//////////////////////////////////////////////////
// Max buffer: tune this to your expected max packet size
// uint8_t* reassemblyBuf = NULL; 
// const size_t REASSEMBLY_SIZE = 262144;
// //*** */ static uint8_t  reassemblyBuf[32768];
// static uint16_t expectedChunks = 0;
// static uint16_t receivedChunks = 0;
// static size_t   reassemblyLen  = 0;
static File receiveFile;
static uint16_t expectedChunks = 0;
static uint16_t receivedChunks = 0;
static size_t totalBytesWritten = 0;
static bool headerParsed = false;
// Header fields parsed from first chunk
static uint16_t numEvents = 0;
static uint32_t audioSize = 0;
static uint8_t headerBuf[6 + 64*7]; // enough for header + up to 64 motor events
static size_t headerBufLen = 0;
static bool headerComplete = false;
//////////////////////////////////////////////////
// // Called once a complete packet has been reassembled
// void handleCompletePacket(uint8_t* data, size_t len) {
//   Serial.printf("Packet received: %d bytes\n", len);
//   if (len < 6) return;
// 
//   useBLE = true;
//   lastBLEtime = millis();
// 
//   // Parse header
//   uint16_t numEvents = data[0] | (data[1] << 8);
//   uint32_t audioSize = data[2] | (data[3] << 8) | (data[4] << 16) | (data[5] << 24);
// 
//   // Parse events
//   int offset = 6;
//   for (int i = 0; i < numEvents; i++) {
//     if (offset + 7 > len) break;
//     MotorEvent e;
//     e.t     = data[offset]   | (data[offset+1] << 8)
//             | (data[offset+2] << 16) | (data[offset+3] << 24);
//     e.motor = data[offset+4];
//     e.state = data[offset+5];
//     e.pwm   = data[offset+6];
//     offset += 7;
// 
//     // Serial.printf("EVENT t=%u motor=%u state=%u pwm=%u\n",
//     //               e.t, e.motor, e.state, e.pwm);
//     pushEvent(e);
//   }
//   // Open file for writing
//   SD.remove(AUDIO_FILE_PATH); // Remove old stored audio  ****** SAVE ALL DATA TO BE REPLAYED LATER? ******  
//   audioFile = SD.open(AUDIO_FILE_PATH, FILE_WRITE);
//   if (!audioFile) {
//     Serial.println("Failed to open file for writing");
//     return;
//   }
//   // Write audio portion
//   if (offset < len) {
//     size_t audioLen = len - offset;
//     audioFile.write(data + offset, audioLen);
//     Serial.printf("Wrote %d bytes of audio to SD\n", audioLen);
//   }
// 
//   audioFile.close();
// 
//   // Signal ready
//   packetReady = true;
// 
//   // Audio data starts here
//   // audioSize and offset are now valid for handing off to btAudio
//   (void)audioSize; // remove this line when you wire up audio handoff
// }
void playAudioFromSD() {
  audioFile = SD.open(AUDIO_FILE_PATH);

  if (!audioFile) {
    Serial.println("Failed to open audio file for reading");
    isPlayingAudio = false;
    return;
  }

  Serial.println("Starting audio playback...");
  digitalWrite(I2S_SD_PIN, HIGH); // Turn the amplifier ON

  while (audioFile.available()) {
    int bytesRead = audioFile.read(audioBuffer, AUDIO_BUFFER_SIZE);

    size_t bytesWritten = 0;
    i2s_write(I2S_NUM_0, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
    vTaskDelay(1); // Feed the watchdog.
    //////////////////////////////////////////////////
    //////////////////////////////////////////////////
    //////////////////////////////////////////////////
    // Optional: If you want to run FFT on this audio data for mouth movements,
    // you would copy the audioBuffer into your vReal array here and call your FFT function.
    //////////////////////////////////////////////////
    //////////////////////////////////////////////////
    //////////////////////////////////////////////////
  }

  audioFile.close();
  digitalWrite(I2S_SD_PIN, LOW); // Turn the amplifier OFF when done
  Serial.println("Audio playback finished");

  isPlayingAudio = false;
}
//////////////////////////////////////////////////
// --- Event queue ---
#define MAX_EVENTS 64
MotorEvent eventQueue[MAX_EVENTS];
volatile int queueHead = 0;
volatile int queueTail = 0;

//////////////////////////////////////////////////
bool once = true;// ********************************
//////////////////////////////////////////////////

portMUX_TYPE queueMux = portMUX_INITIALIZER_UNLOCKED;

void pushEvent(MotorEvent e) {
  portENTER_CRITICAL(&queueMux);
  int next = (queueTail + 1) % MAX_EVENTS;
  if (next != queueHead) {
    eventQueue[queueTail] = e;
    queueTail = next;
  }
  portEXIT_CRITICAL(&queueMux);
}

bool popEvent(MotorEvent &e) {
  portENTER_CRITICAL(&queueMux);
  if (queueHead == queueTail) {
    portEXIT_CRITICAL(&queueMux);
    return false;
  }
  e = eventQueue[queueHead];
  queueHead = (queueHead + 1) % MAX_EVENTS;
  portEXIT_CRITICAL(&queueMux);
  return true;
}
// void pushEvent(MotorEvent e) {
//     int next = (queueTail + 1) % MAX_EVENTS;
//     if (next == queueHead) {
//         Serial.println("WARNING: event queue full");
//         return;
//     }
//     eventQueue[queueTail] = e;
//     queueTail = next;
// } /// *** ?????? "queueHead" & "queueTail" ?????? ***
// 
// bool popEvent(MotorEvent &e) {
//     if (queueHead == queueTail) return false; // empty
//     e = eventQueue[queueHead];
//     queueHead = (queueHead + 1) % MAX_EVENTS;
//     return true;
// }

//////////////////////////////////////////////////
// Called whenever a BLE client writes to the characteristic  //////////////
class MotorCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    uint8_t* data = (uint8_t*)value.data();
    size_t len = value.length();

    if (len < 5) return;

    uint16_t seqNum = data[1] | (data[2] << 8);
    uint16_t totalChunks = data[3] | (data[4] << 8);
    uint8_t* payload = data + 5;
    size_t payloadLen = len - 5;

    if (seqNum == 0) {
      if (!SD.exists("/")) {
        Serial.println("SD not mounted, attempting remount...");
        if (!SD.begin(MicroSD_READER_CS)) {
          Serial.println("ERROR: SD remount failed");
          return;
        }
      }
      //  CHECK FIRST IF AUDIO EXISTS BEFORE TRYING TO REMOVE IT   //
      if (SD.exists(AUDIO_FILE_PATH)) {
        SD.remove(AUDIO_FILE_PATH);
      }
      receiveFile = SD.open(AUDIO_FILE_PATH, FILE_WRITE);
      if (!receiveFile) {
        Serial.println("ERROR: Could not open SD for writing");
        return;
      }
      if (!SD.begin(MicroSD_READER_CS)) {
        Serial.println("ERROR: SD reinit failed");
        return;
      }
      expectedChunks = totalChunks;
      receivedChunks = 0;
      headerComplete = false;
      headerBufLen = 0;
      Serial.println("Fast Sync Started...");
    }
    if (receiveFile) {
      // If we haven't finished the motor list, keep buffering it
      if (!headerComplete) {
        size_t copy = min(payloadLen, sizeof(headerBuf) - headerBufLen);
        memcpy(headerBuf + headerBufLen, payload, copy);
        headerBufLen += copy;

        if (headerBufLen >= 6) {
          numEvents = headerBuf[0] | (headerBuf[1] << 8);
          size_t fullHeaderSize = 6 + (numEvents * 7);
            
          if (headerBufLen >= fullHeaderSize) {
            headerComplete = true;
            // Parse events into the queue
            parseMotorEvents(fullHeaderSize); 
          }
        }
      } else {
        // Header done? Just dump audio data to SD as fast as possible
        receiveFile.write(payload, payloadLen);
      }
    }
    receivedChunks++;
    if (receivedChunks == expectedChunks) {
      receiveFile.close();
      packetReady = true; // Signal the loop to start the show
      Serial.println("Transfer Finished!");
    }
  }
  void parseMotorEvents(size_t headerSize) {
    int offset = 6;
    for (int i = 0; i < numEvents; i++) {
      MotorEvent e;
      e.t = headerBuf[offset] | (headerBuf[offset+1] << 8) | (headerBuf[offset+2] << 16) | (headerBuf[offset+3] << 24);
      e.motor = headerBuf[offset+4];
      e.state = headerBuf[offset+5];
      e.pwm = headerBuf[offset+6];
      pushEvent(e);
      offset += 7;
    }
  }
};
    // receiveFile = SD.open(AUDIO_FILE_PATH, FILE_WRITE);
    // if (!receiveFile) {
    //   Serial.println("ERROR: Could not open SD for writing");
    //   return;
    // }
    // Serial.println("Receiving packet...");
// 
    // Buffer bytes until we have the full header (6 bytes + all motor events)
//     if (!headerComplete) {
//       // Copy into headerBuf until we know how big the header is
//       size_t copy = min(payloadLen, sizeof(headerBuf) - headerBufLen);
//       memcpy(headerBuf + headerBufLen, payload, copy);
//       headerBufLen += copy;
// 
//       // Do we have enough to know the event count?
//       if (headerBufLen >= 6) {
//         numEvents = headerBuf[0] | (headerBuf[1] << 8);
//         audioSize = headerBuf[2] | (headerBuf[3] << 8) 
//                   | (headerBuf[4] << 16) | (headerBuf[5] << 24);
//         size_t fullHeaderSize = 6 + (numEvents * 7);
// 
//         // Do we have the full header yet?
//         if (headerBufLen >= fullHeaderSize) {
//           headerComplete = true;
// 
//           // Parse motor events from header
//           int offset = 6;
//           for (int i = 0; i < numEvents; i++) {
//             if (offset + 7 > (int)headerBufLen) break;
//             MotorEvent e;
//             e.t     = headerBuf[offset]   | (headerBuf[offset+1] << 8)
//                     | (headerBuf[offset+2] << 16) | (headerBuf[offset+3] << 24);
//             e.motor = headerBuf[offset+4];
//             e.state = headerBuf[offset+5];
//             e.pwm   = headerBuf[offset+6];
//             offset += 7;
//             pushEvent(e);
//           }
// 
//           // Write any audio bytes that arrived in this same chunk after the header
//           if (headerBufLen > fullHeaderSize) {
//             size_t audioBytes = headerBufLen - fullHeaderSize;
//             receiveFile.write(headerBuf + fullHeaderSize, audioBytes);
//             totalBytesWritten += audioBytes;
//           }
//         }
//       }
//     } else {
//       // Header already parsed — write payload straight to SD
//       receiveFile.write(payload, payloadLen);
//       totalBytesWritten += payloadLen;
//     }
// 
//     receivedChunks++;
//     expectedChunks = totalChunks;
// 
//     // Last chunk
//     if (receivedChunks == expectedChunks) {
//       receiveFile.close();
//       Serial.printf("Receive complete: %d events, %d audio bytes written\n",
//                     numEvents, totalBytesWritten);
//       startTime = millis();   // sync anchor
//       packetReady = true;
//     }
//   }
// };
//////////////////////////////////////////////////

//////      FOR DEBUGGING     //////
void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
  delay(500); // Gap between blink groups
}
////////////////////////////////////
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    Serial.println("Client connected");
  }
  void onDisconnect(BLEServer* pServer) {
    Serial.println("Client disconnected - restarting advertising");
    delay(500);
    BLEDevice::startAdvertising();
  }
};

// Setup and run the program
void setup(){
  // Set up serial
  Serial.begin(SERIAL_BAUD);
  Serial.printf("Free heap at boot: %d bytes\n", ESP.getFreeHeap());
  // Serial.printf("Free heap min: %d\n", ESP.getMinFreeHeap());
  Serial.println("=== Boot start ==="); // ******

  // Set up debug LED
  pinMode(LED_PIN, OUTPUT);
  // blinkLED(1, 200); // ****** 1 blink = boot started

  pinMode(I2S_SD_PIN, OUTPUT); // ******
  digitalWrite(I2S_SD_PIN, LOW); // ******
  // Set up motor control pins
  pinMode(HEADTAIL_MOTOR_PIN_1, OUTPUT);
  pinMode(HEADTAIL_MOTOR_PIN_2, OUTPUT);
  pinMode(HEADTAIL_MOTOR_PWM_PIN, OUTPUT);
  pinMode(MOUTH_MOTOR_PIN_1, OUTPUT);
  pinMode(MOUTH_MOTOR_PIN_2, OUTPUT);
  pinMode(MOUTH_MOTOR_PWM_PIN, OUTPUT);
  // pinMode(HEADs_or_TAILs, OUTPUT);

  // Set up PWM
  ledcSetup(HEADTAIL_MOTOR_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(MOUTH_MOTOR_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(HEADTAIL_MOTOR_PWM_PIN, HEADTAIL_MOTOR_PWM_CHANNEL);
  ledcAttachPin(MOUTH_MOTOR_PWM_PIN, MOUTH_MOTOR_PWM_CHANNEL);
  ledcWrite(HEADTAIL_MOTOR_PWM_CHANNEL, HEADTAIL_MOTOR_PWM_DUTY_CYCLE);
  ledcWrite(MOUTH_MOTOR_PWM_CHANNEL, MOUTH_MOTOR_PWM_DUTY_CYCLE);
  // digitalWrite(HEADTAIL_MOTOR_PIN_1, HIGH);
  digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW);
  digitalWrite(HEADTAIL_MOTOR_PIN_2, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_1, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_2, LOW);
  // digitalWrite(HEADs_or_TAILs, LOW);

// FROM HERE ***
  i2sbullshit();
  Serial.printf("Free heap after I2S: %d bytes\n", ESP.getFreeHeap());

////////////////////////////////////////////////////////////////////////////////////////////////////////////
  SPI.begin(MicroSD_READER_CLK, MicroSD_READER_DO, MicroSD_READER_DI, MicroSD_READER_CS); // 
////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Set up SD card reader
  if (!SD.begin(MicroSD_READER_CS)) {
    Serial.println("SD init failed!");
  } else {
    Serial.println("SD init success");
  }
  Serial.printf("Free heap after SD: %d bytes\n", ESP.getFreeHeap());

  // Allocate buffer on the heap
  // reassemblyBuf = (uint8_t*)malloc(REASSEMBLY_SIZE);
  // Serial.printf("Free heap after reassembly: %d bytes\n", ESP.getFreeHeap());
  // if (reassemblyBuf == NULL) {
  //     Serial.println("Failed to allocate reassembly buffer!");
  // }
  // 
  // Advertise Bluetooth device
  // audio.begin();
  // Serial.println("audio.begin() done"); // ******
  //////////////////////////////////////////////////
  // Start BLE
  BLEDevice::init(DEVICE_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEDevice::setMTU(517); // Request maximum packet size

  // These lines are missing from your current code — add them back
  BLEService *pService = pServer->createService(BLEUUID::fromString(SERVICE_UUID));
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
    BLEUUID::fromString(CHARACTERISTIC_UUID),
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pCharacteristic->setCallbacks(new MotorCallback());
  pService->start();  // <-- this is critical, service must be started before advertising

  // Advertising block stays exactly as-is
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLEUUID((uint16_t)0xBA55));
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();

  startTime = millis(); // Initialize with correct time reference
  //////////////////////////////////////////////////
  // Serial.println("BLE advertising done"); // ******
  // blinkLED(2, 200); // ****** 2 blinks = audio.begin() completed
  // 
  // Try to automatically reconnect to previously connected Bluetooth device if possible
  // audio.reconnect();
  // Serial.println("audio.reconnect() done"); // ******
  // blinkLED(3, 200); // ****** 3 blinks = reconnect attempted
  // 
  // blinkLED(4, 200); // ****** 4 blinks = I2S configured
  // 
  // // Set our custom sink callback; this will pass audio data to I2S but also store the
  // // data for FFT
  //*** */ audio.setSinkCallback(sinkCallback);
  Serial.println("=== Setup complete ==="); // ******
  blinkLED(3, 200); // ****** 5 blinks = fully initialized
// TO HERE ***

  Serial.println("****************************************"); // ******
  Serial.println(ESP.getChipModel());
  Serial.println("****************************************"); // ******
}
void i2sbullshit(){
  // Configure I2S for audio playback
  // Initialize to zeros first to prevent stack smashing
  i2s_config_t i2s_config;
  memset(&i2s_config, 0, sizeof(i2s_config));

  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_config.sample_rate = SAMPLING_FREQUENCY;
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; // mono
  // i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S);
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 4;//***8;
  i2s_config.dma_buf_len = 32;//***64;
  i2s_config.use_apll = false;

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);

  i2s_pin_config_t pin_config;
  memset(&pin_config, 0, sizeof(pin_config));
  pin_config.bck_io_num = I2S_BCLK_PIN;
  pin_config.ws_io_num = I2S_WS_PIN;
  pin_config.data_out_num = I2S_DOUT_PIN;
  pin_config.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_set_pin(I2S_NUM_0, &pin_config);
  //*** */ audio.I2S(I2S_BCLK_PIN, I2S_DOUT_PIN, I2S_WS_PIN);
  Serial.println("I2S done"); // ******
}
uint8_t i=0;
//////////////////////////////////////////////////////////////
// Main program loop. Runs on core 1, calculates the FFT of the latest audio sample and
// moves the fish motors accordingly.
void loop(){
  // if (i<5){
  //   digitalWrite(HEADs_or_TAILs, LOW);
  //   // Relay Off
  //   digitalWrite(LED_PIN, LOW);
  //   digitalWrite(HEADTAIL_MOTOR_PIN_1, HIGH); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   digitalWrite(MOUTH_MOTOR_PIN_1, HIGH); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   delay(3000);
  //   // blinkLED(1, 200);
  //   digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   digitalWrite(MOUTH_MOTOR_PIN_1, LOW); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   delay(3000);
  //   // Relay ON
  //   digitalWrite(LED_PIN, HIGH);
  //   digitalWrite(HEADs_or_TAILs, HIGH);
  //   digitalWrite(HEADTAIL_MOTOR_PIN_1, HIGH); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   digitalWrite(MOUTH_MOTOR_PIN_1, HIGH); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   delay(3000);
  //   digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   digitalWrite(MOUTH_MOTOR_PIN_1, LOW); //  relay OFF --> Head Motor  |  relay ON --> Tail Motor
  //   i++;
  //   delay(3000);
  // }
// Trigger playback when data is fully received
  if (packetReady && !playbackStarted) {
    playbackStarted = true;
    isPlayingAudio = true;
    startTime = millis(); // Set the start time RIGHT NOW
    
    // Launch audio on Core 0 (System Core)
    xTaskCreatePinnedToCore([](void*){
      playAudioFromSD();
      vTaskDelete(NULL);
    }, "audioTask", 8192, NULL, 1, NULL, 0);
  }

  if (playbackStarted) {
    uint32_t now = millis() - startTime;
    MotorEvent e;

    // Check if the next event in the queue is ready to trigger
    while (queueHead != queueTail) {
      if (now >= eventQueue[queueHead].t) {
        popEvent(e); // Pull it out of the list
        
        // Execute movement
        if (e.motor == 0) (e.state == 1) ? mouthOpen() : mouthClose();
        else if (e.motor == 1) (e.state == 1) ? headOut() : headBack();
        else if (e.motor == 2) (e.state == 1) ? tailOut() : tailBack();
      } else {
        break; // Next event isn't ready yet
      }
    }

    // Clean up when done
    if (!isPlayingAudio && queueHead == queueTail) {
      playbackStarted = false;
      packetReady = false;
      Serial.println("Fish is resting.");
    }
  }

  // // FROM HERE ***
  // //  ****** CHECK IF CUSTOM THING FROM WEBPAGE ****** //
  // if (packetReady && !playbackStarted) {
  //   playbackStarted = true;
  //   // useBLE = true;
  //   isPlayingAudio = true;
  // // 
  //   Serial.println("Playback started!");
  //         // Start audio in background (simple version = blocking)
  //         // playAudioFromSD();
  //   xTaskCreatePinnedToCore([](void*){
  //     playAudioFromSD();
  //     vTaskDelete(NULL);
  //   }, "audioTask", 8192, NULL, 1, NULL, 0 );
  // }
  // // 
  // if (playbackStarted){
  //   MotorEvent e;
  //   uint32_t now = millis() - startTime;
  // // 
  //     while (queueHead != queueTail) {
  //     MotorEvent &e = eventQueue[queueHead];
  //     if (now >= e.t) {
  //         queueHead = (queueHead + 1) % MAX_EVENTS;
  //         // Serial.printf("Motor %d state %d\n", e.motor, e.state);
  // // 
  //         if (e.motor == 0){    // Mouth
  //           (e.state == 1) ? mouthOpen() : mouthRest();
  //         } else if (e.motor == 1){ // Head
  //           (e.state == 1) ? headOut() : headBack();
  //         } else if (e.motor == 2){ // Tail
  //           (e.state == 1) ? tailOut() : tailBack();
  //         }
  // // 
  //       } else {
  //         break;
  //       }
  //     }
  //   }

    // // Exit BLE mode if no events left AND timeout passed
    // if (queueHead == queueTail && (millis() - lastBLEtime > BLE_TIMEOUT_MS)) {
    //   useBLE = false;
    // }
    // delay(5);
    // return; // Skips the FFT stuff
    // }
    // 
    //  Detect when everything is done
    // if (playbackStarted && !isPlayingAudio && queueHead == queueTail) {
    //   Serial.println("Playback complete!");
    //   playbackStarted = false;
    //   packetReady = false;
    //   // 
    //   // Reset receive state for next transfer
    //   expectedChunks = 0;
    //   receivedChunks = 0;
    //   totalBytesWritten = 0;
    //   headerParsed = false;
    //   headerComplete = false;
    //   headerBufLen = 0;
    //   numEvents = 0;
    //   // 
    //   // Restart advertising so webpage can send again
    //   BLEDevice::startAdvertising();
    //   Serial.println("Ready for next transfer");
    // }
  // TO HERE ***
  // //////      GIT REPO CODE --> USES FFT     //////
  // // Copy data out of the inter-process audio transfer buffer into the "real" data buffer
  // // that will be used for the FFT. This is controlled with a mutex lock to ensure we
  // // don't read from it and write to it at the same time.
  // xSemaphoreTake(mutex, portMAX_DELAY);
  // for (int i = 0; i < SAMPLES_PER_FFT; i++)
  // {
  //   vReal[i] = audioTransferBuffer[i];
  // }
  // xSemaphoreGive(mutex);
  // 
  // // Zero out the imaginary data buffer, leaving only the real data buffer written to
  // // by the bluetooth-to-I2S callback
  // for (int i = 0; i < SAMPLES_PER_FFT; i++)
  // {
  //   vImag[i] = 0.0;
  // }
  // 
  // // Compute the FFT. Data will be stored in vReal
  // fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  // fft.compute(FFTDirection::Forward);
  // fft.complexToMagnitude();
  // 
  // // Only first half of vReal contains FFT bins, per Nyquist
  // uint16_t numBins = SAMPLES_PER_FFT >> 1;
  // 
  // // Do we have some power in the FFT in the required band? Iterate through the FFT bins,
  // // finding ones that are within the band we are looking for, and if any power is present,
  // // report that we are receiving audio.
  // boolean receivingAudio = false;
  // for (uint16_t i = 0; i < numBins; i++)
  // {
  //   double freq = ((i * 1.0 * SAMPLING_FREQUENCY) / SAMPLES_PER_FFT);
  //   double power = vReal[i];
  //   if (freq >= VOICE_MIN_FREQ_HZ && freq <= VOICE_MAX_FREQ_HZ && power > 0.0) {
  //     receivingAudio = true;
  //     digitalWrite(I2S_SD_PIN, HIGH);  //  ******
  //     break;
  //   }
  // }
  // 
  // // If we are receiving audio, stick the fish head out and flap the mouth.
  // // If not, then rest both motors.
  // if (receivingAudio)
  // {
  //   headOut();
  //   flapMouth();
  // }
  // else
  // {
  //   headTailRest();
  //   mouthRest();
  // }
  // 
  // // Short delay to give FreeRTOS some breathing space to not trigger the watchdog
  // delay(10);
}

// // Callback for processing an audio data buffer arrived from Bluetooth. Sends the
// // data to I2S but also populates a second buffer for computing the FFT.
// void sinkCallback(const uint8_t *data, uint32_t len)
// {
//   int samplesReceived = len / 2;
//   // Handle input as 16-bit data
//   int16_t* data16 = (int16_t *)data;
// 
//   // Write to I2S
//   size_t i2s_bytes_write = 0;
//   for (int i = 0; i < samplesReceived; i++)
//   {
//     i2s_write(I2S_NUM_0, data16, 2, &i2s_bytes_write, 10);
//     data16++;
//   }
// 
//   // Reset data pointer
//   data16 = (int16_t *)data;
// 
//   // Write to the audio transfer buffer to pass this data over to the FFT task on
//   // the other CPU. This is controlled with a mutex lock to ensure we don't read
//   // from it and write to it at the same time.
//   xSemaphoreTake(mutex, portMAX_DELAY);
//   for (int i = 0; i < min(samplesReceived, SAMPLES_PER_FFT); i++)
//   {
//     audioTransferBuffer[i] = *data16;
//     data16++;
//   }
//   xSemaphoreGive(mutex);
// }

// Bring the fish's head out
void headOut(){
  // digitalWrite(HEADs_or_TAILs, LOW); // Turn relay OFF --> Head Motor
  digitalWrite(HEADTAIL_MOTOR_PIN_1, HIGH);
  // digitalWrite(HEADTAIL_MOTOR_PIN_2, LOW);
}
void headBack(){
  // digitalWrite(HEADs_or_TAILs, LOW); // Turn relay OFF --> Head Motor
  digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW);
  // digitalWrite(HEADTAIL_MOTOR_PIN_2, LOW);
}

void tailOut(){
  // digitalWrite(HEADs_or_TAILs, HIGH); // Turn relay ON --> Tail Motor
  // digitalWrite(HEADTAIL_MOTOR_PIN_1, HIGH);
  digitalWrite(HEADTAIL_MOTOR_PIN_2, HIGH);
}
void tailBack(){
  // digitalWrite(HEADs_or_TAILs, HIGH); // Turn relay ON --> Tail Motor
  // digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW);
  digitalWrite(HEADTAIL_MOTOR_PIN_2, LOW);
}

// Put the fish head and tail back to the neutral position
void headTailRest() //    ***     NECESSARY???      ***
{
  digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW);
  digitalWrite(HEADTAIL_MOTOR_PIN_2, LOW);
}

// Flap the fish's mouth once at the required rate
void flapMouth()
{
  mouthOpen();
  delay(MOUTH_FLAP_INTERVAL_MILLIS);
  mouthClose();
  delay(MOUTH_FLAP_INTERVAL_MILLIS);
}

// Open the fish's mouth
void mouthOpen()
{
  digitalWrite(MOUTH_MOTOR_PIN_1, HIGH);
  digitalWrite(MOUTH_MOTOR_PIN_2, LOW);
  // digitalWrite(LED_PIN, HIGH); // Debug
}

// Close the fish's mouth
void mouthClose()  //    ***     NECESSARY???      ***
{
  digitalWrite(MOUTH_MOTOR_PIN_1, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_2, HIGH);
  // digitalWrite(LED_PIN, LOW); // Debug
}

// Rest the fish's mouth
void mouthRest()
{
  digitalWrite(MOUTH_MOTOR_PIN_1, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_2, LOW);
  // digitalWrite(LED_PIN, LOW); // Debug
}