/*
 * app_init_checker.h
 *
 *  Created on: Nov 27, 2025
 *      Author: andrey
 */

#ifndef INC_APP_INIT_CHECKER_H_
#define INC_APP_INIT_CHECKER_H_

#include "main.h" // Для Error_Handler
#include "shared_resources.h" // Для ручек очередей

void app_init_checker_verifyqueues(void);
void app_init_checker_verifytasks(void);



#endif /* INC_APP_INIT_CHECKER_H_ */
