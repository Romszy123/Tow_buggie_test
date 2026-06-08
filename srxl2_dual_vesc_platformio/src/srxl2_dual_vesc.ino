#include <SingleWireSerial.h>
#include <SingleWireSerial_config.h>
#include <avr/interrupt.h>
#include "spm_srxl.h"
#include "uart.h"

// ---------------------------------------------------------------------
// Basic config
// ---------------------------------------------------------------------
const uint8_t UART_HANDLE = 1;
const uint32_t SERIAL_NUM = 0xFF1FFF1F;  // any likely-unique ID

// UART receive buffer for SRXL frames
uint8_t rxBuffer[2 * SRXL_MAX_BUFFER_SIZE];
uint16_t rxBufferIndex = 0;

// ---------------------------------------------------------------------
// DXS channel scaling and CH5 source selection
// ---------------------------------------------------------------------
const int CH1_MIN = 708;
const int CH1_MAX = 3336;
const int CH2_FULL_RIGHT = 712;
const int CH2_CENTER = 2048;
const int CH2_FULL_LEFT = 3396;
const int DXS_STEERING_INFLUENCE_PERCENT = 50;
const int CH5_DXS_VALUE = 684;
const int CH5_NONE_VALUE = 2048;
const int CH5_BREMOTE_VALUE = 3412;
const int CH5_DXS_TO_NONE_THRESHOLD = (CH5_DXS_VALUE + CH5_NONE_VALUE) / 2;          // 1366
const int CH5_NONE_TO_BREMOTE_THRESHOLD = (CH5_NONE_VALUE + CH5_BREMOTE_VALUE) / 2; // 2730
const uint32_t SRXL_SIGNAL_TIMEOUT_MS = 250;

// ---------------------------------------------------------------------
// BRemote RX PWM inputs
// D21 and D20 are external-interrupt pins on the Arduino Mega.
// ---------------------------------------------------------------------
const uint8_t BREMOTE_INPUT_0_PIN = 21;
const uint8_t BREMOTE_INPUT_1_PIN = 20;
const uint16_t BREMOTE_PWM_MIN_US = 996;
const uint16_t BREMOTE_PWM_MAX_US = 1984;
const uint16_t PWM_INPUT_VALID_MIN_US = 800;
const uint16_t PWM_INPUT_VALID_MAX_US = 2200;
const uint32_t BREMOTE_PWM_TIMEOUT_US = 100000;

// ---------------------------------------------------------------------
// Servo-style PWM outputs for two VESCs
// Timer5 produces stable 50 Hz pulses without blocking SRXL parsing.
// ---------------------------------------------------------------------
const uint8_t VESC_OUTPUT_0_PIN = 6;
const uint8_t VESC_OUTPUT_1_PIN = 8;
const uint16_t VESC_PWM_MIN_US = 996;
const uint16_t VESC_PWM_MAX_US = 1984;
const uint16_t VESC_PWM_FRAME_US = 20000;
const uint16_t TIMER5_TICKS_PER_US = 2; // 16 MHz / 8 prescaler

enum ControlMode {
  CONTROL_MODE_DXS = 0,
  CONTROL_MODE_NONE = 1,
  CONTROL_MODE_BREMOTE = 2
};

volatile uint32_t bremoteRiseMicros[2] = {0, 0};
volatile uint32_t bremoteLastPulseMicros[2] = {0, 0};
volatile uint16_t bremotePulseWidthUs[2] = {0, 0};
volatile uint16_t vescOutputPulseUs[2] = {VESC_PWM_MIN_US, VESC_PWM_MIN_US};

int latestDxsCh1 = CH1_MIN;
int latestDxsCh2 = 0;
int latestDxsCh5 = 0;
bool srxlHasValidFrame = false;
bool srxlFailsafeActive = true;
uint32_t srxlLastChannelDataMs = 0;
int lastLoggedControlMode = -1;

// Forward declarations required by the SRXL library
void userProvidedFillSrxlTelemetry(SrxlTelemetryData* pTelemetry);
void userProvidedReceivedChannelData(SrxlChannelData* pChannelData, bool isFailsafeData);
void userProvidedHandleVtxData(SrxlVtxData* pVtxData);

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------
static int mapConstrained(int x, int inMin, int inMax, int outMin, int outMax) {
  x = constrain(x, inMin, inMax);
  long numerator = (long)(x - inMin) * (outMax - outMin);
  long denominator = (long)(inMax - inMin);
  return (int)(outMin + numerator / denominator);
}

