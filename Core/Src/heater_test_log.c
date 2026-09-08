#include "heater_test_log.h"

#include <string.h>

volatile HeaterTestLog gHeaterTestLog;

/* Keep the on-target format stable for tools/extract_ram_log.py. */
typedef char HeaterLogHeaderMustBe24Bytes[
    (sizeof(HeaterTestLog) - sizeof(gHeaterTestLog.samples) == 24U) ? 1 : -1];
typedef char HeaterLogSampleMustBe12Bytes[
    (sizeof(HeaterTestLogSample) == 12U) ? 1 : -1];

void HeaterTestLog_Reset(void)
{
  static const uint8_t magic[8] = HEATER_TEST_LOG_MAGIC;

  memset((void *)&gHeaterTestLog, 0, sizeof(gHeaterTestLog));
  memcpy((void *)gHeaterTestLog.magic, magic, sizeof(magic));
  gHeaterTestLog.version = HEATER_TEST_LOG_VERSION;
  gHeaterTestLog.sampleSize = sizeof(HeaterTestLogSample);
  gHeaterTestLog.capacity = HEATER_TEST_LOG_CAPACITY;
}

uint8_t HeaterTestLog_Append(uint32_t elapsedMs, int16_t temperatureTenthsC,
                             uint8_t powerPercent, uint8_t heaterOn,
                             HeaterTestLogStatus status)
{
  uint32_t index = gHeaterTestLog.sampleCount;
  volatile HeaterTestLogSample *sample;

  if (index >= HEATER_TEST_LOG_CAPACITY) {
    gHeaterTestLog.complete = 1U;
    return 0U;
  }

  sample = &gHeaterTestLog.samples[index];
  sample->elapsedMs = elapsedMs;
  sample->temperatureTenthsC = temperatureTenthsC;
  sample->powerPercent = powerPercent;
  sample->heaterOn = heaterOn;
  sample->status = (uint8_t)status;
  /* Publish the record only after all its fields have been written. */
  __asm volatile ("" ::: "memory");
  gHeaterTestLog.sampleCount = index + 1U;
  return 1U;
}

void HeaterTestLog_MarkComplete(void)
{
  __asm volatile ("" ::: "memory");
  gHeaterTestLog.complete = 1U;
}
