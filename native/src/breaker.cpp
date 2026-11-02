                                                                                
                                        
  
                                           
                                                                               
                                    
                                                                           
                                                                                 
                                                                          
                                                                                 
                                                                              
                                                                             
                                                                              
                                                                               
                                                                              
                                                                                 
#include "analizador/breaker.hpp"

#include "analizador/analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace analizador::analysis {
namespace {

using analizador::Diagnostic;
using analizador::DiagnosticSeverity;

                                                                        
                                                  
constexpr double kEps = 1e-9;

Diagnostic makeDiagnostic(DiagnosticSeverity severity,
                          std::string code,
                          std::string message,
                          std::string context = {}) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.context = std::move(context);
    return diagnostic;
}

void addDiagnostic(std::vector<Diagnostic>& diagnostics,
                   DiagnosticSeverity severity,
                   std::string code,
                   std::string message,
                   std::string context = {}) {
    diagnostics.push_back(
        makeDiagnostic(severity, std::move(code), std::move(message),
                       std::move(context)));
}

bool isPositiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

std::string millisecondsText(double value_ms) {
    return std::to_string(value_ms) + " ms";
}

                                                                              
                                                                     
                                                                              

struct TimeInfo {
    bool ok = false;
    std::size_t samples = 0;
    double max_dt_s = 0.0;
    double median_dt_s = 0.0;
    std::string reason;
};

TimeInfo inspectTimes(const analizador::SampleData& data) {
    TimeInfo info;
    info.samples = data.sampleCount();
    if (info.samples < 2) {
        info.reason = "se requieren al menos dos muestras";
        return info;
    }
    if (data.times_s.size() != info.samples) {
        info.reason = "el número de timestamps no coincide con las muestras";
        return info;
    }

    std::vector<double> deltas;
    deltas.reserve(info.samples - 1);
    for (std::size_t sample = 0; sample < info.samples; ++sample) {
        if (!std::isfinite(data.times_s[sample])) {
            info.reason = "hay un timestamp no finito";
            return info;
        }
        if (sample == 0) {
            continue;
        }
        const double dt = data.times_s[sample] - data.times_s[sample - 1];
        if (!isPositiveFinite(dt)) {
            info.reason = "los timestamps no son estrictamente crecientes";
            return info;
        }
        deltas.push_back(dt);
        if (dt > info.max_dt_s) {
            info.max_dt_s = dt;
        }
    }
    if (deltas.empty() || !isPositiveFinite(info.max_dt_s)) {
        info.reason = "no se pudo obtener una tasa de muestreo positiva";
        return info;
    }

    const std::size_t middle = deltas.size() / 2;
    std::nth_element(deltas.begin(), deltas.begin() + middle, deltas.end());
    info.median_dt_s = deltas[middle];
    if (!isPositiveFinite(info.median_dt_s)) {
        info.reason = "la tasa mediana de muestreo no es positiva";
        return info;
    }
    info.ok = true;
    return info;
}

struct Transition {
    double time_s = 0.0;
    bool state = false;
    std::size_t sample = 0;
};

struct Track {
    bool initial_state = false;
    bool incomplete_end = false;
    std::vector<std::uint8_t> stable;                                  
    std::optional<Transition> first_rise;
    std::optional<Transition> first_fall;
};

                                                                                
                                                                               
                                                              
template <typename Decode>
Track buildTrack(const analizador::SampleData& data,
                 const std::vector<double>& times,
                 std::size_t channel,
                 double debounce_s,
                 Decode decode) {
    Track track;
    const std::size_t samples = times.size();
    if (samples == 0) {
        return track;
    }

    track.stable.assign(samples, 0);
    const std::size_t digital_channels = data.digital_channels;
    auto rawState = [&](std::size_t sample) {
        return decode(data.digital[sample * digital_channels + channel]);
    };

    track.initial_state = rawState(0);
    bool current = track.initial_state;
    std::fill(track.stable.begin(), track.stable.end(),
              static_cast<std::uint8_t>(current ? 1 : 0));

                                                                                 
                                                          
    std::size_t run_start = 0;
    bool run_state = track.initial_state;
    for (std::size_t sample = 1; sample <= samples; ++sample) {
        const bool state = sample < samples ? rawState(sample) : run_state;
        if (sample == samples || state != run_state) {
            if (run_state != current) {
                const double duration =
                    times[sample - 1] - times[run_start];
                const bool confirmed = duration + kEps >= debounce_s;
                if (!confirmed) {
                    if (sample == samples) {
                        track.incomplete_end = true;
                    }
                } else {
                    current = run_state;
                    const Transition transition{times[run_start], current,
                                                run_start};
                    if (current && !track.first_rise) {
                        track.first_rise = transition;
                    }
                    if (!current && !track.first_fall) {
                        track.first_fall = transition;
                    }
                }
            }
            std::fill(track.stable.begin() +
                          static_cast<std::ptrdiff_t>(run_start),
                      track.stable.begin() +
                          static_cast<std::ptrdiff_t>(sample),
                      static_cast<std::uint8_t>(current ? 1 : 0));
            run_start = sample;
            run_state = state;
        }
    }
    return track;
}

                                                                              
                                                    
                                                                              

