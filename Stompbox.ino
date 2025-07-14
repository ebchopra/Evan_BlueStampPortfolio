const long DEBOUNCE_DELAY_MS = 50;
int lastButtonState = 1;   // The previous reading from the input pin (HIGH because of INPUT_PULLUP)
long lastDebounceTime = 0;    // The last time the output pin was toggled
bool buttonPressedFlag = false; // Flag to indicate a debounced press has occurred

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>
#include <Bounce2.h>
#include <Arduino.h>
#include <U8g2lib.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

AudioControlSGTL5000     sgtl5000_1;     //xy=264,423
AudioInputI2S            i2s1;
AudioOutputI2S           i2s2;
AudioMixer4              mixer1;         //xy=353,208
AudioMixer4              mixer2;         //xy=353,208
AudioEffectFade          fade1;          //xy=232,190
//AudioEffectChorus        chorus1;        //xy=308,131 // Chorus commented out, ensure buffer is also commented out if not used
AudioEffectDelay         delay1;         //xy=510,206
AudioAnalyzeNoteFrequency notefreq1;      //xy=715,294
AudioSynthKarplusStrong  string1;        //xy=384,494
AudioAnalyzePeak         peak1;          //xy=451,348
AudioAnalyzePeak         peak2;          //xy=451,348

AudioConnection          patchCord1(i2s1, 0, mixer1, 0);
AudioConnection          patchCord9(i2s1, 0, peak1, 0);
AudioConnection          patchCord2(i2s1, 0, notefreq1, 0);
AudioConnection          patchCord3(string1, 0, mixer1, 1);
AudioConnection          patchCord4(mixer1, 0, fade1, 0);
AudioConnection          patchCord5(fade1, 0, mixer2, 0);
AudioConnection          patchCord6(mixer2, 0, delay1, 0);
AudioConnection          patchCord7(delay1, 0, mixer2, 1);
AudioConnection          patchCord8(mixer2, 0, i2s2, 0);
AudioConnection          patchCord10(mixer2, 0, peak2, 0);

// U8g2 Constructor for SSD1309 128x64 I2C display.
// Using NONAME2 as suggested by the compiler.
U8G2_SSD1309_128X64_NONAME2_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, 0x3C, &Wire);
const int DISPLAY_X_OFFSET = -2; // Adjust this value if the wrapping is different

// Transient //
double level = 0;
double averageLevel = 0;
double response = 0.1;

double level2 = 0;

// Screen //
long mappedPeak1;
int barHeight1;
long mappedPeak2;
int barHeight2;

long mappedPeak3;
int barHeight3;
long mappedPeak4;
int barHeight4;
long mappedPeak5;
int barHeight5;

bool updated = true;

// Synth //
double lastFreq = 0;
double threshold = 0.25;
int lastNote = 0;
int noteDelay = 250;

// Fade //
int fadeTime = 250;
int lastChange = 0;
bool rising = true;

/* Chorus //
const int CHORUS_DELAY_LENGTH_SAMPLES = 64 * AUDIO_BLOCK_SAMPLES;
short int chorusDelayBuffer[CHORUS_DELAY_LENGTH_SAMPLES];
const int CHORUS_NUM_VOICES = 5;*/

// Delay //
int delayTime = 500;
double delayLevel = 0.25;

// --- Effect Toggle Variables ---
const int EP1 = 0;
bool e1 = false;
Bounce debouncer1 = Bounce();

const int EP2 = 1;
bool e2 = false;
Bounce debouncer2 = Bounce();

const int EP3 = 11;
bool e3 = false;
Bounce debouncer3 = Bounce();

const int KP1 = A2;
const int KP2 = A1;
const int KP3 = A0;

double p1 = 0;
double p2 = 0;
double p3 = 0;

int v1 = 0;
int v2 = 0;
int v3 = 0;
int lv1 = 0;
int lv2 = 0;
int lv3 = 0;

