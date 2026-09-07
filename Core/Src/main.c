/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "max31865.h"
#include "tm1637.h"
#include "button_input.h"
#include "data_logger.h"
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
} GpioOutput;

typedef enum {
  STARTUP_SAFETY_CHECK_PT100 = 0,

  STARTUP_SAFETY_FILL_WATER,
  STARTUP_SAFETY_READY,
  STARTUP_SAFETY_ERROR
} StartupSafetyState;

typedef enum {
  BUZZER_EVENT_OFF = 0,
  BUZZER_EVENT_BUTTON,
  BUZZER_EVENT_READY,
  BUZZER_EVENT_START,
  BUZZER_EVENT_STOP,
  BUZZER_EVENT_COMPLETE,
  BUZZER_EVENT_ERROR,
  BUZZER_EVENT_COUNT
} BuzzerEvent;

typedef struct {
  uint8_t pulseCount;
  uint32_t onMs;
  uint32_t offMs;
 } BuzzerPattern;

 typedef struct {
  uint8_t remainingPulses;
  uint8_t active;
  uint8_t outputOn;
  uint32_t onMs;
  uint32_t offMs;
  uint32_t lastToggleTick;
} BuzzerSequence;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define HEATER_POWER_OPTION_COUNT 3U
#define PROGRAM_LED_COUNT 6U
#define PROGRAM_LED_NONE 0xffU
#define PROGRAM_DEBOUNCE_MS 30U
#define PROGRAM_LONG_PRESS_MS 1000U
#define PROGRAM_REPEAT_MS 500U
#define MINUTE_MS 60000U
#define WATER_FILL_TIMEOUT_MS (4U * MINUTE_MS)
/* Temporary bypass so the cycle can be tested without the water sensor/check.
 * Set 0U to use real sensor*/
#define WATER_CHECK_BYPASS_FOR_TEST 0U
#define MAIN_OVER_TEMPERATURE_TENTHS 1380U

/* Heater identification mode. P1/P2/P3 select 100/70/40 %, START runs a
 * time-proportioned open-loop step test. The samples are CSV over USB CDC. */
#define HEATER_TEST_SAMPLE_MS 10000U
#define HEATER_TEST_WINDOW_MS 10000U
#define HEATER_TEST_DURATION_MS (20U * MINUTE_MS)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi3;

/* USER CODE BEGIN PV */
Max31865Handle gMax31865;
TM1637Handle gDisplay1;
TM1637Handle gDisplay2;
ButtonInput gProgramButtons[HEATER_POWER_OPTION_COUNT];
ButtonInput gStartButton;
int16_t gTemperatureTenthsC = 0;
uint8_t gSensorReady = 0U;
BuzzerSequence gBuzzer = {0U, 0U, 0U, 0U, 0U, 0U};
uint8_t gSafetyErrorActive = 0U;
StartupSafetyState gStartupSafetyState = STARTUP_SAFETY_CHECK_PT100;
uint32_t gStartupSafetyStartTick = 0U;
static uint8_t gHeaterTestPowerPercent = 100U;
static uint8_t gHeaterTestRunning = 0U;
static uint32_t gHeaterTestStartTick = 0U;
static uint32_t gHeaterTestLastSampleTick = 0U;

static const GpioOutput programLeds[PROGRAM_LED_COUNT] = {
  {LD_P1_GPIO_Port, LD_P1_Pin}, {LD_P2_GPIO_Port, LD_P2_Pin},
  {LD_P3_GPIO_Port, LD_P3_Pin}, {LD_P4_GPIO_Port, LD_P4_Pin},
  {LD_P5_GPIO_Port, LD_P5_Pin}, {LD_P6_GPIO_Port, LD_P6_Pin}
};

static const BuzzerPattern buzzerPatterns[BUZZER_EVENT_COUNT] = {
  [BUZZER_EVENT_OFF] = {0U, 0U, 0U},
  [BUZZER_EVENT_BUTTON] = {1U, 300U, 150U},
  [BUZZER_EVENT_READY] = {2U, 300U, 150U},
  [BUZZER_EVENT_START] = {3U, 500U, 500U},
  [BUZZER_EVENT_STOP] = {2U, 1000U, 150U},
  [BUZZER_EVENT_COMPLETE] = {4U, 1000U, 500U},
  [BUZZER_EVENT_ERROR] = {4U, 500U, 150U}
};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI3_Init(void);

