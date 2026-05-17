// SoleSense - 6-FSR matrix-scan demo (Arduino Uno, TinkerCAD)
//
// Mirrors the production firmware in firmware/SoleSenseV2/sensors.cpp:
// 3 shared ADCs (A0/A1/A2) multiplexed across 2 power sets (D7/D8).
// Only one set is powered at a time; the inactive set is driven LOW
// (active-LOW parking) so any leakage path from a pressed FSR shorts
// to GND instead of crosstalking into the other set's reading.
// 4-sample oversampling per ADC kills mains hum and breadboard noise.
//
// Channel map (matches firmware sensors.cpp):
//   Set 1 (D7 HIGH): A0=HeelMed  A1=HeelLat  A2=MidMed
//   Set 2 (D8 HIGH): A0=MidLat   A1=ForeMed  A2=ForeLat
//
// Wiring per FSR: pin 1 -> power-set pin (D7 or D8),
//                 pin 2 -> shared ADC (A0/A1/A2).
// Each ADC has ONE 10k pull-down to GND (3 pull-downs total, shared
// between the two FSRs that hit that column).

const uint8_t PIN_PWR_SET1 = 7;
const uint8_t PIN_PWR_SET2 = 8;
const uint8_t PIN_ADC_A    = A0;
const uint8_t PIN_ADC_B    = A1;
const uint8_t PIN_ADC_C    = A2;

const char* const CHANNEL_NAMES[6] = {
  "HeelMed", "HeelLat", "MidMed",
  "MidLat",  "ForeMed", "ForeLat"
};

int fsr[6]      = {0,0,0,0,0,0};
int fsr_zero[6] = {0,0,0,0,0,0};

const int STEP_THRESHOLD  = 250;
const int STEP_RELEASE    = 100;
const unsigned long STEP_REFRACTORY_MS = 200;
unsigned long last_step_ms    = 0;
unsigned long step_count      = 0;
bool          above_threshold = false;

static inline int read_adc_oversampled(uint8_t pin) {
  int sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogRead(pin);
  return sum >> 2;
}

static void read_set_a() {
  digitalWrite(PIN_PWR_SET2, LOW);
  digitalWrite(PIN_PWR_SET1, HIGH);
  delayMicroseconds(200);
  fsr[0] = read_adc_oversampled(PIN_ADC_A) - fsr_zero[0];
  fsr[1] = read_adc_oversampled(PIN_ADC_B) - fsr_zero[1];
  fsr[2] = read_adc_oversampled(PIN_ADC_C) - fsr_zero[2];
}

static void read_set_b() {
  digitalWrite(PIN_PWR_SET1, LOW);
  digitalWrite(PIN_PWR_SET2, HIGH);
  delayMicroseconds(200);
  fsr[3] = read_adc_oversampled(PIN_ADC_A) - fsr_zero[3];
  fsr[4] = read_adc_oversampled(PIN_ADC_B) - fsr_zero[4];
  fsr[5] = read_adc_oversampled(PIN_ADC_C) - fsr_zero[5];
}

// Drive both sets LOW between active reads so leakage paths are
// grounded, not floating -- kills ghost crosstalk on un-pressed FSRs
// sharing an ADC column.
static void park_sets_grounded() {
  digitalWrite(PIN_PWR_SET1, LOW);
  digitalWrite(PIN_PWR_SET2, LOW);
}

static void read_all_fsrs() {
  read_set_a();
  read_set_b();
  park_sets_grounded();
  for (int i = 0; i < 6; i++) if (fsr[i] < 0) fsr[i] = 0;
}

void setup() {
  Serial.begin(9600);
  pinMode(PIN_PWR_SET1, OUTPUT);
  pinMode(PIN_PWR_SET2, OUTPUT);
  pinMode(PIN_ADC_A, INPUT);
  pinMode(PIN_ADC_B, INPUT);
  pinMode(PIN_ADC_C, INPUT);
  park_sets_grounded();

  delay(500);
  Serial.println();
  Serial.println(F("=== SoleSense 6-FSR matrix-scan demo (Arduino Uno) ==="));
  Serial.println(F("Calibrating zero baseline -- keep all FSR sliders at 0..."));

  long sum[6] = {0,0,0,0,0,0};
  for (int s = 0; s < 32; s++) {
    int tmp[6];
    read_set_a(); for (int i = 0; i < 3; i++) tmp[i]   = fsr[i];
    read_set_b(); for (int i = 3; i < 6; i++) tmp[i]   = fsr[i];
    park_sets_grounded();
    for (int i = 0; i < 6; i++) sum[i] += tmp[i];
    delay(10);
  }
  for (int i = 0; i < 6; i++) fsr_zero[i] = sum[i] / 32;

  Serial.print(F("Zero baselines: "));
  for (int i = 0; i < 6; i++) { Serial.print(fsr_zero[i]); Serial.print(' '); }
  Serial.println();
  Serial.println(F("Ready. Drag the FSR sliders to simulate footstrikes."));
  Serial.println();
}

void loop() {
  read_all_fsrs();

  int heel     = fsr[0] + fsr[1];
  int midfoot  = fsr[2] + fsr[3];
  int forefoot = fsr[4] + fsr[5];
  int total    = heel + midfoot + forefoot;

  int medial  = fsr[0] + fsr[2] + fsr[4];
  int lateral = fsr[1] + fsr[3] + fsr[5];
  int ml_total = medial + lateral;

  const char* pattern = "idle";
  if (total > 200) {
    if      (heel     >= midfoot && heel     >= forefoot) pattern = "heel-strike";
    else if (forefoot >= heel    && forefoot >= midfoot)  pattern = "forefoot";
    else                                                  pattern = "midfoot";
  }

  int any_max = 0;
  for (int i = 0; i < 6; i++) if (fsr[i] > any_max) any_max = fsr[i];
  unsigned long now = millis();
  if (!above_threshold
      && any_max > STEP_THRESHOLD
      && (now - last_step_ms) > STEP_REFRACTORY_MS) {
    above_threshold = true;
    last_step_ms = now;
    step_count++;
  } else if (above_threshold && any_max < STEP_RELEASE) {
    above_threshold = false;
  }

  static unsigned long last_print = 0;
  if (now - last_print >= 100) {
    last_print = now;
    Serial.print(F("Steps:")); Serial.print(step_count);
    Serial.print(F(" | "));
    for (int i = 0; i < 6; i++) {
      Serial.print(CHANNEL_NAMES[i]); Serial.print('=');
      Serial.print(fsr[i]); Serial.print(' ');
    }
    Serial.print(F("| H/M/F: "));
    Serial.print(heel); Serial.print('/');
    Serial.print(midfoot); Serial.print('/');
    Serial.print(forefoot);
    if (ml_total > 50) {
      int med_pct = (medial * 100L) / ml_total;
      Serial.print(F(" | Med/Lat: "));
      Serial.print(med_pct); Serial.print('/'); Serial.print(100 - med_pct);
    }
    Serial.print(F(" | "));
    Serial.println(pattern);
  }

  delay(20);
}
