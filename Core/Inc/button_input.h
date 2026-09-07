/*
 * button_input.h
 *
 *  Created on: Mar 19, 2026
 *      Author: Karukosa
 */

#ifndef INC_BUTTON_INPUT_H_
#define INC_BUTTON_INPUT_H_

#include "main.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t activeLevel;
    uint8_t stableState;
    uint8_t rawState;
    uint8_t pressedEvent;
    uint8_t releasedEvent;
    uint8_t repeating;
    uint32_t lastDebounceTick;
    uint32_t pressedTick;
    uint32_t lastRepeatTick;
} ButtonInput;

void ButtonInput_Init(ButtonInput *button, GPIO_TypeDef *port, uint16_t pin, GPIO_PinState activeLevel);
void ButtonInput_Update(ButtonInput *button, uint32_t now, uint32_t debounceMs, uint32_t longPressMs, uint32_t repeatMs);
uint8_t ButtonInput_ConsumePressed(ButtonInput *button);
uint8_t ButtonInput_ConsumeReleased(ButtonInput *button);
uint8_t ButtonInput_ConsumeRepeat(ButtonInput *button);


#endif /* INC_BUTTON_INPUT_H_ */
