
// Standard C Libraries
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

void uartInit(uint8_t uartNum, uint32_t baudRate);

void uartSetBaud(uint8_t uartNum, uint32_t baudRate);

uint8_t uartReceiveBytes(uint8_t uartNum, uint8_t* pBuffer, uint8_t bufferSize, uint8_t timeout_ms);

uint8_t uartTransmit(uint8_t uartNum, uint8_t* pBuffer, uint8_t bytesToSend);

#ifdef __cplusplus
}
#endif