// --- Setup Function ---
void setup() {
  delay(250);
  AudioMemory(1000); // Ensure enough audio memory is allocated
  delay(250);

  Wire.begin();    // Initialize Wire

  u8g2.begin();
  u8g2.clearBuffer(); // Clear the display buffer
  u8g2.sendBuffer();  // Send the cleared buffer to the display
  Serial.begin(9600); // Start serial communication after initial display setup
  Serial.println("Teensy Audio Processor");


  // Configure the footswitch pins with internal pull-up resistor
  pinMode(EP1, INPUT_PULLUP);
  pinMode(EP2, INPUT_PULLUP);
  pinMode(EP3, INPUT_PULLUP);

  // Configure potentiometer pins as input
  pinMode(KP1, INPUT);
  pinMode(KP2, INPUT);
  pinMode(KP3, INPUT);

  // Attach debouncers to the button pins and set interval
  debouncer1.attach(EP1);
  debouncer1.interval(50);
  debouncer2.attach(EP2);
  debouncer2.interval(50);
  debouncer3.attach(EP3);
  debouncer3.interval(50);

  // Audio Setup
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000_1.volume(0.5);

  lastChange = millis();
  lastNote = millis();

  // If you uncomment chorus, ensure the chorusDelayBuffer is also uncommented and correctly sized.
  // chorus1.begin(chorusDelayBuffer, CHORUS_DELAY_LENGTH_SAMPLES, CHORUS_NUM_VOICES);

  // Delay effect is initially disabled if e3 is false
  // delay1.delay(0, delayTime); // Don't enable here if it's controlled by a button

  mixer1.gain(0, 1); // Input to mixer1 (from i2s1)
  mixer1.gain(1, 0); // String synth to mixer1 (initially off)

  mixer2.gain(0, 1);      // Fade output to mixer2
  mixer2.gain(1, delayLevel); // Delay output to mixer2

  notefreq1.begin(threshold); // Note frequency analysis begins
}

