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
#include "usb_host.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "max31865.h"
#include "tm1637.h"
#include "button_input.h"
#include "pid.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct {
  uint16_t temperatureTenthsC;
  uint16_t sterilizeMinutes;
  uint16_t dryMinutes;
} ProgramConfig;

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
} GpioOutput;

typedef enum {
  USER_EDIT_TEMPERATURE = 0,
  USER_EDIT_STERILIZE = 1,
  USER_EDIT_DRY = 2
} UserEditField;

typedef enum {
  MAIN_PHASE_STANDBY = 0,
  MAIN_PHASE_VACUUM,
  MAIN_PHASE_HEATING,
  MAIN_PHASE_HOLDING,
  MAIN_PHASE_EXHAUST,
  MAIN_PHASE_DRYING,
  MAIN_PHASE_DONE
} MainCyclePhase;

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
#define PROGRAM_COUNT 6U
#define PROGRAM_NONE 0xffU
#define PROGRAM_DEBOUNCE_MS 30U
#define PROGRAM_LONG_PRESS_MS 1000U
#define PROGRAM_REPEAT_MS 500U
#define PROGRAM_TIME_ALTERNATE_MS 2000U
#define TEMPERATURE_REFRESH_MS 300U
#define USER_BLINK_MS 500U
#define USER_TEMPERATURE_MIN_TENTHS 1100U
#define USER_TEMPERATURE_MAX_TENTHS 1340U
#define USER_TEMPERATURE_STEP_TENTHS 10
#define USER_TEMPERATURE_FAST_STEP_TENTHS 50
#define USER_TIME_MIN_MINUTES 0U
#define USER_TIME_MAX_MINUTES 99U
#define USER_TIME_STEP_MINUTES 1
#define USER_TIME_FAST_STEP_MINUTES 10
#define MINUTE_MS 60000U
#define MAIN_VACUUM_MS (13U * MINUTE_MS)
#define MAIN_VACUUM_CYCLE_COUNT 3U
#define MAIN_VACUUM_STEP_MS (MAIN_VACUUM_MS / (MAIN_VACUUM_CYCLE_COUNT * 2U))
#define MAIN_ASSIST_JACKET_HEATER_ON_MS 10000U
#define MAIN_ASSIST_JACKET_HEATER_OFF_MS 10000U
#define MAIN_ASSIST_JACKET_HEATER_CUTOFF_TENTHS 250U
#define MAIN_HEATING_RESISTOR_CUTOFF_TENTHS 250U
#define MAIN_EXHAUST_DRAIN_MS (3U * MINUTE_MS)
#define MAIN_EXHAUST_VACUUM_MS (2U * MINUTE_MS)
#define MAIN_EXHAUST_MS (MAIN_EXHAUST_DRAIN_MS + MAIN_EXHAUST_VACUUM_MS)
#define MAIN_DRY_TEMPERATURE_LOW_TENTHS 1000U
#define MAIN_DRY_TEMPERATURE_HIGH_TENTHS 1020U
#define MAIN_DRY_HEATER_CUTOFF_MS (2U * MINUTE_MS)
#define MAIN_HOLD_PID_WINDOW_MS 1000U
#define MAIN_HOLD_PID_SAMPLE_MS 100U
#define MAIN_HOLD_PID_KP 10.0
#define MAIN_HOLD_PID_KI 1.0
#define MAIN_HOLD_PID_KD 4.0
#define MAIN_HEAT_PID_KI 0.0
#define MAIN_HOLD_PID_INITIAL_OUTPUT 90.0
#define MAIN_HOLD_PID_MAX_OUTPUT 255.0
#define MAIN_HOLD_PID_RECOVERY_ERROR_TENTHS 2
#define MAIN_HOLD_PID_RECOVERY_MIN_OUTPUT 96.0
#define MAIN_HOLD_PID_RECOVERY_BOOST_ERROR_TENTHS 5
#define MAIN_HOLD_PID_RECOVERY_BOOST_OUTPUT 220.0
#define MAIN_HOLD_PID_OVERSHOOT_ERROR_TENTHS 2
#define MAIN_HOLD_PID_OVERSHOOT_BOOST_ERROR_TENTHS 5
#define MAIN_HOLD_PID_OVERSHOOT_OUTPUT 0.0
#define WATER_FILL_TIMEOUT_MS (4U * MINUTE_MS)
/* Temporary bypass so the cycle can be tested without the water sensor/check.
 * Set 0U to use real sensor*/
#define WATER_CHECK_BYPASS_FOR_TEST 0U
#define HEATING_TIMEOUT_MS (35U * MINUTE_MS)
#define MAIN_OVER_TEMPERATURE_TENTHS 1380U
#define MAIN_CYCLE_LED_BLINK_MS 500U
#define ACTIVE_CHANNEL_USER 0U
#define TEMPERATURE_READ_FAIL_MAX 20U
#define TEMPERATURE_FILTER_ALPHA_NUMERATOR 1
#define TEMPERATURE_FILTER_ALPHA_DENOMINATOR 4
#define TEMPERATURE_FILTER_MAX_STEP_TENTHS 50

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
ButtonInput gProgramButtons[PROGRAM_COUNT];
ButtonInput gUserButton;
ButtonInput gSetButton;
ButtonInput gUpButton;
ButtonInput gDownButton;
ButtonInput gStartButton;
int16_t gTemperatureTenthsC = 0;
uint8_t gSensorReady = 0U;
uint8_t gSelectedProgram = PROGRAM_NONE;
uint8_t gProgramTimeDisplayPhase = 0xffU;
uint32_t gProgramSelectedTick = 0U;
uint32_t gLastTemperatureReadTick = 0U;
uint8_t gUserModeActive = 0U;
UserEditField gUserEditField = USER_EDIT_TEMPERATURE;
ProgramConfig gUserProgram = {1210U, 15U, 0U};
uint8_t gUserBlinkPhase = 0xffU;
uint8_t gUserDisplayRefresh = 0U;
uint32_t gUserEditTick = 0U;
uint8_t gMainCycleActive = 0U;
MainCyclePhase gMainCyclePhase = MAIN_PHASE_STANDBY;
ProgramConfig gActiveProgram = {1210U, 15U, 0U};
uint8_t gActiveProgramChannel = ACTIVE_CHANNEL_USER;
uint32_t gMainPhaseStartTick = 0U;
ProgramConfig gLastRunProgram = {1210U, 15U, 0U};
uint8_t gLastRunProgramChannel = PROGRAM_NONE;
uint8_t gCycleLedBlinkPhase = 0xffU;
uint32_t gCycleLedBlinkTick = 0U;
MainCyclePhase gMainDisplayPhase = MAIN_PHASE_STANDBY;
uint16_t gMainDisplayValue = 0xffffU;
BuzzerSequence gBuzzer = {0U, 0U, 0U, 0U, 0U, 0U};
uint8_t gSafetyErrorActive = 0U;
StartupSafetyState gStartupSafetyState = STARTUP_SAFETY_CHECK_PT100;
uint32_t gStartupSafetyStartTick = 0U;
PID_TypeDef gHoldingPid;
double gHoldingPidInput = 0.0;
double gHoldingPidOutput = 0.0;
double gHoldingPidSetpoint = 121.0;
uint32_t gHoldingPidWindowStartTick = 0U;
uint8_t gDryJacketHeaterOn = 0U;
static uint8_t gTemperatureReadFailCount = 0U;
static uint8_t gTemperatureFilterReady = 0U;
static int16_t gFilteredTemperatureTenthsC = 0;

