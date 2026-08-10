#pragma once

#include <cstdint>

// Origen del evento que solicito el encendido de la PC
enum class TriggerSource : uint8_t {
    ALEXA = 0,
    DOUBLE_CLAP = 1,
    ML_KEYWORD = 2
};

// Mensaje enviado a la Queue de task_relay_control
struct RelayEvent {
    TriggerSource source;
    uint32_t timestamp_ms;
};
