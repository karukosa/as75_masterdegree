/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Heater step-response test firmware
  ******************************************************************************
  */
#include "main.h"

#include "button_input.h"
#include "heater_test_log.h"
#include "max31865.h"
#include "tm1637.h"

#define TEST_POWER_COUNT 3U
#define BUTTON_DEBOUNCE_MS 30U
#define BUTTON_LONG_PRESS_MS 1000U
#define BUTTON_REPEAT_MS 500U
#define MINUTE_MS 60000U
#define OVER_TEMPERATURE_TENTHS 1380U
#define TEMPERATURE_FILTER_ALPHA_NUMERATOR 1
#define TEMPERATURE_FILTER_ALPHA_DENOMINATOR 4
#define TEMPERATURE_FILTER_MAX_STEP_TENTHS 50
#define HEATER_TEST_SAMPLE_MS 10000U
#define HEATER_TEST_SAFETY_CHECK_MS 300U
#define HEATER_TEST_WINDOW_MS 10000U
#define HEATER_TEST_DURATION_MS (25U * MINUTE_MS)

typedef struct {
  GPIO_TypeDef *port;
  uint16_t pin;
} GpioOutput;

typedef enum {
  STARTUP_SAFETY_CHECK_PT100 = 0,
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

SPI_HandleTypeDef hspi3;

static Max31865Handle max31865;
static TM1637Handle display1;
static TM1637Handle display2;
static ButtonInput powerButtons[TEST_POWER_COUNT];
static ButtonInput startButton;
static int16_t temperatureTenthsC;
static uint8_t sensorReady;
static uint8_t safetyErrorActive;
static StartupSafetyState startupSafetyState;
static BuzzerSequence buzzer;
static uint8_t temperatureFilterReady;
static int16_t filteredTemperatureTenthsC;
static uint8_t heaterTestPowerPercent = 100U;
static uint8_t heaterTestRunning;
static uint8_t heaterTestHeaterOn;
static uint32_t heaterTestStartTick;
static uint32_t heaterTestLastSampleTick;
static uint32_t heaterTestLastSafetyCheckTick;

static const GpioOutput powerLeds[TEST_POWER_COUNT] = {
  {LD_P1_GPIO_Port, LD_P1_Pin},
  {LD_P2_GPIO_Port, LD_P2_Pin},
  {LD_P3_GPIO_Port, LD_P3_Pin}
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

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI3_Init(void);

static void SafetyOutputs_Stop(void);
static void Buzzer_Play(BuzzerEvent event);
static void HeaterTest_DisplayPower(void);

static uint8_t SegmentForCharacter(char character)
{
  switch (character) {
    case '0': return 0x3fU;
    case '1': return 0x06U;
    case '4': return 0x66U;
    case '7': return 0x07U;
    case 'H': return 0x76U;
    default: return 0x00U;
  }
}

static void DisplayError(uint8_t code)
{
  uint8_t segments[4] = {0x79U, 0x50U, 0x3fU, 0x00U};
  static const uint8_t digits[10] = {
    0x3fU, 0x06U, 0x5bU, 0x4fU, 0x66U,
    0x6dU, 0x7dU, 0x07U, 0x7fU, 0x6fU
  };

  segments[2] = digits[(code / 10U) % 10U];
  segments[3] = digits[code % 10U];
  tm1637DisplaySegments(&display2, segments);
}

static void Buttons_Init(void)
{
  ButtonInput_Init(&powerButtons[0], B_P1_GPIO_Port, B_P1_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&powerButtons[1], B_P2_GPIO_Port, B_P2_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&powerButtons[2], B_P3_GPIO_Port, B_P3_Pin, GPIO_PIN_SET);
  ButtonInput_Init(&startButton, B_Start_GPIO_Port, B_Start_Pin, GPIO_PIN_SET);
}

static void PowerLed_Select(uint8_t selected)
{
  uint8_t index;

  for (index = 0U; index < TEST_POWER_COUNT; ++index) {
    HAL_GPIO_WritePin(powerLeds[index].port, powerLeds[index].pin,
                      (index == selected) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  }
}

static int16_t TemperatureFilter_Apply(int16_t raw)
{
  int32_t delta;

  if (temperatureFilterReady == 0U) {
    filteredTemperatureTenthsC = raw;
    temperatureFilterReady = 1U;
    return raw;
  }

  delta = (int32_t)raw - filteredTemperatureTenthsC;
  if (delta > TEMPERATURE_FILTER_MAX_STEP_TENTHS) {
    delta = TEMPERATURE_FILTER_MAX_STEP_TENTHS;
  } else if (delta < -TEMPERATURE_FILTER_MAX_STEP_TENTHS) {
    delta = -TEMPERATURE_FILTER_MAX_STEP_TENTHS;
  }
  filteredTemperatureTenthsC += (int16_t)((delta * TEMPERATURE_FILTER_ALPHA_NUMERATOR) /
                                           TEMPERATURE_FILTER_ALPHA_DENOMINATOR);
  return filteredTemperatureTenthsC;
}

static uint8_t Temperature_Read(int16_t *temperature)
{
  int16_t raw;

  if (temperature == NULL || Max31865_ReadTemperatureTenthsC(&max31865, &raw) == 0U) {
    return 0U;
  }
  *temperature = TemperatureFilter_Apply(raw);
  return 1U;
}

static void SafetyOutputs_Stop(void)
{
  HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_RESET);
}

static void SafetyError_Set(uint8_t code)
{
  safetyErrorActive = 1U;
  SafetyOutputs_Stop();
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
  DisplayError(code);
  Buzzer_Play(BUZZER_EVENT_ERROR);
}

static void SafetyError_Clear(void)
{
  safetyErrorActive = 0U;
  Buzzer_Play(BUZZER_EVENT_OFF);
}

static uint8_t Temperature_Check(void)
{
  if (sensorReady == 0U || Temperature_Read(&temperatureTenthsC) == 0U) {
    SafetyError_Set(1U);
    return 0U;
  }
  if (temperatureTenthsC > (int16_t)OVER_TEMPERATURE_TENTHS) {
    SafetyError_Set(5U);
    return 0U;
  }
  tm1637DisplayDecimalTenths(&display2, temperatureTenthsC);
  return 1U;
}

static void StartupSafety_SetReady(void)
{
  SafetyOutputs_Stop();
  startupSafetyState = STARTUP_SAFETY_READY;
  HeaterTest_DisplayPower();
  Buzzer_Play(BUZZER_EVENT_READY);
}

static void StartupSafety_Process(void)
{
  if (startupSafetyState == STARTUP_SAFETY_READY ||
      startupSafetyState == STARTUP_SAFETY_ERROR) {
    return;
  }

  SafetyOutputs_Stop();
  if (Temperature_Check() != 0U) {
    StartupSafety_SetReady();
  } else {
    startupSafetyState = STARTUP_SAFETY_ERROR;
  }
}

static void StartupSafety_RequestRecheck(void)
{
  startupSafetyState = STARTUP_SAFETY_CHECK_PT100;
  StartupSafety_Process();
}

static void Buzzer_Set(uint8_t on)
{
  HAL_GPIO_WritePin(Buzzer_GPIO_Port, Buzzer_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Buzzer_Play(BuzzerEvent event)
{
  const BuzzerPattern *pattern;

  if (event == BUZZER_EVENT_OFF) {
    buzzer = (BuzzerSequence){0};
    Buzzer_Set(0U);
    return;
  }
  if (event >= BUZZER_EVENT_COUNT ||
      (safetyErrorActive != 0U && event != BUZZER_EVENT_ERROR)) {
    return;
  }
  pattern = &buzzerPatterns[event];
  buzzer.remainingPulses = pattern->pulseCount;
  buzzer.active = 1U;
  buzzer.outputOn = 1U;
  buzzer.onMs = pattern->onMs;
  buzzer.offMs = pattern->offMs;
  buzzer.lastToggleTick = HAL_GetTick();
  Buzzer_Set(1U);
}

static void Buzzer_Process(uint32_t now)
{
  if (buzzer.active == 0U) {
    Buzzer_Set(0U);
  } else if (buzzer.outputOn != 0U && (now - buzzer.lastToggleTick) >= buzzer.onMs) {
    Buzzer_Set(0U);
    buzzer.outputOn = 0U;
    buzzer.lastToggleTick = now;
    if (buzzer.remainingPulses > 0U) {
      --buzzer.remainingPulses;
    }
  } else if (buzzer.outputOn == 0U && (now - buzzer.lastToggleTick) >= buzzer.offMs) {
    if (buzzer.remainingPulses == 0U) {
      buzzer.active = 0U;
    } else {
      Buzzer_Set(1U);
      buzzer.outputOn = 1U;
      buzzer.lastToggleTick = now;
    }
  }
}

static void HeaterTest_DisplayPower(void)
{
  uint8_t segments[4] = {SegmentForCharacter('H'), 0U, 0U, 0U};

  if (heaterTestPowerPercent == 100U) {
    segments[1] = SegmentForCharacter('1');
    segments[2] = SegmentForCharacter('0');
  } else {
    segments[2] = SegmentForCharacter((char)('0' + heaterTestPowerPercent / 10U));
  }
  segments[3] = SegmentForCharacter('0');
  tm1637DisplaySegments(&display1, segments);
}

static void HeaterTest_LogSample(uint32_t now, HeaterTestLogStatus status)
{
  if (HeaterTestLog_Append(now - heaterTestStartTick, temperatureTenthsC,
                           heaterTestPowerPercent, heaterTestHeaterOn, status) == 0U) {
    SafetyOutputs_Stop();
    heaterTestRunning = 0U;
  }
}

static void HeaterTest_Stop(HeaterTestLogStatus status, uint32_t now)
{
  if (heaterTestRunning != 0U) {
    HeaterTest_LogSample(now, status);
  }
  heaterTestRunning = 0U;
  heaterTestHeaterOn = 0U;
  SafetyOutputs_Stop();
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
  HeaterTestLog_MarkComplete();
}

static void HeaterTest_Start(uint32_t now)
{
  SafetyOutputs_Stop();
  if (Temperature_Check() == 0U) {
    return;
  }

  heaterTestStartTick = now;
  heaterTestLastSampleTick = now - HEATER_TEST_SAMPLE_MS;
  heaterTestLastSafetyCheckTick = now - HEATER_TEST_SAFETY_CHECK_MS;
  HeaterTestLog_Reset();
  heaterTestRunning = 1U;
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_SET);
  Buzzer_Play(BUZZER_EVENT_START);
}

static void HeaterTest_ApplyPower(uint32_t now)
{
  uint32_t elapsed = (now - heaterTestStartTick) % HEATER_TEST_WINDOW_MS;
  uint32_t onTime = HEATER_TEST_WINDOW_MS * heaterTestPowerPercent / 100U;

  heaterTestHeaterOn = (elapsed < onTime) ? 1U : 0U;
  HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin,
                    heaterTestHeaterOn ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void HeaterTest_Process(uint32_t now)
{
  static const uint8_t powers[TEST_POWER_COUNT] = {100U, 70U, 40U};
  uint8_t index;
  uint8_t startPressed;

  for (index = 0U; index < TEST_POWER_COUNT; ++index) {
    ButtonInput_Update(&powerButtons[index], now, BUTTON_DEBOUNCE_MS,
                       BUTTON_LONG_PRESS_MS, BUTTON_REPEAT_MS);
    if (ButtonInput_ConsumePressed(&powerButtons[index]) != 0U &&
        heaterTestRunning == 0U && startupSafetyState == STARTUP_SAFETY_READY) {
      heaterTestPowerPercent = powers[index];
      PowerLed_Select(index);
      HeaterTest_DisplayPower();
      Buzzer_Play(BUZZER_EVENT_BUTTON);
    }
  }

  ButtonInput_Update(&startButton, now, BUTTON_DEBOUNCE_MS,
                     BUTTON_LONG_PRESS_MS, BUTTON_REPEAT_MS);
  startPressed = ButtonInput_ConsumePressed(&startButton);
  if (startPressed != 0U && safetyErrorActive != 0U) {
    SafetyError_Clear();
    StartupSafety_RequestRecheck();
    Buzzer_Play(BUZZER_EVENT_BUTTON);
  } else if (startPressed != 0U && startupSafetyState == STARTUP_SAFETY_READY) {
    if (heaterTestRunning != 0U) {
      HeaterTest_Stop(HEATER_LOG_STATUS_STOP, now);
      Buzzer_Play(BUZZER_EVENT_STOP);
    } else {
      HeaterTest_Start(now);
    }
  }

  if (heaterTestRunning == 0U) {
    return;
  }
  if ((now - heaterTestStartTick) >= HEATER_TEST_DURATION_MS) {
    HeaterTest_Stop(HEATER_LOG_STATUS_DONE, now);
    Buzzer_Play(BUZZER_EVENT_COMPLETE);
    return;
  }

  HeaterTest_ApplyPower(now);
  if ((now - heaterTestLastSafetyCheckTick) < HEATER_TEST_SAFETY_CHECK_MS) {
    return;
  }
  heaterTestLastSafetyCheckTick = now;
  if (Temperature_Check() == 0U) {
    HeaterTest_Stop((temperatureTenthsC > (int16_t)OVER_TEMPERATURE_TENTHS) ?
                    HEATER_LOG_STATUS_OVER_TEMP : HEATER_LOG_STATUS_SENSOR_ERROR, now);
    return;
  }
  if ((now - heaterTestLastSampleTick) >= HEATER_TEST_SAMPLE_MS) {
    heaterTestLastSampleTick = now;
    HeaterTest_LogSample(now, HEATER_LOG_STATUS_RUN);
  }
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SPI3_Init();

  tm1637Init(&display1, TM1637_DISPLAY_1);
  tm1637Init(&display2, TM1637_DISPLAY_2);
  tm1637SetBrightness(&display1, 8);
  tm1637SetBrightness(&display2, 8);
  tm1637Clear(&display1);
  tm1637Clear(&display2);
  Buttons_Init();
  PowerLed_Select(0U);
  HAL_GPIO_WritePin(LD_Start_GPIO_Port, LD_Start_Pin, GPIO_PIN_RESET);
  SafetyOutputs_Stop();

  Max31865_Init(&max31865, &hspi3, CS_GPIO_Port, CS_Pin, 430.0f, 100.0f);
  temperatureFilterReady = 0U;
  sensorReady = Max31865_Begin(&max31865, MAX31865_2WIRE, 1U);
  startupSafetyState = STARTUP_SAFETY_CHECK_PT100;
  StartupSafety_Process();
  HeaterTest_DisplayPower();

  while (1) {
    uint32_t now = HAL_GetTick();
    StartupSafety_Process();
    HeaterTest_Process(now);
    Buzzer_Process(now);
  }
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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SSR_Heater_GPIO_Port, SSR_Heater_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, Buzzer_Pin|CLK1_Pin|DIO1_Pin|CLK2_Pin
                          |DIO2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LD_P3_Pin|LD_P2_Pin|LD_P1_Pin|LD_Start_Pin,
                    GPIO_PIN_RESET);

  /*Configure GPIO pin : SSR_Heater_Pin */
  GPIO_InitStruct.Pin = SSR_Heater_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pins : B_P1_Pin B_P2_Pin B_P3_Pin B_Start_Pin */
  GPIO_InitStruct.Pin = B_P1_Pin|B_P2_Pin|B_P3_Pin|B_Start_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : CS_Pin */
  GPIO_InitStruct.Pin = CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BOOT1_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin;
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

  /*Configure GPIO pins : LD_P3_Pin LD_P2_Pin LD_P1_Pin LD_Start_Pin */
  GPIO_InitStruct.Pin = LD_P3_Pin|LD_P2_Pin|LD_P1_Pin|LD_Start_Pin;
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