static int dxsSteeringOffsetUs(int ch2) {
  const int maxOffsetUs =
      (VESC_PWM_MAX_US - VESC_PWM_MIN_US) * DXS_STEERING_INFLUENCE_PERCENT / 100;

  if (ch2 >= CH2_CENTER) {
    return mapConstrained(ch2, CH2_CENTER, CH2_FULL_LEFT, 0, maxOffsetUs);
  }
  return mapConstrained(ch2, CH2_FULL_RIGHT, CH2_CENTER, -maxOffsetUs, 0);
}

static uint16_t constrainVescPulseUs(int pulseUs) {
  return (uint16_t)constrain(pulseUs, (int)VESC_PWM_MIN_US, (int)VESC_PWM_MAX_US);
}

static ControlMode controlModeFromCh5(int ch5) {
  if (ch5 < CH5_DXS_TO_NONE_THRESHOLD) {
    return CONTROL_MODE_DXS;
  }
  if (ch5 < CH5_NONE_TO_BREMOTE_THRESHOLD) {
    return CONTROL_MODE_NONE;
  }
  return CONTROL_MODE_BREMOTE;
}

static bool srxlChannelDataIsFresh(uint32_t nowMs) {
  return srxlHasValidFrame &&
         !srxlFailsafeActive &&
         ((uint32_t)(nowMs - srxlLastChannelDataMs) <= SRXL_SIGNAL_TIMEOUT_MS);
}

static ControlMode activeControlMode(uint32_t nowMs) {
  if (!srxlChannelDataIsFresh(nowMs)) {
    return CONTROL_MODE_BREMOTE;
  }
  return controlModeFromCh5(latestDxsCh5);
}

static const char* controlModeName(ControlMode controlMode) {
  if (controlMode == CONTROL_MODE_DXS) {
    return "DXS";
  }
  if (controlMode == CONTROL_MODE_BREMOTE) {
    return "BRemote";
  }
  return "None";
}

static void setVescOutputs(uint16_t pulse0Us, uint16_t pulse1Us) {
  pulse0Us = constrain(pulse0Us, VESC_PWM_MIN_US, VESC_PWM_MAX_US);
  pulse1Us = constrain(pulse1Us, VESC_PWM_MIN_US, VESC_PWM_MAX_US);

  noInterrupts();
  vescOutputPulseUs[0] = pulse0Us;
  vescOutputPulseUs[1] = pulse1Us;
  interrupts();
}

static bool readFreshBremotePulse(uint8_t index, uint16_t* pulseUs) {
  uint16_t capturedPulseUs;
  uint32_t lastPulseMicros;

  noInterrupts();
  capturedPulseUs = bremotePulseWidthUs[index];
  lastPulseMicros = bremoteLastPulseMicros[index];
  interrupts();

  if (capturedPulseUs < PWM_INPUT_VALID_MIN_US ||
      capturedPulseUs > PWM_INPUT_VALID_MAX_US ||
      (uint32_t)(micros() - lastPulseMicros) > BREMOTE_PWM_TIMEOUT_US) {
    return false;
  }

  *pulseUs = constrain(capturedPulseUs, BREMOTE_PWM_MIN_US, BREMOTE_PWM_MAX_US);
  return true;
}

static void updateVescControl() {
  uint32_t nowMs = millis();
  bool srxlActive = srxlChannelDataIsFresh(nowMs);
  ControlMode controlMode = activeControlMode(nowMs);
  uint16_t output0Us = VESC_PWM_MIN_US;
  uint16_t output1Us = VESC_PWM_MIN_US;

  if (controlMode == CONTROL_MODE_BREMOTE) {
    readFreshBremotePulse(1, &output0Us);
    readFreshBremotePulse(0, &output1Us);
  } else if (controlMode == CONTROL_MODE_DXS && srxlActive) {
    uint16_t dxsPulseUs =
        mapConstrained(latestDxsCh1, CH1_MIN, CH1_MAX, VESC_PWM_MIN_US, VESC_PWM_MAX_US);
    int steeringOffsetUs = dxsSteeringOffsetUs(latestDxsCh2);
    output0Us = constrainVescPulseUs((int)dxsPulseUs - steeringOffsetUs);
    output1Us = constrainVescPulseUs((int)dxsPulseUs + steeringOffsetUs);
  }

  setVescOutputs(output0Us, output1Us);

  if ((int)controlMode != lastLoggedControlMode) {
    Serial.print("Control source=");
    Serial.println(controlModeName(controlMode));
    lastLoggedControlMode = (int)controlMode;
  }
}

