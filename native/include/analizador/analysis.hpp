                                                                               
  
                                                                           
                                                                                 
                                                                           
                                                                             
                                                                                 
                                                  
                                                                               
                                                                              
                                                                               
                                                                             
                                                                            
                                                                               
                   
  
                                                                                 
                                                                           
#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analizador/model.hpp"
#include "analizador/breaker.hpp"

namespace analizador::analysis {

inline constexpr double kPi = 3.141592653589793238462643383279502884;

                                                                              
                                   
                                                                              

                                                                           
                                                     
enum class ChangeDirection {
    None,
    Rise,
    Fall,
    Change,
};

const char* changeDirectionName(ChangeDirection direction) noexcept;

                                                               
enum class WaveformOrigin {
    Onset,
    Trigger,
    Midpoint,
};

const char* waveformOriginName(WaveformOrigin origin) noexcept;

enum class FaultDistanceStatus {
    NotInterpretable,                                               
    NoReferenceLength,                                                         
    WithinReference,                                                         
    ExceedsReference,                                               
};

const char* faultDistanceStatusName(FaultDistanceStatus status) noexcept;

                                                                              
                                                      
                                                                              

struct TimeAlignment {
    bool ok = false;                                                         

    double first_s = 0.0;                                 
    double last_s = 0.0;
    double median_dt_s = 0.0;                                                 
    double median_rate_hz = 0.0;                                      

    bool first_sample_nonzero = false;                               

    bool trigger_offset_known = false;                                         
    double trigger_offset_cfg_s = 0.0;                                      
    bool trigger_within_capture = false;                                     
    bool trigger_reference_is_midpoint = false;
    double reference_s = 0.0;                                               
    std::string reference_label;                                                   

    std::vector<Diagnostic> diagnostics;
};

                                                                                
TimeAlignment alignTimestamps(const ComtradeRecord& record);

                                                                              
                                                
                                                                              

                                                                    
struct Phasor {
    std::complex<double> value{0.0, 0.0};

    double magnitude() const noexcept;                                    
    double angleDegrees() const noexcept;                                    
    bool isFinite() const noexcept;
};

std::vector<Phasor> makePhasors(const std::vector<std::complex<double>>& values);

                                                                            
                                                                            
struct AcRmsStats {
    double mean = 0.0;                                    
    double rms_ac = 0.0;                            
    std::size_t samples = 0;
};

std::optional<AcRmsStats> windowAcRms(const SampleData& data,
                                      std::size_t channel,
                                      double center_s,
                                      double half_s);

                                                                           
                                                                              
                                                                            
                                                                  
std::optional<std::vector<std::complex<double>>> windowPhasors(
    const SampleData& data,
    const std::vector<std::size_t>& channels,
    double frequency_hz,
    double center_s,
    int cycles = 1);

                                                                              
                                                  
                                                                              

struct SequenceSet {
    std::complex<double> zero{0.0, 0.0};           
    std::complex<double> positive{0.0, 0.0};       
    std::complex<double> negative{0.0, 0.0};       
};

                                                                 
std::optional<SequenceSet> symmetricalComponents(
    const std::vector<std::complex<double>>& phasors);

                                                  
std::array<double, 3> sequenceMagnitudes(const SequenceSet& sequences) noexcept;

struct SequenceRatios {
    std::optional<double> i0_i1;
    std::optional<double> i2_i1;
    std::optional<double> i1_fault_pre;
};

SequenceRatios computeSequenceRatios(const std::optional<SequenceSet>& pre,
                                     const std::optional<SequenceSet>& fault);

                                                                              
                                                       
                                                                              

struct WindowSpec {
    std::string label;
    double center_s = 0.0;
};

struct WindowCoherenceRow {
    std::string window_label;
    std::string channel_name;
    double ac_rms = 0.0;                                             
    double fundamental_rms = 0.0;                         
    double ratio = 0.0;                                                   
};

struct WindowCheckResult {
    std::vector<Diagnostic> errors;                                                
    std::vector<WindowCoherenceRow> rows;
};

                                                                               
                                                                                
                                                                         
WindowCheckResult checkPhasorWindows(const SampleData& data,
                                     const std::vector<std::size_t>& channels,
                                     double frequency_hz,
                                     const std::vector<WindowSpec>& windows,
                                     int cycles = 1,
                                     const std::vector<std::string>& channel_names = {});

                                                                              
                                                  
                                                                              

struct OnsetEvent {
    double time_s = 0.0;                                              
    ChangeDirection direction = ChangeDirection::None;
    std::string direction_text;                                      

