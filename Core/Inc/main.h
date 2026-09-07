/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LD_C3_Pin GPIO_PIN_2
#define LD_C3_GPIO_Port GPIOE
#define LD_C4_Pin GPIO_PIN_3
#define LD_C4_GPIO_Port GPIOE
#define LD_C5_Pin GPIO_PIN_4
#define LD_C5_GPIO_Port GPIOE
#define LD_C6_Pin GPIO_PIN_5
#define LD_C6_GPIO_Port GPIOE
#define LD_C7_Pin GPIO_PIN_6
#define LD_C7_GPIO_Port GPIOE
#define PC14_OSC32_IN_Pin GPIO_PIN_14
#define PC14_OSC32_IN_GPIO_Port GPIOC
#define PC15_OSC32_OUT_Pin GPIO_PIN_15
#define PC15_OSC32_OUT_GPIO_Port GPIOC
#define PH0_OSC_IN_Pin GPIO_PIN_0
#define PH0_OSC_IN_GPIO_Port GPIOH
#define PH1_OSC_OUT_Pin GPIO_PIN_1
#define PH1_OSC_OUT_GPIO_Port GPIOH
#define B_P1_Pin GPIO_PIN_0
#define B_P1_GPIO_Port GPIOC
#define B_P2_Pin GPIO_PIN_1
#define B_P2_GPIO_Port GPIOC
#define B_P3_Pin GPIO_PIN_2
#define B_P3_GPIO_Port GPIOC
#define B_P4_Pin GPIO_PIN_3
#define B_P4_GPIO_Port GPIOC
#define B1_Pin GPIO_PIN_0
#define B1_GPIO_Port GPIOA
#define CS_Pin GPIO_PIN_4
#define CS_GPIO_Port GPIOA
#define B_P5_Pin GPIO_PIN_4
#define B_P5_GPIO_Port GPIOC
#define B_P6_Pin GPIO_PIN_5
#define B_P6_GPIO_Port GPIOC
#define BOOT1_Pin GPIO_PIN_2
#define BOOT1_GPIO_Port GPIOB
#define LD_Alarm_Pin GPIO_PIN_7
#define LD_Alarm_GPIO_Port GPIOE
#define LD_LW_Pin GPIO_PIN_8
#define LD_LW_GPIO_Port GPIOE
#define LD_HW_Pin GPIO_PIN_9
#define LD_HW_GPIO_Port GPIOE
#define SSR_Heater_Pin GPIO_PIN_10
#define SSR_Heater_GPIO_Port GPIOE
#define SSR_HResistor_Pin GPIO_PIN_11
#define SSR_HResistor_GPIO_Port GPIOE
#define Relay_Valve1_Pin GPIO_PIN_12
#define Relay_Valve1_GPIO_Port GPIOE
#define Relay_Valve2_Pin GPIO_PIN_13
#define Relay_Valve2_GPIO_Port GPIOE
#define Relay_Valve3_Pin GPIO_PIN_14
#define Relay_Valve3_GPIO_Port GPIOE
#define Buzzer_Pin GPIO_PIN_10
#define Buzzer_GPIO_Port GPIOB
#define Water_S_Pin GPIO_PIN_12
#define Water_S_GPIO_Port GPIOB
#define L_Switch_Pin GPIO_PIN_13
#define L_Switch_GPIO_Port GPIOB
#define Relay_Pump_Pin GPIO_PIN_8
#define Relay_Pump_GPIO_Port GPIOD
#define LD4_Pin GPIO_PIN_12
#define LD4_GPIO_Port GPIOD
#define LD3_Pin GPIO_PIN_13
#define LD3_GPIO_Port GPIOD
#define LD5_Pin GPIO_PIN_14
#define LD5_GPIO_Port GPIOD
#define LD6_Pin GPIO_PIN_15
#define LD6_GPIO_Port GPIOD
#define B_Start_Pin GPIO_PIN_6
#define B_Start_GPIO_Port GPIOC
#define B_Set_Pin GPIO_PIN_7
#define B_Set_GPIO_Port GPIOC
#define B_Up_Pin GPIO_PIN_8
#define B_Up_GPIO_Port GPIOC
#define B_Down_Pin GPIO_PIN_9
#define B_Down_GPIO_Port GPIOC
#define VBUS_FS_Pin GPIO_PIN_9
#define VBUS_FS_GPIO_Port GPIOA
#define OTG_FS_ID_Pin GPIO_PIN_10
#define OTG_FS_ID_GPIO_Port GPIOA
#define OTG_FS_DM_Pin GPIO_PIN_11
#define OTG_FS_DM_GPIO_Port GPIOA
#define OTG_FS_DP_Pin GPIO_PIN_12
#define OTG_FS_DP_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define B_User_Pin GPIO_PIN_12
#define B_User_GPIO_Port GPIOC
#define LD_P3_Pin GPIO_PIN_0
#define LD_P3_GPIO_Port GPIOD
#define LD_P2_Pin GPIO_PIN_1
#define LD_P2_GPIO_Port GPIOD
#define LD_P1_Pin GPIO_PIN_2
#define LD_P1_GPIO_Port GPIOD
#define LD_P4_Pin GPIO_PIN_3
#define LD_P4_GPIO_Port GPIOD
#define LD_P5_Pin GPIO_PIN_4
#define LD_P5_GPIO_Port GPIOD
#define LD_P6_Pin GPIO_PIN_5
#define LD_P6_GPIO_Port GPIOD
#define LD_Start_Pin GPIO_PIN_6
#define LD_Start_GPIO_Port GPIOD
#define LD_User_Pin GPIO_PIN_7
#define LD_User_GPIO_Port GPIOD
#define CLK1_Pin GPIO_PIN_6
#define CLK1_GPIO_Port GPIOB
#define DIO1_Pin GPIO_PIN_7
#define DIO1_GPIO_Port GPIOB
#define CLK2_Pin GPIO_PIN_8
#define CLK2_GPIO_Port GPIOB
#define DIO2_Pin GPIO_PIN_9
#define DIO2_GPIO_Port GPIOB
#define LD_C1_Pin GPIO_PIN_0
#define LD_C1_GPIO_Port GPIOE
#define LD_C2_Pin GPIO_PIN_1
#define LD_C2_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
