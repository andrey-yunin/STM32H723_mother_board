/*
 * task_dispatcher.h
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#ifndef TASK_DISPATCHER_H_
#define TASK_DISPATCHER_H_

void app_start_task_dispatcher(void *argument);

typedef enum {
	SYS_STATE_POWER_ON,       // Начальное состояние после включения
    SYS_STATE_INITIALIZING,   // Идет процесс инициализации (homing)
    SYS_STATE_READY,          // Система готова к приему команд
    SYS_STATE_BUSY,           // Система выполняет комплексную команду (определяется JobManager'ом)
    SYS_STATE_ERROR           // Произошла ошибка, требуется вмешательство
 } SystemState_t;

 SystemState_t GetSystemState(void);

#endif /* TASK_DISPATCHER_H_ */

