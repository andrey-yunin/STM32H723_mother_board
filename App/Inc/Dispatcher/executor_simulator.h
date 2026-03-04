/*
 * executor_simulator.h
 *
 *  Created on: Mar 3, 2026
 *      Author: andrey
 * Имитатор исполнителей (Executor Simulator).
 * Перехватывает исходящие CAN-команды и генерирует ответы DONE,
 * помещая их в can_rx_queue. Используется для тестирования
 * без физической CAN-шины.
 *
 * Для отключения: закомментировать вызов ExecutorSim_ProcessTxMessage()
 * в task_can_handler.c
 *
 */

#ifndef INC_DISPATCHER_EXECUTOR_SIMULATOR_H_
#define INC_DISPATCHER_EXECUTOR_SIMULATOR_H_

#include "Dispatcher/can_packer.h"

/**
 * @brief Обрабатывает исходящую CAN-команду и генерирует имитированный DONE-ответ.
 *        Помещает ответный CAN-кадр в can_rx_queue_handle.
 *
 * @param cmd_msg Указатель на отправленную CAN-команду.
 */
void ExecutorSim_ProcessTxMessage(const CAN_Message_t* cmd_msg);


#endif /* INC_DISPATCHER_EXECUTOR_SIMULATOR_H_ */