struct ExtinctionInfo {
    bool attempted = false;
    std::optional<double> time_s;
    double cycle_ms = 0.0;
    double threshold_rms = 0.0;
    bool threshold_known = false;
    bool valid_after_contact = false;
    bool current_after_contact = false;
};

                                                                           
                                                                               
                                                                              
                                                                                 
                                                                          
                                                         
                                                                      
                                                                            
                                                                       
template <typename Callback>
void scanCurrentWindows(const analizador::SampleData& data,
                        const std::vector<double>& times,
                        const std::vector<std::size_t>& channels,
                        double cycle_s,
                        std::size_t first_start,
                        Callback&& callback) {
    if (channels.size() != 3 || first_start >= times.size() ||
        !isPositiveFinite(cycle_s)) return;
    const std::size_t n = times.size();
    std::array<double,3> scale{};
    for (std::size_t i=first_start;i<n;++i) {
        for (std::size_t p=0;p<3;++p) {
            const double y=data.analog[i*data.analog_channels+channels[p]];
            if (!std::isfinite(y)) return;
            scale[p]=std::max(scale[p],std::fabs(y));
        }
    }
    const auto value = [&](std::size_t i,std::size_t p) {
        return scale[p]>0 ? data.analog[i*data.analog_channels+channels[p]]/scale[p] : 0.0;
    };
    const auto area=[](double x,double y,double h) {
        return h*(x*x+x*y+y*y)/3.0;
    };
    std::vector<std::array<double,3>> integral(n);
    for (std::size_t i=first_start+1;i<n;++i) {
        const double dt=times[i]-times[i-1];
        for (std::size_t p=0;p<3;++p)
            integral[i][p]=integral[i-1][p]+area(value(i-1,p),value(i,p),dt);
    }
    std::size_t right=first_start;
    for (std::size_t left=first_start;left<n;++left) {
        const double target=times[left]+cycle_s;
        while (right<n && times[right]<target) ++right;
        if (right==n) break;                                              
        if (right-left<8) continue;
        std::array<double,3> rms{};
        double maximum=0;
        bool valid=true;
        for (std::size_t p=0;p<3;++p) {
            double high=integral[right][p];
            if (times[right]>target && right>left) {
                const double dt=times[right]-times[right-1];
                const double fraction=(target-times[right-1])/dt;
                const double y0=value(right-1,p);
                const double yt=y0+(value(right,p)-y0)*fraction;
                high=integral[right-1][p]+area(y0,yt,target-times[right-1]);
            }
            rms[p]=std::sqrt(std::max(0.0,(high-integral[left][p])/cycle_s))*scale[p];
            valid=valid && std::isfinite(rms[p]);
            maximum=std::max(maximum,rms[p]);
        }
        callback(times[left],times[left]+cycle_s/2.0,maximum,right-left,valid,rms);
    }
}

