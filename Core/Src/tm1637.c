/*
 * tm1637.c
 *
 *  Created on: Mar 18, 2026
 *      Author: Karukosa
 */

#include "tm1637.h"

static void tm1637Start(TM1637Handle *handle);
static void tm1637Stop(TM1637Handle *handle);
static void tm1637ReadResult(TM1637Handle *handle);
static void tm1637WriteByte(TM1637Handle *handle, uint8_t b);
static void tm1637WriteSegments(TM1637Handle *handle, const uint8_t segments[4]);
static void tm1637DelayUsec(unsigned int i);
static void tm1637ClkHigh(TM1637Handle *handle);
static void tm1637ClkLow(TM1637Handle *handle);
static void tm1637DioHigh(TM1637Handle *handle);
static void tm1637DioLow(TM1637Handle *handle);
static void tm1637AssignPins(TM1637Handle *handle, TM1637Display display);

static const uint8_t segmentMap[] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, // 0-7
    0x7f, 0x6f, 0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, // 8-9, A-F
    0x00
};

void tm1637Init(TM1637Handle *handle, TM1637Display display)
{
    GPIO_InitTypeDef g = {0};

    if (handle == NULL) {
        return;
    }

    tm1637AssignPins(handle, display);
    __HAL_RCC_GPIOB_CLK_ENABLE();

    g.Pull = GPIO_PULLUP;
    g.Mode = GPIO_MODE_OUTPUT_OD; // OD = open drain
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    g.Pin = handle->clkPin;
    HAL_GPIO_Init(handle->clkPort, &g);

    g.Pin = handle->dioPin;
    HAL_GPIO_Init(handle->dioPort, &g);

    tm1637SetBrightness(handle, 8);
}

void tm1637DisplayDecimal(TM1637Handle *handle, int v, int displaySeparator)
{
    uint8_t segments[4];

    if (handle == NULL) {
        return;
    }

    if (v < 0) {
        v = 0;
    }
    else if (v > 9999) {
        v = 9999;
    }

    for (int pos = 3; pos >= 0; --pos) {
        segments[pos] = segmentMap[v % 10];
        v /= 10;
    }

    if (displaySeparator) {
        segments[1] |= 1U << 7;
    }

    tm1637WriteSegments(handle, segments);
}

void tm1637DisplayDecimalTenths(TM1637Handle *handle, int valueTenths)
{
    uint8_t segments[4] = {0, 0, 0, 0};
    int wholePart;

    if (handle == NULL) {
        return;
    }

    if (valueTenths < 0) {
        valueTenths = 0;
    }
    else if (valueTenths > 9999) {
        valueTenths = 9999;
    }

    wholePart = valueTenths / 10;
    segments[3] = segmentMap[valueTenths % 10];

    for (int pos = 2; pos >= 0; --pos) {
        if (wholePart > 0 || pos == 2) {
            segments[pos] = segmentMap[wholePart % 10];
            wholePart /= 10;
        }
    }

    /* Light decimal point between integer and tenths.
       Some TM1637 modules map the separator to digit 1 instead of digit 2,
       so set both bits to ensure the dot is visible. */
    segments[1] |= 1U << 7;
    segments[2] |= 1U << 7;
    tm1637WriteSegments(handle, segments);
}

void tm1637DisplayTime(TM1637Handle *handle, int hours, int minutes, int showColon)
{
    uint8_t segments[4];

    if (handle == NULL) {
        return;
    }

    if (hours < 0) {
        hours = 0;
    }
    else if (hours > 99) {
        hours = 99;
    }

    if (minutes < 0) {
        minutes = 0;
    }
    else if (minutes > 99) {
        minutes = 99;
    }

    segments[0] = segmentMap[(hours / 10) % 10];
    segments[1] = segmentMap[hours % 10];
    segments[2] = segmentMap[(minutes / 10) % 10];
    segments[3] = segmentMap[minutes % 10];

    if (showColon) {
        segments[1] |= 1U << 7;
    }

    tm1637WriteSegments(handle, segments);
}

void tm1637DisplaySegments(TM1637Handle *handle, const uint8_t segments[4])
{
    if (handle == NULL || segments == NULL) {
        return;
    }

    tm1637WriteSegments(handle, segments);
}

