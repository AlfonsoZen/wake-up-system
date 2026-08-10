#include "audio/dsp_clap.h"

#include <Arduino.h>
#include <cmath>
#include <esp_log.h>

#include "core/config.h"

namespace {

const char *TAG = "dsp_clap";

struct ChunkStats {
    float rms;
    // Fraccion de muestras consecutivas que cambian de signo. Un aplauso
    // (transitorio de alta frecuencia, tipo "clic") tiene un ZCR mucho mas
    // alto que un sonido de voz/respiracion/tos de la misma energia.
    float zcr;
};

ChunkStats analyze_chunk(const int16_t *samples, size_t count) {
    ChunkStats stats{0.0f, 0.0f};
    if (count == 0) {
        return stats;
    }

    double sum_sq = 0.0;
    size_t crossings = 0;
    for (size_t i = 0; i < count; i++) {
        double s = (double)samples[i];
        sum_sq += s * s;
        if (i > 0 && (samples[i] >= 0) != (samples[i - 1] >= 0)) {
            crossings++;
        }
    }

    stats.rms = (float)sqrt(sum_sq / (double)count);
    stats.zcr = (count > 1) ? (float)crossings / (float)(count - 1) : 0.0f;
    return stats;
}

// Estado de la rafaga (burst) actual por encima del umbral.
bool currently_loud = false;
uint32_t loud_start_ms = 0;
bool burst_invalidated = false;
float burst_peak_rms = 0.0f;
float burst_peak_zcr = 0.0f;

bool waiting_for_second_peak = false;
uint32_t first_peak_ms = 0;

// Par de picos que parece un aplauso valido, pendiente de confirmar que no
// venga nada mas detras (risas, conversacion siguen generando ruido).
bool pending_confirmation = false;
uint32_t pending_confirm_deadline_ms = 0;
uint32_t pending_gap_ms = 0;
float pending_reference_rms = 0.0f;

// true si un pico/rafaga posterior es claramente mas debil que el aplauso
// que se esta validando (se interpreta como cola de eco, no como sonido
// nuevo independiente).
bool looks_like_echo(float rms) {
    return rms < pending_reference_rms * CLAP_ECHO_TOLERANCE_RATIO;
}

uint32_t last_level_log_ms = 0;

} // namespace

void dsp_clap_reset() {
    currently_loud = false;
    loud_start_ms = 0;
    burst_invalidated = false;
    burst_peak_rms = 0.0f;
    burst_peak_zcr = 0.0f;
    waiting_for_second_peak = false;
    first_peak_ms = 0;
    pending_confirmation = false;
    pending_confirm_deadline_ms = 0;
    pending_gap_ms = 0;
    pending_reference_rms = 0.0f;
}

