/*
 * ======================================================================================
 * PROJECT TITLE : AUTOMATIC ROOM ENERGY SAVER
 * GROUP         : GROUP 15
 * BOARD         : STM32F103C8T6 (Blue Pill)
 * --------------------------------------------------------------------------------------
 * DESCRIPTION:
 * This system saves electricity by automatically controlling room appliances (Relay &
 * Jumbo LED) based on two real-time conditions:
 *   1. Bi-directional Occupancy Tracking (counting people entering and exiting with
 *      audible Buzzer feedback).
 *   2. Ambient Light Sensing (checking if the room already has enough natural daylight).
 *
 * The Relay and Jumbo LED will ONLY turn ON when:
 *   [Visitor Count > 0]  AND  [Room is Dark]
 *
 * --------------------------------------------------------------------------------------
 * HARDWARE PINOUT SUMMARY:
 *   - 16x2 I2C LCD  : SDA -> PB7  | SCL -> PB6 | VCC -> 5V   | GND -> GND
 *   - IR Sensor 1   : OUT -> PB12 | VCC -> 5V  | GND -> GND (Outside Door / Entry)
 *   - IR Sensor 2   : OUT -> PB13 | VCC -> 5V  | GND -> GND (Inside Door  / Exit)
 *   - Buzzer        : (+) -> PA8  | (-) -> GND (Single beep on Entry, Double beep on Exit)
 *   - LDR Module    : AO  -> PA1  | VCC -> 3.3V| GND -> GND
 *   - Relay Module  : IN  -> PB0  | VCC -> 5V  | GND -> GND (Active-LOW)
 *   - Jumbo LED     : Anode (+) -> PB1 (via 220 ohm resistor) | Cathode (-) -> GND
 * ======================================================================================
 */

#include <Wire.h>               // Built-in library for I2C communication (PB6 = SCL, PB7 = SDA)
#include <LiquidCrystal_I2C.h>  // Library to control the 16x2 LCD screen over I2C

// ======================================================================================
// 1. HARDWARE PIN DEFINITIONS
// ======================================================================================
#define IR_ENTRY_PIN        PB12   // IR Sensor 1 placed at the outer side of the door
#define IR_EXIT_PIN         PB13   // IR Sensor 2 placed at the inner side of the door
#define LDR_PIN             PA1    // Analog pin to read light intensity from the LDR module
#define RELAY_PIN           PB0    // Digital output pin to switch the 5V Relay ON/OFF
#define JUMBO_LED_PIN       PB1    // Digital output pin to drive the external Jumbo LED
#define BUZZER_PIN          PA8    // Digital output pin to drive the Entry/Exit Buzzer

// ======================================================================================
// 2. SYSTEM CONSTANTS & THRESHOLDS
// ======================================================================================
#define LCD_ADDR            0x27   // I2C address of the LCD (change to 0x3F if screen stays blank)
#define LDR_DARK_THRESHOLD  2200   // 12-bit ADC range is 0 to 4095. Values above 2200 mean it is DARK
#define SENSOR_TIMEOUT_MS   3000   // Max time (3 seconds) allowed to cross from Sensor 1 to Sensor 2

// Create the LCD object: address 0x27, 16 columns per line, 2 lines total
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// ======================================================================================
// 3. GLOBAL VARIABLES
// ======================================================================================
int visitorCount = 0;              // Tracks the current number of people inside the room
bool loadState = false;            // Tracks whether the Relay/Jumbo LED is ON (true) or OFF (false)

// Variables for non-blocking LCD refresh (avoids freezing the main loop)
unsigned long lastLcdUpdate = 0;          // Stores the last time (in ms) the LCD was updated
const unsigned long LCD_INTERVAL = 300;   // Refresh the LCD screen every 300 milliseconds