    std::optional<double> recovery_s;
    bool recovery_detected = false;
    bool recovery_censored = true;
    double duration_s = 0.0;
    bool duration_is_lower_bound = true;

    double threshold_delta = 0.0;
    double baseline_rms = 0.0;
    double rms = 0.0;
    double delta = 0.0;
    double contrast = 0.0;                                 

    double peak_envelope = 0.0;
    std::optional<double> peak_contrast;                              

    int window_samples = 0;
    std::string baseline_source;
    bool persistent = true;
};

                                                                              
                                                                              
                                                                                
                                                                                    
std::optional<OnsetEvent> detectOnset(const SampleData& data,
                                      const std::vector<std::size_t>& channels,
                                      double frequency_hz,
                                      double trigger_s,
                                      double ratio = 1.5);

                                                
std::optional<OnsetEvent> detectEvent(const SampleData& data,
                                      const std::vector<std::size_t>& channels,
                                      double frequency_hz,
                                      double trigger_s,
                                      double ratio = 1.5);

                                                                              
                       
                                                                              

struct WindowCenters {
    double prefalla_s = 0.0;
    double falla_s = 0.0;
};

                                                                            
                                                                    
WindowCenters defaultWindowCenters(std::optional<double> onset_s,
                                   double first_s,
                                   double last_s,
                                   double frequency_hz,
                                   int cycles = 1,
                                   std::optional<double> trigger_s = std::nullopt);

struct ClampedCenter {
    double center_s = 0.0;
    bool clamped = false;                                             
};

ClampedCenter clampCenter(double center_s,
                          double first_s,
                          double last_s,
                          double half_s);

                                                                              
                                             
                                                                              

struct FaultClassifyContext {
    std::optional<double> pre_center_s;
    std::optional<double> onset_s;
    std::optional<double> window_duration_s;
};

struct FaultInterpretation {
    std::string label;                                                         
    std::string explanation;                                    
    double i0_i1 = 0.0;
    double i2_i1 = 0.0;
    double i1_fault_pre = 0.0;
};

std::optional<FaultInterpretation> classifyFault(
    const std::vector<std::complex<double>>& pre,
    const std::vector<std::complex<double>>& fault,
    const FaultClassifyContext& context = {});

                                                                              
                   
                                                                              

struct HarmonicFit {
    bool ok = false;
    int cycles = 1;
    int supported_order = 0;                                      
    double nyquist_hz = 0.0;

                                                         
    std::vector<double> harmonics_rms;
    std::optional<double> thd_pct;
    std::optional<double> h2_h1_pct;

    std::vector<Diagnostic> diagnostics;
};

                                                                                
                                                                             
                                                                                
HarmonicFit harmonicFit(const SampleData& data,
                        std::size_t channel,
                        double frequency_hz,
                        double center_s,
                        int cycles = 1,
                        int max_order = 15);

                                                                              
                                                                            
                                                                              

                                                                              
                                                                          
                                                                         
inline constexpr int kFftMinSourceSamples = 16;
inline constexpr std::size_t kFftMaxPoints = 8192;
inline constexpr int kMaxHarmonicOrder = 1000;
inline constexpr double kSaturationH2ThresholdPct = 15.0;
inline constexpr int kSaturationShoulderHits = 2;
inline constexpr double kSaturationCurvatureRatio = 4.0;
inline constexpr double kSaturationHighFraction = 0.55;
inline constexpr int kSaturationMinSamples = 24;

struct SpectrumResult {
    bool ok = false;
    int cycles = 1;
    int max_order = 0;
    int supported_order = 0;

