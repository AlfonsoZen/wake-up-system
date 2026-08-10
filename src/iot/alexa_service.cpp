#include "iot/alexa_service.h"

#include <Arduino.h>
#include <WiFi.h>

// Logging interno de fauxmoESP (peticiones SSDP/UPnP y TCP) por Serial.
// Descomentar para diagnosticar el descubrimiento de Alexa. Nota: no activar
// junto con logs verbosos en otras tareas (p.ej. dsp_clap a nivel Debug) -
// ambos nucleos escribiendo al Serial a la vez puede saturarlo y colgar el
// firmware.
// #define DEBUG_FAUXMO Serial
// #define DEBUG_FAUXMO_VERBOSE_TCP true
// #define DEBUG_FAUXMO_VERBOSE_UDP true

#include <fauxmoESP.h>
#include <esp_log.h>

#include "core/config.h"
#include "core/event_types.h"

namespace {

const char *TAG = "alexa_service";
fauxmoESP fauxmo;
QueueHandle_t g_relay_queue = nullptr;

void connect_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    ESP_LOGI(TAG, "Conectando a Wi-Fi: %s", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    // El modo de ahorro de energia del radio Wi-Fi descarta paquetes
    // multicast/broadcast (como el SSDP M-SEARCH que usa Alexa para
    // descubrir el dispositivo), asi que se desactiva.
    WiFi.setSleep(false);

    ESP_LOGI(TAG, "Wi-Fi conectado, IP: %s", WiFi.localIP().toString().c_str());
}

void setup_fauxmo() {
    // fauxmo.handle() se llama desde task_wifi_alexa; el servidor HTTP interno
    // no necesita correr en su propia tarea.
    fauxmo.createServer(true);
    fauxmo.setPort(80);
    fauxmo.addDevice(ALEXA_DEVICE_NAME);
    fauxmo.enable(true);

    fauxmo.onSetState([](unsigned char device_id, const char *device_name, bool state, unsigned char value) {
        (void)device_id;
        (void)value;

        if (!state || g_relay_queue == nullptr) {
            return;
        }

        ESP_LOGI(TAG, "Alexa solicito encender: %s", device_name);
        RelayEvent evt = { TriggerSource::ALEXA, (uint32_t)millis() };
        xQueueSend(g_relay_queue, &evt, 0);
    });
}

} // namespace

void task_wifi_alexa(void *pvParameters) {
    g_relay_queue = (QueueHandle_t)pvParameters;

    connect_wifi();
    setup_fauxmo();

    for (;;) {
        fauxmo.handle();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
