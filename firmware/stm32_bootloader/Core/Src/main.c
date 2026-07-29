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
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_BASE_ADDR       0x08020000UL
#define APP_MAX_SIZE        0x000E0000UL
#define APP_END_ADDR        (APP_BASE_ADDR + APP_MAX_SIZE)

#define SRAM_START_ADDR     0x20000000UL
#define SRAM_END_ADDR       0x20020000UL

#define CCM_START_ADDR      0x10000000UL
#define CCM_END_ADDR        0x10010000UL

/*
 * M1/M2 jump validation keeps text logs enabled. Set to 0 before USART1
 * enters Modbus RTU protocol mode; protocol mode must never emit raw text.
 */
#ifndef LOG_ENABLE
    #define LOG_ENABLE  0   
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#if LOG_ENABLE
//#define BOOT_LOG(...)       printf(__VA_ARGS__)
#define BOOT_LOG(fmt, ...)   printf("[BOOT] " fmt "\r\n", ##__VA_ARGS__)
#else
#define BOOT_LOG(...)       ((void)0)
#endif
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static uint8_t Boot_IsAppValid(void);
static void Boot_JumpToApp(void);

/*
 * ARM Compiler 5???????
 * R0 = APP MSP
 * R1 = APP Reset_Handler
 */
void Boot_BranchToApp(uint32_t app_msp, uint32_t app_reset);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
	BOOT_LOG("\r\nBootloader started");
	BOOT_LOG("APP address: 0x%08lX",
					 (unsigned long)APP_BASE_ADDR);
				 
		if (Boot_IsAppValid() != 0U)
	{
			BOOT_LOG("APP is valid, jumping...\r\n");

			Boot_JumpToApp();
	}
	else
	{
		BOOT_LOG("No valid APP");
	}
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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

/* USER CODE BEGIN 4 */
int fputc(int ch, FILE *stream)
{
    uint8_t data = (uint8_t)ch;

    (void)stream;

    if (HAL_UART_Transmit(&huart1,
                          &data,
                          1U,
                          10U) != HAL_OK)
    {
        return EOF;
    }

    return ch;
}

static uint8_t Boot_IsAppValid(void)
{
    uint32_t app_msp;
    uint32_t app_reset;
    uint32_t reset_address;
    uint8_t msp_valid;
    uint8_t reset_valid;

    app_msp = *(volatile uint32_t *)APP_BASE_ADDR;
    app_reset = *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

    /*
     * APP???????SRAM,?????CCM RAM?
     * ??????RAM?????
     */
    msp_valid =
        (((app_msp >= SRAM_START_ADDR) &&
          (app_msp <= SRAM_END_ADDR)) ||
         ((app_msp >= CCM_START_ADDR) &&
          (app_msp <= CCM_END_ADDR))) ? 1U : 0U;

    /*
     * Cortex-M?????????1,??Thumb???
     * ??????????????
     */
    reset_address = app_reset & ~1UL;

    reset_valid =
        (((app_reset & 1UL) != 0UL) &&
         (reset_address >= APP_BASE_ADDR) &&
         (reset_address < APP_END_ADDR)) ? 1U : 0U;

    return ((msp_valid != 0U) &&
            (reset_valid != 0U)) ? 1U : 0U;
}

static void Boot_JumpToApp(void)
{
    uint32_t app_msp;
    uint32_t app_reset;
    uint32_t index;

    if (Boot_IsAppValid() == 0U)
    {
        return;
    }

    app_msp =
        *(volatile uint32_t *)APP_BASE_ADDR;

    app_reset =
        *(volatile uint32_t *)(APP_BASE_ADDR + 4U);

    /*
     * ???????????
     * ??printf???USART1??????
     */

    /*
     * ??Bootloader?????HAL????
     * ??????????,????HAL???????
     */
    HAL_RCC_DeInit();
    HAL_DeInit();

    /* ??SysTick */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* ????,????NVIC */
    __disable_irq();

    /*
     * ????????????
     * Cortex-M4 NVIC???8?32?????
     */
    for (index = 0U; index < 8U; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }

    /* ?????SysTick?PendSV */
    SCB->ICSR =
        SCB_ICSR_PENDSTCLR_Msk |
        SCB_ICSR_PENDSVCLR_Msk;

    /* ????????? */
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);

    /* ??APP?????? */
    SCB->VTOR = APP_BASE_ADDR;

    __DSB();
    __ISB();

    /*
     * ???????:
     * 1. ???MSP
     * 2. ??????
     * 3. ??Reset_Handler
     */
    Boot_BranchToApp(app_msp, app_reset);

    /* ????????? */
    while (1)
    {
    }
}

__asm void Boot_BranchToApp(uint32_t app_msp,
                            uint32_t app_reset)
{
    /* Thread????MSP,??????? */
    MOVS    R2, #0
    MSR     CONTROL, R2
    ISB

    /* ??APP?????? */
    MSR     MSP, R0

    /* ??PRIMASK,?????? */
    CPSIE   I

    /* ???APP Reset_Handler */
    BX      R1
}

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
#ifdef USE_FULL_ASSERT
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
