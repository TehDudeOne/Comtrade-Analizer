                                                                 
  
                                                                                
                                                                             
                                                                          
  
                                                            
                                                                                     
                                                                                         
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "analizador/analysis.hpp"
#include "analizador/breaker.hpp"
#include "analizador/model.hpp"

namespace {

using analizador::AnalogChannel;
using analizador::CfgInfo;
using analizador::ComtradeRecord;
using analizador::DataFileType;
using analizador::DateTime;
using analizador::Diagnostic;
using analizador::DigitalChannel;
using analizador::SampleData;
using analizador::SamplingRate;
namespace analysis = analizador::analysis;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSqrt2 = 1.414213562373095048801688724209698079;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kRate = 1000.0;

int g_checks = 0;
int g_failures = 0;

void report(bool condition,
            const char* file,
            int line,
            const char* expression,
            const std::string& detail) {
    ++g_checks;
    if (condition) {
        return;
    }
    ++g_failures;
    std::fprintf(stderr, "FALLA %s:%d  %s%s%s\n", file, line, expression,
                 detail.empty() ? "" : "  -> ", detail.c_str());
}

#define CHECK_TRUE(expression) \
    report(static_cast<bool>(expression), __FILE__, __LINE__, #expression, "")

#define CHECK_NEAR(actual, expected, tolerance)                              \
    do {                                                                     \
        const double a_ = static_cast<double>(actual);                       \
        const double e_ = static_cast<double>(expected);                     \
        const double t_ = static_cast<double>(tolerance);                    \
        const bool ok_ = std::isfinite(a_) && std::fabs(a_ - e_) <= t_;      \
        report(ok_, __FILE__, __LINE__, #actual " ~= " #expected,            \
               ok_ ? std::string()                                           \
                   : "obtenido " + std::to_string(a_) + ", esperado " +      \
                         std::to_string(e_));                                \
    } while (false)

#define CHECK_STRING_CONTAINS(haystack, needle)                              \
    report((haystack).find(needle) != std::string::npos, __FILE__, __LINE__, \
           #haystack " contiene " #needle, "")

bool hasCode(const std::vector<Diagnostic>& diagnostics,
             const std::string& code) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

                                                                              
                        
                                                                              

struct RawDigital {
    std::function<std::uint8_t(double)> trip;
    std::function<std::uint8_t(double)> contact_52a;
    std::function<std::uint8_t(double)> contact_52b;
};

std::array<double, 3> threePhase(double rms_amplitude, double time) {
    const double phases[3] = {0.0, -kTwoPi / 3.0, kTwoPi / 3.0};
    std::array<double, 3> out{};
    for (std::size_t index = 0; index < 3; ++index) {
        out[index] = rms_amplitude * kSqrt2 *
                     std::sin(kTwoPi * 50.0 * time + phases[index]);
    }
    return out;
}

ComtradeRecord makeRecord(
    std::size_t count,
    double first_sample_s,
    const std::function<std::array<double, 3>(double)>& current,
    const RawDigital& digital,
    double sample_rate_hz = kRate) {
    ComtradeRecord record;
    CfgInfo& cfg = record.cfg;
    cfg.station_name = "TESTSTATION";
    cfg.device_id = "TEST";
    cfg.revision = "1999";
    cfg.format_year = 1999;
    cfg.nominal_frequency_hz = 50.0;
    cfg.data_type = DataFileType::Ascii;
    cfg.time_multiplier = 1.0;
    cfg.declared_total_channels = 6;
    cfg.rates.push_back(
        SamplingRate{sample_rate_hz, static_cast<std::uint32_t>(count)});
    cfg.start_time = DateTime{2026, 1, 1, 0, 0, 0, 0, true};
    cfg.trigger_time = DateTime{2026, 1, 1, 0, 0, 0, 100000, true};

    const char* analog_names[3] = {"IA", "IB", "IC"};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        AnalogChannel analog;
        analog.position = channel;
        analog.declared_index = static_cast<std::uint32_t>(channel + 1);
        analog.name = analog_names[channel];
        analog.unit = "A";
        analog.a = 1.0;
        analog.b = 0.0;
        analog.min_value = -32768.0;
        analog.max_value = 32767.0;
        analog.primary = 1000.0;
        analog.secondary = 5.0;
        analog.ps = "P";
        cfg.analog.push_back(analog);
    }

    const char* digital_names[3] = {"TRIP", "52A", "52B"};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        DigitalChannel entry;
        entry.position = channel;
        entry.declared_index = static_cast<std::uint32_t>(channel + 1);
        entry.name = digital_names[channel];
        entry.normal_state = "0";
        cfg.digital.push_back(entry);
    }

    SampleData& data = record.data;
    data.analog_channels = 3;
    data.digital_channels = 3;
    data.sample_numbers.reserve(count);
    data.dat_timestamp_us.reserve(count);
    data.times_s.reserve(count);
    data.analog.reserve(count * 3);
    data.digital.reserve(count * 3);
    for (std::size_t sample = 0; sample < count; ++sample) {
        const double time =
            first_sample_s + static_cast<double>(sample) / sample_rate_hz;
        data.sample_numbers.push_back(static_cast<std::uint32_t>(sample + 1));
        data.dat_timestamp_us.push_back(time * 1e6);
        data.times_s.push_back(time);
        const std::array<double, 3> values = current(time);
        for (double value : values) {
            data.analog.push_back(value);
        }
        data.digital.push_back(
            digital.trip ? digital.trip(time) : std::uint8_t{0});
        data.digital.push_back(
            digital.contact_52a ? digital.contact_52a(time)
                                : std::uint8_t{0});
        data.digital.push_back(
            digital.contact_52b ? digital.contact_52b(time)
                                : std::uint8_t{0});
    }

    record.trigger_offset_s = 0.1;
    record.first_sample_time_s = first_sample_s;
    record.first_sample_nonzero = first_sample_s != 0.0;
    return record;
}

analysis::RecordAnalysis makeAnalysis(
    std::optional<double> onset,
    const std::array<bool, 3>& phases =
        std::array<bool, 3>{true, true, true}) {
    analysis::RecordAnalysis record_analysis;
    record_analysis.frequency_hz = 50.0;
    if (onset.has_value()) {
        record_analysis.event = analysis::OnsetEvent{};
        record_analysis.event->time_s = *onset;
        record_analysis.event->direction = analysis::ChangeDirection::Rise;
        record_analysis.prefalla_usable = true;                                     
    }
    for (std::size_t index = 0; index < 3; ++index) {
        if (phases[index]) {
            record_analysis.phases.index[index] = index;
        }
    }
    return record_analysis;
}

analysis::BreakerTimingOptions makeOptions() {
    analysis::BreakerTimingOptions options;
    options.trip.channel = 0;
    options.contact_52a.channel = 1;
    options.contact_52b.channel = 2;
    options.debounce_ms = 5.0;
    options.extinction_fraction = 0.05;
    options.extinction_cycles = 1;
    return options;
}

std::function<std::array<double, 3>(double)> stepCurrent(double high_until) {
    return [high_until](double time) {
        double amplitude = 0.0;
        if (time < 0.30) {
            amplitude = 1.0;
        } else if (time < high_until) {
            amplitude = 5.0;
        }
        return threePhase(amplitude, time);
    };
}

std::function<std::uint8_t(double)> tripAfter(double time_s) {
    return [time_s](double time) {
        return static_cast<std::uint8_t>(time >= time_s ? 1 : 0);
    };
}

std::function<std::uint8_t(double)> closed52aUntil(double time_s) {
    return [time_s](double time) {
        return static_cast<std::uint8_t>(time < time_s ? 1 : 0);
    };
}

std::function<std::uint8_t(double)> open52bAfter(double time_s) {
    return [time_s](double time) {
        return static_cast<std::uint8_t>(time >= time_s ? 1 : 0);
    };
}

                                                                              
                                    
                                                                              

void testPolarityAndBasicTiming() {
    RawDigital digital;
                                                      
    digital.trip = [](double time) {
        return static_cast<std::uint8_t>(time < 0.31 ? 1 : 0);
    };
                                                 
    digital.contact_52a = [](double time) {
        return static_cast<std::uint8_t>(time < 0.45 ? 0 : 1);
    };

    ComtradeRecord record =
        makeRecord(700, 0.0, stepCurrent(0.50), digital);
    analysis::BreakerTimingOptions options = makeOptions();
    options.trip.active_high = false;
    options.contact_52a.active_high = false;
    options.contact_52b.channel.reset();             

    const auto result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.30), options);
    CHECK_TRUE(result.configured);
    CHECK_TRUE(result.onset_s.has_value());
    CHECK_NEAR(result.onset_s.value_or(-1.0), 0.30, 1e-9);
    CHECK_TRUE(result.trip_s.has_value());
    CHECK_NEAR(result.trip_s.value_or(-1.0), 0.31, 1e-9);
    CHECK_TRUE(result.contact_open_s.has_value());
    CHECK_NEAR(result.contact_open_s.value_or(-1.0), 0.45, 1e-9);
    CHECK_NEAR(result.protection_ms.value_or(-1.0), 10.0, 1e-9);
    CHECK_NEAR(result.contact_opening_ms.value_or(-1.0), 140.0, 1e-9);
    CHECK_NEAR(result.digital_resolution_ms, 5.0, 1e-9);
    CHECK_NEAR(result.extinction_resolution_ms, 20.0, 1e-6);
    CHECK_TRUE(result.current_extinction_s.has_value());
    CHECK_TRUE(result.clearing_ms.has_value());
    CHECK_TRUE(!hasCode(result.diagnostics,
                        "analysis.breaker.contact_contradiction"));
}

