#pragma once
#include <optional>
#include <string>
#include <vector>
#include <cstddef>
#include "analizador/model.hpp"
namespace analizador::analysis {
struct RecordAnalysis;
struct DigitalSignalAssignment {
    std::optional<std::size_t> channel;
    bool active_high = true;
};
struct BreakerTimingOptions {
    DigitalSignalAssignment trip;
    DigitalSignalAssignment contact_52a;                       
    DigitalSignalAssignment contact_52b;                     
    double debounce_ms = 2.0;
    double extinction_fraction = 0.05;
    int extinction_cycles = 1;
};
struct BreakerTimingResult {
    BreakerTimingOptions applied_options;
    bool configured = false;
    std::optional<double> onset_s, trip_s, contact_open_s, current_extinction_s;
    std::optional<double> protection_ms, contact_opening_ms, clearing_ms;
    double digital_resolution_ms = 0.0;
    double extinction_resolution_ms = 0.0;
    double onset_resolution_ms = 0.0;
    std::vector<Diagnostic> diagnostics;
};
BreakerTimingResult analyzeBreakerTiming(const ComtradeRecord& record,
    const RecordAnalysis& analysis, const BreakerTimingOptions& options);
}                                  
