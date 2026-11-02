                                                    
  
                                                                            
                                                                                 
                                              
  
                                                                              
                                                                 
                                                                                                             
                                                                                                                 
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "analizador/analysis.hpp"
#include "analizador/model.hpp"

namespace {

using analizador::AnalogChannel;
using analizador::analysis::AnalysisOptions;
using analizador::CfgInfo;
using analizador::analysis::ChangeDirection;
using analizador::ComtradeRecord;
using analizador::DataFileType;
using analizador::DateTime;
using analizador::Diagnostic;
using analizador::DiagnosticSeverity;
using analizador::DigitalChannel;
using analizador::analysis::FaultDistanceStatus;
using analizador::analysis::PhaseSelection;
using analizador::analysis::RecordAnalysis;
using analizador::SampleData;
using analizador::SamplingRate;
using analizador::analysis::WaveformOrigin;
namespace analysis = analizador::analysis;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSqrt2 = 1.414213562373095048801688724209698079;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kPhaseA = 0.0;
constexpr double kPhaseB = -kTwoPi / 3.0;
constexpr double kPhaseC = kTwoPi / 3.0;

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

                                                                              
                                     
                                                                              

ComtradeRecord makeRecord(
    std::size_t count,
    double sample_rate_hz,
    const std::function<std::array<double, 3>(double)>& signal,
    double first_sample_s = 0.0) {
    ComtradeRecord record;
    CfgInfo& cfg = record.cfg;
    cfg.station_name = "TESTSTATION";
    cfg.device_id = "TEST";
    cfg.revision = "1999";
    cfg.format_year = 1999;
    cfg.nominal_frequency_hz = 50.0;
    cfg.data_type = DataFileType::Ascii;
    cfg.time_multiplier = 1.0;
    cfg.declared_total_channels = 3;
    cfg.rates.push_back(
        SamplingRate{sample_rate_hz, static_cast<std::uint32_t>(count)});
    cfg.start_time = DateTime{2026, 1, 1, 0, 0, 0, 0, true};
    cfg.trigger_time = DateTime{2026, 1, 1, 0, 0, 0, 100000, true};

    const char* names[3] = {"IA", "IB", "IC"};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        AnalogChannel analog;
        analog.position = channel;
        analog.declared_index = static_cast<std::uint32_t>(channel + 1);
        analog.name = names[channel];
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

    SampleData& data = record.data;
    data.analog_channels = 3;
    data.digital_channels = 0;
    data.sample_numbers.reserve(count);
    data.dat_timestamp_us.reserve(count);
    data.times_s.reserve(count);
    data.analog.reserve(count * 3);
    for (std::size_t sample = 0; sample < count; ++sample) {
        const double time =
            first_sample_s + static_cast<double>(sample) / sample_rate_hz;
        const std::array<double, 3> values = signal(time);
        data.sample_numbers.push_back(static_cast<std::uint32_t>(sample + 1));
        data.dat_timestamp_us.push_back(time * 1e6);
        data.times_s.push_back(time);
        for (double value : values) {
            data.analog.push_back(value);
        }
    }

    record.trigger_offset_s = 0.1;
    record.first_sample_time_s = first_sample_s;
    record.first_sample_nonzero = first_sample_s != 0.0;
    return record;
}

                                                                              
                                                                     
ComtradeRecord makeChannelRecord(
    std::size_t count,
    double sample_rate_hz,
    const std::vector<std::string>& names,
    const std::vector<std::string>& units,
    const std::function<std::vector<double>(double)>& signal,
    double first_sample_s = 0.0) {
    ComtradeRecord record;
    CfgInfo& cfg = record.cfg;
    cfg.station_name = "TESTSTATION";
    cfg.device_id = "TEST";
    cfg.revision = "1999";
    cfg.format_year = 1999;
    cfg.nominal_frequency_hz = 50.0;
    cfg.data_type = DataFileType::Ascii;
    cfg.time_multiplier = 1.0;
    cfg.declared_total_channels = names.size();
    cfg.rates.push_back(
        SamplingRate{sample_rate_hz, static_cast<std::uint32_t>(count)});
    cfg.start_time = DateTime{2026, 1, 1, 0, 0, 0, 0, true};
    cfg.trigger_time = DateTime{2026, 1, 1, 0, 0, 0, 100000, true};

    for (std::size_t channel = 0; channel < names.size(); ++channel) {
        AnalogChannel analog;
        analog.position = channel;
        analog.declared_index = static_cast<std::uint32_t>(channel + 1);
        analog.name = names[channel];
        analog.unit = channel < units.size() ? units[channel] : std::string();
        analog.a = 1.0;
        analog.b = 0.0;
        analog.min_value = -32768.0;
        analog.max_value = 32767.0;
        analog.primary = 1000.0;
        analog.secondary = 5.0;
        analog.ps = "P";
        cfg.analog.push_back(analog);
    }

    SampleData& data = record.data;
    data.analog_channels = names.size();
    data.digital_channels = 0;
    data.sample_numbers.reserve(count);
    data.dat_timestamp_us.reserve(count);
    data.times_s.reserve(count);
    data.analog.reserve(count * names.size());
    for (std::size_t sample = 0; sample < count; ++sample) {
        const double time =
            first_sample_s + static_cast<double>(sample) / sample_rate_hz;
        std::vector<double> values = signal(time);
        values.resize(names.size(), 0.0);
        data.sample_numbers.push_back(static_cast<std::uint32_t>(sample + 1));
        data.dat_timestamp_us.push_back(time * 1e6);
        data.times_s.push_back(time);
        for (double value : values) {
            data.analog.push_back(value);
        }
    }

    record.trigger_offset_s = 0.1;
    record.first_sample_time_s = first_sample_s;
    record.first_sample_nonzero = first_sample_s != 0.0;
    return record;
}

std::array<double, 3> makeBalanced(double amplitude, double time) {
    std::array<double, 3> out{};
    const double phases[3] = {kPhaseA, kPhaseB, kPhaseC};
    for (std::size_t index = 0; index < 3; ++index) {
        out[index] =
            amplitude * kSqrt2 * std::sin(kTwoPi * 50.0 * time + phases[index]);
    }
    return out;
}

std::array<double, 3> balancedCurrents(double time) {
    const double amplitude = (time < 0.30 || time >= 0.45) ? 1.0 : 5.0;
    std::array<double, 3> out = makeBalanced(amplitude, time);
    for (double& value : out) {
        value += 0.2;
    }
    return out;
}

std::array<double, 3> persistentHighCurrents(double time) {
    const double amplitude = time < 0.30 ? 1.0 : 5.0;
    std::array<double, 3> out = makeBalanced(amplitude, time);
    for (double& value : out) {
        value += 0.2;
    }
    return out;
}

std::array<double, 3> descendingCurrents(double time) {
    const double amplitude = time < 0.30 ? 5.0 : 1.0;
    std::array<double, 3> out = makeBalanced(amplitude, time);
    for (double& value : out) {
        value += 0.2;
    }
    return out;
}

std::array<double, 3> flatSignal(double) { return {1.0, 1.0, 1.0}; }

std::array<double, 3> highFrequencyDistortion(double time) {
    const double amplitude = time < 0.30 ? 1.0 : 5.0;
    const double frequency = time < 0.30 ? 50.0 : 950.0;
    const double phases[3] = {kPhaseA, kPhaseB, kPhaseC};
    std::array<double, 3> out{};
    for (std::size_t index = 0; index < 3; ++index) {
        out[index] = amplitude * kSqrt2 *
                         std::sin(kTwoPi * frequency * time + phases[index]) +
                     0.2;
    }
    return out;
}

                                                                              
                                                      
std::vector<double> mixedCurrentVoltageSignal(double time) {
    const std::array<double, 3> currents = balancedCurrents(time);
    const double voltage_amplitude = time < 0.30 ? 10.0 : 6.0;
    const double phases[3] = {kPhaseA, kPhaseB, kPhaseC};
    std::vector<double> out(6, 0.0);
    for (std::size_t index = 0; index < 3; ++index) {
        out[index] = currents[index];
        out[3 + index] = voltage_amplitude * kSqrt2 *
                         std::sin(kTwoPi * 50.0 * time + phases[index]);
    }
    return out;
}

                                                                          
std::vector<double> knownHarmonicSignal(double time) {
    const double value =
        10.0 * kSqrt2 * std::sin(kTwoPi * 50.0 * time) +
        2.0 * kSqrt2 * std::sin(kTwoPi * 100.0 * time) + 5.0;
    return {value, value, value};
}

std::vector<double> pureSineSignal(double time) {
    return {10.0 * kSqrt2 * std::sin(kTwoPi * 50.0 * time)};
}

std::vector<double> clippedSineSignal(double time) {
    const double peak = 10.0 * kSqrt2;
    const double limit = 0.60 * peak;
    const double value = std::max(
        -limit, std::min(limit, peak * std::sin(kTwoPi * 50.0 * time)));
    return {value};
}

std::complex<double> rotation120() {
    return std::complex<double>(std::cos(kTwoPi / 3.0), std::sin(kTwoPi / 3.0));
}

bool hasCode(const std::vector<Diagnostic>& diagnostics,
             const std::string& code) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

bool hasError(const RecordAnalysis& record) { return record.hasError(); }

double wrapDegrees(double value) {
    value = std::fmod(value, 360.0);
    if (value > 180.0) {
        value -= 360.0;
    }
    if (value < -180.0) {
        value += 360.0;
    }
    return value;
}

                                                                              
                       
                                                                              

void testTimestampAlignment() {
    ComtradeRecord record = makeRecord(500, 1000.0, balancedCurrents);
    analysis::TimeAlignment aligned = analysis::alignTimestamps(record);
    CHECK_TRUE(aligned.ok);
    CHECK_TRUE(aligned.trigger_within_capture);
    CHECK_NEAR(aligned.trigger_offset_cfg_s, 0.1, 1e-12);
    CHECK_NEAR(aligned.reference_s, 0.1, 1e-12);
    CHECK_TRUE(aligned.reference_label == "trigger");
    CHECK_TRUE(!aligned.first_sample_nonzero);

    ComtradeRecord shifted =
        makeRecord(500, 1000.0, balancedCurrents, 0.001);
    analysis::TimeAlignment shifted_aligned = analysis::alignTimestamps(shifted);
    CHECK_TRUE(shifted_aligned.ok);
    CHECK_TRUE(shifted_aligned.first_sample_nonzero);
    CHECK_NEAR(shifted_aligned.first_s, 0.001, 1e-12);
    CHECK_TRUE(hasCode(shifted_aligned.diagnostics, "dat.first_sample_nonzero"));

    ComtradeRecord outside = makeRecord(500, 1000.0, balancedCurrents);
    outside.trigger_offset_s = 0.6;
    analysis::TimeAlignment outside_aligned = analysis::alignTimestamps(outside);
    CHECK_TRUE(!outside_aligned.trigger_within_capture);
    CHECK_TRUE(outside_aligned.trigger_reference_is_midpoint);
    CHECK_NEAR(outside_aligned.reference_s, 0.5 * (0.0 + 0.499), 1e-12);
    CHECK_TRUE(hasCode(outside_aligned.diagnostics, "cfg.trigger_outside_dat"));

    ComtradeRecord unreadable = makeRecord(500, 1000.0, balancedCurrents);
    unreadable.trigger_offset_s.reset();
    unreadable.cfg.start_time.valid = false;
    analysis::TimeAlignment unreadable_aligned =
        analysis::alignTimestamps(unreadable);
    CHECK_TRUE(!unreadable_aligned.trigger_offset_known);
    CHECK_TRUE(unreadable_aligned.trigger_reference_is_midpoint);
    CHECK_TRUE(unreadable_aligned.reference_label == "referencia provisional");
    CHECK_TRUE(hasCode(unreadable_aligned.diagnostics, "cfg.trigger_unreadable"));

    ComtradeRecord disordered = makeRecord(500, 1000.0, balancedCurrents);
    disordered.data.times_s[3] = disordered.data.times_s[2];
    analysis::TimeAlignment disordered_aligned =
        analysis::alignTimestamps(disordered);
    CHECK_TRUE(!disordered_aligned.ok);
    CHECK_TRUE(hasCode(disordered_aligned.diagnostics, "dat.times_order"));
}

                                                                              
                                                   
                                                                              

void testPhasorsAndAcRms() {
    ComtradeRecord record = makeRecord(60, 1000.0, [](double time) {
        return makeBalanced(1.0, time);
    });
    const auto phasors =
        analysis::windowPhasors(record.data, {0, 1, 2}, 50.0, 0.025, 1);
    CHECK_TRUE(phasors.has_value());
    if (phasors.has_value() && phasors->size() == 3) {
        CHECK_NEAR(std::abs((*phasors)[0]), 1.0, 1e-9);
        CHECK_NEAR(std::abs((*phasors)[1]), 1.0, 1e-9);
        CHECK_NEAR(std::abs((*phasors)[2]), 1.0, 1e-9);
        const double angle_a = std::arg((*phasors)[0]) * 180.0 / kPi;
        const double angle_b = std::arg((*phasors)[1]) * 180.0 / kPi;
        const double angle_c = std::arg((*phasors)[2]) * 180.0 / kPi;
        CHECK_NEAR(wrapDegrees(angle_b - angle_a), -120.0, 1e-6);
        CHECK_NEAR(wrapDegrees(angle_c - angle_a), 120.0, 1e-6);
    }

    const auto rms = analysis::windowAcRms(record.data, 0, 0.025, 0.01);
    CHECK_TRUE(rms.has_value());
    if (rms.has_value()) {
        CHECK_NEAR(rms->rms_ac, 1.0, 1e-6);
        CHECK_NEAR(rms->mean, 0.0, 1e-9);
        CHECK_TRUE(rms->samples >= 8);
    }

    const auto phasor_values = analysis::makePhasors(
        phasors.has_value() ? *phasors : std::vector<std::complex<double>>{});
    CHECK_TRUE(phasor_values.size() == 3);
    if (!phasor_values.empty()) {
        CHECK_NEAR(phasor_values[0].magnitude(), 1.0, 1e-9);
        CHECK_TRUE(phasor_values[0].isFinite());
    }

                                                         
    CHECK_TRUE(!analysis::windowPhasors(record.data, {0}, 50.0, 0.001, 1));
    CHECK_TRUE(!analysis::windowPhasors(record.data, {0}, 0.0, 0.025, 1));
    CHECK_TRUE(!analysis::windowPhasors(record.data, {}, 50.0, 0.025, 1));
    CHECK_TRUE(!analysis::windowAcRms(record.data, 9, 0.025, 0.01));

                                                         
    ComtradeRecord sparse = makeRecord(12, 1000.0, [](double time) {
        return makeBalanced(1.0, time);
    });
    CHECK_TRUE(
        (!analysis::windowPhasors(sparse.data, {0, 1, 2}, 50.0, 0.006, 1)));

                                                                       
    ComtradeRecord corrupted = record;
    corrupted.data.analog[20 * 3 + 1] =
        std::numeric_limits<double>::quiet_NaN();
    CHECK_TRUE(
        (!analysis::windowPhasors(corrupted.data, {0, 1, 2}, 50.0, 0.025, 1)));
    corrupted.data.analog[20 * 3 + 1] = 1.0;
    corrupted.data.analog[20 * 3 + 1] =
        std::numeric_limits<double>::infinity();
    CHECK_TRUE(
        (!analysis::windowPhasors(corrupted.data, {0, 1, 2}, 50.0, 0.025, 1)));
}

                                                                              
                                                     
                                                                              

void testSequencesAndClassification() {
    const std::complex<double> a = rotation120();
    const std::complex<double> a2 = a * a;
    const std::vector<std::complex<double>> positive = {1.0, a2, a};
    const std::vector<std::complex<double>> negative = {1.0, a, a2};

    const auto pos = analysis::symmetricalComponents(positive);
    CHECK_TRUE(pos.has_value());
    if (pos.has_value()) {
        CHECK_NEAR(std::abs(pos->positive), 1.0, 1e-12);
        CHECK_NEAR(std::abs(pos->zero), 0.0, 1e-12);
        CHECK_NEAR(std::abs(pos->negative), 0.0, 1e-12);
    }
    const auto neg = analysis::symmetricalComponents(negative);
    CHECK_TRUE(neg.has_value());
    if (neg.has_value()) {
        CHECK_NEAR(std::abs(neg->negative), 1.0, 1e-12);
        CHECK_NEAR(std::abs(neg->positive), 0.0, 1e-12);
    }
    CHECK_TRUE((!analysis::symmetricalComponents({1.0, a}).has_value()));
    CHECK_TRUE((!analysis::symmetricalComponents(
                     {std::complex<double>(
                          std::numeric_limits<double>::quiet_NaN(), 0.0),
                      1.0, 1.0})
                     .has_value()));

                                                                      
    std::vector<std::complex<double>> fault;
    for (const auto& value : positive) {
        fault.push_back(value * 5.0);
    }
    const auto dominant = analysis::classifyFault(positive, fault);
    CHECK_TRUE(dominant.has_value());
    if (dominant.has_value()) {
        CHECK_TRUE(dominant->label == "Secuencia positiva dominante");
        CHECK_NEAR(dominant->i1_fault_pre, 5.0, 1e-9);
        CHECK_TRUE(dominant->i0_i1 < 1e-9);
    }

                                                           
    analysis::FaultClassifyContext contaminated;
    contaminated.pre_center_s = 0.29;
    contaminated.onset_s = 0.30;
    contaminated.window_duration_s = 0.02;
    const auto contaminated_result =
        analysis::classifyFault(positive, fault, contaminated);
    CHECK_TRUE(contaminated_result.has_value());
    if (contaminated_result.has_value()) {
        CHECK_STRING_CONTAINS(contaminated_result->label, "contaminada");
    }

                               
    std::vector<std::complex<double>> small;
    for (const auto& value : positive) {
        small.push_back(value * 1.2);
    }
    const auto insufficient = analysis::classifyFault(positive, small);
    CHECK_TRUE(insufficient.has_value());
    if (insufficient.has_value()) {
        CHECK_STRING_CONTAINS(insufficient->label, "Sin incremento suficiente");
    }

                                                
    std::vector<std::complex<double>> zero_seq;
    for (const auto& value : positive) {
        zero_seq.push_back(value * 2.0 + 0.7);
    }
    const auto grounded = analysis::classifyFault(positive, zero_seq);
    CHECK_TRUE(grounded.has_value());
    if (grounded.has_value()) {
        CHECK_STRING_CONTAINS(grounded->label, "Secuencia cero elevada");
    }

                                                          
    const std::vector<std::complex<double>> negative_fault = {
        3.0, a2 * 2.0 + a, a * 2.0 + a2};
    const auto unbalanced =
        analysis::classifyFault(positive, negative_fault);
    CHECK_TRUE(unbalanced.has_value());
    if (unbalanced.has_value()) {
        CHECK_STRING_CONTAINS(unbalanced->label, "Secuencia negativa elevada");
        CHECK_NEAR(unbalanced->i2_i1, 0.5, 1e-9);
    }

                                            
    CHECK_TRUE((!analysis::classifyFault(
                     positive, {std::complex<double>(
                                   std::numeric_limits<double>::infinity(), 0.0),
                               1.0, 1.0})
                     .has_value()));
    CHECK_TRUE((!analysis::classifyFault({1.0, 1.0}, fault).has_value()));

                                                                             
    const analysis::SequenceRatios empty =
        analysis::computeSequenceRatios(std::nullopt, std::nullopt);
    CHECK_TRUE(!empty.i0_i1.has_value() && !empty.i2_i1.has_value() &&
               !empty.i1_fault_pre.has_value());
    const analysis::SequenceRatios fault_only = analysis::computeSequenceRatios(
        std::nullopt, analysis::symmetricalComponents(fault));
    CHECK_TRUE(fault_only.i0_i1.has_value());
    CHECK_TRUE(!fault_only.i1_fault_pre.has_value());

                                                                              
                                                                                
    analysis::SequenceSet negative_only;
    negative_only.negative = std::complex<double>(1.0, 0.0);
    const analysis::SequenceRatios negative_ratios =
        analysis::computeSequenceRatios(std::nullopt, negative_only);
    CHECK_TRUE(!negative_ratios.i0_i1.has_value());
    CHECK_TRUE(!negative_ratios.i2_i1.has_value());
    CHECK_TRUE(!negative_ratios.i1_fault_pre.has_value());

    const auto negative_pre = analysis::symmetricalComponents(negative);
    const auto valid_fault = analysis::symmetricalComponents(fault);
    CHECK_TRUE((!analysis::computeSequenceRatios(negative_pre, valid_fault)
                     .i1_fault_pre.has_value()));
    CHECK_TRUE((!analysis::computeSequenceRatios(
                     std::nullopt,
                     analysis::symmetricalComponents(
                         {1.0e-15, 1.0e-15 * a, 1.0e-15 * a2}))
                     .i0_i1.has_value()));

                                                                                   
    analysis::SequenceSet scaled_fault;
    scaled_fault.zero = negative_only.zero;
    scaled_fault.positive = std::complex<double>(2.0, 0.0);
    scaled_fault.negative = std::complex<double>(1.0, 0.0);
    analysis::SequenceSet scaled_small = scaled_fault;
    scaled_small.zero *= 1.0e-12;
    scaled_small.positive *= 1.0e-12;
    scaled_small.negative *= 1.0e-12;
    analysis::SequenceSet scaled_large = scaled_fault;
    scaled_large.zero *= 1.0e12;
    scaled_large.positive *= 1.0e12;
    scaled_large.negative *= 1.0e12;
    const analysis::SequenceRatios normal_ratios =
        analysis::computeSequenceRatios(std::nullopt, scaled_fault);
    const analysis::SequenceRatios small_ratios =
        analysis::computeSequenceRatios(std::nullopt, scaled_small);
    const analysis::SequenceRatios large_ratios =
        analysis::computeSequenceRatios(std::nullopt, scaled_large);
    CHECK_TRUE(normal_ratios.i0_i1.has_value() &&
               normal_ratios.i2_i1.has_value());
    CHECK_NEAR(normal_ratios.i0_i1.value_or(-1.0), 0.0, 1e-12);
    CHECK_NEAR(normal_ratios.i2_i1.value_or(-1.0), 0.5, 1e-12);
    CHECK_TRUE(small_ratios.i0_i1.has_value() &&
               small_ratios.i2_i1.has_value());
    CHECK_TRUE(large_ratios.i0_i1.has_value() &&
               large_ratios.i2_i1.has_value());
    CHECK_NEAR(small_ratios.i2_i1.value_or(-1.0), 0.5, 1e-12);
    CHECK_NEAR(large_ratios.i2_i1.value_or(-1.0), 0.5, 1e-12);

                                                                       
    CHECK_TRUE((!analysis::symmetricalComponents(
                     {std::complex<double>(1.0e308, 0.0),
                      std::complex<double>(1.0e308, 0.0),
                      std::complex<double>(1.0e308, 0.0)})
                     .has_value()));

                                                                                    
    CHECK_TRUE((!analysis::classifyFault(negative, fault).has_value()));
}

                                                                              
                                          
                                                                              

void testHarmonicsAndNyquist() {
    ComtradeRecord record = makeRecord(50, 1000.0, [](double time) {
        std::array<double, 3> out{};
        const double fundamental = kSqrt2 * std::sin(kTwoPi * 50.0 * time);
        const double second =
            0.05 * kSqrt2 * std::sin(kTwoPi * 100.0 * time);
        out[0] = fundamental + second;
        out[1] = fundamental;
        out[2] = fundamental;
        return out;
    });
    const analysis::HarmonicFit fit =
        analysis::harmonicFit(record.data, 0, 50.0, 0.025, 1);
    CHECK_TRUE(fit.ok);
    CHECK_TRUE(fit.supported_order >= 2);
    CHECK_TRUE(fit.harmonics_rms.size() >= 2);
    if (fit.harmonics_rms.size() >= 2) {
        CHECK_NEAR(fit.h2_h1_pct.value_or(-1.0), 5.0, 0.4);
        CHECK_NEAR(fit.thd_pct.value_or(-1.0), 5.0, 0.4);
        CHECK_NEAR(fit.harmonics_rms[0], 1.0, 1e-3);
    }

                                                                           
                                                   
    ComtradeRecord nyquist = makeRecord(13, 200.0, [](double time) {
        std::array<double, 3> out{};
        out[0] = std::sin(kTwoPi * 50.0 * time) +
                 0.2 * std::sin(kTwoPi * 100.0 * time);
        out[1] = out[0];
        out[2] = out[0];
        return out;
    });
    const analysis::HarmonicFit limited =
        analysis::harmonicFit(nyquist.data, 0, 50.0, 0.04, 2);
    CHECK_TRUE(limited.ok);
    CHECK_TRUE(limited.supported_order == 1);
    CHECK_TRUE(limited.harmonics_rms.size() == 1);
    CHECK_TRUE(!limited.h2_h1_pct.has_value());
    CHECK_TRUE(!limited.thd_pct.has_value());

                                              
    CHECK_TRUE(!analysis::harmonicFit(record.data, 0, 50.0, 0.001, 1).ok);
    CHECK_TRUE(!analysis::harmonicFit(record.data, 9, 50.0, 0.025, 1).ok);
}

                                                                              
                                                        
                                                                              

void testOnsetAndWindows() {
                                                                        
    auto trigger_independent = makeRecord(1200, 1000.0, persistentHighCurrents);
    for (double trigger : {0.1, 0.5, 0.9}) {
        const auto e = analysis::detectOnset(trigger_independent.data, {0, 1, 2}, 50.0, trigger);
        CHECK_TRUE(e.has_value());
        if (e) CHECK_NEAR(e->time_s, 0.30, 0.015);
    }
                                                                           
    auto exchanged = makeRecord(1200, 1000.0, [](double t) {
        auto x = makeBalanced(1.0, t);
        x[0] *= t < 0.3 ? 1.0 : 5.0;
        x[1] *= t < 0.3 ? 5.0 : 1.0;
        return x;
    });
    const auto exchange_event = analysis::detectOnset(exchanged.data, {0, 1, 2}, 50.0, 0.9);
    CHECK_TRUE(exchange_event.has_value());
    if (exchange_event) CHECK_NEAR(exchange_event->time_s, 0.3, 0.015);
                                                                          
                                                                             
    auto late_trigger = makeRecord(1200, 1000.0, persistentHighCurrents);
    late_trigger.trigger_offset_s = 0.9;
    const auto late_event = analysis::detectOnset(
        late_trigger.data, {0, 1, 2}, 50.0, 0.9);
    CHECK_TRUE(late_event.has_value());
    if (late_event) {
        CHECK_NEAR(late_event->time_s, 0.30, 0.015);
        CHECK_TRUE(late_event->direction == ChangeDirection::Rise);
        CHECK_STRING_CONTAINS(late_event->baseline_source, "inicial estable");
    }
    auto late_fall = makeRecord(1200, 1000.0, descendingCurrents);
    const auto late_fall_event = analysis::detectOnset(
        late_fall.data, {0, 1, 2}, 50.0, 0.9);
    CHECK_TRUE(late_fall_event.has_value());
    if (late_fall_event) {
        CHECK_NEAR(late_fall_event->time_s, 0.30, 0.015);
        CHECK_TRUE(late_fall_event->direction == ChangeDirection::Fall);
    }
    ComtradeRecord record = makeRecord(500, 1000.0, balancedCurrents);
    const auto event = analysis::detectOnset(record.data, {0, 1, 2}, 50.0, 0.1);
    CHECK_TRUE(event.has_value());
    if (event.has_value()) {
        CHECK_NEAR(event->time_s, 0.30, 0.015);
        CHECK_TRUE(event->direction == ChangeDirection::Rise);
        CHECK_TRUE(event->direction_text == "ascenso");
        CHECK_TRUE(event->recovery_detected);
        CHECK_TRUE(!event->recovery_censored);
        CHECK_TRUE(!event->duration_is_lower_bound);
        CHECK_TRUE(event->recovery_s.has_value());
        if (event->recovery_s.has_value()) {
            CHECK_TRUE(*event->recovery_s >= event->time_s);
            CHECK_NEAR(event->duration_s, *event->recovery_s - event->time_s, 1e-12);
        }
        CHECK_TRUE(event->peak_contrast.has_value());
        if (event->peak_contrast.has_value()) {
            CHECK_TRUE(*event->peak_contrast >= 1.0);
        }
        CHECK_TRUE(event->contrast > 1.0);
        CHECK_TRUE(event->window_samples > 0);
        CHECK_TRUE(!event->baseline_source.empty());
    }

                                                                        
    ComtradeRecord persistent =
        makeRecord(500, 1000.0, persistentHighCurrents);
    const auto censored =
        analysis::detectOnset(persistent.data, {0, 1, 2}, 50.0, 0.1);
    CHECK_TRUE(censored.has_value());
    if (censored.has_value()) {
        CHECK_NEAR(censored->time_s, 0.30, 0.015);
        CHECK_TRUE(!censored->recovery_detected);
        CHECK_TRUE(censored->recovery_censored);
        CHECK_TRUE(!censored->recovery_s.has_value());
        CHECK_TRUE(censored->duration_is_lower_bound);
        CHECK_TRUE(censored->duration_s > 0.15);
    }

                          
    ComtradeRecord descending =
        makeRecord(500, 1000.0, descendingCurrents);
    const auto falling =
        analysis::detectOnset(descending.data, {0, 1, 2}, 50.0, 0.1);
    CHECK_TRUE(falling.has_value());
    if (falling.has_value()) {
        CHECK_NEAR(falling->time_s, 0.30, 0.015);
        CHECK_TRUE(falling->direction == ChangeDirection::Fall);
        CHECK_TRUE(falling->direction_text == "descenso");
    }

                                            
    ComtradeRecord flat = makeRecord(200, 1000.0, flatSignal);
    CHECK_TRUE((!analysis::detectOnset(flat.data, {0, 1, 2}, 50.0, 0.1)));
                                            
    CHECK_TRUE((!analysis::detectOnset(record.data, {0, 1}, 50.0, 0.1)));
                               
    CHECK_TRUE((analysis::detectEvent(record.data, {0, 1, 2}, 50.0, 0.1)
                    .has_value()));

                                                                             
    const analysis::WindowCenters wide =
        analysis::defaultWindowCenters(std::nullopt, 0.0, 2.0, 50.0, 3, 0.5);
    const double half = 3.0 / (2.0 * 50.0);
    CHECK_TRUE(wide.prefalla_s + half <= wide.falla_s - half + 1e-12);
    CHECK_TRUE(wide.prefalla_s - half >= 0.0);
    CHECK_TRUE(wide.falla_s + half <= 2.0);

    const analysis::ClampedCenter clamped =
        analysis::clampCenter(0.001, 0.0, 0.2, 0.01);
    CHECK_TRUE(clamped.clamped);
    CHECK_NEAR(clamped.center_s, 0.01, 1e-12);
    const analysis::ClampedCenter untouched =
        analysis::clampCenter(0.1, 0.0, 0.2, 0.01);
    CHECK_TRUE(!untouched.clamped);
    CHECK_NEAR(untouched.center_s, 0.1, 1e-12);

                                                                                 
    const analysis::ClampedCenter degenerate =
        analysis::clampCenter(0.02, 0.0, 0.015, 0.01);
    CHECK_TRUE(degenerate.clamped);
    CHECK_NEAR(degenerate.center_s, 0.0075, 1e-12);

                                                                          
    auto already_high = makeRecord(700, 1000.0, [](double t) { return makeBalanced(15.0, t); });
    auto high_result = analysis::analyzeRecord(already_high);
    CHECK_TRUE(!high_result.prefalla_usable);
    CHECK_TRUE(!high_result.classification);
    CHECK_TRUE(!high_result.ratios.i1_fault_pre);
    CHECK_TRUE(hasCode(high_result.diagnostics, "analysis.reference.unverified"));
    CHECK_TRUE(hasCode(high_result.diagnostics, "analysis.event.unidentified"));
    auto mixed_units = late_trigger;
    mixed_units.cfg.analog[1].unit = "kA";
    const auto mixed_result = analysis::analyzeRecord(mixed_units);
    CHECK_TRUE(!mixed_result.ratios.i2_i1);
    CHECK_TRUE(!mixed_result.falla_sequences);
    CHECK_TRUE(!mixed_result.classification);
    CHECK_TRUE(hasCode(mixed_result.diagnostics,"analysis.sequence.units_incompatible"));
                                                                                         
    const auto clearing = analysis::analyzeRecord(late_fall);
    CHECK_TRUE(!clearing.prefalla_usable);
    CHECK_TRUE(!clearing.classification);
                                                                               
    analysis::AnalysisOptions manual;
    manual.prefalla_center_s = 0.6;
    manual.falla_center_s = 0.8;
    const auto contaminated = analysis::analyzeRecord(late_trigger, manual);
    CHECK_NEAR(contaminated.prefalla_center_s, 0.6, 1e-12);
    CHECK_NEAR(contaminated.falla_center_s, 0.8, 1e-12);
    CHECK_TRUE(contaminated.prefalla_manual);
    CHECK_TRUE(!contaminated.prefalla_usable);
    CHECK_TRUE(!contaminated.classification);
                                                                                
    auto unstable = makeRecord(1200, 1000.0, [](double t) {
        return makeBalanced(t < 0.08 ? 1.0 + 60.0 * t : 10.0, t);
    });
    const auto unstable_result = analysis::analyzeRecord(unstable);
    CHECK_TRUE(!unstable_result.prefalla_usable);
    CHECK_TRUE(!unstable_result.classification);
    auto stages = makeRecord(1400, 1000.0, [](double t) {
        return makeBalanced(t < 0.3 ? 1.0 : t < 0.6 ? 5.0 : t < 0.95 ? 9.0 : 0.0, t);
    });
    const auto staged = analysis::analyzeRecord(stages);
    for (double target : {0.3, 0.6, 0.95}) {
        bool found = false;
        for (const auto& change : staged.regime_changes) {
            if (std::fabs(change.time_s - target) <= 0.04) found = true;
        }
        CHECK_TRUE(found);
    }
    auto ramped = makeRecord(1000, 1000.0, [](double t) {
        auto x = makeBalanced(1.0, t);
        x[0] *= t < 0.3 ? 1.0 : 7.0;
        x[1] *= t < 0.3 ? 1.0 : t < 0.34 ? 1.0 + 175.0 * (t - 0.3) : 8.0;
        return x;
    });
    const auto ramp_result = analysis::analyzeRecord(ramped);
    bool ramp_b = false;
    for (const auto& change : ramp_result.regime_changes) {
        if (change.phase_direction[1] > 0 && change.time_s >= 0.26 && change.time_s <= 0.38)
            ramp_b = true;
    }
    CHECK_TRUE(ramp_b);
}

                                                                              
                             
                                                                              

void testWindowCoherenceGates() {
    ComtradeRecord record = makeRecord(201, 1000.0, [](double time) {
        return makeBalanced(1.0, time);
    });
    const analysis::WindowCheckResult checks = analysis::checkPhasorWindows(
        record.data, {0, 1, 2}, 50.0,
        {{"Prefalla", 0.09}, {"Falla", 0.10}}, 1, {"IA", "IB", "IC"});
    CHECK_TRUE(checks.rows.size() == 6);
    bool overlap = false;
    for (const Diagnostic& error : checks.errors) {
        if (error.code == "analysis.window.overlap") {
            overlap = true;
        }
    }
    CHECK_TRUE(overlap);
    for (const analysis::WindowCoherenceRow& row : checks.rows) {
        CHECK_NEAR(row.ratio, 1.0, 0.02);
        CHECK_NEAR(row.fundamental_rms, 1.0, 0.02);
    }

    const analysis::WindowCheckResult outside = analysis::checkPhasorWindows(
        record.data, {0, 1, 2}, 50.0, {{"Fuera", 0.005}}, 1,
        {"IA", "IB", "IC"});
    CHECK_TRUE(outside.rows.empty());
    CHECK_TRUE(outside.errors.size() == 1);
    if (!outside.errors.empty()) {
        CHECK_TRUE(outside.errors.front().code == "analysis.window.outside");
    }

                                                                            
    ComtradeRecord distorted =
        makeRecord(1000, 2000.0, highFrequencyDistortion);
    const analysis::WindowCheckResult distorted_checks =
        analysis::checkPhasorWindows(distorted.data, {0, 1, 2}, 50.0,
                                     {{"Falla", 0.33}}, 1, {"IA", "IB", "IC"});
    bool low_fundamental = false;
    for (const Diagnostic& error : distorted_checks.errors) {
        if (error.code == "analysis.window.fundamental_low") {
            low_fundamental = true;
        }
    }
    CHECK_TRUE(low_fundamental);
    CHECK_TRUE(!distorted_checks.rows.empty());
    if (!distorted_checks.rows.empty()) {
        CHECK_TRUE(distorted_checks.rows.front().ratio > 10.0);
    }

                                                    
    CHECK_TRUE((!analysis::checkPhasorWindows(record.data, {}, 50.0,
                                              {{"Prefalla", 0.09}}, 1, {})
                     .errors.empty()));
    CHECK_TRUE((!analysis::checkPhasorWindows(record.data, {0}, 0.0,
                                              {{"Prefalla", 0.09}}, 1, {})
                     .errors.empty()));
}

                                                                              
                       
                                                                              

void testPhaseChannelAssignment() {
    ComtradeRecord record = makeRecord(40, 1000.0, balancedCurrents);
    const PhaseSelection complete = analysis::selectPhaseChannels(record.cfg);
    CHECK_TRUE(complete.complete());
    CHECK_TRUE(complete.inOrder().size() == 3);
    CHECK_TRUE(complete.prefix().size() == 3);
    CHECK_TRUE(complete.has('A') && complete.has('B') && complete.has('C'));
    CHECK_TRUE(!complete.has('X'));

                                                                             
    CfgInfo prefixed_cfg = record.cfg;
    prefixed_cfg.analog[0].name = "F1-IA";
    prefixed_cfg.analog[1].name = "F2-IB";
    prefixed_cfg.analog[2].name = "F3-IC";
    auto ground = prefixed_cfg.analog.front();
    ground.name = "F4-IG";
    prefixed_cfg.analog.insert(prefixed_cfg.analog.begin(), ground);
    const auto prefixed = analysis::selectPhaseChannels(prefixed_cfg);
    CHECK_TRUE(prefixed.complete());
    CHECK_TRUE(prefixed.index[0].value_or(99) == 1);
    CHECK_TRUE(prefixed.index[1].value_or(99) == 2);
    CHECK_TRUE(prefixed.index[2].value_or(99) == 3);

    CfgInfo ambiguous_cfg = record.cfg;
    ambiguous_cfg.analog[0].name = "IA_IB";
    const PhaseSelection ambiguous =
        analysis::selectPhaseChannels(ambiguous_cfg);
    CHECK_TRUE(ambiguous.ambiguous.size() == 1);
    CHECK_TRUE(!ambiguous.has('A'));
    CHECK_TRUE(ambiguous.has('B'));
    CHECK_TRUE(ambiguous.has('C'));
    CHECK_TRUE(ambiguous.candidates[0].empty());
    CHECK_TRUE(!ambiguous.complete());
    const std::vector<Diagnostic> ambiguous_diagnostics =
        analysis::phaseChannelDiagnostics(ambiguous_cfg, ambiguous, true);
    CHECK_TRUE(hasCode(ambiguous_diagnostics, "cfg.phase_ambiguous"));

    CfgInfo spaced_cfg = record.cfg;
    spaced_cfg.analog[0].name = "I A";
    const PhaseSelection spaced = analysis::selectPhaseChannels(spaced_cfg);
    CHECK_TRUE(spaced.has('A'));

    CfgInfo noise_cfg = record.cfg;
    noise_cfg.analog[0].name = "XIA";
    const PhaseSelection noise = analysis::selectPhaseChannels(noise_cfg);
    CHECK_TRUE(!noise.has('A'));

    CfgInfo missing_cfg = record.cfg;
    missing_cfg.analog[2].name = "VA";
    const PhaseSelection missing =
        analysis::selectPhaseChannels(missing_cfg);
    CHECK_TRUE(!missing.complete());
    CHECK_TRUE(missing.inOrder().size() == 2);
    CHECK_TRUE(missing.prefix().size() == 2);
    CHECK_TRUE(hasCode(
        analysis::phaseChannelDiagnostics(missing_cfg, missing, true),
        "cfg.phase_missing"));

                                                                          
    CfgInfo b_only = record.cfg;
    b_only.analog[0].name = "VA";
    b_only.analog[1].name = "IB";
    b_only.analog[2].name = "VC";
    const PhaseSelection partial = analysis::selectPhaseChannels(b_only);
    CHECK_TRUE(partial.has('B'));
    CHECK_TRUE(partial.prefix().empty());
}

                                                                              
                                               
                                                                              

void testUnitsAndSkew() {
    CHECK_NEAR(analysis::unitFactorToAmps("kA").value_or(0.0), 1000.0, 0.0);
    CHECK_NEAR(analysis::unitFactorToAmps("mA").value_or(0.0), 0.001, 1e-15);
    CHECK_NEAR(analysis::unitFactorToAmps(" A ").value_or(0.0), 1.0, 0.0);
    CHECK_TRUE(!analysis::unitFactorToAmps("V").has_value());
    CHECK_TRUE(!analysis::unitFactorToAmps("").has_value());

    CHECK_TRUE(analysis::labelUnit("IA (kA)") == "kA");
    CHECK_TRUE(analysis::labelUnit("IA (A) (kA)") == "kA");
    CHECK_TRUE(analysis::labelUnit("IA") == "");
    CHECK_TRUE(analysis::normalizeUnit("kKV") == "kv");
    CHECK_TRUE(analysis::normalizeUnit(" mmvar ") == "mvar");
    CHECK_TRUE(analysis::unitFamily("MVA").value_or("") == "potencia aparente");
    CHECK_TRUE(!analysis::unitFamily("").has_value());

    ComtradeRecord record = makeRecord(40, 1000.0, balancedCurrents);
    CfgInfo cfg = record.cfg;
    cfg.analog[0].name = "IA (kA)";
    cfg.analog[0].unit = "A";
    cfg.analog[1].name = "IB (kA)";
    cfg.analog[1].unit = "A";
    const std::vector<Diagnostic> prefix_warnings =
        analysis::unitDiagnostics(cfg);
    CHECK_TRUE(prefix_warnings.size() == 1);
    if (!prefix_warnings.empty()) {
        CHECK_STRING_CONTAINS(prefix_warnings.front().message, "prefijo");
        CHECK_STRING_CONTAINS(prefix_warnings.front().message, "2 canal");
    }
    CfgInfo family_cfg = record.cfg;
    family_cfg.analog[0].name = "IA (A)";
    family_cfg.analog[0].unit = "V";
    const std::vector<Diagnostic> family_warnings =
        analysis::unitDiagnostics(family_cfg);
    CHECK_TRUE(family_warnings.size() == 1);
    if (!family_warnings.empty()) {
        CHECK_STRING_CONTAINS(family_warnings.front().message, "unidades");
    }

    CHECK_TRUE(analysis::skewDiagnostics(record.cfg).empty());
    record.cfg.analog[0].skew_us = 25.0;
    const std::vector<Diagnostic> skew = analysis::skewDiagnostics(record.cfg);
    CHECK_TRUE(skew.size() == 1);
    if (!skew.empty()) {
        CHECK_STRING_CONTAINS(skew.front().message, "skew");
        CHECK_STRING_CONTAINS(skew.front().message, "no aplica");
    }

                                                              
    CfgInfo ps_cfg = record.cfg;
    ps_cfg.analog[0].ps = "P";
    ps_cfg.analog[1].ps = "S";
    ps_cfg.analog[2].ps = "S";
    const PhaseSelection selection = analysis::selectPhaseChannels(ps_cfg);
    CHECK_TRUE(hasCode(analysis::phaseMeasurementDiagnostics(ps_cfg, selection),
                       "cfg.phase_ps_mismatch"));
}

                                                                              
                     
                                                                              

void testFaultDistance() {
    CHECK_NEAR(analysis::parseFaultDistanceKm("12,5 km").value_or(-1.0), 12.5,
               1e-12);
    CHECK_NEAR(analysis::parseFaultDistanceKm("1500 m").value_or(-1.0), 1.5,
               1e-12);
    CHECK_NEAR(analysis::parseFaultDistanceKm("2.5 KM").value_or(-1.0), 2.5,
               1e-12);
    CHECK_NEAR(analysis::parseFaultDistanceKm("fault distance: 1,2 km")
                   .value_or(-1.0),
               1.2, 1e-12);
    CHECK_NEAR(analysis::parseFaultDistanceKm("2 kilometre").value_or(-1.0), 2.0,
               1e-12);
    CHECK_TRUE(!analysis::parseFaultDistanceKm("12.5").has_value());
    CHECK_TRUE(!analysis::parseFaultDistanceKm("-3 km").has_value());
    CHECK_TRUE(!analysis::parseFaultDistanceKm("3 kilometros").has_value());
    CHECK_TRUE(!analysis::parseFaultDistanceKm("").has_value());

    const analysis::FaultDistanceResult exceeds =
        analysis::interpretFaultDistance("13 km", 10.0);
    CHECK_TRUE(exceeds.status == FaultDistanceStatus::ExceedsReference);
    CHECK_NEAR(exceeds.distance_km.value_or(-1.0), 13.0, 1e-12);
    CHECK_NEAR(exceeds.ratio.value_or(-1.0), 1.3, 1e-12);
    CHECK_TRUE(hasCode(exceeds.diagnostics, "analysis.distance.exceeds"));

                                                                                
    const analysis::FaultDistanceResult within =
        analysis::interpretFaultDistance("12 km", 10.0);
    CHECK_TRUE(within.status == FaultDistanceStatus::WithinReference);
    CHECK_TRUE(hasCode(within.diagnostics, "analysis.distance.within"));

    const analysis::FaultDistanceResult no_reference =
        analysis::interpretFaultDistance("3 km", 0.0);
    CHECK_TRUE(no_reference.status == FaultDistanceStatus::NoReferenceLength);
    CHECK_TRUE(no_reference.distance_km.has_value());
    CHECK_TRUE(!no_reference.ratio.has_value());
    CHECK_TRUE(hasCode(no_reference.diagnostics,
                       "analysis.distance.no_reference"));

    const analysis::FaultDistanceResult unparsable =
        analysis::interpretFaultDistance("sin unidad", 10.0);
    CHECK_TRUE(unparsable.status == FaultDistanceStatus::NotInterpretable);
    CHECK_TRUE(!unparsable.distance_km.has_value());
    CHECK_TRUE(hasCode(unparsable.diagnostics,
                       "analysis.distance.unparsable"));
}

                                                                              
                         
                                                                              

void testDigitalTransitions() {
    ComtradeRecord record = makeRecord(21, 100.0, flatSignal);
    record.data.digital_channels = 2;
    record.data.digital.assign(21 * 2, 0);
    record.cfg.digital.clear();
    DigitalChannel first;
    first.position = 0;
    first.declared_index = 1;
    first.name = "TRIP";
    record.cfg.digital.push_back(first);
    DigitalChannel second;
    second.position = 1;
    second.declared_index = 2;
    second.name = "CLOSE";
    record.cfg.digital.push_back(second);

    for (std::size_t sample = 5; sample < 21; ++sample) {
        record.data.digital[sample * 2 + 0] = 1;
    }
    for (std::size_t sample = 15; sample < 21; ++sample) {
        record.data.digital[sample * 2 + 1] = 1;
    }

    const auto transitions = analysis::digitalTransitions(record, 0.1, 0.06);
    CHECK_TRUE(transitions.size() == 2);
    if (transitions.size() == 2) {
        CHECK_TRUE(transitions[0].name == "TRIP");
        CHECK_NEAR(transitions[0].relative_to_reference_s, -0.05, 1e-9);
        CHECK_TRUE(transitions[1].name == "CLOSE");
        CHECK_NEAR(transitions[1].relative_to_reference_s, 0.05, 1e-9);
        CHECK_TRUE(transitions[0].state == 1);
    }
    CHECK_TRUE(analysis::digitalTransitions(record, 0.1, 0.02).empty());
    CHECK_TRUE(analysis::digitalTransitions(record, 0.1, 0.049).empty());
}

                                                                              
                     
                                                                              

void testAnalyzeRecordBalanced() {
    ComtradeRecord record = makeRecord(500, 1000.0, balancedCurrents);
    const RecordAnalysis result = analysis::analyzeRecord(record);

    CHECK_TRUE(result.ok);
    CHECK_TRUE(!hasError(result));
    CHECK_TRUE(result.sample_count == 500);
    CHECK_TRUE(result.analog_channels == 3);
    CHECK_NEAR(result.frequency_hz, 50.0, 1e-12);
    CHECK_TRUE(result.time.ok);
    CHECK_TRUE(result.phases.complete());
    CHECK_TRUE(hasCode(result.diagnostics, "analysis.assumptions"));

    CHECK_TRUE(result.event.has_value());
    if (result.event.has_value()) {
        CHECK_NEAR(result.event->time_s, 0.30, 0.015);
    }

    CHECK_TRUE(result.origin == WaveformOrigin::Onset);
    CHECK_TRUE(result.origin_label == "inicio candidato");
    CHECK_TRUE(!result.windows_overlap);

    CHECK_TRUE(result.coherence_rows.size() == 6);
    for (const analysis::WindowCoherenceRow& row : result.coherence_rows) {
        CHECK_NEAR(row.ratio, 1.0, 0.02);
    }
    CHECK_TRUE(result.coherence_errors.empty());

    CHECK_TRUE(result.prefalla_phasors.has_value());
    CHECK_TRUE(result.falla_phasors.has_value());
    if (result.prefalla_phasors.has_value() && result.falla_phasors.has_value()) {
        CHECK_NEAR((*result.prefalla_phasors)[0].magnitude(), 1.0, 0.02);
        CHECK_NEAR((*result.falla_phasors)[0].magnitude(), 5.0, 0.05);
    }
    CHECK_TRUE(result.falla_sequences.has_value());
    CHECK_TRUE(result.ratios.i1_fault_pre.has_value());
    if (result.ratios.i1_fault_pre.has_value()) {
        CHECK_TRUE(*result.ratios.i1_fault_pre > 2.0);
    }
    if (result.ratios.i0_i1.has_value()) {
        CHECK_TRUE(*result.ratios.i0_i1 < 0.01);
    }

    CHECK_TRUE(result.classification.has_value());
    if (result.classification.has_value()) {
        CHECK_TRUE(result.classification->label == "Secuencia positiva dominante");
    }

    CHECK_TRUE(result.harmonics[0].ok);
    CHECK_TRUE(result.harmonics[1].ok);
    CHECK_TRUE(result.harmonics[2].ok);
    if (result.harmonics[0].ok) {
        CHECK_TRUE(result.harmonics[0].cycles == 1);
        CHECK_TRUE(!result.harmonics[0].harmonics_rms.empty());
    }

                                                                  
    CHECK_TRUE(!result.fault_distance.has_value());
}

void testAnalyzeRecordCensoredAndOverlap() {
    ComtradeRecord persistent =
        makeRecord(500, 1000.0, persistentHighCurrents);
    const RecordAnalysis censored = analysis::analyzeRecord(persistent);
    CHECK_TRUE(censored.ok);
    CHECK_TRUE(censored.event.has_value());
    if (censored.event.has_value()) {
        CHECK_TRUE(censored.event->recovery_censored);
        CHECK_TRUE(censored.event->duration_s > 0.15);
    }
    CHECK_TRUE(hasCode(censored.diagnostics,
                       "analysis.event.recovery_censored"));

                                                                         
    ComtradeRecord short_record = makeRecord(25, 1000.0, balancedCurrents);
    const RecordAnalysis overlap = analysis::analyzeRecord(short_record);
    CHECK_TRUE(overlap.ok);
    CHECK_TRUE(overlap.windows_overlap);
    CHECK_TRUE(!overlap.classification.has_value());
    CHECK_TRUE(hasCode(overlap.diagnostics, "analysis.window.overlap"));

                                                                             
                                                        
    ComtradeRecord distorted =
        makeRecord(1000, 2000.0, highFrequencyDistortion);
    const RecordAnalysis gated = analysis::analyzeRecord(distorted);
    CHECK_TRUE(gated.event.has_value());
    CHECK_TRUE(!gated.classification.has_value());
    CHECK_TRUE(hasCode(gated.diagnostics, "analysis.window.fundamental_low"));
}

void testAnalyzeRecordTriggerFallbackAndOptions() {
    ComtradeRecord record = makeRecord(500, 1000.0, balancedCurrents);
    record.cfg.start_time.valid = false;
    record.trigger_offset_s.reset();

    AnalysisOptions options;
    options.cycles = 1;
    options.fault_distance_text = "13 km";
    options.reference_length_km = 10.0;
    const RecordAnalysis result = analysis::analyzeRecord(record, options);

    CHECK_TRUE(result.time.trigger_reference_is_midpoint);
    CHECK_TRUE(result.time.reference_label == "referencia provisional");
    CHECK_TRUE(hasCode(result.diagnostics, "cfg.trigger_unreadable"));
    CHECK_TRUE(result.event.has_value());
    CHECK_TRUE(result.origin == WaveformOrigin::Onset);
    CHECK_TRUE(result.origin_label == "inicio candidato");

    CHECK_TRUE(result.fault_distance.has_value());
    if (result.fault_distance.has_value()) {
        CHECK_TRUE(result.fault_distance->status ==
                   FaultDistanceStatus::ExceedsReference);
        CHECK_NEAR(result.fault_distance->distance_km.value_or(-1.0), 13.0,
                   1e-12);
    }
    CHECK_TRUE(hasCode(result.diagnostics, "analysis.distance.exceeds"));

                                                                      
    ComtradeRecord skewed = makeRecord(500, 1000.0, balancedCurrents);
    skewed.cfg.analog[0].skew_us = 25.0;
    skewed.cfg.analog[0].unit = "kA";
    skewed.cfg.analog[1].unit = "kA";
    skewed.cfg.analog[2].unit = "kA";
    const RecordAnalysis with_skew = analysis::analyzeRecord(skewed);
    CHECK_TRUE(hasCode(with_skew.diagnostics, "cfg.channel_skew"));
    CHECK_TRUE(with_skew.prefalla_phasors.has_value());
    if (with_skew.prefalla_phasors.has_value()) {
                                                                
        CHECK_NEAR((*with_skew.prefalla_phasors)[0].magnitude(), 1.0, 0.02);
    }

                                                                           
    ComtradeRecord no_frequency = makeRecord(120, 1000.0, balancedCurrents);
    no_frequency.cfg.nominal_frequency_hz = 0.0;
    const RecordAnalysis invalid = analysis::analyzeRecord(no_frequency);
    CHECK_TRUE(!invalid.ok);
    CHECK_TRUE(hasCode(invalid.diagnostics, "analysis.invalid_frequency"));

                                                 
    ComtradeRecord no_analog = makeRecord(120, 1000.0, balancedCurrents);
    no_analog.cfg.analog.clear();
    no_analog.data.analog_channels = 0;
    const RecordAnalysis empty = analysis::analyzeRecord(no_analog);
    CHECK_TRUE(!empty.ok);
    CHECK_TRUE(hasCode(empty.diagnostics, "analysis.no_analog_channels"));
}

void testManualPhaseAndWindowOverrides() {
    ComtradeRecord record = makeRecord(500, 1000.0, persistentHighCurrents);
    AnalysisOptions options;
    options.cycles = 2;
    options.phase_indices[0] = 2;
    options.phase_indices[1] = 0;
    options.phase_indices[2] = 1;
    options.prefalla_center_s = 0.12;
    options.falla_center_s = 0.42;
    options.onset_ratio = 1.8;
    const RecordAnalysis result = analysis::analyzeRecord(record, options);

    CHECK_TRUE(result.ok);
    CHECK_TRUE(result.phases.index[0].has_value() &&
               *result.phases.index[0] == 2);
    CHECK_TRUE(result.phases.index[1].has_value() &&
               *result.phases.index[1] == 0);
    CHECK_TRUE(result.phases.index[2].has_value() &&
               *result.phases.index[2] == 1);
    CHECK_TRUE(result.cycles == 2);
    CHECK_NEAR(result.prefalla_center_s, 0.12, 1e-9);
    CHECK_NEAR(result.falla_center_s, 0.42, 1e-9);
    CHECK_TRUE(hasCode(result.diagnostics, "analysis.phase_override"));
    CHECK_TRUE(hasCode(result.diagnostics, "analysis.window.override"));

                                                                           
                
    AnalysisOptions duplicate;
    duplicate.phase_indices[0] = 0;
    duplicate.phase_indices[1] = 0;
    duplicate.phase_indices[2] = 1;
    const RecordAnalysis invalid = analysis::analyzeRecord(record, duplicate);
    CHECK_TRUE(!invalid.ok);
    CHECK_TRUE(hasCode(invalid.diagnostics,
                       "analysis.phase_override_duplicate"));

                                                                              
    AnalysisOptions range;
    range.phase_indices[0] = 0;
    range.phase_indices[1] = 1;
    range.phase_indices[2] = 99;
    const RecordAnalysis out_of_range = analysis::analyzeRecord(record, range);
    CHECK_TRUE(!out_of_range.ok);
    CHECK_TRUE(hasCode(out_of_range.diagnostics,
                       "analysis.phase_override_range"));
}

                                                                              
                                                                
                                                                              

void testVoltageChannels() {
    auto makeCfg = [](const std::vector<std::string>& names) {
        const std::size_t count = names.size();
        std::vector<std::string> units(count, "kV");
        return makeChannelRecord(
                   1, 1000.0, names, units,
                   [count](double) { return std::vector<double>(count, 0.0); })
            .cfg;
    };

    const CfgInfo phase_to_neutral =
        makeCfg({"IA", "IB", "IC", "VAN", "VBN", "VCN"});
    const PhaseSelection van = analysis::selectVoltageChannels(phase_to_neutral);
    CHECK_TRUE(van.complete());
    CHECK_TRUE(van.index[0].has_value() && *van.index[0] == 3);
    CHECK_TRUE(van.index[1].has_value() && *van.index[1] == 4);
    CHECK_TRUE(van.index[2].has_value() && *van.index[2] == 5);
    CHECK_TRUE(van.ambiguous.empty());
    CHECK_TRUE(analysis::voltageChannelDiagnostics(phase_to_neutral, van, true)
                   .empty());

    const CfgInfo phase_to_phase = makeCfg({"VA", "VB", "VC"});
    const PhaseSelection va = analysis::selectVoltageChannels(phase_to_phase);
    CHECK_TRUE(va.complete());
    CHECK_TRUE(va.index[0].has_value() && *va.index[0] == 0);

    const CfgInfo u_names = makeCfg({"UA", "UB", "UC"});
    const PhaseSelection ua = analysis::selectVoltageChannels(u_names);
    CHECK_TRUE(ua.complete());

    const CfgInfo spaced = makeCfg({"V A N", "V B N", "V C N"});
    const PhaseSelection spaced_selection =
        analysis::selectVoltageChannels(spaced);
    CHECK_TRUE(spaced_selection.complete());

                                                                 
    const CfgInfo vaux = makeCfg({"VAUX", "VB", "VC"});
    const PhaseSelection vaux_selection =
        analysis::selectVoltageChannels(vaux);
    CHECK_TRUE(!vaux_selection.has('A'));
    CHECK_TRUE(vaux_selection.has('B'));
    CHECK_TRUE(vaux_selection.has('C'));
    CHECK_TRUE(!vaux_selection.complete());

                                                                            
    const CfgInfo vab = makeCfg({"VAB", "VB", "VC"});
    const PhaseSelection vab_selection = analysis::selectVoltageChannels(vab);
    CHECK_TRUE(vab_selection.ambiguous.size() == 1);
    CHECK_TRUE(!vab_selection.has('A'));
    CHECK_TRUE(vab_selection.has('B'));
    CHECK_TRUE(vab_selection.has('C'));
    CHECK_TRUE(!vab_selection.complete());
    CHECK_TRUE(hasCode(analysis::voltageChannelDiagnostics(vab, vab_selection,
                                                          true),
                       "cfg.voltage_ambiguous"));

                                                               
    const CfgInfo currents = makeCfg({"IA", "IB", "IC"});
    const PhaseSelection no_voltage =
        analysis::selectVoltageChannels(currents);
    CHECK_TRUE(no_voltage.inOrder().empty());
    CHECK_TRUE(hasCode(analysis::voltageChannelDiagnostics(
                           currents, no_voltage, true),
                       "cfg.voltage_missing"));

    const CfgInfo duplicated = makeCfg({"VAN", "VAN (aux)", "VBN", "VCN"});
    const PhaseSelection multiple =
        analysis::selectVoltageChannels(duplicated);
    CHECK_TRUE(multiple.candidates[0].size() == 2);
    CHECK_TRUE(multiple.index[0].has_value());
    CHECK_TRUE(hasCode(analysis::voltageChannelDiagnostics(
                           duplicated, multiple, true),
                       "cfg.voltage_multiple"));
}

void testVoltageAnalysis() {
    const std::vector<std::string> names = {"IA",      "IB",      "IC",
                                            "VAN",     "VBN",     "VCN"};
    const std::vector<std::string> units = {"A", "A", "A",
                                            "kV", "kV", "kV"};
    const ComtradeRecord record = makeChannelRecord(
        500, 1000.0, names, units, mixedCurrentVoltageSignal);

    const RecordAnalysis result = analysis::analyzeRecord(record);
    CHECK_TRUE(result.ok);
    CHECK_TRUE(result.phases.complete());
    CHECK_TRUE(result.voltage.selection.complete());
    CHECK_TRUE(result.voltage.available);
    CHECK_TRUE(result.voltage.units_consistent);
    CHECK_TRUE(result.voltage.unit == "kv");
    CHECK_TRUE(result.voltage.prefalla_phasors.has_value());
    CHECK_TRUE(result.voltage.falla_phasors.has_value());
    CHECK_TRUE(result.voltage.prefalla_sequences.has_value());
    CHECK_TRUE(result.voltage.falla_sequences.has_value());
    if (result.voltage.prefalla_phasors.has_value() &&
        result.voltage.falla_phasors.has_value()) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            CHECK_NEAR((*result.voltage.prefalla_phasors)[slot].magnitude(),
                       10.0, 0.02);
            CHECK_NEAR((*result.voltage.falla_phasors)[slot].magnitude(), 6.0,
                       0.02);
            CHECK_TRUE(result.voltage.change_pct[slot].has_value());
            if (result.voltage.change_pct[slot].has_value()) {
                CHECK_NEAR(*result.voltage.change_pct[slot], -40.0, 0.5);
            }
        }
    }
    if (result.voltage.prefalla_sequences.has_value()) {
        CHECK_NEAR(std::abs(result.voltage.prefalla_sequences->positive), 10.0,
                   0.05);
        CHECK_NEAR(std::abs(result.voltage.prefalla_sequences->zero), 0.0, 0.02);
    }
    CHECK_TRUE(!hasCode(result.diagnostics, "cfg.voltage_missing"));

                                                                         
                                                                             
    for (std::size_t slot = 0; slot < 3; ++slot) {
        CHECK_TRUE(result.spectra[slot].has_value());
        CHECK_TRUE(result.saturation[slot].has_value());
        if (result.spectra[slot].has_value()) {
            CHECK_TRUE(result.spectra[slot]->ok);
            CHECK_TRUE(result.spectra[slot]->unit == "A");
        }
    }
    CHECK_TRUE(!hasCode(result.diagnostics, "analysis.fft.methodology"));
    CHECK_TRUE(!hasCode(result.diagnostics,
                        "analysis.saturation.methodology"));

                                                                             
    AnalysisOptions manual;
    manual.voltage_indices[0] = 5;
    manual.voltage_indices[1] = 4;
    manual.voltage_indices[2] = 3;
    const RecordAnalysis overridden =
        analysis::analyzeRecord(record, manual);
    CHECK_TRUE(overridden.ok);
    CHECK_TRUE(overridden.voltage.selection.complete());
    CHECK_TRUE(overridden.voltage.selection.index[0].has_value() &&
               *overridden.voltage.selection.index[0] == 5);
    CHECK_TRUE(overridden.voltage.selection.index[1].has_value() &&
               *overridden.voltage.selection.index[1] == 4);
    CHECK_TRUE(overridden.voltage.selection.index[2].has_value() &&
               *overridden.voltage.selection.index[2] == 3);
    CHECK_TRUE(overridden.voltage.available);
    CHECK_TRUE(hasCode(overridden.diagnostics, "analysis.voltage_override"));
    CHECK_TRUE(overridden.voltage.change_pct[0].has_value());

                                                                                 
    AnalysisOptions out_of_range;
    out_of_range.voltage_indices[0] = 99;
    const RecordAnalysis bad_range =
        analysis::analyzeRecord(record, out_of_range);
    CHECK_TRUE(bad_range.ok);
    CHECK_TRUE(!bad_range.voltage.available);
    CHECK_TRUE(bad_range.voltage.selection.inOrder().empty());
    CHECK_TRUE(hasCode(bad_range.diagnostics,
                       "analysis.voltage_override_range"));
    CHECK_TRUE(bad_range.falla_phasors.has_value());

                                                       
    AnalysisOptions duplicate;
    duplicate.voltage_indices[0] = 3;
    duplicate.voltage_indices[1] = 3;
    const RecordAnalysis bad_duplicate =
        analysis::analyzeRecord(record, duplicate);
    CHECK_TRUE(bad_duplicate.ok);
    CHECK_TRUE(!bad_duplicate.voltage.available);
    CHECK_TRUE(hasCode(bad_duplicate.diagnostics,
                       "analysis.voltage_override_duplicate"));
    CHECK_TRUE(bad_duplicate.falla_phasors.has_value());

                                                                        
    ComtradeRecord partial = record;
    partial.cfg.analog[5].name = "VX";
    const RecordAnalysis partial_result = analysis::analyzeRecord(partial);
    CHECK_TRUE(partial_result.ok);
    CHECK_TRUE(!partial_result.voltage.available);
    CHECK_TRUE(!partial_result.voltage.selection.complete());
    CHECK_TRUE(hasCode(partial_result.diagnostics, "cfg.voltage_missing"));
    CHECK_TRUE(partial_result.falla_phasors.has_value());

                                                                               
                                                 
    AnalysisOptions partial_manual;
    partial_manual.voltage_indices[0] = 3;
    const RecordAnalysis manual_partial =
        analysis::analyzeRecord(record, partial_manual);
    CHECK_TRUE(manual_partial.ok);
    CHECK_TRUE(!manual_partial.voltage.available);
    CHECK_TRUE(manual_partial.voltage.selection.has('A'));
    CHECK_TRUE(!manual_partial.voltage.selection.complete());
    CHECK_TRUE(hasCode(manual_partial.voltage.diagnostics,
                       "analysis.voltage.partial"));
    CHECK_TRUE(manual_partial.falla_phasors.has_value());

                                                                      
    ComtradeRecord mismatched = record;
    mismatched.cfg.analog[4].unit = "V";
    const RecordAnalysis mismatch_result = analysis::analyzeRecord(mismatched);
    CHECK_TRUE(mismatch_result.ok);
    CHECK_TRUE(mismatch_result.voltage.selection.complete());
    CHECK_TRUE(!mismatch_result.voltage.units_consistent);
    CHECK_TRUE(mismatch_result.voltage.unit.empty());
    CHECK_TRUE(hasCode(mismatch_result.diagnostics,
                       "cfg.voltage_unit_mismatch"));
    CHECK_TRUE(mismatch_result.voltage.prefalla_phasors.has_value());
}

                                                                              
                                                         
                                                                              