void test52aOnlyAnd52bOnly() {
    RawDigital digital_a;
    digital_a.trip = tripAfter(0.31);
    digital_a.contact_52a = closed52aUntil(0.45);

    ComtradeRecord record_a =
        makeRecord(700, 0.0, stepCurrent(0.55), digital_a);
    analysis::BreakerTimingOptions options_a = makeOptions();
    options_a.contact_52b.channel.reset();
    const auto result_a = analysis::analyzeBreakerTiming(
        record_a, makeAnalysis(0.30), options_a);
    CHECK_TRUE(result_a.configured);
    CHECK_TRUE(result_a.contact_open_s.has_value());
    CHECK_NEAR(result_a.contact_open_s.value_or(-1.0), 0.45, 1e-9);
    CHECK_TRUE(result_a.contact_opening_ms.has_value());
    CHECK_NEAR(result_a.contact_opening_ms.value_or(-1.0), 140.0, 1e-9);

    RawDigital digital_b;
    digital_b.trip = tripAfter(0.31);
    digital_b.contact_52b = open52bAfter(0.45);

    ComtradeRecord record_b =
        makeRecord(700, 0.0, stepCurrent(0.55), digital_b);
    analysis::BreakerTimingOptions options_b = makeOptions();
    options_b.contact_52a.channel.reset();
    const auto result_b = analysis::analyzeBreakerTiming(
        record_b, makeAnalysis(0.30), options_b);
    CHECK_TRUE(result_b.configured);
    CHECK_TRUE(result_b.contact_open_s.has_value());
    CHECK_NEAR(result_b.contact_open_s.value_or(-1.0), 0.45, 1e-9);
    CHECK_TRUE(result_b.contact_opening_ms.has_value());
    CHECK_NEAR(result_b.contact_opening_ms.value_or(-1.0), 140.0, 1e-9);

                                                                 
    RawDigital digital_b_low;
    digital_b_low.trip = tripAfter(0.31);
    digital_b_low.contact_52b = [](double time) {
        return static_cast<std::uint8_t>(time < 0.45 ? 1 : 0);
    };
    ComtradeRecord record_b_low =
        makeRecord(700, 0.0, stepCurrent(0.55), digital_b_low);
    analysis::BreakerTimingOptions options_b_low = makeOptions();
    options_b_low.contact_52a.channel.reset();
    options_b_low.contact_52b.active_high = false;
    const auto result_b_low = analysis::analyzeBreakerTiming(
        record_b_low, makeAnalysis(0.30), options_b_low);
    CHECK_TRUE(result_b_low.contact_open_s.has_value());
    CHECK_NEAR(result_b_low.contact_open_s.value_or(-1.0), 0.45, 1e-9);
}

