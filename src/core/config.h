#pragma once

// =====================================================================
// Pinout
// =====================================================================

// Modulo relevador (optoacoplado, activo en ALTO)
#define RELAY_PIN 33
#define RELAY_PULSE_MS 500

// Tiempo minimo entre activaciones del relevador. Evita que una racha de
// falsos positivos (varios disparos seguidos) golpee el boton de encendido
// repetidamente.
#define RELAY_COOLDOWN_MS 8000

// Microfono digital INMP441 (I2S) - L/R a GND => canal izquierdo
#define I2S_WS_PIN 15   // Word Select
#define I2S_SCK_PIN 14  // Serial Clock
#define I2S_SD_PIN 32   // Serial Data

// =====================================================================
// Wi-Fi / Alexa (SinricPro)
// =====================================================================

#define WIFI_SSID "IZZI-ED34"
#define WIFI_PASSWORD "NzMEhxgeZttHc3XzYT"

// Credenciales de la cuenta SinricPro (portal.sinric.pro -> Credentials).
// Son las mismas para todos los dispositivos de la cuenta.
#define SINRICPRO_APP_KEY "7b370d71-6400-4123-bfaa-d868df8ba691"
#define SINRICPRO_APP_SECRET "e82fe98f-dbc1-4184-8d91-d839dede7154-60b29460-89de-4fb0-b2b1-c21feaaafc6b"

// Device ID de cada "Switch" creado en el dashboard de SinricPro. Cada uno
// es un alias independiente para Alexa (ej. "Computadora", "PC",
// "Mamalona"), pero todos disparan el mismo encendido del relevador.
// Agregar mas IDs aqui cuando se creen mas alias en el dashboard.
#define SINRICPRO_DEVICE_IDS { "6a7a385b09efd1746c3b5f34" } // "Computadora"

// =====================================================================
// Audio / I2S
// =====================================================================

#define AUDIO_SAMPLE_RATE_HZ 16000

// Tamano del bloque leido del DMA I2S en cada iteracion de task_audio_i2s
#define I2S_READ_CHUNK_SAMPLES 512

// Ring buffer circular compartido entre task_audio_i2s (productor) y
// task_audio_process (consumidor). Tamano en bytes (PCM de 16 bits).
#define AUDIO_RING_BUFFER_BYTES (I2S_READ_CHUNK_SAMPLES * sizeof(int16_t) * 8)

// =====================================================================
// Deteccion de doble aplauso (DSP por energia RMS)
// =====================================================================

// Umbral de RMS (sobre PCM de 16 bits) por encima del cual se considera un pico.
// Ajustar experimentalmente segun ganancia del INMP441 y distancia al microfono.
#define CLAP_RMS_THRESHOLD 8500.0f

// Ventana total en la que deben ocurrir los dos aplausos. Calibrado al ritmo
// real del usuario (250-350ms entre golpes), con margen arriba.
#define CLAP_WINDOW_MS 500

// Separacion minima entre el primer y segundo aplauso. Se exige una pausa
// clara (patron "clap ... clap" deliberado, no "clap-clap" pegado) porque es
// mas facil de repetir consistentemente a mano, y porque una tos real suele
// tener su estructura interna (pop + salida de aire) mucho mas junta que
// esto, lo que ayuda a rechazarla. Margen debajo del minimo real observado.
#define CLAP_MIN_GAP_MS 200

// Tras encontrar un par de picos que parece un aplauso valido, se espera
// este tiempo sin ninguna otra rafaga (corta o sostenida) antes de disparar
// el relevador. Risas y conversacion siguen generando picos despues del
// "par" que coincidio por casualidad con la ventana; un aplauso deliberado
// termina en seco (salvo eco/resonancia, que ya se filtra aparte).
#define CLAP_POST_VALIDATION_MS 500

// Durante la ventana de validacion, un pico posterior NO cancela el
// candidato si es claramente mas debil que el aplauso (se interpreta como
// cola de eco/resonancia). Si su rms es igual o mayor a esta fraccion del
// rms del aplauso, se considera un sonido nuevo (risa/voz) y se cancela.
#define CLAP_ECHO_TOLERANCE_RATIO 0.6f

// Duracion maxima que puede durar una rafaga por encima del umbral para
// seguir considerandose un aplauso (transitorio corto). Sonidos sostenidos
// (toser, respirar cerca del mic, hablar/cantar) duran mas que esto y se
// descartan.
#define CLAP_MAX_PEAK_DURATION_MS 150

// =====================================================================
// Inferencia TinyML (Edge Impulse)
// =====================================================================

// Cambiar a 1 una vez agregada la libreria Arduino exportada desde
// Edge Impulse Studio en lib/ (ver platformio.ini).
#define EI_CLASSIFIER_ENABLED 0

// Ventana de inferencia: 1 segundo a 16kHz
#define EI_CLASSIFIER_RAW_SAMPLE_COUNT AUDIO_SAMPLE_RATE_HZ

// Solapamiento entre ventanas consecutivas: 300ms
#define ML_WINDOW_OVERLAP_SAMPLES ((AUDIO_SAMPLE_RATE_HZ * 300) / 1000)

// Etiqueta objetivo definida en el proyecto de Edge Impulse y umbral de confianza
#define EI_CLASSIFIER_TARGET_LABEL "frase_magica"
#define EI_CLASSIFIER_CONFIDENCE_THRESHOLD 0.80f

// =====================================================================
// Colas FreeRTOS
// =====================================================================

#define RELAY_QUEUE_LENGTH 4