bool dsp_clap_process(const int16_t *samples, size_t sample_count, uint32_t now_ms) {
    ChunkStats stats = analyze_chunk(samples, sample_count);
    float rms = stats.rms;
    bool is_loud = rms > CLAP_RMS_THRESHOLD;
    bool detected = false;

    // Nivel RMS actual, una vez por segundo (a nivel INFO, no por cada chunk)
    // para poder calibrar CLAP_RMS_THRESHOLD sin saturar el log.
    if (now_ms - last_level_log_ms >= 1000) {
        last_level_log_ms = now_ms;
        ESP_LOGI(TAG, "nivel rms=%.1f (umbral=%.1f)", rms, CLAP_RMS_THRESHOLD);
    }

    // Expira la espera del segundo aplauso si se sale de la ventana permitida.
    if (waiting_for_second_peak && (now_ms - first_peak_ms) > CLAP_WINDOW_MS) {
        waiting_for_second_peak = false;
    }

    // Si ya paso la ventana de validacion sin que nada mas interrumpiera al
    // candidato, se confirma y se dispara el relevador.
    if (pending_confirmation && now_ms >= pending_confirm_deadline_ms) {
        pending_confirmation = false;
        detected = true;
        ESP_LOGI(TAG, "Doble aplauso confirmado (separacion=%ums, sin ruido adicional en %ums)",
                 pending_gap_ms, (unsigned)CLAP_POST_VALIDATION_MS);
    }

    if (is_loud) {
        if (!currently_loud) {
            // Flanco de subida: arranca una nueva rafaga.
            currently_loud = true;
            burst_invalidated = false;
            loud_start_ms = now_ms;
            burst_peak_rms = rms;
            burst_peak_zcr = stats.zcr;
        } else {
            if (rms > burst_peak_rms) {
                burst_peak_rms = rms;
                burst_peak_zcr = stats.zcr;
            }
            if (!burst_invalidated && (now_ms - loud_start_ms) > CLAP_MAX_PEAK_DURATION_MS) {
                // La rafaga ya duro demasiado para ser un aplauso: es un
                // sonido sostenido (tos, respiracion, voz, o el eco/resonancia
                // que sigue a un aplauso fuerte). Se invalida, pero NO se
                // cancela una espera de segundo aplauso en curso: esa espera
                // solo debe expirar por la ventana de tiempo (CLAP_WINDOW_MS),
                // para no perder un aplauso valido cuya cola de eco quede
                // clasificada como sonido sostenido.
                burst_invalidated = true;
                if (pending_confirmation) {
                    if (looks_like_echo(burst_peak_rms)) {
                        ESP_LOGI(TAG, "Sonido sostenido tolerado como eco durante validacion (rms=%.1f, aplauso=%.1f)",
                                 burst_peak_rms, pending_reference_rms);
                    } else {
                        pending_confirmation = false;
                        ESP_LOGI(TAG, "Candidato de aplauso descartado: sonido sostenido durante validacion (posible risa/voz)");
                    }
                }
                ESP_LOGI(TAG, "Sonido sostenido descartado, no es aplauso (rms=%.1f)", burst_peak_rms);
            }
        }
        return detected;
    }

    // !is_loud: si veniamos de una rafaga, este es el flanco de bajada.
    if (currently_loud) {
        currently_loud = false;
        uint32_t burst_duration_ms = now_ms - loud_start_ms;

        if (burst_invalidated || burst_duration_ms > CLAP_MAX_PEAK_DURATION_MS) {
            return false;
        }

        ESP_LOGI(TAG, "Pico de aplauso (dur=%ums, rms=%.1f, zcr=%.3f)", burst_duration_ms, burst_peak_rms, burst_peak_zcr);

        // Un pico nuevo mientras habia un candidato en validacion: si es
        // claramente mas debil que el aplauso, se tolera como cola de eco.
        // Si es comparable o mas fuerte, es senal de que el sonido continua
        // (risa, conversacion) y se descarta el candidato. Este pico igual
        // se evalua normalmente abajo, por si arranca un nuevo intento.
        if (pending_confirmation) {
            if (looks_like_echo(burst_peak_rms)) {
                ESP_LOGI(TAG, "Pico posterior tolerado como eco (rms=%.1f, aplauso=%.1f)", burst_peak_rms, pending_reference_rms);
            } else {
                pending_confirmation = false;
                ESP_LOGI(TAG, "Candidato de aplauso descartado: pico adicional detectado durante validacion (posible risa/ruido)");
            }
        }

        if (!waiting_for_second_peak) {
            first_peak_ms = loud_start_ms;
            waiting_for_second_peak = true;
        } else {
            uint32_t gap_ms = loud_start_ms - first_peak_ms;
            if (gap_ms >= CLAP_MIN_GAP_MS && gap_ms <= CLAP_WINDOW_MS) {
                // Par candidato encontrado: no se dispara todavia. Se espera
                // CLAP_POST_VALIDATION_MS sin ruido adicional antes de
                // confirmarlo (ver arriba).
                pending_confirmation = true;
                pending_confirm_deadline_ms = now_ms + CLAP_POST_VALIDATION_MS;
                pending_gap_ms = gap_ms;
                pending_reference_rms = burst_peak_rms;
                ESP_LOGI(TAG, "Par candidato de aplauso (separacion=%ums), validando %ums sin ruido adicional...",
                         gap_ms, (unsigned)CLAP_POST_VALIDATION_MS);
            } else {
                ESP_LOGI(TAG, "Segundo pico fuera de rango (separacion=%ums)", gap_ms);
            }
            waiting_for_second_peak = false;
        }
    }

    return detected;
}
