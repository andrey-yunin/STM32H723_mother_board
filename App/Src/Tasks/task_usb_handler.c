/*
 * task_usb_handler.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include"task_usb_handler.h"
#include "cmsis_os.h"
#include "shared_resources.h"
#include "app_config.h"
#include "usbd_cdc_if.h" // Для функции CDC_Transmit_HS
#include "string.h"      // Для strlen

void app_start_task_usb_handler(void *argument)
{
  char tx_buffer[APP_USB_RESP_MAX_LEN];
  /* Infinite loop */
  for(;;)
  {
	// Ждем сообщение из очереди usb_tx_queue_handle
	// Задача будет заблокирована, пока в очереди не появится элемент.
         if (xQueueReceive(usb_tx_queue_handle, (void *)tx_buffer, portMAX_DELAY) == pdPASS)
         {
             // Сообщение получено. Отправляем его на ПК по USB.
             // Добавляем символ новой строки для удобства чтения в терминале.
             // Убеждаемся, что есть место для \r\n
             if (strlen(tx_buffer) + 2 < APP_USB_RESP_MAX_LEN) {
                  strcat(tx_buffer, "\r\n");
             } else {
                  // Если буфер почти полон, просто обрезаем и добавляем \0.
                  tx_buffer[APP_USB_RESP_MAX_LEN - 1] = '\0';
                  tx_buffer[APP_USB_RESP_MAX_LEN - 2] = '\n'; // Пытаемся добавить \n
                  tx_buffer[APP_USB_RESP_MAX_LEN - 3] = '\r'; // Пытаемся добавить \r
             }

             // Ждем, пока USB-передатчик не освободится, чтобы отправить наше сообщение.
             // Это предотвращает потерю данных при быстрой передаче.
             while (CDC_Transmit_HS((uint8_t *)tx_buffer, strlen(tx_buffer)) == USBD_BUSY)
             {
                 // Небольшая задержка, чтобы не "забивать" CPU, пока ждем.
                 vTaskDelay(pdMS_TO_TICKS(1));
             }
         }
  }
}