void testBothContactsConsistentAndContradiction() {
    RawDigital consistent;
    consistent.trip = tripAfter(0.31);
    consistent.contact_52a = closed52aUntil(0.45);
    consistent.contact_52b = open52bAfter(0.45);

    ComtradeRecord record_consistent =
        makeRecord(700, 0.0, stepCurrent(0.55), consistent);
    const auto result_consistent = analysis::analyzeBreakerTiming(
        record_consistent, makeAnalysis(0.30), makeOptions());
    CHECK_TRUE(result_consistent.configured);
    CHECK_TRUE(result_consistent.contact_open_s.has_value());
    CHECK_NEAR(result_consistent.contact_open_s.value_or(-1.0), 0.45, 1e-9);
    CHECK_TRUE(!hasCode(result_consistent.diagnostics,
                        "analysis.breaker.contact_contradiction"));

                                                                        
    RawDigital contradiction;
    contradiction.trip = tripAfter(0.31);
    contradiction.contact_52a = closed52aUntil(0.45);
    contradiction.contact_52b = [](double) {
        return std::uint8_t{0};                            
    };

    ComtradeRecord record_contradiction =
        makeRecord(700, 0.0, stepCurrent(0.55), contradiction);
    const auto result_contradiction = analysis::analyzeBreakerTiming(
        record_contradiction, makeAnalysis(0.30), makeOptions());
    CHECK_TRUE(result_contradiction.configured);
    CHECK_TRUE(!result_contradiction.contact_open_s.has_value());
    CHECK_TRUE(!result_contradiction.contact_opening_ms.has_value());
    CHECK_TRUE(hasCode(result_contradiction.diagnostics,
                       "analysis.breaker.contact_contradiction"));
}

                                                                              
                                             
                                                                              

