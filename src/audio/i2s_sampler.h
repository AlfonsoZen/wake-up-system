#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

// Ring buffer circular con las muestras PCM de 16 bits producidas por
// task_audio_i2s y consumidas por task_audio_process.
extern RingbufHandle_t g_audio_ring_buf;

// Configura el periferico I2S (DMA) para el INMP441 y crea el ring buffer.
// Debe llamarse una vez desde setup(), antes de crear las tareas de audio.
void i2s_sampler_init();

// Tarea FreeRTOS (CORE 1): lee continuamente el DMA I2S y publica los
// chunks de PCM 16-bit en g_audio_ring_buf.
void task_audio_i2s(void *pvParameters);
