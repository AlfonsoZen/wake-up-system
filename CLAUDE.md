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
```

## Roadmap / Próximas funcionalidades (documentado, no implementado aún)

### 1. Corte del circuito cuando la PC ya está encendida o suspendida

**Contexto:** un falso positivo (aplauso, ruido, comando de Alexa accidental) mientras la PC ya está prendida se interpreta como un toque del botón de encendido y puede apagarla/suspenderla sin querer. La forma robusta de evitar esto no es perseguir el heurístico perfecto, sino que el propio circuito sepa el estado real de la PC y se niegue a disparar si ya está encendida.

**Propuesta de hardware:**
- Tomar la señal del header `+PWR LED` / `-PWR LED` del panel frontal (`JFP1` en la MSI B550 Tomahawk del usuario) — queda en alto cuando la PC está encendida, en bajo cuando está apagada. Confirmar el pinout exacto contra la serigrafía de la placa o el manual antes de conectar (varía entre fabricantes/modelos).
- Empalmar (no reemplazar) un cable adicional en paralelo a los 2 pines de `PWR_LED`, igual que se hizo con el botón físico de encendido — el LED del gabinete sigue funcionando normal.
- **Adaptador de nivel:** usar uno de los módulos "4 channel bidirectional I2C logic level converter 3.3V↔5V" que el usuario ya tiene (comprados para el proyecto de LEDs WLED; le sobran 3 de 5 canales libres). Un canal sirve perfecto para esto — no hace falta usarlo en modo I2C, funciona igual de bien para una señal digital simple:
  - `GND` del módulo → tierra común (ESP32 + motherboard).
  - `HV` del módulo → +5V.
  - `LV` del módulo → 3.3V del ESP32.
  - `HV1` → cable empalmado a `+PWR LED`.
  - `LV1` → GPIO libre del ESP32 (candidato: GPIO 27 o GPIO 26 — libres en el pinout actual, no son pines de arranque/strapping).
  - `-PWR LED` → tierra común.

**Nota importante sobre "suspendida":** en la mayoría de las motherboards el LED de power no se apaga limpio en suspensión (S3 sleep) — parpadea lento. Una sola lectura de `digitalRead()` puede caer en alto o bajo según el instante exacto del parpadeo. Para distinguir "encendida o suspendida" (ambos casos: no disparar) de "realmente apagada", el firmware debería samplear el pin varias veces a lo largo de ~1-2 segundos: si hubo cualquier flanco alto en esa ventana, tratarlo como "encendida/suspendida" (no disparar); solo si se mantiene establemente bajo todo ese tiempo, considerarla apagada y permitir el disparo.

**Lógica de firmware (pendiente):** antes de que `task_relay_control` accione el GPIO del relevador, verificar el estado sampleado de este pin y descartar el evento silenciosamente (con un log) si la PC ya está encendida o suspendida.

### 2. Servidor de logs por HTTP (sin cable USB)

**Contexto:** revisar el log de un ESP32 hoy requiere reconectarlo por USB (vía WSL + usbipd, con los problemas de estabilidad ya vividos en este proyecto). Para depurar cómodamente desde cualquier dispositivo (celular, laptop, PC) sin cable:

- Servidor HTTP liviano corriendo en el propio ESP32 (`WebServer.h`, ya incluido en el core de arduino-esp32, sin dependencias nuevas).
- Buffer circular en RAM (~16KB) que captura todo lo que ya se manda por `ESP_LOGx`, enganchado vía `esp_log_set_vprintf()` — sigue saliendo por Serial igual que ahora, además se duplica al buffer.
- Página simple con auto-refresco (fetch cada ~1.5s) sirviendo el contenido del buffer como texto plano.
- mDNS (`ESPmDNS.h`, también incluido en el core) para entrar por `http://wake-up-system.local/` en vez de memorizar la IP.
- Corre como una tarea FreeRTOS más, en CORE 0 (IoT/Red), junto a las demás tareas de red.

**A futuro (cuando haya varios ESP32):** un servidor web por dispositivo escala mal (hay que visitar N IPs/hostnames distintos). La evolución natural es que cada ESP32 mande sus logs a un agregador central (home lab / Raspberry Pi / la propia PC, algo tipo Grafana+Loki o un syslog simple) en vez de que cada uno sirva su propia página. Por ahora, con un solo dispositivo, el servidor HTTP directo en el ESP32 es suficiente.

### 3. Actualización de firmware inalámbrica (OTA)

Es posible con `ArduinoOTA` (incluido en el core de arduino-esp32, sin dependencias nuevas).

- **La primera carga siempre tiene que ser por USB** — el ESP32 necesita estar corriendo un firmware que ya tenga `ArduinoOTA.begin()` activo antes de poder recibir la siguiente actualización por red. A partir de ahí, `pio run --target upload` puede apuntar a la IP del ESP32 en vez del puerto USB (`upload_protocol = espota`, `upload_port = <ip-del-esp32>` en `platformio.ini`).
- **Cuidado de no "olvidar" el código de OTA en una actualización futura** — si se sube un firmware que ya no llama `ArduinoOTA.begin()`, se pierde la vía inalámbrica y hay que reconectar el cable USB una vez para recuperarla.
- Conviene ponerle contraseña (`ArduinoOTA.setPassword(...)`) ya que cualquier dispositivo en la misma red podría intentar mandar un firmware si no se protege.
- Se puede combinar con el servidor de logs: ambos corren como tareas de red en CORE 0.

