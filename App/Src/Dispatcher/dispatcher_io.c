/*
 * dispatcher_io.c
 *
 *  Created on: Dec 4, 2025
 *      Author: andrey
 */

#include "Dispatcher/dispatcher_io.h"
#include "shared_resources.h" // Для доступа к usb_tx_queue_handle
#include "app_config.h"       // Для APP_USB_RESP_MAX_LEN
#include <string.h>           // Для strncpy
#include "cmsis_os.h"         // Для pdMS_TO_TICKS

/**
* @brief Отправляет строку-ответ в очередь на передачу по USB.
*
* @param message Указатель на null-terminated строку для отправки.
*/
void Dispatcher_SendUsbResponse(const char* message)
{
// Используем локальный буфер для копирования сообщения.
// Это критически важно для потокобезопасности:
// `message` может указывать на временный буфер другой задачи,
// содержимое которого может измениться, пока FreeRTOS
// помещает его в очередь. Копирование гарантирует, что в очередь
// попадет стабильная, полная строка.
     char response_buffer[APP_USB_RESP_MAX_LEN];
     strncpy(response_buffer, message, APP_USB_RESP_MAX_LEN - 1);
     response_buffer[APP_USB_RESP_MAX_LEN - 1] = '\0'; // Гарантируем null-termination

     // Отправляем скопированное сообщение в очередь на передачу по USB.
     // Если очередь полна, ждем максимум 100 мс. Это предотвращает
     // блокировку всей задачи диспетчера, если USB-передача застряла.
     xQueueSend(usb_tx_queue_handle, (void *)response_buffer, pdMS_TO_TICKS(100));
}
