// Billy Bass Sat Nav / "BLE Bass" control code
// by Ian Renton, 2024. CC Zero / Public Domain
// Based on examples from https://github.com/tierneytim/btAudio & https://github.com/kosme/arduinoFFT
// Libraries used with many thanks.

// Includes
#include <Arduino.h>
#include <arduinoFFT.h>
// Setup Bluetooth stuff
#include <btAudio.h>    // For receiving AUDIO only

#include <BLEDevice.h>  // For receiving motor data
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLECharacteristic.h>

// Bluetooth device name
#define DEVICE_NAME "Billy Bass"
#define SERVICE_UUID "B160BA55-AAAA-0117-3650-005006019920"
#define CHARACTERISTIC_UUID "0ABC1230-0021-0021-0021-333444455555"


// Timing
#define MOUTH_FLAP_INTERVAL_MILLIS 100 // Rate of mouth movement. Lower number = faster
#define SERIAL_BAUD 115200

// FFT parameters. Hopefully these match the sampling frequency and number of samples per packet
// coming from your device. This is what works with my phone!
#define SAMPLING_FREQUENCY 44100
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
#define HEADTAIL_MOTOR_PIN_1 4//12  <-- Motor B
#define HEADTAIL_MOTOR_PIN_2 17//14
#define HEADTAIL_MOTOR_PWM_PIN 13//13
#define MOUTH_MOTOR_PIN_1 33//27    <-- Motor A
#define MOUTH_MOTOR_PIN_2 25//26
#define MOUTH_MOTOR_PWM_PIN 32//25
#define MOTOTR_STBY_PIN 14 //

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

// Function defs
void setup();
void loop();
void sinkCallback(const uint8_t *data, uint32_t len);
void calcFFT(void *pvParameters);
void headOut();
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

// Create Bluetooth audio receiver
btAudio audio = btAudio(DEVICE_NAME);
// Called whenever a BLE client writes to the characteristic
class MotorCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    uint8_t* data = (uint8_t*)value.data();
    size_t len = value.length();

    if (len < 6) return; // too small to be a valid packet

    // Parse header
    uint16_t numEvents = data[0] | (data[1] << 8);
    uint32_t audioSize = data[2] | (data[3] << 8) | (data[4] << 16) | (data[5] << 24);
    (void) audioSize; // ****** used after chunk reassembly is implemented
    
    // Parse events (each is 7 bytes: 4t + 1motor + 1state + 1pwm)
    int offset = 6;
    for (int i = 0; i < numEvents; i++) {
      if (offset + 7 > len) break;

      uint32_t t     = data[offset]     | (data[offset+1] << 8)
                      | (data[offset+2] << 16) | (data[offset+3] << 24);
      uint8_t motor  = data[offset + 4];
      uint8_t state  = data[offset + 5];
      uint8_t pwm    = data[offset + 6];
      offset += 7;

      // TODO: queue or handle motor event here
      Serial.printf("t=%u motor=%u state=%u pwm=%u\n", t, motor, state, pwm);
    }
  }
};

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
  delay(500); // Gap between blink groups
}

