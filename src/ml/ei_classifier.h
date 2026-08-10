#pragma once

#include <cstddef>
#include <cstdint>

// Inicializa el clasificador (no-op mientras el modelo se ejecute sin estado
// entre ventanas; se deja el hook por si el modelo exportado requiere setup).
void ei_classifier_init();

// Ejecuta la inferencia de Edge Impulse sobre una ventana de sample_count
// muestras PCM de 16 bits. Devuelve true si la etiqueta objetivo
// (EI_CLASSIFIER_TARGET_LABEL) supera el umbral de confianza configurado.
// Si EI_CLASSIFIER_ENABLED esta en 0 (modelo aun no integrado), siempre
// devuelve false.
bool ei_classifier_run(const int16_t *samples, size_t sample_count, float *out_confidence = nullptr);