static void printVescOutputs() {
  static uint32_t lastPrintMs = 0;
  uint32_t nowMs = millis();
  if ((uint32_t)(nowMs - lastPrintMs) < 200) return;

  ControlMode controlMode = activeControlMode(nowMs);
  uint16_t output0Us;
  uint16_t output1Us;
  noInterrupts();
  output0Us = vescOutputPulseUs[0];
  output1Us = vescOutputPulseUs[1];
  interrupts();

  Serial.print("src=");
  Serial.print(controlModeName(controlMode));

  if (controlMode == CONTROL_MODE_DXS) {
    Serial.print(" CH1=");
    Serial.print(latestDxsCh1);
    Serial.print(" CH2=");
    Serial.print(latestDxsCh2);
    if (!srxlChannelDataIsFresh(nowMs)) {
      Serial.print(" signal=stale");
    }
  } else if (controlMode == CONTROL_MODE_BREMOTE) {
    uint16_t input0Us = 0;
    uint16_t input1Us = 0;
    bool input0Valid = readFreshBremotePulse(0, &input0Us);
    bool input1Valid = readFreshBremotePulse(1, &input1Us);
    Serial.print(" in0=");
    if (input0Valid) Serial.print(input0Us); else Serial.print("invalid");
    Serial.print("us in1=");
    if (input1Valid) Serial.print(input1Us); else Serial.print("invalid");
    Serial.print("us");
  }

  Serial.print(" out0=");
  Serial.print(output0Us);
  Serial.print("us out1=");
  Serial.print(output1Us);
  Serial.println("us");
  lastPrintMs = nowMs;
}

static void handleBremoteEdge(uint8_t index, uint8_t pin) {
  uint32_t nowMicros = micros();
  if (digitalRead(pin) == HIGH) {
    bremoteRiseMicros[index] = nowMicros;
  } else {
    uint32_t pulseWidthUs = nowMicros - bremoteRiseMicros[index];
    if (pulseWidthUs >= PWM_INPUT_VALID_MIN_US && pulseWidthUs <= PWM_INPUT_VALID_MAX_US) {
      bremotePulseWidthUs[index] = (uint16_t)pulseWidthUs;
      bremoteLastPulseMicros[index] = nowMicros;
    }
  }
}

void bremoteInput0Isr() {
  handleBremoteEdge(0, BREMOTE_INPUT_0_PIN);
}

void bremoteInput1Isr() {
  handleBremoteEdge(1, BREMOTE_INPUT_1_PIN);
}

ISR(TIMER5_OVF_vect) {
  digitalWrite(VESC_OUTPUT_0_PIN, HIGH);
  digitalWrite(VESC_OUTPUT_1_PIN, HIGH);
  OCR5A = vescOutputPulseUs[0] * TIMER5_TICKS_PER_US;
  OCR5B = vescOutputPulseUs[1] * TIMER5_TICKS_PER_US;
}

ISR(TIMER5_COMPA_vect) {
  digitalWrite(VESC_OUTPUT_0_PIN, LOW);
}

ISR(TIMER5_COMPB_vect) {
  digitalWrite(VESC_OUTPUT_1_PIN, LOW);
}

static void initVescPwmOutputs() {
  pinMode(VESC_OUTPUT_0_PIN, OUTPUT);
  pinMode(VESC_OUTPUT_1_PIN, OUTPUT);
  digitalWrite(VESC_OUTPUT_0_PIN, LOW);
  digitalWrite(VESC_OUTPUT_1_PIN, LOW);

  noInterrupts();
  TCCR5A = _BV(WGM51);
  TCCR5B = _BV(WGM53) | _BV(WGM52) | _BV(CS51);
  TCNT5 = 0;
  ICR5 = (VESC_PWM_FRAME_US * TIMER5_TICKS_PER_US) - 1;
  OCR5A = VESC_PWM_MIN_US * TIMER5_TICKS_PER_US;
  OCR5B = VESC_PWM_MIN_US * TIMER5_TICKS_PER_US;
  TIMSK5 = _BV(TOIE5) | _BV(OCIE5A) | _BV(OCIE5B);
  interrupts();
}

