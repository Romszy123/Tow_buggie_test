#include <Arduino.h>
#include <due_can.h>

#if !defined(__SAM3X8E__)
#error "This sketch targets the Arduino Due / SAM3X8E."
#endif

// First bring-up build: leave motor commands disabled until CAN wiring,
// VESC IDs, VESC baud rate, and failsafe behavior have been tested.
const bool ENABLE_VESC_COMMANDS = false;

// CAN bus assignment.
// CAN0: Flipsky/VESC bus, using the Due CANRX/CANTX pins through a 3.3 V CAN transceiver.
// CAN1: JK BMS bus, using Due DAC0 as CAN1 RX and D53 as CAN1 TX through a second transceiver.
const uint32_t VESC_CAN_BAUD = CAN_BPS_500K;
const uint32_t JK_BMS_CAN_BAUD = CAN_BPS_250K;

// VESC IDs configured in VESC Tool.
const uint8_t VESC_LEFT_ID = 1;
const uint8_t VESC_RIGHT_ID = 2;

// Flipsky/VESC command IDs from VESC CAN Protocol V1.0.
enum VescCanPacketId : uint8_t {
  CAN_PACKET_SET_CURRENT_REL = 0x0A,
  CAN_PACKET_STATUS = 0x09,
  CAN_PACKET_STATUS_4 = 0x10,
  CAN_PACKET_STATUS_5 = 0x1B
};

// JK BMS CAN Protocol V2.1 IDs. The default BMS address suffix is F4.
const uint32_t JK_BATT_ST1_ID = 0x02F4;      // Standard: voltage, current, SOC.
const uint32_t JK_CELL_TEMP_ID = 0x05F4;     // Standard: max/min/average temp.
const uint32_t JK_ALM_INFO_ID = 0x07F4;      // Standard: alarm levels, event based.
const uint32_t JK_BATT_ST2_ID = 0x18F128F4;  // Extended: capacity details.
const uint32_t JK_BMSERR_INFO_ID = 0x18F328F4;

const uint32_t VESC_COMMAND_PERIOD_MS = 50;
const uint32_t PRINT_PERIOD_MS = 500;
const uint32_t BREMOTE_TELEMETRY_PERIOD_MS = 500;
const uint32_t BMS_STALE_MS = 1500;
const uint32_t VESC_STALE_MS = 1500;
const uint32_t ELRS_BAUD = 420000;
const uint32_t ELRS_STALE_MS = 250;

// CRSF channel assignments are intentionally easy to change after the
// transmitter mapping is identified from the serial monitor.
const uint8_t ELRS_THROTTLE_CHANNEL = 1;
const uint8_t ELRS_STEERING_CHANNEL = 2;
const uint8_t ELRS_SOURCE_SELECT_CHANNEL = 5;

const uint8_t CRSF_FRAMETYPE_LINK_STATISTICS = 0x14;
const uint8_t CRSF_FRAMETYPE_RC_CHANNELS_PACKED = 0x16;
const uint8_t CRSF_ADDRESS_FLIGHT_CONTROLLER = 0xC8;
const uint8_t CRSF_MAX_FRAME_SIZE = 64;
const uint8_t CRSF_CHANNEL_COUNT = 16;
const uint16_t CRSF_CHANNEL_MIN = 172;
const uint16_t CRSF_CHANNEL_MAX = 1811;

struct VescStatus {
  bool seen = false;
  uint32_t lastMs = 0;
  float rpm = 0.0f;
  float motorCurrentA = 0.0f;
  float duty = 0.0f;
  float fetTempC = 0.0f;
  float motorTempC = 0.0f;
  float inputCurrentA = 0.0f;
  float inputVoltageV = 0.0f;
};

struct BmsStatus {
  bool seen = false;
  uint32_t lastBattMs = 0;
  uint32_t lastTempMs = 0;
  uint32_t lastAlarmMs = 0;
  float packVoltageV = 0.0f;
  float packCurrentA = 0.0f;
  uint8_t socPercent = 0;
  int8_t maxTempC = 0;
  int8_t minTempC = 0;
  int8_t avgTempC = 0;
  uint8_t maxAlarmLevel = 0;
  uint32_t faultBits = 0;
};