void testFftSpectrum() {
    const std::vector<std::string> names = {"IA", "IB", "IC"};
    const std::vector<std::string> units = {"A", "A", "A"};
    const ComtradeRecord record = makeChannelRecord(
        3200, 3200.0, names, units, knownHarmonicSignal);

    const analysis::SpectrumResult spectrum = analysis::fftSpectrum(
        record.data, 0, "A", 50.0, 0.5, 16, 5, analysis::kFftMaxPoints);
    CHECK_TRUE(spectrum.ok);
    CHECK_TRUE(spectrum.fft_points == 1024);
    CHECK_TRUE(spectrum.bin_frequency_hz.size() == 513);
    CHECK_TRUE(spectrum.rms.size() == 513);
    CHECK_TRUE(spectrum.rms[0] == 0.0);
    CHECK_NEAR(spectrum.bin_frequency_hz[1], 3.125, 1e-9);
    CHECK_TRUE(spectrum.bin_frequency_hz.back() <= 1600.0 + 1e-9);
    CHECK_TRUE(spectrum.harmonics_rms.size() == 5);
    CHECK_TRUE(spectrum.supported_order == 5);
    CHECK_TRUE(spectrum.thd_pct.has_value());
    CHECK_TRUE(spectrum.h2_h1_pct.has_value());
    if (spectrum.harmonics_rms.size() >= 2) {
        CHECK_NEAR(spectrum.harmonics_rms[0], 10.0, 1e-4);
        CHECK_NEAR(spectrum.harmonics_rms[1], 2.0, 1e-4);
        CHECK_NEAR(spectrum.harmonics_rms[2], 0.0, 1e-3);
    }
    if (spectrum.thd_pct.has_value()) {
        CHECK_NEAR(*spectrum.thd_pct, 20.0, 1e-3);
    }
    if (spectrum.h2_h1_pct.has_value()) {
        CHECK_NEAR(*spectrum.h2_h1_pct, 20.0, 1e-3);
    }
    CHECK_TRUE(hasCode(spectrum.diagnostics, "analysis.fft.methodology"));

                                                     
    const analysis::SpectrumResult outside = analysis::fftSpectrum(
        record.data, 0, "A", 50.0, 0.01, 16, 5, analysis::kFftMaxPoints);
    CHECK_TRUE(!outside.ok);
    CHECK_TRUE(hasCode(outside.diagnostics, "analysis.fft.out_of_record"));

                                                                        
    const ComtradeRecord coarse = makeChannelRecord(
        100, 1000.0, {"IA"}, {"A"}, pureSineSignal);
    const analysis::SpectrumResult padded = analysis::fftSpectrum(
        coarse.data, 0, "A", 50.0, 0.05, 1, 5, analysis::kFftMaxPoints);
    CHECK_TRUE(padded.ok);
    CHECK_TRUE(padded.fft_points == 32);
    CHECK_TRUE(padded.bin_frequency_hz.size() == 17);
    CHECK_NEAR(padded.bin_frequency_hz[1], 31.25, 1e-9);
    CHECK_NEAR(padded.bin_frequency_hz.back(), 500.0, 1e-9);

                                                     
    const ComtradeRecord sparse = makeChannelRecord(
        200, 100.0, {"IA"}, {"A"}, pureSineSignal);
    const analysis::SpectrumResult few = analysis::fftSpectrum(
        sparse.data, 0, "A", 50.0, 1.0, 1, 5, analysis::kFftMaxPoints);
    CHECK_TRUE(!few.ok);
    CHECK_TRUE(hasCode(few.diagnostics, "analysis.fft.too_few_samples"));

                                                                                
    const ComtradeRecord low_rate = makeChannelRecord(
        400, 80.0, {"IA"}, {"A"}, pureSineSignal);
    const analysis::SpectrumResult nyquist = analysis::fftSpectrum(
        low_rate.data, 0, "A", 50.0, 2.5, 20, 15, analysis::kFftMaxPoints);
    CHECK_TRUE(nyquist.ok);
    CHECK_TRUE(nyquist.supported_order == 0);
    CHECK_TRUE(!nyquist.thd_pct.has_value());
    CHECK_TRUE(!nyquist.h2_h1_pct.has_value());
    for (double value : nyquist.harmonics_rms) {
        CHECK_TRUE(value == 0.0);
    }
    CHECK_TRUE(hasCode(nyquist.diagnostics, "analysis.fft.nyquist"));

                                                                       
    const analysis::SpectrumResult limited = analysis::fftSpectrum(
        record.data, 0, "A", 50.0, 0.5, 16, 5, 128);
    CHECK_TRUE(limited.ok);
    CHECK_TRUE(limited.fft_points <= 128);
    CHECK_TRUE(limited.fft_points == 128);
    CHECK_TRUE(hasCode(limited.diagnostics,
                       "analysis.fft.reduced_resolution"));
    CHECK_TRUE(limited.supported_order >= 2);
    if (limited.harmonics_rms.size() >= 2) {
        CHECK_NEAR(limited.harmonics_rms[0], 10.0, 1e-2);
        CHECK_NEAR(limited.harmonics_rms[1], 2.0, 1e-2);
    }
}

                                                                              
                                                             
                                                                              