void updateScreen() {
  u8g2.clearBuffer(); // Always clear buffer at the start of drawing a frame
  u8g2.setDrawColor(1); // Set draw color to white for all elements

  // Apply X_OFFSET to all drawing operations
  // Peak 1 (Input Level)
  mappedPeak1 = static_cast<long>(level * 1000.0);
  barHeight1 = map(mappedPeak1, 0L, 1000L, 0L, 64L);
  barHeight1 = constrain(barHeight1, 0, 64);
  u8g2.drawBox(0 + DISPLAY_X_OFFSET, 64 - barHeight1, 10, barHeight1);
  u8g2.drawBox(128 + DISPLAY_X_OFFSET, 64 - barHeight1, 2, barHeight1);

  u8g2.drawLine(13 + DISPLAY_X_OFFSET, 54, 13 + DISPLAY_X_OFFSET, 63);
  u8g2.drawLine(2 + DISPLAY_X_OFFSET, 63, 13 + DISPLAY_X_OFFSET, 63);
  

  u8g2.drawLine(13 + DISPLAY_X_OFFSET, 54, 19 + DISPLAY_X_OFFSET, 54);
  u8g2.drawLine(40 + DISPLAY_X_OFFSET, 54, 53 + DISPLAY_X_OFFSET, 54);
  u8g2.drawLine(74 + DISPLAY_X_OFFSET, 54, 87 + DISPLAY_X_OFFSET, 54);

  u8g2.drawLine(108 + DISPLAY_X_OFFSET, 54, 114 + DISPLAY_X_OFFSET, 54);
  u8g2.drawLine(114 + DISPLAY_X_OFFSET, 54, 114 + DISPLAY_X_OFFSET, 63);
  u8g2.drawLine(114 + DISPLAY_X_OFFSET, 63, 130 + DISPLAY_X_OFFSET, 63);

  // Peak 2 (Output Level)
  mappedPeak2 = static_cast<long>(level2 * 1000.0);
  barHeight2 = map(mappedPeak2, 0L, 1000L, 0L, 64L);
  barHeight2 = constrain(barHeight2, 0, 64);
  u8g2.drawBox(118 + DISPLAY_X_OFFSET, 64 - barHeight2, 10, barHeight2);

  // Potentiometer 1 (KP1)
  mappedPeak3 = static_cast<long>(v1 * 10); // v1 is 0-100, map to 0-1000 for consistency
  barHeight3 = map(mappedPeak3, 0L, 1000L, 0L, 35L);
  barHeight3 = constrain(barHeight3, 0, 35);
  u8g2.drawBox(25 + DISPLAY_X_OFFSET, 40 - barHeight3, 10, barHeight3);

  // Potentiometer 2 (KP2)
  mappedPeak4 = static_cast<long>(v2 * 10);
  barHeight4 = map(mappedPeak4, 0L, 1000L, 0L, 35L);
  barHeight4 = constrain(barHeight4, 0, 35);
  u8g2.drawBox(59 + DISPLAY_X_OFFSET, 40 - barHeight4, 10, barHeight4);

  // Potentiometer 3 (KP3)
  mappedPeak5 = static_cast<long>(v3 * 10);
  barHeight5 = map(mappedPeak5, 0L, 1000L, 0L, 35L);
  barHeight5 = constrain(barHeight5, 0, 35);
  u8g2.drawBox(93 + DISPLAY_X_OFFSET, 40 - barHeight5, 10, barHeight5);

  // Button 1 (EP1) State Indicator
  if(e1) {
    u8g2.drawRBox(20 + DISPLAY_X_OFFSET, 44, 20, 20, 7); // Filled box if enabled
  } else {
    u8g2.drawRFrame(20 + DISPLAY_X_OFFSET, 44, 20, 20, 7); // Frame if disabled
  }

  // Button 2 (EP2) State Indicator
  if(e2) {
    u8g2.drawRBox(54 + DISPLAY_X_OFFSET, 44, 20, 20, 7);
  } else {
    u8g2.drawRFrame(54 + DISPLAY_X_OFFSET, 44, 20, 20, 7);
  }

  // Button 3 (EP3) State Indicator
  if(e3) {
    u8g2.drawRBox(88 + DISPLAY_X_OFFSET, 44, 20, 20, 7);
  } else {
    u8g2.drawRFrame(88 + DISPLAY_X_OFFSET, 44, 20, 20, 7);
  }

  //u8g2.drawBox(0 + DISPLAY_X_OFFSET, 0, u8g2.getWidth(), u8g2.getHeight());
  delay(10);
  u8g2.sendBuffer(); // Send the completed buffer to the display
  updated = false;
  
}

