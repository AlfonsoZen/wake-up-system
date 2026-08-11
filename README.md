# Wake-Up System

**Enciende tu PC de escritorio hablándole, aplaudiéndole, o pidiéndoselo a Alexa.**

Un ESP32 escucha el cuarto, vive en dos núcleos a la vez, y cuando detecta la señal correcta, le da al botón de encendido por ti.

[![Platform](https://img.shields.io/badge/platform-ESP32-blue)](https://www.espressif.com/en/products/socs/esp32)
[![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-orange)](https://platformio.org/)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20dual--core-green)](https://www.freertos.org/)
[![Status](https://img.shields.io/badge/estado-v1.0-brightgreen)]()

---

## ¿Qué hace?

Un módulo relevador está soldado en paralelo a los pines `PWR_SW` de la placa base — el mismo circuito que cierra el botón físico de encendido. El ESP32 decide *cuándo* cerrarlo, a través de tres caminos distintos:

| Disparador | Cómo funciona |
|---|---|
| 🗣️ **Alexa** | El ESP32 se anuncia en la red como un enchufe inteligente (emulación Wemo). Decir *"Alexa, enciende Computadora"* dispara el relevador. |
| 👏 **Doble aplauso** | Un micrófono digital I2S (INMP441) escucha en tiempo real. Un algoritmo de DSP detecta dos golpes de energía cortos, separados por una pausa deliberada, y filtra ruido sostenido (voz, tos, respiración). |
| 🎙️ **Frase mágica** | Un modelo de *keyword spotting* (Edge Impulse) corre sobre el mismo audio, offline, sin nube — reconoce una frase entrenada por ti. |

Cualquiera de los tres dispara el mismo evento: un pulso de 500ms en el relevador, con un *cooldown* para que una racha de falsos positivos no le esté dando al botón repetidamente.

## Arquitectura

El ESP32 tiene dos núcleos, y aquí ninguno se queda ocioso: uno entero se dedica a la red, el otro a escuchar y pensar.

```
                 ┌─────────────────────────┐        ┌─────────────────────────────┐
                 │         CORE 0          │        │            CORE 1           │
                 │      IoT / Red           │        │   Audio DSP + Inferencia    │
                 ├─────────────────────────┤        ├─────────────────────────────┤
                 │                          │        │  task_audio_i2s              │
                 │  task_wifi_alexa         │        │   DMA I2S -> RingBuffer      │
                 │   Wi-Fi + fauxmoESP      │        │            │                 │
                 │   (emulación Wemo)       │        │            v                 │
                 │                          │        │  task_audio_process           │
                 └───────────┬──────────────┘        │   RMS/duración -> aplauso?    │
                              │                        │   si no: ventana 1s -> ML     │
                              │                        └───────────┬───────────────┘
                              │                                    │
                              └───────────────┐    ┌───────────────┘
                                               v    v
                                     Queue de FreeRTOS (evento)
                                               │
                                               v
                                   task_relay_control (núcleo indistinto)
                                     pulso de 500ms en el GPIO del relevador
```

Todo se comunica por una `Queue` de FreeRTOS: cualquier tarea que detecte una señal de encendido válida solo publica un evento ahí — quien realmente mueve el GPIO es una única tarea dedicada al relevador, así nunca hay dos fuentes peleándose por el hardware.

## Hardware

| Componente | Detalle |
|---|---|
| Microcontrolador | ESP32 (dual-core) |
| Micrófono | INMP441, digital I2S |
| Relevador | Módulo 1 canal, 5V, optoacoplado |

**Pinout:**

| Señal | GPIO |
|---|---|
| Relevador (IN, activo en ALTO) | 33 |
| I2S WS (Word Select) | 15 |
| I2S SCK (Serial Clock) | 14 |
| I2S SD (Serial Data) | 32 |
| INMP441 L/R | GND o VDD según el canal que uses (ver nota) |

> **Nota de cableado:** el pin L/R del INMP441 selecciona en qué canal (izquierdo/derecho) sale el audio. Si el firmware no detecta nada (RMS siempre en 0), casi seguro es que el canal configurado en `i2s_sampler.cpp` no coincide con cómo quedó cableado ese pin — prueba cambiando entre `I2S_CHANNEL_FMT_ONLY_LEFT` y `..._ONLY_RIGHT`.

## Empezar

Este proyecto usa [PlatformIO](https://platformio.org/).

```bash
# 1. Instalar PlatformIO (si no lo tienes)
pip install platformio

# 2. Configurar tu red y ajustes en src/core/config.h
#    - WIFI_SSID / WIFI_PASSWORD
#    - ALEXA_DEVICE_NAME
#    - CLAP_RMS_THRESHOLD (calibrar según tu micrófono/ambiente)

# 3. Compilar y grabar
pio run --target upload

# 4. Ver los logs en vivo
pio device monitor -b 115200
```

El log en vivo es tu mejor herramienta de calibración: mientras corre, imprime el nivel de RMS del micrófono una vez por segundo, y detalla cada intento de aplauso (duración, energía, si calificó o no). Ajusta `CLAP_RMS_THRESHOLD`, `CLAP_MIN_GAP_MS` y `CLAP_WINDOW_MS` en `config.h` según lo que veas.

Para la frase mágica: entrena y exporta tu modelo desde [Edge Impulse Studio](https://edgeimpulse.com/) como librería Arduino, colócala en `lib/`, y activa `EI_CLASSIFIER_ENABLED` en `config.h`.

## Estructura del proyecto

```
src/
├── main.cpp                 # Entry point: colas, tareas, orquestación
├── core/
│   ├── config.h              # Pines, umbrales, credenciales Wi-Fi
│   └── event_types.h         # Estructura de mensajes entre tareas
├── audio/
│   ├── i2s_sampler.*          # DMA I2S + RingBuffer
│   └── dsp_clap.*             # Detección de doble aplauso
├── ml/
│   └── ei_classifier.*        # Wrapper del SDK de Edge Impulse
├── iot/
│   └── alexa_service.*        # Wi-Fi + emulación Alexa (fauxmoESP)
└── hardware/
    └── relay_ctrl.*            # Control del GPIO del relevador
```

## Estado actual

- [x] Encendido por Alexa (SinricPro — fauxmoESP se descartó por incompatibilidad con Echos modernos, ver `CLAUDE.md`)
- [x] Encendido por doble aplauso (RMS + duración + timing + silencio antes/después del golpe)
- [x] Cooldown de seguridad contra falsos positivos en racha
- [ ] Frase mágica (Edge Impulse) — firmware listo, falta entrenar/integrar el modelo
- [ ] Corte de seguridad cuando la PC ya está encendida/suspendida — evita que un falso positivo la apague (ver `CLAUDE.md`)
- [ ] Servidor de logs por HTTP + actualización de firmware inalámbrica (OTA) (ver `CLAUDE.md`)

---

*Hecho con FreeRTOS, cariño, y bastante café a medianoche.*
