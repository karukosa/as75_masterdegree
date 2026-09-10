#ifndef HEATER_TEST_LOG_H
#define HEATER_TEST_LOG_H

#include <stdint.h>

/* 35 minutes at one sample/10 s, including the t=0 and final-status records. */
#define HEATER_TEST_LOG_CAPACITY 211U
#define HEATER_TEST_LOG_MAGIC "HTLOG001"
#define HEATER_TEST_LOG_VERSION 1U

typedef enum {
  HEATER_LOG_STATUS_RUN = 0U,
  HEATER_LOG_STATUS_DONE,
  HEATER_LOG_STATUS_STOP,
  HEATER_LOG_STATUS_SENSOR_ERROR,
  HEATER_LOG_STATUS_OVER_TEMP,
  HEATER_LOG_STATUS_DOOR_ERROR,
  HEATER_LOG_STATUS_BUFFER_FULL
} HeaterTestLogStatus;

typedef struct {
  uint32_t elapsedMs;
  int16_t temperatureTenthsC;
  uint8_t powerPercent;
  uint8_t heaterOn;
  uint8_t status;
  uint8_t reserved[3];
} HeaterTestLogSample;

typedef struct {
  uint8_t magic[8];
  uint16_t version;
  uint16_t sampleSize;
  uint32_t capacity;
  volatile uint32_t sampleCount;
  volatile uint8_t complete;
  uint8_t reserved[3];
  HeaterTestLogSample samples[HEATER_TEST_LOG_CAPACITY];
} HeaterTestLog;

/* Deliberately global: a debugger can locate and dump this symbol over SWD. */
extern volatile HeaterTestLog gHeaterTestLog;

void HeaterTestLog_Reset(void);
uint8_t HeaterTestLog_Append(uint32_t elapsedMs, int16_t temperatureTenthsC,
                             uint8_t powerPercent, uint8_t heaterOn,
                             HeaterTestLogStatus status);
void HeaterTestLog_MarkComplete(void);

#endif