static void discardRxBytes(uint16_t byteCount) {
  if (byteCount >= rxBufferIndex) {
    rxBufferIndex = 0;
    return;
  }

  rxBufferIndex -= byteCount;
  memmove(rxBuffer, &rxBuffer[byteCount], rxBufferIndex);
}

static void parseBufferedSrxlPackets() {
  while (rxBufferIndex >= 3) {
    if (rxBuffer[0] != SPEKTRUM_SRXL_ID) {
      discardRxBytes(1);
      continue;
    }

    uint8_t packetLength = rxBuffer[2];
    if (packetLength < 5 || packetLength > SRXL_MAX_BUFFER_SIZE) {
      discardRxBytes(1);
      continue;
    }

    if (rxBufferIndex < packetLength) return;

    if (srxlParsePacket(0, rxBuffer, packetLength)) {
      discardRxBytes(packetLength);
    } else {
      discardRxBytes(1);
    }
  }
}

// ---------------------------------------------------------------------
// setup()
// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }

  pinMode(BREMOTE_INPUT_0_PIN, INPUT);
  pinMode(BREMOTE_INPUT_1_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(BREMOTE_INPUT_0_PIN), bremoteInput0Isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(BREMOTE_INPUT_1_PIN), bremoteInput1Isr, CHANGE);

  initVescPwmOutputs();

  // Init one-wire UART used by SRXL2 (on pin 49 via uart.cpp)
  uartInit(UART_HANDLE, 115200);

  uint32_t uniqueID = SERIAL_NUM;
  if (!srxlInitDevice(SRXL_DEVICE_ID, SRXL_DEVICE_PRIORITY, SRXL_DEVICE_INFO, uniqueID)) {
    Serial.println("SRXL device init FAILED");
    while (1) { }
  }

  if (!srxlInitBus(0, UART_HANDLE, SRXL_SUPPORTED_BAUD_RATES)) {
    Serial.println("SRXL bus init FAILED");
    while (1) { }
  }

  Serial.println("Init done: dual VESC PWM router.");
  Serial.println("BRemote inputs: D21 -> out1, D20 -> out0.");
  Serial.println("VESC outputs: D6 -> VESC0, D8 -> VESC1.");
  Serial.println("CH5: low=DXS CH1 throttle + CH2 steering, center=zero throttle, high=BRemote passthrough.");
  Serial.println("SRXL loss or startup without DXS: BRemote passthrough.");
}

// ---------------------------------------------------------------------
// loop()
// ---------------------------------------------------------------------
void loop() {
  uint8_t bytesToRead = min((uint16_t)SRXL_MAX_BUFFER_SIZE,
                            (uint16_t)(sizeof(rxBuffer) - rxBufferIndex));
  uint8_t bytesReceived =
      uartReceiveBytes(UART_HANDLE, &rxBuffer[rxBufferIndex], bytesToRead, 5);

  if (bytesReceived) {
    rxBufferIndex += bytesReceived;
    parseBufferedSrxlPackets();
  } else {
    srxlRun(0, 5);
  }

  updateVescControl();
  printVescOutputs();
}

// ---------------------------------------------------------------------
// SRXL callbacks
// ---------------------------------------------------------------------
void userProvidedFillSrxlTelemetry(SrxlTelemetryData* pTelemetry) {
  memset(pTelemetry->raw, 0, sizeof(pTelemetry->raw));
}

void userProvidedReceivedChannelData(SrxlChannelData* pChannelData, bool isFailsafeData) {
  (void)pChannelData;

  srxlFailsafeActive = isFailsafeData;
  if (!isFailsafeData) {
    srxlHasValidFrame = true;
    srxlLastChannelDataMs = millis();
    latestDxsCh1 = (int)(srxlChData.values[0] >> 4);
    latestDxsCh2 = (int)(srxlChData.values[1] >> 4);
    latestDxsCh5 = (int)(srxlChData.values[4] >> 4);
  }
}

void userProvidedHandleVtxData(SrxlVtxData* pVtxData) {
  (void)pVtxData;
}
