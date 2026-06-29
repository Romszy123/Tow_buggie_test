#include <Arduino.h>
#include <due_can.h>
#include <SPI.h>
#include <SD.h>

#if !defined(__SAM3X8E__)
#error "This sketch targets the Arduino Due / SAM3X8E."
#endif

// First bring-up build: leave motor commands disabled until VESC input
// configuration, wiring, channel mapping, and failsafe behavior are tested.
const bool ENABLE_VESC_COMMANDS = false;

// Flipsky UART Protocol V1.0: 115200 baud, 8 data bits, no parity, 1 stop bit.
// Left VESC: Serial on D1/TX0 and D0/RX0.
// Right VESC: Serial3 on D14/TX3 and D15/RX3.
// Diagnostics use SerialUSB through the Native USB port.
const uint32_t VESC_UART_BAUD = 115200;
const uint32_t VESC_UART_TELEMETRY_PERIOD_MS = 500;
const uint8_t VESC_UART_MAX_PAYLOAD = 96;
const float VESC_UART_MAX_DUTY = 1.0f;
const uint32_t VESC_UART_PACKET_TIMEOUT_MS = 100;

// microSD module on the Due SPI header. Only chip select uses a digital pin.
const uint8_t SD_CS_PIN = 4;
const uint32_t SD_LOG_PERIOD_MS = 200;
const uint32_t SD_FLUSH_PERIOD_MS = 2000;

// CAN1 remains dedicated to the JK BMS.
const uint32_t JK_BMS_CAN_BAUD = CAN_BPS_250K;

