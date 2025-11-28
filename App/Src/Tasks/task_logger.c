/*
 * task_logger.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "task_logger.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "string.h" // Для snprintf
#include "stdio.h"  // Для snprintf

 // Вспомогательная функция для отправки отчета
 static void SendReport(const char* message)
 {
     // Отправляем сообщение в очередь на передачу по USB, ждем до 100 мс
     xQueueSend(usb_tx_queue_handle, (void *)message, pdMS_TO_TICKS(100));
  }

void app_start_task_logger(void *argument)
{
	char report_message[APP_LOG_MESSAGE_MAX_LEN];

     // --- ЕДИНОРАЗОВЫЙ ОТЧЕТ ПРИ СТАРТЕ ---
     // Небольшая задержка, чтобы дать другим задачам, особенно USB, инициализироваться
     vTaskDelay(pdMS_TO_TICKS(500)); // Задержка 500 мс

     SendReport("--- System Initialization Report ---");

     // Проверяем каждую ручку очереди (они не должны быть NULL, если мы дошли досюда благодаря APP_InitChecker_VerifyQueues)
     // и отправляем отчет.
     snprintf(report_message, APP_LOG_MESSAGE_MAX_LEN, "USB RX Queue: %s", (usb_rx_queue_handle != NULL) ? "OK" : "FAIL");
     SendReport(report_message);

     snprintf(report_message, APP_LOG_MESSAGE_MAX_LEN, "USB TX Queue: %s", (usb_tx_queue_handle != NULL) ? "OK" : "FAIL");
     SendReport(report_message);

     snprintf(report_message, APP_LOG_MESSAGE_MAX_LEN, "CAN RX Queue: %s", (can_rx_queue_handle != NULL) ? "OK" : "FAIL");
     SendReport(report_message);

     snprintf(report_message, APP_LOG_MESSAGE_MAX_LEN, "CAN TX Queue: %s", (can_tx_queue_handle != NULL) ? "OK" : "FAIL");
     SendReport(report_message);

     snprintf(report_message, APP_LOG_MESSAGE_MAX_LEN, "Log Queue: %s", (log_queue_handle != NULL) ? "OK" : "FAIL");
     SendReport(report_message);

     SendReport("--- Report End. Logger is running. ---");


     // --- ОСНОВНОЙ ЦИКЛ ЗАДАЧИ ---
     // После отправки отчета, задача переходит в свой обычный режим работы.
     // На данный момент она просто ждет сообщений, но ничего с ними не делает.
     char log_buffer[APP_LOG_MESSAGE_MAX_LEN];
  /* Infinite loop */
  for(;;)
  {
	  // Ждем сообщение из очереди log_queue
      // Если сообщения нет, задача будет заблокирована и не будет потреблять CPU.
         xQueueReceive(log_queue_handle, (void *)log_buffer, portMAX_DELAY);
      // TODO: В будущем здесь будет код для обработки и вывода логов.
  }

}


