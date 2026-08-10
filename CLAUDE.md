# Contexto del Proyecto: Smart PC Power Controller (ESP32)

## Objetivo Principal
Desarrollar el firmware para un ESP32 que actúe como controlador de encendido para una PC de escritorio. El sistema activará un módulo relevador (optoacoplado) durante 500ms para cerrar el circuito de los pines `PWR_SW` de la placa base.

## Disparadores (Triggers)
El relevador debe activarse mediante 3 eventos distintos:
1. **Comando de Voz/Rutina (IoT):** Integración con Alexa vía Wi-Fi (emulación Wemo/FauxmoESP o SinricPro).
2. **Doble Aplauso (Audio DSP):** Detección local de picos de energía (RMS) en el buffer de audio.
3. **Inferencia TinyML (Edge Impulse):** Reconocimiento de una frase específica (Keyword Spotting) procesada de forma offline.

## Hardware y Pinout
- **Microcontrolador:** ESP32 (Dual-Core).
- **Micrófono Digital:** INMP441 (Interfaz I2S).
  - WS (Word Select): GPIO 15
  - SCK (Serial Clock): GPIO 14
  - SD (Serial Data): GPIO 32
  - L/R: Conectado a GND (Canal Izquierdo).
- **Módulo Relevador:** 1 Canal 5V (Optoacoplado).
  - IN (Control): GPIO 33 (Activo en ALTO).

## Arquitectura de Software Requerida (FreeRTOS)
El sistema debe procesar audio continuo sin bloquear la conexión Wi-Fi ni perder paquetes. Se exige una arquitectura modular orientada a tareas (Tasks) de FreeRTOS, distribuyendo la carga entre los dos núcleos del ESP32.

### Distribución de Núcleos y Tareas
*   **CORE 0 (IoT y Red):**
    *   `task_wifi_alexa`: Mantiene la conexión Wi-Fi y escucha los comandos de Alexa. Al recibir la orden de encendido, envía un evento a la cola del relevador.
*   **CORE 1 (Procesamiento Acústico y ML):**
    *   `task_audio_i2s`: Configura el DMA I2S. Lee continuamente muestras de 16-bit a 16kHz del micrófono y llena un `RingBuffer` circular.
    *   `task_audio_process`: Consume el `RingBuffer`. Evalúa la energía (RMS) buscando el patrón de doble aplauso (ventana de ~800ms). Si no hay aplauso, pasa ventanas de 1 segundo (con solapamiento de 300ms) al modelo de Edge Impulse para inferencia. Si se detecta el aplauso o la frase mágica, envía un evento a la cola del relevador.
*   **CORE Indistinto (Control de Hardware):**
    *   `task_relay_control`: Bloqueada esperando mensajes en una `Queue` de FreeRTOS. Al recibir el comando, pone el GPIO 33 en `HIGH` por 500ms y luego en `LOW`.

## Estructura de Directorios Deseada
Por favor, genera el código dividiéndolo en los siguientes archivos (estilo PlatformIO/ESP-IDF):

```text
src/
├── main.cpp                 # Entry point, inicialización de colas y creación de Tasks
├── core/
│   ├── config.h             # Definición de pines, umbrales RMS y credenciales Wi-Fi
│   └── event_types.h        # Definición de la estructura de mensajes para las Queues
├── audio/
│   ├── i2s_sampler.h/.cpp   # Configuración de I2S DMA y RingBuffer
│   └── dsp_clap.h/.cpp      # Algoritmo de detección de picos/doble aplauso
├── ml/
│   └── ei_classifier.h/.cpp # Wrapper para el SDK de Edge Impulse
├── iot/
│   └── alexa_service.h/.cpp # Gestión Wi-Fi y servidor de emulación Alexa
└── hardware/
    └── relay_ctrl.h/.cpp    # Inicialización del GPIO y tarea de control