void testInitialOpenAndMissingTrip() {
    RawDigital initial_open;
    initial_open.trip = tripAfter(0.31);
    initial_open.contact_52a = [](double) {
        return std::uint8_t{0};                                      
    };

    ComtradeRecord record_open =
        makeRecord(700, 0.0, stepCurrent(0.55), initial_open);
    analysis::BreakerTimingOptions options_open = makeOptions();
    options_open.contact_52b.channel.reset();
    const auto result_open = analysis::analyzeBreakerTiming(
        record_open, makeAnalysis(0.30), options_open);
    CHECK_TRUE(result_open.configured);
    CHECK_TRUE(result_open.trip_s.has_value());
    CHECK_TRUE(!result_open.contact_open_s.has_value());
    CHECK_TRUE(!result_open.contact_opening_ms.has_value());
    CHECK_TRUE(hasCode(result_open.diagnostics,
                       "analysis.breaker.contact_initial_open"));
    if (result_open.contact_open_s.has_value()) {
        CHECK_TRUE(std::fabs(*result_open.contact_open_s) > 1e-6);
    }

                                                                    
    RawDigital missing_trip;
    missing_trip.contact_52a = closed52aUntil(0.45);

    ComtradeRecord record_missing =
        makeRecord(700, 0.0, stepCurrent(0.55), missing_trip);
    analysis::BreakerTimingOptions options_missing = makeOptions();
    options_missing.trip.channel.reset();
    options_missing.contact_52b.channel.reset();
    const auto result_missing = analysis::analyzeBreakerTiming(
        record_missing, makeAnalysis(0.30), options_missing);
    CHECK_TRUE(result_missing.configured);
    CHECK_TRUE(!result_missing.trip_s.has_value());
    CHECK_TRUE(!result_missing.protection_ms.has_value());
    CHECK_TRUE(result_missing.contact_open_s.has_value());
    CHECK_NEAR(result_missing.contact_open_s.value_or(-1.0), 0.45, 1e-9);
    CHECK_TRUE(!result_missing.contact_opening_ms.has_value());
    CHECK_TRUE(hasCode(result_missing.diagnostics,
                       "analysis.breaker.trip_not_mapped"));

                                                                       
    RawDigital initial_active;
    initial_active.trip = [](double) { return std::uint8_t{1}; };
    initial_active.contact_52a = closed52aUntil(0.450);
    ComtradeRecord record_active =
        makeRecord(700, 0.0, stepCurrent(0.550), initial_active);
    analysis::BreakerTimingOptions options_active = makeOptions();
    options_active.contact_52b.channel.reset();
    const auto result_active = analysis::analyzeBreakerTiming(
        record_active, makeAnalysis(0.300), options_active);
    CHECK_TRUE(!result_active.trip_s.has_value());
    CHECK_TRUE(!result_active.protection_ms.has_value());
    CHECK_TRUE(!result_active.contact_opening_ms.has_value());
    CHECK_TRUE(result_active.contact_open_s.has_value());
    CHECK_TRUE(hasCode(result_active.diagnostics,
                       "analysis.breaker.trip_initial_active"));

                                                       
    RawDigital no_transition;
    no_transition.trip = [](double) { return std::uint8_t{0}; };
    no_transition.contact_52a = closed52aUntil(0.450);
    ComtradeRecord record_no_transition =
        makeRecord(700, 0.0, stepCurrent(0.550), no_transition);
    analysis::BreakerTimingOptions options_no_transition = makeOptions();
    options_no_transition.contact_52b.channel.reset();
    const auto result_no_transition = analysis::analyzeBreakerTiming(
        record_no_transition, makeAnalysis(0.300), options_no_transition);
    CHECK_TRUE(!result_no_transition.trip_s.has_value());
    CHECK_TRUE(hasCode(result_no_transition.diagnostics,
                       "analysis.breaker.trip_missing"));
}

void testDebounceBounce() {
    RawDigital bounce;
                                                                   
    bounce.trip = [](double time) {
        if (time >= 0.200 && time < 0.203) {
            return std::uint8_t{1};
        }
        return static_cast<std::uint8_t>(time >= 0.400 ? 1 : 0);
    };
                                                                     
    bounce.contact_52a = [](double time) {
        if (time >= 0.400 && time < 0.403) {
            return std::uint8_t{0};
        }
        return static_cast<std::uint8_t>(time < 0.500 ? 1 : 0);
    };

    ComtradeRecord record =
        makeRecord(700, 0.0, stepCurrent(0.550), bounce);
    analysis::BreakerTimingOptions options = makeOptions();
    options.contact_52b.channel.reset();
    const auto result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.30), options);
    CHECK_TRUE(result.configured);
    CHECK_TRUE(result.trip_s.has_value());
    CHECK_NEAR(result.trip_s.value_or(-1.0), 0.400, 1e-9);
    CHECK_TRUE(result.contact_open_s.has_value());
    CHECK_NEAR(result.contact_open_s.value_or(-1.0), 0.500, 1e-9);
    CHECK_TRUE(!hasCode(result.diagnostics,
                        "analysis.breaker.contact_rebound"));
}

                                                                              
                          
                                                                              

