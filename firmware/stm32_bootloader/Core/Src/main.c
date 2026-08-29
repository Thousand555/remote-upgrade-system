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
#include "flash_layout.h"
#include "flash_if.h"
#include "flash_if_self_test.h"
#include "boot_metadata.h"
#include "boot_metadata_self_test.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SRAM_START_ADDR     0x20000000UL
#define SRAM_END_ADDR       0x20020000UL

#define CCM_START_ADDR      0x10000000UL
#define CCM_END_ADDR        0x10010000UL

/*
 * The debug target defines LOG_ENABLE=1 for M1/M2 diagnostics. The normal
 * target keeps the default zero; protocol mode must never emit raw text.
 */
#ifndef LOG_ENABLE
    #define LOG_ENABLE  0
#endif

#if FLASH_IF_SELF_TEST_ENABLE && BOOT_METADATA_SELF_TEST_ENABLE
#error "Enable only one destructive Flash self-test at a time"
#endif
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#if LOG_ENABLE
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
#if !FLASH_IF_SELF_TEST_ENABLE && !BOOT_METADATA_SELF_TEST_ENABLE
static uint8_t Boot_IsAppValid(void);
static void Boot_JumpToApp(void);

/*
 * ARM Compiler 5 passes the first two arguments in R0 and R1.
 * R0 = APP MSP
 * R1 = APP Reset_Handler
 */
void Boot_BranchToApp(uint32_t app_msp, uint32_t app_reset);
#endif
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
	BOOT_LOG("Bootloader started");
	BOOT_LOG("APP address: 0x%08lX",
					 (unsigned long)FLASH_LAYOUT_APP_BASE_ADDR);

#if FLASH_IF_SELF_TEST_ENABLE
	BOOT_LOG("WARNING: destructive Flash self-test enabled");
	{
		flash_if_status_t self_test_status;
		uint32_t self_test_started_at;
		uint32_t self_test_elapsed_ms;

		self_test_started_at = HAL_GetTick();
		self_test_status = flash_if_self_test_run();
		self_test_elapsed_ms = HAL_GetTick() - self_test_started_at;

		BOOT_LOG("Flash self-test status=%u, elapsed=%lu ms, HAL=0x%08lX, failed=0x%08lX",
				 (unsigned int)self_test_status,
				 (unsigned long)self_test_elapsed_ms,
				 (unsigned long)flash_if_get_last_hal_error(),
				 (unsigned long)flash_if_get_last_failure_address());
	}
#elif BOOT_METADATA_SELF_TEST_ENABLE
	BOOT_LOG("WARNING: destructive Metadata self-test enabled");
	{
		boot_metadata_record_t latest_record;
		boot_metadata_status_t metadata_status;
		uint32_t free_record_count;
		uint32_t self_test_started_at;
		uint32_t self_test_elapsed_ms;

		self_test_started_at = HAL_GetTick();
		metadata_status = boot_metadata_self_test_run();
		self_test_elapsed_ms = HAL_GetTick() - self_test_started_at;

		BOOT_LOG("Metadata self-test status=%u, elapsed=%lu ms, HAL=0x%08lX, failed=0x%08lX",
				 (unsigned int)metadata_status,
				 (unsigned long)self_test_elapsed_ms,
				 (unsigned long)boot_metadata_get_last_hal_error(),
				 (unsigned long)boot_metadata_get_last_failure_address());

		if ((metadata_status == BOOT_METADATA_OK) &&
			(boot_metadata_load_latest(&latest_record) == BOOT_METADATA_OK) &&
			(boot_metadata_get_free_record_count(&free_record_count) ==
			 BOOT_METADATA_OK))
		{
			BOOT_LOG("Metadata latest sequence=%lu, state=%u, free=%lu/%lu",
					 (unsigned long)latest_record.sequence_number,
					 (unsigned int)latest_record.state,
					 (unsigned long)free_record_count,
					 (unsigned long)BOOT_METADATA_RECORD_CAPACITY);
		}
	}
