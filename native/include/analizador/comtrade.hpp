                                        
  
                        
                                                                      
                                                      
                                                                    
                                                                             
                                                                               
                                                                              
  
                                                                          
             
                                                                             
                                                                      
                                                                               
                                                                               
                                                                                
                                                                               
                                                                               
  
                                                                           
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "analizador/model.hpp"

namespace analizador {

                                                                              
                                    
                                                                              

struct TextDecodeResult {
    bool ok = false;
    std::string text;                      
    TextEncoding encoding = TextEncoding::Utf8;
    std::vector<Diagnostic> diagnostics;
};

                                                                             
                                                                            
TextDecodeResult decodeComtradeText(const std::vector<std::uint8_t>& raw);

struct DateTimeParseResult {
    bool ok = false;
    bool ambiguous = false;                                      
    DateTime value;
    std::vector<Diagnostic> diagnostics;
};

                                                                          
                                                                         
                                                                             
DateTimeParseResult parseComtradeDateTime(const std::string& text);

                                                                              
                 
                                                                              

struct CfgParseOptions {
                                                                               
                                                                               
                                                                             
                                                                          
                          
    std::size_t max_text_bytes = 64ull * 1024ull * 1024ull;
                                                                              
                                                                             
    std::size_t max_lines = 1'000'000;
    std::size_t max_diagnostics = 256;
};

struct DatParseOptions {
                                                                          
    std::size_t max_samples = 10'000'000;
    std::size_t max_analog_cells = 50'000'000;
    std::size_t max_digital_cells = 50'000'000;
                                                                                
                                                                    
                                                                               
                                                        
    std::size_t max_ascii_line_bytes = 1ull * 1024ull * 1024ull;
    std::size_t max_diagnostics = 256;
};

struct ParseOptions {
    CfgParseOptions cfg;
    DatParseOptions dat;
};

struct CfgParseResult {
    bool ok = false;
    CfgInfo cfg;
    std::vector<Diagnostic> diagnostics;

    const Diagnostic* firstError() const noexcept {
        for (const auto& item : diagnostics) {
            if (item.severity == DiagnosticSeverity::Error) {
                return &item;
            }
        }
        return nullptr;
    }
};

                                                                                
                                                                         
                                                                               
                                                                         
CfgParseResult parseCfg(const std::vector<std::uint8_t>& cfg_bytes,
                        const CfgParseOptions& options = {});

struct DatParseResult {
    bool ok = false;
    SampleData data;
    TextEncoding encoding = TextEncoding::Utf8;                                               
    std::vector<Diagnostic> diagnostics;

    const Diagnostic* firstError() const noexcept {
        for (const auto& item : diagnostics) {
            if (item.severity == DiagnosticSeverity::Error) {
                return &item;
            }
        }
        return nullptr;
    }
};

DatParseResult parseDat(const std::vector<std::uint8_t>& dat_bytes,
                        const CfgInfo& cfg,
                        const DatParseOptions& options = {});

struct ComtradeParseResult {
    bool ok = false;
    ComtradeRecord record;
    std::vector<Diagnostic> diagnostics;

    const Diagnostic* firstError() const noexcept {
        for (const auto& item : diagnostics) {
            if (item.severity == DiagnosticSeverity::Error) {
                return &item;
            }
        }
        return nullptr;
    }
};

                                                                               
                                                                             
ComtradeParseResult parseComtrade(const std::vector<std::uint8_t>& cfg_bytes,
                                  const std::vector<std::uint8_t>& dat_bytes,
                                  const ParseOptions& options = {});

                                                                              
                                                               
                                                                              

namespace zip {

constexpr std::uint64_t kDefaultMaxEntries = 10'000;
constexpr std::uint64_t kDefaultMaxMemberBytes = 256ull * 1024ull * 1024ull;
constexpr std::uint64_t kDefaultMaxTotalBytes = 1024ull * 1024ull * 1024ull;
constexpr std::uint64_t kDefaultMaxCompressionRatio = 2'000ull;

struct Limits {
    std::uint64_t max_entries = kDefaultMaxEntries;
    std::uint64_t max_member_bytes = kDefaultMaxMemberBytes;
    std::uint64_t max_total_bytes = kDefaultMaxTotalBytes;
    std::uint64_t max_compression_ratio = kDefaultMaxCompressionRatio;
};

struct Entry {
    std::size_t index = 0;                                       
    std::string name;                                                       
    std::string normalized_name;                                          
    std::string normalized_key;                                           

    bool is_directory = false;
    bool encrypted = false;
    bool path_valid = true;
    bool supported_method = false;
    bool exceeds_member_limit = false;
    bool compression_ratio_invalid = false;

    std::uint16_t flags = 0;
    std::uint16_t method = 0;                         
    std::uint32_t crc32 = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t local_header_offset = 0;
};

struct Pair {
    std::size_t cfg_entry = 0;
    std::size_t dat_entry = 0;
    std::string cfg_name;
    std::string dat_name;
    std::string record_name;
    std::vector<std::string> aux_names;                                      
};

struct PairingResult {
    std::vector<Pair> pairs;
    std::vector<Diagnostic> failures;                             
};

struct ReadResult {
    bool ok = false;
    std::vector<std::uint8_t> data;
    std::vector<Diagnostic> diagnostics;

    const Diagnostic* firstError() const noexcept {
        for (const auto& item : diagnostics) {
            if (item.severity == DiagnosticSeverity::Error) {
                return &item;
            }
        }
        return nullptr;
    }
};

class Archive;

struct OpenResult {
    bool ok = false;
    std::unique_ptr<Archive> archive;
    std::vector<Diagnostic> diagnostics;
};

                                                                       
                                                      
std::string normalizeMemberName(const std::string& name);

                                                                         
                                                       
bool isIgnoredMember(const std::string& name);

class Archive {
public:
                                                                            
                                                                         
                                                                               
                                                                 
    static OpenResult openFromMemory(std::vector<std::uint8_t> bytes,
                                     const Limits& limits = {});
                                                                           
                                                                            
                                                   
    static OpenResult openFromFile(const std::filesystem::path& path,
                                   const Limits& limits = {});

    Archive(Archive&&) noexcept = default;
    Archive& operator=(Archive&&) noexcept = default;
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    const std::vector<Entry>& entries() const noexcept { return entries_; }
    const Limits& limits() const noexcept { return limits_; }

                                                                                   
                                                                  
                                                                           
    ReadResult readEntry(const Entry& entry) const;
    ReadResult readEntryByName(const std::string& name) const;

                                                                            
                                                               
    PairingResult pairRecords() const;

                                                                                
                                                                                  
    PairingResult recordBundles() const;

private:
    Archive(std::vector<std::uint8_t> bytes, Limits limits);

    std::vector<std::uint8_t> bytes_;
    Limits limits_;
    std::vector<Entry> entries_;
    std::vector<Diagnostic> index_diagnostics_;
};

}                  
}                         