// Setup and run the program
void setup()
{
  // Set up serial
  Serial.begin(SERIAL_BAUD);
  Serial.println("=== Boot start ==="); // ******

  // Set up debug LED
  pinMode(LED_PIN, OUTPUT);
  blinkLED(1, 200); // ****** 1 blink = boot started

  pinMode(I2S_SD_PIN, OUTPUT); // ******
  digitalWrite(I2S_SD_PIN, LOW); // ******
  // Set up motor control pins
  pinMode(HEADTAIL_MOTOR_PIN_1, OUTPUT);
  pinMode(HEADTAIL_MOTOR_PIN_2, OUTPUT);
  pinMode(HEADTAIL_MOTOR_PWM_PIN, OUTPUT);
  pinMode(MOUTH_MOTOR_PIN_1, OUTPUT);
  pinMode(MOUTH_MOTOR_PIN_2, OUTPUT);
  pinMode(MOUTH_MOTOR_PWM_PIN, OUTPUT);

  // Set up PWM
  ledcSetup(HEADTAIL_MOTOR_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcSetup(MOUTH_MOTOR_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(HEADTAIL_MOTOR_PWM_PIN, HEADTAIL_MOTOR_PWM_CHANNEL);
  ledcAttachPin(MOUTH_MOTOR_PWM_PIN, MOUTH_MOTOR_PWM_CHANNEL);
  ledcWrite(HEADTAIL_MOTOR_PWM_CHANNEL, HEADTAIL_MOTOR_PWM_DUTY_CYCLE);
  ledcWrite(MOUTH_MOTOR_PWM_CHANNEL, MOUTH_MOTOR_PWM_DUTY_CYCLE);

  // Advertise Bluetooth device
  audio.begin();
  Serial.println("audio.begin() done"); // ******
  // Start BLE for motor data
  BLEDevice::init(DEVICE_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristic->setCallbacks(new MotorCallback());
  pService->start();
  BLEDevice::startAdvertising();
  Serial.println("audio.begin() done"); // ******
  blinkLED(2, 200); // ****** 2 blinks = audio.begin() completed

  // Try to automatically reconnect to previously connected Bluetooth device if possible
  audio.reconnect();
  Serial.println("audio.reconnect() done"); // ******
  blinkLED(3, 200); // ****** 3 blinks = reconnect attempted

  // Set up streaming audio over I2S
  audio.I2S(I2S_BCLK_PIN, I2S_DOUT_PIN, I2S_WS_PIN);
  Serial.println("I2S done"); // ******
  blinkLED(4, 200); // ****** 4 blinks = I2S configured

  // Set our custom sink callback; this will pass audio data to I2S but also store the
  // data for FFT
  audio.setSinkCallback(sinkCallback);
  Serial.println("=== Setup complete ==="); // ******
  blinkLED(5, 200); // ****** 5 blinks = fully initialized
}
//////////////////////////////////////////////////////////////
// void playCustom(Event e){
//   if (e.motor == "mouth"){
//     digitalWrite()
//   }
// }
//////////////////////////////////////////////////////////////
// Main program loop. Runs on core 1, calculates the FFT of the latest audio sample and
// moves the fish motors accordingly.
void loop(){
 
  //  ****** CHECK IF CUSTOM THING FROM WEBPAGE ****** //
  //  ****** CHECK IF CUSTOM THING FROM WEBPAGE ****** //
  //  ****** CHECK IF CUSTOM THING FROM WEBPAGE ****** //

  // Copy data out of the inter-process audio transfer buffer into the "real" data buffer
  // that will be used for the FFT. This is controlled with a mutex lock to ensure we
  // don't read from it and write to it at the same time.
  xSemaphoreTake(mutex, portMAX_DELAY);
  for (int i = 0; i < SAMPLES_PER_FFT; i++)
  {
    vReal[i] = audioTransferBuffer[i];
  }
  xSemaphoreGive(mutex);

  // Zero out the imaginary data buffer, leaving only the real data buffer written to
  // by the bluetooth-to-I2S callback
  for (int i = 0; i < SAMPLES_PER_FFT; i++)
  {
    vImag[i] = 0.0;
  }

  // Compute the FFT. Data will be stored in vReal
  fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();

  // Only first half of vReal contains FFT bins, per Nyquist
  uint16_t numBins = SAMPLES_PER_FFT >> 1;

  // Do we have some power in the FFT in the required band? Iterate through the FFT bins,
  // finding ones that are within the band we are looking for, and if any power is present,
  // report that we are receiving audio.
  boolean receivingAudio = false;
  for (uint16_t i = 0; i < numBins; i++)
  {
    double freq = ((i * 1.0 * SAMPLING_FREQUENCY) / SAMPLES_PER_FFT);
    double power = vReal[i];
    if (freq >= VOICE_MIN_FREQ_HZ && freq <= VOICE_MAX_FREQ_HZ && power > 0.0) {
      receivingAudio = true;
      digitalWrite(I2S_SD_PIN, HIGH);  //  ******
      break;
    }
  }

  // If we are receiving audio, stick the fish head out and flap the mouth.
  // If not, then rest both motors.
  if (receivingAudio)
  {
    headOut();
    flapMouth();
  }
  else
  {
    headTailRest();
    mouthRest();
  }

  // Short delay to give FreeRTOS some breathing space to not trigger the watchdog
  delay(10);
}

// Callback for processing an audio data buffer arrived from Bluetooth. Sends the
// data to I2S but also populates a second buffer for computing the FFT.
void sinkCallback(const uint8_t *data, uint32_t len)
{
  int samplesReceived = len / 2;
  // Handle input as 16-bit data
  int16_t* data16 = (int16_t *)data;

  // Write to I2S
  size_t i2s_bytes_write = 0;
  for (int i = 0; i < samplesReceived; i++)
  {
    i2s_write(I2S_NUM_0, data16, 2, &i2s_bytes_write, 10);
    data16++;
  }

  // Reset data pointer
  data16 = (int16_t *)data;

  // Write to the audio transfer buffer to pass this data over to the FFT task on
  // the other CPU. This is controlled with a mutex lock to ensure we don't read
  // from it and write to it at the same time.
  xSemaphoreTake(mutex, portMAX_DELAY);
  for (int i = 0; i < min(samplesReceived, SAMPLES_PER_FFT); i++)
  {
    audioTransferBuffer[i] = *data16;
    data16++;
  }
  xSemaphoreGive(mutex);
}

// Bring the fish's head out
void headOut()
{
  digitalWrite(HEADTAIL_MOTOR_PIN_1, LOW);
  digitalWrite(HEADTAIL_MOTOR_PIN_2, HIGH);
}

// Put the fish head and tail back to the neutral position
void headTailRest()
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
  digitalWrite(MOUTH_MOTOR_PIN_1, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_2, HIGH);
  digitalWrite(LED_PIN, HIGH); // Debug
}

// Close the fish's mouth
void mouthClose()  //    ***     NECESSARY???      ***
{
  digitalWrite(MOUTH_MOTOR_PIN_1, HIGH);
  digitalWrite(MOUTH_MOTOR_PIN_2, LOW);
  digitalWrite(LED_PIN, LOW); // Debug
}

// Rest the fish's mouth
void mouthRest()
{
  digitalWrite(MOUTH_MOTOR_PIN_1, LOW);
  digitalWrite(MOUTH_MOTOR_PIN_2, LOW);
  digitalWrite(LED_PIN, LOW); // Debug
}