static const ProgramConfig programPresets[PROGRAM_COUNT] = {
  {1210U, 15U, 0U}, {1210U, 20U, 15U}, {1320U, 7U, 10U},
  {1340U, 7U, 10U}, {1340U, 10U, 20U}, {1340U, 7U, 5U}
};

static const GpioOutput programLeds[PROGRAM_COUNT] = {
  {LD_P1_GPIO_Port, LD_P1_Pin}, {LD_P2_GPIO_Port, LD_P2_Pin},
  {LD_P3_GPIO_Port, LD_P3_Pin}, {LD_P4_GPIO_Port, LD_P4_Pin},
  {LD_P5_GPIO_Port, LD_P5_Pin}, {LD_P6_GPIO_Port, LD_P6_Pin}
};

static const GpioOutput cycleLeds[7] = {
  {LD_C1_GPIO_Port, LD_C1_Pin}, {LD_C2_GPIO_Port, LD_C2_Pin},
  {LD_C3_GPIO_Port, LD_C3_Pin}, {LD_C4_GPIO_Port, LD_C4_Pin},
  {LD_C5_GPIO_Port, LD_C5_Pin}, {LD_C6_GPIO_Port, LD_C6_Pin},
  {LD_C7_GPIO_Port, LD_C7_Pin}
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
void MX_USB_HOST_Process(void);

/* USER CODE BEGIN PFP */
static void DisplayErrorOnDisplay2(uint8_t code);
static void ProgramButtons_Init(void);
static void ProgramButtons_Process(uint32_t now);
static void Program_Select(uint8_t programIndex, uint32_t now);
static void ProgramLeds_Set(uint8_t programIndex);
static void ProgramDisplay_Update(uint32_t now);
static void DisplayProgramTime(TM1637Handle *display, const char prefix[2], uint16_t minutes);
static void TemperatureDisplay_Process(uint32_t now);
static void UserButtons_Init(void);
static void UserButtons_Process(uint32_t now);
static void UserMode_Enter(uint32_t now);
static void UserMode_Exit(void);
static void UserLed_Update(void);
static void UserMode_NextField(uint32_t now);
static void UserMode_Adjust(int16_t delta, uint32_t now);
static uint16_t ClampUint16(int32_t value, uint16_t minValue, uint16_t maxValue);
static void UserDisplay_Update(uint32_t now);
static void UserDisplay_MarkDirty(uint32_t now);
static void StartButton_Init(void);
static void StartButton_Process(uint32_t now);
static void MainCycle_Start(uint32_t now);
static void MainCycle_Stop(void);
static void MainCycle_EnableStopExhaust(void);
static void MainCycle_RecallLastRun(void);
static void MainCycle_Process(uint32_t now);
static void MainCycle_SetPhase(MainCyclePhase phase, uint32_t now);
static void MainCycle_ApplyOutputs(uint32_t now);
static void MainCycle_UpdateVacuumOutputs(uint32_t elapsed);
static GPIO_PinState MainCycle_GetAssistJacketHeaterState(uint32_t elapsed);
static void MainCycle_StartPidControl(uint32_t now);
static void MainCycle_UpdatePidHeaterOutput(uint32_t now);
static void MainCycle_UpdateHoldingOutputs(uint32_t now);
static void MainCycle_UpdateExhaustOutputs(uint32_t elapsed);
static void MainCycle_UpdateDryingOutputs(uint32_t elapsed);
static void MainCycle_UpdateTemperatureDisplay(uint32_t now);
static void MainCycleDisplay_Update(uint32_t now);
static void DisplayCycleChannel(void);
static void DisplayEndMessage(void);
static void DisplayReadyMessage(void);
static void DisplayChannel(uint8_t channel);
static uint16_t MainCycle_RemainingMinutes(uint32_t elapsed, uint16_t totalMinutes);
static void MainCycleLeds_Clear(void);
static void MainCycleLeds_Update(uint32_t now);
static void MainCycleLed_Set(uint8_t ledIndex, uint8_t on);
static void MainCycleHoldingLeds_Update(uint32_t elapsed, uint8_t blinkOn);
static uint8_t MainCycle_ReadTemperatureOrFail(void);
static uint8_t DoorSwitch_IsClosed(void);
static uint8_t MainCycle_CheckDoorOrFail(void);
static uint8_t Temperature_ReadFilteredTenthsC(int16_t *temperatureTenths);
static int16_t TemperatureFilter_Apply(int16_t rawTemperatureTenths);
static void TemperatureFilter_Reset(void);
static void StartupSafety_Init(uint32_t now);
static void StartupSafety_Process(uint32_t now);
static uint8_t StartupSafety_IsReady(void);
static void StartupSafety_RequestRecheck(uint32_t now);
static void StartupSafety_SetReady(void);
static uint8_t StartupSafety_CheckPt100(void);
static uint8_t MainCycle_CheckStartConditions(uint32_t now);
static uint8_t WaterSensor_HasWater(void);
static void WaterLeds_Update(void);
static void SafetyOutputs_Stop(void);
static void SafetyError_Set(uint8_t code);
static void SafetyError_Clear(void);
static void Buzzer_Play(BuzzerEvent event);
static void Buzzer_Process(uint32_t now);
static void Buzzer_Set(uint8_t on);

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
  ButtonInput_Init(&gProgramButtons[3], B_P4_GPIO_Port, B_P4_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&gProgramButtons[4], B_P5_GPIO_Port, B_P5_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&gProgramButtons[5], B_P6_GPIO_Port, B_P6_Pin, GPIO_PIN_SET);
}

static void ProgramButtons_Process(uint32_t now)
{
  for (uint8_t i = 0U; i < PROGRAM_COUNT; ++i) {
    ButtonInput_Update(&gProgramButtons[i], now, PROGRAM_DEBOUNCE_MS, PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);
    if (StartupSafety_IsReady() == 0U || gMainCycleActive != 0U || gSafetyErrorActive != 0U) {
      (void)ButtonInput_ConsumePressed(&gProgramButtons[i]);
    }
    else if (ButtonInput_ConsumePressed(&gProgramButtons[i]) != 0U) {
      Program_Select(i, now);
      Buzzer_Play(BUZZER_EVENT_BUTTON);
    }
  }
}

static void Program_Select(uint8_t programIndex, uint32_t now)
{
  if (programIndex >= PROGRAM_COUNT || gMainCycleActive != 0U) {
    return;
  }

  UserMode_Exit();
  gSelectedProgram = programIndex;
  gProgramSelectedTick = now;
  gProgramTimeDisplayPhase = 0xffU;
  ProgramLeds_Set(programIndex);
  ProgramDisplay_Update(now);
  tm1637DisplayDecimalTenths(&gDisplay2, programPresets[programIndex].temperatureTenthsC);
}

