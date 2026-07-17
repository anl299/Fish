// ???????????????????????????????????????????????????? //
// 
// Why store audio on SD and motor events in RAM?
//      Can they both be in SD? 
//      Can they both be 'chunked' into RAM and played from there? 
// 
//  Need to ENFORCE Audio from webpage is:
//            16-bit PCM  <-- ?
//            44.1kHz   <-- Maybe change later?
//            mono
// 
//  MAX_EVENTS --> Should it be higher? How much higher can it be?
// 
//  "uint8_t type = data[0]" is unused in MotorCallback()
//        Do I need it?
// 
//
//
// On the file size — you're right, and I need to walk back part of what I said. "Play from PSRAM, skip SD" only made sense for a 10-second clip. For full songs at 10MB+, you can't hold the file in PSRAM at all (you've got single-digit MB, and the addressable chunk is smaller still), so SD is the correct store, and your existing playAudioFromSD streaming-from-SD-to-I2S already scales to any length. So the plan is: stream the upload onto the SD card fast, then stream it back off during playback. Your instinct for how to do the "fast" part — accumulate in PSRAM, flush a big block, repeat — is exactly the right move, and it's the standard pattern.
// The number that matters is the flush size, and it's smaller than you'd think: SD cards hit their efficient stride around 16–64KB writes, with diminishing returns above that. So 32KB is a great threshold — it's 64 aligned sectors, it turns ~23 of those tiny TCP chunks into one efficient write, and it's microscopic next to your PSRAM, so your "leave headroom" instinct is satisfied automatically (you're using well under 1%). Here's the change to your upload callback; note it's totally transparent to the file contents — header, events, and audio still land on the card in the same order, so your parser and playback don't change at all:
// Here's the honest caveat, though, so you're not surprised: each flushBuf() still runs inside the AsyncTCP callback, so during that one 32KB write (a handful of milliseconds) the connection still stalls. You've cut the number of stalls by ~20×, which is a huge win, but you haven't eliminated them. For a 10-second clip you won't care. For a 4-minute song you might. The fully robust version — and the proper answer to "+10MB files" — is double-buffering: the callback only ever memcpys into buffer A, and when A fills you hand it to a separate writer task pinned to the other core that writes to SD while the callback keeps filling buffer B. That way the TCP task never blocks on SD at all and you get near-wire-speed regardless of file size. It's maybe 40 more lines and a queue. I'd start with the simple version above, measure your 4-minute case, and only reach for the double-buffer if the stalls actually bother you — no point in the complexity until you've confirmed you need it. Say the word if you want me to write that version.
// And to check your PSRAM at runtime rather than guessing: ESP.getPsramSize() and ESP.getFreePsram(). WROVER modules ship with 4 or 8MB depending on the exact part, but for a 32KB buffer it genuinely doesn't matter which you have.
//
// 
//////      IMPORTANT CHANGE      //////
//    live playback in sinkCallback()  DISABLED
//    Can ONLY be used later if I want fallback
// 
// 
// ???????????????????????????????????????????????????? //

// Start mDNS -> Access via http://billybass.local

// Billy Bass Sat Nav / "BLE Bass" control code
// by Ian Renton, 2024. CC Zero / Public Domain
// Based on examples from https://github.com/tierneytim/btAudio & https://github.com/kosme/arduinoFFT
// Libraries used with many thanks.

//////      INCLUDES      //////
// Basic setup
#include <Arduino.h>
//  Wi-Fi shit
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
//  Stuff for MicroSD card reader
#include <SPI.h>
#include <SD.h>
// Native I2S driver to send audio from SD card to amplifier
#include <driver/i2s.h>

// --- Wi-Fi Credentials ---
const char* ssid = "slnetX";
const char* password = "4Rk%E_i36&e"; //  <-- WIFI_PASSWORD

// --- Web Server ---
AsyncWebServer server(80);