    double fundamental_hz = 0.0;
    double sample_rate_hz = 0.0;                                                   
    double window_start_s = 0.0;
    double window_end_s = 0.0;
    std::size_t fft_points = 0;

                                                                               
                                                                         
    std::vector<double> bin_frequency_hz;
    std::vector<double> rms;

                                                                               
                                                                             
    std::vector<double> harmonics_rms;
    std::optional<double> thd_pct;
    std::optional<double> h2_h1_pct;

    std::string unit;                                                             
    std::vector<Diagnostic> diagnostics;
};

                                                                              
                                                                            
                                                                              
                                                                    
                                                                               
                                                               
SpectrumResult fftSpectrum(const SampleData& data,
                           std::size_t channel,
                           const std::string& unit,
                           double frequency_hz,
                           double center_s,
                           int cycles = 1,
                           int max_order = 15,
                           std::size_t max_points = kFftMaxPoints);

                                                                              
                                                                              
                                                                              
                                                       
struct SaturationIndicator {
    bool available = false;
    bool possible = false;                                                      
    std::optional<double> flat_top_pct;
    double longest_flat_top_ms = 0.0;
    bool flat_top_elevated = false;

    int shoulder_hits = 0;
    int shoulder_required_hits = kSaturationShoulderHits;
    double shoulder_peak = 0.0;
    double curvature_threshold = kSaturationCurvatureRatio;
    double high_fraction = kSaturationHighFraction;

    std::optional<double> h2_h1_pct;
    double h2_threshold_pct = kSaturationH2ThresholdPct;
    bool h2_elevated = false;

    std::string methodology;
    std::vector<Diagnostic> diagnostics;
};

                                                                          
                                                                
SaturationIndicator shoulderSaturation(const SampleData& data,
                                       std::size_t channel,
                                       double frequency_hz,
                                       double center_s,
                                       int cycles = 1,
                                       const HarmonicFit* fit = nullptr);

                                                                              
                                              
                                                                              

struct PhaseSelection {
                                   
    std::array<std::optional<std::size_t>, 3> index{};
    std::array<std::vector<std::size_t>, 3> candidates{};

                                                                             
                                                                          
    std::vector<std::pair<std::size_t, std::string>> ambiguous;

    bool has(char phase) const noexcept;
    bool complete() const noexcept;
    std::vector<std::size_t> inOrder() const;                            
    std::vector<std::size_t> prefix() const;                                        
};

PhaseSelection selectPhaseChannels(const CfgInfo& cfg);

std::vector<Diagnostic> phaseChannelDiagnostics(const CfgInfo& cfg,
                                                const PhaseSelection& selection,
                                                bool include_missing = true);

std::vector<Diagnostic> phaseMeasurementDiagnostics(const CfgInfo& cfg,
                                                    const PhaseSelection& selection);

                                                                             
                                                                            
                                                                                
                                                                               
PhaseSelection selectVoltageChannels(const CfgInfo& cfg);

std::vector<Diagnostic> voltageChannelDiagnostics(
    const CfgInfo& cfg,
    const PhaseSelection& selection,
    bool include_missing = true);

std::vector<Diagnostic> voltageMeasurementDiagnostics(
    const CfgInfo& cfg,
    const PhaseSelection& selection);

                                                                               
                                                                             
                                                                           
                                              
struct VoltageAnalysis {
    PhaseSelection selection;
    bool available = false;
    std::string unit;
    bool units_consistent = false;

    std::optional<std::array<Phasor, 3>> prefalla_phasors;
    std::optional<std::array<Phasor, 3>> falla_phasors;
    std::optional<SequenceSet> prefalla_sequences;
    std::optional<SequenceSet> falla_sequences;

                                                                            
                                                    
    std::array<std::optional<double>, 3> change_pct{};

