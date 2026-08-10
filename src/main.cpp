#include <Arduino.h>
#include <cstring>
#include <esp_log.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#include "audio/dsp_clap.h"
#include "audio/i2s_sampler.h"
#include "core/config.h"
#include "core/event_types.h"
#include "hardware/relay_ctrl.h"
#include "iot/alexa_service.h"
#include "ml/ei_classifier.h"

namespace {

QueueHandle_t g_relay_queue = nullptr;

void send_relay_event(TriggerSource source) {
    RelayEvent evt = { source, (uint32_t)millis() };
    xQueueSend(g_relay_queue, &evt, 0);
}

// CORE 1: consume el RingBuffer de audio. Evalua energia RMS buscando el
// patron de doble aplauso; si no lo encuentra, acumula ventanas de 1s (con
// solapamiento de 300ms) y las pasa al modelo de Edge Impulse.
void task_audio_process(void *pvParameters) {
    (void)pvParameters;

    static int16_t ml_window[EI_CLASSIFIER_RAW_SAMPLE_COUNT];
    size_t ml_window_filled = 0;

    uint32_t last_heartbeat_ms = 0;
    uint32_t chunks_since_heartbeat = 0;

    ESP_LOGI("audio_process", "task_audio_process arrancada");

    for (;;) {
        size_t item_size = 0;
        uint8_t *chunk = (uint8_t *)xRingbufferReceive(g_audio_ring_buf, &item_size, pdMS_TO_TICKS(100));

        uint32_t hb_now = millis();
        if (hb_now - last_heartbeat_ms >= 1000) {
            last_heartbeat_ms = hb_now;
            // Heartbeat de diagnostico desactivado (mucho ruido en el log).
            // Descomentar si hace falta volver a verificar que esta tarea
            // este consumiendo el ring buffer.
            // ESP_LOGI("audio_process", "heartbeat: %u chunks/seg recibidos del ring buffer", (unsigned)chunks_since_heartbeat);
            chunks_since_heartbeat = 0;
        }

        if (chunk == nullptr) {
            continue;
        }
        chunks_since_heartbeat++;

        const int16_t *pcm = (const int16_t *)chunk;
        size_t sample_count = item_size / sizeof(int16_t);
        uint32_t now_ms = millis();

        if (dsp_clap_process(pcm, sample_count, now_ms)) {
            send_relay_event(TriggerSource::DOUBLE_CLAP);
            ml_window_filled = 0; // Descarta la ventana ML en curso tras un aplauso valido.
            vRingbufferReturnItem(g_audio_ring_buf, chunk);
            continue;
        }

        for (size_t i = 0; i < sample_count; i++) {
            if (ml_window_filled < EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
                ml_window[ml_window_filled++] = pcm[i];
            }

            if (ml_window_filled == EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
                float confidence = 0.0f;
                if (ei_classifier_run(ml_window, EI_CLASSIFIER_RAW_SAMPLE_COUNT, &confidence)) {
                    send_relay_event(TriggerSource::ML_KEYWORD);
                }

                // Conserva los ultimos 300ms como solapamiento para la siguiente ventana.
                memmove(ml_window,
                        ml_window + (EI_CLASSIFIER_RAW_SAMPLE_COUNT - ML_WINDOW_OVERLAP_SAMPLES),
                        ML_WINDOW_OVERLAP_SAMPLES * sizeof(int16_t));
                ml_window_filled = ML_WINDOW_OVERLAP_SAMPLES;
            }
        }

        vRingbufferReturnItem(g_audio_ring_buf, chunk);
    }
}

} // namespace

void setup() {
    Serial.begin(115200);

    g_relay_queue = xQueueCreate(RELAY_QUEUE_LENGTH, sizeof(RelayEvent));

    relay_ctrl_init();
    i2s_sampler_init();
    dsp_clap_reset();
    ei_classifier_init();

    // CORE Indistinto: control de hardware del relevador.
    xTaskCreatePinnedToCore(task_relay_control, "relay_ctrl", 4096, (void *)g_relay_queue, 2, nullptr, tskNO_AFFINITY);

    // CORE 0: IoT y red (Wi-Fi + emulacion Alexa).
    xTaskCreatePinnedToCore(task_wifi_alexa, "wifi_alexa", 8192, (void *)g_relay_queue, 1, nullptr, 0);

    // CORE 1: procesamiento acustico y ML.
    xTaskCreatePinnedToCore(task_audio_i2s, "audio_i2s", 4096, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(task_audio_process, "audio_process", 8192, nullptr, 2, nullptr, 1);
}

void loop() {
    // Toda la logica corre en las tareas de FreeRTOS creadas en setup().
    vTaskDelay(pdMS_TO_TICKS(1000));
}