// Timing & Audio
#define SERIAL_BAUD 115200
#define SAMPLING_FREQUENCY 16000 // 44100
// #define SAMPLES_PER_FFT 2048

//  AMPLIFIER  I2S pins
#define I2S_WS_PIN 21     // <-- Connects to amp "LRC"/"LRCLK" pin
#define I2S_BCLK_PIN 26   // 
#define I2S_DOUT_PIN 22   // <-- Connects to amp "DIN" pin
#define I2S_SD_PIN  27 // amp Shut Down pin. HIGH=on, LOW=off

//  MOTOR  control pins
//  H-Bridge output pins: Aout1, Aou2, GND, GND, Bout2, Bout1  //
#define HEADTAIL_MOTOR_PWM_PIN 13
#define HEADTAIL_MOTOR_PIN_2 15 // 17 in PCB --> GPIO 16/17 PSRAM on WROVER model --> UNUSABLE
#define HEADTAIL_MOTOR_PIN_1 4
#define MOTOTR_STBY_PIN 14 //  Currently unused
#define MOUTH_MOTOR_PIN_1 25// Flipped in PCB? *Fixed in V2*****************************************
#define MOUTH_MOTOR_PIN_2 33// Flipped in PCB? *Fixed in V2*****************************************
#define MOUTH_MOTOR_PWM_PIN 32
// #define HEADs_or_TAILs 16  //  Currently unused   <-- Not needed

// SD CARD READER
#define MicroSD_READER_CS 5 //
#define MicroSD_READER_DI 23 //
#define MicroSD_READER_DO 19 //
#define MicroSD_READER_CLK 18 //
// #define MicroSD_READER_CD  //  Currently unused

// Misc
#define LED_PIN 2
#define PWM_FREQUENCY 1000
#define PWM_RESOLUTION 8
#define HEADTAIL_MOTOR_PWM_CHANNEL 0
#define MOUTH_MOTOR_PWM_CHANNEL 1
#define HEADTAIL_MOTOR_PWM_DUTY_CYCLE 255 // Proxy for motor speed, up to 2^resolution
#define MOUTH_MOTOR_PWM_DUTY_CYCLE 255    // Proxy for motor speed, up to 2^resolution

////////////////////////////////////////////////
volatile bool cardReaderWorks = false;
////////////////////////////////////////////////
// --- State Variables ---
File audioFile;
volatile bool packetReady = false;
volatile bool playbackStarted = false;
volatile bool isPlayingAudio = false;
#define AUDIO_FILE_PATH "/audio.raw"
#define AUDIO_BUFFER_SIZE 4096
uint8_t audioBuffer[AUDIO_BUFFER_SIZE];
static uint32_t startTime = 0;

// Motor event struct
struct MotorEvent {
    uint32_t t;
    uint8_t  motor;
    uint8_t  state;
    uint8_t  pwm;
};

// --- Dynamic Queue (Uses PSRAM) ---
MotorEvent* eventQueue = nullptr; 
uint16_t numEvents = 0;
volatile int currentEventIndex = 0;

// Function defs
void setup();
void i2sbullshit();
void loop();
void playAudioFromSD();
bool parsePayloadFile();
void blinkLED(int times, int delayMs);

void headOut();
void tailOut();
void headBack();
void tailBack();
void headTailRest();
void flapMouth();
void mouthOpen();
void mouthClose();
void mouthRest();

