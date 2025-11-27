/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
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
#include "cmsis_os.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "task_dispatcher.h"
#include "task_can_handler.h"
#include "task_usb_handler.h"
#include "task_watchdog.h"
#include "task_jobs_monitor.h"
#include "task_logger.h"
#include "shared_resources.h"
#include "app_config.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

FDCAN_HandleTypeDef hfdcan1;

/* Definitions for task_can_handle */
osThreadId_t task_can_handleHandle;
const osThreadAttr_t task_can_handle_attributes = {
  .name = "task_can_handle",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh1,
};
/* Definitions for task_usb_handle */
osThreadId_t task_usb_handleHandle;
const osThreadAttr_t task_usb_handle_attributes = {
  .name = "task_usb_handle",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh2,
};
/* Definitions for task_dispatcher */
osThreadId_t task_dispatcherHandle;
const osThreadAttr_t task_dispatcher_attributes = {
  .name = "task_dispatcher",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for task_watchdog */
osThreadId_t task_watchdogHandle;
const osThreadAttr_t task_watchdog_attributes = {
  .name = "task_watchdog",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for task_jobs_monit */
osThreadId_t task_jobs_monitHandle;
const osThreadAttr_t task_jobs_monit_attributes = {
  .name = "task_jobs_monit",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for task_logger */
osThreadId_t task_loggerHandle;
const osThreadAttr_t task_logger_attributes = {
  .name = "task_logger",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow1,
};
/* USER CODE BEGIN PV */

QueueHandle_t usb_rx_queue_handle;
QueueHandle_t usb_tx_queue_handle;
QueueHandle_t can_rx_queue_handle;
QueueHandle_t can_tx_queue_handle;
QueueHandle_t log_queue_handle;



/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
void start_task_can_handler(void *argument);
void start_task_usb_handler(void *argument);
void start_task_dispatcher(void *argument);
void start_task_watchdog(void *argument);
void start_task_jobs_monitor(void *argument);
void start_task_logger(void *argument);

/* USER CODE BEGIN PFP */

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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_FDCAN1_Init();
  /* USER CODE BEGIN 2 */
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  usb_rx_queue_handle = xQueueCreate(APP_USB_RX_QUEUE_LENGTH, APP_USB_CMD_MAX_LEN); // 10 команд, каждая до 256 байт
  usb_tx_queue_handle = xQueueCreate(APP_USB_TX_QUEUE_LENGTH, APP_USB_RESP_MAX_LEN); // 10 ответов, каждая до 256 байт (пока)

// Для CAN передаются структурированные сообщения.
// Пока что представим, что это будет 8-байтный массив (payload CAN-сообщения).
// Позже мы определим структуру CanMessage_t.
  can_rx_queue_handle = xQueueCreate(APP_CAN_RX_QUEUE_LENGTH, APP_CAN_MESSAGE_MAX_LEN); // 20 CAN-сообщений
  can_tx_queue_handle = xQueueCreate(APP_CAN_TX_QUEUE_LENGTH, APP_CAN_MESSAGE_MAX_LEN); // 20 CAN-сообщений

  log_queue_handle = xQueueCreate(APP_LOG_QUEUE_LENGTH , APP_LOG_MESSAGE_MAX_LEN); // 30 сообщений для лога, каждое до 128 байт

// Важно: всегда проверяйте, что очереди успешно создались!
// Если какая-либо очередь не создалась (handle == NULL),
// это указывает на нехватку памяти FreeRTOS (heap).
// Пока можно игнорировать, но в продакшн-коде нужно обрабатывать!

  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of task_can_handle */
  task_can_handleHandle = osThreadNew(start_task_can_handler, NULL, &task_can_handle_attributes);

  /* creation of task_usb_handle */
  task_usb_handleHandle = osThreadNew(start_task_usb_handler, NULL, &task_usb_handle_attributes);

  /* creation of task_dispatcher */
  task_dispatcherHandle = osThreadNew(start_task_dispatcher, NULL, &task_dispatcher_attributes);

  /* creation of task_watchdog */
  task_watchdogHandle = osThreadNew(start_task_watchdog, NULL, &task_watchdog_attributes);

  /* creation of task_jobs_monit */
  task_jobs_monitHandle = osThreadNew(start_task_jobs_monitor, NULL, &task_jobs_monit_attributes);

  /* creation of task_logger */
  task_loggerHandle = osThreadNew(start_task_logger, NULL, &task_logger_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

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

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 12;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief FDCAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_FDCAN1_Init(void)
{

  /* USER CODE BEGIN FDCAN1_Init 0 */

  /* USER CODE END FDCAN1_Init 0 */

  /* USER CODE BEGIN FDCAN1_Init 1 */

  /* USER CODE END FDCAN1_Init 1 */
  hfdcan1.Instance = FDCAN1;
  hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan1.Init.AutoRetransmission = DISABLE;
  hfdcan1.Init.TransmitPause = DISABLE;
  hfdcan1.Init.ProtocolException = DISABLE;
  hfdcan1.Init.NominalPrescaler = 16;
  hfdcan1.Init.NominalSyncJumpWidth = 1;
  hfdcan1.Init.NominalTimeSeg1 = 1;
  hfdcan1.Init.NominalTimeSeg2 = 1;
  hfdcan1.Init.DataPrescaler = 1;
  hfdcan1.Init.DataSyncJumpWidth = 1;
  hfdcan1.Init.DataTimeSeg1 = 1;
  hfdcan1.Init.DataTimeSeg2 = 1;
  hfdcan1.Init.MessageRAMOffset = 0;
  hfdcan1.Init.StdFiltersNbr = 0;
  hfdcan1.Init.ExtFiltersNbr = 0;
  hfdcan1.Init.RxFifo0ElmtsNbr = 0;
  hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
  hfdcan1.Init.RxFifo1ElmtsNbr = 0;
  hfdcan1.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
  hfdcan1.Init.RxBuffersNbr = 0;
  hfdcan1.Init.RxBufferSize = FDCAN_DATA_BYTES_8;
  hfdcan1.Init.TxEventsNbr = 0;
  hfdcan1.Init.TxBuffersNbr = 0;
  hfdcan1.Init.TxFifoQueueElmtsNbr = 0;
  hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
  if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN1_Init 2 */

  /* USER CODE END FDCAN1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_start_task_can_handler */
/**
  * @brief  Function implementing the task_can_handle thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_start_task_can_handler */
void start_task_can_handler(void *argument)
{
  /* USER CODE BEGIN 5 */
  app_start_task_can_handler(argument);
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_start_task_usb_handler */
/**
* @brief Function implementing the task_usb_handle thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_start_task_usb_handler */
void start_task_usb_handler(void *argument)
{
  /* USER CODE BEGIN start_task_usb_handler */
  app_start_task_usb_handler(argument);

  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END start_task_usb_handler */
}

/* USER CODE BEGIN Header_start_task_dispatcher */
/**
* @brief Function implementing the task_dispatcher thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_start_task_dispatcher */
void start_task_dispatcher(void *argument)
{
  /* USER CODE BEGIN start_task_dispatcher */

 app_start_task_dispatcher(argument);

 /* Infinite loop */
 // этот код никогда не выполнится, но мы его не трогаем

  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END start_task_dispatcher */
}

/* USER CODE BEGIN Header_start_task_watchdog */
/**
* @brief Function implementing the task_watchdog thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_start_task_watchdog */
void start_task_watchdog(void *argument)
{
  /* USER CODE BEGIN start_task_watchdog */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END start_task_watchdog */
}

/* USER CODE BEGIN Header_start_task_jobs_monitor */
/**
* @brief Function implementing the task_jobs_monit thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_start_task_jobs_monitor */
void start_task_jobs_monitor(void *argument)
{
  /* USER CODE BEGIN start_task_jobs_monitor */

	app_start_task_jobs_monitor(argument);
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END start_task_jobs_monitor */
}

/* USER CODE BEGIN Header_start_task_logger */
/**
* @brief Function implementing the task_logger thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_start_task_logger */
void start_task_logger(void *argument)
{
  /* USER CODE BEGIN start_task_logger */
  app_start_task_logger(argument);
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END start_task_logger */
}

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
