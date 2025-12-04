/*
 * task_jobs_monitor.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "task_jobs_monitor.h"
#include "cmsis_os.h"
#include "Dispatcher/job_manager.h"

#define JOBS_MONITOR_PERIOD_MS 100


/**
* @brief Основная логика задачи монитора заданий.
*        Эта задача периодически просыпается и вызывает JobManager_Run()
*        для проверки таймаутов и обработки внутренних шагов рецептов.
*/

void app_start_task_jobs_monitor(void *argument)
{

  for(;;)
  {
	  JobManager_Run();
	  // "Засыпаем" на заданный период
	  osDelay(JOBS_MONITOR_PERIOD_MS);
  }

}

