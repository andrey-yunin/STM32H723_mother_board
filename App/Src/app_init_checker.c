/*
 * app_init_checker.c
 *
 *  Created on: Nov 27, 2025
 *      Author: andrey
 */

#include "app_init_checker.h"
#include "shared_resources.h" // Для доступа к глобальным ручкам очередей
#include "stdio.h"            // Для sprintf (если нужно)
#include "string.h"           // Для strlen (если нужно)

/**
   * @brief  Проверяет, что все необходимые очереди FreeRTOS были успешно созданы.
   *         В случае сбоя вызывает Error_Handler.
   * @param  None
   * @retval None
   */
 void app_init_checker_verifyqueues(void)
 {
     if (usb_rx_queue_handle == NULL || // Можно было бы отправить в логгер сообщение об ошибке, но так как логгер еще не гарантированно работает, сразу вызываем Error_Handler
         usb_tx_queue_handle == NULL ||
         log_queue_handle == NULL)
     {
    	 Error_Handler();
     }


     //can_rx_queue_handle == NULL ||
     //can_tx_queue_handle == NULL ||

     // Если все проверки пройдены, значит, все очереди успешно созданы.
 }

 // TODO: Здесь можно добавить другие функции проверки инициализации
 //       Например, проверку других FreeRTOS-ресурсов, аппаратных компонентов и т.д.



