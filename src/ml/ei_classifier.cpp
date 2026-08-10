#include "ml/ei_classifier.h"

#include <cstring>

#include "core/config.h"

#if EI_CLASSIFIER_ENABLED
// Reemplazar por el header generado al exportar el modelo desde Edge Impulse
// Studio como "Arduino library" (queda en lib/<nombre_proyecto>_inferencing/).
#include <edge-impulse-sdk_inferencing.h>
#endif

namespace {

#if EI_CLASSIFIER_ENABLED

float g_signal_buffer[EI_CLASSIFIER_RAW_SAMPLE_COUNT];

int get_signal_data(size_t offset, size_t length, float *out_ptr) {
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = g_signal_buffer[offset + i];
    }
    return 0;
}

#endif // EI_CLASSIFIER_ENABLED

} // namespace

void ei_classifier_init() {
    // El modelo de Edge Impulse se ejecuta de forma stateless por ventana;
    // no requiere inicializacion adicional.
}

bool ei_classifier_run(const int16_t *samples, size_t sample_count, float *out_confidence) {
#if EI_CLASSIFIER_ENABLED
    if (sample_count < EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
        return false;
    }

    for (size_t i = 0; i < EI_CLASSIFIER_RAW_SAMPLE_COUNT; i++) {
        g_signal_buffer[i] = (float)samples[i];
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &get_signal_data;

    ei_impulse_result_t result = {};
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    if (err != EI_IMPULSE_OK) {
        return false;
    }

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (strcmp(result.classification[i].label, EI_CLASSIFIER_TARGET_LABEL) == 0) {
            float confidence = result.classification[i].value;
            if (out_confidence != nullptr) {
                *out_confidence = confidence;
            }
            return confidence >= EI_CLASSIFIER_CONFIDENCE_THRESHOLD;
        }
    }
    return false;
#else
    (void)samples;
    (void)sample_count;
    if (out_confidence != nullptr) {
        *out_confidence = 0.0f;
    }
    return false;
#endif
}