ExtinctionInfo computeExtinction(const analizador::ComtradeRecord& record,
                                 const RecordAnalysis& analysis,
                                 const BreakerTimingOptions& options,
                                 const TimeInfo& time_info,
                                 const std::optional<double>& onset_s,
                                 const std::optional<double>& contact_open_s,
                                 std::vector<Diagnostic>& diagnostics) {
    ExtinctionInfo info;
    const analizador::SampleData& data = record.data;

    std::vector<std::size_t> channels;
    bool complete_assignment = true;
    for (std::size_t slot = 0; slot < analysis.phases.index.size(); ++slot) {
        const std::optional<std::size_t>& selected = analysis.phases.index[slot];
        if (!selected.has_value()) {
            complete_assignment = false;
            continue;
        }
        const std::size_t channel = *selected;
        if (channel >= data.analog_channels) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.phase_channel_invalid",
                          "Un canal de fase seleccionado está fuera del rango "
                          "analógico; no se calcula la extinción de corriente.",
                          "índice " + std::to_string(channel));
            return info;
        }
        if (std::find(channels.begin(), channels.end(), channel) !=
            channels.end()) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.phase_channel_invalid",
                          "Un canal analógico está asignado a más de una fase; "
                          "no se calcula la extinción de corriente.",
                          "índice " + std::to_string(channel));
            return info;
        }
        channels.push_back(channel);
    }

    if (channels.empty()) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.phase_assignment_missing",
                      "No hay asignación válida de fases A/B/C; no se calcula "
                      "la extinción de corriente.");
        return info;
    }
    if (!complete_assignment) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.phase_partial",
                      "Se requieren A/B/C completas para informar despeje eléctrico; una fase ausente no se presume extinguida.");
        return info;
    }

    if (data.analog_channels == 0 ||
        data.sampleCount() > data.analog.size() / data.analog_channels) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.analog_data_missing",
                      "Faltan datos analógicos para las muestras declaradas; no "
                      "se calcula la extinción de corriente.");
        return info;
    }

    double frequency_hz = analysis.frequency_hz;
    if (!isPositiveFinite(frequency_hz)) {
        frequency_hz = record.cfg.nominal_frequency_hz;
    }
    if (!isPositiveFinite(frequency_hz)) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.frequency_invalid",
                      "La frecuencia nominal no es positiva y finita; no se "
                      "calcula la extinción por ciclo completo.");
        return info;
    }

    std::optional<std::string> common_unit;
    for (const auto channel : channels) {
        if (channel >= record.cfg.analog.size()) return info;
        const std::string unit = normalizeUnit(record.cfg.analog[channel].unit);
        const auto family = unitFamily(unit);
        if (!family || *family != "corriente" || (common_unit && *common_unit != unit)) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                "analysis.breaker.current_units",
                "La extinción requiere tres corrientes con la misma unidad CFG; no se mezclan tensiones, porcentajes, A y kA sin conversión explícita.");
            return info;
        }
        common_unit=unit;
    }
    const double cycle_s = 1.0 / frequency_hz;
    if (time_info.max_dt_s > cycle_s/8.0 + kEps) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
            "analysis.breaker.current_resolution",
            "Hay menos de ocho muestras por ciclo o un hueco demasiado grande; extinción no evaluable.");
        return info;
    }
    if (!isPositiveFinite(cycle_s)) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.frequency_invalid",
                      "El período de un ciclo no es finito; no se calcula la "
                      "extinción por ciclo completo.");
        return info;
    }
    info.cycle_ms = cycle_s * 1000.0;
    info.attempted = true;

    if (!onset_s.has_value()) {
        return info;
    }
    if (time_info.samples < 2) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.current_windows_unavailable",
                      "No hay ventanas de ciclo completo válidas; no se calcula "
                      "la extinción de corriente.");
        return info;
    }

    const auto first = std::lower_bound(record.data.times_s.begin(),
                                        record.data.times_s.end(), *onset_s);
    const std::size_t first_start =
        static_cast<std::size_t>(first - record.data.times_s.begin());
    if (first_start >= record.data.times_s.size()) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.current_windows_unavailable",
                      "El onset candidato queda fuera de la captura; no se "
                      "calcula la extinción de corriente.");
        return info;
    }

    double maximum_rms = 0.0;
    std::array<double,3> phase_reference{};
    bool any_valid_window = false;
    scanCurrentWindows(
        data, record.data.times_s, channels, cycle_s, first_start,
        [&](double, double, double max_rms, std::size_t, bool valid, const std::array<double,3>& per_phase) {
            if (!valid) {
                return;
            }
            any_valid_window = true;
            maximum_rms = std::max(maximum_rms, max_rms);
            for (std::size_t p=0;p<3;++p)
                phase_reference[p]=std::max(phase_reference[p],per_phase[p]);
        });

    if (!any_valid_window) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.current_windows_unavailable",
                      "No hay ventanas de ciclo completo válidas dentro de la "
                      "captura (datos no finitos o registro corto); no se "
                      "calcula la extinción de corriente.");
        return info;
    }
    if (!isPositiveFinite(maximum_rms)) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.current_reference_missing",
                      "No hay una corriente de falla positiva que sirva de "
                      "referencia; no se calcula la extinción.");
        return info;
    }

                                                                              
                                                                             
                                              
    const double threshold_rms =
        options.extinction_fraction * maximum_rms;
    if (!isPositiveFinite(threshold_rms)) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.current_reference_missing",
                      "El umbral relativo de extinción no es utilizable; no se "
                      "calcula la extinción.");
        return info;
    }
    info.threshold_rms = threshold_rms;
    info.threshold_known = true;

    const double persistence_s =
        static_cast<double>(options.extinction_cycles - 1) * cycle_s;
    bool in_run = false;
    bool found = false;
    bool seen_energized = false;
    double run_start_s = 0.0;
    double run_center_s = 0.0;

    scanCurrentWindows(
        data, record.data.times_s, channels, cycle_s, first_start,
        [&](double start_s, double center_s, double max_rms,
            std::size_t, bool valid, const std::array<double,3>& per_phase) {
            if (contact_open_s.has_value() &&
                start_s + kEps >= *contact_open_s) {
                if (valid) {
                    info.valid_after_contact = true;
                    if (max_rms > threshold_rms) {
                        info.current_after_contact = true;
                    }
                }
            }
            if (!valid) {
                in_run = false;
                return;
            }
            bool low=true;
            for (std::size_t p=0;p<3;++p)
                low=low && per_phase[p] <= phase_reference[p]*options.extinction_fraction*(1.0+1e-9);
            if (!low) seen_energized=true;
            if (!seen_energized) return;
            if (!low) {
                in_run = false;
                return;
            }
            if (!in_run) {
                in_run = true;
                run_start_s = start_s;
                run_center_s = center_s;
            }
            if (!found &&
                start_s - run_start_s + kEps >= persistence_s) {
                found = true;
                                                                               
                                                               
                info.time_s = run_center_s;
            }
        });

    if (!found) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.extinction_not_found",
                      "No se detectó una extinción sostenida de corriente para "
                      "todas las fases seleccionadas.",
                      "ventanas de " + millisecondsText(info.cycle_ms) +
                          ", persistencia de " +
                          std::to_string(options.extinction_cycles) +
                          " ciclo(s)");
    }
    return info;
}

}              

                                                                              
               
                                                                              