static void ProgramLeds_Set(uint8_t programIndex)
{
  for (uint8_t i = 0U; i < PROGRAM_COUNT; ++i) {
    HAL_GPIO_WritePin(programLeds[i].port, programLeds[i].pin, GPIO_PIN_RESET);
  }

  if (programIndex < PROGRAM_COUNT) {
    HAL_GPIO_WritePin(programLeds[programIndex].port, programLeds[programIndex].pin, GPIO_PIN_SET);
  }
}

static void ProgramDisplay_Update(uint32_t now)
{
  uint8_t phase;
  const ProgramConfig *program;

  if (gMainCycleActive != 0U || gUserModeActive != 0U || gSelectedProgram >= PROGRAM_COUNT) {
    return;
  }

  phase = (uint8_t)(((now - gProgramSelectedTick) / PROGRAM_TIME_ALTERNATE_MS) & 0x01U);
  if (phase == gProgramTimeDisplayPhase) {
    return;
  }

  gProgramTimeDisplayPhase = phase;
  program = &programPresets[gSelectedProgram];

  if (phase == 0U) {
    DisplayProgramTime(&gDisplay1, "St", program->sterilizeMinutes);
  }
  else {
    DisplayProgramTime(&gDisplay1, "Dr", program->dryMinutes);
  }
}

static void DisplayProgramTime(TM1637Handle *display, const char prefix[2], uint16_t minutes)
{
  uint8_t segments[4];

  if (minutes > 99U) {
    minutes = 99U;
  }

  segments[0] = SegmentForCharacter(prefix[0]);
  segments[1] = SegmentForCharacter(prefix[1]) | (1U << 7);
  segments[2] = SegmentForCharacter((char)('0' + ((minutes / 10U) % 10U)));
  segments[3] = SegmentForCharacter((char)('0' + (minutes % 10U)));
  tm1637DisplaySegments(display, segments);
}

static void TemperatureDisplay_Process(uint32_t now)
{
  if (gMainCycleActive != 0U) {
    MainCycle_UpdateTemperatureDisplay(now);
    return;
  }

  if (gUserModeActive != 0U) {
    return;
  }

  if ((now - gLastTemperatureReadTick) < TEMPERATURE_REFRESH_MS) {
    return;
  }

  gLastTemperatureReadTick = now;
  if (gSelectedProgram != PROGRAM_NONE) {
    return;
  }

  if (gSensorReady != 0U) {
	  if (Temperature_ReadFilteredTenthsC(&gTemperatureTenthsC) != 0U) {
      tm1637DisplayDecimalTenths(&gDisplay2, gTemperatureTenthsC);
    }
    else {
      DisplayErrorOnDisplay2(Max31865_ReadFault(&gMax31865, MAX31865_FAULT_NONE));
    }
  }
}

static void UserButtons_Init(void)
{
  ButtonInput_Init(&gUserButton, B_User_GPIO_Port, B_User_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&gSetButton, B_Set_GPIO_Port, B_Set_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&gUpButton, B_Up_GPIO_Port, B_Up_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&gDownButton, B_Down_GPIO_Port, B_Down_Pin, GPIO_PIN_SET);
}

static void StartButton_Init(void)
{
  ButtonInput_Init(&gStartButton, B_Start_GPIO_Port, B_Start_Pin, GPIO_PIN_SET);
}

static void UserButtons_Process(uint32_t now)
{
  ButtonInput_Update(&gUserButton, now, PROGRAM_DEBOUNCE_MS, PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);
  ButtonInput_Update(&gSetButton, now, PROGRAM_DEBOUNCE_MS, PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);
  ButtonInput_Update(&gUpButton, now, PROGRAM_DEBOUNCE_MS, PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);
  ButtonInput_Update(&gDownButton, now, PROGRAM_DEBOUNCE_MS, PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);

  if (StartupSafety_IsReady() == 0U || gMainCycleActive != 0U || gSafetyErrorActive != 0U) {
    (void)ButtonInput_ConsumePressed(&gUserButton);
    (void)ButtonInput_ConsumePressed(&gSetButton);
    (void)ButtonInput_ConsumePressed(&gUpButton);
    (void)ButtonInput_ConsumeRepeat(&gUpButton);
    (void)ButtonInput_ConsumePressed(&gDownButton);
    (void)ButtonInput_ConsumeRepeat(&gDownButton);
    return;
  }

  if (ButtonInput_ConsumePressed(&gUserButton) != 0U) {
    if (gMainCycleActive == 0U) {
      UserMode_Enter(now);
    }
    Buzzer_Play(BUZZER_EVENT_BUTTON);
  }

  if (gUserModeActive == 0U) {
    (void)ButtonInput_ConsumePressed(&gSetButton);
    (void)ButtonInput_ConsumePressed(&gUpButton);
    (void)ButtonInput_ConsumeRepeat(&gUpButton);
    (void)ButtonInput_ConsumePressed(&gDownButton);
    (void)ButtonInput_ConsumeRepeat(&gDownButton);
    return;
  }

  if (ButtonInput_ConsumePressed(&gSetButton) != 0U) {
    UserMode_NextField(now);
    Buzzer_Play(BUZZER_EVENT_BUTTON);
  }

  if (ButtonInput_ConsumePressed(&gUpButton) != 0U) {
    UserMode_Adjust(1, now);
  }
  if (ButtonInput_ConsumeRepeat(&gUpButton) != 0U) {
    UserMode_Adjust(10, now);
  }

  if (ButtonInput_ConsumePressed(&gDownButton) != 0U) {
    UserMode_Adjust(-1, now);
  }
  if (ButtonInput_ConsumeRepeat(&gDownButton) != 0U) {
    UserMode_Adjust(-10, now);
  }
}

static void UserMode_Enter(uint32_t now)
{
  if (gMainCycleActive != 0U) {
    return;
  }

  gUserModeActive = 1U;
  gSelectedProgram = PROGRAM_NONE;
  gUserEditField = USER_EDIT_TEMPERATURE;
  ProgramLeds_Set(PROGRAM_NONE);
  UserLed_Update();
  UserDisplay_MarkDirty(now);
}

static void UserMode_Exit(void)
{
  gUserModeActive = 0U;
  UserLed_Update();
}