enum VescUartCommand : uint8_t {
  COMM_GET_VALUES = 4,
  COMM_SET_DUTY = 5
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
  uint8_t faultCode = 0;
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
uint32_t lastVescUartTelemetryMs = 0;
uint32_t lastPrintMs = 0;
uint32_t lastBremoteTelemetryMs = 0;
float lastVescLeftCommand = 0.0f;
float lastVescRightCommand = 0.0f;

enum VescUartRxState : uint8_t {
  VESC_UART_WAIT_START,
  VESC_UART_WAIT_LENGTH,
  VESC_UART_READ_PAYLOAD,
  VESC_UART_READ_CRC_HIGH,
  VESC_UART_READ_CRC_LOW,
  VESC_UART_WAIT_END
};

struct VescUartEndpoint {
  Stream *port = nullptr;
  VescStatus *status = nullptr;
  VescUartRxState rxState = VESC_UART_WAIT_START;
  uint8_t payload[VESC_UART_MAX_PAYLOAD] = {0};
  uint8_t payloadLength = 0;
  uint8_t payloadIndex = 0;
  uint16_t receivedCrc = 0;
  uint32_t lastByteMs = 0;
  uint32_t goodPackets = 0;
  uint32_t crcErrors = 0;
  uint32_t formatErrors = 0;
  uint32_t timeouts = 0;
};

VescUartEndpoint vescLeftUart;
VescUartEndpoint vescRightUart;

uint8_t crsfFrame[CRSF_MAX_FRAME_SIZE] = {0};
uint8_t crsfFrameIndex = 0;
uint8_t crsfFrameLength = 0;

File logFile;
char logFileName[13] = {0};
bool sdLogReady = false;
uint32_t lastSdLogMs = 0;
uint32_t lastSdFlushMs = 0;
uint32_t sdWriteErrors = 0;
uint32_t bmsFrameCount = 0;
uint32_t lastBmsFrameMs = 0;

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

static uint16_t vescCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0;
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

static void sendVescUartPayload(VescUartEndpoint &endpoint,
                                const uint8_t *payload,
                                uint8_t length) {
  if (endpoint.port == nullptr || length == 0 || length > VESC_UART_MAX_PAYLOAD) {
    return;
  }

  uint16_t crc = vescCrc16(payload, length);
  endpoint.port->write((uint8_t)0x02);
  endpoint.port->write(length);
  endpoint.port->write(payload, length);
  endpoint.port->write((uint8_t)(crc >> 8));
  endpoint.port->write((uint8_t)crc);
  endpoint.port->write((uint8_t)0x03);
}

static void sendVescUartDuty(VescUartEndpoint &endpoint, float command) {
  command = constrain(command, 0.0f, 1.0f);
  int32_t scaledDuty = (int32_t)(command * VESC_UART_MAX_DUTY * 100000.0f);
  uint8_t payload[5] = {COMM_SET_DUTY, 0, 0, 0, 0};
  writeI32BE(&payload[1], scaledDuty);
  sendVescUartPayload(endpoint, payload, sizeof(payload));
}

static void requestVescUartValues(VescUartEndpoint &endpoint) {
  const uint8_t payload[1] = {COMM_GET_VALUES};
  sendVescUartPayload(endpoint, payload, sizeof(payload));
}

static void processVescUartPayload(VescUartEndpoint &endpoint,
                                   const uint8_t *payload,
                                   uint8_t length) {
  // COMM_GET_VALUES fields through input voltage occupy 29 payload bytes.
  if (endpoint.status == nullptr || length < 29 || payload[0] != COMM_GET_VALUES) {
    return;
  }

  VescStatus *status = endpoint.status;
  status->fetTempC = (float)readI16BE(&payload[1]) / 10.0f;
  status->motorTempC = (float)readI16BE(&payload[3]) / 10.0f;
  status->motorCurrentA = (float)readI32BE(&payload[5]) / 100.0f;
  status->inputCurrentA = (float)readI32BE(&payload[9]) / 100.0f;
  status->duty = (float)readI16BE(&payload[21]) / 1000.0f;
  status->rpm = (float)readI32BE(&payload[23]);
  status->inputVoltageV = (float)readI16BE(&payload[27]) / 10.0f;
  if (length > 53) {
    status->faultCode = payload[53];
  }
  status->seen = true;
  status->lastMs = millis();
  endpoint.goodPackets++;
}

static void resetVescUartRx(VescUartEndpoint &endpoint) {
  endpoint.rxState = VESC_UART_WAIT_START;
  endpoint.payloadLength = 0;
  endpoint.payloadIndex = 0;
  endpoint.receivedCrc = 0;
}

static void consumeVescUartByte(VescUartEndpoint &endpoint, uint8_t value) {
  endpoint.lastByteMs = millis();

  switch (endpoint.rxState) {
    case VESC_UART_WAIT_START:
      if (value == 0x02) {
        endpoint.rxState = VESC_UART_WAIT_LENGTH;
      }
      break;

    case VESC_UART_WAIT_LENGTH:
      if (value == 0 || value > VESC_UART_MAX_PAYLOAD) {
        endpoint.formatErrors++;
        resetVescUartRx(endpoint);
      } else {
        endpoint.payloadLength = value;
        endpoint.payloadIndex = 0;
        endpoint.rxState = VESC_UART_READ_PAYLOAD;
      }
      break;

    case VESC_UART_READ_PAYLOAD:
      endpoint.payload[endpoint.payloadIndex++] = value;
      if (endpoint.payloadIndex >= endpoint.payloadLength) {
        endpoint.rxState = VESC_UART_READ_CRC_HIGH;
      }
      break;

    case VESC_UART_READ_CRC_HIGH:
      endpoint.receivedCrc = (uint16_t)value << 8;
      endpoint.rxState = VESC_UART_READ_CRC_LOW;
      break;

    case VESC_UART_READ_CRC_LOW:
      endpoint.receivedCrc |= value;
      endpoint.rxState = VESC_UART_WAIT_END;
      break;

    case VESC_UART_WAIT_END:
      if (value != 0x03) {
        endpoint.formatErrors++;
      } else if (vescCrc16(endpoint.payload, endpoint.payloadLength) != endpoint.receivedCrc) {
        endpoint.crcErrors++;
      } else {
        processVescUartPayload(endpoint, endpoint.payload, endpoint.payloadLength);
      }
      resetVescUartRx(endpoint);
      break;
  }
}

static void pollVescUart(VescUartEndpoint &endpoint) {
  if (endpoint.port == nullptr) {
    return;
  }

  if (endpoint.rxState != VESC_UART_WAIT_START &&
      millis() - endpoint.lastByteMs > VESC_UART_PACKET_TIMEOUT_MS) {
    endpoint.timeouts++;
    resetVescUartRx(endpoint);
  }

  while (endpoint.port->available() > 0) {
    consumeVescUartByte(endpoint, (uint8_t)endpoint.port->read());
  }
}

static void setVescOutputs(float leftCommand, float rightCommand) {
  leftCommand = constrain(leftCommand, 0.0f, 1.0f);
  rightCommand = constrain(rightCommand, 0.0f, 1.0f);
  lastVescLeftCommand = leftCommand;
  lastVescRightCommand = rightCommand;

  sendVescUartDuty(vescLeftUart, leftCommand);
  sendVescUartDuty(vescRightUart, rightCommand);
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
  bmsFrameCount++;
  lastBmsFrameMs = now;

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

  while (Can1.available() > 0) {
    Can1.read(frame);
    processBmsFrame(frame);
  }
}

static bool bmsFresh() {
  return bms.seen && (millis() - bms.lastBattMs <= BMS_STALE_MS);
}

static uint32_t dataAgeMs(bool seen, uint32_t lastMs, uint32_t now) {
  return seen ? now - lastMs : UINT32_MAX;
}

static void initSdLogging() {
  if (logFile) {
    logFile.close();
  }
  sdLogReady = false;
  logFileName[0] = '\0';

  pinMode(SD_CS_PIN, OUTPUT);
  if (!SD.begin(SD_CS_PIN)) {
    SerialUSB.println("SD logging unavailable: card or wiring not detected.");
    return;
  }

  for (uint16_t index = 0; index < 10000; index++) {
    snprintf(logFileName, sizeof(logFileName), "LOG%04u.CSV", index);
    if (!SD.exists(logFileName)) {
      logFile = SD.open(logFileName, FILE_WRITE);
      break;
    }
  }

  if (!logFile) {
    SerialUSB.println("SD logging unavailable: no log file could be created.");
    return;
  }

  logFile.println(
      "ms,elrs_fresh,elrs_age_ms,elrs_frames,elrs_crc,"
      "left_cmd,right_cmd,"
      "left_age_ms,left_rpm,left_motor_A,left_input_A,left_duty,left_vin_V,"
      "left_fet_C,left_motor_C,left_fault,left_uart_ok,left_uart_crc,left_uart_fmt,left_uart_timeout,"
      "right_age_ms,right_rpm,right_motor_A,right_input_A,right_duty,right_vin_V,"
      "right_fet_C,right_motor_C,right_fault,right_uart_ok,right_uart_crc,right_uart_fmt,right_uart_timeout,"
      "bms_fresh,bms_age_ms,bms_frames,bms_V,bms_A,bms_soc,bms_max_C,bms_min_C,bms_avg_C,"
      "bms_alarm,bms_fault_bits,sd_errors");
  logFile.flush();
  sdLogReady = true;
  lastSdFlushMs = millis();

  SerialUSB.print("SD logging active: ");
  SerialUSB.println(logFileName);
}

static void pollUsbCommands() {
  while (SerialUSB.available() > 0) {
    char command = (char)SerialUSB.read();
    if (command == 'E' || command == 'e') {
      if (sdLogReady) {
        logFile.flush();
        logFile.close();
        sdLogReady = false;
        SerialUSB.println("SD log closed. The card can now be removed.");
      } else {
        SerialUSB.println("SD logging is already offline.");
      }
    } else if (command == 'R' || command == 'r') {
      if (sdLogReady) {
        SerialUSB.print("SD logging already active: ");
        SerialUSB.println(logFileName);
      } else {
        initSdLogging();
      }
    }
  }
}

static void writeSdLogRecord(uint32_t now) {
  if (!sdLogReady) {
    return;
  }

  logFile.print(now);
  logFile.print(',');
  logFile.print(elrsFresh() ? 1 : 0);
  logFile.print(',');
  logFile.print(dataAgeMs(elrs.seenChannels, elrs.lastChannelsMs, now));
  logFile.print(',');
  logFile.print(elrs.validFrames);
  logFile.print(',');
  logFile.print(elrs.crcErrors);
  logFile.print(',');
  logFile.print(lastVescLeftCommand, 4);
  logFile.print(',');
  logFile.print(lastVescRightCommand, 4);

  VescStatus *statuses[2] = {&vescLeft, &vescRight};
  VescUartEndpoint *endpoints[2] = {&vescLeftUart, &vescRightUart};
  for (uint8_t index = 0; index < 2; index++) {
    const VescStatus &status = *statuses[index];
    const VescUartEndpoint &endpoint = *endpoints[index];
    logFile.print(',');
    logFile.print(dataAgeMs(status.seen, status.lastMs, now));
    logFile.print(',');
    logFile.print(status.rpm, 0);
    logFile.print(',');
    logFile.print(status.motorCurrentA, 2);
    logFile.print(',');
    logFile.print(status.inputCurrentA, 2);
    logFile.print(',');
    logFile.print(status.duty, 4);
    logFile.print(',');
    logFile.print(status.inputVoltageV, 2);
    logFile.print(',');
    logFile.print(status.fetTempC, 1);
    logFile.print(',');
    logFile.print(status.motorTempC, 1);
    logFile.print(',');
    logFile.print(status.faultCode);
    logFile.print(',');
    logFile.print(endpoint.goodPackets);
    logFile.print(',');
    logFile.print(endpoint.crcErrors);
    logFile.print(',');
    logFile.print(endpoint.formatErrors);
    logFile.print(',');
    logFile.print(endpoint.timeouts);
  }

  logFile.print(',');
  logFile.print(bmsFresh() ? 1 : 0);
  logFile.print(',');
  logFile.print(dataAgeMs(bmsFrameCount > 0, lastBmsFrameMs, now));
  logFile.print(',');
  logFile.print(bmsFrameCount);
  logFile.print(',');
  logFile.print(bms.packVoltageV, 2);
  logFile.print(',');
  logFile.print(bms.packCurrentA, 2);
  logFile.print(',');
  logFile.print(bms.socPercent);
  logFile.print(',');
  logFile.print(bms.maxTempC);
  logFile.print(',');
  logFile.print(bms.minTempC);
  logFile.print(',');
  logFile.print(bms.avgTempC);
  logFile.print(',');
  logFile.print(bms.maxAlarmLevel);
  logFile.print(',');
  logFile.print(bms.faultBits, HEX);
  logFile.print(',');
  logFile.println(sdWriteErrors);

  if (logFile.getWriteError()) {
    sdWriteErrors++;
    logFile.clearWriteError();
  }
}

static void serviceSdLogging(uint32_t now) {
  if (!sdLogReady) {
    return;
  }

  if (now - lastSdLogMs >= SD_LOG_PERIOD_MS) {
    lastSdLogMs = now;
    writeSdLogRecord(now);
  }

  if (now - lastSdFlushMs >= SD_FLUSH_PERIOD_MS) {
    lastSdFlushMs = now;
    logFile.flush();
  }
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
    // Do not transmit motor control packets until explicitly enabled.
    lastVescLeftCommand = 0.0f;
    lastVescRightCommand = 0.0f;
    return;
  }

