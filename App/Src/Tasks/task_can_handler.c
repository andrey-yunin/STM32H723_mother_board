/*
 * task_can_handler.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "Tasks/task_can_handler.h"
#include "cmsis_os.h"

#include"task_can_handler.h"

void app_start_task_can_handler(void *argument)
{
  for(;;)
  {
	  osDelay(1000);
  }

}