void testExtinctionBeforeAndAfterContact() {
    RawDigital before;
    before.trip = tripAfter(0.310);
    before.contact_52a = closed52aUntil(0.550);

    ComtradeRecord record_before =
        makeRecord(700, 0.0, stepCurrent(0.450), before);
    analysis::BreakerTimingOptions options_before = makeOptions();
    options_before.contact_52b.channel.reset();
    const auto result_before = analysis::analyzeBreakerTiming(
        record_before, makeAnalysis(0.300), options_before);
    CHECK_TRUE(result_before.configured);
    CHECK_TRUE(result_before.current_extinction_s.has_value());
    CHECK_TRUE(result_before.current_extinction_s.value_or(-1.0) > 0.440);
    CHECK_TRUE(result_before.current_extinction_s.value_or(-1.0) <= 0.490);
    CHECK_TRUE(result_before.clearing_ms.has_value());
    CHECK_NEAR(result_before.clearing_ms.value_or(-1.0), 160.0, 30.0);
    CHECK_TRUE(hasCode(result_before.diagnostics,
                       "analysis.breaker.extinction_before_contact"));

    RawDigital after;
    after.trip = tripAfter(0.310);
    after.contact_52a = closed52aUntil(0.450);

    ComtradeRecord record_after =
        makeRecord(700, 0.0, stepCurrent(0.550), after);
    analysis::BreakerTimingOptions options_after = makeOptions();
    options_after.contact_52b.channel.reset();
    const auto result_after = analysis::analyzeBreakerTiming(
        record_after, makeAnalysis(0.300), options_after);
    CHECK_TRUE(result_after.configured);
    CHECK_TRUE(result_after.current_extinction_s.has_value());
    CHECK_TRUE(result_after.current_extinction_s.value_or(-1.0) > 0.540);
    CHECK_TRUE(result_after.current_extinction_s.value_or(-1.0) <= 0.590);
    CHECK_TRUE(result_after.clearing_ms.has_value());
    CHECK_NEAR(result_after.clearing_ms.value_or(-1.0), 260.0, 30.0);
    CHECK_TRUE(!hasCode(result_after.diagnostics,
                        "analysis.breaker.extinction_before_contact"));
}

void testCurrentContinuesAfterContact() {
    RawDigital digital;
    digital.trip = tripAfter(0.310);
    digital.contact_52a = closed52aUntil(0.450);

    ComtradeRecord record =
        makeRecord(800, 0.0, stepCurrent(1.000), digital);
    analysis::BreakerTimingOptions options = makeOptions();
    options.contact_52b.channel.reset();
    const auto result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), options);
    CHECK_TRUE(result.configured);
    CHECK_TRUE(result.contact_open_s.has_value());
    CHECK_NEAR(result.contact_open_s.value_or(-1.0), 0.450, 1e-9);
    CHECK_TRUE(!result.current_extinction_s.has_value());
    CHECK_TRUE(!result.clearing_ms.has_value());
    CHECK_TRUE(hasCode(result.diagnostics,
                       "analysis.breaker.clearing_current_continues"));

                                                                                
                               
    ComtradeRecord one_phase =
        makeRecord(800, 0.0, stepCurrent(0.450), digital);
    for (std::size_t sample = 0; sample < one_phase.data.sampleCount();
         ++sample) {
        const double time = one_phase.data.times_s[sample];
        if (time >= 0.450) {
            one_phase.data.analog[sample * 3 + 1] =
                5.0 * kSqrt2 * std::sin(kTwoPi * 50.0 * time - kTwoPi / 3.0);
        }
    }
    const auto one_phase_result = analysis::analyzeBreakerTiming(
        one_phase, makeAnalysis(0.300), options);
    CHECK_TRUE(!one_phase_result.current_extinction_s.has_value());
    CHECK_TRUE(!one_phase_result.clearing_ms.has_value());
}

void testExtinctionPersistenceCycles() {
    RawDigital digital;
    digital.trip = tripAfter(0.310);

    const auto brief_drop = [](double time) {
        double amplitude = 1.0;
        if (time >= 0.300 && time < 0.450) {
            amplitude = 5.0;
        } else if (time >= 0.450 && time < 0.475) {
            amplitude = 0.0;
        } else if (time >= 0.475) {
            amplitude = 5.0;
        }
        return threePhase(amplitude, time);
    };

    ComtradeRecord record = makeRecord(700, 0.0, brief_drop, digital);
    analysis::BreakerTimingOptions one_cycle = makeOptions();
    one_cycle.contact_52a.channel.reset();
    one_cycle.contact_52b.channel.reset();
    one_cycle.extinction_cycles = 1;
    const auto one_cycle_result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), one_cycle);
    CHECK_TRUE(one_cycle_result.current_extinction_s.has_value());

    analysis::BreakerTimingOptions two_cycles = one_cycle;
    two_cycles.extinction_cycles = 2;
    const auto two_cycle_result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), two_cycles);
    CHECK_TRUE(!two_cycle_result.current_extinction_s.has_value());
    CHECK_TRUE(!two_cycle_result.clearing_ms.has_value());
    CHECK_TRUE(hasCode(two_cycle_result.diagnostics,
                       "analysis.breaker.extinction_not_found"));
}

void testContactRebound() {
    RawDigital digital;
    digital.trip = tripAfter(0.310);
    digital.contact_52a = [](double time) {
        if (time >= 0.450 && time < 0.550) {
            return std::uint8_t{0};             
        }
        return std::uint8_t{1};                              
    };

    ComtradeRecord record =
        makeRecord(800, 0.0, stepCurrent(0.700), digital);
    analysis::BreakerTimingOptions options = makeOptions();
    options.contact_52b.channel.reset();
    const auto result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), options);
    CHECK_TRUE(result.contact_open_s.has_value());
    CHECK_NEAR(result.contact_open_s.value_or(-1.0), 0.450, 1e-9);
    CHECK_TRUE(hasCode(result.diagnostics,
                       "analysis.breaker.contact_rebound"));
}

