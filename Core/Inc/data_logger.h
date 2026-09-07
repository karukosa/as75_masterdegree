#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <stdint.h>

/* Writes text to ITM stimulus port 0. STM32CubeIDE receives it through the
 * on-board ST-LINK/SWO connection; no UART or second USB cable is required. */
uint8_t DataLogger_Write(const char *text);

#endif /* DATA_LOGGER_H */
