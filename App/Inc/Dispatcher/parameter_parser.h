/*
 * parameter_parcer.h
 *
 *  Created on: Jan 29, 2026
 *      Author: andrey
 */

#ifndef INC_DISPATCHER_PARAMETER_PARSER_H_
#define INC_DISPATCHER_PARAMETER_PARSER_H_

#include <stdint.h>
#include <stdbool.h>
#include "host_command_model.h"

 /**
  * @brief Разбирает сырые байты параметров команды в структурированные данные.
  * @param cmd[in, out] Указатель на универсальную команду. Функция будет заполнять
  *                     соответствующее поле в cmd->args на основе cmd->command_code.
  * @param raw_params[in] Указатель на сырые байты параметров из пакета.
  * @param params_len[in] Длина сырых байт.
  * @return true, если разбор прошел успешно, false в случае ошибки.
  */
 bool Parameters_Parse(UniversalCommand_t* cmd, const uint8_t* raw_params, uint16_t params_len);

#endif /* INC_DISPATCHER_PARAMETER_PARSER_H_ */
