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
	  if (xQueueReceive(usb_tx_queue_handle, (void *)tx_buffer, portMAX_DELAY) == pdPASS)
         {
             if (strlen(tx_buffer) + 2 < APP_USB_RESP_MAX_LEN) {
                  strcat(tx_buffer, "\r\n");
             }

             while (CDC_Transmit_HS((uint8_t *)tx_buffer, strlen(tx_buffer)) == USBD_BUSY)
             {
                 osDelay(1);
             }
         }


  }
}


