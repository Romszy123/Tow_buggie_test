#include "uart.h"
#include <SingleWireSerial.h>

SingleWireSerial srxl2_serial(false);



void uartInit(uint8_t uartNum, uint32_t baudRate){
  // We actually disregard the uartNum, we have only 1 usable uart port
  srxl2_serial.begin(baudRate);
}

void uartSetBaud(uint8_t uartNum, uint32_t baudRate){
  // We actually disregard the uartNum, we have only 1 usable uart port
  srxl2_serial.end();
  srxl2_serial.begin(baudRate);
}

uint8_t uartReceiveBytes(uint8_t uartNum, uint8_t* pBuffer, uint8_t bufferSize, uint8_t timeout_ms){
  // We actually disregard the uartNum, we have only 1 usable uart port
  srxl2_serial.setTimeout(timeout_ms);
  return srxl2_serial.readBytes(pBuffer, bufferSize);
}

uint8_t uartTransmit(uint8_t uartNum, uint8_t* pBuffer, uint8_t bytesToSend){
  // We actually disregard the uartNum, we have only 1 usable uart port
  for (int i=0; i<bytesToSend; i++){
    srxl2_serial.write(pBuffer[i]);
  }
}