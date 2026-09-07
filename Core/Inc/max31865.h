/*
 * max31865.h
 *
 *  Created on: Mar 25, 2026
 *      Author: Karukosa
 */

#ifndef INC_MAX31865_H_
#define INC_MAX31865_H_

#include "main.h"

#define MAX31865_CONFIG_REG         0x00U
#define MAX31865_CONFIG_BIAS        0x80U
#define MAX31865_CONFIG_MODEAUTO    0x40U
#define MAX31865_CONFIG_1SHOT       0x20U
#define MAX31865_CONFIG_3WIRE       0x10U
#define MAX31865_CONFIG_FAULTSTAT   0x02U
#define MAX31865_CONFIG_FILT50HZ    0x01U

#define MAX31865_RTDMSB_REG         0x01U
#define MAX31865_HFAULTMSB_REG      0x03U
#define MAX31865_HFAULTLSB_REG      0x04U
#define MAX31865_LFAULTMSB_REG      0x05U
#define MAX31865_LFAULTLSB_REG      0x06U
#define MAX31865_FAULTSTAT_REG      0x07U

#define MAX31865_FAULT_HIGHTHRESH   0x80U
#define MAX31865_FAULT_LOWTHRESH    0x40U
#define MAX31865_FAULT_REFINLOW     0x20U
#define MAX31865_FAULT_REFINHIGH    0x10U
#define MAX31865_FAULT_RTDINLOW     0x08U
#define MAX31865_FAULT_OVUV         0x04U

typedef enum {
    MAX31865_2WIRE = 0,
    MAX31865_3WIRE = 1,
    MAX31865_4WIRE = 0
} Max31865NumWires;

typedef enum {
    MAX31865_FAULT_NONE = 0,
    MAX31865_FAULT_AUTO,
    MAX31865_FAULT_MANUAL_RUN,
    MAX31865_FAULT_MANUAL_FINISH
} Max31865FaultCycle;

typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *csPort;
    uint16_t csPin;
    float rRefOhms;
    float rNominalOhms;
    Max31865NumWires wires;
    uint8_t filter50Hz;
} Max31865Handle;

void Max31865_Init(Max31865Handle *handle,
                   SPI_HandleTypeDef *spi,
                   GPIO_TypeDef *csPort,
                   uint16_t csPin,
                   float rRefOhms,
                   float rNominalOhms);

uint8_t Max31865_Begin(Max31865Handle *handle, Max31865NumWires wires, uint8_t filter50Hz);
void Max31865_SetWires(Max31865Handle *handle, Max31865NumWires wires);
void Max31865_Enable50Hz(Max31865Handle *handle, uint8_t enable50Hz);
void Max31865_EnableBias(Max31865Handle *handle, uint8_t enable);
void Max31865_AutoConvert(Max31865Handle *handle, uint8_t enable);
void Max31865_ClearFault(Max31865Handle *handle);
uint8_t Max31865_ReadFault(Max31865Handle *handle, Max31865FaultCycle faultCycle);
void Max31865_SetThresholds(Max31865Handle *handle, uint16_t lower, uint16_t upper);
uint16_t Max31865_ReadRTD(Max31865Handle *handle);
float Max31865_CalculateTemperature(uint16_t rawRtd, float rtdNominal, float refResistor);
uint8_t Max31865_ReadTemperatureC(Max31865Handle *handle, float *temperatureC);
uint8_t Max31865_ReadTemperatureTenthsC(Max31865Handle *handle, int16_t *temperatureTenths);

#endif /* INC_MAX31865_H_ */
