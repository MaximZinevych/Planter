#include <Arduino.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

/* ----------- user settings ------------ */
#define DHT_PIN        2
#define RELAY_PIN      3
#define RELAY_ON_LEVEL LOW
#define BACKLIGHT_BUTTON_PIN 4

const float TEMP_THRESHOLD = 30.0; // °C.
const uint32_t DHT11_READ_TIMEOUT = 10UL * 1000UL;

const uint32_t BACKLIGHT_TIMEOUT = 60UL * 1000UL; 
uint32_t backlightTimerStart = 0;
bool backlightOn = true;
/* -------------------------------------- */

const uint32_t HOT_ON_MS   = 20UL * 60UL * 1000UL;   // 20 min
const uint32_t HOT_OFF_MS  = 10UL * 60UL * 1000UL;   // 10 min
const uint32_t COOL_ON_MS  = 15UL * 60UL * 1000UL;   // 15 min
const uint32_t COOL_OFF_MS = 15UL * 60UL * 1000UL;   // 15 min
// const uint32_t HOT_ON_MS   = 20UL * 1000UL;   // 20 seconds
// const uint32_t HOT_OFF_MS  = 10UL * 1000UL;   // 10 seconds
// const uint32_t COOL_ON_MS  = 15UL * 1000UL;   // 15 seconds
// const uint32_t COOL_OFF_MS = 15UL * 1000UL;   // 15 seconds

#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* ---- state variables ---- */
bool     pumpOn        = false;
uint32_t stateStarted  = 0;

uint32_t onMs          = COOL_ON_MS;
uint32_t offMs         = COOL_OFF_MS;
const char* cycleTag   = "15/15";

bool hotDetected       = false;  // ✅ remembers if we've seen T >= 30°C

uint32_t lastSensorRead = 0;
float    lastTemp = NAN, lastHum = NAN;

/* Decide cycle mode based on sensor + hotDetected flag */
void updateCycle()
{
  if (hotDetected) {
    onMs = HOT_ON_MS;
    offMs = HOT_OFF_MS;
    cycleTag = "20/10";
  } else {
    onMs = COOL_ON_MS;
    offMs = COOL_OFF_MS;
    cycleTag = "15/15";
  }
}

/* Turn pump ON/OFF and re-check mode after switch */
void switchPump(bool turnOn)
{
  pumpOn = turnOn;
  digitalWrite(RELAY_PIN, turnOn ? RELAY_ON_LEVEL : !RELAY_ON_LEVEL);
  stateStarted = millis();

  // ✅ Re-evaluate mode only when switching
  updateCycle();
}

void setup()
{
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BACKLIGHT_BUTTON_PIN, INPUT_PULLUP);  // button pressed = LOW

  switchPump(true);  // start with pump OFF

  Serial.begin(9600);
  dht.begin();

  lcd.init();
  lcd.backlight();
  backlightTimerStart = millis();  // start 60s timer
  backlightOn = true;

  lcd.clear();
  lcd.print(F("Plant Monitor"));
  delay(1500);
}

void loop()
{
  uint32_t now = millis();

  /* ---- Read DHT11 every N s ---- */
  if (now - lastSensorRead >= DHT11_READ_TIMEOUT)
  {
    lastSensorRead = now;
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    if (!isnan(h) && !isnan(t)) {
      lastHum = h;
      lastTemp = t;

      // ✅ If hot detected — lock into hot mode (until next switch)
      if (t >= TEMP_THRESHOLD && !hotDetected) {
        hotDetected = true;
        updateCycle(); // immediately change cycle to 20/10
      }
    }
  }

  /* ---- Pump timing ---- */
  uint32_t elapsed = now - stateStarted;
  if (pumpOn && elapsed >= onMs)
  {
    hotDetected = false;   // ✅ allow switch back to cool after this cycle
    // decide next cycle based on latest temperature
    if (lastTemp >= TEMP_THRESHOLD) hotDetected = true;
    updateCycle();
    switchPump(false);
  }
  else if (!pumpOn && elapsed >= offMs)
  {
    hotDetected = false;   // ✅ allow switch back to cool after this cycle
    if (lastTemp >= TEMP_THRESHOLD) hotDetected = true;
    updateCycle();
    switchPump(true);
  }

  /* ---- LCD update ---- */
  static uint32_t lastLcd = 0;
  if (now - lastLcd >= 500UL)
  {
    lastLcd = now;

    // Row 1: temperature and humidity
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(lastTemp, 1);
    lcd.print((char)223);
    lcd.print("C H:");
    lcd.print(lastHum, 0);
    lcd.print("%  ");

    // Row 2: pump status, countdown, mode
    uint32_t remaining = ((pumpOn ? onMs : offMs) - elapsed + 999UL) / 1000UL;
    uint8_t mm = remaining / 60U;
    uint8_t ss = remaining % 60U;

    lcd.setCursor(0, 1);
    lcd.print("P:");
    lcd.print(pumpOn ? "ON " : "OFF");
    lcd.print(mm < 10 ? "0" : "");
    lcd.print(mm);
    lcd.print(':');
    lcd.print(ss < 10 ? "0" : "");
    lcd.print(ss);
    lcd.print(' ');
    lcd.print(cycleTag);

    int used = 2 + (pumpOn ? 3 : 4) + 4 + 1 + strlen(cycleTag);
    for (int i = used; i < 16; ++i) lcd.print(' ');
  }

  // Check button press to re-enable backlight
  if (!digitalRead(BACKLIGHT_BUTTON_PIN)) {  // button pressed
    if (!backlightOn) {
      lcd.backlight();
      backlightOn = true;
    }
    backlightTimerStart = millis();  // reset timer
  }

  // Turn off backlight after 30s
  if (backlightOn && millis() - backlightTimerStart >= BACKLIGHT_TIMEOUT) {
    lcd.noBacklight();
    backlightOn = false;
  }
}