// ======================================================================================
// 4. SETUP FUNCTION (Runs once when the board is powered on or reset)
// ======================================================================================
void setup() {
  // Set STM32 Analog-to-Digital Converter (ADC) to native 12-bit resolution (0 - 4095)
  analogReadResolution(12);

  // Configure IR sensor pins as inputs with internal pull-up resistors enabled.
  // IR sensors output HIGH (1) normally, and drop to LOW (0) when an obstacle blocks them.
  pinMode(IR_ENTRY_PIN, INPUT_PULLUP);
  pinMode(IR_EXIT_PIN, INPUT_PULLUP);

  // Configure the LDR pin as an analog voltage input
  pinMode(LDR_PIN, INPUT_ANALOG);

  // Configure the Relay, Jumbo LED, and Buzzer pins as digital outputs
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(JUMBO_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Set safe initial states before the program starts:
  // Most 5V Relay modules are "Active-LOW" (HIGH = Relay OFF, LOW = Relay ON)
  digitalWrite(RELAY_PIN, HIGH);     // Keep Relay OFF at startup
  digitalWrite(JUMBO_LED_PIN, LOW);  // Keep Jumbo LED OFF at startup
  digitalWrite(BUZZER_PIN, LOW);     // Keep Buzzer silent at startup

  // Initialize I2C bus and LCD screen
  Wire.begin();
  lcd.init();
  lcd.backlight();                   // Turn on the LCD background LED
  lcd.clear();

  // Short startup beep (100ms) to confirm the Buzzer and power are working
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);

  // --- STARTUP BANNER: SCREEN 1 (Project Title) ---
  lcd.setCursor(1, 0);               // Column 1, Line 0 (top row)
  lcd.print("AUTOMATIC ROOM");
  lcd.setCursor(2, 1);               // Column 2, Line 1 (bottom row)
  lcd.print("ENERGY SAVER");
  delay(2000);                       // Hold title on screen for 2 seconds

  // --- STARTUP BANNER: SCREEN 2 (Team Info) ---
  lcd.clear();
  lcd.setCursor(4, 0);               // Centered on top row
  lcd.print("GROUP 15");
  lcd.setCursor(1, 1);               // Bottom row
  lcd.print("System Ready...");
  delay(1500);                       // Hold team info for 1.5 seconds

  lcd.clear();                       // Clear screen before entering the main loop
}

// ======================================================================================
// 5. MAIN LOOP (Runs continuously)
// ======================================================================================
void loop() {
  detectOccupancy();         // Task 1: Check IR sensors for Entry/Exit and beep the Buzzer
  evaluateEnergyControl();   // Task 2: Decide whether to turn the Relay & Jumbo LED ON or OFF
  updateDisplay();           // Task 3: Update the 16x2 LCD every 300ms
}

// ======================================================================================
// 6. HELPER FUNCTION: BUZZER AUDIO ALERTS
// ======================================================================================
// Plays 1 beep when someone enters, and 2 quick beeps when someone exits
void triggerBuzzer(int beeps, int durationMs) {
  for (int i = 0; i < beeps; i++) {
    digitalWrite(BUZZER_PIN, HIGH);  // Turn Buzzer ON
    delay(durationMs);
    digitalWrite(BUZZER_PIN, LOW);   // Turn Buzzer OFF
    if (i < beeps - 1) {
      delay(70);                     // Short pause between multiple beeps
    }
  }
}

// ======================================================================================
// 7. TASK 1: BI-DIRECTIONAL VISITOR COUNTING LOGIC
// ======================================================================================
void detectOccupancy() {
  
  // ------------------------- CASE A: SOMEONE IS ENTERING -------------------------
  // Sequence: Outer Sensor (IR_ENTRY) is triggered FIRST, followed by Inner Sensor (IR_EXIT)
  if (digitalRead(IR_ENTRY_PIN) == LOW) {
    unsigned long startTime = millis();  // Record the exact time the outer beam was broken
    
    // Wait until the person steps past the first sensor (beam returns to HIGH)
    while (digitalRead(IR_ENTRY_PIN) == LOW) { 
      delay(10); 
    }

    bool entrySuccess = false;
    
    // Open a 3-second time window to wait for the person to cross the second (inner) sensor
    while ((millis() - startTime) < SENSOR_TIMEOUT_MS) {
      if (digitalRead(IR_EXIT_PIN) == LOW) {
        // Wait until the person completely clears the second sensor beam
        while (digitalRead(IR_EXIT_PIN) == LOW) { 
          delay(10); 
        }
        entrySuccess = true; // Both sensors were crossed in the Entry order!
        break;               // Exit the waiting loop immediately
      }
    }

    // If the full entry sequence was completed within 3 seconds:
    if (entrySuccess) {
      visitorCount++;        // Add 1 person to the room count
      triggerBuzzer(1, 120); // Sound 1 crisp beep (120ms) for Entry
      delay(150);            // Short cooldown delay to prevent accidental double-counting
    }
  }

  // ------------------------- CASE B: SOMEONE IS EXITING --------------------------
  // Sequence: Inner Sensor (IR_EXIT) is triggered FIRST, followed by Outer Sensor (IR_ENTRY)
  if (digitalRead(IR_EXIT_PIN) == LOW) {
    unsigned long startTime = millis();  // Record the exact time the inner beam was broken
    
    // Wait until the person clears the inner sensor
    while (digitalRead(IR_EXIT_PIN) == LOW) { 
      delay(10); 
    }

    bool exitSuccess = false;
    
    // Open a 3-second time window to wait for the person to cross the outer sensor
    while ((millis() - startTime) < SENSOR_TIMEOUT_MS) {
      if (digitalRead(IR_ENTRY_PIN) == LOW) {
        // Wait until the person completely clears the outer sensor beam
        while (digitalRead(IR_ENTRY_PIN) == LOW) { 
          delay(10); 
        }
        exitSuccess = true;  // Both sensors were crossed in the Exit order!
        break;               // Exit the waiting loop
      }
    }

    // If the full exit sequence was completed:
    if (exitSuccess) {
      if (visitorCount > 0) {
        visitorCount--;      // Subtract 1 person (prevent count from dropping below 0)
      }
      triggerBuzzer(2, 70);  // Sound 2 quick beeps (70ms each) for Exit
      delay(150);            // Cooldown delay to prevent false triggers
    }
  }
}