/* USER CODE BEGIN PFP */
static void DisplayErrorOnDisplay2(uint8_t code);
static uint8_t SegmentForCharacter(char character);
static void DisplayReadyMessage(void);
static void ProgramButtons_Init(void);
static void ProgramLeds_Set(uint8_t programIndex);
static void StartButton_Init(void);
static uint8_t DoorSwitch_IsClosed(void);
static uint8_t HeaterTest_CheckDoorOrFail(void);
static void StartupSafety_Init(uint32_t now);
static void StartupSafety_Process(uint32_t now);
static uint8_t StartupSafety_IsReady(void);
static void StartupSafety_RequestRecheck(uint32_t now);
static void StartupSafety_SetReady(void);
static uint8_t StartupSafety_CheckPt100(void);
static uint8_t HeaterTest_CheckStartConditions(uint32_t now);
static uint8_t WaterSensor_HasWater(void);
static void WaterLeds_Update(void);
static void SafetyOutputs_Stop(void);
static void SafetyError_Set(uint8_t code);
static void SafetyError_Clear(void);
static void Buzzer_Play(BuzzerEvent event);
static void Buzzer_Process(uint32_t now);
static void Buzzer_Set(uint8_t on);
static void HeaterTest_Process(uint32_t now);
static void HeaterTest_Start(uint32_t now);
static void HeaterTest_Stop(const char *status, uint32_t now);
static void HeaterTest_ApplyPower(uint32_t now);
static void HeaterTest_LogSample(uint32_t now, const char *status);
static void HeaterTest_DisplayPower(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void DisplayErrorOnDisplay2(uint8_t code)
{
  /* E r 0 <code> => Er01..Er09. */
  uint8_t segments[4] = {0x79U, 0x50U, 0x3fU, 0x00U};
  static const uint8_t digitMap[10] = {0x3fU, 0x06U, 0x5bU, 0x4fU, 0x66U, 0x6dU, 0x7dU, 0x07U, 0x7fU, 0x6fU};
  segments[2] = digitMap[(code / 10U) % 10U];
  segments[3] = digitMap[code % 10U];
  tm1637DisplaySegments(&gDisplay2, segments);
}

static uint8_t SegmentForCharacter(char character)
{
  switch (character) {
    case '0': return 0x3fU;
    case '1': return 0x06U;
    case '2': return 0x5bU;
    case '3': return 0x4fU;
    case '4': return 0x66U;
    case '5': return 0x6dU;
    case '6': return 0x7dU;
    case '7': return 0x07U;
    case '8': return 0x7fU;
    case '9': return 0x6fU;
    case 'C': return 0x39U;
    case 'D': return 0x5eU;
    case 'E': return 0x79U;
    case 'H': return 0x76U;
    case 'S': return 0x6dU;
    case 'U': return 0x3eU;
    case 'd': return 0x5eU;
    case 'n': return 0x54U;
    case 'r': return 0x50U;
    case 't': return 0x78U;
    case 'y': return 0x6eU;
    default: return 0x00U;
  }
}

static void ProgramButtons_Init(void)
{
  ButtonInput_Init(&gProgramButtons[0], B_P1_GPIO_Port, B_P1_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&gProgramButtons[1], B_P2_GPIO_Port, B_P2_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&gProgramButtons[2], B_P3_GPIO_Port, B_P3_Pin, GPIO_PIN_SET);
}

static void ProgramLeds_Set(uint8_t programIndex)
{
  for (uint8_t i = 0U; i < PROGRAM_LED_COUNT; ++i) {
    HAL_GPIO_WritePin(programLeds[i].port, programLeds[i].pin,
                      (i == programIndex) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }
}

static void StartButton_Init(void)
{
  ButtonInput_Init(&gStartButton, B_Start_GPIO_Port, B_Start_Pin, GPIO_PIN_SET);
}

static void DisplayReadyMessage(void)
{
  uint8_t segments[4] = {
    SegmentForCharacter('r'), SegmentForCharacter('d'),
    SegmentForCharacter('y'), 0U
  };
  tm1637DisplaySegments(&gDisplay1, segments);
}

static uint8_t DoorSwitch_IsClosed(void)
{
  return (HAL_GPIO_ReadPin(L_Switch_GPIO_Port, L_Switch_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t HeaterTest_CheckDoorOrFail(void)
{
  if (DoorSwitch_IsClosed() == 0U) {
    SafetyError_Set(3U);
    return 0U;
  }

  return 1U;
}

static void StartupSafety_Init(uint32_t now)
{
  gStartupSafetyState = STARTUP_SAFETY_CHECK_PT100;
  gStartupSafetyStartTick = now;
  tm1637Clear(&gDisplay1);
  StartupSafety_Process(now);
}

static void StartupSafety_Process(uint32_t now)
{
  if (gStartupSafetyState == STARTUP_SAFETY_READY || gStartupSafetyState == STARTUP_SAFETY_ERROR) {
    return;
  }

  WaterLeds_Update();

  if (gStartupSafetyState == STARTUP_SAFETY_CHECK_PT100) {
    SafetyOutputs_Stop();
    if (StartupSafety_CheckPt100() == 0U) {
      gStartupSafetyState = STARTUP_SAFETY_ERROR;
      return;
    }

    if (WaterSensor_HasWater() != 0U) {
      StartupSafety_SetReady();
    }
    else {
      HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_SET);
      gStartupSafetyState = STARTUP_SAFETY_FILL_WATER;
      gStartupSafetyStartTick = now;
    }
    return;
  }

  if (WaterSensor_HasWater() != 0U) {
    HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_SET);
	HAL_Delay(5000U);
    StartupSafety_SetReady();
  }
  else if ((now - gStartupSafetyStartTick) >= WATER_FILL_TIMEOUT_MS) {
    SafetyError_Set(2U);
    gStartupSafetyState = STARTUP_SAFETY_ERROR;
  }
  else {
    HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_SET);
  }
}

static uint8_t StartupSafety_IsReady(void)
{
  return (gStartupSafetyState == STARTUP_SAFETY_READY) ? 1U : 0U;
}

static void StartupSafety_RequestRecheck(uint32_t now)
{
  gStartupSafetyState = STARTUP_SAFETY_CHECK_PT100;
  gStartupSafetyStartTick = now;
  StartupSafety_Process(now);
}

static void StartupSafety_SetReady(void){

  SafetyOutputs_Stop();
  WaterLeds_Update();
  gStartupSafetyState = STARTUP_SAFETY_READY;
  DisplayReadyMessage();
  Buzzer_Play(BUZZER_EVENT_READY);
}

static uint8_t StartupSafety_CheckPt100(void)
{
  if (gSensorReady == 0U ||
      Max31865_ReadTemperatureTenthsC(&gMax31865, &gTemperatureTenthsC) == 0U) {
    SafetyError_Set(1U);
    return 0U;
  }

  tm1637DisplayDecimalTenths(&gDisplay2, gTemperatureTenthsC);
  if (gTemperatureTenthsC >= 0 && (uint16_t)gTemperatureTenthsC > MAIN_OVER_TEMPERATURE_TENTHS) {
    SafetyError_Set(5U);
    return 0U;
  }

  return 1U;
}

static uint8_t HeaterTest_CheckStartConditions(uint32_t now)
{
  SafetyOutputs_Stop();

  if (HeaterTest_CheckDoorOrFail() == 0U) {
    return 0U;
  }

  if (WaterSensor_HasWater() == 0U) {
      StartupSafety_RequestRecheck(now);
      return 0U;
    }

  return 1U;
}

static uint8_t WaterSensor_HasWater(void)
{
  #if WATER_CHECK_BYPASS_FOR_TEST
    return 1U;
  #else
    return (HAL_GPIO_ReadPin(Water_S_GPIO_Port, Water_S_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
  #endif
}

static void WaterLeds_Update(void)
{
  if (WaterSensor_HasWater() != 0U) {
    HAL_GPIO_WritePin(LD_HW_GPIO_Port, LD_HW_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD_LW_GPIO_Port, LD_LW_Pin, GPIO_PIN_SET);
  }
  else {
    HAL_GPIO_WritePin(LD_HW_GPIO_Port, LD_HW_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LD_LW_GPIO_Port, LD_LW_Pin, GPIO_PIN_RESET);
  }
}

static void SafetyOutputs_Stop(void)
{
  HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_RESET);
}

static void SafetyError_Set(uint8_t code)
{
  gSafetyErrorActive = 1U;
  SafetyOutputs_Stop();
  HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LD_Alarm_GPIO_Port, LD_Alarm_Pin, GPIO_PIN_SET);
  DisplayErrorOnDisplay2(code);
  Buzzer_Play(BUZZER_EVENT_ERROR);
}

static void SafetyError_Clear(void)
{
  gSafetyErrorActive = 0U;
  HAL_GPIO_WritePin(LD_Alarm_GPIO_Port, LD_Alarm_Pin, GPIO_PIN_RESET);
  Buzzer_Play(BUZZER_EVENT_OFF);
}

static void Buzzer_Play(BuzzerEvent event)
{
  const BuzzerPattern *pattern;

  if (event == BUZZER_EVENT_OFF) {
    gBuzzer.remainingPulses = 0U;
    gBuzzer.active = 0U;
    gBuzzer.outputOn = 0U;
    Buzzer_Set(0U);
    return;
  }

  if (event >= BUZZER_EVENT_COUNT) {
     return;
   }

   if (event == BUZZER_EVENT_ERROR) {
     gSafetyErrorActive = 1U;
   }
   else if (gSafetyErrorActive != 0U) {
     return;
   }

   pattern = &buzzerPatterns[event];
   if (pattern->pulseCount == 0U || pattern->onMs == 0U) {
    return;
  }

  gBuzzer.remainingPulses = pattern->pulseCount;
  gBuzzer.active = 1U;
  gBuzzer.outputOn = 1U;
  gBuzzer.onMs = pattern->onMs;
  gBuzzer.offMs = pattern->offMs;
  gBuzzer.lastToggleTick = HAL_GetTick();
  Buzzer_Set(1U);
}

static void Buzzer_Process(uint32_t now)
{
  if (gBuzzer.active == 0U) {
    Buzzer_Set(0U);
    return;
  }

  if (gBuzzer.outputOn != 0U) {
    if ((now - gBuzzer.lastToggleTick) >= gBuzzer.onMs) {
      Buzzer_Set(0U);
      gBuzzer.outputOn = 0U;
      gBuzzer.lastToggleTick = now;
      if (gBuzzer.remainingPulses > 0U) {
        --gBuzzer.remainingPulses;
      }
    }
  }
  else if ((now - gBuzzer.lastToggleTick) >= gBuzzer.offMs) {
    if (gBuzzer.remainingPulses == 0U) {
      gBuzzer.active = 0U;
    }
    else {
      Buzzer_Set(1U);
      gBuzzer.outputOn = 1U;
      gBuzzer.lastToggleTick = now;
    }
  }
}

static void Buzzer_Set(uint8_t on)
{
  HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void HeaterTest_DisplayPower(void)
{
  uint8_t segments[4] = {SegmentForCharacter('H'), 0U, 0U, 0U};
  uint8_t power = gHeaterTestPowerPercent;

  if (power == 100U) {
    segments[1] = SegmentForCharacter('1');
    segments[2] = SegmentForCharacter('0');
    segments[3] = SegmentForCharacter('0');
  }
  else {
    segments[2] = SegmentForCharacter((char)('0' + (power / 10U)));
    segments[3] = SegmentForCharacter('0');
  }
  tm1637DisplaySegments(&gDisplay1, segments);
}

static void HeaterTest_Start(uint32_t now)
{
  if (HeaterTest_CheckStartConditions(now) == 0U || StartupSafety_CheckPt100() == 0U) {
    return;
  }

  gHeaterTestStartTick = now;
  gHeaterTestLastSampleTick = now - HEATER_TEST_SAMPLE_MS;
  (void)DataLogger_Write("elapsed_s,power_percent,temperature_c,status\r\n");

  gHeaterTestRunning = 1U;
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_SET);
  Buzzer_Play(BUZZER_EVENT_START);
}

static void HeaterTest_Stop(const char *status, uint32_t now)
{
  if (gHeaterTestRunning != 0U) {
    HeaterTest_LogSample(now, status);
  }
  gHeaterTestRunning = 0U;
  SafetyOutputs_Stop();
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
}

static void HeaterTest_ApplyPower(uint32_t now)
{
  uint32_t elapsedInWindow = (now - gHeaterTestStartTick) % HEATER_TEST_WINDOW_MS;
  uint8_t heaterOn;
  /* Pure open-loop time-proportioning:
   *   100 % = 10 s ON / 0 s OFF
   *    70 % =  7 s ON / 3 s OFF
   *    40 % =  4 s ON / 6 s OFF
   * There is deliberately no setpoint, error calculation, or PID call here. */
  uint32_t onTime = (HEATER_TEST_WINDOW_MS * gHeaterTestPowerPercent) / 100U;

  heaterOn = (elapsedInWindow < onTime) ? 1U : 0U;
  HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin,
                    (heaterOn != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_SET);
}

static void HeaterTest_LogSample(uint32_t now, const char *status)
{
  char line[96];
  int16_t temperature = gTemperatureTenthsC;

  (void)snprintf(line, sizeof(line), "%lu,%u,%d.%01d,%s\r\n",
                 (unsigned long)((now - gHeaterTestStartTick) / 1000U),
                 gHeaterTestPowerPercent,
                 temperature / 10, (temperature < 0 ? -temperature : temperature) % 10,
                 status);
  (void)DataLogger_Write(line);
}

static void HeaterTest_Process(uint32_t now)
{
  static const uint8_t powers[3] = {100U, 70U, 40U};
  uint8_t startPressed;

  for (uint8_t i = 0U; i < HEATER_POWER_OPTION_COUNT; ++i) {
    ButtonInput_Update(&gProgramButtons[i], now, PROGRAM_DEBOUNCE_MS,
                       PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);
    if (ButtonInput_ConsumePressed(&gProgramButtons[i]) != 0U &&
        gHeaterTestRunning == 0U && StartupSafety_IsReady() != 0U) {
      gHeaterTestPowerPercent = powers[i];
      ProgramLeds_Set(i);
      HeaterTest_DisplayPower();
      Buzzer_Play(BUZZER_EVENT_BUTTON);
    }
  }

  ButtonInput_Update(&gStartButton, now, PROGRAM_DEBOUNCE_MS,
                     PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);
  startPressed = ButtonInput_ConsumePressed(&gStartButton);
  if (startPressed != 0U && gSafetyErrorActive != 0U) {
    SafetyError_Clear();
    StartupSafety_RequestRecheck(now);
    Buzzer_Play(BUZZER_EVENT_BUTTON);
  }
  else if (startPressed != 0U && StartupSafety_IsReady() != 0U) {
    if (gHeaterTestRunning != 0U) {
      HeaterTest_Stop("STOP", now);
      Buzzer_Play(BUZZER_EVENT_STOP);
    }
    else {
      HeaterTest_Start(now);
    }
  }

  if (gHeaterTestRunning == 0U) {
    return;
  }
  if (HeaterTest_CheckDoorOrFail() == 0U) {
    HeaterTest_Stop("DOOR_ERROR", now);
    return;
  }
  if ((now - gHeaterTestStartTick) >= HEATER_TEST_DURATION_MS) {
    HeaterTest_Stop("DONE", now);
    Buzzer_Play(BUZZER_EVENT_COMPLETE);
    return;
  }

  HeaterTest_ApplyPower(now);
  if ((now - gHeaterTestLastSampleTick) < HEATER_TEST_SAMPLE_MS) {
    return;
  }
  /* Log the unfiltered PT100 value: the application filter would add an
   * artificial lag to the plant model. Temperature still only trips safety. */
  if (Max31865_ReadTemperatureTenthsC(&gMax31865, &gTemperatureTenthsC) == 0U) {
    SafetyError_Set(1U);
    HeaterTest_Stop("SENSOR_ERROR", now);
    return;
  }
  if (gTemperatureTenthsC > (int16_t)MAIN_OVER_TEMPERATURE_TENTHS) {
    SafetyError_Set(5U);
    HeaterTest_Stop("OVER_TEMP", now);
    return;
  }

  gHeaterTestLastSampleTick = now;
  tm1637DisplayDecimalTenths(&gDisplay2, gTemperatureTenthsC);
  HeaterTest_LogSample(now, "RUN");
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI3_Init();
  /* USER CODE BEGIN 2 */
  tm1637Init(&gDisplay1, TM1637_DISPLAY_1);
  tm1637Init(&gDisplay2, TM1637_DISPLAY_2);
  tm1637SetBrightness(&gDisplay1, 8);
  tm1637SetBrightness(&gDisplay2, 8);
  tm1637Clear(&gDisplay1);
  tm1637Clear(&gDisplay2);
  ProgramButtons_Init();
  StartButton_Init();
  ProgramLeds_Set(PROGRAM_LED_NONE);
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LD_HW_GPIO_Port, LD_HW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_LW_GPIO_Port, LD_LW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_Alarm_GPIO_Port, LD_Alarm_Pin, GPIO_PIN_RESET);
  SafetyOutputs_Stop();
  HAL_GPIO_WritePin(LD_HW_GPIO_Port, LD_HW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_LW_GPIO_Port, LD_LW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_Alarm_GPIO_Port, LD_Alarm_Pin, GPIO_PIN_RESET);

  Max31865_Init(&gMax31865, &hspi3, CS_GPIO_Port, CS_Pin, 430.0f, 100.0f);
  gSensorReady = Max31865_Begin(&gMax31865, MAX31865_2WIRE, 1U);
  StartupSafety_Init(HAL_GetTick());
  ProgramLeds_Set(0U);
  HeaterTest_DisplayPower();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    StartupSafety_Process(now);
    WaterLeds_Update();
    HeaterTest_Process(now);
    Buzzer_Process(now);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, LD_C3_Pin|LD_C4_Pin|LD_C5_Pin|LD_C6_Pin
                          |LD_C7_Pin|LD_Alarm_Pin|LD_LW_Pin|LD_HW_Pin
                          |SSR_Heater_Pin|SSR_HResistor_Pin|Relay_Valve1_Pin|Relay_Valve2_Pin
                          |Relay_Valve3_Pin|LD_C1_Pin|LD_C2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, Buzzer_Pin|CLK1_Pin|DIO1_Pin|CLK2_Pin
                          |DIO2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, Relay_Pump_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD6_Pin|LD_P3_Pin|LD_P2_Pin|LD_P1_Pin
                          |LD_P4_Pin|LD_P5_Pin|LD_P6_Pin|LD_Start_Pin
                          |LD_User_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : LD_C3_Pin LD_C4_Pin LD_C5_Pin LD_C6_Pin
                           LD_C7_Pin LD_Alarm_Pin LD_LW_Pin LD_HW_Pin
                           SSR_Heater_Pin SSR_HResistor_Pin Relay_Valve1_Pin Relay_Valve2_Pin
                           Relay_Valve3_Pin LD_C1_Pin LD_C2_Pin */
  GPIO_InitStruct.Pin = LD_C3_Pin|LD_C4_Pin|LD_C5_Pin|LD_C6_Pin
                          |LD_C7_Pin|LD_Alarm_Pin|LD_LW_Pin|LD_HW_Pin
                          |SSR_Heater_Pin|SSR_HResistor_Pin|Relay_Valve1_Pin|Relay_Valve2_Pin
                          |Relay_Valve3_Pin|LD_C1_Pin|LD_C2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : B_P1_Pin B_P2_Pin B_P3_Pin B_P4_Pin
                           B_P5_Pin B_P6_Pin B_Start_Pin B_Set_Pin
                           B_Up_Pin B_Down_Pin B_User_Pin */
  GPIO_InitStruct.Pin = B_P1_Pin|B_P2_Pin|B_P3_Pin|B_P4_Pin
                          |B_P5_Pin|B_P6_Pin|B_Start_Pin|B_Set_Pin
                          |B_Up_Pin|B_Down_Pin|B_User_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CS_Pin */
  GPIO_InitStruct.Pin = CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BOOT1_Pin Water_S_Pin L_Switch_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin|Water_S_Pin|L_Switch_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : Buzzer_Pin CLK1_Pin DIO1_Pin CLK2_Pin
                           DIO2_Pin */
  GPIO_InitStruct.Pin = Buzzer_Pin|CLK1_Pin|DIO1_Pin|CLK2_Pin
                          |DIO2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : Relay_Pump_Pin LD4_Pin LD3_Pin LD5_Pin
                           LD6_Pin LD_P3_Pin LD_P2_Pin LD_P1_Pin
                           LD_P4_Pin LD_P5_Pin LD_P6_Pin LD_Start_Pin
                           LD_User_Pin */
  GPIO_InitStruct.Pin = Relay_Pump_Pin|LD4_Pin|LD3_Pin|LD5_Pin
                          |LD6_Pin|LD_P3_Pin|LD_P2_Pin|LD_P1_Pin
                          |LD_P4_Pin|LD_P5_Pin|LD_P6_Pin|LD_Start_Pin
                          |LD_User_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
