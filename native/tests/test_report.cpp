                                                                              
  
                                              
                                                                            
                                                                                     
                                                           
                                                           
                                                               
                                                                          
                                                                              
                                                                               
  
                       
                                                                                    
                                                                                    
                           

#include "analizador/analysis.hpp"
#include "analizador/comtrade.hpp"
#include "analizador/model.hpp"
#include "analizador/report.hpp"

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::cerr << "FALLO " << __FILE__ << ":" << __LINE__ << "  " #condition "\n"; \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

using analizador::AnalogChannel;
using analizador::ComtradeRecord;
using analizador::DataFileType;
using analizador::DateTime;
using analizador::SampleData;
using analizador::SamplingRate;
namespace analysis = analizador::analysis;
namespace report = analizador::report;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTwoPi = 2.0 * kPi;

ComtradeRecord makeRecord(double amplitude_after, double first_time = 0.0) {
    ComtradeRecord record;
    auto& cfg = record.cfg;
    cfg.station_name = "ESTACION";
    cfg.device_id = "IED";
    cfg.revision = "1999";
    cfg.format_year = 1999;
    cfg.nominal_frequency_hz = 50.0;
    cfg.data_type = DataFileType::Ascii;
    cfg.time_multiplier = 1.0;
    cfg.declared_total_channels = 3;
    cfg.rates.push_back(SamplingRate{1000.0, 500});
    cfg.start_time = DateTime{2026, 1, 1, 0, 0, 0, 0, true};
    cfg.trigger_time = DateTime{2026, 1, 1, 0, 0, 0, 100000, true};
    cfg.start_time_text = "01/01/2026,00:00:00.000000";
    cfg.trigger_time_text = "01/01/2026,00:00:00.100000";
    const char* names[3] = {"IA", "IB", "IC"};
    for (std::size_t index = 0; index < 3; ++index) {
        AnalogChannel channel;
        channel.position = index;
        channel.declared_index = static_cast<std::uint32_t>(index + 1);
        channel.name = names[index];
        channel.unit = "A";
        channel.a = 1.0;
        channel.b = 0.0;
        channel.min_value = -32768.0;
        channel.max_value = 32767.0;
        channel.primary = 1000.0;
        channel.secondary = 5.0;
        channel.ps = "P";
        cfg.analog.push_back(channel);
    }
    auto& data = record.data;
    data.analog_channels = 3;
    data.digital_channels = 0;
    data.sample_numbers.reserve(500);
    data.dat_timestamp_us.reserve(500);
    data.times_s.reserve(500);
    data.analog.reserve(1500);
    const double phases[3] = {0.0, -kTwoPi / 3.0, kTwoPi / 3.0};
    for (std::size_t sample = 0; sample < 500; ++sample) {
        const double time = first_time + static_cast<double>(sample) / 1000.0;
        const double amplitude = time < 0.30 ? 1.0 : amplitude_after;
        data.sample_numbers.push_back(static_cast<std::uint32_t>(sample + 1));
        data.dat_timestamp_us.push_back(time * 1e6);
        data.times_s.push_back(time);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            data.analog.push_back(amplitude * std::sqrt(2.0) *
                                  std::sin(kTwoPi * 50.0 * time + phases[channel]));
        }
    }
    record.trigger_offset_s = 0.1;
    record.first_sample_time_s = first_time;
    record.first_sample_nonzero = first_time != 0.0;
    return record;
}

                                                                          
                                                                        