void testSaturationIndicator() {
    const ComtradeRecord clipped = makeChannelRecord(
        2000, 2000.0, {"IA"}, {"A"}, clippedSineSignal);
    const analysis::SaturationIndicator sat =
        analysis::shoulderSaturation(clipped.data, 0, 50.0, 0.5, 10, nullptr);
    CHECK_TRUE(sat.available);
    CHECK_TRUE(sat.shoulder_hits >= analysis::kSaturationShoulderHits);
    CHECK_TRUE(sat.possible);
    CHECK_TRUE(sat.shoulder_required_hits == analysis::kSaturationShoulderHits);
    CHECK_TRUE(sat.curvature_threshold == analysis::kSaturationCurvatureRatio);
    CHECK_TRUE(sat.high_fraction == analysis::kSaturationHighFraction);
    CHECK_TRUE(sat.h2_threshold_pct == analysis::kSaturationH2ThresholdPct);
    CHECK_NEAR(sat.shoulder_peak, 0.60 * 10.0 * kSqrt2, 1e-6);
    CHECK_TRUE(!sat.h2_h1_pct.has_value());
    CHECK_TRUE(sat.flat_top_pct.has_value());
    CHECK_TRUE(sat.flat_top_elevated);
    CHECK_TRUE(sat.longest_flat_top_ms > 1.0);
    CHECK_STRING_CONTAINS(sat.methodology, "heurístico");
    CHECK_STRING_CONTAINS(sat.methodology, "98 %");
    CHECK_STRING_CONTAINS(sat.methodology, "12.5 %");
    CHECK_TRUE(hasCode(sat.diagnostics, "analysis.saturation.possible"));
    CHECK_TRUE(hasCode(sat.diagnostics, "analysis.saturation.flat_top"));

                                                       
    analysis::HarmonicFit elevated;
    elevated.ok = true;
    elevated.cycles = 10;
    elevated.supported_order = 2;
    elevated.harmonics_rms = {10.0, 2.0};
    elevated.h2_h1_pct = 20.0;
    const analysis::SaturationIndicator with_h2 =
        analysis::shoulderSaturation(clipped.data, 0, 50.0, 0.5, 10, &elevated);
    CHECK_TRUE(with_h2.available);
    CHECK_TRUE(with_h2.h2_h1_pct.has_value());
    if (with_h2.h2_h1_pct.has_value()) {
        CHECK_NEAR(*with_h2.h2_h1_pct, 20.0, 1e-12);
    }
    CHECK_TRUE(with_h2.h2_elevated);
    CHECK_TRUE(with_h2.possible);

                                                                                 
    const ComtradeRecord pure = makeChannelRecord(
        2000, 2000.0, {"IA"}, {"A"}, pureSineSignal);
    const analysis::SaturationIndicator h2_only =
        analysis::shoulderSaturation(pure.data, 0, 50.0, 0.5, 10, &elevated);
    CHECK_TRUE(h2_only.available);
    CHECK_TRUE(h2_only.shoulder_hits < analysis::kSaturationShoulderHits);
    CHECK_TRUE(h2_only.h2_elevated);
    CHECK_TRUE(h2_only.possible);

                                                                            
    analysis::HarmonicFit low;
    low.ok = true;
    low.cycles = 10;
    low.supported_order = 2;
    low.harmonics_rms = {10.0, 1.0};
    low.h2_h1_pct = 10.0;
    const analysis::SaturationIndicator quiet =
        analysis::shoulderSaturation(pure.data, 0, 50.0, 0.5, 10, &low);
    CHECK_TRUE(quiet.available);
    CHECK_TRUE(!quiet.h2_elevated);
    CHECK_TRUE(quiet.shoulder_hits < analysis::kSaturationShoulderHits);
    CHECK_TRUE(!quiet.possible);
    CHECK_TRUE(quiet.flat_top_pct.has_value());
    if (quiet.flat_top_pct.has_value()) {
        CHECK_NEAR(*quiet.flat_top_pct, 0.0, 1e-12);
    }
    CHECK_TRUE(!quiet.flat_top_elevated);
    CHECK_NEAR(quiet.longest_flat_top_ms, 0.0, 1e-12);

                                                                                   
    const ComtradeRecord short_record = makeChannelRecord(
        100, 1000.0, {"IA"}, {"A"}, pureSineSignal);
    const analysis::SaturationIndicator unavailable =
        analysis::shoulderSaturation(short_record.data, 0, 50.0, 0.05, 1,
                                     nullptr);
    CHECK_TRUE(!unavailable.available);
    CHECK_TRUE(!unavailable.possible);
    CHECK_TRUE(!unavailable.flat_top_pct.has_value());
    CHECK_TRUE(!unavailable.flat_top_elevated);
    CHECK_TRUE(hasCode(unavailable.diagnostics,
                       "analysis.saturation.too_few_samples"));
}

