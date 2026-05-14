/*
 * task_jobs_monitor.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 */

#include "task_jobs_monitor.h"
#include "cmsis_os.h"
#include "Dispatcher/job_manager.h"
#include "task_watchdog.h"
#include "Dispatcher/can_response_router.h"
#include "Dispatcher/service_manager.h"



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
	  Watchdog_Kick(TASK_ID_JOBS_MONITOR);

	  /*
	   * Router первым забирает raw CAN RX и раскладывает ответы по владельцам.
	   * JobManager после этого видит только Host-operation ответы.
	   */
	  CanResponseRouter_Run();

	  /*
	   * ServiceManager обрабатывает discovery/recovery отдельно от Host jobs.
	   */
	  ServiceManager_Run();


	  JobManager_Run();
	  // "Засыпаем" на заданный период
	  osDelay(JOBS_MONITOR_PERIOD_MS);
  }

}

