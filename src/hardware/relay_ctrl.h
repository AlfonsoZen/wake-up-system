#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Configura el GPIO del relevador como salida y lo deja en LOW.
// Debe llamarse una vez desde setup(), antes de crear task_relay_control.
void relay_ctrl_init();

// Tarea FreeRTOS (nucleo indistinto): bloqueada esperando mensajes en la
// Queue del relevador. Al recibir un evento, activa el GPIO del relevador
// (HIGH) durante RELAY_PULSE_MS y luego lo vuelve a poner en LOW.
// pvParameters debe ser el QueueHandle_t de la cola del relevador, casteado
// a void*.
void task_relay_control(void *pvParameters);