// ======================================================================================
// 8. TASK 2: SMART ENERGY SAVING & RELAY / JUMBO LED CONTROL
// ======================================================================================
void evaluateEnergyControl() {
  // Read the analog voltage from the LDR sensor (returns a number between 0 and 4095)
  // Higher value = More darkness | Lower value = Brighter light
  int lightLevel = analogRead(LDR_PIN);
  
  // Check if the room is darker than our threshold (2200)
  bool isDark = (lightLevel > LDR_DARK_THRESHOLD);

  // MAIN ENERGY SAVING CONDITION:
  // Turn appliances ON only if at least 1 person is in the room AND the room is dark
  if (visitorCount > 0 && isDark) {
    loadState = true;
    digitalWrite(RELAY_PIN, LOW);      // Pull pin LOW to energize the Active-LOW Relay
    digitalWrite(JUMBO_LED_PIN, HIGH); // Pull pin HIGH to turn ON the Jumbo LED
  } 
  // Otherwise (if the room is empty OR there is plenty of natural sunlight), save power!
  else {
    loadState = false;
    digitalWrite(RELAY_PIN, HIGH);     // Pull pin HIGH to turn OFF the Relay
    digitalWrite(JUMBO_LED_PIN, LOW);  // Pull pin LOW to turn OFF the Jumbo LED
  }
}

// ======================================================================================
// 9. TASK 3: NON-BLOCKING LCD SCREEN UPDATE
// ======================================================================================
void updateDisplay() {
  // Only update the LCD if 300ms have passed since the last refresh.
  if (millis() - lastLcdUpdate >= LCD_INTERVAL) {
    lastLcdUpdate = millis();          // Save the current time for the next cycle

    // Read the LDR and convert the 0-4095 value into an easy-to-read 0-100% scale
    int lightLevel = analogRead(LDR_PIN);
    int lightPercent = map(lightLevel, 0, 4095, 0, 100);

    // --- ROW 0 (TOP LINE): Display Visitor Count and Load Status ---
    // Example output: "Count: 2  LOAD:ON "
    lcd.setCursor(0, 0);
    lcd.print("Count: ");
    lcd.print(visitorCount);
    lcd.print("   ");                  // Extra spaces erase leftover digits (e.g. when 10 drops to 9)

    lcd.setCursor(10, 0);
    lcd.print(loadState ? "LOAD:ON " : "LOAD:OFF");

    // --- ROW 1 (BOTTOM LINE): Display Ambient Light Condition ---
    // Example output: "Light: DARK   68% "
    lcd.setCursor(0, 1);
    lcd.print("Light: ");
    if (lightLevel > LDR_DARK_THRESHOLD) {
      lcd.print("DARK  ");
    } else {
      lcd.print("BRIGHT");
    }
    lcd.print(" ");
    lcd.print(lightPercent);
    lcd.print("% ");                   // Extra space clears trailing '%' when 100% drops to 99%
  }
}