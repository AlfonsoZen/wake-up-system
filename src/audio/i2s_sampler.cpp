#include "audio/i2s_sampler.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_log.h>

#include "core/config.h"

namespace {
const char *TAG = "i2s_sampler";
constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
} // namespace

RingbufHandle_t g_audio_ring_buf = nullptr;

void i2s_sampler_init() {
    g_audio_ring_buf = xRingbufferCreate(AUDIO_RING_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (g_audio_ring_buf == nullptr) {
        ESP_LOGE(TAG, "No se pudo crear el ring buffer de audio");
    }

    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = AUDIO_SAMPLE_RATE_HZ,
        // El INMP441 entrega 24 bits de audio justificados a la izquierda dentro
        // de una palabra de 32 bits.
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        // El pin L/R de este modulo INMP441 selecciona el canal derecho en la
        // practica (verificado con hardware real); si se recablea L/R a GND
        // firme, cambiar a I2S_CHANNEL_FMT_ONLY_LEFT.
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    const i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,
        .ws_io_num = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_PIN
    };

    esp_err_t install_err = i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
    if (install_err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_driver_install fallo: %s", esp_err_to_name(install_err));
    }

    esp_err_t pin_err = i2s_set_pin(I2S_PORT, &pin_config);
    if (pin_err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_set_pin fallo: %s", esp_err_to_name(pin_err));
    }

    i2s_zero_dma_buffer(I2S_PORT);
}

void task_audio_i2s(void *pvParameters) {
    (void)pvParameters;

    static int32_t raw_samples[I2S_READ_CHUNK_SAMPLES];
    static int16_t pcm16[I2S_READ_CHUNK_SAMPLES];

    uint32_t last_heartbeat_ms = 0;
    uint32_t bytes_since_heartbeat = 0;

    for (;;) {
        size_t bytes_read = 0;
        // Timeout acotado (en vez de portMAX_DELAY) para poder detectar y
        // reportar si el DMA no esta entregando datos (mic mal cableado,
        // sin alimentacion, o driver mal configurado).
        esp_err_t result = i2s_read(I2S_PORT, raw_samples, sizeof(raw_samples), &bytes_read, pdMS_TO_TICKS(1000));

        uint32_t now_ms = millis();
        if (result != ESP_OK) {
            if (now_ms - last_heartbeat_ms >= 1000) {
                last_heartbeat_ms = now_ms;
                ESP_LOGW(TAG, "i2s_read sin datos (result=%s)", esp_err_to_name(result));
            }
            continue;
        }
        if (bytes_read == 0) {
            continue;
        }

        bytes_since_heartbeat += bytes_read;
        if (now_ms - last_heartbeat_ms >= 1000) {
            last_heartbeat_ms = now_ms;
            ESP_LOGI(TAG, "I2S OK: %u bytes/seg leidos del DMA", (unsigned)bytes_since_heartbeat);
            bytes_since_heartbeat = 0;
        }

        size_t samples_read = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < samples_read; i++) {
            // Descarta los bits bajos sin datos y reduce a PCM de 16 bits.
            // El shift es ajustable (config.h no lo expone porque depende
            // del modulo INMP441 concreto): 14 funciona bien en la mayoria.
            pcm16[i] = (int16_t)(raw_samples[i] >> 14);
        }

        if (xRingbufferSend(g_audio_ring_buf, pcm16, samples_read * sizeof(int16_t), pdMS_TO_TICKS(50)) != pdTRUE) {
            ESP_LOGW(TAG, "Ring buffer lleno: se descarta un chunk de audio");
        }
    }
}
