/*
 * button_input.c
 *
 *  Created on: Mar 19, 2026
 *      Author: Karukosa
 */

#include "button_input.h"

void ButtonInput_Init(ButtonInput *button, GPIO_TypeDef *port, uint16_t pin, GPIO_PinState activeLevel)
{
    GPIO_PinState pinState;

    if (button == NULL) {
        return;
    }

    pinState = HAL_GPIO_ReadPin(port, pin);

    button->port = port;
    button->pin = pin;
    button->activeLevel = (uint8_t)activeLevel;
    button->stableState = (uint8_t)pinState;
    button->rawState = (uint8_t)pinState;
    button->pressedEvent = 0U;
    button->releasedEvent = 0U;
    button->repeating = 0U;
    button->lastDebounceTick = HAL_GetTick();
    button->pressedTick = 0U;
    button->lastRepeatTick = 0U;
}

void ButtonInput_Update(ButtonInput *button, uint32_t now, uint32_t debounceMs, uint32_t longPressMs, uint32_t repeatMs)
{
    uint8_t currentReading;

    if (button == NULL) {
        return;
    }

    currentReading = (uint8_t)HAL_GPIO_ReadPin(button->port, button->pin);

    if (currentReading != button->rawState) {
        button->rawState = currentReading;
        button->lastDebounceTick = now;
    }

    if ((now - button->lastDebounceTick) >= debounceMs && button->stableState != button->rawState) {
        button->stableState = button->rawState;

        if (button->stableState == button->activeLevel) {
            button->pressedEvent = 1U;
            button->repeating = 0U;
            button->pressedTick = now;
            button->lastRepeatTick = now;
        }
        else {
            button->releasedEvent = 1U;
            button->repeating = 0U;
        }
    }

    if (button->stableState == button->activeLevel && (now - button->pressedTick) >= longPressMs) {
        if ((now - button->lastRepeatTick) >= repeatMs) {
            button->repeating = 1U;
            button->lastRepeatTick = now;
        }
    }
}

uint8_t ButtonInput_ConsumePressed(ButtonInput *button)
{
    uint8_t event;

    if (button == NULL) {
        return 0U;
    }

    event = button->pressedEvent;
    button->pressedEvent = 0U;
    return event;
}

uint8_t ButtonInput_ConsumeReleased(ButtonInput *button)
{
    uint8_t event;

    if (button == NULL) {
        return 0U;
    }

    event = button->releasedEvent;
    button->releasedEvent = 0U;
    return event;
}

uint8_t ButtonInput_ConsumeRepeat(ButtonInput *button)
{
    uint8_t event;

    if (button == NULL) {
        return 0U;
    }

    event = button->repeating;
    button->repeating = 0U;
    return event;
}