#else
	{
		boot_metadata_record_t latest_record;
		boot_metadata_status_t metadata_status;
		uint8_t metadata_allows_boot;

		metadata_status = boot_metadata_load_latest(&latest_record);
		metadata_allows_boot = 0U;

		if (metadata_status == BOOT_METADATA_OK)
		{
			BOOT_LOG("Metadata sequence=%lu, state=%u, received=%lu/%lu",
					 (unsigned long)latest_record.sequence_number,
					 (unsigned int)latest_record.state,
					 (unsigned long)latest_record.received_bytes,
					 (unsigned long)latest_record.image_size);
			metadata_allows_boot = boot_metadata_state_allows_app_boot(
				(boot_state_t)latest_record.state) ? 1U : 0U;
		}
		else if (metadata_status == BOOT_METADATA_EMPTY)
		{
			BOOT_LOG("Metadata is empty");
			metadata_allows_boot = 1U;
		}
		else if (metadata_status == BOOT_METADATA_CORRUPT)
		{
			BOOT_LOG("WARNING: Metadata is corrupt; using APP vector fallback");
			metadata_allows_boot = 1U;
		}
		else
		{
			BOOT_LOG("Metadata scan failed, status=%u",
					 (unsigned int)metadata_status);
		}

		if ((metadata_allows_boot != 0U) && (Boot_IsAppValid() != 0U))
		{
			BOOT_LOG("APP is valid, jumping...");
			Boot_JumpToApp();
		}
		else if (metadata_allows_boot == 0U)
		{
			BOOT_LOG("Metadata requires recovery mode");
		}
		else
		{
			BOOT_LOG("No valid APP");
		}
	}
#endif
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

#if !FLASH_IF_SELF_TEST_ENABLE && !BOOT_METADATA_SELF_TEST_ENABLE
static uint8_t Boot_IsAppValid(void)
{
    uint32_t app_msp;
    uint32_t app_reset;
    uint32_t reset_address;
    uint8_t msp_valid;
    uint8_t reset_valid;

    app_msp = *(volatile uint32_t *)FLASH_LAYOUT_APP_BASE_ADDR;
    app_reset = *(volatile uint32_t *)(FLASH_LAYOUT_APP_BASE_ADDR + 4U);

    /*
     * The initial stack pointer may be at the top of SRAM or CCM RAM.
     * ARM AAPCS requires the stack to be 8-byte aligned at public interfaces.
     */
    msp_valid =
        ((((app_msp >= SRAM_START_ADDR) &&
           (app_msp <= SRAM_END_ADDR)) ||
          ((app_msp >= CCM_START_ADDR) &&
           (app_msp <= CCM_END_ADDR))) &&
         ((app_msp & 0x7UL) == 0UL)) ? 1U : 0U;

    /*
     * Cortex-M reset vectors must have the Thumb bit set. The actual reset
     * handler address must remain inside the application partition.
     */
    reset_address = app_reset & ~1UL;

    reset_valid =
        (((app_reset & 1UL) != 0UL) &&
         (reset_address >= FLASH_LAYOUT_APP_BASE_ADDR) &&
         (reset_address < FLASH_LAYOUT_APP_END_ADDR)) ? 1U : 0U;

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
        *(volatile uint32_t *)FLASH_LAYOUT_APP_BASE_ADDR;

    app_reset =
        *(volatile uint32_t *)(FLASH_LAYOUT_APP_BASE_ADDR + 4U);

    /*
     * Complete any blocking UART log transmission before peripheral reset.
     * Protocol builds keep LOG_ENABLE at zero and do not emit text here.
     */

    /*
     * Stop interrupts before resetting peripherals so no Bootloader ISR can
     * execute while the hardware is being handed over to the application.
     */
    __disable_irq();

    HAL_RCC_DeInit();
    HAL_DeInit();

    /* Stop the Bootloader SysTick source. */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /*
     * STM32F407 exposes up to eight 32-bit NVIC enable/pending banks.
     */
    for (index = 0U; index < 8U; index++)
    {
        NVIC->ICER[index] = 0xFFFFFFFFUL;
        NVIC->ICPR[index] = 0xFFFFFFFFUL;
    }

    /* Clear pending SysTick and PendSV exceptions. */
    SCB->ICSR =
        SCB_ICSR_PENDSTCLR_Msk |
        SCB_ICSR_PENDSVCLR_Msk;

    /* Remove masks inherited from the Bootloader except PRIMASK. */
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);

    /* Relocate the vector table before branching. */
    SCB->VTOR = FLASH_LAYOUT_APP_BASE_ADDR;

    __DSB();
    __ISB();

    /*
     * The assembly helper switches to MSP, enables interrupts only after all
     * sources have been disabled, and branches to the APP Reset_Handler.
     */
    Boot_BranchToApp(app_msp, app_reset);

    /* The application reset handler must never return. */
    while (1)
    {
    }
}

__asm void Boot_BranchToApp(uint32_t app_msp,
                            uint32_t app_reset)
{
    /* Select MSP for Thread mode and privileged execution. */
    MOVS    R2, #0
    MSR     CONTROL, R2
    ISB

    /* Install the application's initial main stack pointer. */
    MSR     MSP, R0

    /* Clear PRIMASK after all Bootloader interrupt sources were disabled. */
    CPSIE   I

    /* Branch to the application's Reset_Handler (Thumb bit is validated). */
    BX      R1
}
#endif

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