void playAudioFromSD() {
    audioFile = SD.open(AUDIO_FILE_PATH);
    if (!audioFile) {
        Serial.println("Failed to open audio file for reading");
        isPlayingAudio = false;
        return;
    }

    Serial.println("Starting audio playback...");
    digitalWrite(I2S_SD_PIN, HIGH); // Amp ON

    // Seek past the header to where the raw audio starts
    size_t headerSize = 6 + (numEvents * 7);
    audioFile.seek(headerSize);
    
    // --- new ---
    while (audioFile.available()) {
      int bytesRead = audioFile.read(audioBuffer, AUDIO_BUFFER_SIZE);
      size_t bytesWritten = 0;
      i2s_write(I2S_NUM_0, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
    } // --- end ---
    // while (audioFile.available()) {
    //     int bytesRead = audioFile.read(audioBuffer, AUDIO_BUFFER_SIZE);
    //     size_t bytesWritten = 0;
    //     i2s_write(I2S_NUM_0, audioBuffer, bytesRead, &bytesWritten, portMAX_DELAY);
    //     vTaskDelay(1); 
    // }

    audioFile.close();
    digitalWrite(I2S_SD_PIN, LOW); // Amp OFF
    Serial.println("Audio playback finished");
    isPlayingAudio = false;
}

// Parses the file saved on the SD card and loads motor data into PSRAM
bool parsePayloadFile() {
  File file = SD.open(AUDIO_FILE_PATH, FILE_READ);
  if (!file) return false;

  uint8_t header[6];
  file.read(header, 6);
  numEvents = header[0] | (header[1] << 8);

  Serial.printf("Loading %d events into PSRAM...\n", numEvents);

  // Free old memory if it exists, allocate new memory
  if (eventQueue != nullptr) {
    free(eventQueue);
  }

  // Allocate exact array size in PSRAM!
  // eventQueue = (MotorEvent*)ps_malloc(sizeof(MotorEvent) * numEvents);
  if (psramFound()) { //  Aimed for ESP32 with WROVER
    eventQueue = (MotorEvent*)ps_malloc(sizeof(MotorEvent) * numEvents);
  } else {  //  Adjustment for ESP32-type-C WROOM
    Serial.println("No PSRAM. Falling back to internal RAM...");
    eventQueue = (MotorEvent*)malloc(sizeof(MotorEvent) * numEvents);
  }

  if(eventQueue == nullptr) {
    Serial.println("PSRAM MALLOC FAILED! Out of memory.");
    file.close();
    return false;
  }

  uint8_t evBuf[7];
  for (int i = 0; i < numEvents; i++) {
    file.read(evBuf, 7);
    eventQueue[i].t = evBuf[0] | (evBuf[1] << 8) | (evBuf[2] << 16) | (evBuf[3] << 24);
    eventQueue[i].motor = evBuf[4];
    eventQueue[i].state = evBuf[5];
    eventQueue[i].pwm = evBuf[6];
  }
  
  file.close();
  currentEventIndex = 0;
  return true;
}

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


// Setup and run the program
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println("BUILD MARKER 001");
  delay(100);
  Serial.println("=== Boot start ===");

  if(psramInit()) Serial.println("PSRAM detected! Billy has a big brain now.");
  else Serial.println("PSRAM NOT FOUND.");

  pinMode(LED_PIN, OUTPUT);
  pinMode(I2S_SD_PIN, OUTPUT);
  digitalWrite(I2S_SD_PIN, LOW);

  // Motor setup...
  ledcSetup(HEADTAIL_MOTOR_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(MOUTH_MOTOR_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(HEADTAIL_MOTOR_PWM_PIN, HEADTAIL_MOTOR_PWM_CHANNEL);
  ledcAttachPin(MOUTH_MOTOR_PWM_PIN, MOUTH_MOTOR_PWM_CHANNEL);

  ledcWrite(HEADTAIL_MOTOR_PWM_CHANNEL, HEADTAIL_MOTOR_PWM_DUTY_CYCLE);
  ledcWrite(MOUTH_MOTOR_PWM_CHANNEL, MOUTH_MOTOR_PWM_DUTY_CYCLE);

  // Set up motor control pins
  pinMode(MOTOTR_STBY_PIN, OUTPUT);
  // pinMode(MOUTH_MOTOR_PWM_PIN, OUTPUT);
  // pinMode(HEADTAIL_MOTOR_PWM_PIN, OUTPUT);
  pinMode(MOUTH_MOTOR_PIN_1, OUTPUT);
  pinMode(MOUTH_MOTOR_PIN_2, OUTPUT);
  pinMode(HEADTAIL_MOTOR_PIN_1, OUTPUT);
  pinMode(HEADTAIL_MOTOR_PIN_2, OUTPUT);
  digitalWrite(MOTOTR_STBY_PIN, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_1, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_2, LOW);
  digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW);
  digitalWrite(HEADTAIL_MOTOR_PIN_2, LOW);

  i2sbullshit();

  // SD Setup
  SPI.begin(MicroSD_READER_CLK, MicroSD_READER_DO, MicroSD_READER_DI, MicroSD_READER_CS);
  if (!SD.begin(MicroSD_READER_CS, SPI, 20000000)) {
    Serial.println("SD init failed! (Check your wiring/formatting)");
  } else {
    Serial.println("SD init success");
    cardReaderWorks = true;/////////// *********************************************************
  }

  // Initialize LittleFS for the Web Interface
  if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
    return;
  }

  blinkLED(1, 200);
  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  // WiFi.setTxPower(WIFI_POWER_11dBm);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Start mDNS -> Access via http://billybass.local
  if (!MDNS.begin("billybass")) {
    Serial.println("Error setting up MDNS responder!");
  }
  blinkLED(2, 200);

// Allow CORS so the UI can be hosted anywhere
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "*");
  // Catch the "OPTIONS" preflight request browsers send
  server.on("/upload", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });

  // Route to load index.html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  // Route to load css
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });
  // Route to load js
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/main.js", "text/javascript");
  });

  // This tells the ESP32: "If someone asks for a URL, look in LittleFS for a file with that exact name/path."
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // The Data Upload Endpoint   <--   Upload Callback?
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Data Received");
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {

    // --- new ---
    #define FLUSH_THRESHOLD (32 * 1024)
    static uint8_t* sdBuf = nullptr;
    static size_t   sdBufLen = 0;
    static File     uploadFile;

    if (!index) {
      Serial.printf("UploadStart: %s\n", filename.c_str());
      if (SD.exists(AUDIO_FILE_PATH)) SD.remove(AUDIO_FILE_PATH);
      uploadFile = SD.open(AUDIO_FILE_PATH, FILE_WRITE);
      if (!sdBuf) sdBuf = (uint8_t*) ps_malloc(FLUSH_THRESHOLD);
      sdBufLen = 0;
    }

    if (sdBuf && uploadFile) {
      if (sdBufLen + len > FLUSH_THRESHOLD) {   // flush before overflow
        uploadFile.write(sdBuf, sdBufLen);
        sdBufLen = 0;
      }
      memcpy(sdBuf + sdBufLen, data, len);
      sdBufLen += len;
    }

    if (final) {
      if (uploadFile) {
        if (sdBufLen) uploadFile.write(sdBuf, sdBufLen);  // final partial block
        uploadFile.close();
      }
      Serial.printf("UploadEnd: %s, %u B\n", filename.c_str(), index + len);
      if (parsePayloadFile()) packetReady = true;
    } // --- end ---
    // static File uploadFile;
    // if (!index) {
    //   Serial.printf("UploadStart: %s\n", filename.c_str());
    //   if (SD.exists(AUDIO_FILE_PATH)) SD.remove(AUDIO_FILE_PATH);
    //   uploadFile = SD.open(AUDIO_FILE_PATH, FILE_WRITE);
    // }
    //
    // if (uploadFile) {
    //   uploadFile.write(data, len);
    // }
    //
    // if (final) {
    //   Serial.printf("UploadEnd: %s, %u B\n", filename.c_str(), index + len);
    //   if (uploadFile) {
    //     uploadFile.close();
    //   }
    //   // Parse the newly saved file
    //   if (parsePayloadFile()) {
    //     packetReady = true; 
    //   }
    // }
  });
  blinkLED(3, 200);

  server.begin();
  Serial.println("=== Setup complete ===");
  
  digitalWrite(MOTOTR_STBY_PIN, HIGH);//******
}