void testFlatTopIndicator() {
    analysis::HarmonicFit low_h2;
    low_h2.ok = true;
    low_h2.cycles = 10;
    low_h2.supported_order = 2;
    low_h2.harmonics_rms = {10.0, 1.0};
    low_h2.h2_h1_pct = 10.0;

                                                                        
    for (double rate : {1000.0, 2000.0, 5000.0}) {
        const ComtradeRecord pure = makeChannelRecord(
            static_cast<std::size_t>(rate), rate, {"IA"}, {"A"},
            pureSineSignal);
        const analysis::SaturationIndicator sat = analysis::shoulderSaturation(
            pure.data, 0, 50.0, 0.5, 10, &low_h2);
        CHECK_TRUE(sat.available);
        CHECK_TRUE(sat.flat_top_pct.has_value());
        if (sat.flat_top_pct.has_value()) {
            CHECK_NEAR(*sat.flat_top_pct, 0.0, 1e-12);
        }
        CHECK_NEAR(sat.longest_flat_top_ms, 0.0, 1e-12);
        CHECK_TRUE(!sat.flat_top_elevated);
        CHECK_TRUE(!sat.possible);
    }

                                                                            
    for (double rate : {1000.0, 2000.0, 5000.0}) {
        const ComtradeRecord clipped = makeChannelRecord(
            static_cast<std::size_t>(rate), rate, {"IA"}, {"A"},
            clippedSineSignal);
        const analysis::SaturationIndicator sat = analysis::shoulderSaturation(
            clipped.data, 0, 50.0, 0.5, 10, &low_h2);
        CHECK_TRUE(sat.available);
        CHECK_TRUE(sat.flat_top_pct.has_value());
        if (sat.flat_top_pct.has_value()) {
            CHECK_TRUE(*sat.flat_top_pct > 1.0);
        }
        CHECK_TRUE(sat.longest_flat_top_ms > 1.0);
        CHECK_TRUE(sat.flat_top_elevated);
        CHECK_TRUE(sat.possible);
        CHECK_TRUE(hasCode(sat.diagnostics, "analysis.saturation.flat_top"));
    }

                                                                         
    const auto kilo_signal = [](double time) {
        const double peak = 10.0 * kSqrt2;
        const double limit = 0.60 * peak;
        const double value = std::max(
            -limit, std::min(limit, peak * std::sin(kTwoPi * 50.0 * time)));
        return std::vector<double>{value * 1.0e-3};
    };
    const ComtradeRecord amperes = makeChannelRecord(
        2000, 2000.0, {"IA"}, {"A"}, clippedSineSignal);
    const ComtradeRecord kiloamperes = makeChannelRecord(
        2000, 2000.0, {"IA"}, {"kA"}, kilo_signal);
    const analysis::SaturationIndicator amps = analysis::shoulderSaturation(
        amperes.data, 0, 50.0, 0.5, 10, nullptr);
    const analysis::SaturationIndicator kamps = analysis::shoulderSaturation(
        kiloamperes.data, 0, 50.0, 0.5, 10, nullptr);
    CHECK_TRUE(amps.flat_top_pct.has_value() && kamps.flat_top_pct.has_value());
    if (amps.flat_top_pct.has_value() && kamps.flat_top_pct.has_value()) {
        CHECK_NEAR(*kamps.flat_top_pct, *amps.flat_top_pct, 1e-9);
    }
    CHECK_NEAR(kamps.longest_flat_top_ms, amps.longest_flat_top_ms, 1e-9);
    CHECK_TRUE(kamps.flat_top_elevated == amps.flat_top_elevated);

                                                                        
    const ComtradeRecord zero = makeChannelRecord(
        2000, 2000.0, {"IA"}, {"A"},
        [](double) { return std::vector<double>{0.0}; });
    const analysis::SaturationIndicator zero_sat =
        analysis::shoulderSaturation(zero.data, 0, 50.0, 0.5, 10, nullptr);
    CHECK_TRUE(!zero_sat.available);
    CHECK_TRUE(!zero_sat.flat_top_pct.has_value());
    CHECK_TRUE(!zero_sat.flat_top_elevated);

    const ComtradeRecord dc = makeChannelRecord(
        2000, 2000.0, {"IA"}, {"A"},
        [](double) { return std::vector<double>{5.0}; });
    const analysis::SaturationIndicator dc_sat =
        analysis::shoulderSaturation(dc.data, 0, 50.0, 0.5, 10, nullptr);
    CHECK_TRUE(!dc_sat.available);
    CHECK_TRUE(!dc_sat.flat_top_pct.has_value());
    CHECK_TRUE(!dc_sat.flat_top_elevated);

                                                                        
    ComtradeRecord nonfinite = amperes;
    nonfinite.data.analog[1000] =
        std::numeric_limits<double>::quiet_NaN();
    nonfinite.data.analog[1001] = std::numeric_limits<double>::infinity();
    const analysis::SaturationIndicator bad =
        analysis::shoulderSaturation(nonfinite.data, 0, 50.0, 0.5, 10,
                                     nullptr);
    CHECK_TRUE(!bad.available);
    CHECK_TRUE(!bad.flat_top_pct.has_value());
    CHECK_TRUE(!bad.flat_top_elevated);

                                                                              
    const ComtradeRecord coarse = makeChannelRecord(
        200, 200.0, {"IA"}, {"A"}, clippedSineSignal);
    const analysis::SaturationIndicator coarse_sat =
        analysis::shoulderSaturation(coarse.data, 0, 50.0, 0.5, 10, nullptr);
    CHECK_TRUE(coarse_sat.available);
    CHECK_TRUE(!coarse_sat.flat_top_pct.has_value());
    CHECK_TRUE(!coarse_sat.flat_top_elevated);
    CHECK_TRUE(hasCode(coarse_sat.diagnostics,
                       "analysis.saturation.flat_top_unavailable"));

                                                                         
    const auto noisy = [](double time) {
        std::uint64_t x =
            static_cast<std::uint64_t>(std::llround(time * 1.0e9));
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        const double unit =
            static_cast<double>(x >> 11) / 9007199254740992.0;
        const double fundamental = 10.0 * kSqrt2 * std::sin(kTwoPi * 50.0 * time);
        return std::vector<double>{fundamental + 3.0 * (2.0 * unit - 1.0)};
    };
    const ComtradeRecord noise_record = makeChannelRecord(
        2000, 2000.0, {"IA"}, {"A"}, noisy);
    const analysis::SaturationIndicator noise_sat =
        analysis::shoulderSaturation(noise_record.data, 0, 50.0, 0.5, 10,
                                     nullptr);
    CHECK_TRUE(noise_sat.available);
    CHECK_TRUE(!noise_sat.flat_top_elevated);
}

