                                                                
  
                
                                                                             
                                                                  
                                                                              
                                                                                   
                                                                                
                                                       
                                                                     
                                                                               
                                                                            
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace analizador {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

                                                                            
                                                                              
                                                                     
struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;                                                            
    std::string message;                                                        
    std::string context;                                         

    std::size_t line = 0;                                                      
    bool has_line = false;

    std::size_t record = 0;                                                     
    bool has_record = false;

    std::uint32_t sample_number = 0;                                         
    bool has_sample_number = false;
};

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Cp1252,
    Utf16Le,
    Utf16Be,
};

const char* textEncodingName(TextEncoding encoding) noexcept;

enum class DataFileType {
    Ascii,
    Binary16,
    Binary32,
    Float32,
};

const char* dataFileTypeName(DataFileType type) noexcept;

                                                                         
struct DateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int microsecond = 0;
    bool valid = false;
};

                                                                         
                                                  
std::optional<double> dateTimeDifferenceSeconds(const DateTime& from, const DateTime& to) noexcept;

struct AnalogChannel {
    std::size_t position = 0;                                         
    std::uint32_t declared_index = 0;                                                     
    std::string name;
    std::string phase;
    std::string circuit_component;
    std::string unit;
    double a = 1.0;                                 
    double b = 0.0;                                         
    double skew_us = 0.0;
    double min_value = 0.0;
    double max_value = 0.0;
    double primary = 1.0;                                          
    double secondary = 1.0;                                        
    std::string ps = "S";                                       
};

struct DigitalChannel {
    std::size_t position = 0;
    std::uint32_t declared_index = 0;
    std::string name;
    std::string phase;
    std::string circuit_component;
    std::string normal_state;
};

struct SamplingRate {
    double rate_hz = 0.0;
    std::uint32_t end_sample = 0;                                       
};

struct CfgInfo {
    std::string station_name;
    std::string device_id;
    std::string revision;

                                                                                 
                                                                             
    int format_year = 0;

    std::size_t declared_total_channels = 0;
    std::vector<AnalogChannel> analog;
    std::vector<DigitalChannel> digital;

    double nominal_frequency_hz = 0.0;
    std::vector<SamplingRate> rates;                                      

    DateTime start_time;
    DateTime trigger_time;
    std::string start_time_text;                                         
    std::string trigger_time_text;                                       

    DataFileType data_type = DataFileType::Ascii;
    double time_multiplier = 1.0;
    TextEncoding encoding = TextEncoding::Utf8;

    std::size_t analogCount() const noexcept { return analog.size(); }
    std::size_t digitalCount() const noexcept { return digital.size(); }
};

                                                                               
struct SampleData {
    std::size_t analog_channels = 0;
    std::size_t digital_channels = 0;

    std::vector<std::uint32_t> sample_numbers;
    std::vector<double> dat_timestamp_us;                                         
    std::vector<double> times_s;                                            
    std::vector<double> analog;                                                    
    std::vector<std::uint8_t> digital;                                              

    std::size_t sampleCount() const noexcept { return sample_numbers.size(); }

                                                                                
    double analogValue(std::size_t sample, std::size_t channel) const noexcept {
        if (sample >= sampleCount() || channel >= analog_channels) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return analog[sample * analog_channels + channel];
    }

    std::uint8_t digitalValue(std::size_t sample, std::size_t channel) const noexcept {
        if (sample >= sampleCount() || channel >= digital_channels) {
            return 0;
        }
        return digital[sample * digital_channels + channel];
    }
};

struct ComtradeRecord {
    CfgInfo cfg;
    SampleData data;

                                                                            
                                                                     
    TextEncoding dat_encoding = TextEncoding::Utf8;

                                                                                 
    std::optional<double> trigger_offset_s;
    double first_sample_time_s = 0.0;
    bool first_sample_nonzero = false;
};

}                         