    std::vector<Diagnostic> diagnostics;
};

                                                                              
                                             
                                                                              

std::optional<double> unitFactorToAmps(const std::string& unit);
std::string labelUnit(const std::string& channel_name);                     
std::string normalizeUnit(const std::string& unit);
std::optional<std::string> unitFamily(const std::string& unit);

std::vector<Diagnostic> unitDiagnostics(const CfgInfo& cfg);
std::vector<Diagnostic> skewDiagnostics(const CfgInfo& cfg);

                                                                              
                                                                         
                                                                              

struct FaultDistanceResult {
    FaultDistanceStatus status = FaultDistanceStatus::NotInterpretable;
    std::optional<double> distance_km;
    double reference_length_km = 0.0;
    std::optional<double> ratio;                                       
    std::vector<Diagnostic> diagnostics;
};

                                                                            
                                                                               
std::optional<double> parseFaultDistanceKm(const std::string& text);

                                                                             
                                                                     
FaultDistanceResult interpretFaultDistance(const std::string& text,
                                           double reference_length_km);

                                                                              
                         
                                                                              

struct DigitalTransition {
    std::size_t channel = 0;
    std::string name;
    double time_s = 0.0;                                             
    double relative_to_reference_s = 0.0;                                    
    std::uint8_t state = 0;
};

                                                           
std::vector<DigitalTransition> digitalTransitions(const ComtradeRecord& record,
                                                 double reference_s,
                                                 double half_window_s = 1.0);

                                                                              
                                    
                                                                              

struct AnalysisOptions {
    BreakerTimingOptions breaker;
    int cycles = 1;                                                              
    int max_harmonic_order = 15;
    double frequency_hz = 0.0;                                        
    double onset_ratio = 1.5;                                           
    std::optional<std::string> fault_distance_text;                             
    double reference_length_km = 0.0;                                         

                                                                               
                                                                                 
                                                   
    std::array<std::optional<std::size_t>, 3> phase_indices{};

                                                                              
                                                                   
    std::array<std::optional<std::size_t>, 3> voltage_indices{};

                                                                             
                                                                         
    std::optional<double> prefalla_center_s;
    std::optional<double> falla_center_s;
};

struct RegimeChange {
    double time_s = 0.0;                                                       
    std::array<int, 3> phase_direction{};                                  
    bool voltage_change_nearby = false;
    bool digital_transition_nearby = false;
};

struct RecordAnalysis {
    BreakerTimingResult breaker;
    bool ok = false;

                                                                 
    std::vector<Diagnostic> diagnostics;

    TimeAlignment time;
    PhaseSelection phases;

    int cycles = 1;
    double frequency_hz = 0.0;

    std::optional<OnsetEvent> event;
    std::vector<RegimeChange> regime_changes;

    double prefalla_center_s = 0.0;
    double falla_center_s = 0.0;
    double window_half_s = 0.0;
    bool prefalla_clamped = false;
    bool falla_clamped = false;
    bool windows_overlap = false;
                                                                               
    bool prefalla_usable = false;
    bool prefalla_manual = false;
    std::string prefalla_status;

    std::vector<WindowCoherenceRow> coherence_rows;
    std::vector<Diagnostic> coherence_errors;

    std::optional<std::array<Phasor, 3>> prefalla_phasors;            
    std::optional<std::array<Phasor, 3>> falla_phasors;
    std::optional<SequenceSet> prefalla_sequences;
    std::optional<SequenceSet> falla_sequences;
    SequenceRatios ratios;
    std::optional<FaultInterpretation> classification;

                                                                         
    std::array<HarmonicFit, 3> harmonics{};

                                                                               
    std::array<std::optional<SpectrumResult>, 3> spectra{};
    std::array<std::optional<SaturationIndicator>, 3> saturation{};

    VoltageAnalysis voltage;

    WaveformOrigin origin = WaveformOrigin::Midpoint;
    std::string origin_label;
    double event_origin_s = 0.0;

    std::optional<FaultDistanceResult> fault_distance;

    std::size_t sample_count = 0;
    std::size_t analog_channels = 0;

    bool hasError() const noexcept;
};

                                                                   
RecordAnalysis analyzeRecord(const ComtradeRecord& record,
                             const AnalysisOptions& options = {});

}                                   
