#include "iot/alexa_service.h"

#include <Arduino.h>
#include <WiFi.h>

#include <SinricPro.h>
#include <SinricProSwitch.h>

#include <esp_log.h>

#include "core/config.h"
#include "core/event_types.h"

namespace {

const char *TAG = "alexa_service";
QueueHandle_t g_relay_queue = nullptr;

void connect_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    ESP_LOGI(TAG, "Conectando a Wi-Fi: %s", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    // El modo de ahorro de energia del radio Wi-Fi puede cortar la conexion
    // persistente (WebSocket) que SinricPro mantiene abierta, asi que se
    // desactiva.
    WiFi.setSleep(false);

    ESP_LOGI(TAG, "Wi-Fi conectado, IP: %s", WiFi.localIP().toString().c_str());
}

// Se registra igual para todos los "Switch" de la cuenta: no importa cual
// alias uso Alexa, todos disparan el mismo encendido del relevador.
bool on_power_state(const String &device_id, bool &state) {
    if (state && g_relay_queue != nullptr) {
        ESP_LOGI(TAG, "Alexa solicito encender (device_id=%s)", device_id.c_str());
        RelayEvent evt = { TriggerSource::ALEXA, (uint32_t)millis() };
        xQueueSend(g_relay_queue, &evt, 0);
    }
    return true; // confirma a SinricPro/Alexa que el comando se proceso
}

void setup_sinricpro() {
    static const char *const kDeviceIds[] = SINRICPRO_DEVICE_IDS;
    for (const char *id : kDeviceIds) {
        SinricProSwitch &sw = SinricPro[id];
        sw.onPowerState(on_power_state);
    }

    SinricPro.onConnected([]() { ESP_LOGI(TAG, "Conectado a SinricPro"); });
    SinricPro.onDisconnected([]() { ESP_LOGW(TAG, "Desconectado de SinricPro"); });

    SinricPro.begin(SINRICPRO_APP_KEY, SINRICPRO_APP_SECRET);
}

} // namespace

void task_wifi_alexa(void *pvParameters) {
    g_relay_queue = (QueueHandle_t)pvParameters;

    connect_wifi();
    setup_sinricpro();

    for (;;) {
        SinricPro.handle();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
