#include "hardware/relay_ctrl.h"

#include <Arduino.h>
#include <esp_log.h>

#include "core/config.h"
#include "core/event_types.h"

namespace {
const char *TAG = "relay_ctrl";
}

void relay_ctrl_init() {
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
}

void task_relay_control(void *pvParameters) {
    QueueHandle_t relay_queue = (QueueHandle_t)pvParameters;
    RelayEvent evt;
    uint32_t last_activation_ms = 0;

    for (;;) {
        if (xQueueReceive(relay_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        uint32_t now_ms = millis();
        // Cooldown: ignora disparos que lleguen muy seguido del anterior,
        // para que una racha de falsos positivos no dispare el relevador
        // varias veces seguidas.
        if (last_activation_ms != 0 && (now_ms - last_activation_ms) < RELAY_COOLDOWN_MS) {
            ESP_LOGW(TAG, "Disparo ignorado por cooldown (fuente=%d, faltan %ums)",
                     (int)evt.source, RELAY_COOLDOWN_MS - (now_ms - last_activation_ms));
            continue;
        }
        last_activation_ms = now_ms;

        ESP_LOGI(TAG, "Encendido de PC solicitado (fuente=%d)", (int)evt.source);
        digitalWrite(RELAY_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(RELAY_PULSE_MS));
        digitalWrite(RELAY_PIN, LOW);
    }
}