// Valid brightness values: 0 - 8.
// 0 = display off.
void tm1637SetBrightness(TM1637Handle *handle, char brightness)
{
    uint8_t command;

    if (handle == NULL) {
        return;
    }

    if (brightness <= 0) {
        command = 0x80;
    }
    else {
        if (brightness > 8) {
            brightness = 8;
        }
        command = 0x88 | (uint8_t)(brightness - 1);
    }

    tm1637Start(handle);
    tm1637WriteByte(handle, command);
    tm1637ReadResult(handle);
    tm1637Stop(handle);
}

void tm1637Clear(TM1637Handle *handle)
{
    static const uint8_t blankSegments[4] = {0, 0, 0, 0};

    if (handle == NULL) {
        return;
    }

    tm1637WriteSegments(handle, blankSegments);
}


static void tm1637Start(TM1637Handle *handle)
{
    tm1637ClkHigh(handle);
    tm1637DioHigh(handle);
    tm1637DelayUsec(2);
    tm1637DioLow(handle);
}

static void tm1637Stop(TM1637Handle *handle)
{
    tm1637ClkLow(handle);
    tm1637DelayUsec(2);
    tm1637DioLow(handle);
    tm1637DelayUsec(2);
    tm1637ClkHigh(handle);
    tm1637DelayUsec(2);
    tm1637DioHigh(handle);
}

static void tm1637ReadResult(TM1637Handle *handle)
{
    tm1637ClkLow(handle);
    tm1637DelayUsec(5);
    // TODO: Read ACK from DIO if you need bus error detection.
    tm1637ClkHigh(handle);
    tm1637DelayUsec(2);
    tm1637ClkLow(handle);
}

static void tm1637WriteByte(TM1637Handle *handle, uint8_t b)
{
    for (int i = 0; i < 8; ++i) {
        tm1637ClkLow(handle);
        if (b & 0x01U) {
            tm1637DioHigh(handle);
        }
        else {
            tm1637DioLow(handle);
        }
        tm1637DelayUsec(3);
        b >>= 1;
        tm1637ClkHigh(handle);
        tm1637DelayUsec(3);
    }
}

static void tm1637WriteSegments(TM1637Handle *handle, const uint8_t segments[4])
{
    tm1637Start(handle);
    tm1637WriteByte(handle, 0x40);
    tm1637ReadResult(handle);
    tm1637Stop(handle);

    tm1637Start(handle);
    tm1637WriteByte(handle, 0xc0);
    tm1637ReadResult(handle);

    for (int i = 0; i < 4; ++i) {
        tm1637WriteByte(handle, segments[i]);
        tm1637ReadResult(handle);
    }

    tm1637Stop(handle);
}

static void tm1637DelayUsec(unsigned int i)
{
    for (; i > 0; i--) {
        for (int j = 0; j < 10; ++j) {
            __asm__ __volatile__("nop\n\t" ::: "memory");
        }
    }
}

static void tm1637ClkHigh(TM1637Handle *handle)
{
    HAL_GPIO_WritePin(handle->clkPort, handle->clkPin, GPIO_PIN_SET);
}

static void tm1637ClkLow(TM1637Handle *handle)
{
    HAL_GPIO_WritePin(handle->clkPort, handle->clkPin, GPIO_PIN_RESET);
}

static void tm1637DioHigh(TM1637Handle *handle)
{
    HAL_GPIO_WritePin(handle->dioPort, handle->dioPin, GPIO_PIN_SET);
}

static void tm1637DioLow(TM1637Handle *handle)
{
    HAL_GPIO_WritePin(handle->dioPort, handle->dioPin, GPIO_PIN_RESET);
}

static void tm1637AssignPins(TM1637Handle *handle, TM1637Display display)
{
    if (display == TM1637_DISPLAY_2) {
        handle->clkPort = CLK2_GPIO_Port;
        handle->clkPin = CLK2_Pin;
        handle->dioPort = DIO2_GPIO_Port;
        handle->dioPin = DIO2_Pin;
    }
    else {
        handle->clkPort = CLK1_GPIO_Port;
        handle->clkPin = CLK1_Pin;
        handle->dioPort = DIO1_GPIO_Port;
        handle->dioPin = DIO1_Pin;
    }
}
