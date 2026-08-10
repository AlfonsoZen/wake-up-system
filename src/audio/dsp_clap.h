#pragma once

#include <cstddef>
#include <cstdint>

// Reinicia la maquina de estados de deteccion de doble aplauso.
void dsp_clap_reset();

// Analiza un chunk de muestras PCM (16 bits) y actualiza la maquina de
// estados de deteccion de doble aplauso.
// now_ms: marca de tiempo (millis()) correspondiente al chunk recibido.
// Devuelve true unicamente cuando se completa un patron de doble aplauso
// valido (dos picos de energia separados entre CLAP_MIN_GAP_MS y
// CLAP_WINDOW_MS).
bool dsp_clap_process(const int16_t *samples, size_t sample_count, uint32_t now_ms);
