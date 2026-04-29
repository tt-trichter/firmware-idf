// Trichter Hall Effect Flow Sensor Emulator
// Emulates a G3/4 sensor: pulse frequency (Hz) = 6.6 * Q (L/min)
//
// Wiring: Pin 9 -> 10kΩ -> ESP32 GPIO4; midpoint -> 10kΩ -> GND
//         Also connect Arduino GND to ESP32 GND.
//
// Commands (115200 baud):
//   flow <lpm>        Start continuous flow at <lpm> L/min
//   stop              Stop flow
//   run <lpm> <secs>  Run for a fixed duration then stop
//   status            Show current state
//   test_normal       run 10 5   (~10 L/min, passes startup)
//   test_low          run 2 3    (below startup minimum, firmware returns timeout)
//   test_border       run 4 3    (just above 3.79 L/min startup minimum)
//   test_max          run 30 5   (maximum rated flow)
//   test_short        run 10 1   (tests idle timeout detection)

const int OUTPUT_PIN = 9;
const float PULSES_PER_LPM = 6.6;
const bool DEBUG_SLOW = false;
const float DEBUG_SCALE = 1; // 5% speed

float  g_rate_lpm       = 0.0;
bool   g_flowing        = false;
unsigned long g_run_until_ms   = 0;
unsigned long g_last_toggle_us = 0;
unsigned long g_half_period_us = 0;
bool   g_pin_state      = false;
String g_serial_buf     = "";

void printHelp() {
  Serial.println(F("--- Trichter Sensor Emulator ---"));
  Serial.println(F("Commands:"));
  Serial.println(F("  flow <lpm>        Continuous flow at L/min"));
  Serial.println(F("  stop              Stop flow"));
  Serial.println(F("  run <lpm> <secs>  Timed session"));
  Serial.println(F("  status            Current state"));
  Serial.println(F("Presets: test_normal, test_low, test_border, test_max, test_short"));
}

void startFlow(float lpm, unsigned long duration_ms) {
  if (lpm <= 0.0) {
    Serial.println(F("Error: rate must be > 0"));
    return;
  }
  g_rate_lpm = lpm;
  g_flowing = true;
  g_run_until_ms = (duration_ms > 0) ? millis() + duration_ms : 0;

  float freq = PULSES_PER_LPM * lpm;
  if (DEBUG_SLOW) freq *= DEBUG_SCALE;

  g_half_period_us = (unsigned long)(500000.0 / freq);
  g_last_toggle_us = micros();
  g_pin_state = false;
  digitalWrite(OUTPUT_PIN, LOW);

  Serial.print(F("Flowing: "));
  Serial.print(lpm, 1);
  Serial.print(F(" L/min  freq="));
  Serial.print(freq, 1);
  Serial.print(F(" Hz  half_period="));
  Serial.print(g_half_period_us);
  if (duration_ms > 0) {
    Serial.print(F(" us  duration="));
    Serial.print(duration_ms / 1000.0, 1);
    Serial.println(F(" s"));
  } else {
    Serial.println(F(" us  (continuous)"));
  }
}

void stopFlow() {
  g_flowing = false;
  g_run_until_ms = 0;
  g_rate_lpm = 0.0;
  g_pin_state = false;
  digitalWrite(OUTPUT_PIN, LOW);
  Serial.println(F("Stopped. ESP32 will detect idle in ~500 ms."));
}

void handleCommand(String &line) {
  line.trim();
  if (line.length() == 0) return;

  if (line == "stop") {
    stopFlow();
  } else if (line == "status") {
    if (g_flowing) {
      Serial.print(F("Flowing: "));
      Serial.print(g_rate_lpm, 1);
      Serial.print(F(" L/min  freq="));
      Serial.print(PULSES_PER_LPM * g_rate_lpm, 1);
      Serial.println(F(" Hz"));
    } else {
      Serial.println(F("Idle"));
    }
  } else if (line == "help") {
    printHelp();
  } else if (line == "test_normal") {
    startFlow(10.0, 5000);
  } else if (line == "test_low") {
    Serial.println(F("Note: 2 L/min = 13.2 Hz, below 25 Hz startup minimum -> expect ESP_ERR_TIMEOUT"));
    startFlow(2.0, 3000);
  } else if (line == "test_border") {
    Serial.println(F("Note: 4 L/min = 26.4 Hz, just above 25 Hz minimum"));
    startFlow(4.0, 3000);
  } else if (line == "test_max") {
    startFlow(30.0, 5000);
  } else if (line == "test_short") {
    Serial.println(F("Note: 1 s session, idle detected ~500 ms after stop"));
    startFlow(10.0, 1000);
  } else if (line.startsWith("flow ")) {
    float lpm = line.substring(5).toFloat();
    startFlow(lpm, 0);
  } else if (line.startsWith("run ")) {
    String args = line.substring(4);
    args.trim();
    int space = args.indexOf(' ');
    if (space < 0) {
      Serial.println(F("Usage: run <lpm> <secs>"));
      return;
    }
    float lpm = args.substring(0, space).toFloat();
    float secs = args.substring(space + 1).toFloat();
    if (secs <= 0.0) {
      Serial.println(F("Usage: run <lpm> <secs>"));
      return;
    }
    startFlow(lpm, (unsigned long)(secs * 1000.0));
  } else {
    Serial.print(F("Unknown command: "));
    Serial.println(line);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);
  printHelp();
}

void loop() {
  // Non-blocking serial line reader
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_serial_buf.length() > 0) {
        handleCommand(g_serial_buf);
        g_serial_buf = "";
      }
    } else {
      g_serial_buf += c;
    }
  }

  // End timed run
  if (g_flowing && g_run_until_ms > 0 && millis() >= g_run_until_ms) {
    stopFlow();
    return;
  }

  // Pulse generation — drift-free by advancing last_toggle by fixed half-period
  if (g_flowing && g_half_period_us > 0) {
    unsigned long now = micros();
    if (now - g_last_toggle_us >= g_half_period_us) {
      g_last_toggle_us += g_half_period_us;
      g_pin_state = !g_pin_state;
      digitalWrite(OUTPUT_PIN, g_pin_state ? HIGH : LOW);
    }
  }
}