ComtradeRecord makeReorderedRecord() {
    ComtradeRecord record = makeRecord(4.0);
    const double factors[3] = {1.0, 2.0, 3.0};
    for (std::size_t sample = 0; sample < record.data.sampleCount(); ++sample) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            record.data.analog[sample * 3 + channel] *= factors[channel];
        }
    }
    const auto original = record.cfg.analog;
    record.cfg.analog = {original[1], original[0], original[2]};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        record.cfg.analog[channel].position = channel;
        record.cfg.analog[channel].declared_index =
            static_cast<std::uint32_t>(channel + 1);
    }
    for (std::size_t sample = 0; sample < record.data.sampleCount(); ++sample) {
        const double values[3] = {
            record.data.analog[sample * 3 + 1],
            record.data.analog[sample * 3 + 0],
            record.data.analog[sample * 3 + 2],
        };
        for (std::size_t channel = 0; channel < 3; ++channel) {
            record.data.analog[sample * 3 + channel] = values[channel];
        }
    }
    return record;
}

                                                                             
ComtradeRecord makeVoltageRecord() {
    ComtradeRecord record = makeRecord(4.0);
    const std::vector<double> original = record.data.analog;
    const char* names[3] = {"VAN", "VBN", "VCN"};
    const double rms[3] = {100.0, 101.0, 102.0};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        AnalogChannel voltage;
        voltage.position = 3 + channel;
        voltage.declared_index = static_cast<std::uint32_t>(4 + channel);
        voltage.name = names[channel];
        voltage.unit = "V";
        voltage.a = 1.0;
        voltage.b = 0.0;
        voltage.min_value = -32768.0;
        voltage.max_value = 32767.0;
        voltage.primary = 1.0;
        voltage.secondary = 1.0;
        voltage.ps = "S";
        record.cfg.analog.push_back(voltage);
    }
    record.cfg.declared_total_channels = 6;
    record.data.analog_channels = 6;
    record.data.analog.clear();
    record.data.analog.reserve(record.data.sampleCount() * 6);
    const double phases[3] = {0.0, -kTwoPi / 3.0, kTwoPi / 3.0};
    for (std::size_t sample = 0; sample < record.data.sampleCount(); ++sample) {
        const double time = record.data.times_s[sample];
        for (std::size_t channel = 0; channel < 3; ++channel) {
            record.data.analog.push_back(original[sample * 3 + channel]);
        }
        for (std::size_t channel = 0; channel < 3; ++channel) {
            record.data.analog.push_back(
                rms[channel] * std::sqrt(2.0) *
                std::sin(kTwoPi * 50.0 * time + phases[channel]));
        }
    }
    return record;
}

const report::ComparisonRow* findComparisonRow(
    const report::ComparisonResult& comparison,
    const std::string& record,
    const std::string& window,
    const std::string& phase,
    const std::string& quantity) {
    for (const auto& row : comparison.rows) {
        if (row.record == record && row.window == window &&
            row.phase == phase && row.quantity == quantity) {
            return &row;
        }
    }
    return nullptr;
}

bool closeTo(double value, double expected, double tolerance = 1e-6) {
    return std::isfinite(value) && std::fabs(value - expected) <= tolerance;
}

bool hasNonFiniteJsonToken(const std::string& text) {
    static const std::regex token(
        R"((^|[^A-Za-z0-9_])[-+]?(nan|infinity|inf)($|[^A-Za-z0-9_]))",
        std::regex_constants::icase);
    return std::regex_search(text, token);
}

