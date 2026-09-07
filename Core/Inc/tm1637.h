/*
 * tm1637.h
 *
 *  Created on: Mar 18, 2026
 *      Author: Karukosa
 */

#ifndef INC_TM1637_H_
#define INC_TM1637_H_

#include "main.h"

typedef enum {
    TM1637_DISPLAY_1 = 0,
    TM1637_DISPLAY_2 = 1
} TM1637Display;

typedef struct {
    GPIO_TypeDef *clkPort;
    uint16_t clkPin;
    GPIO_TypeDef *dioPort;
    uint16_t dioPin;
} TM1637Handle;

void tm1637Init(TM1637Handle *handle, TM1637Display display);
void tm1637DisplayDecimal(TM1637Handle *handle, int v, int displaySeparator);
void tm1637DisplayDecimalTenths(TM1637Handle *handle, int valueTenths);
void tm1637DisplayTime(TM1637Handle *handle, int hours, int minutes, int showColon);
void tm1637DisplaySegments(TM1637Handle *handle, const uint8_t segments[4]);
void tm1637SetBrightness(TM1637Handle *handle, char brightness);
void tm1637Clear(TM1637Handle *handle);

#endif /* INC_TM1637_H_ */