void run(const char* name, void (*test)()) {
    const int before = g_failures;
    test();
    std::printf("[%s] %s\n", g_failures == before ? "OK" : "FALLA", name);
}

}              

int main() {
    run("alineacion_temporal", testTimestampAlignment);
    run("fasores_y_rms", testPhasorsAndAcRms);
    run("secuencias_y_clasificacion", testSequencesAndClassification);
    run("armonicos_y_nyquist", testHarmonicsAndNyquist);
    run("onset_y_ventanas", testOnsetAndWindows);
    run("coherencia_ventanas", testWindowCoherenceGates);
    run("asignacion_fases", testPhaseChannelAssignment);
    run("unidades_y_skew", testUnitsAndSkew);
    run("distancia_falla", testFaultDistance);
    run("transiciones_digitales", testDigitalTransitions);
    run("analisis_registro_balanceado", testAnalyzeRecordBalanced);
    run("analisis_censurado_y_solape", testAnalyzeRecordCensoredAndOverlap);
    run("analisis_referencia_y_opciones",
        testAnalyzeRecordTriggerFallbackAndOptions);
    run("seleccion_manual_fases_ventanas",
        testManualPhaseAndWindowOverrides);
    run("canales_tension", testVoltageChannels);
    run("analisis_tension", testVoltageAnalysis);
    run("espectro_fft", testFftSpectrum);
    run("saturacion_heuristica", testSaturationIndicator);
    run("techo_plano_saturacion", testFlatTopIndicator);

    std::printf("\n%d verificaciones, %d fallas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