  // Keep zero commands during receiver bring-up. Once channel assignments and
  // failsafe behavior are verified, map ELRS throttle/steering and the BRemote
  // fallback into these two normalized forward commands.
  setVescOutputs(0.0f, 0.0f);
}

static void printOneVesc(const char *label, const VescStatus &status) {
  SerialUSB.print(label);
  if (!status.seen || millis() - status.lastMs > VESC_STALE_MS) {
    SerialUSB.print(" stale");
    return;
  }

  SerialUSB.print(" rpm=");
  SerialUSB.print(status.rpm, 0);
  SerialUSB.print(" motorA=");
  SerialUSB.print(status.motorCurrentA, 1);
  SerialUSB.print(" duty=");
  SerialUSB.print(status.duty, 3);
  SerialUSB.print(" vin=");
  SerialUSB.print(status.inputVoltageV, 1);
  SerialUSB.print(" tempFET=");
  SerialUSB.print(status.fetTempC, 1);
  SerialUSB.print(" fault=");
  SerialUSB.print(status.faultCode);
}

static void printStatus() {
  SerialUSB.print("ELRS ");
  if (elrsFresh()) {
    SerialUSB.print("fresh LQ=");
    SerialUSB.print(elrs.uplinkLinkQuality);
    SerialUSB.print("% SNR=");
    SerialUSB.print(elrs.uplinkSnr);
    SerialUSB.print(" map(thr/steer/src)=");
    SerialUSB.print(elrsChannelUs(ELRS_THROTTLE_CHANNEL));
    SerialUSB.print("/");
    SerialUSB.print(elrsChannelUs(ELRS_STEERING_CHANNEL));
    SerialUSB.print("/");
    SerialUSB.print(elrsChannelUs(ELRS_SOURCE_SELECT_CHANNEL));
    SerialUSB.print("us");
  } else {
    SerialUSB.print(elrs.seenChannels ? "stale" : "waiting");
  }
  SerialUSB.print(" frames=");
  SerialUSB.print(elrs.validFrames);
  SerialUSB.print(" crcErr=");
  SerialUSB.print(elrs.crcErrors);
  SerialUSB.println();

  if (elrs.seenChannels) {
    SerialUSB.print("CRSF channels:");
    for (uint8_t channel = 0; channel < CRSF_CHANNEL_COUNT; channel++) {
      SerialUSB.print(" CH");
      SerialUSB.print(channel + 1);
      SerialUSB.print("=");
      SerialUSB.print(elrs.channelRaw[channel]);
      SerialUSB.print("/");
      SerialUSB.print(elrs.channelUs[channel]);
      SerialUSB.print("us");
    }
    SerialUSB.println();
  }

  SerialUSB.print("BMS ");
  if (bmsFresh()) {
    SerialUSB.print(bms.packVoltageV, 1);
    SerialUSB.print("V ");
    SerialUSB.print(bms.packCurrentA, 1);
    SerialUSB.print("A SOC=");
    SerialUSB.print(bms.socPercent);
    SerialUSB.print("% avgTemp=");
    SerialUSB.print(bms.avgTempC);
    SerialUSB.print("C alarm=");
    SerialUSB.print(bms.maxAlarmLevel);
  } else {
    SerialUSB.print("stale");
  }

  SerialUSB.print(" | ");
  printOneVesc("VESC L", vescLeft);
  SerialUSB.print(" | ");
  printOneVesc("VESC R", vescRight);
  SerialUSB.print(" | UART cmd L/R=");
  SerialUSB.print(lastVescLeftCommand, 3);
  SerialUSB.print("/");
  SerialUSB.print(lastVescRightCommand, 3);
  SerialUSB.print(" | SD=");
  if (sdLogReady) {
    SerialUSB.print(logFileName);
    SerialUSB.print(" err=");
    SerialUSB.print(sdWriteErrors);
  } else {
    SerialUSB.print("offline");
  }
  SerialUSB.println();
}

