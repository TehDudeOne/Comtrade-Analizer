                                                     
  
                                                                           
                                                                                
                                                        
                                                 
  
                                                                              
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "analizador/analysis.hpp"
#include "analizador/model.hpp"

namespace analizador::report {

                                                                              
                                                                       
                                                                              

                                                                               
                                                                           
                                                           
struct EventLogRow {
    std::string source;
    std::string when_text;
    DateTime when;
    bool when_valid = false;
    std::string structure;
    std::string signal;
    std::string value;
    std::optional<double> seconds_from_start;
    std::optional<double> seconds_from_trigger;
};

struct EventLogParseResult {
    std::vector<EventLogRow> rows;
    std::vector<Diagnostic> diagnostics;
};

                                                                               
                                                                               
                                                                           
EventLogParseResult parseEventLogCsv(const std::vector<std::uint8_t>& bytes,
                                     const std::string& source);

                                                                              
std::optional<std::string> selectFaultDistanceText(
    const std::vector<EventLogRow>& rows);

struct TimelineRow {
    double seconds_from_reference = 0.0;
    std::string source;
    std::string event;
    std::string detail;
};

struct TimelineResult {
    std::vector<TimelineRow> rows;
    std::size_t excluded_log_events = 0;
    std::vector<Diagnostic> diagnostics;
};

                                                                           
                                                                                 
                                         
TimelineResult buildTimeline(const ComtradeRecord& record,
                             const analysis::RecordAnalysis* analysis,
                             std::vector<EventLogRow>& log,
                             double clock_offset_s = 0.0);

struct TrendPoint {
    double time_s = 0.0;                            
    std::array<double, 3> phase_rms{};
    std::array<double, 3> sequence_rms{};
};

struct TrendResult {
    std::vector<TrendPoint> points;
    int cycles = 1;
    std::string unit;
    std::vector<Diagnostic> diagnostics;
};

                                                                       
                                                                                
                                        
TrendResult rollingTrend(
    const ComtradeRecord& record,
    const std::array<std::optional<std::size_t>, 3>& phase_indices,
    int cycles = 1,
    std::size_t max_points = 600);

struct Result {
    bool ok = false;
    std::string error;                                              
};

                                                                              
                                      
                                                                              

struct ComparisonRow {
    std::string record;
    std::string window;
    std::string phase;
    std::string quantity;                              
    double frequency_hz = 0.0;
    double magnitude = 0.0;
    double angle_deg = 0.0;
    std::string unit;
    bool finite = false;
};

struct ComparisonResult {
    std::vector<ComparisonRow> rows;
    std::vector<std::string> warnings;
    std::vector<Diagnostic> diagnostics;
};

                                                                                
                                                                              
                                                                              
                                                                   
struct ComparisonTarget {
    std::string label;
    const ComtradeRecord* record = nullptr;
    const analysis::RecordAnalysis* analysis = nullptr;
    std::array<std::optional<std::size_t>, 3> phase_indices{};
    std::array<std::optional<std::size_t>, 3> voltage_indices{};
};

                                                                     
                                                                             
                                                                           
                                                                             
                                          
ComparisonResult compareRecords(
    const std::string& current_label,
    const ComtradeRecord& current,
    const analysis::RecordAnalysis& current_analysis,
    const std::vector<ComparisonTarget>& others,
    int cycles = 1);

                                                                            
                                                              
ComparisonResult compareRecords(
    const std::string& current_label,
    const ComtradeRecord& current,
    const analysis::RecordAnalysis& current_analysis,
    const std::vector<std::pair<std::string, const ComtradeRecord*>>& others,
    int cycles = 1);

struct BatchItem {
    std::string label;
    std::string source;
    const ComtradeRecord* record = nullptr;
    const analysis::RecordAnalysis* analysis = nullptr;
    std::vector<Diagnostic> diagnostics;
    std::array<std::optional<std::size_t>, 3> phase_indices{};
    std::array<std::optional<std::size_t>, 3> voltage_indices{};
};

                                                                           
                                                                            
                                                                              
                                                                      
                                           
Result writeBatchHtml(const std::filesystem::path& path,
                      const std::vector<BatchItem>& items,
                      const std::vector<std::string>& failures,
                      int cycles = 1,
                      const ComparisonResult* comparison = nullptr);

struct Context {
                                                                       
                                                               
    std::string title;
    std::string source;

                                                                             
                                                                           
                                                                 
    const ComtradeRecord* record = nullptr;
    const std::vector<Diagnostic>* diagnostics = nullptr;
    const analysis::RecordAnalysis* analysis = nullptr;

                                                                                
                                                                         
    const std::vector<EventLogRow>* event_log = nullptr;
    const TimelineResult* timeline = nullptr;
    const TrendResult* trend = nullptr;
    const ComparisonResult* comparison = nullptr;
                                                                                
                                                                       
    const std::map<std::size_t, std::string>* unit_overrides = nullptr;
    double reference_length_km = 0.0;
    double log_clock_offset_s = 0.0;
};

                                                                     
struct PackageData {
    const std::vector<EventLogRow>* event_log = nullptr;
    const TimelineResult* timeline = nullptr;
    const TrendResult* trend = nullptr;
    const ComparisonResult* comparison = nullptr;
    const std::vector<BatchItem>* batch = nullptr;
    std::vector<std::string> batch_failures;
};

struct Options {
                                                                              
                                                              
    std::size_t max_svg_points = 2400;
    std::size_t max_svg_channels = 12;
};

                                                                                 
                                                                           
Result writeHtmlReport(const std::filesystem::path& path,
                       const Context& context,
                       const Options& options = {});

                                                                           
                                                                        
                                  
Result writeSampleCsv(const std::filesystem::path& path,
                      const Context& context);

                                                                                
                                                         
Result writeSummaryJson(const std::filesystem::path& path,
                        const Context& context);

                                                                              
                                                                                
                                                                                
                                                                    
Result writeAnalysisPackage(const std::filesystem::path& path,
                            const Context& context,
                            const Options& options,
                            const PackageData& package);

                                                                      
                                                                            
std::wstring safeFileStem(std::wstring_view text);

}                                 