static void UserLed_Update(void)
{
  HAL_GPIO_WritePin(LD_User_GPIO_Port, LD_User_Pin,
                    (gUserModeActive != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void UserMode_NextField(uint32_t now)
{
  if (gUserEditField == USER_EDIT_TEMPERATURE) {
    gUserEditField = USER_EDIT_STERILIZE;
  }
  else if (gUserEditField == USER_EDIT_STERILIZE) {
    gUserEditField = USER_EDIT_DRY;
  }
  else {
    gUserEditField = USER_EDIT_TEMPERATURE;
  }

  UserDisplay_MarkDirty(now);
}

static void UserMode_Adjust(int16_t delta, uint32_t now)
{
  if (gUserEditField == USER_EDIT_TEMPERATURE) {
    int16_t step = (delta > 0) ? USER_TEMPERATURE_STEP_TENTHS : (int16_t)-USER_TEMPERATURE_STEP_TENTHS;
    if (delta >= 10) {
      step = USER_TEMPERATURE_FAST_STEP_TENTHS;
    }
    else if (delta <= -10) {
      step = (int16_t)-USER_TEMPERATURE_FAST_STEP_TENTHS;
    }
    gUserProgram.temperatureTenthsC = ClampUint16((int32_t)gUserProgram.temperatureTenthsC + step,
                                                  USER_TEMPERATURE_MIN_TENTHS,
                                                  USER_TEMPERATURE_MAX_TENTHS);
  }
  else if (gUserEditField == USER_EDIT_STERILIZE) {
    int16_t step = (delta > 0) ? USER_TIME_STEP_MINUTES : (int16_t)-USER_TIME_STEP_MINUTES;
    if (delta >= 10) {
      step = USER_TIME_FAST_STEP_MINUTES;
    }
    else if (delta <= -10) {
      step = (int16_t)-USER_TIME_FAST_STEP_MINUTES;
    }
    gUserProgram.sterilizeMinutes = ClampUint16((int32_t)gUserProgram.sterilizeMinutes + step,
                                                USER_TIME_MIN_MINUTES,
                                                USER_TIME_MAX_MINUTES);
  }
  else {
    int16_t step = (delta > 0) ? USER_TIME_STEP_MINUTES : (int16_t)-USER_TIME_STEP_MINUTES;
    if (delta >= 10) {
      step = USER_TIME_FAST_STEP_MINUTES;
    }
    else if (delta <= -10) {
      step = (int16_t)-USER_TIME_FAST_STEP_MINUTES;
    }
    gUserProgram.dryMinutes = ClampUint16((int32_t)gUserProgram.dryMinutes + step,
                                          USER_TIME_MIN_MINUTES,
                                          USER_TIME_MAX_MINUTES);
  }

  UserDisplay_MarkDirty(now);
}

static uint16_t ClampUint16(int32_t value, uint16_t minValue, uint16_t maxValue)
{
  if (value < (int32_t)minValue) {
    return minValue;
  }
  if (value > (int32_t)maxValue) {
    return maxValue;
  }
  return (uint16_t)value;
}

static void UserDisplay_Update(uint32_t now)
{
  uint8_t blinkPhase;
  uint8_t blinkOn;

  if (gMainCycleActive != 0U || gUserModeActive == 0U) {
    return;
  }

  blinkPhase = (uint8_t)(((now - gUserEditTick) / USER_BLINK_MS) & 0x01U);
  if (blinkPhase == gUserBlinkPhase && gUserDisplayRefresh == 0U) {
    return;
  }

  gUserBlinkPhase = blinkPhase;
  gUserDisplayRefresh = 0U;
  blinkOn = (blinkPhase == 0U) ? 1U : 0U;

  if (gUserEditField == USER_EDIT_TEMPERATURE) {
    DisplayProgramTime(&gDisplay1, "St", gUserProgram.sterilizeMinutes);
    if (blinkOn != 0U) {
      tm1637DisplayDecimalTenths(&gDisplay2, gUserProgram.temperatureTenthsC);
    }
    else {
      tm1637Clear(&gDisplay2);
    }
  }
  else if (gUserEditField == USER_EDIT_STERILIZE) {
    tm1637DisplayDecimalTenths(&gDisplay2, gUserProgram.temperatureTenthsC);
    if (blinkOn != 0U) {
      DisplayProgramTime(&gDisplay1, "St", gUserProgram.sterilizeMinutes);
    }
    else {
      tm1637Clear(&gDisplay1);
    }
  }
  else {
    tm1637DisplayDecimalTenths(&gDisplay2, gUserProgram.temperatureTenthsC);
    if (blinkOn != 0U) {
      DisplayProgramTime(&gDisplay1, "Dr", gUserProgram.dryMinutes);
    }
    else {
      tm1637Clear(&gDisplay1);
    }
  }
}

static void UserDisplay_MarkDirty(uint32_t now)
{
  gUserBlinkPhase = 0xffU;
  gUserDisplayRefresh = 1U;
  gUserEditTick = now;
  UserDisplay_Update(now);
}

static void StartButton_Process(uint32_t now)
{
  ButtonInput_Update(&gStartButton, now, PROGRAM_DEBOUNCE_MS, PROGRAM_LONG_PRESS_MS, PROGRAM_REPEAT_MS);
  if (ButtonInput_ConsumePressed(&gStartButton) == 0U) {
    return;
  }

  if (StartupSafety_IsReady() == 0U) {
      return;
  }

  if (gSafetyErrorActive != 0U) {
    SafetyError_Clear();
    MainCycle_Stop();
    gLastTemperatureReadTick = now - TEMPERATURE_REFRESH_MS;
    Buzzer_Play(BUZZER_EVENT_BUTTON);
    return;
  }

  if (gMainCycleActive != 0U) {
	uint8_t completed = (gMainCyclePhase == MAIN_PHASE_DONE) ? 1U : 0U;
    MainCycle_Stop();
    if (completed != 0U) {
      StartupSafety_RequestRecheck(now);
      MainCycle_RecallLastRun();
    }
    else {
      MainCycle_EnableStopExhaust();
    }
    Buzzer_Play(BUZZER_EVENT_STOP);
  }
  else {
    MainCycle_Start(now);
  }
}

static void MainCycle_Start(uint32_t now)
{
  if (gUserModeActive != 0U) {
    gActiveProgram = gUserProgram;
	gActiveProgramChannel = ACTIVE_CHANNEL_USER;
  }
  else if (gSelectedProgram < PROGRAM_COUNT) {
	gActiveProgram = programPresets[gSelectedProgram];
	gActiveProgramChannel = (uint8_t)(gSelectedProgram + 1U);
  }
  else if (gLastRunProgramChannel != PROGRAM_NONE) {
	gActiveProgram = gLastRunProgram;
	gActiveProgramChannel = gLastRunProgramChannel;
  }
  else {
    gActiveProgram = gUserProgram;
    gActiveProgramChannel = ACTIVE_CHANNEL_USER;
  }

  SafetyError_Clear();
    if (MainCycle_CheckStartConditions(now) == 0U) {
      return;
   }

  gMainCycleActive = 1U;
  UserMode_Exit();
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_SET);
  tm1637Clear(&gDisplay1);
  gCycleLedBlinkPhase = 0xffU;
  gMainDisplayPhase = MAIN_PHASE_STANDBY;
  gMainDisplayValue = 0xffffU;
  gLastTemperatureReadTick = now - TEMPERATURE_REFRESH_MS;

  MainCycle_SetPhase(MAIN_PHASE_VACUUM, now);

  MainCycle_UpdateTemperatureDisplay(now);
  MainCycleDisplay_Update(now);
  MainCycleLeds_Update(now);
  Buzzer_Play(BUZZER_EVENT_START);
}

static void MainCycle_Stop(void)
{
  gMainCycleActive = 0U;
  gMainCyclePhase = MAIN_PHASE_STANDBY;
  gSelectedProgram = PROGRAM_NONE;
  gActiveProgramChannel = ACTIVE_CHANNEL_USER;
  gProgramTimeDisplayPhase = 0xffU;
  gMainDisplayPhase = MAIN_PHASE_STANDBY;
  gMainDisplayValue = 0xffffU;
  UserMode_Exit();
  SafetyOutputs_Stop();
  ProgramLeds_Set(PROGRAM_NONE);
  MainCycleLeds_Clear();
  gCycleLedBlinkPhase = 0xffU;
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
  tm1637Clear(&gDisplay1);
  tm1637Clear(&gDisplay2);
}

static void MainCycle_EnableStopExhaust(void)
{
  HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_RESET);
}

static void MainCycle_RecallLastRun(void)
{
  if (gLastRunProgramChannel == PROGRAM_NONE) {
    return;
  }

  gActiveProgram = gLastRunProgram;
  gActiveProgramChannel = gLastRunProgramChannel;
  gMainDisplayPhase = MAIN_PHASE_STANDBY;
  gMainDisplayValue = 0xffffU;
  DisplayCycleChannel();
}

static void MainCycle_Process(uint32_t now)
{
  uint32_t elapsed;

  if (gMainCycleActive == 0U) {
    return;
  }

  if (gMainCyclePhase != MAIN_PHASE_DONE && MainCycle_CheckDoorOrFail() == 0U) {
     return;
   }

  if (gMainCyclePhase != MAIN_PHASE_DONE) {
	  if (MainCycle_ReadTemperatureOrFail() == 0U) {
       return;
     }
     if (gTemperatureTenthsC >= 0 && (uint16_t)gTemperatureTenthsC > MAIN_OVER_TEMPERATURE_TENTHS) {
       SafetyError_Set(5U);
       return;
     }
   }

  elapsed = now - gMainPhaseStartTick;

  switch (gMainCyclePhase) {
    case MAIN_PHASE_VACUUM:
      if (elapsed >= MAIN_VACUUM_MS) {
        MainCycle_SetPhase(MAIN_PHASE_HEATING, now);
        return;
      }
      break;

    case MAIN_PHASE_HEATING:
      if (elapsed >= HEATING_TIMEOUT_MS) {
    	SafetyError_Set(4U);
    	return;
      }
        else if (gTemperatureTenthsC >= 0 && (uint16_t)gTemperatureTenthsC >= gActiveProgram.temperatureTenthsC) {
        MainCycle_SetPhase(MAIN_PHASE_HOLDING, now);
        return;
      }
      break;

    case MAIN_PHASE_HOLDING:
      if (elapsed >= ((uint32_t)gActiveProgram.sterilizeMinutes * MINUTE_MS)) {
        MainCycle_SetPhase(MAIN_PHASE_EXHAUST, now);
        return;
      }
      break;

    case MAIN_PHASE_EXHAUST:
      if (elapsed >= MAIN_EXHAUST_MS) {
        MainCycle_SetPhase(MAIN_PHASE_DRYING, now);
        return;
      }
      break;

    case MAIN_PHASE_DRYING:
      if (elapsed >= ((uint32_t)gActiveProgram.dryMinutes * MINUTE_MS)) {
        gLastRunProgram = gActiveProgram;
        gLastRunProgramChannel = gActiveProgramChannel;
        MainCycle_SetPhase(MAIN_PHASE_DONE, now);
        Buzzer_Play(BUZZER_EVENT_COMPLETE);
        return;
      }
      break;

    case MAIN_PHASE_DONE:
      break;

    default:
      MainCycle_Stop();
      return;
  }

  MainCycle_ApplyOutputs(now);
}

static void MainCycle_SetPhase(MainCyclePhase phase, uint32_t now)
{
  gTemperatureReadFailCount = 0U;
  gMainCyclePhase = phase;
  gMainPhaseStartTick = now;
  gCycleLedBlinkPhase = 0xffU;
  gCycleLedBlinkTick = now;
  gMainDisplayPhase = MAIN_PHASE_STANDBY;
  gMainDisplayValue = 0xffffU;
  if (phase == MAIN_PHASE_HEATING || phase == MAIN_PHASE_HOLDING) {
        MainCycle_StartPidControl(now);
    }
    else {
      PID_SetMode(&gHoldingPid, _PID_MODE_MANUAL);
      HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_RESET);
    }

    if (phase == MAIN_PHASE_DRYING) {
      gDryJacketHeaterOn = 0U;
    }

    MainCycle_ApplyOutputs(now);
  }

  static void MainCycle_ApplyOutputs(uint32_t now)
  {
    uint32_t elapsed = now - gMainPhaseStartTick;

    switch (gMainCyclePhase) {
      case MAIN_PHASE_VACUUM:
        MainCycle_UpdateVacuumOutputs(elapsed);
        break;

      case MAIN_PHASE_HEATING:
      {
        uint16_t resistorCutoffTemperatureTenthsC = 0U;

        if (gActiveProgram.temperatureTenthsC > MAIN_HEATING_RESISTOR_CUTOFF_TENTHS) {
        resistorCutoffTemperatureTenthsC = gActiveProgram.temperatureTenthsC - MAIN_HEATING_RESISTOR_CUTOFF_TENTHS;
        }

        MainCycle_UpdatePidHeaterOutput(now);
        HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin,
                                 (gTemperatureTenthsC >= 0 &&
                                 (uint16_t)gTemperatureTenthsC >= resistorCutoffTemperatureTenthsC) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_SET);
        break;
      }

      case MAIN_PHASE_HOLDING:
        MainCycle_UpdateHoldingOutputs(now);
        break;

      case MAIN_PHASE_EXHAUST:
        MainCycle_UpdateExhaustOutputs(elapsed);
        break;

      case MAIN_PHASE_DRYING:
    	  MainCycle_UpdateDryingOutputs(elapsed);
        break;

      case MAIN_PHASE_DONE:
      default:
        SafetyOutputs_Stop();
        break;
    }
  }

  static void MainCycle_UpdateVacuumOutputs(uint32_t elapsed)
  {
    uint8_t subPhase = (uint8_t)(elapsed / MAIN_VACUUM_STEP_MS);
    uint8_t heaterStep = ((subPhase & 0x01U) != 0U) ? 1U : 0U;

    HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin,
                          (heaterStep != 0U) ? MainCycle_GetAssistJacketHeaterState(elapsed) : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_SET);

    if (heaterStep != 0U) {
      HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_RESET);
    }
    else {
      HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_SET);
    }
  }

  static GPIO_PinState MainCycle_GetAssistJacketHeaterState(uint32_t elapsed)
   {
     const uint32_t cycleMs = MAIN_ASSIST_JACKET_HEATER_ON_MS + MAIN_ASSIST_JACKET_HEATER_OFF_MS;
     uint16_t cutoffTemperatureTenthsC = 0U;

     if (gActiveProgram.temperatureTenthsC > MAIN_ASSIST_JACKET_HEATER_CUTOFF_TENTHS) {
       cutoffTemperatureTenthsC = gActiveProgram.temperatureTenthsC - MAIN_ASSIST_JACKET_HEATER_CUTOFF_TENTHS;
     }

     if (gTemperatureTenthsC >= 0 && (uint16_t)gTemperatureTenthsC >= cutoffTemperatureTenthsC) {
       return GPIO_PIN_RESET;
     }

     if (cycleMs == 0U) {
       return GPIO_PIN_RESET;
     }

     return ((elapsed % cycleMs) < MAIN_ASSIST_JACKET_HEATER_ON_MS) ? GPIO_PIN_SET : GPIO_PIN_RESET;
   }

  static void MainCycle_StartPidControl(uint32_t now)
    {
      /* Ki=0 during HEATING avoids integral windup while ramping up from a
       * large initial error; the full Ki is only used once HOLDING begins,
       * where it corrects the small steady-state error needed to keep
       * temperature pinned at setpoint. */
      double ki = (gMainCyclePhase == MAIN_PHASE_HOLDING) ? MAIN_HOLD_PID_KI : MAIN_HEAT_PID_KI;

      gHoldingPidInput = (double)gTemperatureTenthsC / 10.0;
      gHoldingPidOutput = MAIN_HOLD_PID_INITIAL_OUTPUT;
      gHoldingPidSetpoint = (double)gActiveProgram.temperatureTenthsC / 10.0;
      PID2(&gHoldingPid, &gHoldingPidInput, &gHoldingPidOutput, &gHoldingPidSetpoint,
           MAIN_HOLD_PID_KP, ki, MAIN_HOLD_PID_KD, _PID_CD_DIRECT);
      PID_SetOutputLimits(&gHoldingPid, 0.0, MAIN_HOLD_PID_MAX_OUTPUT);
      PID_SetSampleTime(&gHoldingPid, MAIN_HOLD_PID_SAMPLE_MS);
      PID_SetMode(&gHoldingPid, _PID_MODE_AUTOMATIC);
      gHoldingPidWindowStartTick = now;
    }

    static void MainCycle_UpdatePidHeaterOutput(uint32_t now)
  {
    uint32_t onMs;

    gHoldingPidInput = (double)gTemperatureTenthsC / 10.0;
    gHoldingPidSetpoint = (double)gActiveProgram.temperatureTenthsC / 10.0;
    (void)PID_Compute(&gHoldingPid);

    /* Act immediately when temperature leaves the setpoint band in either direction.
         * Below setpoint, force a minimum heater duty; above setpoint, force heater off
         * and bleed the integral term so residual PID output cannot keep heating.
         * Only applies during HOLDING: this is meant to react to small disturbances
         * around an already-reached setpoint. During HEATING the ramp-up should be
         * left to the plain P+D output (Ki=0, see MainCycle_StartPidControl) so it
         * can taper off on its own before reaching the setpoint instead of being
         * forced to a floor duty. */
        if (gMainCyclePhase == MAIN_PHASE_HOLDING && gTemperatureTenthsC >= 0) {
          int16_t errorTenths = (int16_t)gActiveProgram.temperatureTenthsC - gTemperatureTenthsC;

          if (errorTenths <= -MAIN_HOLD_PID_OVERSHOOT_BOOST_ERROR_TENTHS) {
            gHoldingPidOutput = MAIN_HOLD_PID_OVERSHOOT_OUTPUT;
            gHoldingPid.OutputSum = MAIN_HOLD_PID_OVERSHOOT_OUTPUT;
          }
          else if (errorTenths <= -MAIN_HOLD_PID_OVERSHOOT_ERROR_TENTHS &&
                   gHoldingPidOutput > MAIN_HOLD_PID_OVERSHOOT_OUTPUT) {
            gHoldingPidOutput = MAIN_HOLD_PID_OVERSHOOT_OUTPUT;
            if (gHoldingPid.OutputSum > MAIN_HOLD_PID_OVERSHOOT_OUTPUT) {
              gHoldingPid.OutputSum = MAIN_HOLD_PID_OVERSHOOT_OUTPUT;
            }
          }
          else if (errorTenths >= MAIN_HOLD_PID_RECOVERY_BOOST_ERROR_TENTHS &&
                   gHoldingPidOutput < MAIN_HOLD_PID_RECOVERY_BOOST_OUTPUT) {
            gHoldingPidOutput = MAIN_HOLD_PID_RECOVERY_BOOST_OUTPUT;
          }
          else if (errorTenths >= MAIN_HOLD_PID_RECOVERY_ERROR_TENTHS &&
                   gHoldingPidOutput < MAIN_HOLD_PID_RECOVERY_MIN_OUTPUT) {
            gHoldingPidOutput = MAIN_HOLD_PID_RECOVERY_MIN_OUTPUT;
          }
        }

    if ((now - gHoldingPidWindowStartTick) >= MAIN_HOLD_PID_WINDOW_MS) {
      gHoldingPidWindowStartTick += MAIN_HOLD_PID_WINDOW_MS;
      if ((now - gHoldingPidWindowStartTick) >= MAIN_HOLD_PID_WINDOW_MS) {
        gHoldingPidWindowStartTick = now;
      }
    }

    onMs = (uint32_t)((gHoldingPidOutput * (double)MAIN_HOLD_PID_WINDOW_MS) / 255.0);
    HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin,
                      ((now - gHoldingPidWindowStartTick) < onMs) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }

   static void MainCycle_UpdateHoldingOutputs(uint32_t now)
   {
    MainCycle_UpdatePidHeaterOutput(now);
    HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_SET);
  }

  static void MainCycle_UpdateExhaustOutputs(uint32_t elapsed)
  {
    HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_RESET);

    if (elapsed < MAIN_EXHAUST_DRAIN_MS) {
      HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_RESET);
    }
    else {
      HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin,
                              MainCycle_GetAssistJacketHeaterState(elapsed - MAIN_EXHAUST_DRAIN_MS));
    }
  }

  static void MainCycle_UpdateDryingOutputs(uint32_t elapsed)
  {
    HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve1_GPIO_Port, Relay_Valve1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(Relay_Valve3_GPIO_Port, Relay_Valve3_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(Relay_Pump_GPIO_Port, Relay_Pump_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(Relay_Valve2_GPIO_Port, Relay_Valve2_Pin, GPIO_PIN_SET);

    if ((((uint32_t)gActiveProgram.dryMinutes * MINUTE_MS) <= MAIN_DRY_HEATER_CUTOFF_MS) ||
            (elapsed >= (((uint32_t)gActiveProgram.dryMinutes * MINUTE_MS) - MAIN_DRY_HEATER_CUTOFF_MS))) {
        gDryJacketHeaterOn = 0U;
      }
      else if (gTemperatureTenthsC >= 0) {
      if ((uint16_t)gTemperatureTenthsC <= MAIN_DRY_TEMPERATURE_LOW_TENTHS) {
        gDryJacketHeaterOn = 1U;
      }
      else if ((uint16_t)gTemperatureTenthsC >= MAIN_DRY_TEMPERATURE_HIGH_TENTHS) {
        gDryJacketHeaterOn = 0U;
      }
    }

    HAL_GPIO_WritePin(SSR_HResistor_GPIO_Port, SSR_HResistor_Pin,
                      (gDryJacketHeaterOn != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void MainCycle_UpdateTemperatureDisplay(uint32_t now)
{
  if ((now - gLastTemperatureReadTick) < TEMPERATURE_REFRESH_MS) {
    return;
  }

  gLastTemperatureReadTick = now;
  if (gSensorReady != 0U) {
	  if (Temperature_ReadFilteredTenthsC(&gTemperatureTenthsC) != 0U) {
      gTemperatureReadFailCount = 0U;
      tm1637DisplayDecimalTenths(&gDisplay2, gTemperatureTenthsC);
    }
    else {
      /* Read failed — could be transient relay switching noise.
       * Count consecutive failures; only raise Er01 after TEMPERATURE_READ_FAIL_MAX.
       * Keep displaying the last valid temperature in the meantime. */
      gTemperatureReadFailCount++;
      if (gTemperatureReadFailCount >= TEMPERATURE_READ_FAIL_MAX) {
        gTemperatureReadFailCount = 0U;
        SafetyError_Set(1U);
      }
    }
  }
}

static void MainCycleDisplay_Update(uint32_t now)
{
  uint16_t displayValue = 0xffffU;
  uint32_t elapsed;

  if (gMainCycleActive == 0U) {
    return;
  }

  elapsed = now - gMainPhaseStartTick;
  switch (gMainCyclePhase) {
    case MAIN_PHASE_VACUUM:
    case MAIN_PHASE_HEATING:
    case MAIN_PHASE_EXHAUST:
      if (gMainDisplayPhase != gMainCyclePhase) {
        DisplayCycleChannel();
      }
      gMainDisplayPhase = gMainCyclePhase;
      gMainDisplayValue = 0xffffU;
      break;

    case MAIN_PHASE_HOLDING:
      displayValue = MainCycle_RemainingMinutes(elapsed, gActiveProgram.sterilizeMinutes);
      if (gMainDisplayPhase != gMainCyclePhase || displayValue != gMainDisplayValue) {
        DisplayProgramTime(&gDisplay1, "St", displayValue);
      }
      gMainDisplayPhase = gMainCyclePhase;
      gMainDisplayValue = displayValue;
      break;

    case MAIN_PHASE_DRYING:
      displayValue = MainCycle_RemainingMinutes(elapsed, gActiveProgram.dryMinutes);
      if (gMainDisplayPhase != gMainCyclePhase || displayValue != gMainDisplayValue) {
        DisplayProgramTime(&gDisplay1, "Dr", displayValue);
      }
      gMainDisplayPhase = gMainCyclePhase;
      gMainDisplayValue = displayValue;
      break;

    case MAIN_PHASE_DONE:
      displayValue = (gCycleLedBlinkPhase == 0U) ? 0U : 1U;
      if (gMainDisplayPhase != gMainCyclePhase || displayValue != gMainDisplayValue) {
        if (displayValue != 0U) {
          DisplayEndMessage();
        }
        else {
          tm1637Clear(&gDisplay1);
        }
      }
      gMainDisplayPhase = gMainCyclePhase;
      gMainDisplayValue = displayValue;
      break;

    default:
      if (gMainDisplayPhase != gMainCyclePhase) {
        tm1637Clear(&gDisplay1);
      }
      gMainDisplayPhase = gMainCyclePhase;
      gMainDisplayValue = 0xffffU;
      break;
  }
}

static void DisplayCycleChannel(void)
{
  DisplayChannel(gActiveProgramChannel);
}

static void DisplayChannel(uint8_t channel)
{
  uint8_t segments[4];

  segments[0] = SegmentForCharacter('C');
  segments[1] = SegmentForCharacter('H') | (1U << 7);
  if (channel == ACTIVE_CHANNEL_USER) {
    segments[2] = SegmentForCharacter('U');
    segments[3] = SegmentForCharacter('S');
  }
  else {
    segments[2] = SegmentForCharacter('0');
    segments[3] = SegmentForCharacter((char)('0' + (channel  % 10U)));
  }

  tm1637DisplaySegments(&gDisplay1, segments);
}

static void DisplayEndMessage(void)
{
  uint8_t segments[4];

  segments[0] = 0x00U;
  segments[1] = SegmentForCharacter('E');
  segments[2] = SegmentForCharacter('n');
  segments[3] = SegmentForCharacter('d');
  tm1637DisplaySegments(&gDisplay1, segments);
}

static void DisplayReadyMessage(void)
{
  uint8_t segments[4];

  segments[0] = SegmentForCharacter('r');
  segments[1] = SegmentForCharacter('E');
  segments[2] = SegmentForCharacter('d');
  segments[3] = SegmentForCharacter('y');
  tm1637DisplaySegments(&gDisplay1, segments);
}

static uint16_t MainCycle_RemainingMinutes(uint32_t elapsed, uint16_t totalMinutes)
{
  uint32_t totalMs = (uint32_t)totalMinutes * MINUTE_MS;
  uint32_t remainingMs;

  if (elapsed >= totalMs) {
    return 0U;
  }

  remainingMs = totalMs - elapsed;
  return (uint16_t)((remainingMs + MINUTE_MS - 1U) / MINUTE_MS);
}

static void MainCycleLeds_Clear(void)
{
  for (uint8_t i = 0U; i < 7U; ++i) {
    MainCycleLed_Set(i, 0U);
  }
}

static void MainCycleLeds_Update(uint32_t now)
{
  uint8_t blinkOn;
  uint32_t elapsed;

  if (gMainCycleActive == 0U) {
    return;
  }

  if (gCycleLedBlinkPhase == 0xffU || (now - gCycleLedBlinkTick) >= MAIN_CYCLE_LED_BLINK_MS) {
    if (gCycleLedBlinkPhase == 0xffU) {
      gCycleLedBlinkPhase = 1U;
    }
    else {
      gCycleLedBlinkPhase ^= 1U;
    }
    gCycleLedBlinkTick = now;
  }

  blinkOn = gCycleLedBlinkPhase;
  elapsed = now - gMainPhaseStartTick;
  MainCycleLeds_Clear();

  switch (gMainCyclePhase) {
    case MAIN_PHASE_VACUUM:
      MainCycleLed_Set(0U, blinkOn);
      break;

    case MAIN_PHASE_HEATING:
      MainCycleLed_Set(0U, 1U);
      MainCycleLed_Set(1U, blinkOn);
      break;

    case MAIN_PHASE_HOLDING:
      MainCycleLed_Set(0U, 1U);
      MainCycleLed_Set(1U, 1U);
      MainCycleHoldingLeds_Update(elapsed, blinkOn);
      break;

    case MAIN_PHASE_EXHAUST:
      for (uint8_t i = 0U; i < 5U; ++i) {
        MainCycleLed_Set(i, 1U);
      }
      MainCycleLed_Set(5U, blinkOn);
      break;

    case MAIN_PHASE_DRYING:
      for (uint8_t i = 0U; i < 6U; ++i) {
        MainCycleLed_Set(i, 1U);
      }
      MainCycleLed_Set(6U, blinkOn);
      break;

    case MAIN_PHASE_DONE:
      for (uint8_t i = 0U; i < 7U; ++i) {
        MainCycleLed_Set(i, blinkOn);
      }
      break;

    default:
      break;
  }
}

static void MainCycleLed_Set(uint8_t ledIndex, uint8_t on)
{
  if (ledIndex >= 7U) {
    return;
  }
  HAL_GPIO_WritePin(cycleLeds[ledIndex].port, cycleLeds[ledIndex].pin,
                    (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void MainCycleHoldingLeds_Update(uint32_t elapsed, uint8_t blinkOn)
{
  uint32_t holdMs = (uint32_t)gActiveProgram.sterilizeMinutes * MINUTE_MS;
  uint32_t firstThird;
  uint32_t secondThird;

  if (holdMs == 0U) {
    for (uint8_t i = 2U; i <= 4U; ++i) {
      MainCycleLed_Set(i, 1U);
    }
    return;
  }

  firstThird = holdMs / 3U;
  secondThird = (holdMs * 2U) / 3U;
  if (firstThird == 0U) {
    firstThird = 1U;
  }
  if (secondThird <= firstThird) {
    secondThird = firstThird + 1U;
  }

  if (elapsed < firstThird) {
    MainCycleLed_Set(2U, blinkOn);
  }
  else if (elapsed < secondThird) {
    MainCycleLed_Set(2U, 1U);
    MainCycleLed_Set(3U, blinkOn);
  }
  else {
    MainCycleLed_Set(2U, 1U);
    MainCycleLed_Set(3U, 1U);
    MainCycleLed_Set(4U, blinkOn);
  }
}

static uint8_t MainCycle_ReadTemperatureOrFail(void)
{
	if (gSensorReady == 0U || Temperature_ReadFilteredTenthsC(&gTemperatureTenthsC) == 0U) {
    SafetyError_Set(1U);
    return 0U;
  }
  return 1U;
}

static uint8_t Temperature_ReadFilteredTenthsC(int16_t *temperatureTenths)
{
  int16_t rawTemperatureTenths;

  if (temperatureTenths == NULL) {
    return 0U;
  }

  if (Max31865_ReadTemperatureTenthsC(&gMax31865, &rawTemperatureTenths) == 0U) {
    return 0U;
  }

  *temperatureTenths = TemperatureFilter_Apply(rawTemperatureTenths);
  return 1U;
}

static int16_t TemperatureFilter_Apply(int16_t rawTemperatureTenths)
{
  int32_t delta;
  int32_t filteredTemperature;

  if (gTemperatureFilterReady == 0U) {
    gFilteredTemperatureTenthsC = rawTemperatureTenths;
    gTemperatureFilterReady = 1U;
    return gFilteredTemperatureTenthsC;
  }

  delta = (int32_t)rawTemperatureTenths - (int32_t)gFilteredTemperatureTenthsC;
  if (delta > TEMPERATURE_FILTER_MAX_STEP_TENTHS) {
    delta = TEMPERATURE_FILTER_MAX_STEP_TENTHS;
  }
  else if (delta < -(int32_t)TEMPERATURE_FILTER_MAX_STEP_TENTHS) {
    delta = -(int32_t)TEMPERATURE_FILTER_MAX_STEP_TENTHS;
  }

  filteredTemperature = (int32_t)gFilteredTemperatureTenthsC +
      ((delta * TEMPERATURE_FILTER_ALPHA_NUMERATOR) / TEMPERATURE_FILTER_ALPHA_DENOMINATOR);
  gFilteredTemperatureTenthsC = (int16_t)filteredTemperature;

  return gFilteredTemperatureTenthsC;
}

static void TemperatureFilter_Reset(void)
{
  gTemperatureFilterReady = 0U;
  gFilteredTemperatureTenthsC = 0;
}

static uint8_t DoorSwitch_IsClosed(void)
{
  return (HAL_GPIO_ReadPin(L_Switch_GPIO_Port, L_Switch_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t MainCycle_CheckDoorOrFail(void)
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
  if (MainCycle_ReadTemperatureOrFail() == 0U) {
    return 0U;
  }

  tm1637DisplayDecimalTenths(&gDisplay2, gTemperatureTenthsC);
  if (gTemperatureTenthsC >= 0 && (uint16_t)gTemperatureTenthsC > MAIN_OVER_TEMPERATURE_TENTHS) {
    SafetyError_Set(5U);
    return 0U;
  }

  return 1U;
}

static uint8_t MainCycle_CheckStartConditions(uint32_t now)
{
  SafetyOutputs_Stop();

  if (MainCycle_CheckDoorOrFail() == 0U) {
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
  gMainCycleActive = 0U;
  gMainCyclePhase = MAIN_PHASE_STANDBY;
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
  MX_USB_HOST_Init();
  MX_SPI3_Init();
  /* USER CODE BEGIN 2 */
  tm1637Init(&gDisplay1, TM1637_DISPLAY_1);
  tm1637Init(&gDisplay2, TM1637_DISPLAY_2);
  tm1637SetBrightness(&gDisplay1, 8);
  tm1637SetBrightness(&gDisplay2, 8);
  tm1637Clear(&gDisplay1);
  tm1637Clear(&gDisplay2);
  ProgramButtons_Init();
  UserButtons_Init();
  StartButton_Init();
  ProgramLeds_Set(PROGRAM_NONE);
  UserLed_Update();
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(LD_HW_GPIO_Port, LD_HW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_LW_GPIO_Port, LD_LW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_Alarm_GPIO_Port, LD_Alarm_Pin, GPIO_PIN_RESET);
  SafetyOutputs_Stop();
  HAL_GPIO_WritePin(LD_HW_GPIO_Port, LD_HW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_LW_GPIO_Port, LD_LW_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_Alarm_GPIO_Port, LD_Alarm_Pin, GPIO_PIN_RESET);

  Max31865_Init(&gMax31865, &hspi3, CS_GPIO_Port, CS_Pin, 430.0f, 100.0f);
  TemperatureFilter_Reset();
  gSensorReady = Max31865_Begin(&gMax31865, MAX31865_2WIRE, 1U);
  StartupSafety_Init(HAL_GetTick());

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    MX_USB_HOST_Process();

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    StartupSafety_Process(now);
    WaterLeds_Update();
    StartButton_Process(now);
    ProgramButtons_Process(now);
    UserButtons_Process(now);
    UserLed_Update();
    ProgramDisplay_Update(now);
    UserDisplay_Update(now);
    MainCycle_Process(now);
    TemperatureDisplay_Process(now);
    MainCycleDisplay_Update(now);
    MainCycleLeds_Update(now);
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