struct ElrsStatus {
  bool seenChannels = false;
  uint32_t lastChannelsMs = 0;
  uint32_t validFrames = 0;
  uint32_t crcErrors = 0;
  uint16_t channelRaw[CRSF_CHANNEL_COUNT] = {0};
  uint16_t channelUs[CRSF_CHANNEL_COUNT] = {0};
  uint8_t uplinkLinkQuality = 0;
  int8_t uplinkSnr = 0;
  uint8_t activeAntenna = 0;
};

VescStatus vescLeft;
VescStatus vescRight;
BmsStatus bms;
ElrsStatus elrs;

uint32_t lastVescCommandMs = 0;
uint32_t lastPrintMs = 0;
uint32_t lastBremoteTelemetryMs = 0;

uint8_t crsfFrame[CRSF_MAX_FRAME_SIZE] = {0};
uint8_t crsfFrameIndex = 0;
uint8_t crsfFrameLength = 0;

static uint16_t readU16LE(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t readU32LE(const uint8_t *data) {
  return (uint32_t)data[0] |
         ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) |
         ((uint32_t)data[3] << 24);
}

static int16_t readI16BE(const uint8_t *data) {
  return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static int32_t readI32BE(const uint8_t *data) {
  return (int32_t)(((uint32_t)data[0] << 24) |
                   ((uint32_t)data[1] << 16) |
                   ((uint32_t)data[2] << 8) |
                   data[3]);
}

static void writeI32BE(uint8_t *data, int32_t value) {
  data[0] = (uint8_t)(value >> 24);
  data[1] = (uint8_t)(value >> 16);
  data[2] = (uint8_t)(value >> 8);
  data[3] = (uint8_t)value;
}

static void writeU16LE(uint8_t *data, uint16_t value) {
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static uint16_t crc16Ccitt(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

static uint8_t crc8DvbS2(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static uint16_t crsfRawToMicroseconds(uint16_t raw) {
  raw = constrain(raw, CRSF_CHANNEL_MIN, CRSF_CHANNEL_MAX);
  return (uint16_t)map(raw, CRSF_CHANNEL_MIN, CRSF_CHANNEL_MAX, 988, 2012);
}

static void decodeCrsfChannels(const uint8_t *payload) {
  uint32_t bitBuffer = 0;
  uint8_t bitsAvailable = 0;
  uint8_t payloadIndex = 0;

  for (uint8_t channel = 0; channel < CRSF_CHANNEL_COUNT; channel++) {
    while (bitsAvailable < 11) {
      bitBuffer |= (uint32_t)payload[payloadIndex++] << bitsAvailable;
      bitsAvailable += 8;
    }

    uint16_t raw = bitBuffer & 0x07FF;
    bitBuffer >>= 11;
    bitsAvailable -= 11;
    elrs.channelRaw[channel] = raw;
    elrs.channelUs[channel] = crsfRawToMicroseconds(raw);
  }

  elrs.seenChannels = true;
  elrs.lastChannelsMs = millis();
}

static void processCrsfFrame() {
  const uint8_t type = crsfFrame[2];
  const uint8_t payloadLength = crsfFrameLength - 2;
  const uint8_t *payload = &crsfFrame[3];

  if (type == CRSF_FRAMETYPE_RC_CHANNELS_PACKED && payloadLength == 22) {
    decodeCrsfChannels(payload);
  } else if (type == CRSF_FRAMETYPE_LINK_STATISTICS && payloadLength >= 10) {
    elrs.uplinkLinkQuality = payload[2];
    elrs.uplinkSnr = (int8_t)payload[3];
    elrs.activeAntenna = payload[4];
  }
}

static void consumeCrsfByte(uint8_t value) {
  if (crsfFrameIndex == 0) {
    if (value != CRSF_ADDRESS_FLIGHT_CONTROLLER) {
      return;
    }
    crsfFrame[crsfFrameIndex++] = value;
    return;
  }

  if (crsfFrameIndex == 1) {
    if (value < 2 || value > CRSF_MAX_FRAME_SIZE - 2) {
      crsfFrameIndex = 0;
      crsfFrameLength = 0;
      return;
    }
    crsfFrameLength = value;
    crsfFrame[crsfFrameIndex++] = value;
    return;
  }

  crsfFrame[crsfFrameIndex++] = value;
  const uint8_t totalFrameSize = crsfFrameLength + 2;
  if (crsfFrameIndex < totalFrameSize) {
    return;
  }

  const uint8_t receivedCrc = crsfFrame[totalFrameSize - 1];
  const uint8_t calculatedCrc = crc8DvbS2(&crsfFrame[2], crsfFrameLength - 1);
  if (receivedCrc == calculatedCrc) {
    elrs.validFrames++;
    processCrsfFrame();
  } else {
    elrs.crcErrors++;
  }

  crsfFrameIndex = 0;
  crsfFrameLength = 0;
}

static void pollElrs() {
  while (Serial2.available() > 0) {
    consumeCrsfByte((uint8_t)Serial2.read());
  }
}

static bool elrsFresh() {
  return elrs.seenChannels && (millis() - elrs.lastChannelsMs <= ELRS_STALE_MS);
}

static uint16_t elrsChannelUs(uint8_t oneBasedChannel) {
  if (oneBasedChannel < 1 || oneBasedChannel > CRSF_CHANNEL_COUNT) {
    return 1500;
  }
  return elrs.channelUs[oneBasedChannel - 1];
}

static VescStatus *statusForVescId(uint8_t id) {
  if (id == VESC_LEFT_ID) {
    return &vescLeft;
  }
  if (id == VESC_RIGHT_ID) {
    return &vescRight;
  }
  return nullptr;
}

static void sendVescCurrentRel(uint8_t vescId, float currentRel) {
  currentRel = constrain(currentRel, -1.0f, 1.0f);
  int32_t scaled = (int32_t)(currentRel * 100000.0f);

  CAN_FRAME frame;
  frame.id = ((uint32_t)CAN_PACKET_SET_CURRENT_REL << 8) | vescId;
  frame.extended = true;
  frame.length = 4;
  writeI32BE(frame.data.bytes, scaled);

  Can0.sendFrame(frame);
}

static void processVescFrame(const CAN_FRAME &frame) {
  if (!frame.extended) {
    return;
  }

  uint8_t vescId = frame.id & 0xFF;
  uint8_t command = (frame.id >> 8) & 0xFF;
  VescStatus *status = statusForVescId(vescId);
  if (status == nullptr) {
    return;
  }

  status->seen = true;
  status->lastMs = millis();

  if (command == CAN_PACKET_STATUS && frame.length >= 8) {
    status->rpm = (float)readI32BE(&frame.data.bytes[0]);
    status->motorCurrentA = (float)readI16BE(&frame.data.bytes[4]) / 10.0f;
    status->duty = (float)readI16BE(&frame.data.bytes[6]) / 1000.0f;
  } else if (command == CAN_PACKET_STATUS_4 && frame.length >= 8) {
    status->fetTempC = (float)readI16BE(&frame.data.bytes[0]) / 10.0f;
    status->motorTempC = (float)readI16BE(&frame.data.bytes[2]) / 10.0f;
    status->inputCurrentA = (float)readI16BE(&frame.data.bytes[4]) / 10.0f;
  } else if (command == CAN_PACKET_STATUS_5 && frame.length >= 6) {
    status->inputVoltageV = (float)readI16BE(&frame.data.bytes[4]) / 10.0f;
  }
}

static uint8_t maxTwoBitAlarmLevel(uint32_t alarmField) {
  uint8_t maxLevel = 0;
  for (uint8_t bit = 0; bit < 30; bit += 2) {
    maxLevel = max(maxLevel, (uint8_t)((alarmField >> bit) & 0x03));
  }
  return maxLevel;
}

static void processBmsFrame(const CAN_FRAME &frame) {
  uint32_t id = frame.id;
  uint32_t now = millis();

  if (!frame.extended && id == JK_BATT_ST1_ID && frame.length >= 5) {
    uint16_t rawVoltage = readU16LE(&frame.data.bytes[0]);
    uint16_t rawCurrent = readU16LE(&frame.data.bytes[2]);

    bms.packVoltageV = (float)rawVoltage * 0.1f;
    bms.packCurrentA = ((float)rawCurrent * 0.1f) - 400.0f;
    bms.socPercent = frame.data.bytes[4];
    bms.seen = true;
    bms.lastBattMs = now;
  } else if (!frame.extended && id == JK_CELL_TEMP_ID && frame.length >= 5) {
    bms.maxTempC = (int8_t)frame.data.bytes[0] - 50;
    bms.minTempC = (int8_t)frame.data.bytes[2] - 50;
    bms.avgTempC = (int8_t)frame.data.bytes[4] - 50;
    bms.lastTempMs = now;
  } else if (!frame.extended && id == JK_ALM_INFO_ID && frame.length >= 4) {
    uint32_t alarms = readU32LE(&frame.data.bytes[0]);
    bms.maxAlarmLevel = maxTwoBitAlarmLevel(alarms);
    bms.lastAlarmMs = now;
  } else if (frame.extended && id == JK_BMSERR_INFO_ID && frame.length >= 4) {
    bms.faultBits = readU32LE(&frame.data.bytes[0]);
  }
}

static void pollCan() {
  CAN_FRAME frame;

  while (Can0.available() > 0) {
    Can0.read(frame);
    processVescFrame(frame);
  }

  while (Can1.available() > 0) {
    Can1.read(frame);
    processBmsFrame(frame);
  }
}

static bool bmsFresh() {
  return bms.seen && (millis() - bms.lastBattMs <= BMS_STALE_MS);
}

static void sendBremoteTelemetry() {
  // Packet format for a matching BRemote RX parser:
  // A5 5A, version, flags, pack_mV u16, pack_current_dA i16, SOC u8,
  // avg_temp_C i8, alarm_level u8, crc16-ccitt little-endian.
  uint8_t packet[13] = {0};
  packet[0] = 0xA5;
  packet[1] = 0x5A;
  packet[2] = 1;
  packet[3] = bmsFresh() ? 0x01 : 0x00;

  uint16_t packMv = bmsFresh() ? (uint16_t)constrain((int)(bms.packVoltageV * 1000.0f), 0, 65535) : 0;
  int16_t packCurrentDa = bmsFresh() ? (int16_t)constrain((int)(bms.packCurrentA * 10.0f), -32768, 32767) : 0;
  writeU16LE(&packet[4], packMv);
  writeU16LE(&packet[6], (uint16_t)packCurrentDa);
  packet[8] = bmsFresh() ? bms.socPercent : 0xFF;
  packet[9] = bmsFresh() ? (uint8_t)bms.avgTempC : 0x80;
  packet[10] = bms.maxAlarmLevel;

  uint16_t crc = crc16Ccitt(packet, 11);
  writeU16LE(&packet[11], crc);
  Serial1.write(packet, sizeof(packet));
}

static void sendSafeVescCommands() {
  if (!ENABLE_VESC_COMMANDS) {
    return;
  }

  // Keep zero commands during receiver bring-up. Once channel assignments and
  // failsafe behavior are verified, map ELRS throttle/steering and the BRemote
  // fallback into these two relative-current commands.
  sendVescCurrentRel(VESC_LEFT_ID, 0.0f);
  sendVescCurrentRel(VESC_RIGHT_ID, 0.0f);
}

static void printOneVesc(const char *label, const VescStatus &status) {
  Serial.print(label);
  if (!status.seen || millis() - status.lastMs > VESC_STALE_MS) {
    Serial.print(" stale");
    return;
  }

  Serial.print(" rpm=");
  Serial.print(status.rpm, 0);
  Serial.print(" motorA=");
  Serial.print(status.motorCurrentA, 1);
  Serial.print(" duty=");
  Serial.print(status.duty, 3);
  Serial.print(" vin=");
  Serial.print(status.inputVoltageV, 1);
  Serial.print(" tempFET=");
  Serial.print(status.fetTempC, 1);
}

static void printStatus() {
  Serial.print("ELRS ");
  if (elrsFresh()) {
    Serial.print("fresh LQ=");
    Serial.print(elrs.uplinkLinkQuality);
    Serial.print("% SNR=");
    Serial.print(elrs.uplinkSnr);
    Serial.print(" map(thr/steer/src)=");
    Serial.print(elrsChannelUs(ELRS_THROTTLE_CHANNEL));
    Serial.print("/");
    Serial.print(elrsChannelUs(ELRS_STEERING_CHANNEL));
    Serial.print("/");
    Serial.print(elrsChannelUs(ELRS_SOURCE_SELECT_CHANNEL));
    Serial.print("us");
  } else {
    Serial.print(elrs.seenChannels ? "stale" : "waiting");
  }
  Serial.print(" frames=");
  Serial.print(elrs.validFrames);
  Serial.print(" crcErr=");
  Serial.print(elrs.crcErrors);
  Serial.println();

  if (elrs.seenChannels) {
    Serial.print("CRSF channels:");
    for (uint8_t channel = 0; channel < CRSF_CHANNEL_COUNT; channel++) {
      Serial.print(" CH");
      Serial.print(channel + 1);
      Serial.print("=");
      Serial.print(elrs.channelRaw[channel]);
      Serial.print("/");
      Serial.print(elrs.channelUs[channel]);
      Serial.print("us");
    }
    Serial.println();
  }

  Serial.print("BMS ");
  if (bmsFresh()) {
    Serial.print(bms.packVoltageV, 1);
    Serial.print("V ");
    Serial.print(bms.packCurrentA, 1);
    Serial.print("A SOC=");
    Serial.print(bms.socPercent);
    Serial.print("% avgTemp=");
    Serial.print(bms.avgTempC);
    Serial.print("C alarm=");
    Serial.print(bms.maxAlarmLevel);
  } else {
    Serial.print("stale");
  }

  Serial.print(" | ");
  printOneVesc("VESC L", vescLeft);
  Serial.print(" | ");
  printOneVesc("VESC R", vescRight);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);  // Due D18 TX1 / D19 RX1 to BRemote U1-0 UART.
  Serial2.begin(ELRS_BAUD);  // Due D16 TX2 / D17 RX2 to ELRS CRSF UART.

  while (!Serial && millis() < 3000) {
    delay(10);
  }

  Can0.begin(VESC_CAN_BAUD);
  Can1.begin(JK_BMS_CAN_BAUD);

  Serial.println("Tow Buggie Due CAN + ELRS CRSF bring-up.");
  Serial.println("CAN0: VESC, CAN1: JK BMS, Serial1: BRemote, Serial2: ELRS CRSF.");
  Serial.println("ELRS wiring: receiver TX -> D17/RX2, receiver RX -> D16/TX2.");
  Serial.println(ENABLE_VESC_COMMANDS ? "VESC commands enabled." : "VESC commands disabled for safe bring-up.");
}

void loop() {
  uint32_t now = millis();
  pollCan();
  pollElrs();

  if (now - lastVescCommandMs >= VESC_COMMAND_PERIOD_MS) {
    lastVescCommandMs = now;
    sendSafeVescCommands();
  }

  if (now - lastBremoteTelemetryMs >= BREMOTE_TELEMETRY_PERIOD_MS) {
    lastBremoteTelemetryMs = now;
    sendBremoteTelemetry();
  }

  if (now - lastPrintMs >= PRINT_PERIOD_MS) {
    lastPrintMs = now;
    printStatus();
  }
}