BreakerTimingResult analyzeBreakerTiming(const ComtradeRecord& record,
                                         const RecordAnalysis& analysis,
                                         const BreakerTimingOptions& options) {
    BreakerTimingResult result;
    result.applied_options = options;
    std::vector<Diagnostic>& diagnostics = result.diagnostics;

                                                                              
                                          
                                                                              
    bool options_ok = true;
    if (!std::isfinite(options.debounce_ms) || options.debounce_ms < 0.0) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Error,
                      "analysis.breaker.invalid_options",
                      "El debounce debe ser finito y no negativo.",
                      millisecondsText(options.debounce_ms));
        options_ok = false;
    }
    if (!std::isfinite(options.extinction_fraction) ||
        !(options.extinction_fraction > 0.0) ||
        options.extinction_fraction >= 1.0) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Error,
                      "analysis.breaker.invalid_options",
                      "La fracción de extinción debe ser finita y estar en "
                      "(0, 1).");
        options_ok = false;
    }
    if (options.extinction_cycles < 1) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Error,
                      "analysis.breaker.invalid_options",
                      "La persistencia de extinción debe ser de al menos un "
                      "ciclo.",
                      std::to_string(options.extinction_cycles));
        options_ok = false;
    }

    struct Assignment {
        bool present = false;
        std::size_t channel = 0;
        bool active_high = true;
        const char* label = "";
    };
    Assignment trip;
    trip.present = options.trip.channel.has_value();
    trip.channel = options.trip.channel.value_or(0);
    trip.active_high = options.trip.active_high;
    trip.label = "disparo";

    Assignment contact_52a;
    contact_52a.present = options.contact_52a.channel.has_value();
    contact_52a.channel = options.contact_52a.channel.value_or(0);
    contact_52a.active_high = options.contact_52a.active_high;
    contact_52a.label = "52a";

    Assignment contact_52b;
    contact_52b.present = options.contact_52b.channel.has_value();
    contact_52b.channel = options.contact_52b.channel.value_or(0);
    contact_52b.active_high = options.contact_52b.active_high;
    contact_52b.label = "52b";

    const bool any_mapping = trip.present || contact_52a.present ||
                             contact_52b.present;
    if (!any_mapping) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.no_mapping",
                      "No hay asignación digital explícita; no se calcula la "
                      "temporización del interruptor.");
        result.configured = false;
        return result;
    }

    bool assignments_ok = true;
    const std::size_t digital_channels = record.data.digital_channels;
    auto validateAssignment = [&](const Assignment& assignment) {
        if (!assignment.present) {
            return;
        }
        if (assignment.channel >= digital_channels) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Error,
                          "analysis.breaker.channel_range",
                          std::string("Canal digital de ") + assignment.label +
                              " fuera de rango.",
                          "índice " + std::to_string(assignment.channel) +
                              ", canales " +
                              std::to_string(digital_channels));
            assignments_ok = false;
        }
    };
    validateAssignment(trip);
    validateAssignment(contact_52a);
    validateAssignment(contact_52b);

    if (trip.present && contact_52a.present &&
        trip.channel == contact_52a.channel) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Error,
                      "analysis.breaker.channel_duplicate",
                      "El canal de disparo no puede ser el mismo que 52a.",
                      "índice " + std::to_string(trip.channel));
        assignments_ok = false;
    }
    if (trip.present && contact_52b.present &&
        trip.channel == contact_52b.channel) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Error,
                      "analysis.breaker.channel_duplicate",
                      "El canal de disparo no puede ser el mismo que 52b.",
                      "índice " + std::to_string(trip.channel));
        assignments_ok = false;
    }
    if (contact_52a.present && contact_52b.present &&
        contact_52a.channel == contact_52b.channel) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Error,
                      "analysis.breaker.channel_duplicate",
                      "52a y 52b no pueden compartir el mismo canal.",
                      "índice " + std::to_string(contact_52a.channel));
        assignments_ok = false;
    }

    if (!options_ok || !assignments_ok) {
        result.configured = false;
        return result;
    }
    result.configured = true;

                                                                              
                                       
                                                                              
    const TimeInfo time_info = inspectTimes(record.data);
    if (!time_info.ok) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.timestamps_invalid",
                      "No se puede medir con tiempo real: " + time_info.reason +
                          ".");
        return result;
    }

    const double debounce_s = options.debounce_ms / 1000.0;
    if (any_mapping) {
        result.digital_resolution_ms =
            std::max(options.debounce_ms, time_info.max_dt_s * 1000.0);
    }

    if (digital_channels == 0 ||
        record.data.sampleCount() > record.data.digital.size() / digital_channels) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.digital_data_missing",
                      "Faltan datos digitales para las muestras declaradas; no "
                      "se detectan transiciones.");
        return result;
    }

    const bool trip_active_high = trip.active_high;
    const bool a_active_high = contact_52a.active_high;
    const bool b_active_high = contact_52b.active_high;
    auto activeValue = [](std::uint8_t raw, bool active_high) {
        const bool high = raw != 0;
        return active_high ? high : !high;
    };

    std::optional<Track> trip_track;
    std::optional<Track> contact_a_track;
    std::optional<Track> contact_b_track;

    if (trip.present) {
        trip_track = buildTrack(
            record.data, record.data.times_s, trip.channel, debounce_s,
            [activeValue, trip_active_high](std::uint8_t raw) {
                return activeValue(raw, trip_active_high);
            });
    }
    if (contact_52a.present) {
        contact_a_track = buildTrack(
            record.data, record.data.times_s, contact_52a.channel, debounce_s,
            [activeValue, a_active_high](std::uint8_t raw) {
                                                
                return activeValue(raw, a_active_high);
            });
    }
    if (contact_52b.present) {
        contact_b_track = buildTrack(
            record.data, record.data.times_s, contact_52b.channel, debounce_s,
            [activeValue, b_active_high](std::uint8_t raw) {
                                                                                
                return !activeValue(raw, b_active_high);
            });
    }

                                                                              
                     
                                                                              
    std::optional<double> trip_s;
    if (!trip.present) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.trip_not_mapped",
                      "No hay canal de disparo asignado; no se calcula el "
                      "instante de disparo.");
    } else if (trip_track->initial_state) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.trip_initial_active",
                      "La entrada de disparo ya está activa en la primera "
                      "muestra; el instante de disparo es desconocido (no se "
                      "usa t=0).");
    } else if (trip_track->first_rise.has_value()) {
        trip_s = trip_track->first_rise->time_s;
    } else {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.trip_missing",
                      "No se detectó una transición sostenida de disparo.",
                      trip_track->incomplete_end
                          ? std::string(
                                "cambio final no confirmado por debounce")
                          : std::string());
    }

                                                                              
                        
                                                                              
    std::optional<double> contact_open_s;
    bool contact_contradiction = false;
    bool contradiction_reported = false;

    auto reportContradiction = [&](const std::string& context) {
        contact_contradiction = true;
        if (!contradiction_reported) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_contradiction",
                          "52a y 52b presentan una contradicción persistente; "
                          "no se informa un instante de apertura de contacto "
                          "confiable.",
                          context);
            contradiction_reported = true;
        }
    };

    if (contact_52a.present && contact_a_track->initial_state == false) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.contact_initial_open",
                      "52a ya indica abierto en la primera muestra; su instante "
                      "de apertura es desconocido (no se usa t=0).");
    }
    if (contact_52b.present && contact_b_track->initial_state == false) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.contact_initial_open",
                      "52b ya indica abierto en la primera muestra; su instante "
                      "de apertura es desconocido (no se usa t=0).");
    }

    if (contact_52a.present && contact_52b.present) {
        if (contact_a_track->initial_state !=
            contact_b_track->initial_state) {
            reportContradiction("estado inicial distinto");
        }

                                                                              
                                                                             
                                                                                
        const double contradiction_persistence_s =
            std::max(debounce_s, time_info.max_dt_s);
        std::size_t sample = 0;
        while (sample < time_info.samples) {
            const bool a_closed = contact_a_track->stable[sample] != 0;
            const bool b_closed = contact_b_track->stable[sample] != 0;
            if (a_closed == b_closed) {
                ++sample;
                continue;
            }
            std::size_t end = sample + 1;
            while (end < time_info.samples) {
                const bool next_a = contact_a_track->stable[end] != 0;
                const bool next_b = contact_b_track->stable[end] != 0;
                if (next_a == next_b) {
                    break;
                }
                ++end;
            }
            const double duration =
                record.data.times_s[end - 1] - record.data.times_s[sample];
            if (duration + kEps >= contradiction_persistence_s) {
                reportContradiction(
                    "desacuerdo desde " +
                    std::to_string(record.data.times_s[sample]) + " s");
            }
            sample = end;
        }

                                                                             
                                                                         
        std::optional<Transition> a_open;
        std::optional<Transition> b_open;
        if (contact_a_track->initial_state) {
            a_open = contact_a_track->first_fall;
        }
        if (contact_b_track->initial_state) {
            b_open = contact_b_track->first_fall;
        }
        if (a_open.has_value() && b_open.has_value()) {
            const double difference =
                std::fabs(a_open->time_s - b_open->time_s);
            const double tolerance =
                std::max(debounce_s, time_info.max_dt_s) + kEps;
            if (difference <= tolerance) {
                contact_open_s =
                    std::min(a_open->time_s, b_open->time_s);
            } else {
                reportContradiction(
                    "aperturas separadas por " +
                    millisecondsText(difference * 1000.0));
            }
        } else if (a_open.has_value() || b_open.has_value()) {
            if (!contact_contradiction) {
                addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                              "analysis.breaker.contact_pair_unconfirmed",
                              "Solo uno de los dos contactos confirma la "
                              "apertura; no se informa un instante único.");
            }
        } else if (contact_a_track->initial_state &&
                   contact_b_track->initial_state) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_missing",
                          "Ningún contacto indica una apertura sostenida.");
        }

        if (contact_a_track->initial_state &&
            contact_a_track->first_fall.has_value() &&
            contact_a_track->first_rise.has_value() &&
            contact_a_track->first_rise->time_s >
                contact_a_track->first_fall->time_s + kEps) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_rebound",
                          "52a vuelve a cerrar después de la apertura; se "
                          "conserva la primera apertura como indicio.",
                          std::to_string(contact_a_track->first_rise->time_s) +
                              " s");
        }
        if (contact_b_track->initial_state &&
            contact_b_track->first_fall.has_value() &&
            contact_b_track->first_rise.has_value() &&
            contact_b_track->first_rise->time_s >
                contact_b_track->first_fall->time_s + kEps) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_rebound",
                          "52b vuelve a cerrar después de la apertura; se "
                          "conserva la primera apertura como indicio.",
                          std::to_string(contact_b_track->first_rise->time_s) +
                              " s");
        }

        if (contact_contradiction) {
            contact_open_s.reset();
        }
    } else if (contact_52a.present) {
        if (contact_a_track->initial_state &&
            contact_a_track->first_fall.has_value()) {
            contact_open_s = contact_a_track->first_fall->time_s;
        } else if (contact_a_track->initial_state) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_missing",
                          "52a no indica una apertura sostenida.");
        }
        if (contact_a_track->initial_state &&
            contact_a_track->first_fall.has_value() &&
            contact_a_track->first_rise.has_value() &&
            contact_a_track->first_rise->time_s >
                contact_a_track->first_fall->time_s + kEps) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_rebound",
                          "52a vuelve a cerrar después de la apertura.",
                          std::to_string(contact_a_track->first_rise->time_s) +
                              " s");
        }
    } else if (contact_52b.present) {
        if (contact_b_track->initial_state &&
            contact_b_track->first_fall.has_value()) {
            contact_open_s = contact_b_track->first_fall->time_s;
        } else if (contact_b_track->initial_state) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_missing",
                          "52b no indica una apertura sostenida.");
        }
        if (contact_b_track->initial_state &&
            contact_b_track->first_fall.has_value() &&
            contact_b_track->first_rise.has_value() &&
            contact_b_track->first_rise->time_s >
                contact_b_track->first_fall->time_s + kEps) {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_rebound",
                          "52b vuelve a cerrar después de la apertura.",
                          std::to_string(contact_b_track->first_rise->time_s) +
                              " s");
        }
    } else {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.contact_not_mapped",
                      "No hay contacto 52a/52b asignado; no se calcula la "
                      "apertura de contacto.");
    }

    if (contact_open_s.has_value()) {
        addDiagnostic(diagnostics, DiagnosticSeverity::Info,
                      "analysis.breaker.contact_indication",
                      "La indicación de contacto no prueba por sí sola la "
                      "interrupción de corriente; el clearing depende de la "
                      "extinción medida.");
    }

                                                                              
                                         
                                                                              
    std::optional<double> onset_s;
    if (analysis.event.has_value() && analysis.prefalla_usable &&
        analysis.event->direction == ChangeDirection::Rise &&
        std::isfinite(analysis.event->time_s)) {
        const double candidate = analysis.event->time_s;
        if (candidate + kEps >= record.data.times_s.front() &&
            candidate <= record.data.times_s.back() + kEps) {
            onset_s = candidate;
            addDiagnostic(diagnostics, DiagnosticSeverity::Info,
                          "analysis.breaker.onset_candidate",
                          "Se usa el inicio candidato heurístico del evento; no "
                          "es el trigger ni un inicio de falla certificado.",
                          std::to_string(candidate) + " s");
        } else {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.onset_outside_capture",
                          "El inicio candidato queda fuera de la captura; no se "
                          "usa el trigger como sustituto.");
        }
    } else {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.onset_missing",
                      "No hay un ascenso con referencia previa utilizable; el inicio de falla no es identificable. No se usa el trigger ni un despeje como inicio.");
    }
    result.onset_s = onset_s;
    result.trip_s = trip_s;
    result.contact_open_s = contact_open_s;
    if (onset_s && isPositiveFinite(analysis.frequency_hz))
        result.onset_resolution_ms = 1000.0 / analysis.frequency_hz;

                                                                              
                                   
                                                                              
    if (onset_s.has_value() && trip_s.has_value()) {
        if (*trip_s + kEps >= *onset_s) {
            result.protection_ms =
                std::max(0.0, (*trip_s - *onset_s) * 1000.0);
        } else {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.protection_order",
                          "El disparo es anterior al onset candidato; no se "
                          "informa protection_ms.",
                          "disparo " + std::to_string(*trip_s) +
                              " s, onset " + std::to_string(*onset_s) + " s");
        }
    } else {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.protection_missing",
                      "No se puede calcular protection_ms sin onset y disparo "
                      "válidos.");
    }

                                                                              
                                                    
                                                                              
    if (trip_s.has_value() && contact_open_s.has_value()) {
        if (*contact_open_s + kEps >= *trip_s) {
            result.contact_opening_ms =
                std::max(0.0, (*contact_open_s - *trip_s) * 1000.0);
        } else {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.contact_opening_order",
                          "La apertura indicada es anterior al disparo; no se "
                          "informa contact_opening_ms.",
                          "apertura " + std::to_string(*contact_open_s) +
                              " s, disparo " + std::to_string(*trip_s) + " s");
        }
    } else {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.contact_opening_missing",
                      "No se puede calcular contact_opening_ms sin disparo y "
                      "apertura de contacto válidos.");
    }

                                                                              
                                                 
                                                                              
    if (onset_s.has_value()) {
        const ExtinctionInfo extinction =
            computeExtinction(record, analysis, options, time_info, onset_s,
                              contact_open_s, diagnostics);
        if (extinction.attempted) {
            result.extinction_resolution_ms = extinction.cycle_ms;
        }
        if (extinction.time_s.has_value()) {
            result.current_extinction_s = extinction.time_s;
            if (*extinction.time_s + kEps >= *onset_s) {
                result.clearing_ms =
                    std::max(0.0, (*extinction.time_s - *onset_s) * 1000.0);
                if (contact_open_s.has_value() &&
                    *extinction.time_s + kEps < *contact_open_s) {
                    addDiagnostic(
                        diagnostics, DiagnosticSeverity::Warning,
                        "analysis.breaker.extinction_before_contact",
                        "La extinción de corriente precede a la apertura "
                        "indicada por el contacto; se informa con esa "
                        "incertidumbre.",
                        "extinción " +
                            std::to_string(*extinction.time_s) +
                            " s, contacto " +
                            std::to_string(*contact_open_s) + " s");
                }
            } else {
                addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                              "analysis.breaker.clearing_order",
                              "La extinción es anterior al onset; no se informa "
                              "clearing_ms.");
            }
        } else {
            addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                          "analysis.breaker.clearing_missing",
                          "No se puede calcular clearing_ms sin una extinción "
                          "sostenida válida.");
            if (contact_open_s.has_value() && extinction.attempted &&
                extinction.current_after_contact) {
                addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                              "analysis.breaker.clearing_current_continues",
                              "La corriente sigue presente después de la "
                              "apertura indicada; no se inventa una extinción.");
            } else if (contact_open_s.has_value() && extinction.attempted &&
                       !extinction.valid_after_contact) {
                addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                              "analysis.breaker.clearing_unconfirmed",
                              "No hay ventanas completas posteriores a la "
                              "apertura del contacto para confirmar la "
                              "extinción.");
            }
        }
    } else {
        addDiagnostic(diagnostics, DiagnosticSeverity::Warning,
                      "analysis.breaker.clearing_missing",
                      "No se puede calcular clearing_ms sin onset candidato; no "
                      "se usa el trigger.");
    }

                                                                              
                             
                                                                              
    if (time_info.ok) {
        std::string context =
            "muestreo máx. " +
            millisecondsText(time_info.max_dt_s * 1000.0) + ", debounce " +
            millisecondsText(options.debounce_ms);
        if (result.extinction_resolution_ms > 0.0) {
            context += ", extinción " +
                       millisecondsText(result.extinction_resolution_ms) +
                       " (ciclo completo)";
        }
        addDiagnostic(
            diagnostics, DiagnosticSeverity::Info,
            "analysis.breaker.resolution",
            "La resolución se limita al intervalo de muestreo y al debounce; "
            "la extinción se informa con incertidumbre de un ciclo completo. No "
            "se garantiza precisión de milisegundos.",
            context);
    }

    return result;
}

}                                   
