/*
 * task_jobs_monitor.c
 *
 *  Created on: Nov 26, 2025
 *      Author: andrey
 *
 * Runtime polling order owner:
 * 1. CanResponseRouter_Run() раскладывает raw CAN RX по owner queues.
 * 2. SafetyOperation_Run() имеет приоритет над service/recovery/jobs.
 * 3. HostDirectOperation_Run() обрабатывает executor-backed Host direct path.
 * 4. ServiceManager_Run() обслуживает discovery/recovery/diagnostics.
 * 5. JobManager_Run() продвигает recipe runtime.
 *
 * Здесь не размещается доменная policy; задача только задает порядок владельцев.
 */

#include "task_jobs_monitor.h"
#include "cmsis_os.h"
#include "Dispatcher/job_manager.h"
#include "task_watchdog.h"
#include "Dispatcher/can_response_router.h"
#include "Dispatcher/host_direct_operation.h"
#include "Dispatcher/service_manager.h"
#include "Dispatcher/safety_operation.h"



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
	   * Safety operation has priority over service/recovery and Host jobs.
	   */
	  SafetyOperation_Run();

	  /*
	   * Executor-backed Host direct operations have their own RX route/queue.
	   */
	  HostDirectOperation_Run();

	  /*
	   * ServiceManager обрабатывает discovery/recovery отдельно от Host jobs.
	   */
	  ServiceManager_Run();


	  JobManager_Run();
	  // "Засыпаем" на заданный период
	  osDelay(JOBS_MONITOR_PERIOD_MS);
  }

}