void setup() {
  SerialUSB.begin(115200);  // Native USB port; does not block standalone startup.
  Serial.begin(VESC_UART_BAUD);  // Left VESC: D1/TX0 and D0/RX0.
  Serial1.begin(115200);  // Due D18 TX1 / D19 RX1 to BRemote U1-0 UART.
  Serial2.begin(ELRS_BAUD);  // Due D16 TX2 / D17 RX2 to ELRS CRSF UART.
  Serial3.begin(VESC_UART_BAUD);  // Right VESC: D14/TX3 and D15/RX3.

  vescLeftUart.port = &Serial;
  vescLeftUart.status = &vescLeft;
  vescRightUart.port = &Serial3;
  vescRightUart.status = &vescRight;

  Can1.begin(JK_BMS_CAN_BAUD);
  initSdLogging();

  SerialUSB.println("Tow Buggie Due dual VESC UART + BMS CAN + ELRS CRSF bring-up.");
  SerialUSB.println("Left VESC: Serial D1/D0, right VESC: Serial3 D14/D15.");
  SerialUSB.println("CAN1: JK BMS, Serial1: BRemote, Serial2: ELRS CRSF.");
  SerialUSB.println("Diagnostics: Native USB / SerialUSB.");
  SerialUSB.println("SD commands: E=flush/eject, R=reinitialize after reinserting.");
  SerialUSB.println("ELRS wiring: receiver TX -> D17/RX2, receiver RX -> D16/TX2.");
  SerialUSB.println(ENABLE_VESC_COMMANDS ? "VESC commands enabled." : "VESC commands disabled for safe bring-up.");
}

void loop() {
  uint32_t now = millis();
  pollCan();
  pollElrs();
  pollUsbCommands();
  pollVescUart(vescLeftUart);
  pollVescUart(vescRightUart);

  if (now - lastVescCommandMs >= VESC_COMMAND_PERIOD_MS) {
    lastVescCommandMs = now;
    sendSafeVescCommands();
  }

  if (now - lastVescUartTelemetryMs >= VESC_UART_TELEMETRY_PERIOD_MS) {
    lastVescUartTelemetryMs = now;
    requestVescUartValues(vescLeftUart);
    requestVescUartValues(vescRightUart);
  }

  if (now - lastBremoteTelemetryMs >= BREMOTE_TELEMETRY_PERIOD_MS) {
    lastBremoteTelemetryMs = now;
    sendBremoteTelemetry();
  }

  if (now - lastPrintMs >= PRINT_PERIOD_MS) {
    lastPrintMs = now;
    printStatus();
  }

  serviceSdLogging(now);
}