void testZeroCrossingsAreNotExtinction() {
    RawDigital digital;
    digital.trip = tripAfter(0.310);
    digital.contact_52a = closed52aUntil(0.450);

    ComtradeRecord record =
        makeRecord(700, 0.0, stepCurrent(0.450), digital);
    analysis::BreakerTimingOptions options = makeOptions();
    options.contact_52b.channel.reset();
    const auto result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), options);
    CHECK_TRUE(result.current_extinction_s.has_value());
                                                                              
                                                                   
    CHECK_TRUE(result.current_extinction_s.value_or(-1.0) > 0.450 - 1e-9);
}

                                                                              
                                           
                                                                              

void testNonzeroTimestampsRemainAbsolute() {
    RawDigital digital;
    digital.trip = tripAfter(10.310);
    digital.contact_52a = closed52aUntil(10.450);

    ComtradeRecord record =
        makeRecord(700, 10.0, stepCurrent(10.500), digital);
    analysis::BreakerTimingOptions options = makeOptions();
    options.contact_52b.channel.reset();
    const auto result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(10.300), options);
    CHECK_TRUE(result.configured);
    CHECK_TRUE(result.trip_s.has_value());
    CHECK_NEAR(result.trip_s.value_or(-1.0), 10.310, 1e-6);
    CHECK_TRUE(result.contact_open_s.has_value());
    CHECK_NEAR(result.contact_open_s.value_or(-1.0), 10.450, 1e-6);
    CHECK_TRUE(result.current_extinction_s.has_value());
    CHECK_TRUE(result.current_extinction_s.value_or(0.0) > 10.500);
    CHECK_TRUE(result.current_extinction_s.value_or(0.0) < 10.600);
    CHECK_NEAR(result.protection_ms.value_or(-1.0), 10.0, 1e-6);
    CHECK_NEAR(result.contact_opening_ms.value_or(-1.0), 140.0, 1e-6);
    CHECK_NEAR(result.clearing_ms.value_or(-1.0), 210.0, 30.0);
}

void testNoOnsetAndMissingPhaseAssignment() {
    RawDigital digital;
    digital.trip = tripAfter(0.310);
    digital.contact_52a = closed52aUntil(0.450);

    ComtradeRecord record =
        makeRecord(700, 0.0, stepCurrent(0.450), digital);
    analysis::BreakerTimingOptions options = makeOptions();
    options.contact_52b.channel.reset();

    const auto no_onset = analysis::analyzeBreakerTiming(
        record, makeAnalysis(std::nullopt), options);
    CHECK_TRUE(no_onset.configured);
    CHECK_TRUE(!no_onset.onset_s.has_value());
    CHECK_TRUE(!no_onset.protection_ms.has_value());
    CHECK_TRUE(!no_onset.clearing_ms.has_value());
    CHECK_TRUE(no_onset.trip_s.has_value());
    CHECK_TRUE(no_onset.contact_open_s.has_value());
    CHECK_TRUE(hasCode(no_onset.diagnostics,
                       "analysis.breaker.onset_missing"));
                                                     
    CHECK_TRUE(!no_onset.onset_s.has_value());

    const auto outside_onset = analysis::analyzeBreakerTiming(
        record, makeAnalysis(-1.0), options);
    CHECK_TRUE(!outside_onset.onset_s.has_value());
    CHECK_TRUE(!outside_onset.protection_ms.has_value());
    CHECK_TRUE(!outside_onset.clearing_ms.has_value());
    CHECK_TRUE(hasCode(outside_onset.diagnostics,
                       "analysis.breaker.onset_outside_capture"));

    const auto no_phase = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300, {false, false, false}), options);
    CHECK_TRUE(no_phase.configured);
    CHECK_TRUE(no_phase.onset_s.has_value());
    CHECK_TRUE(no_phase.trip_s.has_value());
    CHECK_TRUE(no_phase.contact_open_s.has_value());
    CHECK_TRUE(!no_phase.current_extinction_s.has_value());
    CHECK_TRUE(!no_phase.clearing_ms.has_value());
    CHECK_TRUE(hasCode(no_phase.diagnostics,
                       "analysis.breaker.phase_assignment_missing"));
}

void testOrderingRejectsNonChronologicalEndpoints() {
    RawDigital digital;
    digital.trip = tripAfter(0.500);
    digital.contact_52a = closed52aUntil(0.400);                                

    ComtradeRecord record =
        makeRecord(800, 0.0, stepCurrent(0.550), digital);
    analysis::BreakerTimingOptions options = makeOptions();
    options.contact_52b.channel.reset();
    const auto result = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), options);
    CHECK_TRUE(result.trip_s.has_value());
    CHECK_TRUE(result.contact_open_s.has_value());
    CHECK_TRUE(!result.contact_opening_ms.has_value());
    CHECK_TRUE(hasCode(result.diagnostics,
                       "analysis.breaker.contact_opening_order"));
}

                                                                              
                                               
                                                                              