void i2sbullshit() {  
  i2s_config_t i2s_config;
  memset(&i2s_config, 0, sizeof(i2s_config));
  i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2s_config.sample_rate = SAMPLING_FREQUENCY;
  // Serial.println(SAMPLING_FREQUENCY);
  i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; 
  i2s_config.communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S);
  i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  i2s_config.dma_buf_count = 8; //4;
  i2s_config.dma_buf_len = 1024; //32;
  i2s_config.use_apll = true;

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  // i2s_set_clk(I2S_NUM_0, SAMPLING_FREQUENCY, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  i2s_pin_config_t pin_config;
  memset(&pin_config, 0, sizeof(pin_config));
  pin_config.bck_io_num = I2S_BCLK_PIN;
  pin_config.ws_io_num = I2S_WS_PIN;
  pin_config.data_out_num = I2S_DOUT_PIN;
  pin_config.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(I2S_NUM_0, &pin_config);
  Serial.println("I2S done"); 
}

//////////////////////////////////////////////////////////////
// Main program loop. Runs on core 1, calculates the FFT of the latest audio sample and
// Moves the fish motors accordingly.
void loop(){
  // if (cardReaderWorks){ /////////// *********************************************************
  //   digitalWrite(MOUTH_MOTOR_PIN_1, HIGH);  //            Mouth ON
  //   delay(1000);
  //   digitalWrite(MOUTH_MOTOR_PIN_1, LOW);  //            Mouth OFF
  //   delay(1000);
  //
  // } else{
  //   blinkLED(1, 500);
  // }
  //
  // blinkLED(1, 500);
  // // delay(200);
  // digitalWrite(HEADTAIL_MOTOR_PIN_2, LOW); //  Tail OFF
  // digitalWrite(MOUTH_MOTOR_PIN_1, HIGH);  //            Mouth ON
  // delay(1000);
  // blinkLED(2, 500);
  // digitalWrite(MOUTH_MOTOR_PIN_1, LOW);   //  Mouth OFF
  // digitalWrite(HEADTAIL_MOTOR_PIN_1, HIGH); //          Head ON
  // delay(1000);
  // blinkLED(3, 500);
  // digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW); //  Head OFF
  // digitalWrite(HEADTAIL_MOTOR_PIN_2, HIGH); //          Tail ON
  // delay(1000);
  
  if (packetReady && !playbackStarted) {
    playbackStarted = true;
    isPlayingAudio = true;
    startTime = millis(); 
  
    xTaskCreatePinnedToCore([](void*){
      playAudioFromSD();
      vTaskDelete(NULL);
    }, "audioTask", 8192, NULL, 1, NULL, 0);
  }
  
  if (playbackStarted) {
    uint32_t now = millis() - startTime;
  
    // Loop through PSRAM array instead of a fixed queue
    while (currentEventIndex < numEvents) {
      MotorEvent e = eventQueue[currentEventIndex];
  
      if (now >= e.t) {
        if (e.motor == 0) (e.state == 1) ? mouthOpen() : mouthClose();
        else if (e.motor == 1) (e.state == 1) ? headOut() : headBack();
        else if (e.motor == 2) (e.state == 1) ? tailOut() : tailBack();
  
        currentEventIndex++; // Move to next event
      } else {
        break; // Not time for the next event yet
      }
    }
  
    if (!isPlayingAudio && currentEventIndex >= numEvents) {
      playbackStarted = false;
      packetReady = false;
      // Serial.println("!!! Elapsed time: ");
      // Serial.println((now - startTime) / 1000);
      Serial.println("Fish is resting.");
    }
  }
}

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