void loop() {
  // Read audio peak levels
  if(peak1.available()) {
    level = peak1.read();
    updated = true;
  }
  if(peak2.available()) {
    level2 = peak2.read();
    updated = true;
  }

  // Read and smooth potentiometer values
  // p1, p2, p3 are smoothed float values (0-101)
  p1 = (0.25 * map(analogRead(KP1), 0, 1023, 0, 101)) + (0.75 * p1);
  p2 = (0.25 * map(analogRead(KP2), 0, 1023, 0, 101)) + (0.75 * p2);
  p3 = (0.25 * map(analogRead(KP3), 0, 1023, 0, 101)) + (0.75 * p3);

  // Update integer potentiometer values for display (v1, v2, v3)
  // Only update if there's a significant change to avoid constant redraws for minor fluctuations
  // The original condition `abs(v1 - floor(p1)) >= 0` is always true if p1 >= 0.
  // Consider using a small threshold like `abs(v1 - static_cast<int>(p1)) >= 1`
  if(abs(v1 - static_cast<int>(p1)) >= 0) {
    v1 = static_cast<int>(p1);
    updated = true; // Mark for screen update if value changes
  }
  if(abs(v2 - static_cast<int>(p2)) >= 0) {
    v2 = static_cast<int>(p2);
    updated = true;
  }
  if(abs(v3 - static_cast<int>(p3)) >= 0) {
    v3 = static_cast<int>(p3);
    updated = true;
  }


  // Update debouncers for buttons
  debouncer1.update();
  debouncer2.update();
  debouncer3.update();

  // Button 1 (EP1) - Synth Toggle
  if (debouncer1.fell()) {
    updated = true;
    e1 = !e1;
    Serial.print("E1 (Synth): ");
    Serial.println(e1 ? "ON" : "OFF");

    if(e1) {
      mixer1.gain(0, 0); // Mute audio input
      mixer1.gain(1, 1); // Enable string synth
    } else {
      mixer1.gain(0, 1); // Enable audio input
      mixer1.gain(1, 0); // Mute string synth
    }
  }

  // Button 2 (EP2) - Tremolo/Fade Toggle
  if (debouncer2.fell()) {
    updated = true;
    e2 = !e2;
    Serial.print("E2 (Tremolo): ");
    Serial.println(e2 ? "ON" : "OFF");

    /* If chorus is uncommented, use this logic:
    if(e2) {
      chorus1.voices(CHORUS_NUM_VOICES);
    } else {
      chorus1.voices(1); // Set to 1 voice to effectively disable chorus
    }*/
  }

  // Button 3 (EP3) - Delay Toggle
  if (debouncer3.fell()) {
    updated = true;
    e3 = !e3;
    Serial.print("E3 (Delay): ");
    Serial.println(e3 ? "ON" : "OFF");

    if(e3) {
      delay1.delay(0, delayTime); // Enable delay
    } else {
      delay1.disable(0); // Disable delay
    }
  }

  // Logic for Synth (e1) - Note detection and triggering Karplus-Strong
  if(e1) {
    if(((level - averageLevel) > 0.025) && (static_cast<long unsigned int>(millis()) - static_cast<long unsigned int>(lastNote) > static_cast<long unsigned int>(noteDelay))) { // Add static_cast to address warning
      double freq = notefreq1.read();
      if (freq > 0) { // Only trigger if a valid frequency is detected
        string1.noteOn(freq, (double) v1 / 100); // Trigger string synth with detected frequency
        lastFreq = freq; // Store last detected frequency
        lastNote = millis(); // Reset note delay timer
      }
    }
  }

  // Logic for Tremolo/Fade (e2)
  if(e2) {
    if ((static_cast<long unsigned int>(millis()) - static_cast<long unsigned int>(lastChange)) >= static_cast<long unsigned int>(fadeTime)) { // Add static_cast to address warning
      lastChange = millis();
      fadeTime = (100 - v2) * 10;
      if(rising) {
        fade1.fadeOut(fadeTime); // Start fading out
      } else {
        fade1.fadeIn(fadeTime);  // Start fading in
      }
      rising = !rising; // Toggle direction for next cycle
    }
  } else { // If e2 is OFF, ensure fade is always at full volume
    if(!rising) { // If it was fading out or had faded out
      fade1.fadeIn(fadeTime); // Fade back in to full volume
      rising = true; // Set state to rising (full volume)
    }
  }

  if(e3) {
    delay(10);
    if(v3 != lv3) {
      delayTime = (100 - v3) * 10;
      delayLevel = 0.9 - ((double) (100 - v3) / 150);
      delay1.delay(0, delayTime);
      mixer2.gain(1, delayLevel); // Delay output to mixer2
    }
  }

  // Smooth the average level for transient detection
  averageLevel = (response * level) + ((1.0 - response) * averageLevel);

  lv1 = v1;
  lv2 = v2;
  lv3 = v3;

  // Update the screen only if something has changed
  if(updated) {
    updateScreen();
  }
}
