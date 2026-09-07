#include "data_logger.h"
#include "stm32f4xx.h"

uint8_t DataLogger_Write(const char *text)
{
  if (text == NULL) {
    return 0U;
  }

  while (*text != '\0') {
    (void)ITM_SendChar((uint32_t)(uint8_t)*text);
    ++text;
  }

  return 1U;
}
