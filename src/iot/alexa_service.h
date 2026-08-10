#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Tarea FreeRTOS (CORE 0): conecta Wi-Fi, expone el dispositivo emulado
// (fauxmoESP, protocolo Wemo) y encola un RelayEvent cuando Alexa solicita
// encender el dispositivo.
// pvParameters debe ser el QueueHandle_t de la cola del relevador, casteado
// a void*.
void task_wifi_alexa(void *pvParameters);