## Bugs conocidos / deuda técnica (detectados, no corregidos)

Detectados en la revisión del 2026-08-11 sobre el código de la v2.1.1. Cada uno es
independiente y se puede atacar por separado desde cualquier máquina.

### B1. El switch de SinricPro nunca vuelve a reportar estado "apagado"

**Dónde:** `src/iot/alexa_service.cpp`, callback `on_power_state()`.

**Síntoma:** el relevador es un pulso momentáneo (500ms), pero para Alexa/SinricPro el
dispositivo queda marcado como "encendido" de forma permanente después del primer
comando. El callback confirma la orden (`return true`) y nunca emite el evento inverso.
Consecuencia: al segundo "Alexa, enciende la computadora" el asistente puede considerar
que ya está en ese estado y responder sin volver a mandar la directiva, dejando el
disparo por voz inutilizable hasta reiniciar el ESP32.

**Arreglo propuesto:** después de que el relevador complete el pulso, emitir
`sw.sendPowerStateEvent(false)` para que el dispositivo vuelva a estado OFF y se comporte
como un botón momentáneo. Requiere que la tarea del relevador pueda notificar de vuelta
al servicio IoT (una segunda `Queue` de confirmación, o exponer una función
`alexa_service_report_off()` que `task_relay_control` invoque tras el pulso).
Ojo: `sendPowerStateEvent` debe llamarse desde el contexto de la tarea de red
(CORE 0), no directamente desde `task_relay_control`.

### B2. No hay reconexión de Wi-Fi

**Dónde:** `src/iot/alexa_service.cpp`, `connect_wifi()` y el loop de `task_wifi_alexa()`.

**Síntoma:** `connect_wifi()` bloquea en un `while (WiFi.status() != WL_CONNECTED)` una
sola vez al arranque y nunca se vuelve a evaluar. Si el router se reinicia o la señal se
cae, SinricPro sigue reintentando su WebSocket sobre una interfaz caída y el disparo por
Alexa queda muerto hasta un reset manual del ESP32. Los disparos por aplauso/ML sí
siguen funcionando (corren en CORE 1, independientes de la red).

**Arreglo propuesto:** agregar `WiFi.setAutoReconnect(true)` y `WiFi.persistent(true)`
antes del `begin()`, y dentro del loop de `task_wifi_alexa` chequear periódicamente
(cada ~5s) `WiFi.status()`; si no está conectado, hacer `WiFi.disconnect()` +
`WiFi.begin()` con backoff, y saltear el `SinricPro.handle()` mientras no haya enlace.
No volver a bloquear con un `while` infinito: la tarea tiene que seguir viva.

### B3. Credenciales en claro commiteadas al repo

**Dónde:** `src/core/config.h` — `WIFI_SSID`, `WIFI_PASSWORD`, `SINRICPRO_APP_KEY`,
`SINRICPRO_APP_SECRET`.

**Síntoma:** la contraseña del Wi-Fi de la casa y el secret de la cuenta de SinricPro
están en texto plano dentro del control de versiones. El `APP_SECRET` da control sobre
todos los dispositivos de esa cuenta SinricPro, no solo sobre este ESP32.

**Arreglo propuesto:** mover esos cuatro defines a un `src/core/config_secrets.h`
incluido desde `config.h` y agregado al `.gitignore`, dejando un
`config_secrets.example.h` versionado como plantilla. Alternativa equivalente: pasarlos
por `build_flags` en un `platformio.ini` local no versionado.
**Importante:** quitarlos del archivo actual no los borra del historial de git — si el
repo se llega a publicar, hay que además rotar la contraseña del Wi-Fi y regenerar las
credenciales en el portal de SinricPro.

### B4. (Mejora, no bug) Umbral de aplauso fijo en vez de adaptativo

**Dónde:** `CLAP_RMS_THRESHOLD` en `src/core/config.h`, consumido por
`src/audio/dsp_clap.cpp`.

**Síntoma:** el umbral es un valor absoluto calibrado a mano (8500) contra un ambiente
concreto. Un ventilador nuevo, la ventana abierta o mover el micrófono cambian el piso de
ruido y obligan a recalibrar y reflashear. Si el piso sube demasiado, aparecen falsos
positivos; si el umbral queda alto, el aplauso deja de detectarse.

**Arreglo propuesto:** calcular en runtime un piso de ruido (media móvil del RMS de los
últimos ~10s, ignorando los picos que superan el umbral vigente) y derivar el umbral
efectivo como `max(piso * FACTOR, PISO_MINIMO)`. Dejar `CLAP_RMS_THRESHOLD` como piso
mínimo absoluto para que un ambiente en silencio total no baje el umbral hasta detectar
cualquier cosa. Ataca el mismo problema de falsos positivos que la funcionalidad 1 del
roadmap, pero desde el lado del heurístico; ambas son complementarias.