void testValidationAndInsufficientData() {
    RawDigital digital;
    digital.trip = tripAfter(0.310);
    digital.contact_52a = closed52aUntil(0.450);

    ComtradeRecord record =
        makeRecord(700, 0.0, stepCurrent(0.550), digital);

    analysis::BreakerTimingOptions invalid_debounce = makeOptions();
    invalid_debounce.debounce_ms = -1.0;
    const auto bad_debounce = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), invalid_debounce);
    CHECK_TRUE(!bad_debounce.configured);
    CHECK_TRUE(hasCode(bad_debounce.diagnostics,
                       "analysis.breaker.invalid_options"));

    analysis::BreakerTimingOptions invalid_fraction = makeOptions();
    invalid_fraction.extinction_fraction = 0.0;
    const auto bad_fraction = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), invalid_fraction);
    CHECK_TRUE(!bad_fraction.configured);

    analysis::BreakerTimingOptions full_fraction = makeOptions();
    full_fraction.extinction_fraction = 1.0;
    const auto bad_full_fraction = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), full_fraction);
    CHECK_TRUE(!bad_full_fraction.configured);

    analysis::BreakerTimingOptions invalid_cycles = makeOptions();
    invalid_cycles.extinction_cycles = 0;
    const auto bad_cycles = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), invalid_cycles);
    CHECK_TRUE(!bad_cycles.configured);

    analysis::BreakerTimingOptions duplicate = makeOptions();
    duplicate.contact_52b.channel = duplicate.contact_52a.channel;
    const auto bad_duplicate = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), duplicate);
    CHECK_TRUE(!bad_duplicate.configured);
    CHECK_TRUE(hasCode(bad_duplicate.diagnostics,
                       "analysis.breaker.channel_duplicate"));

    analysis::BreakerTimingOptions out_of_range = makeOptions();
    out_of_range.trip.channel = 99;
    const auto bad_range = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), out_of_range);
    CHECK_TRUE(!bad_range.configured);
    CHECK_TRUE(hasCode(bad_range.diagnostics,
                       "analysis.breaker.channel_range"));

    analysis::BreakerTimingOptions no_mapping = makeOptions();
    no_mapping.trip.channel.reset();
    no_mapping.contact_52a.channel.reset();
    no_mapping.contact_52b.channel.reset();
    const auto bad_mapping = analysis::analyzeBreakerTiming(
        record, makeAnalysis(0.300), no_mapping);
    CHECK_TRUE(!bad_mapping.configured);
    CHECK_TRUE(hasCode(bad_mapping.diagnostics,
                       "analysis.breaker.no_mapping"));

                                                     
    ComtradeRecord bad_times = record;
    bad_times.data.times_s[20] =
        std::numeric_limits<double>::quiet_NaN();
    const auto invalid_times = analysis::analyzeBreakerTiming(
        bad_times, makeAnalysis(0.300), makeOptions());
    CHECK_TRUE(invalid_times.configured);
    CHECK_TRUE(!invalid_times.trip_s.has_value());
    CHECK_TRUE(!invalid_times.contact_open_s.has_value());
    CHECK_TRUE(hasCode(invalid_times.diagnostics,
                       "analysis.breaker.timestamps_invalid"));

                                                                              
    ComtradeRecord short_record =
        makeRecord(5, 0.0, stepCurrent(0.550), digital);
    const auto insufficient = analysis::analyzeBreakerTiming(
        short_record, makeAnalysis(0.001), makeOptions());
    CHECK_TRUE(insufficient.configured);
    CHECK_TRUE(!insufficient.current_extinction_s.has_value());
    CHECK_TRUE(!insufficient.clearing_ms.has_value());
    CHECK_TRUE(hasCode(insufficient.diagnostics,
                       "analysis.breaker.current_windows_unavailable"));

                                                                            
                                                
    ComtradeRecord low_rate =
        makeRecord(100, 0.0, stepCurrent(0.550), digital, 100.0);
    const auto low_rate_result = analysis::analyzeBreakerTiming(
        low_rate, makeAnalysis(0.300), makeOptions());
    CHECK_TRUE(!low_rate_result.current_extinction_s.has_value());
    CHECK_TRUE(hasCode(low_rate_result.diagnostics,
                       "analysis.breaker.current_resolution"));

                                                                
    ComtradeRecord nonfinite = record;
    for (std::size_t sample = 0; sample < nonfinite.data.sampleCount();
         ++sample) {
        if (nonfinite.data.times_s[sample] >= 0.300) {
            for (std::size_t channel = 0; channel < 3; ++channel) {
                nonfinite.data.analog[sample * 3 + channel] =
                    std::numeric_limits<double>::quiet_NaN();
            }
        }
    }
    const auto bad_current = analysis::analyzeBreakerTiming(
        nonfinite, makeAnalysis(0.300), makeOptions());
    CHECK_TRUE(!bad_current.current_extinction_s.has_value());
    CHECK_TRUE(!bad_current.clearing_ms.has_value());
    CHECK_TRUE(hasCode(bad_current.diagnostics,
                       "analysis.breaker.current_windows_unavailable"));

                                            
    ComtradeRecord no_analog = record;
    no_analog.data.analog.clear();
    const auto missing_analog = analysis::analyzeBreakerTiming(
        no_analog, makeAnalysis(0.300), makeOptions());
    CHECK_TRUE(!missing_analog.current_extinction_s.has_value());
    CHECK_TRUE(hasCode(missing_analog.diagnostics,
                       "analysis.breaker.analog_data_missing"));

    ComtradeRecord no_digital = record;
    no_digital.data.digital.clear();
    const auto missing_digital = analysis::analyzeBreakerTiming(
        no_digital, makeAnalysis(0.300), makeOptions());
    CHECK_TRUE(!missing_digital.trip_s.has_value());
    CHECK_TRUE(!missing_digital.contact_open_s.has_value());
    CHECK_TRUE(hasCode(missing_digital.diagnostics,
                       "analysis.breaker.digital_data_missing"));
}

