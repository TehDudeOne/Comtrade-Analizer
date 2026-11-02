                                                     
  
                                                                               
                                                                               
                                                   

#include "analizador/ui.hpp"
#include "analizador/runtime_guard.hpp"

#include "analizador/analysis.hpp"
#include "analizador/comtrade.hpp"
#include "analizador/report.hpp"

#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace analizador::winapp {
namespace {

constexpr wchar_t kMainClass[] = L"AnalizadorComtrade.MainWindow";
constexpr wchar_t kWaveClass[] = L"AnalizadorComtrade.Waveform";
constexpr UINT kMessageLoadDone = WM_APP + 41;

enum CommandId : UINT {
    kCommandOpen = 40001,
    kCommandOpenPair = 40002,
    kCommandExportHtml = 40003,
    kCommandExportCsv = 40004,
    kCommandExportJson = 40005,
    kCommandResetView = 40006,
    kCommandZoomIn = 40007,
    kCommandZoomOut = 40008,
    kCommandExit = 40009,
    kCommandAbout = 40010,
    kCommandAnalyzeBatch = 40011,
    kCommandCompare = 40012,
    kCommandExportPackage = 40013,
    kCommandApply = 40014,
    kCommandModeTrace = 40015,
    kCommandModePhasor = 40016,
    kCommandModeTrend = 40017,
    kCommandModeHarmonic = 40018,
    kCommandModeTimeline = 40019,
    kCommandExportBatchHtml = 40020,
    kCommandExportDiagnostics = 40021,
    kCommandBreakerInfo = 40022,
};

enum ControlId : UINT {
    kControlOpenButton = 50001,
    kControlHtmlButton = 50002,
    kControlCsvButton = 50003,
    kControlRecordCombo = 50004,
    kControlChannelList = 50005,
    kControlDiagnosticsList = 50006,
    kControlStatus = 50007,
    kControlRecordLabel = 50008,
    kControlChannelLabel = 50009,
    kControlDiagnosticsLabel = 50010,
    kControlPhaseA = 50011,
    kControlPhaseB = 50012,
    kControlPhaseC = 50013,
    kControlCycles = 50014,
    kControlOnset = 50015,
    kControlPre = 50016,
    kControlFault = 50017,
    kControlLineLength = 50018,
    kControlLogOffset = 50019,
    kControlUnitChannel = 50020,
    kControlUnitText = 50021,
    kControlTrip = 50022,
    kControlTripPolarity = 50023,
    kControlContact52a = 50024,
    kControlContact52aPolarity = 50025,
    kControlContact52b = 50026,
    kControlContact52bPolarity = 50027,
};

                                                                              
                   
                                                                              

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return L"(texto no disponible)";
    }
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), length);
    return out;
}

std::string wideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
        nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), length, nullptr, nullptr);
    return out;
}

std::string windowsErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring message;
    if (length && buffer) {
        message.assign(buffer, length);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' ||
                message.back() == L' ' || message.back() == L'\t')) {
            message.pop_back();
        }
    }
    if (buffer) {
        ::LocalFree(buffer);
    }
    if (message.empty()) {
        message = L"Error de Windows " + std::to_wstring(code);
    }
    return wideToUtf8(message);
}

std::wstring lowerWide(std::wstring text) {
    if (!text.empty()) {
        ::CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    }
    return text;
}

std::wstring formatNumberW(double value, int precision = 9) {
    if (!std::isfinite(value)) {
        return L"—";
    }
    char buffer[96];
    const auto converted = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::general,
        precision);
    if (converted.ec != std::errc{}) {
        return L"—";
    }
    return utf8ToWide(std::string_view(buffer,
                                       static_cast<std::size_t>(converted.ptr -
                                                                buffer)));
}

std::wstring formatFixedNumberW(double value, int precision = 6) {
    if (!std::isfinite(value)) {
        return L"—";
    }
    char buffer[128];
    const auto converted = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::fixed,
        std::clamp(precision, 0, 12));
    if (converted.ec != std::errc{}) {
        return L"—";
    }
    return utf8ToWide(std::string_view(buffer,
                                       static_cast<std::size_t>(converted.ptr -
                                                                buffer)));
}

std::wstring formatCountW(std::size_t value) {
    return std::to_wstring(value);
}

std::wstring pathFileName(const std::filesystem::path& path) {
    return path.filename().wstring();
}

std::wstring pathExtensionLower(const std::filesystem::path& path) {
    return lowerWide(path.extension().wstring());
}

std::wstring pathStemLower(const std::filesystem::path& path) {
    return lowerWide(path.stem().wstring());
}

std::wstring normalizedPathKey(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return lowerWide((error ? path : absolute).lexically_normal().wstring());
}

const wchar_t* severityW(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Error:
            return L"Error";
        case DiagnosticSeverity::Warning:
            return L"Advertencia";
        case DiagnosticSeverity::Info:
            return L"Información";
    }
    return L"Diagnóstico";
}

std::wstring diagnosticText(const Diagnostic& diagnostic) {
    std::wstring text = L"[";
    text += severityW(diagnostic.severity);
    text += L"] ";
    if (!diagnostic.code.empty()) {
        text += utf8ToWide(diagnostic.code);
        text += L": ";
    }
    text += utf8ToWide(diagnostic.message);
    if (!diagnostic.context.empty()) {
        text += L" — ";
        text += utf8ToWide(diagnostic.context);
    }
    if (diagnostic.has_line) {
        text += L" (línea ";
        text += formatCountW(diagnostic.line);
        text += L")";
    }
    if (diagnostic.has_record) {
        text += L" (muestra ";
        text += formatCountW(diagnostic.record + 1);
        text += L")";
    }
    return text;
}

std::string csvField(std::string_view value) {
    std::string out;
    const bool quote = value.find_first_of(",\"\r\n") !=
                       std::string_view::npos;
    if (quote) {
        out.push_back('"');
    }
    for (char ch : value) {
        if (ch == '"') {
            out.push_back('"');
        }
        out.push_back(ch);
    }
    if (quote) {
        out.push_back('"');
    }
    return out;
}

std::string_view diagnosticSeverityName(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Info:
            return "Información";
        case DiagnosticSeverity::Warning:
            return "Advertencia";
        case DiagnosticSeverity::Error:
            return "Error";
    }
    return "Diagnóstico";
}

const Diagnostic* firstError(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return &diagnostic;
        }
    }
    return nullptr;
}

std::string firstErrorText(const std::vector<Diagnostic>& diagnostics) {
    if (const Diagnostic* diagnostic = firstError(diagnostics)) {
        if (!diagnostic->message.empty()) {
            return diagnostic->message;
        }
        return diagnostic->code.empty() ? "Error sin detalle."
                                        : diagnostic->code;
    }
    return {};
}

Diagnostic makeDiagnostic(DiagnosticSeverity severity, std::string code,
                          std::string message) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    return diagnostic;
}

bool sameDiagnostic(const Diagnostic& left, const Diagnostic& right) {
    return left.severity == right.severity && left.code == right.code &&
           left.message == right.message && left.context == right.context &&
           left.has_line == right.has_line && left.line == right.line &&
           left.has_record == right.has_record &&
           left.record == right.record &&
           left.has_sample_number == right.has_sample_number &&
           left.sample_number == right.sample_number;
}

                                                                        
void appendUniqueDiagnostics(std::vector<Diagnostic>& destination,
                             const std::vector<Diagnostic>& extra) {
    for (const auto& diagnostic : extra) {
        const bool exists = std::any_of(
            destination.begin(), destination.end(),
            [&](const Diagnostic& existing) {
                return sameDiagnostic(existing, diagnostic);
            });
        if (!exists) {
            destination.push_back(diagnostic);
        }
    }
}

std::wstring sourceForDisplay(const std::wstring& source) {
    return source.empty() ? L"Archivo local" : source;
}

                                                                              
                   
                                                                              

struct RecordDescriptor {
    std::wstring label;
    std::wstring record_name;
    std::wstring source_description;
    bool from_zip = false;
    std::filesystem::path archive_path;
    std::string cfg_entry_name;
    std::string dat_entry_name;
    std::filesystem::path cfg_path;
    std::filesystem::path dat_path;
    std::vector<std::string> aux_entry_names;                                  
    std::vector<std::filesystem::path> aux_paths;                                   
};

struct LoadedRecord {
    RecordDescriptor descriptor;
    ComtradeRecord record;
    std::vector<Diagnostic> diagnostics;
    analysis::RecordAnalysis analysis;
    std::vector<Diagnostic> source_diagnostics;
    bool analysis_available = false;

                                                                             
    std::vector<report::EventLogRow> event_log;
    report::TimelineResult timeline;
    report::TrendResult trend;
    std::vector<Diagnostic> aux_diagnostics;
    std::map<std::size_t, std::string> unit_overrides;
    std::array<std::optional<std::size_t>, 3> phase_indices{};
    std::array<std::optional<std::size_t>, 3> voltage_indices{};
                                                                            
                                                                     
    analysis::BreakerTimingOptions breaker_options;
    int cycles = 1;
    double onset_ratio = 1.5;
    double prefalla_center_s = 0.0;
    double falla_center_s = 0.0;
    bool prefalla_manual = false;
    bool falla_manual = false;
    double reference_length_km = 0.0;
    double log_clock_offset_s = 0.0;
    std::string fault_distance_text;
};

struct LoadResult {
    bool ok = false;
    std::string error;
    std::shared_ptr<LoadedRecord> loaded;
    std::vector<Diagnostic> diagnostics;
};

struct OpenBundle {
    std::vector<RecordDescriptor> records;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

bool readFileBytes(const std::filesystem::path& path,
                   std::vector<std::uint8_t>& bytes, std::string& error) {
    bytes.clear();
    HANDLE handle = ::CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                                        FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = windowsErrorText(::GetLastError());
        return false;
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(handle, &size)) {
        error = windowsErrorText(::GetLastError());
        ::CloseHandle(handle);
        return false;
    }
    if (size.QuadPart < 0 ||
        static_cast<std::uint64_t>(size.QuadPart) >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
        error = "El archivo es demasiado grande para la memoria del proceso.";
        ::CloseHandle(handle);
        return false;
    }
    const std::size_t total = static_cast<std::size_t>(size.QuadPart);
    bytes.resize(total);
    std::size_t offset = 0;
    while (offset < total) {
        const std::size_t remaining = total - offset;
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(remaining, 16ull * 1024ull * 1024ull));
        DWORD read = 0;
        if (!::ReadFile(handle, bytes.data() + offset, chunk, &read, nullptr)) {
            error = windowsErrorText(::GetLastError());
            ::CloseHandle(handle);
            bytes.clear();
            return false;
        }
        if (read == 0) {
            error = "El archivo terminó antes de lo declarado.";
            ::CloseHandle(handle);
            bytes.clear();
            return false;
        }
        offset += read;
    }
    ::CloseHandle(handle);
    return true;
}

bool containsInsensitive(std::string_view text, std::string_view needle) {
    if (needle.empty() || needle.size() > text.size()) {
        return false;
    }
    for (std::size_t index = 0; index + needle.size() <= text.size(); ++index) {
        bool match = true;
        for (std::size_t offset = 0; offset < needle.size(); ++offset) {
            const unsigned char left =
                static_cast<unsigned char>(text[index + offset]);
            const unsigned char right =
                static_cast<unsigned char>(needle[offset]);
            const unsigned char lower_left =
                (left >= 'A' && left <= 'Z') ? left - 'A' + 'a' : left;
            const unsigned char lower_right =
                (right >= 'A' && right <= 'Z') ? right - 'A' + 'a' : right;
            if (lower_left != lower_right) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

LoadResult loadRecord(const RecordDescriptor& descriptor,
                      const analysis::AnalysisOptions& analysis_options) {
    LoadResult result;
    std::vector<std::uint8_t> cfg_bytes;
    std::vector<std::uint8_t> dat_bytes;
    std::vector<Diagnostic> diagnostics;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> aux_files;

    if (descriptor.from_zip) {
        auto archive = zip::Archive::openFromFile(descriptor.archive_path);
        diagnostics.insert(diagnostics.end(), archive.diagnostics.begin(),
                           archive.diagnostics.end());
        if (!archive.ok || !archive.archive) {
            result.diagnostics = std::move(diagnostics);
            result.error = firstErrorText(result.diagnostics);
            if (result.error.empty()) {
                result.error = "No se pudo abrir el ZIP.";
            }
            return result;
        }

        auto cfg = archive.archive->readEntryByName(descriptor.cfg_entry_name);
        auto dat = archive.archive->readEntryByName(descriptor.dat_entry_name);
        diagnostics.insert(diagnostics.end(), cfg.diagnostics.begin(),
                           cfg.diagnostics.end());
        diagnostics.insert(diagnostics.end(), dat.diagnostics.begin(),
                           dat.diagnostics.end());
        if (!cfg.ok || !dat.ok) {
            result.diagnostics = std::move(diagnostics);
            result.error = firstErrorText(result.diagnostics);
            if (result.error.empty()) {
                result.error = "No se pudieron leer el CFG y el DAT del ZIP.";
            }
            return result;
        }
        cfg_bytes = std::move(cfg.data);
        dat_bytes = std::move(dat.data);
        for (const auto& name : descriptor.aux_entry_names) {
            auto aux = archive.archive->readEntryByName(name);
            if (!aux.ok) {
                diagnostics.insert(diagnostics.end(), aux.diagnostics.begin(),
                                   aux.diagnostics.end());
                continue;
            }
            aux_files.emplace_back(name, std::move(aux.data));
        }
    } else {
        std::string error;
        if (!readFileBytes(descriptor.cfg_path, cfg_bytes, error)) {
            result.error = "No se pudo leer el CFG: " + error;
            return result;
        }
        if (!readFileBytes(descriptor.dat_path, dat_bytes, error)) {
            result.error = "No se pudo leer el DAT: " + error;
            return result;
        }
        for (const auto& path : descriptor.aux_paths) {
            std::vector<std::uint8_t> bytes;
            if (readFileBytes(path, bytes, error)) {
                aux_files.emplace_back(wideToUtf8(path.filename().wstring()),
                                       std::move(bytes));
            } else {
                diagnostics.push_back(makeDiagnostic(
                    DiagnosticSeverity::Warning, "ui.aux.read",
                    "No se pudo leer el auxiliar " +
                        wideToUtf8(path.filename().wstring()) + ": " + error));
            }
        }
    }

    auto parsed = parseComtrade(cfg_bytes, dat_bytes);
    diagnostics.insert(diagnostics.end(), parsed.diagnostics.begin(),
                       parsed.diagnostics.end());
    auto loaded = std::make_shared<LoadedRecord>();
    loaded->descriptor = descriptor;
    loaded->record = std::move(parsed.record);

                                                                                
                                                           
    for (const auto& aux : aux_files) {
        const std::wstring name = lowerWide(utf8ToWide(aux.first));
        if (name.size() < 4 ||
            name.substr(name.size() - 4) != L".csv") {
            continue;
        }
        auto log = report::parseEventLogCsv(aux.second, aux.first);
        loaded->aux_diagnostics.insert(loaded->aux_diagnostics.end(),
                                       log.diagnostics.begin(),
                                       log.diagnostics.end());
        for (auto& row : log.rows) {
            loaded->event_log.push_back(std::move(row));
        }
    }
    if (const auto distance =
            report::selectFaultDistanceText(loaded->event_log)) {
        loaded->fault_distance_text = *distance;
    }
    diagnostics.insert(diagnostics.end(), loaded->aux_diagnostics.begin(),
                       loaded->aux_diagnostics.end());
    loaded->source_diagnostics = diagnostics;

    if (parsed.ok) {
        analysis::AnalysisOptions options = analysis_options;
        if (!options.fault_distance_text.has_value() &&
            !loaded->fault_distance_text.empty()) {
            options.fault_distance_text = loaded->fault_distance_text;
        }
        loaded->analysis = analysis::analyzeRecord(loaded->record, options);
        loaded->analysis_available = true;
        loaded->cycles = loaded->analysis.cycles;
        loaded->prefalla_center_s = loaded->analysis.prefalla_center_s;
        loaded->falla_center_s = loaded->analysis.falla_center_s;
        loaded->phase_indices = loaded->analysis.phases.index;
        loaded->voltage_indices = loaded->analysis.voltage.selection.index;
        loaded->onset_ratio = options.onset_ratio;
        loaded->reference_length_km = options.reference_length_km;
        loaded->breaker_options = options.breaker;
        loaded->prefalla_manual = options.prefalla_center_s.has_value();
        loaded->falla_manual = options.falla_center_s.has_value();
        loaded->timeline = report::buildTimeline(
            loaded->record, &loaded->analysis, loaded->event_log,
            loaded->log_clock_offset_s);
        loaded->trend = report::rollingTrend(
            loaded->record, loaded->phase_indices, loaded->cycles, 600);
        diagnostics.insert(diagnostics.end(),
                           loaded->analysis.diagnostics.begin(),
                           loaded->analysis.diagnostics.end());
        diagnostics.insert(diagnostics.end(),
                           loaded->timeline.diagnostics.begin(),
                           loaded->timeline.diagnostics.end());
        diagnostics.insert(diagnostics.end(), loaded->trend.diagnostics.begin(),
                           loaded->trend.diagnostics.end());
        appendUniqueDiagnostics(diagnostics,
                                loaded->analysis.breaker.diagnostics);
    }
    loaded->diagnostics = diagnostics;

    result.ok = parsed.ok;
    result.loaded = std::move(loaded);
    result.diagnostics = std::move(diagnostics);
    if (!result.ok) {
        result.error = firstErrorText(result.diagnostics);
        if (result.error.empty()) {
            result.error = "El registro COMTRADE no pudo interpretarse.";
        }
    }
    return result;
}

std::filesystem::path findSibling(const std::filesystem::path& source,
                                  const wchar_t* extension) {
    std::error_code error;
    std::filesystem::path direct = source;
    direct.replace_extension(extension);
    if (std::filesystem::is_regular_file(direct, error)) {
        return direct;
    }

    const std::filesystem::path directory =
        source.parent_path().empty() ? std::filesystem::path(L".")
                                     : source.parent_path();
    const std::wstring wanted_extension = lowerWide(extension);
    const std::wstring wanted_stem = pathStemLower(source);
    error.clear();
    std::filesystem::directory_iterator iterator(directory, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        const auto& candidate = iterator->path();
        std::error_code file_error;
        if (lowerWide(candidate.extension().wstring()) == wanted_extension &&
            pathStemLower(candidate) == wanted_stem &&
            std::filesystem::is_regular_file(candidate, file_error)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::filesystem::path> findAuxiliaryFiles(
    const std::filesystem::path& source, const std::wstring& stem) {
    std::vector<std::filesystem::path> result;
    const std::filesystem::path directory =
        source.parent_path().empty() ? std::filesystem::path(L".")
                                     : source.parent_path();
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        const auto& candidate = iterator->path();
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(candidate, file_error)) {
            continue;
        }
        const std::wstring extension = pathExtensionLower(candidate);
        if (extension != L".hdr" && extension != L".inf" &&
            extension != L".csv") {
            continue;
        }
        if (pathStemLower(candidate) == lowerWide(stem)) {
            result.push_back(candidate);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const std::filesystem::path& left,
                 const std::filesystem::path& right) {
                  return lowerWide(left.filename().wstring()) <
                         lowerWide(right.filename().wstring());
              });
    return result;
}

std::vector<std::filesystem::path> findAllAuxiliaryFiles(
    const std::filesystem::path& source) {
    std::vector<std::filesystem::path> result;
    const std::filesystem::path directory =
        source.parent_path().empty() ? std::filesystem::path(L".")
                                     : source.parent_path();
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        const auto& candidate = iterator->path();
        std::error_code file_error;
        if (!std::filesystem::is_regular_file(candidate, file_error)) {
            continue;
        }
        const std::wstring extension = pathExtensionLower(candidate);
        if (extension == L".hdr" || extension == L".inf" ||
            extension == L".csv") {
            result.push_back(candidate);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const std::filesystem::path& left,
                 const std::filesystem::path& right) {
                  return lowerWide(left.filename().wstring()) <
                         lowerWide(right.filename().wstring());
              });
    return result;
}

std::wstring directPairKey(const std::filesystem::path& path) {
    const std::filesystem::path parent =
        path.parent_path().empty() ? std::filesystem::path(L".")
                                   : path.parent_path();
    return normalizedPathKey(parent) + L"|" + pathStemLower(path);
}

OpenBundle buildZipBundle(const std::filesystem::path& path) {
    OpenBundle bundle;
    auto opened = zip::Archive::openFromFile(path);
    bundle.diagnostics.insert(bundle.diagnostics.end(),
                              opened.diagnostics.begin(),
                              opened.diagnostics.end());
    if (!opened.ok || !opened.archive) {
        bundle.error = firstErrorText(bundle.diagnostics);
        if (bundle.error.empty()) {
            bundle.error = "No se pudo abrir el ZIP.";
        }
        return bundle;
    }

    auto pairing = opened.archive->recordBundles();
    bundle.diagnostics.insert(bundle.diagnostics.end(),
                              pairing.failures.begin(),
                              pairing.failures.end());
    if (pairing.pairs.empty()) {
        if (bundle.diagnostics.empty()) {
            bundle.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "ui.zip.no_pairs",
                "El ZIP no contiene pares CFG/DAT utilizables."));
        }
        bundle.error = firstErrorText(bundle.diagnostics);
        if (bundle.error.empty()) {
            bundle.error = "El ZIP no contiene pares CFG/DAT utilizables.";
        }
        return bundle;
    }

    std::set<std::wstring> labels;
    for (const auto& pair : pairing.pairs) {
        RecordDescriptor descriptor;
        descriptor.from_zip = true;
        descriptor.archive_path = path;
        descriptor.cfg_entry_name = pair.cfg_name;
        descriptor.dat_entry_name = pair.dat_name;
        descriptor.aux_entry_names = pair.aux_names;
        descriptor.record_name = utf8ToWide(pair.record_name);
        if (descriptor.record_name.empty()) {
            descriptor.record_name = pathFileName(path);
        }
        descriptor.label = descriptor.record_name;
        const std::wstring lower = lowerWide(descriptor.label);
        if (labels.count(lower) != 0) {
            std::size_t suffix = 2;
            do {
                descriptor.label = descriptor.record_name + L" [" +
                                   formatCountW(suffix++) + L"]";
            } while (labels.count(lowerWide(descriptor.label)) != 0);
        }
        labels.insert(lowerWide(descriptor.label));
        descriptor.source_description =
            pathFileName(path) + L" :: " + descriptor.record_name + L" (" +
            utf8ToWide(pair.cfg_name) + L" + " +
            utf8ToWide(pair.dat_name) + L")";
        bundle.records.push_back(std::move(descriptor));
    }
    return bundle;
}

void appendUniquePath(std::vector<std::filesystem::path>& destination,
                      const std::filesystem::path& candidate,
                      std::set<std::wstring>& seen) {
    const std::wstring key = normalizedPathKey(candidate);
    if (key.empty() || !seen.insert(key).second) {
        return;
    }
    destination.push_back(candidate);
}

OpenBundle buildDirectBundle(
    const std::vector<std::filesystem::path>& selected_paths) {
    OpenBundle bundle;
    std::map<std::wstring, std::vector<std::filesystem::path>> cfgs;
    std::map<std::wstring, std::vector<std::filesystem::path>> dats;
    std::map<std::wstring, std::wstring> display_stems;
    std::set<std::wstring> seen;

    for (const auto& path : selected_paths) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            bundle.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "ui.open.missing",
                "No existe o no es un archivo: " +
                    wideToUtf8(path.wstring())));
            continue;
        }
        const std::wstring extension = pathExtensionLower(path);
        const std::wstring key = directPairKey(path);
        if (extension == L".cfg") {
            appendUniquePath(cfgs[key], path, seen);
            display_stems[key] = path.stem().wstring();
        } else if (extension == L".dat") {
            appendUniquePath(dats[key], path, seen);
            display_stems[key] = path.stem().wstring();
        } else {
            bundle.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "ui.open.extension",
                "Se ignoró un archivo que no es CFG/DAT/ZIP: " +
                    wideToUtf8(pathFileName(path))));
        }
    }

    for (auto& item : cfgs) {
        if (dats[item.first].empty()) {
            const auto sibling = findSibling(item.second.front(), L".dat");
            if (!sibling.empty()) {
                appendUniquePath(dats[item.first], sibling, seen);
            }
        }
    }
    for (auto& item : dats) {
        if (cfgs[item.first].empty()) {
            const auto sibling = findSibling(item.second.front(), L".cfg");
            if (!sibling.empty()) {
                appendUniquePath(cfgs[item.first], sibling, seen);
            }
        }
    }

    std::set<std::wstring> keys;
    for (const auto& item : cfgs) {
        keys.insert(item.first);
    }
    for (const auto& item : dats) {
        keys.insert(item.first);
    }

    for (const auto& key : keys) {
        const auto& cfg_list = cfgs[key];
        const auto& dat_list = dats[key];
        const std::wstring display = display_stems[key].empty() ? key
                                                                : display_stems[key];
        if (cfg_list.empty()) {
            bundle.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "ui.direct.missing_cfg",
                "El DAT " + wideToUtf8(display) +
                    " no tiene un CFG con el mismo nombre base."));
            continue;
        }
        if (dat_list.empty()) {
            bundle.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "ui.direct.missing_dat",
                "El CFG " + wideToUtf8(display) +
                    " no tiene un DAT hermano localizable."));
            continue;
        }
        if (cfg_list.size() > 1 || dat_list.size() > 1) {
            bundle.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "ui.direct.ambiguous",
                "Hay más de un CFG o DAT para el nombre base " +
                    wideToUtf8(display) + "; la asociación es ambigua."));
            continue;
        }

        RecordDescriptor descriptor;
        descriptor.record_name = display;
        descriptor.label = display;
        descriptor.cfg_path = cfg_list.front();
        descriptor.dat_path = dat_list.front();
        descriptor.aux_paths =
            findAuxiliaryFiles(descriptor.cfg_path, descriptor.record_name);
        descriptor.source_description =
            pathFileName(descriptor.cfg_path) + L" + " +
            pathFileName(descriptor.dat_path);
        bundle.records.push_back(std::move(descriptor));
    }

                                                                           
                                                                           
    if (bundle.records.size() == 1) {
        bundle.records.front().aux_paths =
            findAllAuxiliaryFiles(bundle.records.front().cfg_path);
    }

    std::sort(bundle.records.begin(), bundle.records.end(),
              [](const RecordDescriptor& left, const RecordDescriptor& right) {
                  return lowerWide(left.label) < lowerWide(right.label);
              });
    std::set<std::wstring> used_labels;
    for (auto& record : bundle.records) {
        const std::wstring base = record.label;
        std::size_t suffix = 2;
        while (!used_labels.insert(lowerWide(record.label)).second) {
            record.label = base + L" [" + formatCountW(suffix++) + L"]";
        }
    }

    if (bundle.records.empty()) {
        if (bundle.diagnostics.empty()) {
            bundle.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "ui.direct.no_pairs",
                "No se encontró un par CFG/DAT utilizable."));
        }
        bundle.error = firstErrorText(bundle.diagnostics);
        if (bundle.error.empty()) {
            bundle.error = "No se encontró un par CFG/DAT utilizable.";
        }
    }
    return bundle;
}