std::vector<std::uint8_t> bytesOf(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void testEventLogAndTimeline() {
    const ComtradeRecord record = makeRecord(4.0);
    const std::string csv =
        "Date;Time;Name;FunctionStructure;Value\n"
        "01/01/2026;00:00:00.150;Fault distance;Linea;3.5 km\n"
        "01/01/2026;00:00:00.200;Trip;87L;1\n"
        "sin-fecha;;Pickup;50;1\n";
    auto parsed = report::parseEventLogCsv(bytesOf(csv), "Fault log.csv");
    CHECK(parsed.rows.size() == 2);
    std::vector<report::EventLogRow> distance_rows{
        report::EventLogRow{.signal = "Fault distance in %",
                            .value = "1253.89%"},
        report::EventLogRow{.signal = "Fault distance",
                            .value = "48.02km"}};
    const auto selected_distance =
        report::selectFaultDistanceText(distance_rows);
    CHECK(selected_distance.has_value());
    if (selected_distance) {
        CHECK(*selected_distance == "48.02km");
        CHECK(std::fabs(analysis::parseFaultDistanceKm(*selected_distance)
                            .value_or(-1.0) -
                        48.02) < 1e-9);
    }
    distance_rows.pop_back();
    CHECK(!report::selectFaultDistanceText(distance_rows).has_value());
    CHECK(parsed.diagnostics.size() >= 1);
    if (parsed.rows.size() == 2) {
        CHECK(parsed.rows[0].signal == "Fault distance");
        CHECK(parsed.rows[0].value == "3.5 km");
        CHECK(parsed.rows[1].when_valid);
    }
    auto timeline = report::buildTimeline(record, nullptr, parsed.rows, 0.0);
    CHECK(timeline.rows.size() == 2);
    CHECK(timeline.excluded_log_events == 0);
    if (timeline.rows.size() == 2) {
        CHECK(std::fabs(timeline.rows[0].seconds_from_reference - 0.05) < 1e-9);
        CHECK(std::fabs(timeline.rows[1].seconds_from_reference - 0.10) < 1e-9);
    }

                                                                  
    auto shifted = report::buildTimeline(record, nullptr, parsed.rows, 0.01);
    CHECK(shifted.rows.size() == 2);
    if (shifted.rows.size() == 2) {
        CHECK(std::fabs(shifted.rows[0].seconds_from_reference - 0.06) < 1e-9);
    }

                                                         
    const std::string far =
        "Date,Time,Name,Value\n"
        "01/01/2026,00:10:00.000,Trip,1\n";
    auto far_parsed = report::parseEventLogCsv(bytesOf(far), "otra.csv");
    CHECK(far_parsed.rows.size() == 1);
    auto excluded = report::buildTimeline(record, nullptr, far_parsed.rows, 0.0);
    CHECK(excluded.excluded_log_events == 1);
    CHECK(excluded.rows.empty());
}

void testTrendAndComparison() {
    const ComtradeRecord record = makeRecord(4.0);
    const auto record_analysis = analysis::analyzeRecord(record);
    CHECK(record_analysis.ok);
    std::array<std::optional<std::size_t>, 3> phases;
    phases[0] = 0;
    phases[1] = 1;
    phases[2] = 2;
    const auto trend = report::rollingTrend(record, phases, 1, 100);
    CHECK(!trend.points.empty());
    CHECK(trend.points.size() <= 100);
    if (!trend.points.empty()) {
        CHECK(std::isfinite(trend.points.front().phase_rms[0]));
        CHECK(std::isfinite(trend.points.front().sequence_rms[1]));
    }

    const ComtradeRecord other = makeRecord(2.0);
    std::vector<std::pair<std::string, const ComtradeRecord*>> others{
        {"otro", &other}};
    const auto comparison =
        report::compareRecords("actual", record, record_analysis, others, 1);
    CHECK(comparison.rows.size() == 12);
    CHECK(comparison.warnings.empty());

                                                      
    ComtradeRecord no_trigger = makeRecord(2.0);
    no_trigger.trigger_offset_s.reset();
    const auto blocked =
        report::compareRecords("actual", no_trigger, record_analysis, others, 1);
    CHECK(blocked.rows.empty());
    CHECK(!blocked.warnings.empty());
}

void testComparisonManualPhaseOrder() {
    const ComtradeRecord current = makeRecord(4.0);
    const auto current_analysis = analysis::analyzeRecord(current);
    CHECK(current_analysis.ok);
    CHECK(current_analysis.time.trigger_within_capture);

    const ComtradeRecord reordered = makeReorderedRecord();

                                                                                
    report::ComparisonTarget manual_target;
    manual_target.label = "reordenado";
    manual_target.record = &reordered;
    manual_target.phase_indices = {1, 0, 2};
    const auto manual_comparison = report::compareRecords(
        "actual", current, current_analysis,
        std::vector<report::ComparisonTarget>{manual_target}, 1);
    CHECK(manual_comparison.rows.size() == 12);
    CHECK(manual_comparison.warnings.empty());
                                                                            
                                                                
    const auto* current_row =
        findComparisonRow(manual_comparison, "actual", "Prefalla", "A",
                          "Corriente");
    CHECK(current_row != nullptr);
    const auto* row_a = findComparisonRow(manual_comparison, "reordenado",
                                          "Prefalla", "A", "Corriente");
    const auto* row_b = findComparisonRow(manual_comparison, "reordenado",
                                          "Prefalla", "B", "Corriente");
    const auto* row_c = findComparisonRow(manual_comparison, "reordenado",
                                          "Prefalla", "C", "Corriente");
    CHECK(row_a != nullptr);
    CHECK(row_b != nullptr);
    CHECK(row_c != nullptr);
    if (row_a && row_b && row_c) {
        CHECK(row_a->finite);
        CHECK(closeTo(row_a->magnitude, 1.0, 2e-3));
        CHECK(closeTo(row_b->magnitude, 2.0, 3e-3));
        CHECK(closeTo(row_c->magnitude, 3.0, 4e-3));
        CHECK(row_a->unit == "A");
        CHECK(row_a->phase == "A");
    }

                                                                               
                                             
    const auto reordered_analysis = analysis::analyzeRecord(reordered);
    CHECK(reordered_analysis.phases.index[0] == 1);
    CHECK(reordered_analysis.phases.index[1] == 0);
    CHECK(reordered_analysis.phases.index[2] == 2);
    report::ComparisonTarget auto_target;
    auto_target.label = "reordenado-auto";
    auto_target.record = &reordered;
    auto_target.analysis = &reordered_analysis;
    const auto auto_comparison = report::compareRecords(
        "actual", current, current_analysis,
        std::vector<report::ComparisonTarget>{auto_target}, 1);
    const auto* auto_a = findComparisonRow(auto_comparison, "reordenado-auto",
                                           "Prefalla", "A", "Corriente");
    const auto* auto_b = findComparisonRow(auto_comparison, "reordenado-auto",
                                           "Prefalla", "B", "Corriente");
    const auto* auto_c = findComparisonRow(auto_comparison, "reordenado-auto",
                                           "Prefalla", "C", "Corriente");
    CHECK(auto_a != nullptr);
    CHECK(auto_b != nullptr);
    CHECK(auto_c != nullptr);
    if (auto_a && auto_b && auto_c) {
        CHECK(closeTo(auto_a->magnitude, 1.0, 2e-3));
        CHECK(closeTo(auto_b->magnitude, 2.0, 3e-3));
        CHECK(closeTo(auto_c->magnitude, 3.0, 4e-3));
    }
}

void testVoltageComparisonAndReports() {
    analysis::AnalysisOptions options;
    options.cycles = 2;
    const ComtradeRecord record = makeVoltageRecord();
    const auto record_analysis = analysis::analyzeRecord(record, options);
    CHECK(record_analysis.ok);
    CHECK(record_analysis.voltage.available);
    CHECK(record_analysis.voltage.selection.index[0] == 3);
    CHECK(record_analysis.voltage.selection.index[1] == 4);
    CHECK(record_analysis.voltage.selection.index[2] == 5);
    CHECK(record_analysis.spectra[0].has_value());
    if (record_analysis.spectra[0].has_value()) {
        CHECK(record_analysis.spectra[0]->ok);
    }
    CHECK(record_analysis.saturation[0].has_value());
    if (record_analysis.saturation[0].has_value()) {
        CHECK(record_analysis.saturation[0]->available);
    }

    const ComtradeRecord other = makeVoltageRecord();
    const auto other_analysis = analysis::analyzeRecord(other, options);
    CHECK(other_analysis.voltage.available);

    report::ComparisonTarget target;
    target.label = "otro-tension";
    target.record = &other;
    target.analysis = &other_analysis;
    target.phase_indices = other_analysis.phases.index;
    target.voltage_indices = other_analysis.voltage.selection.index;
    const auto comparison = report::compareRecords(
        "actual", record, record_analysis,
        std::vector<report::ComparisonTarget>{target}, 2);
    CHECK(comparison.warnings.empty());
    CHECK(comparison.rows.size() == 24);
    std::size_t voltage_rows = 0;
    for (const auto& row : comparison.rows) {
        CHECK(std::isfinite(row.magnitude));
        CHECK(std::isfinite(row.angle_deg));
        if (row.quantity == "Tensión") {
            ++voltage_rows;
            CHECK(row.unit == "V");
            CHECK(row.finite);
        }
    }
    CHECK(voltage_rows == 12);
    const auto* row_va =
        findComparisonRow(comparison, "actual", "Falla", "A", "Tensión");
    const auto* row_vb =
        findComparisonRow(comparison, "actual", "Falla", "B", "Tensión");
    const auto* row_vc =
        findComparisonRow(comparison, "actual", "Falla", "C", "Tensión");
    CHECK(row_va != nullptr);
    CHECK(row_vb != nullptr);
    CHECK(row_vc != nullptr);
    if (row_va && row_vb && row_vc) {
        CHECK(closeTo(row_va->magnitude, 100.0, 0.2));
        CHECK(closeTo(row_vb->magnitude, 101.0, 0.2));
        CHECK(closeTo(row_vc->magnitude, 102.0, 0.2));
    }

    std::vector<analizador::Diagnostic> diagnostics =
        record_analysis.diagnostics;
    report::Context context;
    context.title = "Registro con tensión";
    context.source = "prueba";
    context.record = &record;
    context.analysis = &record_analysis;
    context.diagnostics = &diagnostics;

    const auto directory = std::filesystem::temp_directory_path();
    const auto html_path = directory / "analizador_test_tension.html";
    const auto json_path = directory / "analizador_test_tension.json";
    CHECK(report::writeHtmlReport(html_path, context).ok);
    CHECK(report::writeSummaryJson(json_path, context).ok);
    const std::string html = readFile(html_path);
    const std::string json = readFile(json_path);
    CHECK(html.find("Tensión (unidad CFG, sin conversión)") !=
          std::string::npos);
    CHECK(html.find("Espectro FFT de inspección") != std::string::npos);
    CHECK(html.find("Indicador heurístico de saturación") != std::string::npos);
    CHECK(html.find("100") != std::string::npos);
    CHECK(json.find("\"voltage\"") != std::string::npos);
    CHECK(json.find("\"spectra\"") != std::string::npos);
    CHECK(json.find("\"saturation\"") != std::string::npos);
    CHECK(!hasNonFiniteJsonToken(html));
    CHECK(!hasNonFiniteJsonToken(json));

    std::error_code error;
    std::filesystem::remove(html_path, error);
    std::filesystem::remove(json_path, error);
}

void testAdvancedDiagnosticCardAndBreakerEscaping() {
    ComtradeRecord record = makeRecord(4.0);
    record.cfg.analog[0].name = "IA</script><script>alert(1)</script>";
    record.cfg.analog[1].name = "IB\" onmouseover=\"x\n\x01<script>";
    record.cfg.analog[1].unit = "kA";
    record.cfg.analog[2].unit = "V";
    record.cfg.station_name = "EST<&>";

    analysis::RecordAnalysis record_analysis;
    record_analysis.ok = true;
    record_analysis.phases.index = {0, 1, 2};
    record_analysis.prefalla_center_s = 0.10;
    record_analysis.falla_center_s = 0.40;
    record_analysis.window_half_s = 0.01;
    record_analysis.ratios.i0_i1 = 0.01;
    record_analysis.ratios.i2_i1 = 0.25;
    record_analysis.ratios.i1_fault_pre = 4.0;

    std::array<analysis::Phasor, 3> pre{};
    std::array<analysis::Phasor, 3> fault{};
    for (std::size_t slot = 0; slot < 3; ++slot) {
        pre[slot].value = std::complex<double>(1.0 + static_cast<double>(slot),
                                               0.0);
        fault[slot].value = std::complex<double>(4.0 + static_cast<double>(slot),
                                                 -0.5);
    }
    record_analysis.prefalla_phasors = pre;
    record_analysis.falla_phasors = fault;

    analysis::SaturationIndicator sat_a;
    sat_a.available = true;
    sat_a.possible = true;
    sat_a.flat_top_pct = 12.5;
    sat_a.longest_flat_top_ms = 3.25;
    sat_a.flat_top_elevated = true;
    sat_a.h2_h1_pct = 25.0;
    sat_a.h2_elevated = true;
    sat_a.shoulder_hits = 3;
    sat_a.curvature_threshold = 4.0;
    sat_a.high_fraction = 0.55;
    sat_a.shoulder_peak = 2.0;
    sat_a.methodology = "<b>metodologia</b>";
    analizador::Diagnostic sat_diag;
    sat_diag.severity = analizador::DiagnosticSeverity::Warning;
    sat_diag.code = "analysis.sat.<script>";
    sat_diag.message = "<script>alert('sat')</script>";
    sat_a.diagnostics.push_back(sat_diag);
    record_analysis.saturation[0] = sat_a;

    analysis::SaturationIndicator sat_b;
    sat_b.available = true;
    sat_b.possible = false;
    sat_b.h2_h1_pct = 5.0;
    sat_b.shoulder_hits = 0;
    sat_b.shoulder_required_hits = 2;
    record_analysis.saturation[1] = sat_b;
                                                                            
    record_analysis.saturation[2].reset();

    auto& breaker = record_analysis.breaker;
    breaker.configured = true;
    breaker.onset_s = 0.30;
    breaker.trip_s.reset();
    breaker.contact_open_s = 0.32;
    breaker.current_extinction_s.reset();
    breaker.protection_ms = 20.0;
    breaker.contact_opening_ms.reset();
    breaker.clearing_ms.reset();
    breaker.digital_resolution_ms = 1.0;
    breaker.extinction_resolution_ms = 2.0;
    analizador::Diagnostic breaker_diag;
    breaker_diag.severity = analizador::DiagnosticSeverity::Info;
    breaker_diag.code = "breaker.<script>";
    breaker_diag.message = "<script>alert('breaker')</script>";
    breaker.diagnostics.push_back(breaker_diag);

    std::vector<analizador::Diagnostic> diagnostics;
    report::Context context;
    context.title = "Diagnóstico avanzado";
    context.source = "prueba";
    context.record = &record;
    context.analysis = &record_analysis;
    context.diagnostics = &diagnostics;

    const auto directory = std::filesystem::temp_directory_path();
    const auto html_path = directory / "analizador_test_avanzado.html";
    const auto json_path = directory / "analizador_test_avanzado.json";
    CHECK(report::writeHtmlReport(html_path, context).ok);
    CHECK(report::writeSummaryJson(json_path, context).ok);
    const std::string html = readFile(html_path);
    const std::string json = readFile(json_path);

    CHECK(html.find("Indicador heurístico de saturación y diagnóstico avanzado") !=
          std::string::npos);
    CHECK(html.find("I2 / I1 × 100") != std::string::npos);
    CHECK(html.find("25 %") != std::string::npos);
    CHECK(html.find("indicios compatibles con saturación o recorte") !=
          std::string::npos);
    CHECK(html.find("sin indicios con este criterio") != std::string::npos);
    CHECK(html.find("no evaluado") != std::string::npos);
    CHECK(html.find("No calculable") != std::string::npos);
    CHECK(html.find("Tiempos de interruptor (opcional)") != std::string::npos);
    CHECK(html.find("No observado / no calculable") != std::string::npos);
    CHECK(html.find("<script>") == std::string::npos);
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    CHECK(html.find("&quot;") != std::string::npos);
    CHECK(html.find("&#x01;") != std::string::npos);
    CHECK(html.find("kA") != std::string::npos);
    CHECK(html.find("no comparten una unidad CFG") != std::string::npos);
    CHECK(!hasNonFiniteJsonToken(html));

    CHECK(json.find("\"breaker\"") != std::string::npos);
    CHECK(json.find("\"configured\": true") != std::string::npos);
    CHECK(json.find("\"trip_s\": null") != std::string::npos);
    CHECK(json.find("\"current_extinction_s\": null") != std::string::npos);
    CHECK(json.find("\"protection_ms\": 20") != std::string::npos);
    CHECK(json.find("\"digital_resolution_ms\": 1") != std::string::npos);
    CHECK(json.find("\"flat_top_pct\": 12.5") != std::string::npos);
    CHECK(json.find("\"i2_i1_pct\": 25") != std::string::npos);
    CHECK(json.find("\"indicator_state\": \"indicios compatibles") !=
          std::string::npos);
    CHECK(json.find("\"trip_s\": 0") == std::string::npos);
    CHECK(json.find("\\u0001") != std::string::npos);
    CHECK(!hasNonFiniteJsonToken(json));

                                                                    
    analysis::RecordAnalysis missing = record_analysis;
    missing.breaker = analysis::BreakerTimingResult{};
    report::Context missing_context = context;
    missing_context.analysis = &missing;
    const auto missing_json_path = directory / "analizador_test_avanzado_sin.json";
    const auto missing_html_path = directory / "analizador_test_avanzado_sin.html";
    CHECK(report::writeSummaryJson(missing_json_path, missing_context).ok);
    CHECK(report::writeHtmlReport(missing_html_path, missing_context).ok);
    const std::string missing_json = readFile(missing_json_path);
    const std::string missing_html = readFile(missing_html_path);
    CHECK(missing_json.find("\"configured\": false") != std::string::npos);
    CHECK(missing_json.find("\"onset_s\": null") != std::string::npos);
    CHECK(missing_json.find("\"digital_resolution_ms\": null") !=
          std::string::npos);
    CHECK(missing_json.find("\"onset_s\": 0") == std::string::npos);
    CHECK(!hasNonFiniteJsonToken(missing_json));
    CHECK(missing_html.find("No evaluado") != std::string::npos);
    CHECK(missing_html.find("No observado / no calculable") !=
          std::string::npos);
    CHECK(!hasNonFiniteJsonToken(missing_html));

    std::error_code error;
    std::filesystem::remove(html_path, error);
    std::filesystem::remove(json_path, error);
    std::filesystem::remove(missing_json_path, error);
    std::filesystem::remove(missing_html_path, error);
}

void testBatchHtmlUsesSuppliedComparison() {
    const ComtradeRecord record = makeRecord(4.0);
    const auto record_analysis = analysis::analyzeRecord(record);
    const ComtradeRecord other = makeRecord(2.0);
    const auto other_analysis = analysis::analyzeRecord(other);

    report::BatchItem first;
    first.label = "primero";
    first.source = "origen-1";
    first.record = &record;
    first.analysis = &record_analysis;
    first.phase_indices = {2, 1, 0};
    first.voltage_indices = {};
    report::BatchItem second;
    second.label = "segundo";
    second.source = "origen-2";
    second.record = &other;
    second.analysis = &other_analysis;
    second.phase_indices = other_analysis.phases.index;
    second.voltage_indices = other_analysis.voltage.selection.index;
    std::vector<report::BatchItem> items{first, second};

    report::ComparisonResult supplied;
    report::ComparisonRow injected;
    injected.record = "solo-esta-fila";
    injected.window = "Falla";
    injected.phase = "B";
    injected.quantity = "Tensión";
    injected.frequency_hz = 50.0;
    injected.magnitude = 42.0;
    injected.angle_deg = -10.0;
    injected.unit = "V";
    injected.finite = true;
    supplied.rows.push_back(injected);
    supplied.warnings.push_back("advertencia inyectada");

    const auto directory = std::filesystem::temp_directory_path();
    const auto path = directory / "analizador_test_lote.html";
    const std::vector<std::string> failures{"fallo de lectura X"};
    CHECK(report::writeBatchHtml(path, items, failures, 1, &supplied).ok);
    const std::string html = readFile(path);
    CHECK(html.find("solo-esta-fila") != std::string::npos);
    CHECK(html.find("advertencia inyectada") != std::string::npos);
    CHECK(html.find("Tensión") != std::string::npos);
    CHECK(html.find(">42<") != std::string::npos ||
          html.find("42") != std::string::npos);
    CHECK(html.find("A=IC (#3)") != std::string::npos);
    CHECK(html.find("B=IB (#2)") != std::string::npos);
    CHECK(html.find("C=IA (#1)") != std::string::npos);
    CHECK(html.find("fallo de lectura X") != std::string::npos);
    CHECK(html.find("no dictamina") != std::string::npos);
    CHECK(!hasNonFiniteJsonToken(html));

    std::error_code error;
    std::filesystem::remove(path, error);
}

void testHtmlAndPackage() {
    const ComtradeRecord record = makeRecord(4.0);
    const auto record_analysis = analysis::analyzeRecord(record);
    std::vector<analizador::Diagnostic> diagnostics = record_analysis.diagnostics;
    report::Context context;
    context.title = "<script>alert(1)</script>";
    context.source = "prueba";
    context.record = &record;
    context.analysis = &record_analysis;
    context.diagnostics = &diagnostics;

    const auto directory = std::filesystem::temp_directory_path();
    const auto html_path = directory / "analizador_test_reporte.html";
    const auto zip_path = directory / "analizador_test_paquete.zip";
    const auto html_result = report::writeHtmlReport(html_path, context);
    CHECK(html_result.ok);
    const std::string html = readFile(html_path);
    CHECK(html.find("&lt;script&gt;") != std::string::npos);
    CHECK(html.find("<script>") == std::string::npos);
    CHECK(html.find("Estado de la referencia prefalla") != std::string::npos);
    CHECK(html.find("Intervalo de armónicos") != std::string::npos);
    CHECK(html.find("Cambios de régimen por etapas") != std::string::npos);

    report::PackageData package;
    std::vector<report::EventLogRow> log;
    package.event_log = &log;
    const auto zip_result =
        report::writeAnalysisPackage(zip_path, context, {}, package);
    CHECK(zip_result.ok);
    const std::string zip = readFile(zip_path);
    CHECK(zip.size() > 100);
    CHECK(zip.rfind("PK\x03\x04", 0) == 0);
    CHECK(zip.find("informe.html") != std::string::npos);
    CHECK(zip.find("muestras.csv") != std::string::npos);
    CHECK(zip.find("resumen.json") != std::string::npos);
    CHECK(zip.find("prefalla_usable") != std::string::npos);
    CHECK(zip.find("harmonic_start_s") != std::string::npos);
    CHECK(zip.find("regime_changes") != std::string::npos);
    CHECK(zip.find("espectro_fft.csv") != std::string::npos);
    CHECK(zip.find("espectro_fft_bins.csv") != std::string::npos);
    CHECK(zip.find("saturacion_heuristica.csv") != std::string::npos);
    CHECK(zip.find("Aplanamiento %") != std::string::npos);
    CHECK(zip.find("Curvatura umbral") != std::string::npos);
    CHECK(zip.find("interruptor_tiempos.csv") != std::string::npos);
    CHECK(zip.find("Resolucion digital ms") != std::string::npos);
    CHECK(zip.find("tension_fasores.csv") != std::string::npos);
    CHECK(zip.find("tension_secuencias.csv") != std::string::npos);

    std::error_code error;
    std::filesystem::remove(html_path, error);
    std::filesystem::remove(zip_path, error);
}

}              

int main() {
                                                                            
    CHECK(!hasNonFiniteJsonToken("Secuencia positiva dominante"));
    CHECK(hasNonFiniteJsonToken("{\"value\":NaN}"));
    CHECK(hasNonFiniteJsonToken("{\"value\":-Infinity}"));
    testEventLogAndTimeline();
    testTrendAndComparison();
    testComparisonManualPhaseOrder();
    testVoltageComparisonAndReports();
    testAdvancedDiagnosticCardAndBreakerEscaping();
    testBatchHtmlUsesSuppliedComparison();
    testHtmlAndPackage();
    if (g_failures != 0) {
        std::cerr << g_failures << " fallo(s)\n";
        return 1;
    }
    std::cout << "Pruebas de informes: OK\n";
    return 0;
}