void testDirectorAdversarialCases() {
    RawDigital digital;
    digital.trip=tripAfter(0.31);
    digital.contact_52a=closed52aUntil(0.45);
    digital.contact_52b=open52bAfter(0.45);
    auto record=makeRecord(800,0.0,stepCurrent(0.45),digital);
    const auto options=makeOptions();
    auto uncertain=makeAnalysis(0.3);
    uncertain.prefalla_usable=false;
    const auto no_baseline=analysis::analyzeBreakerTiming(record,uncertain,options);
    CHECK_TRUE(no_baseline.trip_s.has_value());
    CHECK_TRUE(no_baseline.contact_open_s.has_value());
    CHECK_TRUE(!no_baseline.protection_ms);
    CHECK_TRUE(!no_baseline.clearing_ms);
    auto descending=makeAnalysis(0.3);
    descending.event->direction=analysis::ChangeDirection::Fall;
    CHECK_TRUE(!analysis::analyzeBreakerTiming(record,descending,options).clearing_ms);
    CHECK_TRUE(!analysis::analyzeBreakerTiming(record,makeAnalysis(0.3,{true,true,false}),options).clearing_ms);

    auto mismatch=record;
    mismatch.cfg.analog[1].unit="kA";
    auto mixed=analysis::analyzeBreakerTiming(mismatch,makeAnalysis(0.3),options);
    CHECK_TRUE(!mixed.clearing_ms);
    CHECK_TRUE(hasCode(mixed.diagnostics,"analysis.breaker.current_units"));
    for(auto& channel:mismatch.cfg.analog) channel.unit="V";
    CHECK_TRUE(!analysis::analyzeBreakerTiming(mismatch,makeAnalysis(0.3),options).clearing_ms);

                                                                                   
    auto unequal=record;
    for(std::size_t i=0;i<unequal.data.sampleCount();++i) {
        const double t=unequal.data.times_s[i];
        unequal.data.analog[i*3] *= 1000.0;
        unequal.data.analog[i*3+1] = 0.1*kSqrt2*std::sin(kTwoPi*50.0*t-kTwoPi/3.0);
    }
    CHECK_TRUE(!analysis::analyzeBreakerTiming(unequal,makeAnalysis(0.3),options).clearing_ms);

    auto irregular=record;
    for(std::size_t i=0;i<irregular.data.sampleCount();++i) {
        const double t=static_cast<double>(i/2)*0.002+(i%2 ? 0.0005 : 0.0);
        irregular.data.times_s[i]=t;
        irregular.data.dat_timestamp_us[i]=t*1e6;
        const auto currents=stepCurrent(0.45)(t);
        for(std::size_t p=0;p<3;++p) irregular.data.analog[i*3+p]=currents[p];
        irregular.data.digital[i*3]=digital.trip(t);
        irregular.data.digital[i*3+1]=digital.contact_52a(t);
        irregular.data.digital[i*3+2]=digital.contact_52b(t);
    }
    const auto irregular_result=analysis::analyzeBreakerTiming(irregular,makeAnalysis(0.3),options);
    CHECK_TRUE(irregular_result.clearing_ms.has_value());
    CHECK_NEAR(irregular_result.current_extinction_s.value_or(-1),0.46,0.025);
    CHECK_NEAR(irregular_result.protection_ms.value_or(-1),10.0,1.5);
    CHECK_NEAR(irregular_result.onset_resolution_ms,20.0,1e-12);
    CHECK_TRUE(irregular_result.applied_options.trip.channel==options.trip.channel);
}

}              

int main() {
    testDirectorAdversarialCases();
    testPolarityAndBasicTiming();
    test52aOnlyAnd52bOnly();
    testBothContactsConsistentAndContradiction();
    testInitialOpenAndMissingTrip();
    testDebounceBounce();
    testExtinctionBeforeAndAfterContact();
    testCurrentContinuesAfterContact();
    testExtinctionPersistenceCycles();
    testContactRebound();
    testZeroCrossingsAreNotExtinction();
    testNonzeroTimestampsRemainAbsolute();
    testNoOnsetAndMissingPhaseAssignment();
    testOrderingRejectsNonChronologicalEndpoints();
    testValidationAndInsufficientData();

    std::fprintf(stderr, "Comprobaciones: %d, fallas: %d\n", g_checks,
                 g_failures);
    return g_failures == 0 ? 0 : 1;
}