int dpiForWindow(HWND window) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto function = reinterpret_cast<GetDpiForWindowFn>(
        ::GetProcAddress(::GetModuleHandleW(L"user32.dll"),
                         "GetDpiForWindow"));
    if (function && window) {
        const UINT dpi = function(window);
        if (dpi != 0) {
            return static_cast<int>(dpi);
        }
    }
    HDC screen = ::GetDC(nullptr);
    const int dpi = screen ? ::GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen) {
        ::ReleaseDC(nullptr, screen);
    }
    return dpi > 0 ? dpi : 96;
}

int scaleFor(int dpi, int value) {
    return ::MulDiv(value, dpi, 96);
}

HFONT makeFont(int dpi, int points, int weight) {
    LOGFONTW font{};
    font.lfHeight = -::MulDiv(points, dpi, 72);
    font.lfWeight = weight;
    font.lfQuality = CLEARTYPE_QUALITY;
    font.lfCharSet = DEFAULT_CHARSET;
    ::lstrcpynW(font.lfFaceName, L"Segoe UI", LF_FACESIZE);
    return ::CreateFontIndirectW(&font);
}

void setControlFont(HWND control, HFONT font) {
    if (control) {
        ::SendMessageW(control, WM_SETFONT,
                       reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void drawText(HDC dc, std::wstring_view text, RECT rectangle, HFONT font,
              COLORREF color, UINT format) {
    if (text.empty()) {
        return;
    }
    const HGDIOBJ old_font = ::SelectObject(dc, font);
    const COLORREF old_color = ::SetTextColor(dc, color);
    const int old_mode = ::SetBkMode(dc, TRANSPARENT);
    ::DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rectangle,
                format | DT_NOPREFIX);
    ::SetBkMode(dc, old_mode);
    ::SetTextColor(dc, old_color);
    ::SelectObject(dc, old_font);
}

class WaitCursor {
public:
    WaitCursor()
        : previous_(::SetCursor(::LoadCursorW(nullptr, IDC_WAIT))) {}
    ~WaitCursor() { ::SetCursor(previous_); }
    WaitCursor(const WaitCursor&) = delete;
    WaitCursor& operator=(const WaitCursor&) = delete;

private:
    HCURSOR previous_ = nullptr;
};

                                                                              
                                                                       
                                                                              

enum class ViewMode { Trace, Phasor, Trend, Harmonic, Timeline };

class WaveformView {
public:
    static bool registerClass(HINSTANCE instance) {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_DBLCLKS;
        window_class.lpfnWndProc = &WaveformView::windowProc;
        window_class.hInstance = instance;
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
        window_class.lpszClassName = kWaveClass;
        return ::RegisterClassExW(&window_class) != 0 ||
               ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool create(HWND parent, HINSTANCE instance, int control_id) {
        instance_ = instance;
        hwnd_ = ::CreateWindowExW(
            0, kWaveClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0,
            parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
            instance, this);
        return hwnd_ != nullptr;
    }

    HWND hwnd() const noexcept { return hwnd_; }

    void setRecord(const LoadedRecord* record) {
        record_ = record;
        channel_ = 0;
        timeline_scroll_rows_ = 0;
        phasor_zoom_ = 1.0;
        phasor_offset_x_ = 0.0;
        phasor_offset_y_ = 0.0;
        harmonic_zoom_ = 1.0;
        has_harmonic_center_ = false;
        has_view_ = false;
        cursor_valid_ = false;
        dragging_ = false;
        ensureView();
        if (hwnd_) {
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void setChannel(std::size_t channel) {
        if (!record_ || record_->record.cfg.analog.empty()) {
            channel_ = 0;
        } else {
            channel_ = std::min(channel,
                                record_->record.cfg.analog.size() - 1);
        }
        cursor_valid_ = false;
        if (hwnd_) {
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void setMode(ViewMode mode) {
        mode_ = mode;
        if (mode_ == ViewMode::Timeline) {
            timeline_scroll_rows_ = 0;
        }
        cursor_valid_ = false;
        if (hwnd_) {
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    ViewMode mode() const noexcept { return mode_; }

    void resetView() {
        has_view_ = false;
        phasor_zoom_ = 1.0;
        phasor_offset_x_ = 0.0;
        phasor_offset_y_ = 0.0;
        harmonic_zoom_ = 1.0;
        has_harmonic_center_ = false;
        ensureView();
        if (hwnd_) {
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void zoomAtCenter(double factor) {
        if (!record_ || record_->record.data.times_s.empty()) {
            return;
        }
        ensureView();
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const RECT plot = plotRect(client);
        const int center_x = (plot.left + plot.right) / 2;
        const int center_y = (plot.top + plot.bottom) / 2 + scaleFor(dpiForWindow(hwnd_), 12);
        zoomAtPixel(center_x, center_y, factor);
    }

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) {
        WaveformView* self = reinterpret_cast<WaveformView*>(
            ::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<WaveformView*>(create->lpCreateParams);
            if (self) {
                self->hwnd_ = window;
                ::SetWindowLongPtrW(window, GWLP_USERDATA,
                                    reinterpret_cast<LONG_PTR>(self));
            }
        }
        if (self) {
            return self->handleMessage(message, wparam, lparam);
        }
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_ERASEBKGND:
                return 1;
            case WM_SIZE:
                ::InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            case WM_PAINT:
                paint();
                return 0;
            case WM_MOUSEMOVE:
                onMouseMove(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam),
                            wparam);
                return 0;
            case WM_MOUSELEAVE:
                if (!dragging_) {
                    cursor_valid_ = false;
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return 0;
            case WM_LBUTTONDOWN:
                onLeftDown(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            case WM_LBUTTONUP:
                if (dragging_) {
                    dragging_ = false;
                    ::ReleaseCapture();
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return 0;
            case WM_CAPTURECHANGED:
                dragging_ = false;
                ::InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            case WM_LBUTTONDBLCLK:
                resetView();
                return 0;
            case WM_MOUSEWHEEL:
                onMouseWheel(GET_WHEEL_DELTA_WPARAM(wparam),
                             GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            case WM_SETCURSOR:
                if (LOWORD(lparam) == HTCLIENT) {
                    POINT point{};
                    ::GetCursorPos(&point);
                    ::ScreenToClient(hwnd_, &point);
                    RECT client{};
                    ::GetClientRect(hwnd_, &client);
                    const RECT plot = plotRect(client);
                    if (dragging_) {
                        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEWE));
                    } else if (::PtInRect(&plot, point)) {
                        ::SetCursor(::LoadCursorW(nullptr, IDC_CROSS));
                    } else {
                        ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
                    }
                    return TRUE;
                }
                break;
            case WM_DESTROY:
                releaseFonts();
                return 0;
            case WM_NCDESTROY: {
                HWND window = hwnd_;
                ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                hwnd_ = nullptr;
                return ::DefWindowProcW(window, message, wparam, lparam);
            }
            default:
                break;
        }
        return ::DefWindowProcW(hwnd_, message, wparam, lparam);
    }

    std::vector<std::size_t> traceChannels() const {
        std::vector<std::size_t> channels;
        if (!record_ || channel_ >= record_->record.cfg.analog.size() ||
            channel_ >= record_->record.data.analog_channels) {
            return channels;
        }
        channels.push_back(channel_);
        const std::string unit = displayUnitFor(channel_);
        for (const auto& index : record_->phase_indices) {
            if (index && *index < record_->record.cfg.analog.size() &&
                *index < record_->record.data.analog_channels &&
                displayUnitFor(*index) == unit &&
                std::find(channels.begin(), channels.end(), *index) == channels.end()) {
                channels.push_back(*index);
            }
        }
        return channels;
    }

    COLORREF traceChannelColor(std::size_t channel) const {
        if (record_) {
            for (std::size_t phase = 0; phase < 3; ++phase) {
                if (record_->phase_indices[phase] == channel) {
                    return channelColor(phase);
                }
            }
        }
                                                                               
        return channelColor(6);
    }

    std::wstring traceChannelLabel(std::size_t channel) const {
        const auto& cfg = record_->record.cfg.analog[channel];
        std::wstring label = L"#" + formatCountW(channel + 1) + L" · ";
        label += cfg.name.empty() ? L"Canal sin nombre" : utf8ToWide(cfg.name);
        const std::string unit = displayUnitFor(channel);
        label += L" [" + (unit.empty() ? std::wstring(L"unidad no declarada")
                                        : utf8ToWide(unit)) + L"]";
        bool assigned_phase = false;
        for (std::size_t phase = 0; phase < 3; ++phase) {
            if (record_->phase_indices[phase] == channel) {
                label += L" · fase ";
                label += static_cast<wchar_t>(L'A' + phase);
                assigned_phase = true;
                break;
            }
        }
        if (!assigned_phase) {
            label += L" · canal adicional (fuera del trío A/B/C)";
        }
        if (channel == channel_) {
            label += L" · seleccionado";
        }
        if (unit != cfg.unit) {
            label += L" · unidad CFG: " + utf8ToWide(cfg.unit) +
                     L" (etiqueta modificada, sin conversión)";
        }
        return label;
    }

    std::wstring harmonicWindowLabel() const {
        if (!record_ || !record_->analysis_available) return {};
        const auto& a = record_->analysis;
        return L"Armónicos: " + formatFixedNumberW(a.falla_center_s - a.window_half_s, 6) +
            L" a " + formatFixedNumberW(a.falla_center_s + a.window_half_s, 6) +
            L" s desde inicio DAT · " + formatCountW(a.cycles) + L" ciclo(s), " +
            formatFixedNumberW(2000.0 * a.window_half_s, 3) + L" ms";
    }

    struct TraceLegendRow {
        std::size_t channel;
        std::wstring label;
        RECT bounds;
    };

    std::vector<TraceLegendRow> traceLegendRows(const RECT& client) const {
        const int dpi = dpiForWindow(hwnd_);
        const int left = scaleFor(dpi, 128);
        const int right = std::max(left + scaleFor(dpi, 80),
                                   static_cast<int>(client.right) - scaleFor(dpi, 24));
        int top = scaleFor(dpi, 82);
        std::vector<TraceLegendRow> rows;
        HDC measure_dc = ::GetDC(hwnd_);
        HGDIOBJ old_font = nullptr;
        if (measure_dc && small_font_) {
            old_font = ::SelectObject(measure_dc, small_font_);
        }
        for (const auto channel : traceChannels()) {
            auto label = traceChannelLabel(channel);
            RECT measured{0, 0, right - left, 0};
            if (measure_dc) {
                ::DrawTextW(measure_dc, label.c_str(), -1, &measured,
                            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            }
            const int height = std::max(scaleFor(dpi, 22),
                static_cast<int>(measured.bottom) + scaleFor(dpi, 6));
            rows.push_back({channel, std::move(label),
                            RECT{left, top, right, top + height}});
            top += height;
        }
        if (old_font) {
            ::SelectObject(measure_dc, old_font);
        }
        if (measure_dc) {
            ::ReleaseDC(hwnd_, measure_dc);
        }
        return rows;
    }

    void paintTraceLegend(HDC dc, const RECT& client) {
        const int dpi = dpiForWindow(hwnd_);
        for (const auto& row : traceLegendRows(client)) {
            const COLORREF color = traceChannelColor(row.channel);
            const HPEN pen = ::CreatePen(PS_SOLID, scaleFor(dpi, 3), color);
            const HGDIOBJ old_pen = ::SelectObject(dc, pen);
            const int y = row.bounds.top + scaleFor(dpi, 10);
            ::MoveToEx(dc, row.bounds.left - scaleFor(dpi, 34), y, nullptr);
            ::LineTo(dc, row.bounds.left - scaleFor(dpi, 8), y);
            ::SelectObject(dc, old_pen);
            ::DeleteObject(pen);
            drawText(dc, row.label, row.bounds, small_font_, color,
                     DT_WORDBREAK | DT_LEFT | DT_NOPREFIX);
        }
        const auto rows = traceLegendRows(client);
        if (!rows.empty() && record_ && record_->analysis_available) {
            const int top = rows.back().bounds.bottom;
            RECT swatch{scaleFor(dpi, 94), top + scaleFor(dpi, 6),
                        scaleFor(dpi, 119), top + scaleFor(dpi, 17)};
            HBRUSH brush = ::CreateSolidBrush(RGB(255, 235, 183));
            ::FillRect(dc, &swatch, brush);
            ::DeleteObject(brush);
            drawText(dc, harmonicWindowLabel(),
                RECT{scaleFor(dpi, 128), top, client.right - scaleFor(dpi, 24),
                     top + scaleFor(dpi, 25)}, small_font_, RGB(100, 70, 18),
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
    }

    RECT plotRect(const RECT& client) const {
        const int dpi = dpiForWindow(hwnd_);
        RECT plot{};
        plot.left = scaleFor(dpi, 88);
        plot.top = scaleFor(dpi, 82);
        if (mode_ == ViewMode::Trace) {
            const auto rows = traceLegendRows(client);
            if (!rows.empty()) {
                plot.top = rows.back().bounds.bottom + scaleFor(dpi, 33);
            }
        }
        plot.right = std::max(plot.left + 1, client.right - scaleFor(dpi, 24));
        plot.bottom =
            std::max(plot.top + 1, client.bottom - scaleFor(dpi, 52));
        return plot;
    }

    void ensureFonts() {
        const int dpi = dpiForWindow(hwnd_);
        if (dpi == font_dpi_ && title_font_ && body_font_ && small_font_) {
            return;
        }
        releaseFonts();
        font_dpi_ = dpi;
        title_font_ = makeFont(dpi, 10, FW_SEMIBOLD);
        body_font_ = makeFont(dpi, 9, FW_NORMAL);
        small_font_ = makeFont(dpi, 8, FW_NORMAL);
    }

    void releaseFonts() {
        if (title_font_) {
            ::DeleteObject(title_font_);
            title_font_ = nullptr;
        }
        if (body_font_) {
            ::DeleteObject(body_font_);
            body_font_ = nullptr;
        }
        if (small_font_) {
            ::DeleteObject(small_font_);
            small_font_ = nullptr;
        }
        font_dpi_ = 0;
    }

    void ensureView() {
        if (!record_ || record_->record.data.times_s.empty()) {
            view_lo_ = 0.0;
            view_hi_ = 1.0;
            has_view_ = false;
            return;
        }
        const double first = record_->record.data.times_s.front();
        const double last = record_->record.data.times_s.back();
        if (!has_view_ || !(view_hi_ > view_lo_) ||
            view_lo_ < first - 1e-12 || view_hi_ > last + 1e-12) {
            view_lo_ = first;
            view_hi_ = last;
            if (!(view_hi_ > view_lo_)) {
                view_lo_ -= 0.5;
                view_hi_ += 0.5;
            }
            has_view_ = true;
        }
    }

    bool hasValidTrigger() const {
        if (!record_ || record_->record.data.times_s.empty() ||
            !record_->record.trigger_offset_s) {
            return false;
        }
        const double trigger = *record_->record.trigger_offset_s;
        return std::isfinite(trigger) &&
               trigger >= record_->record.data.times_s.front() &&
               trigger <= record_->record.data.times_s.back();
    }

    double timeReference() const {
        if (!record_ || record_->record.data.times_s.empty()) {
            return 0.0;
        }
        if (hasValidTrigger()) {
            return *record_->record.trigger_offset_s;
        }
        return record_->record.data.times_s.front();
    }

    void paint() {
        PAINTSTRUCT paint_struct{};
        HDC target = ::BeginPaint(hwnd_, &paint_struct);
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) {
            ::EndPaint(hwnd_, &paint_struct);
            return;
        }

        HDC dc = ::CreateCompatibleDC(target);
        HBITMAP bitmap = ::CreateCompatibleBitmap(target, width, height);
        const HGDIOBJ old_bitmap = ::SelectObject(dc, bitmap);
        const auto present = [&]() {
            ::BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        };
        ::SetBkMode(dc, TRANSPARENT);
        ensureFonts();

        const HBRUSH background = ::CreateSolidBrush(RGB(252, 253, 255));
        ::FillRect(dc, &client, background);
        ::DeleteObject(background);

        const HBRUSH border = ::CreateSolidBrush(RGB(214, 222, 233));
        RECT border_rect = client;
        ::FrameRect(dc, &border_rect, border);
        ::DeleteObject(border);

        if (!record_) {
            drawEmpty(dc, client,
                      L"Abra un ZIP o un par CFG/DAT para comenzar.",
                      L"La aplicación lee el registro en memoria, no extrae "
                      L"miembros ZIP y no envía datos a la red.");
            present();
            ::SelectObject(dc, old_bitmap);
            ::DeleteObject(bitmap);
            ::DeleteDC(dc);
            ::EndPaint(hwnd_, &paint_struct);
            return;
        }

        const auto& data = record_->record.data;
        const auto& cfg = record_->record.cfg;
        if (data.sampleCount() == 0 || data.times_s.empty()) {
            drawEmpty(dc, client, L"El registro no contiene muestras DAT.",
                      L"Revise la pestaña de diagnósticos y el informe HTML.");
            present();
            ::SelectObject(dc, old_bitmap);
            ::DeleteObject(bitmap);
            ::DeleteDC(dc);
            ::EndPaint(hwnd_, &paint_struct);
            return;
        }
        if (cfg.analog.empty()) {
            drawEmpty(dc, client,
                      L"El registro no declara canales analógicos.",
                      L"Use la exportación HTML/CSV para revisar metadatos y "
                      L"canales digitales.");
            present();
            ::SelectObject(dc, old_bitmap);
            ::DeleteObject(bitmap);
            ::DeleteDC(dc);
            ::EndPaint(hwnd_, &paint_struct);
            return;
        }

        if (mode_ != ViewMode::Trace) {
            switch (mode_) {
                case ViewMode::Phasor:
                    paintPhasors(dc, client);
                    break;
                case ViewMode::Trend:
                    paintTrend(dc, client);
                    break;
                case ViewMode::Harmonic:
                    paintHarmonics(dc, client);
                    break;
                case ViewMode::Timeline:
                    paintTimeline(dc, client);
                    break;
                default:
                    break;
            }
            present();
            ::SelectObject(dc, old_bitmap);
            ::DeleteObject(bitmap);
            ::DeleteDC(dc);
            ::EndPaint(hwnd_, &paint_struct);
            return;
        }

        ensureView();
        channel_ = std::min(channel_, cfg.analog.size() - 1);
        const auto& channel = cfg.analog[channel_];
        const RECT plot = plotRect(client);
        if (plot.right <= plot.left + 4 || plot.bottom <= plot.top + 4) {
            present();
            ::SelectObject(dc, old_bitmap);
            ::DeleteObject(bitmap);
            ::DeleteDC(dc);
            ::EndPaint(hwnd_, &paint_struct);
            return;
        }

        RECT title_rect{scaleFor(dpiForWindow(hwnd_), 16),
                        scaleFor(dpiForWindow(hwnd_), 12),
                        client.right - scaleFor(dpiForWindow(hwnd_), 20),
                        scaleFor(dpiForWindow(hwnd_), 32)};
        std::wstring title = channel.name.empty()
                                ? L"Canal " + formatCountW(channel_ + 1)
                                : utf8ToWide(channel.name);
        const std::string shown_unit = displayUnitFor(channel_);
        if (!shown_unit.empty()) {
            title += L"  ·  ";
            title += utf8ToWide(shown_unit);
        }
        drawText(dc, title, title_rect, title_font_, RGB(23, 37, 56),
                 DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        const int dpi = dpiForWindow(hwnd_);
        RECT subtitle_rect{scaleFor(dpi, 16), scaleFor(dpi, 34),
                           client.right - scaleFor(dpi, 20),
                           scaleFor(dpi, 52)};
        std::wstring subtitle =
            formatCountW(data.sampleCount()) + L" muestras · " +
            utf8ToWide(dataFileTypeName(cfg.data_type)) + L" · " +
            formatNumberW(cfg.nominal_frequency_hz, 7) + L" Hz nominales · " +
            (hasValidTrigger() ? L"trigger CFG dentro del DAT"
                               : L"referencia = inicio del DAT");
        if (record_->analysis_available) {
            if (record_->analysis.event) {
                subtitle += L" · inicio candidato " +
                            formatNumberW(record_->analysis.event->time_s, 7) +
                            L" s";
            } else {
                subtitle += L" · inicio automático no identificado en las fases seleccionadas";
            }
        }
        drawText(dc, subtitle, subtitle_rect, small_font_, RGB(85, 103, 125),
                 DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        paintTraceLegend(dc, client);
        paintPlot(dc, plot, channel);
        paintCursor(dc, plot, channel);

        present();
        ::SelectObject(dc, old_bitmap);
        ::DeleteObject(bitmap);
        ::DeleteDC(dc);
        ::EndPaint(hwnd_, &paint_struct);
    }

    void paintPlot(HDC dc, const RECT& plot, const AnalogChannel&) {
        const auto& data = record_->record.data;
        const double first = data.times_s.front();
        const double last = data.times_s.back();
        ensureView();
        const double x_lo = view_lo_;
        const double x_hi = view_hi_;
        const double span = x_hi - x_lo;
        const double reference = timeReference();
        const bool use_seconds = span >= 10.0;
        const double time_factor = use_seconds ? 1.0 : 1000.0;

        const auto lower = std::lower_bound(data.times_s.begin(),
                                            data.times_s.end(), x_lo);
        const auto upper = std::upper_bound(data.times_s.begin(),
                                            data.times_s.end(), x_hi);
        std::size_t begin =
            static_cast<std::size_t>(lower - data.times_s.begin());
        std::size_t end =
            static_cast<std::size_t>(upper - data.times_s.begin());
        if (begin > 0) {
            --begin;
        }
        if (end < data.times_s.size()) {
            ++end;
        }
        if (begin >= end) {
            const HBRUSH background = ::CreateSolidBrush(RGB(249, 251, 254));
            ::FillRect(dc, &plot, background);
            ::DeleteObject(background);
            RECT text_rect = plot;
            drawText(dc, L"No hay muestras en la ventana visible.", text_rect,
                     body_font_, RGB(100, 116, 139),
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            return;
        }

        const std::string shown_unit = displayUnitFor(channel_);
        const auto draw_channels = traceChannels();
        double y_min = std::numeric_limits<double>::infinity();
        double y_max = -std::numeric_limits<double>::infinity();
        for (const std::size_t draw_channel : draw_channels) {
            for (std::size_t sample = begin; sample < end; ++sample) {
                const double value = data.analogValue(sample, draw_channel);
                if (std::isfinite(value)) {
                    y_min = std::min(y_min, value);
                    y_max = std::max(y_max, value);
                }
            }
        }
        if (!std::isfinite(y_min) || !std::isfinite(y_max)) {
            const HBRUSH background = ::CreateSolidBrush(RGB(249, 251, 254));
            ::FillRect(dc, &plot, background);
            ::DeleteObject(background);
            RECT text_rect = plot;
            drawText(dc, L"El canal no tiene valores finitos en la ventana.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            return;
        }
        if (y_max <= y_min) {
            const double padding = std::max(1.0, std::abs(y_min) * 0.05);
            y_min -= padding;
            y_max += padding;
        } else {
            const double padding = (y_max - y_min) * 0.07;
            y_min -= padding;
            y_max += padding;
        }

        const int plot_width = plot.right - plot.left;
        const int plot_height = plot.bottom - plot.top;
        const HBRUSH plot_background = ::CreateSolidBrush(RGB(250, 252, 255));
        ::FillRect(dc, &plot, plot_background);
        ::DeleteObject(plot_background);
        if (record_->analysis_available) {
            const auto& a = record_->analysis;
            const double lo = std::max(x_lo, a.falla_center_s - a.window_half_s);
            const double hi = std::min(x_hi, a.falla_center_s + a.window_half_s);
            if (hi > lo) {
                RECT band{plot.left + static_cast<int>((lo - x_lo) / span * plot_width),
                    plot.top, plot.left + static_cast<int>((hi - x_lo) / span * plot_width),
                    plot.bottom};
                HBRUSH brush = ::CreateSolidBrush(RGB(255, 235, 183));
                ::FillRect(dc, &band, brush);
                ::DeleteObject(brush);
            }
        }
        if (!shown_unit.empty()) {
            RECT unit_rect{plot.left + scaleFor(dpiForWindow(hwnd_), 6),
                           plot.top + scaleFor(dpiForWindow(hwnd_), 4),
                           plot.right - scaleFor(dpiForWindow(hwnd_), 6),
                           plot.top + scaleFor(dpiForWindow(hwnd_), 20)};
            drawText(dc, L"Unidad: " + utf8ToWide(shown_unit), unit_rect,
                     small_font_, RGB(85, 103, 125),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        }

        const HPEN grid_pen = ::CreatePen(PS_SOLID, 1, RGB(226, 233, 241));
        const HGDIOBJ old_pen = ::SelectObject(dc, grid_pen);

        for (int tick = 0; tick <= 4; ++tick) {
            const int y = plot.top + ::MulDiv(plot_height, tick, 4);
            ::MoveToEx(dc, plot.left, y, nullptr);
            ::LineTo(dc, plot.right, y);
            const double value =
                y_max + (y_min - y_max) * (static_cast<double>(tick) / 4.0);
            RECT label{plot.left - scaleFor(dpiForWindow(hwnd_), 78),
                       y - scaleFor(dpiForWindow(hwnd_), 9),
                       plot.left - scaleFor(dpiForWindow(hwnd_), 7),
                       y + scaleFor(dpiForWindow(hwnd_), 9)};
            drawText(dc, formatNumberW(value, 7), label, small_font_,
                     RGB(77, 93, 112),
                     DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        }
        for (int tick = 0; tick <= 5; ++tick) {
            const int x = plot.left + ::MulDiv(plot_width, tick, 5);
            ::MoveToEx(dc, x, plot.top, nullptr);
            ::LineTo(dc, x, plot.bottom);
            const double time = x_lo + span * (static_cast<double>(tick) / 5.0);
            const std::wstring label =
                formatNumberW((time - reference) * time_factor, 6);
            RECT label_rect{x - scaleFor(dpiForWindow(hwnd_), 42),
                            plot.bottom + scaleFor(dpiForWindow(hwnd_), 5),
                            x + scaleFor(dpiForWindow(hwnd_), 42),
                            plot.bottom + scaleFor(dpiForWindow(hwnd_), 23)};
            drawText(dc, label, label_rect, small_font_, RGB(77, 93, 112),
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }

        ::SelectObject(dc, old_pen);
        ::DeleteObject(grid_pen);

        const std::wstring axis_title =
            use_seconds
                ? (hasValidTrigger() ? L"Tiempo (s desde trigger)"
                                     : L"Tiempo (s desde inicio)")
                : (hasValidTrigger() ? L"Tiempo (ms desde trigger)"
                                     : L"Tiempo (ms desde inicio)");
        RECT axis_rect{plot.left, plot.bottom + scaleFor(dpiForWindow(hwnd_), 24),
                       plot.right, plot.bottom + scaleFor(dpiForWindow(hwnd_), 48)};
        drawText(dc, axis_title, axis_rect, small_font_, RGB(77, 93, 112),
                 DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        if (record_->record.trigger_offset_s) {
            const double trigger = *record_->record.trigger_offset_s;
            if (trigger >= x_lo && trigger <= x_hi) {
                const int trigger_x =
                    plot.left + static_cast<int>(
                                    (trigger - x_lo) / span * plot_width);
                const HPEN trigger_pen =
                    ::CreatePen(PS_DOT, 1, RGB(15, 23, 42));
                const HGDIOBJ previous = ::SelectObject(dc, trigger_pen);
                ::MoveToEx(dc, trigger_x, plot.top, nullptr);
                ::LineTo(dc, trigger_x, plot.bottom);
                ::SelectObject(dc, previous);
                ::DeleteObject(trigger_pen);
                RECT label{trigger_x + scaleFor(dpiForWindow(hwnd_), 4),
                           plot.top + scaleFor(dpiForWindow(hwnd_), 2),
                           plot.right - scaleFor(dpiForWindow(hwnd_), 4),
                           plot.top + scaleFor(dpiForWindow(hwnd_), 18)};
                drawText(dc, L"Trigger", label, small_font_, RGB(15, 23, 42),
                         DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            }
        }

        if (record_->analysis_available && record_->analysis.event) {
            const double onset = record_->analysis.event->time_s;
            if (onset >= x_lo && onset <= x_hi) {
                const int onset_x =
                    plot.left + static_cast<int>(
                                    (onset - x_lo) / span * plot_width);
                const HPEN onset_pen =
                    ::CreatePen(PS_DOT, 1, RGB(194, 65, 12));
                const HGDIOBJ previous = ::SelectObject(dc, onset_pen);
                ::MoveToEx(dc, onset_x, plot.top, nullptr);
                ::LineTo(dc, onset_x, plot.bottom);
                ::SelectObject(dc, previous);
                ::DeleteObject(onset_pen);
                RECT label{onset_x + scaleFor(dpiForWindow(hwnd_), 4),
                           plot.top + scaleFor(dpiForWindow(hwnd_), 20),
                           plot.right - scaleFor(dpiForWindow(hwnd_), 4),
                           plot.top + scaleFor(dpiForWindow(hwnd_), 36)};
                drawText(dc, L"Inicio candidato", label, small_font_,
                         RGB(154, 52, 18),
                         DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            }
        }

        const std::size_t samples = end - begin;
        const std::size_t maximum_points =
            static_cast<std::size_t>(std::max(64, plot_width * 2));
        const std::size_t stride =
            std::max<std::size_t>(1, (samples + maximum_points - 1) /
                                         maximum_points);
        const int saved = ::SaveDC(dc);
        ::IntersectClipRect(dc, plot.left, plot.top, plot.right, plot.bottom);
        for (const std::size_t draw_channel : draw_channels) {
            const HPEN trace_pen = ::CreatePen(
                PS_SOLID, scaleFor(dpiForWindow(hwnd_), 2),
                traceChannelColor(draw_channel));
            const HGDIOBJ previous_pen = ::SelectObject(dc, trace_pen);
            bool open_segment = false;
            for (std::size_t sample = begin; sample < end; sample += stride) {
                const double value = data.analogValue(sample, draw_channel);
                const double time = data.times_s[sample];
                const bool finite = std::isfinite(value) && std::isfinite(time);
                if (!finite) {
                    open_segment = false;
                    continue;
                }
                const int x = plot.left +
                              static_cast<int>((time - x_lo) / span *
                                               plot_width);
                const int y = plot.top + static_cast<int>(
                                              (y_max - value) / (y_max - y_min) *
                                              plot_height);
                if (!open_segment) {
                    ::MoveToEx(dc, x, y, nullptr);
                    open_segment = true;
                } else {
                    ::LineTo(dc, x, y);
                }
            }
            if (end > begin && open_segment) {
                const double value = data.analogValue(end - 1, draw_channel);
                const double time = data.times_s[end - 1];
                if (std::isfinite(value) && std::isfinite(time)) {
                    const int x = plot.left + static_cast<int>(
                                                  (time - x_lo) / span *
                                                  plot_width);
                    const int y = plot.top + static_cast<int>(
                                                  (y_max - value) /
                                                  (y_max - y_min) *
                                                  plot_height);
                    ::LineTo(dc, x, y);
                }
            }
            ::SelectObject(dc, previous_pen);
            ::DeleteObject(trace_pen);
        }
        ::RestoreDC(dc, saved);

        const HPEN frame_pen = ::CreatePen(PS_SOLID, 1, RGB(184, 197, 213));
        const HGDIOBJ frame_old = ::SelectObject(dc, frame_pen);
        ::MoveToEx(dc, plot.left, plot.top, nullptr);
        ::LineTo(dc, plot.right, plot.top);
        ::LineTo(dc, plot.right, plot.bottom);
        ::LineTo(dc, plot.left, plot.bottom);
        ::LineTo(dc, plot.left, plot.top);
        ::SelectObject(dc, frame_old);
        ::DeleteObject(frame_pen);

        if (x_lo <= first && x_hi >= last && data.times_s.front() != 0.0) {
            RECT note{plot.left, plot.top + scaleFor(dpiForWindow(hwnd_), 6),
                      plot.right - scaleFor(dpiForWindow(hwnd_), 8),
                      plot.top + scaleFor(dpiForWindow(hwnd_), 26)};
            drawText(dc, L"El primer timestamp DAT no es cero; se conserva.",
                     note, small_font_, RGB(85, 103, 125),
                     DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        }
    }

    void paintPhasors(HDC dc, const RECT& client) {
        const RECT plot = plotRect(client);
        const int dpi = dpiForWindow(hwnd_);
        const HBRUSH background = ::CreateSolidBrush(RGB(250, 252, 255));
        ::FillRect(dc, &plot, background);
        ::DeleteObject(background);
        drawText(dc, L"Fasores RMS · unidad CFG (sin conversión P/S)",
                 RECT{plot.left + scaleFor(dpi, 8), plot.top + scaleFor(dpi, 4),
                      plot.right - scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 24)},
                 title_font_, RGB(23, 37, 56),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        drawText(dc, L"Rueda: zoom desde el cursor · doble clic: restablecer",
                 RECT{plot.right - scaleFor(dpi, 300),
                      plot.top + scaleFor(dpi, 4),
                      plot.right - scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 22)},
                 small_font_, RGB(85, 103, 125),
                 DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
        if (!record_ || !record_->analysis_available ||
            (!record_->analysis.prefalla_phasors.has_value() &&
             !record_->analysis.falla_phasors.has_value())) {
            RECT text_rect = plot;
            drawText(dc, L"No hay fasores disponibles; revise la selección A/B/C "
                         L"y las ventanas.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_WORDBREAK | DT_CENTER | DT_VCENTER);
            return;
        }
        double maximum = 0.0;
        const auto update_max = [&](const std::optional<
                                        std::array<analysis::Phasor, 3>>& set) {
            if (!set) {
                return;
            }
            for (const auto& phasor : *set) {
                if (phasor.isFinite()) {
                    maximum = std::max(maximum, phasor.magnitude());
                }
            }
        };
        update_max(record_->analysis.prefalla_phasors);
        update_max(record_->analysis.falla_phasors);
        if (!(maximum > 0.0)) {
            RECT text_rect = plot;
            drawText(dc, L"Los fasores no tienen magnitud finita.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            return;
        }
        const int center_x = (plot.left + plot.right) / 2 +
                             static_cast<int>(std::lround(phasor_offset_x_));
        const int center_y = (plot.top + plot.bottom) / 2 + scaleFor(dpi, 12) +
                             static_cast<int>(std::lround(phasor_offset_y_));
        const int radius = std::max(
            20, static_cast<int>(std::min(plot.right - plot.left,
                                          plot.bottom - plot.top) /
                                 2 - scaleFor(dpi, 34)));
        const double scale = static_cast<double>(radius) / maximum * phasor_zoom_;
        const HPEN axis_pen = ::CreatePen(PS_SOLID, 1, RGB(160, 174, 192));
        const HGDIOBJ old_pen = ::SelectObject(dc, axis_pen);
        ::MoveToEx(dc, plot.left + scaleFor(dpi, 12), center_y, nullptr);
        ::LineTo(dc, plot.right - scaleFor(dpi, 12), center_y);
        ::MoveToEx(dc, center_x, plot.top + scaleFor(dpi, 30), nullptr);
        ::LineTo(dc, center_x, plot.bottom - scaleFor(dpi, 30));
        ::SelectObject(dc, old_pen);
        ::DeleteObject(axis_pen);
        RECT zero_label{center_x + scaleFor(dpi, 4),
                        center_y + scaleFor(dpi, 2),
                        center_x + scaleFor(dpi, 80),
                        center_y + scaleFor(dpi, 18)};
        drawText(dc, L"0", zero_label, small_font_, RGB(100, 116, 139),
                 DT_SINGLELINE | DT_LEFT);
        const auto draw_set = [&](const std::optional<
                                      std::array<analysis::Phasor, 3>>& set,
                                  bool fault) {
            if (!set) {
                return;
            }
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const auto& phasor = (*set)[slot];
                if (!phasor.isFinite()) {
                    continue;
                }
                const int x = center_x +
                              static_cast<int>(phasor.value.real() * scale);
                const int y = center_y -
                              static_cast<int>(phasor.value.imag() * scale);
                const COLORREF color = channelColor(slot);
                const HPEN pen = ::CreatePen(
                    fault ? PS_DOT : PS_SOLID, fault ? 1 : scaleFor(dpi, 2),
                    color);
                const HGDIOBJ previous = ::SelectObject(dc, pen);
                ::MoveToEx(dc, center_x, center_y, nullptr);
                ::LineTo(dc, x, y);
                ::SelectObject(dc, previous);
                ::DeleteObject(pen);
                const HBRUSH brush = ::CreateSolidBrush(color);
                const HGDIOBJ old_brush = ::SelectObject(dc, brush);
                ::Ellipse(dc, x - scaleFor(dpi, 4), y - scaleFor(dpi, 4),
                          x + scaleFor(dpi, 4), y + scaleFor(dpi, 4));
                ::SelectObject(dc, old_brush);
                ::DeleteObject(brush);
            }
        };
        const int clip_state = ::SaveDC(dc);
        ::IntersectClipRect(dc, plot.left + scaleFor(dpi, 12),
                            plot.top + scaleFor(dpi, 30),
                            plot.right - scaleFor(dpi, 12),
                            plot.bottom - scaleFor(dpi, 112));
        draw_set(record_->analysis.prefalla_phasors, false);
        draw_set(record_->analysis.falla_phasors, true);
        if (clip_state != 0) {
            ::RestoreDC(dc, clip_state);
        }
                                                                           
                                                                           
        RECT values_area{plot.left, plot.bottom - scaleFor(dpi, 110),
                         plot.right, plot.bottom - scaleFor(dpi, 48)};
        ::FillRect(dc, &values_area,
                   static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
        const int column_width = static_cast<int>(plot.right - plot.left) / 3;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            for (int row = 0; row < 2; ++row) {
                const auto& values = row == 0
                    ? record_->analysis.prefalla_phasors
                    : record_->analysis.falla_phasors;
                std::wstring label(1, static_cast<wchar_t>(L'A' + slot));
                label += row == 0 ? L" prefalla: " : L" falla: ";
                label += values && (*values)[slot].isFinite()
                    ? formatNumberW((*values)[slot].magnitude(), 6) + L" ∠ " +
                          formatNumberW((*values)[slot].angleDegrees(), 5) + L"°"
                    : L"—";
                const int left = plot.left + static_cast<int>(slot) * column_width;
                const int top = values_area.top + scaleFor(dpi, 6 + row * 23);
                drawText(dc, label,
                         RECT{left + scaleFor(dpi, 8), top,
                              left + column_width - scaleFor(dpi, 4),
                              top + scaleFor(dpi, 20)},
                         small_font_, channelColor(slot),
                         DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
            }
        }
        RECT legend{plot.left + scaleFor(dpi, 8),
                    plot.bottom - scaleFor(dpi, 22),
                    plot.right - scaleFor(dpi, 8),
                    plot.bottom - scaleFor(dpi, 4)};
        drawText(dc, L"Línea continua: prefalla · punteada: falla · A/B/C",
                 legend, small_font_, RGB(85, 103, 125),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        if (record_->analysis.falla_sequences.has_value()) {
            const auto& sequences = *record_->analysis.falla_sequences;
            const std::wstring text =
                L"I0 " + formatNumberW(std::abs(sequences.zero), 6) +
                L" · I1 " + formatNumberW(std::abs(sequences.positive), 6) +
                L" · I2 " + formatNumberW(std::abs(sequences.negative), 6) +
                L" (unidad CFG)";
            drawText(dc, text,
                     RECT{plot.left + scaleFor(dpi, 8),
                          plot.bottom - scaleFor(dpi, 42),
                          plot.right - scaleFor(dpi, 8),
                          plot.bottom - scaleFor(dpi, 24)},
                     small_font_, RGB(71, 85, 105),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
    }

    void paintTrend(HDC dc, const RECT& client) {
        const RECT plot = plotRect(client);
        const int dpi = dpiForWindow(hwnd_);
        const HBRUSH background = ::CreateSolidBrush(RGB(250, 252, 255));
        ::FillRect(dc, &plot, background);
        ::DeleteObject(background);
        drawText(dc, L"Tendencia fasorial móvil · RMS por fase y secuencias",
                 RECT{plot.left + scaleFor(dpi, 8), plot.top + scaleFor(dpi, 4),
                      plot.right - scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 24)},
                 title_font_, RGB(23, 37, 56),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        drawText(dc, L"Rueda: zoom horizontal · arrastre: desplazar · doble clic: restablecer",
                 RECT{plot.left + scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 24),
                      plot.right - scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 38)},
                 small_font_, RGB(85, 103, 125),
                 DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
        if (!record_ || record_->trend.points.empty()) {
            RECT text_rect = plot;
            drawText(dc, L"No hay ventanas completas para la tendencia. "
                         L"Seleccione A/B/C y aumente el registro.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_WORDBREAK | DT_CENTER | DT_VCENTER);
            return;
        }
        ensureView();
        double x0 = std::max(view_lo_, record_->trend.points.front().time_s);
        double x1 = std::min(view_hi_, record_->trend.points.back().time_s);
        if (!(x1 > x0)) {
            RECT text_rect = plot;
            drawText(dc, L"No hay ventanas de tendencia dentro del rango visible. "
                         L"Reduzca el zoom o restablezca la vista.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_WORDBREAK | DT_CENTER | DT_VCENTER);
            return;
        }
        double y_max = 0.0;
        for (const auto& point : record_->trend.points) {
            if (point.time_s < x0 || point.time_s > x1) {
                continue;
            }
            for (double value : point.phase_rms) {
                if (std::isfinite(value)) {
                    y_max = std::max(y_max, value);
                }
            }
            for (double value : point.sequence_rms) {
                if (std::isfinite(value)) {
                    y_max = std::max(y_max, value);
                }
            }
        }
        if (!(y_max > 0.0)) {
            y_max = 1.0;
        }
        y_max *= 1.08;
        const RECT chart{plot.left, plot.top + scaleFor(dpi, 38),
                         plot.right, plot.bottom - scaleFor(dpi, 58)};
        const HPEN grid = ::CreatePen(PS_SOLID, 1, RGB(226, 233, 241));
        const HGDIOBJ old_pen = ::SelectObject(dc, grid);
        for (int tick = 0; tick <= 4; ++tick) {
            const int y = chart.top +
                          ::MulDiv(chart.bottom - chart.top, tick, 4);
            ::MoveToEx(dc, chart.left, y, nullptr);
            ::LineTo(dc, chart.right, y);
            const double value =
                y_max * (1.0 - static_cast<double>(tick) / 4.0);
            drawText(dc, formatNumberW(value, 6),
                     RECT{chart.left - scaleFor(dpi, 78),
                          y - scaleFor(dpi, 9),
                           chart.left - scaleFor(dpi, 6),
                          y + scaleFor(dpi, 9)},
                     small_font_, RGB(77, 93, 112),
                     DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        }
        for (int tick = 0; tick <= 5; ++tick) {
            const int x = chart.left +
                          ::MulDiv(chart.right - chart.left, tick, 5);
            ::MoveToEx(dc, x, chart.top, nullptr);
            ::LineTo(dc, x, chart.bottom);
            const double time =
                x0 + (x1 - x0) * static_cast<double>(tick) / 5.0;
            drawText(dc, formatNumberW(time, 6),
                     RECT{x - scaleFor(dpi, 48),
                          chart.bottom + scaleFor(dpi, 4),
                          x + scaleFor(dpi, 48),
                          chart.bottom + scaleFor(dpi, 22)},
                     small_font_, RGB(77, 93, 112),
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
        ::SelectObject(dc, old_pen);
        ::DeleteObject(grid);
        const auto draw_series = [&](int series, bool sequence, int style) {
            const HPEN pen = ::CreatePen(style, scaleFor(dpi, 2),
                                         channelColor(static_cast<std::size_t>(
                                             sequence ? series + 3 : series)));
            const HGDIOBJ previous = ::SelectObject(dc, pen);
            const int clip_state = ::SaveDC(dc);
            ::IntersectClipRect(dc, chart.left, chart.top, chart.right,
                                chart.bottom);
            bool open = false;
            for (const auto& point : record_->trend.points) {
                if (point.time_s < x0 || point.time_s > x1) {
                    continue;
                }
                const double value =
                    sequence
                        ? point.sequence_rms[static_cast<std::size_t>(series)]
                        : point.phase_rms[static_cast<std::size_t>(series)];
                if (!std::isfinite(value)) {
                    open = false;
                    continue;
                }
                const int x = chart.left + static_cast<int>(
                                               (point.time_s - x0) / (x1 - x0) *
                                               (chart.right - chart.left));
                const int y = chart.bottom - static_cast<int>(
                                                value / y_max *
                                                (chart.bottom - chart.top));
                if (!open) {
                    ::MoveToEx(dc, x, y, nullptr);
                    open = true;
                } else {
                    ::LineTo(dc, x, y);
                }
            }
            if (clip_state != 0) {
                ::RestoreDC(dc, clip_state);
            }
            ::SelectObject(dc, previous);
            ::DeleteObject(pen);
        };
        for (int series = 0; series < 3; ++series) {
            draw_series(series, false, PS_SOLID);
            draw_series(series, true, PS_DOT);
        }
        const wchar_t* legend_names[] = {L"A", L"B", L"C", L"I0", L"I1", L"I2"};
        const int legend_width = std::max(
            1, static_cast<int>(plot.right - plot.left) / 6);
        const int legend_y = plot.bottom - scaleFor(dpi, 15);
        for (int index = 0; index < 6; ++index) {
            const int x = plot.left + index * legend_width;
            const HPEN pen = ::CreatePen(
                index < 3 ? PS_SOLID : PS_DOT, scaleFor(dpi, 2),
                channelColor(static_cast<std::size_t>(index)));
            const HGDIOBJ previous = ::SelectObject(dc, pen);
            ::MoveToEx(dc, x + scaleFor(dpi, 4), legend_y, nullptr);
            ::LineTo(dc, x + scaleFor(dpi, 22), legend_y);
            ::SelectObject(dc, previous);
            ::DeleteObject(pen);
            drawText(dc, legend_names[index],
                     RECT{x + scaleFor(dpi, 26),
                          legend_y - scaleFor(dpi, 10),
                          x + legend_width - scaleFor(dpi, 2),
                          legend_y + scaleFor(dpi, 10)},
                     small_font_, RGB(71, 85, 105),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
    }

    void paintHarmonics(HDC dc, const RECT& client) {
        const RECT plot = plotRect(client);
        const int dpi = dpiForWindow(hwnd_);
        const HBRUSH background = ::CreateSolidBrush(RGB(250, 252, 255));
        ::FillRect(dc, &plot, background);
        ::DeleteObject(background);
        drawText(dc, L"Armónicos y THD · ajuste por mínimos cuadrados",
                 RECT{plot.left + scaleFor(dpi, 8), plot.top + scaleFor(dpi, 4),
                      plot.right - scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 24)},
                 title_font_, RGB(23, 37, 56),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        drawText(dc, L"Rueda: zoom en órdenes · arrastre: desplazar · doble clic: restablecer",
                 RECT{plot.left + scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 24),
                      plot.right - scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 38)},
                 small_font_, RGB(85, 103, 125),
                 DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_END_ELLIPSIS);
        if (!record_ || !record_->analysis_available) {
            RECT text_rect = plot;
            drawText(dc, L"No hay análisis de armónicos disponible.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            return;
        }
        drawText(dc, harmonicWindowLabel(),
            RECT{plot.left + scaleFor(dpi, 8), plot.top + scaleFor(dpi, 41),
                 plot.right - scaleFor(dpi, 8), plot.top + scaleFor(dpi, 60)},
            small_font_, RGB(100, 70, 18), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        const auto& analysis = record_->analysis;
        const std::wstring window_detail = L"Falla s = centro: " +
            formatFixedNumberW(analysis.falla_center_s, 6) + L" s · " +
            formatNumberW(analysis.frequency_hz, 6) + L" Hz · modificar Falla s / Ciclos y aplicar";
        drawText(dc, window_detail,
            RECT{plot.left + scaleFor(dpi, 8), plot.top + scaleFor(dpi, 61),
                 plot.right - scaleFor(dpi, 8), plot.top + scaleFor(dpi, 80)},
            small_font_, RGB(85, 103, 125), DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        drawText(dc, utf8ToWide(analysis.prefalla_status),
            RECT{plot.left + scaleFor(dpi, 8), plot.top + scaleFor(dpi, 81),
                 plot.right - scaleFor(dpi, 8), plot.top + scaleFor(dpi, 100)},
            small_font_, analysis.prefalla_usable ? RGB(85, 103, 125) : RGB(160, 65, 20),
            DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        std::size_t orders = 0;
        double maximum = 0.0;
        for (const auto& fit : record_->analysis.harmonics) {
            if (!fit.ok) {
                continue;
            }
            orders = std::max(orders, fit.harmonics_rms.size());
            for (double value : fit.harmonics_rms) {
                if (std::isfinite(value)) {
                    maximum = std::max(maximum, value);
                }
            }
        }
        if (orders == 0 || !(maximum > 0.0)) {
            RECT text_rect = plot;
            drawText(dc, L"Nyquist no permite armónicos o no hay valores "
                         L"finitos para graficar.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_WORDBREAK | DT_CENTER | DT_VCENTER);
            return;
        }
        maximum *= 1.08;
        const int chart_top = plot.top + scaleFor(dpi, 104);
        const int chart_bottom = plot.bottom - scaleFor(dpi, 60);
        const int chart_height = std::max(1, chart_bottom - chart_top);
        if (!has_harmonic_center_) {
            harmonic_center_order_ =
                (static_cast<double>(orders) + 1.0) / 2.0;
            has_harmonic_center_ = true;
        }
        const double group_width =
            static_cast<double>(plot.right - plot.left) /
            static_cast<double>(orders) * harmonic_zoom_;
        const double chart_center =
            (static_cast<double>(plot.left) + plot.right) / 2.0;
        const int clip_state = ::SaveDC(dc);
        ::IntersectClipRect(dc, plot.left, chart_top, plot.right,
                            chart_bottom + scaleFor(dpi, 22));
        const int bar_width = std::max(2, static_cast<int>(group_width / 5.0));
        for (std::size_t order = 0; order < orders; ++order) {
            const double order_center = chart_center +
                (static_cast<double>(order + 1) - harmonic_center_order_) *
                    group_width;
            const int group_left = static_cast<int>(std::lround(
                order_center - group_width / 2.0));
            const int base_x = group_left + static_cast<int>(std::lround(
                (group_width - 3.0 * bar_width) / 2.0));
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const auto& fit = record_->analysis.harmonics[slot];
                if (!fit.ok || order >= fit.harmonics_rms.size()) {
                    continue;
                }
                const double value = fit.harmonics_rms[order];
                if (!std::isfinite(value)) {
                    continue;
                }
                const int height =
                    static_cast<int>(value / maximum * chart_height);
                const int x = base_x + static_cast<int>(slot) * bar_width;
                const HBRUSH brush = ::CreateSolidBrush(channelColor(slot));
                RECT bar{x, chart_bottom - height, x + bar_width,
                         chart_bottom};
                ::FillRect(dc, &bar, brush);
                ::DeleteObject(brush);
            }
            RECT label{group_left, chart_bottom + scaleFor(dpi, 3),
                       group_left + static_cast<int>(std::lround(group_width)),
                       chart_bottom + scaleFor(dpi, 21)};
            drawText(dc, L"h" + formatCountW(order + 1), label, small_font_,
                     RGB(77, 93, 112),
                     DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        }
        if (clip_state != 0) {
            ::RestoreDC(dc, clip_state);
        }
        std::wstring thd_text;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& fit = record_->analysis.harmonics[slot];
            if (!fit.thd_pct.has_value()) {
                continue;
            }
            if (!thd_text.empty()) {
                thd_text += L" · ";
            }
            thd_text += std::wstring(1, static_cast<wchar_t>(L'A' + slot)) +
                        L" THD " + formatNumberW(*fit.thd_pct, 5) + L"%";
        }
        if (thd_text.empty()) {
            thd_text = L"THD no disponible (Nyquist insuficiente)";
        }
        drawText(dc, thd_text,
                 RECT{plot.left + scaleFor(dpi, 8),
                      plot.bottom - scaleFor(dpi, 43),
                      plot.right - scaleFor(dpi, 8),
                      plot.bottom - scaleFor(dpi, 25)},
                 small_font_, RGB(71, 85, 105),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        const int legend_y = plot.bottom - scaleFor(dpi, 14);
        int legend_x = plot.left + scaleFor(dpi, 8);
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const HBRUSH swatch = ::CreateSolidBrush(channelColor(slot));
            RECT swatch_rect{legend_x, legend_y - scaleFor(dpi, 5),
                             legend_x + scaleFor(dpi, 12),
                             legend_y + scaleFor(dpi, 5)};
            ::FillRect(dc, &swatch_rect, swatch);
            ::DeleteObject(swatch);
            const std::wstring name(1, static_cast<wchar_t>(L'A' + slot));
            drawText(dc, name,
                     RECT{legend_x + scaleFor(dpi, 16),
                          legend_y - scaleFor(dpi, 10),
                          legend_x + scaleFor(dpi, 38),
                          legend_y + scaleFor(dpi, 10)},
                     small_font_, RGB(71, 85, 105),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            legend_x += scaleFor(dpi, 52);
        }
        drawText(dc, L"unidad CFG",
                 RECT{legend_x, legend_y - scaleFor(dpi, 10),
                      plot.right - scaleFor(dpi, 8),
                      legend_y + scaleFor(dpi, 10)},
                 small_font_, RGB(85, 103, 125),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    void paintTimeline(HDC dc, const RECT& client) {
        const RECT plot = plotRect(client);
        const int dpi = dpiForWindow(hwnd_);
        const HBRUSH background = ::CreateSolidBrush(RGB(250, 252, 255));
        ::FillRect(dc, &plot, background);
        ::DeleteObject(background);
        drawText(dc, L"Línea de tiempo · COMTRADE + bitácora Fault log.csv",
                 RECT{plot.left + scaleFor(dpi, 8), plot.top + scaleFor(dpi, 4),
                      plot.right - scaleFor(dpi, 8),
                      plot.top + scaleFor(dpi, 24)},
                 title_font_, RGB(23, 37, 56),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        if (!record_ || record_->timeline.rows.empty()) {
            RECT text_rect = plot;
            drawText(dc, L"No hay eventos alineados. Cargue una bitácora CSV "
                         L"junto al CFG/DAT.",
                     text_rect, body_font_, RGB(100, 116, 139),
                     DT_WORDBREAK | DT_CENTER | DT_VCENTER);
            return;
        }
        const auto& rows = record_->timeline.rows;
        const int pad = scaleFor(dpi, 8);
        const int header_h = scaleFor(dpi, 30);
        const int row_h = scaleFor(dpi, 32);
        const int footer_h = scaleFor(dpi, 24);
        const int table_left = plot.left + pad;
        const int table_right = plot.right - pad;
        const int table_top = plot.top + scaleFor(dpi, 34);
        const int table_bottom = plot.bottom - footer_h;
        const int row_capacity = std::max(
            0, static_cast<int>((table_bottom - table_top - header_h) / row_h));
        const std::size_t first = std::min(timeline_scroll_rows_, rows.size());
        const std::size_t visible = std::min(
            rows.size() - first, static_cast<std::size_t>(row_capacity));
        const int width = table_right - table_left;
        const int time_w = std::min(scaleFor(dpi, 128), width / 5);
        const int source_w = std::min(scaleFor(dpi, 190), width / 4);
        const int event_w = std::min(scaleFor(dpi, 170), width / 5);
        const int x_time = table_left;
        const int x_source = x_time + time_w;
        const int x_event = x_source + source_w;
        const int x_detail = x_event + event_w;
        const int header_y = table_top;
        HBRUSH header_brush = ::CreateSolidBrush(RGB(229, 238, 248));
        RECT header_rect{table_left, header_y, table_right, header_y + header_h};
        ::FillRect(dc, &header_rect, header_brush);
        ::DeleteObject(header_brush);
        const COLORREF grid = RGB(207, 218, 231);
        const HPEN grid_pen = ::CreatePen(PS_SOLID, 1, grid);
        const HGDIOBJ old_pen = ::SelectObject(dc, grid_pen);
        const std::array<std::pair<int, const wchar_t*>, 4> columns{{
            {x_time, L"Tiempo rel. (s)"},
            {x_source, L"Fuente"}, {x_event, L"Evento"},
            {x_detail, L"Detalle"}}};
        const std::array<int, 5> edges{{x_time, x_source, x_event, x_detail,
                                        table_right}};
        for (const auto& column : columns) {
            drawText(dc, column.second,
                     RECT{column.first + pad, header_y,
                          (column.first == x_time ? x_source :
                           column.first == x_source ? x_event :
                           column.first == x_event ? x_detail : table_right) - pad,
                          header_y + header_h},
                     small_font_, RGB(41, 58, 78),
                     DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        for (std::size_t index = 0; index < visible; ++index) {
            const auto& row = rows[first + index];
            const int y = header_y + header_h +
                          static_cast<int>(index) * row_h;
            RECT row_rect{table_left, y, table_right, y + row_h};
            HBRUSH row_brush = ::CreateSolidBrush(
                index % 2 == 0 ? RGB(255, 255, 255) : RGB(246, 249, 252));
            ::FillRect(dc, &row_rect, row_brush);
            ::DeleteObject(row_brush);
            const int text_top = y + scaleFor(dpi, 2);
            const int text_bottom = y + row_h - scaleFor(dpi, 2);
            const auto cell = [&](int left, int right, const std::wstring& text,
                                  COLORREF color, UINT align = DT_LEFT) {
                drawText(dc, text,
                         RECT{left + pad, text_top, right - pad, text_bottom},
                         small_font_, color,
                         DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | align);
            };
            cell(x_time, x_source,
                 formatFixedNumberW(row.seconds_from_reference, 6),
                 RGB(56, 72, 91));
            cell(x_source, x_event, utf8ToWide(row.source),
                 RGB(76, 91, 110));
            cell(x_event, x_detail, utf8ToWide(row.event),
                 RGB(154, 52, 18));
            cell(x_detail, table_right, utf8ToWide(row.detail),
                 RGB(56, 72, 91));
            ::MoveToEx(dc, table_left, y + row_h - 1, nullptr);
            ::LineTo(dc, table_right, y + row_h - 1);
        }
        for (int x : edges) {
            ::MoveToEx(dc, x, header_y, nullptr);
            ::LineTo(dc, x, header_y + header_h +
                                 static_cast<int>(visible) * row_h);
        }
        ::SelectObject(dc, old_pen);
        ::DeleteObject(grid_pen);
        const std::wstring range = rows.empty()
            ? L"0 eventos"
            : L"Mostrando " + formatCountW(first + 1) + L"–" +
                  formatCountW(first + visible) + L" de " +
                  formatCountW(rows.size()) +
                  L" eventos · use la rueda para desplazarse";
        drawText(dc, range,
                 RECT{table_left, plot.bottom - footer_h + scaleFor(dpi, 2),
                      table_right, plot.bottom - scaleFor(dpi, 2)},
                 small_font_, RGB(85, 103, 125),
                 DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    }

    void paintCursor(HDC dc, const RECT& plot,
                     const AnalogChannel&) {
        const int dpi = dpiForWindow(hwnd_);
        if (cursor_valid_ && cursor_x_ >= plot.left && cursor_x_ <= plot.right) {
            const auto& data = record_->record.data;
            const double ratio =
                static_cast<double>(cursor_x_ - plot.left) /
                static_cast<double>(std::max<LONG>(1, plot.right - plot.left));
            const double time = view_lo_ + (view_hi_ - view_lo_) * ratio;
            auto iterator = std::lower_bound(data.times_s.begin(),
                                             data.times_s.end(), time);
            std::size_t index = static_cast<std::size_t>(
                iterator - data.times_s.begin());
            const std::size_t time_count = data.times_s.size();
            if (time_count == 0) {
                return;
            }
            if (index >= time_count) {
                index = time_count - 1;
            }
            if (index > 0 && index < time_count &&
                std::abs(data.times_s[index - 1] - time) <
                    std::abs(data.times_s[index] - time)) {
                --index;
            }
            const double sample_time = data.times_s[index];
            const double value = data.analogValue(index, channel_);
            const double reference = timeReference();
            const int x = static_cast<int>(std::min<LONG>(
                plot.right,
                std::max<LONG>(plot.left,
                         plot.left + static_cast<int>(
                                         (sample_time - view_lo_) /
                                         (view_hi_ - view_lo_) *
                                         std::max<LONG>(1, plot.right - plot.left)))));
            const HPEN cursor_pen =
                ::CreatePen(PS_DOT, 1, RGB(71, 85, 105));
            const HGDIOBJ old_pen = ::SelectObject(dc, cursor_pen);
            ::MoveToEx(dc, x, plot.top, nullptr);
            ::LineTo(dc, x, plot.bottom);
            ::SelectObject(dc, old_pen);
            ::DeleteObject(cursor_pen);

            std::wstring text =
                L"t = " + formatNumberW(sample_time, 9) + L" s";
            if (hasValidTrigger()) {
                text += L" · " +
                        formatNumberW((sample_time - reference) * 1000.0, 7) +
                        L" ms desde trigger";
            }
            text += L" · " + formatNumberW(value, 9);
            const std::string shown_unit = displayUnitFor(channel_);
            if (!shown_unit.empty()) {
                text += L" ";
                text += utf8ToWide(shown_unit);
            }
            RECT label{plot.left, scaleFor(dpi, 56),
                       plot.right - scaleFor(dpi, 6), scaleFor(dpi, 72)};
            ::SetBkMode(dc, OPAQUE);
            ::SetBkColor(dc, RGB(255, 255, 255));
            drawText(dc, text, label, small_font_, RGB(71, 85, 105),
                     DT_SINGLELINE | DT_VCENTER | DT_RIGHT |
                         DT_END_ELLIPSIS);
        } else {
            RECT hint{plot.left, scaleFor(dpi, 56),
                      plot.right - scaleFor(dpi, 6), scaleFor(dpi, 72)};
            drawText(dc, L"Rueda: zoom · arrastrar: desplazar · doble clic: "
                         L"ajustar vista",
                     hint, small_font_, RGB(100, 116, 139),
                     DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        }
    }

    void drawEmpty(HDC dc, const RECT& client, std::wstring_view title,
                   std::wstring_view body) {
        const int dpi = dpiForWindow(hwnd_);
        RECT title_rect{client.left + scaleFor(dpi, 24),
                        client.top + scaleFor(dpi, 48),
                        client.right - scaleFor(dpi, 24),
                        client.top + scaleFor(dpi, 88)};
        drawText(dc, title, title_rect, title_font_, RGB(23, 37, 56),
                 DT_SINGLELINE | DT_VCENTER | DT_CENTER);

        RECT body_rect{client.left + scaleFor(dpi, 60),
                       client.top + scaleFor(dpi, 94),
                       client.right - scaleFor(dpi, 60),
                       client.top + scaleFor(dpi, 170)};
        drawText(dc, body, body_rect, body_font_, RGB(85, 103, 125),
                 DT_WORDBREAK | DT_CENTER);
    }

    std::string displayUnitFor(std::size_t channel) const {
        if (!record_ || channel >= record_->record.cfg.analog.size()) {
            return {};
        }
        const auto found = record_->unit_overrides.find(channel);
        if (found != record_->unit_overrides.end()) {
            return found->second;
        }
        return record_->record.cfg.analog[channel].unit;
    }

    static COLORREF channelColor(std::size_t channel) {
        static constexpr COLORREF palette[] = {
            RGB(0, 114, 178),   RGB(213, 94, 0),  RGB(0, 158, 115),
            RGB(204, 121, 167), RGB(86, 180, 233), RGB(230, 159, 0),
            RGB(51, 34, 136),   RGB(170, 68, 153), RGB(68, 170, 153),
            RGB(153, 153, 51),  RGB(136, 34, 85),  RGB(102, 17, 0)};
        return palette[channel % (sizeof(palette) / sizeof(palette[0]))];
    }

    void onMouseMove(int x, int y, WPARAM wparam) {
        if (!record_ || record_->record.data.times_s.empty()) {
            return;
        }
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const RECT plot = plotRect(client);
        if (dragging_) {
            if (mode_ == ViewMode::Harmonic) {
                const std::size_t orders = harmonicOrderCount();
                const double width = static_cast<double>(
                    std::max<LONG>(1, plot.right - plot.left));
                if (orders > 0) {
                    const double base_group =
                        width / static_cast<double>(orders);
                    const double group = std::max(
                        1e-9, base_group * harmonic_zoom_);
                    harmonic_center_order_ = std::clamp(
                        drag_harmonic_center_ -
                            static_cast<double>(x - drag_start_.x) / group,
                        1.0, static_cast<double>(orders));
                    ::InvalidateRect(hwnd_, nullptr, FALSE);
                }
                return;
            }
            const int width = static_cast<int>(std::max<LONG>(1, plot.right - plot.left));
            const double span = drag_hi_ - drag_lo_;
            const double shift = -static_cast<double>(x - drag_start_.x) /
                                 width * span;
            setView(drag_lo_ + shift, drag_hi_ + shift);
            return;
        }

        const bool inside = x >= plot.left && x <= plot.right &&
                            y >= plot.top && y <= plot.bottom;
        if (inside || cursor_valid_) {
            cursor_valid_ = inside;
            cursor_x_ = static_cast<int>(std::min<LONG>(std::max<LONG>(x, plot.left), plot.right));
            ::InvalidateRect(hwnd_, nullptr, FALSE);
        }

        if (inside) {
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd_;
            ::TrackMouseEvent(&track);
        }
        (void)wparam;
    }

    void onLeftDown(int x, int y) {
        if (!record_ || record_->record.data.times_s.empty()) {
            return;
        }
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const RECT plot = plotRect(client);
        if (!::PtInRect(&plot, POINT{x, y})) {
            return;
        }
        if (mode_ == ViewMode::Phasor) {
            return;
        }
        if (mode_ == ViewMode::Harmonic) {
            ensureHarmonicCenter();
            drag_harmonic_center_ = harmonic_center_order_;
        }
        ensureView();
        dragging_ = true;
        drag_start_ = POINT{x, y};
        drag_lo_ = view_lo_;
        drag_hi_ = view_hi_;
        ::SetCapture(hwnd_);
        ::SetFocus(hwnd_);
    }

    void onMouseWheel(int delta, int x, int y) {
        if (mode_ == ViewMode::Timeline && record_ &&
            !record_->timeline.rows.empty() && delta != 0) {
            RECT client{};
            ::GetClientRect(hwnd_, &client);
            const int dpi = dpiForWindow(hwnd_);
            const RECT plot = plotRect(client);
            const int row_h = scaleFor(dpi, 32);
            const int table_top = plot.top + scaleFor(dpi, 34);
            const int table_bottom = plot.bottom - scaleFor(dpi, 24);
            const int header_h = scaleFor(dpi, 30);
            const std::size_t capacity = static_cast<std::size_t>(std::max(
                1, static_cast<int>((table_bottom - table_top - header_h) /
                                    row_h)));
            const std::size_t max_first = record_->timeline.rows.size() > capacity
                ? record_->timeline.rows.size() - capacity : 0;
            const std::size_t step = static_cast<std::size_t>(std::max(
                1, std::abs(delta) / WHEEL_DELTA * 3));
            if (delta < 0) {
                timeline_scroll_rows_ = std::min(max_first,
                    timeline_scroll_rows_ + step);
            } else {
                timeline_scroll_rows_ = timeline_scroll_rows_ > step
                    ? timeline_scroll_rows_ - step : 0;
            }
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (!record_ || record_->record.data.times_s.empty() ||
            delta == 0) {
            return;
        }
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const RECT plot = plotRect(client);
        POINT point{x, y};
        ::ScreenToClient(hwnd_, &point);
        const bool inside_plot = ::PtInRect(&plot, point) != FALSE;
        const int anchor = inside_plot
                               ? static_cast<int>(std::min<LONG>(std::max<LONG>(point.x, plot.left),
                                          plot.right))
                               : (plot.left + plot.right) / 2;
        const int anchor_y = mode_ == ViewMode::Phasor && inside_plot
                                 ? point.y
                                 : (plot.top + plot.bottom) / 2;
        zoomAtPixel(mode_ == ViewMode::Phasor ? point.x : anchor,
                    anchor_y, delta > 0 ? 0.8 : 1.25);
    }

    void zoomAtPixel(int pixel_x, int pixel_y, double factor) {
        if (!record_ || record_->record.data.times_s.empty() ||
            !(factor > 0.0)) {
            return;
        }
        if (mode_ == ViewMode::Phasor) {
            const double old_zoom = phasor_zoom_;
            const double new_zoom = std::clamp(old_zoom / factor, 0.25, 16.0);
            if (old_zoom != new_zoom) {
                RECT client{};
                ::GetClientRect(hwnd_, &client);
                const RECT plot = plotRect(client);
                const int dpi = dpiForWindow(hwnd_);
                const double center_x = (plot.left + plot.right) / 2.0;
                const double center_y = (plot.top + plot.bottom) / 2.0 +
                                        scaleFor(dpi, 12);
                const double ratio = new_zoom / old_zoom;
                const double anchor_x = pixel_x - center_x;
                const double anchor_y = pixel_y - center_y;
                phasor_offset_x_ = anchor_x - (anchor_x - phasor_offset_x_) * ratio;
                phasor_offset_y_ = anchor_y - (anchor_y - phasor_offset_y_) * ratio;
                phasor_zoom_ = new_zoom;
            }
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        if (mode_ == ViewMode::Harmonic) {
            zoomHarmonicsAtPixel(pixel_x, 1.0 / factor);
            return;
        }
        ensureView();
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const RECT plot = plotRect(client);
        const int width = static_cast<int>(std::max<LONG>(1, plot.right - plot.left));
        const double ratio = static_cast<double>(pixel_x - plot.left) /
                             static_cast<double>(width);
        const double anchor = view_lo_ + (view_hi_ - view_lo_) * ratio;
        const double full_first = record_->record.data.times_s.front();
        const double full_last = record_->record.data.times_s.back();
        const double full_span = std::max(1e-9, full_last - full_first);
        double span = (view_hi_ - view_lo_) * factor;
        span = std::max(span, full_span / 1e6);
        if (span >= full_span) {
            view_lo_ = full_first;
            view_hi_ = full_last;
            if (!(view_hi_ > view_lo_)) {
                view_lo_ -= 0.5;
                view_hi_ += 0.5;
            }
            has_view_ = true;
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        }
        double low = anchor - (anchor - view_lo_) * factor;
        double high = low + span;
        if (low < full_first) {
            low = full_first;
            high = low + span;
        }
        if (high > full_last) {
            high = full_last;
            low = high - span;
        }
        setView(low, high);
    }

    std::size_t harmonicOrderCount() const {
        if (!record_ || !record_->analysis_available) {
            return 0;
        }
        std::size_t orders = 0;
        for (const auto& fit : record_->analysis.harmonics) {
            if (fit.ok) {
                orders = std::max(orders, fit.harmonics_rms.size());
            }
        }
        return orders;
    }

    void ensureHarmonicCenter() {
        const std::size_t orders = harmonicOrderCount();
        if (orders == 0) {
            has_harmonic_center_ = false;
            return;
        }
        if (!has_harmonic_center_) {
            harmonic_center_order_ =
                (static_cast<double>(orders) + 1.0) / 2.0;
            has_harmonic_center_ = true;
        }
        harmonic_center_order_ = std::clamp(
            harmonic_center_order_, 1.0, static_cast<double>(orders));
    }

    void zoomHarmonicsAtPixel(int pixel, double scale) {
        const std::size_t orders = harmonicOrderCount();
        if (orders == 0 || !(scale > 0.0)) {
            return;
        }
        ensureHarmonicCenter();
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const RECT plot = plotRect(client);
        const double width = static_cast<double>(
            std::max<LONG>(1, plot.right - plot.left));
        const double base_group = width / static_cast<double>(orders);
        const double center_x = (plot.left + plot.right) / 2.0;
        const double anchor = harmonic_center_order_ +
            (static_cast<double>(pixel) - center_x) /
                (base_group * harmonic_zoom_);
        const double max_zoom = std::max(
            1.0, std::min(32.0, static_cast<double>(orders)));
        harmonic_zoom_ = std::clamp(harmonic_zoom_ * scale, 1.0, max_zoom);
        harmonic_center_order_ = std::clamp(
            anchor - (static_cast<double>(pixel) - center_x) /
                         (base_group * harmonic_zoom_),
            1.0, static_cast<double>(orders));
        has_harmonic_center_ = true;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void setView(double low, double high) {
        if (!record_ || record_->record.data.times_s.empty()) {
            return;
        }
        const double first = record_->record.data.times_s.front();
        const double last = record_->record.data.times_s.back();
        const double full_span = std::max(1e-9, last - first);
        if (!(high > low)) {
            return;
        }
        if (high - low > full_span) {
            low = first;
            high = last;
        }
        if (low < first) {
            high += first - low;
            low = first;
        }
        if (high > last) {
            low -= high - last;
            high = last;
        }
        view_lo_ = std::max(first, low);
        view_hi_ = std::min(last, high);
        if (!(view_hi_ > view_lo_)) {
            view_lo_ = first;
            view_hi_ = last;
        }
        has_view_ = true;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    const LoadedRecord* record_ = nullptr;
    std::size_t channel_ = 0;
    std::size_t timeline_scroll_rows_ = 0;
    double phasor_zoom_ = 1.0;
    double phasor_offset_x_ = 0.0;
    double phasor_offset_y_ = 0.0;
    double harmonic_zoom_ = 1.0;
    double harmonic_center_order_ = 1.0;
    double drag_harmonic_center_ = 1.0;
    bool has_harmonic_center_ = false;
    ViewMode mode_ = ViewMode::Trace;
    double view_lo_ = 0.0;
    double view_hi_ = 1.0;
    bool has_view_ = false;
    bool cursor_valid_ = false;
    int cursor_x_ = 0;
    bool dragging_ = false;
    POINT drag_start_{};
    double drag_lo_ = 0.0;
    double drag_hi_ = 1.0;
    HFONT title_font_ = nullptr;
    HFONT body_font_ = nullptr;
    HFONT small_font_ = nullptr;
    int font_dpi_ = 0;
};

                                                                              
                    
                                                                              

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance)
        : instance_(instance), background_(::GetSysColorBrush(COLOR_WINDOW)) {}

    ~MainWindow() {
        closing_.store(true);
        if (worker_.joinable()) {
            worker_.join();
        }
        releaseFonts();
        if (accelerator_) {
            ::DestroyAcceleratorTable(accelerator_);
            accelerator_ = nullptr;
        }
    }

    bool create(int show_command, const std::wstring& initial_path) {
        if (!WaveformView::registerClass(instance_)) {
            return false;
        }
        initial_path_ = initial_path;

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &MainWindow::windowProc;
        window_class.hInstance = instance_;
        window_class.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kMainClass;
        if (!::RegisterClassExW(&window_class) &&
            ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        HMENU menu = createMenu();
        const int dpi = dpiForWindow(nullptr);
        const RECT work = workArea();
        const int work_width = static_cast<int>(work.right - work.left);
        const int work_height = static_cast<int>(work.bottom - work.top);
        const int width = std::min(scaleFor(dpi, 1280),
                                   std::max(scaleFor(dpi, 900), work_width - 80));
        const int height = std::min(scaleFor(dpi, 820),
                                    std::max(scaleFor(dpi, 620), work_height - 80));
        const int x = static_cast<int>(work.left) + std::max(0, (work_width - width) / 2);
        const int y = static_cast<int>(work.top) + std::max(0, (work_height - height) / 2);

        hwnd_ = ::CreateWindowExW(
            0, kMainClass, L"Analizador COMTRADE",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height, nullptr,
            menu, instance_, this);
        if (!hwnd_) {
            if (menu) {
                ::DestroyMenu(menu);
            }
            return false;
        }

        ACCEL accelerators[] = {
            {FVIRTKEY | FCONTROL, 'O', kCommandOpen},
            {FVIRTKEY | FCONTROL, 'E', kCommandExportHtml},
            {FVIRTKEY | FCONTROL, 'S', kCommandExportCsv},
            {FVIRTKEY | FCONTROL, 'J', kCommandExportJson},
            {FVIRTKEY | FCONTROL, '0', kCommandResetView},
            {FVIRTKEY, VK_F1, kCommandAbout},
        };
        accelerator_ = ::CreateAcceleratorTableW(
            accelerators, static_cast<int>(sizeof(accelerators) /
                                           sizeof(accelerators[0])));

        ::ShowWindow(hwnd_, show_command);
        ::UpdateWindow(hwnd_);
        return true;
    }

    int run() {
        if (analizador::runtime_guard::debuggerAttached()) {
            closeForDebugger();
        } else if (::SetTimer(hwnd_, analizador::runtime_guard::kDebuggerCheckTimerId,
                              analizador::runtime_guard::kDebuggerCheckIntervalMs,
                              nullptr) == 0) {
            runtime_exit_code_ = 1;
            ::MessageBoxW(hwnd_, L"No se pudo iniciar el control de protección. La aplicación se cerrará.",
                          L"Analizador COMTRADE", MB_OK | MB_ICONERROR);
            ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        } else if (!initial_path_.empty()) {
            openPaths({std::filesystem::path(initial_path_)});
        }
        MSG message{};
        while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (accelerator_ && ::TranslateAcceleratorW(hwnd_, accelerator_, &message)) continue;
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    bool runtime_guard_closing_ = false;
    int runtime_exit_code_ = 0;
    void closeForDebugger() {
        if (runtime_guard_closing_) return;
        runtime_guard_closing_ = true;
        runtime_exit_code_ = ERROR_ACCESS_DENIED;
        ::KillTimer(hwnd_, analizador::runtime_guard::kDebuggerCheckTimerId);
        analizador::runtime_guard::showDebuggerWarning(hwnd_);
        ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam,
                                       LPARAM lparam) {
        MainWindow* self = reinterpret_cast<MainWindow*>(
            ::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<MainWindow*>(create->lpCreateParams);
            if (self) {
                self->hwnd_ = window;
                ::SetWindowLongPtrW(window, GWLP_USERDATA,
                                    reinterpret_cast<LONG_PTR>(self));
            }
        }
        if (self) {
            return self->handleMessage(message, wparam, lparam);
        }
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    static LRESULT CALLBACK diagnosticsListProc(HWND window, UINT message,
                                               WPARAM wparam, LPARAM lparam) {
        auto* self = static_cast<MainWindow*>(::GetPropW(
            window, L"AnalizadorComtrade.DiagnosticsOwner"));
        if (!self || !self->diagnostics_original_proc_) {
            return ::DefWindowProcW(window, message, wparam, lparam);
        }
        if (message == WM_CONTEXTMENU) {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1) {
                ::GetCursorPos(&point);
            }
            self->showDiagnosticsContextMenu(point);
            return 0;
        }
        if (message == WM_NCDESTROY) {
            WNDPROC original = self->diagnostics_original_proc_;
            ::RemovePropW(window, L"AnalizadorComtrade.DiagnosticsOwner");
            self->diagnostics_original_proc_ = nullptr;
            return ::CallWindowProcW(original, window, message, wparam, lparam);
        }
        return ::CallWindowProcW(self->diagnostics_original_proc_, window,
                                 message, wparam, lparam);
    }

    LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_CREATE:
                if (!createChildren()) {
                    return -1;
                }
                return 0;
            case WM_SIZE:
                layout();
                return 0;
            case WM_DPICHANGED:
                dpi_ = HIWORD(wparam);
                if (lparam) {
                    const auto* suggested =
                        reinterpret_cast<const RECT*>(lparam);
                    ::SetWindowPos(hwnd_, nullptr, suggested->left,
                                   suggested->top,
                                   suggested->right - suggested->left,
                                   suggested->bottom - suggested->top,
                                   SWP_NOZORDER | SWP_NOACTIVATE);
                }
                recreateFonts();
                layout();
                ::InvalidateRect(hwnd_, nullptr, TRUE);
                return 0;
            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
                const int dpi = dpiForWindow(hwnd_);
                info->ptMinTrackSize.x = scaleFor(dpi, 900);
                info->ptMinTrackSize.y = scaleFor(dpi, 600);
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
                paintBackground();
                return 0;
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(wparam);
                ::SetBkMode(dc, TRANSPARENT);
                ::SetTextColor(dc, RGB(61, 75, 94));
                return reinterpret_cast<LRESULT>(background_);
            }
            case WM_COMMAND:
                onCommand(LOWORD(wparam), HIWORD(wparam),
                          reinterpret_cast<HWND>(lparam));
                return 0;
            case WM_DROPFILES:
                onDropFiles(reinterpret_cast<HDROP>(wparam));
                return 0;
            case kMessageLoadDone:
                onLoadDone();
                return 0;
            case WM_TIMER:
                if (wparam == analizador::runtime_guard::kDebuggerCheckTimerId) {
                    if (!runtime_guard_closing_ && analizador::runtime_guard::debuggerAttached()) closeForDebugger();
                    return 0;
                }
                break;
            case WM_CLOSE:
                closing_.store(true);
                if (worker_.joinable()) {
                    worker_.join();
                }
                ::DestroyWindow(hwnd_);
                return 0;
            case WM_DESTROY:
                closing_.store(true);
                if (worker_.joinable()) {
                    worker_.join();
                }
                ::KillTimer(hwnd_, 1);
                ::KillTimer(hwnd_, analizador::runtime_guard::kDebuggerCheckTimerId);
                releaseFonts();
                ::PostQuitMessage(runtime_exit_code_);
                return 0;
            case WM_NCDESTROY: {
                HWND window = hwnd_;
                ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                hwnd_ = nullptr;
                return ::DefWindowProcW(window, message, wparam, lparam);
            }
            default:
                break;
        }
        return ::DefWindowProcW(hwnd_, message, wparam, lparam);
    }

    HMENU createMenu() {
        HMENU file = ::CreatePopupMenu();
        HMENU view = ::CreatePopupMenu();
        HMENU help = ::CreatePopupMenu();
        if (!file || !view || !help) {
            if (file) {
                ::DestroyMenu(file);
            }
            if (view) {
                ::DestroyMenu(view);
            }
            if (help) {
                ::DestroyMenu(help);
            }
            return nullptr;
        }
        ::AppendMenuW(file, MF_STRING, kCommandOpen,
                      L"&Abrir registro…\tCtrl+O");
        ::AppendMenuW(file, MF_STRING, kCommandOpenPair,
                      L"Abrir CFG y &DAT…");
        ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(file, MF_STRING, kCommandExportHtml,
                      L"Exportar informe &HTML…\tCtrl+E");
        ::AppendMenuW(file, MF_STRING, kCommandExportCsv,
                      L"Exportar datos &CSV…\tCtrl+S");
        ::AppendMenuW(file, MF_STRING, kCommandExportJson,
                      L"Exportar resumen &JSON…\tCtrl+J");
        ::AppendMenuW(file, MF_STRING, kCommandExportPackage,
                      L"Exportar paquete ZIP de análisis…");
        ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(file, MF_STRING, kCommandAnalyzeBatch,
                      L"Analizar lote de ZIP(s)…");
        ::AppendMenuW(file, MF_STRING, kCommandCompare,
                      L"Comparar registros cargados…");
        ::AppendMenuW(file, MF_STRING, kCommandExportBatchHtml,
                      L"Exportar informe de lote HTML…");
        ::AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(file, MF_STRING, kCommandExit, L"&Salir");

        ::AppendMenuW(view, MF_STRING, kCommandModeTrace, L"&Trazas");
        ::AppendMenuW(view, MF_STRING, kCommandModePhasor, L"&Fasores");
        ::AppendMenuW(view, MF_STRING, kCommandModeTrend, L"&Tendencia");
        ::AppendMenuW(view, MF_STRING, kCommandModeHarmonic, L"&Armónicos");
        ::AppendMenuW(view, MF_STRING, kCommandModeTimeline, L"&Eventos");
        ::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(view, MF_STRING, kCommandResetView,
                      L"&Ajustar vista\tCtrl+0");
        ::AppendMenuW(view, MF_STRING, kCommandZoomIn, L"A&cercar");
        ::AppendMenuW(view, MF_STRING, kCommandZoomOut, L"A&lejar");
        ::AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(view, MF_STRING, kCommandBreakerInfo,
                      L"&Tiempos de interruptor…");

        ::AppendMenuW(help, MF_STRING, kCommandAbout,
                      L"&Acerca de Analizador COMTRADE\tF1");

        HMENU main_menu = ::CreateMenu();
        if (!main_menu) {
            ::DestroyMenu(file);
            ::DestroyMenu(view);
            ::DestroyMenu(help);
            return nullptr;
        }
        ::AppendMenuW(main_menu, MF_POPUP,
                      reinterpret_cast<UINT_PTR>(file), L"&Archivo");
        ::AppendMenuW(main_menu, MF_POPUP,
                      reinterpret_cast<UINT_PTR>(view), L"&Ver");
        ::AppendMenuW(main_menu, MF_POPUP,
                      reinterpret_cast<UINT_PTR>(help), L"A&yuda");
        return main_menu;
    }

    bool createChildren() {
        dpi_ = dpiForWindow(hwnd_);
        recreateFonts();

        open_button_ = createButton(kCommandOpen, L"Abrir ZIP/CFG/DAT…");
        html_button_ = createButton(kCommandExportHtml, L"Informe HTML");
        csv_button_ = createButton(kCommandExportCsv, L"Datos CSV");
        package_button_ = createButton(kCommandExportPackage, L"Paquete ZIP");
        batch_button_ = createButton(kCommandAnalyzeBatch, L"Analizar lote");
        compare_button_ = createButton(kCommandCompare, L"Comparar");

        record_label_ = createStatic(kControlRecordLabel, L"Registro:");
        channel_label_ =
            createStatic(kControlChannelLabel, L"Canales analógicos");
        diagnostics_label_ =
            createStatic(kControlDiagnosticsLabel, L"Diagnósticos");

        phase_a_ = createCombo(kControlPhaseA);
        phase_b_ = createCombo(kControlPhaseB);
        phase_c_ = createCombo(kControlPhaseC);
        cycles_ = createCombo(kControlCycles);
        ::SendMessageW(cycles_, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(L"1 ciclo"));
        ::SendMessageW(cycles_, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(L"2 ciclos"));
        ::SendMessageW(cycles_, CB_ADDSTRING, 0,
                       reinterpret_cast<LPARAM>(L"3 ciclos"));
        ::SendMessageW(cycles_, CB_SETCURSEL, 0, 0);
        unit_channel_ = createCombo(kControlUnitChannel);
        onset_edit_ = createEdit(kControlOnset, L"1.5");
        pre_edit_ = createEdit(kControlPre, L"0");
        fault_edit_ = createEdit(kControlFault, L"0");
        line_edit_ = createEdit(kControlLineLength, L"0");
        log_edit_ = createEdit(kControlLogOffset, L"0");
        unit_edit_ = createEdit(kControlUnitText, L"");
        apply_button_ = createButton(kCommandApply, L"Aplicar");

                                                                          
                                                                  
        trip_combo_ = createCombo(kControlTrip);
        contact52a_combo_ = createCombo(kControlContact52a);
        contact52b_combo_ = createCombo(kControlContact52b);
        trip_polarity_ = createCheckBox(kControlTripPolarity, L"Activo alto");
        contact52a_polarity_ =
            createCheckBox(kControlContact52aPolarity, L"Activo alto");
        contact52b_polarity_ =
            createCheckBox(kControlContact52bPolarity, L"Activo alto");
        trip_label_ = createStatic(60011, L"Trip");
        contact52a_label_ = createStatic(60012, L"52a=cerrado");
        contact52b_label_ = createStatic(60013, L"52b=abierto");

        onset_label_ = createStatic(60001, L"Sens.");
        pre_label_ = createStatic(60002, L"Prefalla s");
        fault_label_ = createStatic(60003, L"Falla s");
        line_label_ = createStatic(60004, L"Línea km");
        log_label_ = createStatic(60005, L"Reloj log s");
        unit_label_ = createStatic(60006, L"Unidad");
        phase_a_label_ = createStatic(60007, L"Fase A");
        phase_b_label_ = createStatic(60008, L"Fase B");
        phase_c_label_ = createStatic(60009, L"Fase C");
        cycles_label_ = createStatic(60010, L"Ciclos");

        record_combo_ = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                CBS_DROPDOWNLIST,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kControlRecordCombo)),
            instance_, nullptr);
        channel_list_ = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kControlChannelList)),
            instance_, nullptr);
        diagnostics_list_ = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(
                static_cast<INT_PTR>(kControlDiagnosticsList)),
            instance_, nullptr);
        status_ = ::CreateWindowExW(
            0, L"STATIC", L"Abra un ZIP o un par CFG/DAT para comenzar.",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlStatus)),
            instance_, nullptr);

        const wchar_t* mode_names[] = {L"Trazas", L"Fasores", L"Tendencia",
                                       L"Armónicos", L"Eventos"};
        for (std::size_t index = 0; index < 5; ++index) {
            mode_buttons_[index] = createButton(
                static_cast<UINT>(kCommandModeTrace) +
                    static_cast<UINT>(index),
                mode_names[index]);
        }

        if (!open_button_ || !html_button_ || !csv_button_ ||
            !package_button_ || !batch_button_ || !compare_button_ ||
            !record_label_ || !channel_label_ || !diagnostics_label_ ||
            !record_combo_ || !channel_list_ || !diagnostics_list_ ||
            !status_ || !phase_a_ || !phase_b_ || !phase_c_ || !cycles_ ||
            !onset_edit_ || !pre_edit_ || !fault_edit_ || !line_edit_ ||
            !log_edit_ || !unit_channel_ || !unit_edit_ || !apply_button_ ||
            !trip_combo_ || !contact52a_combo_ || !contact52b_combo_ ||
            !trip_polarity_ || !contact52a_polarity_ ||
            !contact52b_polarity_ || !trip_label_ || !contact52a_label_ ||
            !contact52b_label_ || !onset_label_ || !pre_label_ ||
            !fault_label_ || !line_label_ || !log_label_ || !unit_label_ ||
            !phase_a_label_ || !phase_b_label_ || !phase_c_label_ ||
            !cycles_label_) {
            return false;
        }
        for (HWND button : mode_buttons_) {
            if (!button) {
                return false;
            }
        }
        if (!waveform_.create(hwnd_, instance_, 50020)) {
            return false;
        }

        setControlFont(open_button_, ui_font_);
        setControlFont(html_button_, ui_font_);
        setControlFont(csv_button_, ui_font_);
        setControlFont(package_button_, ui_font_);
        setControlFont(batch_button_, ui_font_);
        setControlFont(compare_button_, ui_font_);
        setControlFont(record_label_, ui_font_);
        setControlFont(channel_label_, ui_font_);
        setControlFont(diagnostics_label_, ui_font_);
        setControlFont(record_combo_, ui_font_);
        setControlFont(channel_list_, ui_font_);
        setControlFont(diagnostics_list_, ui_font_);
        if (!::SetPropW(diagnostics_list_,
                        L"AnalizadorComtrade.DiagnosticsOwner", this)) {
            return false;
        }
        diagnostics_original_proc_ = reinterpret_cast<WNDPROC>(
            ::SetWindowLongPtrW(
                diagnostics_list_, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&MainWindow::diagnosticsListProc)));
        if (!diagnostics_original_proc_) {
            ::RemovePropW(diagnostics_list_,
                          L"AnalizadorComtrade.DiagnosticsOwner");
            return false;
        }
        setControlFont(status_, small_font_);
        for (HWND control : {phase_a_, phase_b_, phase_c_, cycles_,
                             unit_channel_, onset_edit_, pre_edit_, fault_edit_,
                             line_edit_, log_edit_, unit_edit_, apply_button_,
                             trip_combo_, contact52a_combo_,
                             contact52b_combo_, trip_polarity_,
                             contact52a_polarity_, contact52b_polarity_,
                             onset_label_, pre_label_, fault_label_, line_label_,
                             log_label_, unit_label_, phase_a_label_,
                             phase_b_label_, phase_c_label_, cycles_label_,
                             trip_label_, contact52a_label_,
                             contact52b_label_}) {
            setControlFont(control, ui_font_);
        }
        for (HWND button : mode_buttons_) {
            setControlFont(button, ui_font_);
        }

        ::SendMessageW(record_combo_, CB_SETDROPPEDWIDTH,
                       static_cast<WPARAM>(scaleFor(dpi_, 460)), 0);
        ::DragAcceptFiles(hwnd_, TRUE);
        updateControls();
        return true;
    }

    HWND createButton(UINT command, const wchar_t* text) {
        return ::CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0,
            hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(command)),
            instance_, nullptr);
    }

    HWND createStatic(UINT id, const wchar_t* text) {
        return ::CreateWindowExW(
            0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_,
            nullptr);
    }

    HWND createCombo(UINT id) {
        return ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                CBS_DROPDOWNLIST,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_,
            nullptr);
    }

    HWND createEdit(UINT id, const wchar_t* text) {
        return ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_,
            nullptr);
    }

    HWND createCheckBox(UINT id, const wchar_t* text) {
        return ::CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 0, 0, hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_,
            nullptr);
    }

    void recreateFonts() {
        releaseFonts();
        dpi_ = dpiForWindow(hwnd_);
        ui_font_ = makeFont(dpi_, 9, FW_NORMAL);
        small_font_ = makeFont(dpi_, 8, FW_NORMAL);
        if (!hwnd_) {
            return;
        }
        setControlFont(open_button_, ui_font_);
        setControlFont(html_button_, ui_font_);
        setControlFont(csv_button_, ui_font_);
        setControlFont(record_label_, ui_font_);
        setControlFont(channel_label_, ui_font_);
        setControlFont(diagnostics_label_, ui_font_);
        setControlFont(record_combo_, ui_font_);
        setControlFont(channel_list_, ui_font_);
        setControlFont(diagnostics_list_, ui_font_);
        setControlFont(status_, small_font_);
        setControlFont(package_button_, ui_font_);
        setControlFont(batch_button_, ui_font_);
        setControlFont(compare_button_, ui_font_);
        for (HWND control : {phase_a_, phase_b_, phase_c_, cycles_,
                             unit_channel_, onset_edit_, pre_edit_, fault_edit_,
                             line_edit_, log_edit_, unit_edit_, apply_button_,
                             trip_combo_, contact52a_combo_,
                             contact52b_combo_, trip_polarity_,
                             contact52a_polarity_, contact52b_polarity_,
                             onset_label_, pre_label_, fault_label_, line_label_,
                             log_label_, unit_label_, phase_a_label_,
                             phase_b_label_, phase_c_label_, cycles_label_,
                             trip_label_, contact52a_label_,
                             contact52b_label_}) {
            setControlFont(control, ui_font_);
        }
        for (HWND button : mode_buttons_) {
            setControlFont(button, ui_font_);
        }
    }

    void releaseFonts() {
        if (ui_font_) {
            ::DeleteObject(ui_font_);
            ui_font_ = nullptr;
        }
        if (small_font_) {
            ::DeleteObject(small_font_);
            small_font_ = nullptr;
        }
    }

    void layout() {
        if (!hwnd_) {
            return;
        }
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const int width = static_cast<int>(std::max<LONG>(0, client.right - client.left));
        const int height = static_cast<int>(std::max<LONG>(0, client.bottom - client.top));
        if (width == 0 || height == 0) {
            return;
        }
        dpi_ = dpiForWindow(hwnd_);
        const int pad = scaleFor(dpi_, 12);
        const int gap = scaleFor(dpi_, 8);
        const int top = scaleFor(dpi_, 8);
        const int button_height = scaleFor(dpi_, 30);
        const int status_height = scaleFor(dpi_, 26);
        const int left_width =
            std::min(scaleFor(dpi_, 330),
                     std::max(scaleFor(dpi_, 240), width / 3));

        const int row1 = top;
        const int row2 = row1 + button_height + gap;
        const int row3 = row2 + button_height + gap;
        const int row4 = row3 + button_height + gap;
        const int row5 = row4 + button_height + gap;
        const int row6 = row5 + button_height + gap;
        const int top_band = row6 + button_height + gap;

        int x = pad;
        ::MoveWindow(open_button_, x, row1, scaleFor(dpi_, 152),
                     button_height, TRUE);
        x += scaleFor(dpi_, 152) + gap;
        ::MoveWindow(html_button_, x, row1, scaleFor(dpi_, 104),
                     button_height, TRUE);
        x += scaleFor(dpi_, 104) + gap;
        ::MoveWindow(csv_button_, x, row1, scaleFor(dpi_, 86), button_height,
                     TRUE);
        x += scaleFor(dpi_, 86) + gap;
        ::MoveWindow(package_button_, x, row1, scaleFor(dpi_, 108),
                     button_height, TRUE);
        x += scaleFor(dpi_, 108) + gap;
        ::MoveWindow(batch_button_, x, row1, scaleFor(dpi_, 108),
                     button_height, TRUE);
        x += scaleFor(dpi_, 108) + gap;
        ::MoveWindow(compare_button_, x, row1, scaleFor(dpi_, 94),
                     button_height, TRUE);

        const int label_width = scaleFor(dpi_, 62);
        ::MoveWindow(record_label_, pad, row2, label_width, button_height, TRUE);
        ::MoveWindow(record_combo_, pad + label_width, row2,
                     std::max(scaleFor(dpi_, 140),
                              width - pad * 2 - label_width),
                     button_height, TRUE);

        int x3 = pad;
        const auto place_labeled = [&](HWND label, HWND control, int label_w,
                                       int control_w, int row) {
            ::MoveWindow(label, x3, row, scaleFor(dpi_, label_w),
                         button_height, TRUE);
            x3 += scaleFor(dpi_, label_w);
            ::MoveWindow(control, x3, row, scaleFor(dpi_, control_w),
                         button_height, TRUE);
            x3 += scaleFor(dpi_, control_w) + gap;
        };
        place_labeled(phase_a_label_, phase_a_, 42, 112, row3);
        place_labeled(phase_b_label_, phase_b_, 42, 112, row3);
        place_labeled(phase_c_label_, phase_c_, 42, 112, row3);
        place_labeled(cycles_label_, cycles_, 42, 82, row3);
        ::MoveWindow(apply_button_, x3, row3, scaleFor(dpi_, 78),
                     button_height, TRUE);

        x3 = pad;
        place_labeled(onset_label_, onset_edit_, 34, 52, row4);
        place_labeled(pre_label_, pre_edit_, 50, 78, row4);
        place_labeled(fault_label_, fault_edit_, 50, 78, row4);
        place_labeled(line_label_, line_edit_, 54, 70, row4);
        place_labeled(log_label_, log_edit_, 64, 70, row4);
        place_labeled(unit_label_, unit_channel_, 46, 110, row4);
        ::MoveWindow(unit_edit_, x3, row4, scaleFor(dpi_, 84), button_height,
                     TRUE);

        int x5 = pad;
        for (HWND button : mode_buttons_) {
            ::MoveWindow(button, x5, row5, scaleFor(dpi_, 92), button_height,
                         TRUE);
            x5 += scaleFor(dpi_, 92) + gap;
        }

        int x6 = pad;
        const auto place_breaker =
            [&](HWND label, HWND combo, HWND check, int label_w) {
                ::MoveWindow(label, x6, row6, scaleFor(dpi_, label_w),
                             button_height, TRUE);
                x6 += scaleFor(dpi_, label_w);
                ::MoveWindow(combo, x6, row6, scaleFor(dpi_, 100),
                             button_height, TRUE);
                x6 += scaleFor(dpi_, 100) + gap;
                ::MoveWindow(check, x6, row6, scaleFor(dpi_, 100),
                             button_height, TRUE);
                x6 += scaleFor(dpi_, 100) + gap;
            };
        place_breaker(trip_label_, trip_combo_, trip_polarity_, 32);
        place_breaker(contact52a_label_, contact52a_combo_,
                      contact52a_polarity_, 72);
        place_breaker(contact52b_label_, contact52b_combo_,
                      contact52b_polarity_, 72);

        const int content_top = top_band + pad;
        const int content_bottom = height - status_height - pad;
        const int available = std::max(scaleFor(dpi_, 120),
                                       content_bottom - content_top);
        const int channel_label_h = scaleFor(dpi_, 20);
        const int diagnostics_label_h = scaleFor(dpi_, 20);
        const int list_gap = scaleFor(dpi_, 6);
        int channel_list_h = std::max(scaleFor(dpi_, 120),
                                      available * 44 / 100);
        int diag_list_h = available - channel_label_h - diagnostics_label_h -
                          channel_list_h - list_gap * 2;
        if (diag_list_h < scaleFor(dpi_, 90)) {
            diag_list_h = scaleFor(dpi_, 90);
            channel_list_h = std::max(
                scaleFor(dpi_, 90),
                available - channel_label_h - diagnostics_label_h -
                    diag_list_h - list_gap * 2);
        }

        ::MoveWindow(channel_label_, pad, content_top,
                     left_width - pad * 2, channel_label_h, TRUE);
        ::MoveWindow(channel_list_, pad, content_top + channel_label_h,
                     left_width - pad * 2, channel_list_h, TRUE);
        const int diag_top = content_top + channel_label_h + channel_list_h +
                             list_gap;
        ::MoveWindow(diagnostics_label_, pad, diag_top,
                     left_width - pad * 2, diagnostics_label_h, TRUE);
        ::MoveWindow(diagnostics_list_, pad, diag_top + diagnostics_label_h,
                     left_width - pad * 2, std::max(scaleFor(dpi_, 60), diag_list_h),
                     TRUE);

        ::MoveWindow(waveform_.hwnd(), left_width, top_band,
                     std::max(scaleFor(dpi_, 200), width - left_width),
                     std::max(scaleFor(dpi_, 200),
                              height - top_band - status_height),
                     TRUE);
        ::MoveWindow(status_, 0, height - status_height, width, status_height,
                     TRUE);
        ::InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void paintBackground() {
        PAINTSTRUCT paint_struct{};
        HDC dc = ::BeginPaint(hwnd_, &paint_struct);
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        ::FillRect(dc, &client, background_);

        const int dpi = dpiForWindow(hwnd_);
        const int top_band = scaleFor(dpi, 8) + 6 * scaleFor(dpi, 30) +
                             6 * scaleFor(dpi, 8);
        const int status_height = scaleFor(dpi, 26);
        const HPEN pen = ::CreatePen(PS_SOLID, 1, RGB(214, 222, 233));
        const HGDIOBJ old = ::SelectObject(dc, pen);
        ::MoveToEx(dc, 0, top_band - 1, nullptr);
        ::LineTo(dc, client.right, top_band - 1);
        ::MoveToEx(dc, 0, client.bottom - status_height, nullptr);
        ::LineTo(dc, client.right, client.bottom - status_height);
        ::SelectObject(dc, old);
        ::DeleteObject(pen);
        ::EndPaint(hwnd_, &paint_struct);
    }

    void onCommand(UINT id, UINT code, HWND control) {
        const bool phase_combo = control == phase_a_ || control == phase_b_ ||
                                 control == phase_c_;
        const bool breaker_combo = control == trip_combo_ ||
                                   control == contact52a_combo_ ||
                                   control == contact52b_combo_;
        const bool dropdown_combo =
            phase_combo || breaker_combo || control == cycles_ ||
            control == unit_channel_;
        if (code == CBN_DROPDOWN && control && dropdown_combo) {
            if (phase_combo) {
                phase_dropdown_id_ = id;
                phase_selection_before_dropdown_ = ::SendMessageW(
                    control, CB_GETCURSEL, 0, 0);
            }
            resizeComboDropList(control, true);
            return;
        }
        if (code == CBN_CLOSEUP && control && dropdown_combo) {
            resizeComboDropList(control, false);
            if (phase_combo && id == phase_dropdown_id_) {
                const LRESULT selection = ::SendMessageW(
                    control, CB_GETCURSEL, 0, 0);
                const bool changed = selection != phase_selection_before_dropdown_;
                phase_dropdown_id_ = 0;
                phase_selection_before_dropdown_ = CB_ERR;
                if (changed && loaded_) {
                    applyAnalysisOptions(false);
                }
            }
            return;
        }
        if (id == kControlRecordCombo && code == CBN_SELCHANGE &&
            !suppress_combo_) {
            const int selection =
                static_cast<int>(::SendMessageW(record_combo_, CB_GETCURSEL, 0,
                                                0));
            if (selection >= 0 &&
                static_cast<std::size_t>(selection) < records_.size()) {
                beginLoad(static_cast<std::size_t>(selection));
            }
            return;
        }
        if (id == kControlUnitChannel && code == CBN_SELCHANGE && loaded_) {
            const int selection = static_cast<int>(
                ::SendMessageW(unit_channel_, CB_GETCURSEL, 0, 0));
            if (selection >= 0) {
                const LRESULT data = ::SendMessageW(
                    unit_channel_, CB_GETITEMDATA, selection, 0);
                if (data != CB_ERR && data >= 0 &&
                    static_cast<std::size_t>(data) <
                        loaded_->record.cfg.analog.size()) {
                    const std::size_t index = static_cast<std::size_t>(data);
                    const auto found = loaded_->unit_overrides.find(index);
                    const std::string unit =
                        found != loaded_->unit_overrides.end()
                            ? found->second
                            : loaded_->record.cfg.analog[index].unit;
                    SetWindowTextW(unit_edit_, utf8ToWide(unit).c_str());
                }
            }
            return;
        }
        if (breaker_combo && code == CBN_SELCHANGE) {
            updateBreakerPolarityEnabled();
            return;
        }
        if (id == kControlChannelList && code == LBN_SELCHANGE) {
            const int selection = static_cast<int>(
                ::SendMessageW(channel_list_, LB_GETCURSEL, 0, 0));
            if (selection >= 0) {
                const LRESULT data =
                    ::SendMessageW(channel_list_, LB_GETITEMDATA, selection, 0);
                if (data != LB_ERR && data >= 0) {
                    waveform_.setChannel(static_cast<std::size_t>(data));
                }
            }
            return;
        }
        (void)control;

        switch (id) {
            case kCommandOpen:
                chooseOpen(false);
                break;
            case kCommandOpenPair:
                chooseOpen(false);
                break;
            case kCommandExportHtml:
                chooseExport(ExportKind::Html);
                break;
            case kCommandExportCsv:
                chooseExport(ExportKind::Csv);
                break;
            case kCommandExportJson:
                chooseExport(ExportKind::Json);
                break;
            case kCommandExportPackage:
                chooseExport(ExportKind::Package);
                break;
            case kCommandAnalyzeBatch:
                analyzeBatch();
                break;
            case kCommandCompare:
                compareLoadedRecords();
                break;
            case kCommandExportBatchHtml:
                exportBatchHtml();
                break;
            case kCommandApply:
                applyAnalysisOptions();
                break;
            case kCommandModeTrace:
                waveform_.setMode(ViewMode::Trace);
                break;
            case kCommandModePhasor:
                waveform_.setMode(ViewMode::Phasor);
                break;
            case kCommandModeTrend:
                waveform_.setMode(ViewMode::Trend);
                break;
            case kCommandModeHarmonic:
                waveform_.setMode(ViewMode::Harmonic);
                break;
            case kCommandModeTimeline:
                waveform_.setMode(ViewMode::Timeline);
                break;
            case kCommandResetView:
                waveform_.resetView();
                break;
            case kCommandZoomIn:
                waveform_.zoomAtCenter(0.8);
                break;
            case kCommandZoomOut:
                waveform_.zoomAtCenter(1.25);
                break;
            case kCommandBreakerInfo:
                showBreakerInfo();
                break;
            case kCommandExit:
                ::PostMessageW(hwnd_, WM_CLOSE, 0, 0);
                break;
            case kCommandAbout:
                showAbout();
                break;
            default:
                break;
        }
    }

    void resizeComboDropList(HWND combo, bool expanded) {
        RECT rect{};
        if (!::GetWindowRect(combo, &rect)) {
            return;
        }
        ::MapWindowPoints(HWND_DESKTOP, hwnd_,
                          reinterpret_cast<POINT*>(&rect), 2);
        const int width = rect.right - rect.left;
        const int collapsed_height = scaleFor(dpi_, 30);
        int height = collapsed_height;
        if (expanded) {
            const int count = static_cast<int>(::SendMessageW(
                combo, CB_GETCOUNT, 0, 0));
            const int item_height = static_cast<int>(::SendMessageW(
                combo, CB_GETITEMHEIGHT, 0, 0));
            const int visible_items = std::min(10, std::max(1, count));
            height += visible_items *
                      std::max(item_height, scaleFor(dpi_, 22));
        }
        ::SetWindowPos(combo, nullptr, rect.left, rect.top, width, height,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }

    enum class ExportKind { Html, Csv, Json, Package };

    void chooseOpen(bool zip_only) {
        if (loading_) {
            return;
        }
        std::vector<wchar_t> buffer(64 * 1024, L'\0');
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrTitle =
            zip_only ? L"Abrir registro ZIP" : L"Abrir ZIP o CFG/DAT";
        dialog.lpstrFilter =
            zip_only
                ? L"Archivos ZIP (*.zip)\0*.zip\0Todos los archivos (*.*)\0*.*\0\0"
                : L"Registros COMTRADE (*.zip;*.cfg;*.dat)\0*.zip;*.cfg;*.dat\0"
                  L"Archivos ZIP (*.zip)\0*.zip\0Configuración (*.cfg)\0*.cfg\0"
                  L"Datos (*.dat)\0*.dat\0Todos los archivos (*.*)\0*.*\0\0";
        dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!zip_only) {
            dialog.Flags |= OFN_ALLOWMULTISELECT;
        }
        if (!::GetOpenFileNameW(&dialog)) {
            const DWORD error = ::CommDlgExtendedError();
            if (error != 0) {
                showError("No se pudo mostrar el selector de archivos. Código " +
                          std::to_string(error) + ".");
            }
            return;
        }
        const auto paths = parseOpenDialog(buffer);
        if (paths.empty()) {
            return;
        }
        if (zip_only && paths.size() != 1) {
            showError("Seleccione un único archivo ZIP.");
            return;
        }
        openPaths(paths);
    }

    static std::vector<std::filesystem::path> parseOpenDialog(
        const std::vector<wchar_t>& buffer) {
        std::vector<std::filesystem::path> paths;
        if (buffer.empty() || buffer.front() == L'\0') {
            return paths;
        }
        const std::wstring first(buffer.data());
        const std::wstring second(buffer.data() + first.size() + 1);
        if (second.empty()) {
            paths.emplace_back(first);
            return paths;
        }
        const std::filesystem::path directory(first);
        const wchar_t* cursor = buffer.data() + first.size() + 1;
        while (*cursor != L'\0') {
            const std::wstring name(cursor);
            paths.emplace_back(directory / name);
            cursor += name.size() + 1;
        }
        return paths;
    }

    void openPaths(const std::vector<std::filesystem::path>& paths) {
        if (loading_ || paths.empty()) {
            return;
        }
        try {
            std::vector<std::filesystem::path> zips;
            std::vector<std::filesystem::path> direct;
            for (const auto& path : paths) {
                if (pathExtensionLower(path) == L".zip") {
                    zips.push_back(path);
                } else {
                    direct.push_back(path);
                }
            }
            OpenBundle bundle;
            {
                WaitCursor wait;
                for (const auto& zip_path : zips) {
                    OpenBundle part = buildZipBundle(zip_path);
                    bundle.records.insert(
                        bundle.records.end(),
                        std::make_move_iterator(part.records.begin()),
                        std::make_move_iterator(part.records.end()));
                    bundle.diagnostics.insert(
                        bundle.diagnostics.end(),
                        std::make_move_iterator(part.diagnostics.begin()),
                        std::make_move_iterator(part.diagnostics.end()));
                    if (!part.error.empty()) {
                        bundle.diagnostics.push_back(makeDiagnostic(
                            DiagnosticSeverity::Warning, "ui.zip.partial",
                            part.error));
                    }
                }
                if (!direct.empty()) {
                    OpenBundle part = buildDirectBundle(direct);
                    bundle.records.insert(
                        bundle.records.end(),
                        std::make_move_iterator(part.records.begin()),
                        std::make_move_iterator(part.records.end()));
                    bundle.diagnostics.insert(
                        bundle.diagnostics.end(),
                        std::make_move_iterator(part.diagnostics.begin()),
                        std::make_move_iterator(part.diagnostics.end()));
                    if (!part.error.empty()) {
                        bundle.diagnostics.push_back(makeDiagnostic(
                            DiagnosticSeverity::Warning, "ui.direct.partial",
                            part.error));
                    }
                }
            }
                                                                         
            std::set<std::wstring> used;
            for (auto& record : bundle.records) {
                const std::wstring base = record.label;
                std::size_t suffix = 2;
                while (!used.insert(lowerWide(record.label)).second) {
                    record.label = base + L" [" + formatCountW(suffix++) + L"]";
                }
            }
            if (bundle.records.empty()) {
                bundle.error = firstErrorText(bundle.diagnostics);
                if (bundle.error.empty()) {
                    bundle.error = "No se encontró un registro utilizable.";
                }
            }
            applyOpenBundle(std::move(bundle));
        } catch (const std::exception& exception) {
            showError(std::string("No se pudo abrir el registro: ") +
                      exception.what());
        } catch (...) {
            showError("No se pudo abrir el registro por un error inesperado.");
        }
    }

    void applyOpenBundle(OpenBundle bundle) {
        if (bundle.records.empty()) {
            open_diagnostics_ = std::move(bundle.diagnostics);
            refreshDiagnostics();
            last_load_diagnostics_.clear();
            updateStatus();
            showError(bundle.error.empty()
                          ? "No se encontró un registro utilizable."
                          : bundle.error);
            return;
        }

        loaded_.reset();
        waveform_.setRecord(nullptr);
        ::SendMessageW(channel_list_, LB_RESETCONTENT, 0, 0);
        breaker_options_by_record_.clear();
        populateBreakerControls();
        records_ = std::move(bundle.records);
        open_diagnostics_ = std::move(bundle.diagnostics);
        last_load_diagnostics_.clear();
        fillRecordCombo();
        refreshDiagnostics();
        updateStatus();

        ::SendMessageW(record_combo_, CB_SETCURSEL, 0, 0);
        beginLoad(0);
    }

    void fillRecordCombo() {
        suppress_combo_ = true;
        ::SendMessageW(record_combo_, CB_RESETCONTENT, 0, 0);
        for (const auto& record : records_) {
            ::SendMessageW(record_combo_, CB_ADDSTRING, 0,
                           reinterpret_cast<LPARAM>(record.label.c_str()));
        }
        if (records_.empty()) {
            ::SendMessageW(record_combo_, CB_SETCURSEL,
                           static_cast<WPARAM>(-1), 0);
        }
        suppress_combo_ = false;
    }

    void beginLoad(std::size_t index) {
        if (loading_ || index >= records_.size()) {
            return;
        }
        if (worker_.joinable()) {
            worker_.join();
        }
        loading_ = true;
        selected_index_ = index;
        last_load_diagnostics_.clear();
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_.reset();
        }
        updateControls();
        updateStatus();
        SetWindowTextW(hwnd_, (L"Analizador COMTRADE — leyendo " +
                               records_[index].label + L"…")
                                  .c_str());
        ::SetCursor(::LoadCursorW(nullptr, IDC_WAIT));

        const RecordDescriptor descriptor = records_[index];
        analysis::AnalysisOptions options;
        options.cycles = cycle_count_;
        options.onset_ratio = onset_ratio_;
        options.reference_length_km = reference_length_km_;
                                                                             
                                                                              
        if (const auto found = breaker_options_by_record_.find(index);
            found != breaker_options_by_record_.end()) {
            options.breaker = found->second;
        }
        try {
            worker_ = std::thread([this, descriptor, options]() {
                std::shared_ptr<LoadResult> result;
                try {
                    result = std::make_shared<LoadResult>(
                        loadRecord(descriptor, options));
                } catch (const std::exception& exception) {
                    result = std::make_shared<LoadResult>();
                    result->error = std::string("Error inesperado al cargar: ") +
                                    exception.what();
                    result->diagnostics.push_back(makeDiagnostic(
                        DiagnosticSeverity::Error, "ui.load.exception",
                        result->error));
                } catch (...) {
                    result = std::make_shared<LoadResult>();
                    result->error = "Error inesperado al cargar el registro.";
                    result->diagnostics.push_back(makeDiagnostic(
                        DiagnosticSeverity::Error, "ui.load.exception",
                        result->error));
                }
                if (closing_.load()) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(pending_mutex_);
                    pending_ = result;
                }
                if (closing_.load()) {
                    return;
                }
                if (!::PostMessageW(hwnd_, kMessageLoadDone, 0, 0)) {
                    std::lock_guard<std::mutex> lock(pending_mutex_);
                    pending_.reset();
                }
            });
        } catch (const std::exception& exception) {
            loading_ = false;
            updateControls();
            last_load_diagnostics_.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "ui.thread",
                std::string("No se pudo iniciar la carga en segundo plano: ") +
                    exception.what()));
            updateStatus();
            updateTitle();
            refreshDiagnostics();
            showError("No se pudo iniciar la carga en segundo plano.");
        }
    }

    void onLoadDone() {
        if (worker_.joinable()) {
            worker_.join();
        }
        std::shared_ptr<LoadResult> result;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            result = pending_;
            pending_.reset();
        }
        loading_ = false;
        updateControls();
        if (!result) {
            updateStatus();
            updateTitle();
            return;
        }
        if (!result->loaded) {
            last_load_diagnostics_ = result->diagnostics;
            loaded_.reset();
            waveform_.setRecord(nullptr);
            ::SendMessageW(channel_list_, LB_RESETCONTENT, 0, 0);
            populateBreakerControls();
            updateControls();
            refreshDiagnostics();
            updateStatus();
            updateTitle();
            showError(result->error.empty()
                          ? "No se pudo cargar el registro."
                          : result->error);
            return;
        }

        last_load_diagnostics_.clear();
        loaded_ = result->loaded;
        waveform_.setRecord(loaded_.get());
        populateChannels();
        populatePhaseControls();
        populateBreakerControls();
        updateControls();
        refreshDiagnostics();
        updateStatus();
        updateTitle();
        if (!result->ok) {
            showMessage(result->error.empty()
                            ? "El registro se cargó parcialmente; revise los "
                              "diagnósticos."
                            : "El registro se cargó parcialmente: " +
                                  result->error,
                        MB_ICONWARNING);
        }
    }

    void populateChannels() {
        ::SendMessageW(channel_list_, LB_RESETCONTENT, 0, 0);
        if (!loaded_ || loaded_->record.cfg.analog.empty()) {
            ::EnableWindow(channel_list_, FALSE);
            waveform_.setChannel(0);
            return;
        }
        ::EnableWindow(channel_list_, TRUE);
        for (std::size_t index = 0;
             index < loaded_->record.cfg.analog.size(); ++index) {
            const auto& channel = loaded_->record.cfg.analog[index];
            std::wstring text = formatCountW(index + 1) + L". ";
            text += utf8ToWide(channel.name.empty()
                                   ? "Canal sin nombre"
                                   : channel.name);
            const auto override = loaded_->unit_overrides.find(index);
            const std::string shown_unit =
                override != loaded_->unit_overrides.end() ? override->second
                                                          : channel.unit;
            if (!shown_unit.empty()) {
                text += L" [";
                text += utf8ToWide(shown_unit);
                text += L"]";
            }
            const LRESULT item =
                ::SendMessageW(channel_list_, LB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(text.c_str()));
            if (item != LB_ERR) {
                ::SendMessageW(channel_list_, LB_SETITEMDATA, item,
                               static_cast<LPARAM>(index));
            }
        }
        std::size_t preferred = 0;
        if (loaded_->analysis_available &&
            loaded_->analysis.phases.index[0].has_value()) {
            preferred = *loaded_->analysis.phases.index[0];
            if (preferred >= loaded_->record.cfg.analog.size()) {
                preferred = 0;
            }
        }
        ::SendMessageW(channel_list_, LB_SETCURSEL, preferred, 0);
        waveform_.setChannel(preferred);
        ::SendMessageW(channel_list_, LB_SETHORIZONTALEXTENT,
                       static_cast<WPARAM>(scaleFor(dpi_, 620)), 0);
    }

    void populatePhaseControls() {
        if (!loaded_) {
            return;
        }
        const auto& analog = loaded_->record.cfg.analog;
        for (HWND combo : {phase_a_, phase_b_, phase_c_, unit_channel_}) {
            ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        }
        for (std::size_t index = 0; index < analog.size(); ++index) {
            std::wstring text = formatCountW(index + 1) + L". ";
            text += utf8ToWide(analog[index].name.empty()
                                   ? "Canal sin nombre"
                                   : analog[index].name);
            if (!analog[index].unit.empty()) {
                text += L" [";
                text += utf8ToWide(analog[index].unit);
                text += L"]";
            }
            for (HWND combo : {phase_a_, phase_b_, phase_c_, unit_channel_}) {
                const LRESULT item = ::SendMessageW(
                    combo, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(text.c_str()));
                if (item != CB_ERR) {
                    ::SendMessageW(combo, CB_SETITEMDATA, item,
                                   static_cast<LPARAM>(index));
                }
            }
        }
        for (std::size_t slot = 0; slot < 3; ++slot) {
            HWND combo = slot == 0 ? phase_a_ : (slot == 1 ? phase_b_ : phase_c_);
            const auto& index = loaded_->phase_indices[slot];
            if (index.has_value() && *index < analog.size()) {
                ::SendMessageW(combo, CB_SETCURSEL,
                               static_cast<WPARAM>(*index), 0);
            } else {
                ::SendMessageW(combo, CB_SETCURSEL,
                               static_cast<WPARAM>(-1), 0);
            }
            ::SendMessageW(combo, CB_SETDROPPEDWIDTH,
                           static_cast<WPARAM>(scaleFor(dpi_, 300)), 0);
        }
        ::SendMessageW(cycles_, CB_SETCURSEL,
                       static_cast<WPARAM>(
                           std::min(2, std::max(0, loaded_->cycles - 1))),
                       0);
        SetWindowTextW(onset_edit_,
                       formatNumberW(loaded_->onset_ratio, 4).c_str());
        SetWindowTextW(pre_edit_,
                       formatNumberW(loaded_->prefalla_center_s, 9).c_str());
        SetWindowTextW(fault_edit_,
                       formatNumberW(loaded_->falla_center_s, 9).c_str());
        SetWindowTextW(line_edit_,
                       formatNumberW(loaded_->reference_length_km, 6).c_str());
        SetWindowTextW(log_edit_,
                       formatNumberW(loaded_->log_clock_offset_s, 6).c_str());
        ::SendMessageW(unit_channel_, CB_SETCURSEL, 0, 0);
        if (!analog.empty()) {
            SetWindowTextW(unit_edit_,
                           utf8ToWide(analog.front().unit).c_str());
        }
    }

    void populateBreakerControls() {
        if (!trip_combo_ || !contact52a_combo_ || !contact52b_combo_) {
            return;
        }
        const auto fill = [&](HWND combo) {
            ::SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            const LRESULT unassigned = ::SendMessageW(
                combo, CB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(L"Sin asignar"));
            if (unassigned != CB_ERR) {
                ::SendMessageW(combo, CB_SETITEMDATA, unassigned, -1);
            }
            const std::size_t count =
                loaded_ ? loaded_->record.cfg.digital.size() : 0;
            for (std::size_t index = 0; index < count; ++index) {
                const auto& channel = loaded_->record.cfg.digital[index];
                std::wstring text = formatCountW(index + 1) + L". ";
                text += utf8ToWide(channel.name.empty()
                                       ? "Canal digital sin nombre"
                                       : channel.name);
                const LRESULT item = ::SendMessageW(
                    combo, CB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(text.c_str()));
                if (item != CB_ERR) {
                    ::SendMessageW(combo, CB_SETITEMDATA, item,
                                   static_cast<LPARAM>(index));
                }
            }
            ::SendMessageW(combo, CB_SETDROPPEDWIDTH,
                           static_cast<WPARAM>(scaleFor(dpi_, 360)), 0);
        };
        fill(trip_combo_);
        fill(contact52a_combo_);
        fill(contact52b_combo_);

        const auto select = [&](HWND combo, HWND check,
                                const analysis::DigitalSignalAssignment& value) {
            int target = 0;
            const std::size_t count =
                loaded_ ? loaded_->record.cfg.digital.size() : 0;
            if (value.channel.has_value() && *value.channel < count) {
                target = static_cast<int>(*value.channel + 1);
            }
            ::SendMessageW(combo, CB_SETCURSEL,
                           static_cast<WPARAM>(target), 0);
            ::SendMessageW(check, BM_SETCHECK,
                           value.active_high ? BST_CHECKED : BST_UNCHECKED, 0);
        };
        if (loaded_) {
            select(trip_combo_, trip_polarity_,
                   loaded_->breaker_options.trip);
            select(contact52a_combo_, contact52a_polarity_,
                   loaded_->breaker_options.contact_52a);
            select(contact52b_combo_, contact52b_polarity_,
                   loaded_->breaker_options.contact_52b);
        } else {
            for (HWND combo : {trip_combo_, contact52a_combo_,
                               contact52b_combo_}) {
                ::SendMessageW(combo, CB_SETCURSEL, 0, 0);
            }
            for (HWND check : {trip_polarity_, contact52a_polarity_,
                               contact52b_polarity_}) {
                ::SendMessageW(check, BM_SETCHECK, BST_CHECKED, 0);
            }
        }
        updateBreakerPolarityEnabled();
    }

    void updateBreakerPolarityEnabled() {
        if (!trip_combo_ || !contact52a_combo_ || !contact52b_combo_) {
            return;
        }
        const bool interactive = loaded_ != nullptr && !loading_;
        const auto assigned = [&](HWND combo) {
            const int selection =
                static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0));
            if (selection < 0) {
                return false;
            }
            const LRESULT data =
                ::SendMessageW(combo, CB_GETITEMDATA, selection, 0);
            return data != CB_ERR && data >= 0 &&
                   static_cast<std::size_t>(data) <
                       loaded_->record.cfg.digital.size();
        };
        ::EnableWindow(trip_polarity_, interactive && assigned(trip_combo_));
        ::EnableWindow(contact52a_polarity_,
                       interactive && assigned(contact52a_combo_));
        ::EnableWindow(contact52b_polarity_,
                       interactive && assigned(contact52b_combo_));
    }

    analysis::DigitalSignalAssignment readBreakerAssignment(
        HWND combo, HWND check) const {
        analysis::DigitalSignalAssignment assignment;
        assignment.active_high =
            ::SendMessageW(check, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const int selection =
            static_cast<int>(::SendMessageW(combo, CB_GETCURSEL, 0, 0));
        if (selection >= 0) {
            const LRESULT data =
                ::SendMessageW(combo, CB_GETITEMDATA, selection, 0);
            const std::size_t count =
                loaded_ ? loaded_->record.cfg.digital.size() : 0;
            if (data != CB_ERR && data >= 0 &&
                static_cast<std::size_t>(data) < count) {
                assignment.channel = static_cast<std::size_t>(data);
            }
        }
        return assignment;
    }

    std::wstring digitalAssignmentLabel(
        const analysis::DigitalSignalAssignment& assignment) const {
        if (!loaded_ || !assignment.channel.has_value()) {
            return L"Sin asignar";
        }
        if (*assignment.channel >= loaded_->record.cfg.digital.size()) {
            return L"Índice fuera de rango";
        }
        const auto& channel =
            loaded_->record.cfg.digital[*assignment.channel];
        return utf8ToWide(channel.name.empty()
                              ? "Canal digital " +
                                    std::to_string(*assignment.channel + 1)
                              : channel.name);
    }

    void showBreakerInfo() {
        if (!loaded_ || !loaded_->analysis_available) {
            showError("Cargue un registro antes de consultar los tiempos de "
                      "interruptor.");
            return;
        }
        const auto& breaker = loaded_->analysis.breaker;
        const auto seconds_text = [](const std::optional<double>& value) {
            if (!value.has_value() || !std::isfinite(*value)) {
                return std::wstring(L"No observado / no calculable");
            }
            return formatNumberW(*value, 9) + L" s";
        };
        const auto milliseconds_text = [](const std::optional<double>& value) {
            if (!value.has_value() || !std::isfinite(*value)) {
                return std::wstring(L"No calculable");
            }
            return formatNumberW(*value, 6) + L" ms";
        };
        const auto resolution_text = [&](double value) {
            if (!breaker.configured) {
                return std::wstring(L"No evaluado");
            }
            if (!std::isfinite(value) || value <= 0.0) {
                return std::wstring(L"No calculable");
            }
            return formatNumberW(value, 6) + L" ms";
        };

        std::wstring text = L"Tiempos de interruptor (segundos DAT)\n\n";
        text += breaker.configured
                    ? L"Estado: configurado con asignaciones explícitas.\n"
                    : L"Estado: no evaluado (sin asignaciones explícitas).\n";
        text += L"Inicio candidato: ";
        text += seconds_text(breaker.onset_s);
        text += L"\nTrip observado: ";
        text += seconds_text(breaker.trip_s);
        text += L"\nContacto abierto (52a/52b): ";
        text += seconds_text(breaker.contact_open_s);
        text += L"\nExtinción de corriente: ";
        text += seconds_text(breaker.current_extinction_s);
        text += L"\nProtección (Trip - inicio): ";
        text += milliseconds_text(breaker.protection_ms);
        text += L"\nApertura de contacto: ";
        text += milliseconds_text(breaker.contact_opening_ms);
        text += L"\nDespeje eléctrico: ";
        text += milliseconds_text(breaker.clearing_ms);
        text += L"\nResolución digital observada: ";
        text += resolution_text(breaker.digital_resolution_ms);
        text += L"\nIncertidumbre del inicio candidato: ";
        text += resolution_text(breaker.onset_resolution_ms);
        text += L"\nResolución de extinción: ";
        text += resolution_text(breaker.extinction_resolution_ms);
        text += L"\n\nAsignaciones actuales (pulse Aplicar para recalcular):\n";
        const auto describe =
            [&](const wchar_t* label,
                const analysis::DigitalSignalAssignment& assignment,
                const wchar_t* meaning) {
                text += label;
                text += L": ";
                text += digitalAssignmentLabel(assignment);
                text += L" · ";
                text += assignment.active_high ? L"activo alto (1)"
                                               : L"activo bajo (0)";
                text += L" · ";
                text += meaning;
                text += L"\n";
            };
        describe(L"Trip", readBreakerAssignment(trip_combo_, trip_polarity_),
                 L"disparo");
        describe(L"52a",
                 readBreakerAssignment(contact52a_combo_,
                                       contact52a_polarity_),
                 L"activo = cerrado");
        describe(L"52b",
                 readBreakerAssignment(contact52b_combo_,
                                       contact52b_polarity_),
                 L"activo = abierto");
        text += L"\nLos contactos indican posición; la apertura eléctrica es "
                L"la extinción observada de corriente. El inicio es un "
                L"candidato heurístico y no un tiempo de operación de "
                L"protección. Las duraciones que usan el inicio incluyen su incertidumbre de ciclo; la resolución digital no garantiza su precisión total. Lo no observado no es cero.\n";
        std::size_t shown = 0, omitted = 0;
        for (const auto& diagnostic : breaker.diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Info) continue;
            if (shown == 0) text += L"\nObservaciones:\n";
            if (shown >= 3) { ++omitted; continue; }
            ++shown;
            text += L"· " + utf8ToWide(diagnostic.message) + L"\n";
        }
        if (omitted > 0) {
            text += L"Hay más observaciones; consulte Diagnósticos o el informe.\n";
        }
        showMessage(text, MB_ICONINFORMATION);
    }

    std::vector<Diagnostic> diagnosticsForDisplay() const {
        std::vector<Diagnostic> combined = open_diagnostics_;
        combined.insert(combined.end(), last_load_diagnostics_.begin(),
                        last_load_diagnostics_.end());
        if (loaded_) {
            combined.insert(combined.end(), loaded_->diagnostics.begin(),
                            loaded_->diagnostics.end());
            if (loaded_->analysis_available) {
                appendUniqueDiagnostics(
                    combined, loaded_->analysis.breaker.diagnostics);
            }
        }
        return combined;
    }

    void refreshDiagnostics() {
        if (!diagnostics_list_) {
            return;
        }
        ::SendMessageW(diagnostics_list_, LB_RESETCONTENT, 0, 0);
        const std::vector<Diagnostic> combined = diagnosticsForDisplay();
        if (combined.empty()) {
            ::SendMessageW(
                diagnostics_list_, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(L"Sin diagnósticos del parser."));
        } else {
            for (const auto& diagnostic : combined) {
                const std::wstring text = diagnosticText(diagnostic);
                ::SendMessageW(diagnostics_list_, LB_ADDSTRING, 0,
                               reinterpret_cast<LPARAM>(text.c_str()));
            }
        }
        ::SendMessageW(diagnostics_list_, LB_SETHORIZONTALEXTENT,
                       static_cast<WPARAM>(scaleFor(dpi_, 1400)), 0);
    }

    void showDiagnosticsContextMenu(POINT point) {
        const std::vector<Diagnostic> diagnostics = diagnosticsForDisplay();
        HMENU menu = ::CreatePopupMenu();
        if (!menu) {
            showError("No se pudo abrir el menú de diagnósticos.");
            return;
        }
        ::AppendMenuW(menu, MF_STRING |
                                (diagnostics.empty() ? MF_GRAYED : MF_ENABLED),
                      kCommandExportDiagnostics,
                      L"Exportar lista a CSV…");
        const UINT command = ::TrackPopupMenu(
            menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, point.x, point.y, 0,
            hwnd_, nullptr);
        ::DestroyMenu(menu);
        if (command == kCommandExportDiagnostics) {
            exportDiagnosticsCsv();
        }
    }

    void exportDiagnosticsCsv() {
        const std::vector<Diagnostic> diagnostics = diagnosticsForDisplay();
        if (diagnostics.empty()) {
            showError("No hay diagnósticos para exportar.");
            return;
        }
        std::wstring stem = L"diagnosticos_COMTRADE";
        if (loaded_) {
            const std::wstring source = loaded_->descriptor.record_name.empty()
                                            ? loaded_->descriptor.label
                                            : loaded_->descriptor.record_name;
            stem = report::safeFileStem(source) + L"_diagnosticos";
        }
        std::vector<wchar_t> buffer(1024, L'\0');
        const std::size_t copy_length =
            std::min(stem.size(), buffer.size() - 5);
        std::copy_n(stem.begin(), copy_length, buffer.begin());
        std::copy_n(L".csv", 5, buffer.begin() +
                                      static_cast<std::ptrdiff_t>(copy_length));

        const wchar_t filter[] =
            L"CSV de diagnósticos (*.csv)\0*.csv\0Todos los archivos (*.*)\0*.*\0\0";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrDefExt = L"csv";
        dialog.lpstrTitle = L"Exportar diagnósticos para análisis";
        dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!::GetSaveFileNameW(&dialog)) {
            const DWORD error = ::CommDlgExtendedError();
            if (error != 0) {
                showError("No se pudo mostrar el selector de guardado. Código " +
                          std::to_string(error) + ".");
            }
            return;
        }

        std::ofstream output(std::filesystem::path(buffer.data()),
                             std::ios::binary | std::ios::trunc);
        if (!output) {
            showError("No se pudo crear el archivo CSV de diagnósticos.");
            return;
        }
        output.write("\xEF\xBB\xBF", 3);
        const std::array<std::string_view, 8> headers = {
            "registro", "severidad", "codigo", "mensaje", "contexto",
            "linea_cfg", "registro_dat_1based", "numero_muestra_dat"};
        const auto writeRow = [&](const auto& fields) {
            for (std::size_t index = 0; index < fields.size(); ++index) {
                if (index != 0) {
                    output.put(',');
                }
                output << csvField(fields[index]);
            }
            output.write("\r\n", 2);
        };
        writeRow(headers);
        const std::string record_name = loaded_
            ? wideToUtf8(loaded_->descriptor.record_name.empty()
                             ? loaded_->descriptor.label
                             : loaded_->descriptor.record_name)
            : std::string{};
        for (const Diagnostic& diagnostic : diagnostics) {
            std::array<std::string, 8> fields = {
                record_name, std::string(diagnosticSeverityName(diagnostic.severity)),
                diagnostic.code, diagnostic.message, diagnostic.context,
                diagnostic.has_line ? std::to_string(diagnostic.line) : "",
                diagnostic.has_record
                    ? std::to_string(diagnostic.record + 1)
                    : "",
                diagnostic.has_sample_number
                    ? std::to_string(diagnostic.sample_number)
                    : ""};
            writeRow(fields);
        }
        output.flush();
        if (!output) {
            showError("Ocurrió un error al escribir el CSV de diagnósticos.");
            return;
        }
        showMessage("Se exportaron " + std::to_string(diagnostics.size()) +
                        " diagnósticos a CSV.",
                    MB_ICONINFORMATION);
    }

    void updateControls() {
        const bool has_records = !records_.empty();
        const bool has_loaded = loaded_ != nullptr;
        const bool interactive = !loading_;
        ::EnableWindow(open_button_, interactive);
        ::EnableWindow(html_button_, interactive && has_loaded);
        ::EnableWindow(csv_button_, interactive && has_loaded);
        ::EnableWindow(package_button_, interactive && has_loaded);
        ::EnableWindow(batch_button_, interactive && has_records);
        ::EnableWindow(compare_button_, interactive && has_records &&
                                            records_.size() > 1);
        ::EnableWindow(record_combo_, interactive && has_records);
        ::EnableWindow(channel_list_, interactive && has_loaded &&
                                           !loaded_->record.cfg.analog.empty());
        ::EnableWindow(diagnostics_list_, interactive);
        const bool options_enabled = interactive && has_loaded;
        for (HWND control : {phase_a_, phase_b_, phase_c_, cycles_, onset_edit_,
                             pre_edit_, fault_edit_, line_edit_, log_edit_,
                             unit_channel_, unit_edit_, apply_button_,
                             trip_combo_, contact52a_combo_,
                             contact52b_combo_}) {
            ::EnableWindow(control, options_enabled);
        }
        for (HWND button : mode_buttons_) {
            ::EnableWindow(button, has_loaded);
        }
        updateBreakerPolarityEnabled();
        if (hwnd_) {
            HMENU menu = ::GetMenu(hwnd_);
            if (menu) {
                const UINT commands[] = {
                    kCommandOpen,          kCommandOpenPair,
                    kCommandExportHtml,    kCommandExportCsv,
                    kCommandExportJson,    kCommandExportPackage,
                    kCommandAnalyzeBatch,  kCommandCompare,
                    kCommandExportBatchHtml, kCommandResetView,
                    kCommandZoomIn,        kCommandZoomOut,
                    kCommandBreakerInfo,   kCommandModeTrace,
                    kCommandModePhasor,    kCommandModeTrend,
                    kCommandModeHarmonic,  kCommandModeTimeline};
                for (const UINT command : commands) {
                    const bool needs_loaded =
                        command == kCommandExportHtml ||
                        command == kCommandExportCsv ||
                        command == kCommandExportJson ||
                        command == kCommandExportPackage ||
                        command == kCommandResetView ||
                        command == kCommandZoomIn ||
                        command == kCommandZoomOut ||
                        command == kCommandBreakerInfo ||
                        command == kCommandModeTrace ||
                        command == kCommandModePhasor ||
                        command == kCommandModeTrend ||
                        command == kCommandModeHarmonic ||
                        command == kCommandModeTimeline;
                    const bool needs_records =
                        command == kCommandAnalyzeBatch ||
                        command == kCommandCompare ||
                        command == kCommandExportBatchHtml;
                    const bool enabled =
                        interactive && (needs_loaded ? has_loaded
                                          : needs_records ? has_records
                                                          : true);
                    ::EnableMenuItem(menu, command,
                                     MF_BYCOMMAND |
                                         (enabled ? MF_ENABLED : MF_GRAYED));
                }
            }
        }
    }

    void updateStatus() {
        if (!status_) {
            return;
        }
        std::wstring text;
        if (loading_) {
            text = L"Leyendo y validando el registro…";
        } else if (!loaded_) {
            text = records_.empty()
                       ? L"Abra un ZIP o un par CFG/DAT para comenzar."
                       : L"Seleccione un registro para cargarlo.";
        } else {
            const auto& record = loaded_->record;
            text = record.cfg.station_name.empty()
                       ? L"Estación no informada"
                       : utf8ToWide(record.cfg.station_name);
            text += L" · ";
            text += loaded_->descriptor.label;
            text += L" · ";
            text += formatCountW(record.data.sampleCount());
            text += L" muestras · ";
            text += formatCountW(record.cfg.analogCount());
            text += L" analógicas / ";
            text += formatCountW(record.cfg.digitalCount());
            text += L" digitales · ";
            text += utf8ToWide(dataFileTypeName(record.cfg.data_type));
            text += L" · ";
            text += formatNumberW(record.cfg.nominal_frequency_hz, 7);
            text += L" Hz";

            std::size_t errors = 0;
            std::size_t warnings = 0;
            const auto count_diagnostics = [&](const std::vector<Diagnostic>& list) {
                for (const auto& diagnostic : list) {
                    if (diagnostic.severity == DiagnosticSeverity::Error) {
                        ++errors;
                    } else if (diagnostic.severity ==
                               DiagnosticSeverity::Warning) {
                        ++warnings;
                    }
                }
            };
            count_diagnostics(open_diagnostics_);
            count_diagnostics(loaded_->diagnostics);
            if (errors != 0 || warnings != 0) {
                text += L" · ";
                text += formatCountW(errors);
                text += L" error(es) / ";
                text += formatCountW(warnings);
                text += L" advertencia(s)";
            }
        }
        ::SetWindowTextW(status_, text.c_str());
    }

    void updateTitle() {
        if (!hwnd_) {
            return;
        }
        std::wstring title = L"Analizador COMTRADE";
        if (loaded_) {
            title += L" — ";
            title += loaded_->descriptor.label;
        }
        ::SetWindowTextW(hwnd_, title.c_str());
    }

    void chooseExport(ExportKind kind) {
        if (!loaded_ || loading_) {
            showError("Cargue un registro antes de exportar.");
            return;
        }
        const wchar_t* extension = kind == ExportKind::Html      ? L".html"
                                   : kind == ExportKind::Csv     ? L".csv"
                                   : kind == ExportKind::Json    ? L".json"
                                                                 : L".zip";
        const wchar_t* default_extension = kind == ExportKind::Html   ? L"html"
                                           : kind == ExportKind::Csv  ? L"csv"
                                           : kind == ExportKind::Json ? L"json"
                                                                      : L"zip";
        const wchar_t* filter =
            kind == ExportKind::Html
                ? L"Informe HTML (*.html)\0*.html\0Todos los archivos (*.*)\0*.*\0\0"
                : kind == ExportKind::Csv
                      ? L"Datos CSV (*.csv)\0*.csv\0Todos los archivos (*.*)\0*.*\0\0"
                      : kind == ExportKind::Json
                            ? L"Resumen JSON (*.json)\0*.json\0Todos los archivos (*.*)\0*.*\0\0"
                            : L"Paquete ZIP (*.zip)\0*.zip\0Todos los archivos (*.*)\0*.*\0\0";
        const std::wstring title =
            kind == ExportKind::Html      ? L"Exportar informe HTML"
            : kind == ExportKind::Csv     ? L"Exportar datos CSV"
            : kind == ExportKind::Json    ? L"Exportar resumen JSON"
                                          : L"Exportar paquete ZIP de análisis";

        std::wstring stem = report::safeFileStem(
            loaded_->descriptor.record_name.empty()
                ? loaded_->descriptor.label
                : loaded_->descriptor.record_name);
        std::wstring default_name = stem + extension;
        std::vector<wchar_t> buffer(1024, L'\0');
        const std::size_t copy_length =
            std::min(default_name.size(), buffer.size() - 1);
        std::copy_n(default_name.begin(), copy_length, buffer.begin());

        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFilter = filter;
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrDefExt = default_extension;
        dialog.lpstrTitle = title.c_str();
        dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!::GetSaveFileNameW(&dialog)) {
            const DWORD error = ::CommDlgExtendedError();
            if (error != 0) {
                showError("No se pudo mostrar el selector de guardado. Código " +
                          std::to_string(error) + ".");
            }
            return;
        }
        exportTo(std::filesystem::path(buffer.data()), kind);
    }

    void exportTo(const std::filesystem::path& path, ExportKind kind) {
        if (!loaded_) {
            return;
        }
        std::vector<Diagnostic> diagnostics = open_diagnostics_;
        diagnostics.insert(diagnostics.end(), loaded_->diagnostics.begin(),
                           loaded_->diagnostics.end());

        report::Context context;
        context.title = wideToUtf8(loaded_->descriptor.record_name.empty()
                                       ? loaded_->descriptor.label
                                       : loaded_->descriptor.record_name);
        context.source = wideToUtf8(
            sourceForDisplay(loaded_->descriptor.source_description));
        context.record = &loaded_->record;
        context.diagnostics = &diagnostics;
        context.analysis = loaded_->analysis_available ? &loaded_->analysis
                                                        : nullptr;
        context.event_log = &loaded_->event_log;
        context.timeline = &loaded_->timeline;
        context.trend = &loaded_->trend;
        context.comparison = &comparison_;
        context.unit_overrides = &loaded_->unit_overrides;
        context.reference_length_km = loaded_->reference_length_km;
        context.log_clock_offset_s = loaded_->log_clock_offset_s;

        report::Result result;
        try {
            WaitCursor wait;
            if (kind == ExportKind::Html) {
                result = report::writeHtmlReport(path, context);
            } else if (kind == ExportKind::Csv) {
                result = report::writeSampleCsv(path, context);
            } else if (kind == ExportKind::Json) {
                result = report::writeSummaryJson(path, context);
            } else {
                report::PackageData package;
                package.event_log = &loaded_->event_log;
                package.timeline = &loaded_->timeline;
                package.trend = &loaded_->trend;
                package.comparison = &comparison_;
                package.batch = &batch_items_;
                package.batch_failures = batch_failures_;
                result = report::writeAnalysisPackage(path, context, {}, package);
            }
        } catch (const std::exception& exception) {
            showError(std::string("No se pudo generar el archivo: ") +
                      exception.what());
            return;
        } catch (...) {
            showError("No se pudo generar el archivo por un error inesperado.");
            return;
        }
        if (!result.ok) {
            showError(result.error.empty()
                          ? "No se pudo generar el archivo."
                          : result.error);
            return;
        }
        const std::wstring message =
            L"Archivo generado:\n" + path.wstring() +
            L"\n\nEl contenido es offline y no carga recursos remotos.";
        showMessage(message, MB_ICONINFORMATION);
    }

    static bool readEditDouble(HWND edit, double& value) {
        wchar_t buffer[128]{};
        ::GetWindowTextW(edit, buffer, 128);
        wchar_t* end = nullptr;
        const double parsed = std::wcstod(buffer, &end);
        if (end == buffer || !std::isfinite(parsed)) {
            return false;
        }
        value = parsed;
        return true;
    }

    analysis::BreakerTimingOptions readBreakerOptionsFromControls() const {
        analysis::BreakerTimingOptions breaker;
        breaker.trip =
            readBreakerAssignment(trip_combo_, trip_polarity_);
        breaker.contact_52a =
            readBreakerAssignment(contact52a_combo_, contact52a_polarity_);
        breaker.contact_52b =
            readBreakerAssignment(contact52b_combo_, contact52b_polarity_);
        return breaker;
    }

    analysis::AnalysisOptions analysisOptionsFromControls() const {
        analysis::AnalysisOptions options;
        const int selection = static_cast<int>(
            ::SendMessageW(cycles_, CB_GETCURSEL, 0, 0));
        options.cycles = selection >= 0 ? selection + 1 : 1;
        double value = 1.5;
        if (readEditDouble(onset_edit_, value) && value > 1.0) {
            options.onset_ratio = value;
        }
        if (readEditDouble(pre_edit_, value) &&
            (!loaded_ || loaded_->prefalla_manual ||
             std::abs(value - loaded_->prefalla_center_s) > 1e-9)) {
            options.prefalla_center_s = value;
        }
        if (readEditDouble(fault_edit_, value) &&
            (!loaded_ || loaded_->falla_manual ||
             std::abs(value - loaded_->falla_center_s) > 1e-9)) {
            options.falla_center_s = value;
        }
        if (readEditDouble(line_edit_, value) && value >= 0.0) {
            options.reference_length_km = value;
        }
        const HWND combos[3] = {phase_a_, phase_b_, phase_c_};
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const int item = static_cast<int>(
                ::SendMessageW(combos[slot], CB_GETCURSEL, 0, 0));
            if (item >= 0) {
                const LRESULT data = ::SendMessageW(
                    combos[slot], CB_GETITEMDATA, item, 0);
                if (data != CB_ERR && data >= 0) {
                    options.phase_indices[slot] =
                        static_cast<std::size_t>(data);
                }
            }
        }
        if (loaded_ && !loaded_->fault_distance_text.empty()) {
            options.fault_distance_text = loaded_->fault_distance_text;
        }
        options.breaker = readBreakerOptionsFromControls();
        return options;
    }

    void applyAnalysisOptions(bool show_confirmation = true) {
        if (!loaded_) {
            showError("Cargue un registro antes de aplicar opciones.");
            return;
        }
        analysis::AnalysisOptions options = analysisOptionsFromControls();
        options.voltage_indices = loaded_->voltage_indices;
        if (loaded_->fault_distance_text.empty()) options.fault_distance_text.reset();
        else options.fault_distance_text = loaded_->fault_distance_text;
        options.breaker = readBreakerOptionsFromControls();
        double log_offset = 0.0;
        if (!readEditDouble(log_edit_, log_offset) ||
            std::abs(log_offset) > 86400.0) {
            showError("La corrección de reloj debe ser un número entre -86400 y "
                      "86400 segundos.");
            return;
        }
        WaitCursor wait;
        auto updated = std::make_shared<LoadedRecord>(*loaded_);
        updated->breaker_options = options.breaker;
        updated->prefalla_manual = options.prefalla_center_s.has_value();
        updated->falla_manual = options.falla_center_s.has_value();
        updated->analysis = analysis::analyzeRecord(updated->record, options);
        updated->analysis_available = true;
        updated->cycles = updated->analysis.cycles;
        updated->onset_ratio = options.onset_ratio;
        updated->prefalla_center_s = updated->analysis.prefalla_center_s;
        updated->falla_center_s = updated->analysis.falla_center_s;
        updated->phase_indices = updated->analysis.phases.index;
        updated->voltage_indices = updated->analysis.voltage.selection.index;
        updated->reference_length_km = options.reference_length_km;
        updated->log_clock_offset_s = log_offset;
        cycle_count_ = options.cycles;
        onset_ratio_ = options.onset_ratio;
        reference_length_km_ = options.reference_length_km;
        updated->diagnostics = updated->source_diagnostics;
        updated->diagnostics.insert(
            updated->diagnostics.end(), updated->analysis.diagnostics.begin(),
            updated->analysis.diagnostics.end());
        appendUniqueDiagnostics(updated->diagnostics,
                                updated->analysis.breaker.diagnostics);
        updated->timeline = report::buildTimeline(
            updated->record, &updated->analysis, updated->event_log, log_offset);
        updated->trend = report::rollingTrend(updated->record,
                                              updated->phase_indices,
                                              updated->cycles, 600);
        updated->diagnostics.insert(
            updated->diagnostics.end(), updated->timeline.diagnostics.begin(),
            updated->timeline.diagnostics.end());
        updated->diagnostics.insert(updated->diagnostics.end(),
                                    updated->trend.diagnostics.begin(),
                                    updated->trend.diagnostics.end());

                                                                      
        const int unit_item = static_cast<int>(
            ::SendMessageW(unit_channel_, CB_GETCURSEL, 0, 0));
        if (unit_item >= 0) {
            const LRESULT data = ::SendMessageW(unit_channel_, CB_GETITEMDATA,
                                                unit_item, 0);
            wchar_t buffer[128]{};
            ::GetWindowTextW(unit_edit_, buffer, 128);
            const std::wstring reported = trimWide(buffer);
            if (data != CB_ERR && data >= 0 &&
                static_cast<std::size_t>(data) <
                    updated->record.cfg.analog.size()) {
                const std::size_t index = static_cast<std::size_t>(data);
                const std::wstring declared =
                    utf8ToWide(updated->record.cfg.analog[index].unit);
                if (!reported.empty() && reported != declared) {
                    updated->unit_overrides[index] = wideToUtf8(reported);
                } else {
                    updated->unit_overrides.erase(index);
                }
            }
        }
        const ViewMode previous_mode = waveform_.mode();
        breaker_options_by_record_[selected_index_] = options.breaker;
        loaded_ = std::move(updated);
        waveform_.setRecord(loaded_.get());
        waveform_.setMode(previous_mode);
        populatePhaseControls();
        populateBreakerControls();
        refreshDiagnostics();
        updateStatus();
        updateTitle();
        if (show_confirmation) {
            showMessage("Opciones aplicadas. Los valores numéricos conservan la "
                        "escala y unidad del CFG; la unidad informada solo "
                        "cambia la presentación.",
                        MB_ICONINFORMATION);
        }
    }

    static std::wstring trimWide(std::wstring text) {
        while (!text.empty() &&
               (text.back() == L' ' || text.back() == L'\t' ||
                text.back() == L'\r' || text.back() == L'\n')) {
            text.pop_back();
        }
        std::size_t begin = 0;
        while (begin < text.size() &&
               (text[begin] == L' ' || text[begin] == L'\t')) {
            ++begin;
        }
        return text.substr(begin);
    }

    void analyzeBatch() {
        if (records_.empty()) {
            showError("Abra al menos un ZIP o par CFG/DAT para analizar el lote.");
            return;
        }
        WaitCursor wait;
        analysis::AnalysisOptions base = analysisOptionsFromControls();
        base.prefalla_center_s.reset();
        base.falla_center_s.reset();
        base.phase_indices = {};
        base.voltage_indices = {};
                                                                                
                                                  
        base.breaker = {};
                                                                               
                                                 
        base.fault_distance_text.reset();
        if (loaded_) {
                                                                             
                                                                             
            base.cycles = loaded_->cycles;
            base.onset_ratio = loaded_->onset_ratio;
            base.reference_length_km = loaded_->reference_length_km;
        }

        batch_records_.clear();
        batch_items_.clear();
        batch_failures_.clear();
        std::vector<std::size_t> item_descriptors;

        for (std::size_t descriptor_index = 0;
             descriptor_index < records_.size(); ++descriptor_index) {
            const RecordDescriptor& descriptor = records_[descriptor_index];
            analysis::AnalysisOptions options = base;
            const bool is_current =
                loaded_ && descriptor_index == selected_index_;
            if (is_current) {
                                                                         
                                                                           
                                                   
                options.phase_indices = loaded_->phase_indices;
                options.voltage_indices = loaded_->voltage_indices;
                options.prefalla_center_s = loaded_->prefalla_center_s;
                options.falla_center_s = loaded_->falla_center_s;
                options.breaker = loaded_->breaker_options;
            }
                                                                              
                                                                            
                                            
            auto result = loadRecord(descriptor, options);
            if (!result.ok || !result.loaded) {
                batch_failures_.push_back(
                    wideToUtf8(descriptor.label) + ": " +
                    (result.error.empty() ? "no se pudo analizar"
                                          : result.error));
                continue;
            }
            auto loaded = result.loaded;
            batch_records_.push_back(loaded);
            report::BatchItem item;
            item.label = wideToUtf8(descriptor.label);
            item.source = wideToUtf8(descriptor.source_description);
            item.record = &loaded->record;
            item.analysis = &loaded->analysis;
            item.diagnostics = loaded->diagnostics;
            item.phase_indices = loaded->phase_indices;
            item.voltage_indices = loaded->voltage_indices;
            batch_items_.push_back(std::move(item));
            item_descriptors.push_back(descriptor_index);
        }

        comparison_.rows.clear();
        comparison_.warnings.clear();
        comparison_.diagnostics.clear();
        std::size_t current_position = batch_items_.size();
        if (!batch_items_.empty()) {
                                                                            
                                                                          
            for (std::size_t index = 0; index < batch_items_.size(); ++index) {
                if (item_descriptors[index] == selected_index_ &&
                    batch_items_[index].analysis &&
                    batch_items_[index].analysis->time.trigger_within_capture) {
                    current_position = index;
                    break;
                }
            }
            if (current_position == batch_items_.size()) {
                for (std::size_t index = 0; index < batch_items_.size();
                     ++index) {
                    if (batch_items_[index].analysis &&
                        batch_items_[index].analysis->time.trigger_within_capture) {
                        current_position = index;
                        break;
                    }
                }
            }
        }
        if (current_position < batch_items_.size()) {
            std::vector<report::ComparisonTarget> targets;
            for (std::size_t index = 0; index < batch_items_.size(); ++index) {
                if (index == current_position) {
                    continue;
                }
                report::ComparisonTarget target;
                target.label = batch_items_[index].label;
                target.record = batch_items_[index].record;
                target.analysis = batch_items_[index].analysis;
                target.phase_indices = batch_items_[index].phase_indices;
                target.voltage_indices = batch_items_[index].voltage_indices;
                targets.push_back(std::move(target));
            }
            const auto& current = batch_items_[current_position];
            comparison_ = report::compareRecords(
                current.label, *current.record, *current.analysis, targets,
                base.cycles);
        }
        updateStatus();
        std::wstring message = L"Lote analizado: ";
        message += formatCountW(batch_items_.size());
        message += L" registro(s) válido(s), ";
        message += formatCountW(batch_failures_.size());
        message += L" fallo(s).";
        if (!comparison_.rows.empty()) {
            message += L"\nComparación de fasores calculada para ";
            message += formatCountW(comparison_.rows.size());
            message += L" fila(s).";
        }
        message += L"\nUse “Exportar informe de lote HTML” para el detalle.";
        showMessage(message, MB_ICONINFORMATION);
    }

    void compareLoadedRecords() {
        if (!loaded_ || !loaded_->analysis_available) {
            showError("Cargue un registro antes de comparar.");
            return;
        }
        if (records_.size() < 2) {
            showError("Abra al menos dos registros (ZIP o pares CFG/DAT) para "
                      "comparar.");
            return;
        }
        WaitCursor wait;
        analysis::AnalysisOptions options = analysisOptionsFromControls();
                                                                               
                                                                         
        options.prefalla_center_s.reset();
        options.falla_center_s.reset();
        options.phase_indices = {};
        options.voltage_indices = {};
                                                                        
                                                                  
        options.fault_distance_text.reset();
        options.cycles = loaded_->cycles;
        options.onset_ratio = loaded_->onset_ratio;
        options.reference_length_km = loaded_->reference_length_km;
        std::vector<std::shared_ptr<LoadedRecord>> keep_alive;
        std::vector<report::ComparisonTarget> others;
        for (std::size_t index = 0; index < records_.size(); ++index) {
            if (index == selected_index_) {
                continue;
            }
            auto result = loadRecord(records_[index], options);
            if (!result.ok || !result.loaded) {
                continue;
            }
            keep_alive.push_back(result.loaded);
            report::ComparisonTarget target;
            target.label = wideToUtf8(records_[index].label);
            target.record = &result.loaded->record;
            target.analysis = &result.loaded->analysis;
            target.phase_indices = result.loaded->phase_indices;
            target.voltage_indices = result.loaded->voltage_indices;
            others.push_back(std::move(target));
        }
        comparison_ = report::compareRecords(
            wideToUtf8(loaded_->descriptor.label), loaded_->record,
            loaded_->analysis, others, loaded_->cycles);
        waveform_.setMode(ViewMode::Phasor);
        updateStatus();
        std::wstring message = L"Comparación: ";
        message += formatCountW(comparison_.rows.size());
        message += L" fila(s) de fasores.";
        if (!comparison_.warnings.empty()) {
            message += L"\n";
            message += formatCountW(comparison_.warnings.size());
            message += L" advertencia(s); revise el informe o los diagnósticos.";
        }
        showMessage(message, MB_ICONINFORMATION);
    }

    void exportBatchHtml() {
        if (batch_items_.empty()) {
            showError("Ejecute “Analizar lote” antes de exportar el informe de "
                      "lote.");
            return;
        }
        std::vector<wchar_t> buffer(1024, L'\0');
        const std::wstring default_name = L"comparacion_registros.html";
        std::copy(default_name.begin(), default_name.end(), buffer.begin());
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFilter =
            L"Informe HTML (*.html)\0*.html\0Todos los archivos (*.*)\0*.*\0\0";
        dialog.lpstrFile = buffer.data();
        dialog.nMaxFile = static_cast<DWORD>(buffer.size());
        dialog.lpstrDefExt = L"html";
        dialog.lpstrTitle = L"Exportar informe de lote HTML";
        dialog.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!::GetSaveFileNameW(&dialog)) {
            return;
        }
        WaitCursor wait;
        const auto result = report::writeBatchHtml(
            std::filesystem::path(buffer.data()), batch_items_,
            batch_failures_, cycle_count_, &comparison_);
        if (!result.ok) {
            showError(result.error.empty() ? "No se pudo generar el informe de "
                                            "lote."
                                           : result.error);
            return;
        }
        showMessage(L"Informe de lote generado:\n" +
                        std::filesystem::path(buffer.data()).wstring(),
                    MB_ICONINFORMATION);
    }

    void showError(const std::string& message) {
        showMessage(message.empty() ? "Ocurrió un error." : message,
                    MB_ICONERROR);
    }

    void showMessage(const std::string& message, UINT flags) {
        const std::wstring text = utf8ToWide(message);
        ::MessageBoxW(hwnd_, text.c_str(), L"Analizador COMTRADE", MB_OK | flags);
    }

    void showMessage(const std::wstring& message, UINT flags) {
        ::MessageBoxW(hwnd_, message.c_str(), L"Analizador COMTRADE", MB_OK | flags);
    }

    void showAbout() {
        const std::wstring text =
            L"Analizador COMTRADE nativo\n"
            L"Versión freeware 0.1 · revisión 2\n\n"
            L"Desarrollado por Ricardo Porras G.\n"
            L"\n"
            L"Aplicación offline para revisar registros CFG/DAT y ZIP "
            L"COMTRADE.\n\n"
            L"• Lee ZIP sin extraer miembros a disco.\n"
            L"• Conserva escalas y unidades declaradas en el CFG.\n"
            L"• Genera informes HTML, CSV y JSON autocontenidos.\n"
            L"• No identifica por sí sola la causa de una falla ni sustituye "
            L"la revisión de ingeniería.\n\n"
            L"Win32/GDI · C++20 · sin dependencias de terceros.";
        ::MessageBoxW(hwnd_, text.c_str(), L"Acerca de",
                      MB_OK | MB_ICONINFORMATION);
    }

    void onDropFiles(HDROP drop) {
        const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::filesystem::path> paths;
        for (UINT index = 0; index < count; ++index) {
            const UINT length = ::DragQueryFileW(drop, index, nullptr, 0);
            if (length == 0) {
                continue;
            }
            std::wstring name(length + 1, L'\0');
            ::DragQueryFileW(drop, index, name.data(), length + 1);
            name.resize(length);
            paths.emplace_back(name);
        }
        ::DragFinish(drop);
        openPaths(paths);
    }

    RECT workArea() const {
        RECT area{};
        if (!::SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0)) {
            area = RECT{0, 0, ::GetSystemMetrics(SM_CXSCREEN),
                        ::GetSystemMetrics(SM_CYSCREEN)};
        }
        return area;
    }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND open_button_ = nullptr;
    HWND html_button_ = nullptr;
    HWND csv_button_ = nullptr;
    HWND package_button_ = nullptr;
    HWND batch_button_ = nullptr;
    HWND compare_button_ = nullptr;
    HWND record_label_ = nullptr;
    HWND record_combo_ = nullptr;
    HWND phase_a_ = nullptr;
    HWND phase_b_ = nullptr;
    HWND phase_c_ = nullptr;
    HWND cycles_ = nullptr;
    HWND onset_edit_ = nullptr;
    HWND pre_edit_ = nullptr;
    HWND fault_edit_ = nullptr;
    HWND line_edit_ = nullptr;
    HWND log_edit_ = nullptr;
    HWND unit_channel_ = nullptr;
    HWND unit_edit_ = nullptr;
    HWND apply_button_ = nullptr;
    HWND trip_combo_ = nullptr;
    HWND contact52a_combo_ = nullptr;
    HWND contact52b_combo_ = nullptr;
    HWND trip_polarity_ = nullptr;
    HWND contact52a_polarity_ = nullptr;
    HWND contact52b_polarity_ = nullptr;
    HWND trip_label_ = nullptr;
    HWND contact52a_label_ = nullptr;
    HWND contact52b_label_ = nullptr;
    HWND onset_label_ = nullptr;
    HWND pre_label_ = nullptr;
    HWND fault_label_ = nullptr;
    HWND line_label_ = nullptr;
    HWND log_label_ = nullptr;
    HWND unit_label_ = nullptr;
    HWND phase_a_label_ = nullptr;
    HWND phase_b_label_ = nullptr;
    HWND phase_c_label_ = nullptr;
    HWND cycles_label_ = nullptr;
    std::array<HWND, 5> mode_buttons_{};
    HWND channel_label_ = nullptr;
    HWND channel_list_ = nullptr;
    HWND diagnostics_label_ = nullptr;
    HWND diagnostics_list_ = nullptr;
    WNDPROC diagnostics_original_proc_ = nullptr;
    HWND status_ = nullptr;
    WaveformView waveform_;
    HBRUSH background_ = nullptr;
    HFONT ui_font_ = nullptr;
    HFONT small_font_ = nullptr;
    HACCEL accelerator_ = nullptr;
    int dpi_ = 96;
    int cycle_count_ = 1;
    double onset_ratio_ = 1.5;
    double reference_length_km_ = 0.0;
    std::wstring initial_path_;

    std::vector<RecordDescriptor> records_;
    std::map<std::size_t, analysis::BreakerTimingOptions>
        breaker_options_by_record_;
    std::vector<Diagnostic> open_diagnostics_;
    std::vector<Diagnostic> last_load_diagnostics_;
    std::shared_ptr<LoadedRecord> loaded_;
    report::ComparisonResult comparison_;
    std::vector<report::BatchItem> batch_items_;
    std::vector<std::string> batch_failures_;
    std::vector<std::shared_ptr<LoadedRecord>> batch_records_;
    std::size_t selected_index_ = 0;
    bool suppress_combo_ = false;
    UINT phase_dropdown_id_ = 0;
    LRESULT phase_selection_before_dropdown_ = CB_ERR;
    bool loading_ = false;
    std::thread worker_;
    std::mutex pending_mutex_;
    std::shared_ptr<LoadResult> pending_;
    std::atomic<bool> closing_{false};
};

}              

int runApplication(HINSTANCE instance, int show_command,
                   const std::wstring& initial_path) {
    MainWindow window(instance);
    if (!window.create(show_command, initial_path)) {
        ::MessageBoxW(nullptr,
                      L"No se pudo crear la ventana principal de la aplicación.",
                      L"Analizador COMTRADE", MB_OK | MB_ICONERROR);
        return 1;
    }
    return window.run();
}

}                                 
