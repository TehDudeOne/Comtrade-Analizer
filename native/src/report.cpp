                                                                               
  
                                                                             
                                                                              

#include "analizador/report.hpp"

#include "analizador/comtrade.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace analizador::report {
namespace {

                                                                              
                                  
                                                                              

std::string wideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), size, nullptr, nullptr);
    return out;
}

std::string windowsErrorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = ::FormatMessageW(
        flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
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

std::string formatDouble(double value, int precision) {
    if (!std::isfinite(value)) {
        return {};
    }
    char buffer[96];
    const auto converted = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::general,
        precision);
    if (converted.ec != std::errc{}) {
        return {};
    }
    return std::string(buffer, converted.ptr);
}

std::string formatFull(double value) {
    return formatDouble(value, std::numeric_limits<double>::max_digits10);
}

std::string displayNumber(double value, int precision = 10) {
    if (!std::isfinite(value)) {
        return "No disponible";
    }

                                                                               
                                                                            
    const int significant_digits = std::max(1, precision);
    int decimal_places = 0;
    if (value != 0.0) {
        const double exponent = std::floor(std::log10(std::abs(value)));
        const double places = significant_digits - 1.0 - exponent;
        decimal_places = static_cast<int>(std::clamp(places, 0.0, 350.0));
    }

    char buffer[768];
    const auto converted = std::to_chars(
        buffer, buffer + sizeof(buffer), value, std::chars_format::fixed,
        decimal_places);
    if (converted.ec != std::errc{}) {
        return "No disponible";
    }

    std::string text(buffer, converted.ptr);
    const auto decimal_point = text.find('.');
    if (decimal_point != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

std::string jsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    return formatFull(value);
}

std::string formatCount(std::size_t value) {
    return std::to_string(value);
}

                                                                              
          
                                                                              

std::string htmlEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                if (c < 0x20 && c != '\n' && c != '\r' && c != '\t') {
                    static constexpr char kHex[] = "0123456789ABCDEF";
                    out += "&#x";
                    out.push_back(kHex[(c >> 4) & 0x0F]);
                    out.push_back(kHex[c & 0x0F]);
                    out += ';';
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 8);
    static constexpr char kHex[] = "0123456789abcdef";
    for (const char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out.push_back(kHex[(c >> 4) & 0x0F]);
                    out.push_back(kHex[c & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

void appendJsonString(std::string& out, std::string_view value) {
    out.push_back('"');
    out += jsonEscape(value);
    out.push_back('"');
}

void appendJsonKey(std::string& out, std::string_view key) {
    appendJsonString(out, key);
    out += ": ";
}

                                                                              
                                            
                                                                              

class FileWriter {
public:
    FileWriter() = default;
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    ~FileWriter() {
        closeHandle();
        if (!committed_ && !temporary_.empty()) {
            ::DeleteFileW(temporary_.c_str());
        }
    }

    bool open(const std::filesystem::path& final_path, std::string& error) {
        if (final_path.empty()) {
            error = "La ruta de salida está vacía.";
            return false;
        }
        final_ = final_path;
        static std::atomic<std::uint64_t> sequence{0};
        for (unsigned int attempt = 0; attempt < 128; ++attempt) {
            std::filesystem::path candidate = final_path;
            candidate += L".tmp.";
            candidate += std::to_wstring(::GetCurrentProcessId());
            candidate += L".";
            candidate += std::to_wstring(::GetTickCount64());
            candidate += L".";
            candidate += std::to_wstring(sequence.fetch_add(1));
            handle_ = ::CreateFileW(
                candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) {
                temporary_ = std::move(candidate);
                return true;
            }
            const DWORD error_code = ::GetLastError();
            if (error_code != ERROR_FILE_EXISTS &&
                error_code != ERROR_ALREADY_EXISTS) {
                error = windowsErrorText(error_code);
                return false;
            }
        }
        error = "No se pudo reservar un nombre temporal único para el informe.";
        return false;
    }

    bool write(std::string_view bytes, std::string& error) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const std::size_t remaining = bytes.size() - offset;
            const DWORD chunk = static_cast<DWORD>(
                std::min<std::size_t>(remaining, 1ull << 20));
            DWORD written = 0;
            if (!::WriteFile(handle_, bytes.data() + offset, chunk, &written,
                             nullptr)) {
                error = windowsErrorText(::GetLastError());
                return false;
            }
            if (written == 0) {
                error = "Windows no escribió ningún byte.";
                return false;
            }
            offset += written;
        }
        return true;
    }

    bool commit(std::string& error) {
        if (!closeHandle()) {
            error = windowsErrorText(::GetLastError());
            return false;
        }
        if (!::MoveFileExW(temporary_.c_str(), final_.c_str(),
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error = windowsErrorText(::GetLastError());
            return false;
        }
        committed_ = true;
        temporary_.clear();
        return true;
    }

private:
    bool closeHandle() {
        if (handle_ == INVALID_HANDLE_VALUE) {
            return true;
        }
        const bool ok = ::CloseHandle(handle_) != 0;
        handle_ = INVALID_HANDLE_VALUE;
        return ok;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::filesystem::path final_;
    std::filesystem::path temporary_;
    bool committed_ = false;
};

Result writeAll(const std::filesystem::path& path, std::string_view bytes) {
    FileWriter writer;
    std::string error;
    if (!writer.open(path, error)) {
        return {false, "No se pudo crear el archivo de salida: " + error};
    }
    if (!writer.write(bytes, error)) {
        return {false, "No se pudo escribir el archivo de salida: " + error};
    }
    if (!writer.commit(error)) {
        return {false, "No se pudo finalizar el archivo de salida: " + error};
    }
    return {true, {}};
}

                                                                              
                         
                                                                              

std::string dateTimeText(const DateTime& value) {
    if (!value.valid) {
        return "No disponible";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%06d",
                  value.year, value.month, value.day, value.hour, value.minute,
                  value.second, value.microsecond);
    return buffer;
}

std::string severityLabel(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Error:
            return "Error";
        case DiagnosticSeverity::Warning:
            return "Advertencia";
        case DiagnosticSeverity::Info:
            return "Información";
    }
    return "Diagnóstico";
}

std::string severityClass(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Error:
            return "error";
        case DiagnosticSeverity::Warning:
            return "warning";
        case DiagnosticSeverity::Info:
            return "info";
    }
    return "info";
}

std::string diagnosticLocation(const Diagnostic& diagnostic) {
    std::string location;
    if (diagnostic.has_line) {
        location += "línea " + formatCount(diagnostic.line);
    }
    if (diagnostic.has_record) {
        if (!location.empty()) {
            location += ", ";
        }
        location += "muestra " + formatCount(diagnostic.record + 1);
    }
    if (diagnostic.has_sample_number) {
        if (!location.empty()) {
            location += ", ";
        }
        location += "número " + formatCount(diagnostic.sample_number);
    }
    return location;
}

std::string normalizedUnit(const std::string& unit) {
    return unit.empty() ? "(sin unidad)" : unit;
}

const char* kChannelPalette[] = {
    "#0057B8", "#D14900", "#007A3D", "#CC79A7", "#56B4E9", "#E69F00",
    "#332288", "#AA4499", "#44AA99", "#999933", "#882255", "#661100"};

const char* paletteColor(std::size_t index) {
    return kChannelPalette[index % (sizeof(kChannelPalette) /
                                    sizeof(kChannelPalette[0]))];
}

                                                                              
                
                                                                              

struct TimeReference {
    double seconds = 0.0;
    const char* axis_title = "ms desde el inicio del registro";
    bool from_trigger = false;
};

TimeReference timeReference(const ComtradeRecord& record) {
    TimeReference reference;
    if (record.data.times_s.empty()) {
        return reference;
    }
    const double first = record.data.times_s.front();
    const double last = record.data.times_s.back();
    if (record.trigger_offset_s) {
        const double trigger = *record.trigger_offset_s;
        if (std::isfinite(trigger) && trigger >= first && trigger <= last) {
            reference.seconds = trigger;
            reference.axis_title = "ms desde el trigger CFG";
            reference.from_trigger = true;
            return reference;
        }
    }
    reference.seconds = first;
    reference.axis_title = "ms desde el inicio del registro";
    return reference;
}

struct SvgChartGroup {
    std::string unit;
    std::vector<std::size_t> channels;
};

std::vector<SvgChartGroup> buildChartGroups(const ComtradeRecord& record,
                                            std::size_t max_channels) {
    std::vector<SvgChartGroup> groups;
    if (max_channels == 0) {
        return groups;
    }
    const std::size_t limit =
        std::min(max_channels, record.cfg.analog.size());
    for (std::size_t index = 0; index < limit; ++index) {
        const std::string unit =
            normalizedUnit(record.cfg.analog[index].unit);
        auto found = std::find_if(groups.begin(), groups.end(),
                                  [&](const SvgChartGroup& item) {
                                      return item.unit == unit;
                                  });
        if (found == groups.end()) {
            groups.push_back({unit, {index}});
        } else {
            found->channels.push_back(index);
        }
    }
    return groups;
}

std::string svgPathForChannel(const ComtradeRecord& record,
                              std::size_t channel,
                              const std::vector<std::size_t>& sample_indices,
                              double x0,
                              double x1,
                              double y_min,
                              double y_max,
                              double plot_left,
                              double plot_top,
                              double plot_width,
                              double plot_height) {
    const double x_span = x1 - x0;
    const double y_span = y_max - y_min;
    std::string path;
    path.reserve(sample_indices.size() * 16 + 16);
    bool open = false;
    for (const std::size_t sample : sample_indices) {
        if (sample >= record.data.times_s.size()) {
            open = false;
            continue;
        }
        const double value = record.data.analogValue(
            sample, channel);
        if (!std::isfinite(value)) {
            open = false;
            continue;
        }
        const double time = record.data.times_s[sample];
        const double x = plot_left + (time - x0) / x_span * plot_width;
        const double y = plot_top + (y_max - value) / y_span * plot_height;
        if (!std::isfinite(x) || !std::isfinite(y)) {
            open = false;
            continue;
        }
        if (!open) {
            path += "M";
            open = true;
        } else {
            path += "L";
        }
        path += formatDouble(x, 7);
        path += ' ';
        path += formatDouble(y, 7);
    }
    return path;
}

std::string renderSvgCharts(
    const ComtradeRecord& record,
    const Options& options,
    const analysis::RecordAnalysis* record_analysis) {
    if (record.data.sampleCount() == 0 || record.data.times_s.empty() ||
        record.cfg.analog.empty()) {
        return "<p class=\"note\">No hay muestras analógicas para dibujar.</p>";
    }

    const std::size_t max_channels =
        options.max_svg_channels == 0 ? 12 : options.max_svg_channels;
    const std::size_t max_points =
        options.max_svg_points < 32 ? 32 : options.max_svg_points;
    const auto groups = buildChartGroups(record, max_channels);
    const TimeReference reference = timeReference(record);

    const double first_time = record.data.times_s.front();
    const double last_time = record.data.times_s.back();
    double x0 = first_time;
    double x1 = last_time;
    double focus_time = reference.seconds;
    if (record_analysis && record_analysis->event &&
        std::isfinite(record_analysis->event->time_s) &&
        record_analysis->event->time_s >= first_time &&
        record_analysis->event->time_s <= last_time) {
        focus_time = record_analysis->event->time_s;
    }
    double frequency = record_analysis &&
                               std::isfinite(record_analysis->frequency_hz) &&
                               record_analysis->frequency_hz > 0.0
                           ? record_analysis->frequency_hz
                           : record.cfg.nominal_frequency_hz;
    if (!std::isfinite(frequency) || frequency <= 0.0) {
        frequency = 50.0;
    }
    constexpr double kCyclesBeforeEvent = 5.0;
    constexpr double kCyclesAfterEvent = 20.0;
    const double period = 1.0 / frequency;
    x0 = std::max(first_time, focus_time - kCyclesBeforeEvent * period);
    x1 = std::min(last_time, focus_time + kCyclesAfterEvent * period);
    if (!(x1 > x0)) {
        x0 -= 0.5;
        x1 += 0.5;
    }

    std::string html;
    html.reserve(groups.size() * 6000);
    html += "<p class=\"note\">La gráfica muestra una ventana enfocada en el "
            "evento (5 ciclos previos y 20 posteriores); el CSV conserva todas "
            "las muestras del registro. "
            "La referencia temporal es ";
    html += reference.from_trigger ? "el trigger CFG" : "el inicio del registro";
    html += " y no se infiere zona horaria.</p>";

    for (std::size_t group_index = 0; group_index < groups.size();
         ++group_index) {
        const auto& group = groups[group_index];
        const std::size_t count =
            std::min(record.data.sampleCount(), record.data.times_s.size());
        const auto first_visible = std::lower_bound(
            record.data.times_s.begin(), record.data.times_s.begin() + count,
            x0);
        const auto after_visible = std::upper_bound(
            first_visible, record.data.times_s.begin() + count, x1);
        const std::size_t first_index = static_cast<std::size_t>(
            std::distance(record.data.times_s.begin(), first_visible));
        const std::size_t visible_count = static_cast<std::size_t>(
            std::distance(first_visible, after_visible));
        if (visible_count == 0) {
            html += "<p class=\"note\">No hay muestras dentro de la ventana "
                    "del evento para graficar.</p>";
            continue;
        }
        const std::size_t stride =
            std::max<std::size_t>(
                1, (visible_count + max_points - 1) / max_points);
        std::vector<std::size_t> indices;
        indices.reserve(std::min(visible_count, max_points + 2));
        const std::size_t end_index = first_index + visible_count;
        for (std::size_t sample = first_index; sample < end_index;
             sample += stride) {
            indices.push_back(sample);
        }
        if (indices.empty() || indices.back() != end_index - 1) {
            indices.push_back(end_index - 1);
        }

        double y_min = std::numeric_limits<double>::infinity();
        double y_max = -std::numeric_limits<double>::infinity();
        for (const std::size_t sample : indices) {
            for (const std::size_t channel : group.channels) {
                const double value = record.data.analogValue(sample, channel);
                if (std::isfinite(value)) {
                    y_min = std::min(y_min, value);
                    y_max = std::max(y_max, value);
                }
            }
        }
        if (!std::isfinite(y_min) || !std::isfinite(y_max)) {
            html += "<p class=\"note\">La unidad ";
            html += htmlEscape(group.unit);
            html += " no contiene valores finitos para dibujar.</p>";
            continue;
        }
        if (y_max <= y_min) {
            const double padding =
                std::max(1.0, std::abs(y_min) * 0.05);
            y_min -= padding;
            y_max += padding;
        } else {
            const double padding = (y_max - y_min) * 0.06;
            y_min -= padding;
            y_max += padding;
        }

        constexpr double width = 960.0;
        constexpr double height = 330.0;
        constexpr double left = 82.0;
        constexpr double top = 22.0;
        constexpr double right = 26.0;
        constexpr double bottom = 48.0;
        const double plot_width = width - left - right;
        const double plot_height = height - top - bottom;
        const std::string clip_id =
            "plot-clip-" + std::to_string(group_index);
        const std::string title_id =
            "plot-title-" + std::to_string(group_index);

        html += "<figure class=\"chart\">";
        html += "<figcaption id=\"";
        html += title_id;
        html += "\">Trazas analógicas · unidad ";
        html += htmlEscape(group.unit);
        html += "</figcaption>";
        html += "<svg viewBox=\"0 0 960 330\" role=\"img\" aria-labelledby=\"";
        html += title_id;
        html += "\">";
        html += "<title>";
        html += htmlEscape("Trazas analógicas en " + group.unit);
        html += "</title>";
        html += "<defs><clipPath id=\"";
        html += clip_id;
        html += "\"><rect x=\"";
        html += formatDouble(left, 2);
        html += "\" y=\"";
        html += formatDouble(top, 2);
        html += "\" width=\"";
        html += formatDouble(plot_width, 2);
        html += "\" height=\"";
        html += formatDouble(plot_height, 2);
        html += "\"/></clipPath></defs>";
        html += "<rect class=\"plot-bg\" x=\"";
        html += formatDouble(left, 2);
        html += "\" y=\"";
        html += formatDouble(top, 2);
        html += "\" width=\"";
        html += formatDouble(plot_width, 2);
        html += "\" height=\"";
        html += formatDouble(plot_height, 2);
        html += "\"/>";

        for (int tick = 0; tick <= 4; ++tick) {
            const double ratio = static_cast<double>(tick) / 4.0;
            const double y = top + plot_height * ratio;
            const double value = y_max + (y_min - y_max) * ratio;
            html += "<line class=\"grid\" x1=\"";
            html += formatDouble(left, 2);
            html += "\" y1=\"";
            html += formatDouble(y, 2);
            html += "\" x2=\"";
            html += formatDouble(left + plot_width, 2);
            html += "\" y2=\"";
            html += formatDouble(y, 2);
            html += "\"/>";
            html += "<text class=\"axis\" x=\"";
            html += formatDouble(left - 9.0, 2);
            html += "\" y=\"";
            html += formatDouble(y + 4.0, 2);
            html += "\" text-anchor=\"end\">";
            html += htmlEscape(displayNumber(value, 6));
            html += "</text>";
        }

        for (int tick = 0; tick <= 5; ++tick) {
            const double ratio = static_cast<double>(tick) / 5.0;
            const double x = left + plot_width * ratio;
            const double time = x0 + (x1 - x0) * ratio;
            html += "<line class=\"grid\" x1=\"";
            html += formatDouble(x, 2);
            html += "\" y1=\"";
            html += formatDouble(top, 2);
            html += "\" x2=\"";
            html += formatDouble(x, 2);
            html += "\" y2=\"";
            html += formatDouble(top + plot_height, 2);
            html += "\"/>";
            html += "<text class=\"axis\" x=\"";
            html += formatDouble(x, 2);
            html += "\" y=\"";
            html += formatDouble(top + plot_height + 20.0, 2);
            html += "\" text-anchor=\"middle\">";
            html += htmlEscape(displayNumber((time - reference.seconds) * 1000.0, 6));
            html += "</text>";
        }

        if (reference.from_trigger && reference.seconds >= x0 &&
            reference.seconds <= x1) {
            const double trigger_x =
                left + (reference.seconds - x0) / (x1 - x0) * plot_width;
            html += "<line class=\"trigger\" x1=\"";
            html += formatDouble(trigger_x, 2);
            html += "\" y1=\"";
            html += formatDouble(top, 2);
            html += "\" x2=\"";
            html += formatDouble(trigger_x, 2);
            html += "\" y2=\"";
            html += formatDouble(top + plot_height, 2);
            html += "\"/><text class=\"trigger-label\" x=\"";
            html += formatDouble(trigger_x + 5.0, 2);
            html += "\" y=\"";
            html += formatDouble(top + 14.0, 2);
            html += "\">Trigger</text>";
        }

        html += "<g clip-path=\"url(#";
        html += clip_id;
        html += ")\">";
                                                                           
                                                                            
        for (std::size_t color_index = 0; color_index < group.channels.size();
             ++color_index) {
            const std::size_t channel = group.channels[color_index];
            const std::string path = svgPathForChannel(
                record, channel, indices, x0, x1, y_min, y_max, left, top,
                plot_width, plot_height);
            if (path.empty()) {
                continue;
            }
            html += "<path class=\"trace\" d=\"";
            html += path;
            html += "\" stroke=\"";
            html += paletteColor(color_index);
            html += "\"/>";
        }
        html += "</g>";

        html += "<text class=\"axis-title\" x=\"";
        html += formatDouble(left + plot_width / 2.0, 2);
        html += "\" y=\"";
        html += formatDouble(height - 7.0, 2);
        html += "\" text-anchor=\"middle\">";
        html += htmlEscape(reference.axis_title);
        html += "</text>";
        html += "</svg>";

        html += "<div class=\"legend\">";
        for (std::size_t color_index = 0; color_index < group.channels.size();
             ++color_index) {
            const std::size_t channel = group.channels[color_index];
            const auto& cfg_channel = record.cfg.analog[channel];
            html += "<span><i style=\"background:";
            html += paletteColor(color_index);
            html += "\"></i>";
            html += htmlEscape(cfg_channel.name.empty()
                                   ? "Canal " + formatCount(channel + 1)
                                   : cfg_channel.name);
            html += " <small>(";
            html += htmlEscape(normalizedUnit(cfg_channel.unit));
            html += ")</small></span>";
        }
        html += "</div></figure>";
    }

    if (record.cfg.analog.size() > max_channels) {
        html += "<p class=\"note\">Se muestran los primeros ";
        html += formatCount(max_channels);
        html += " canales analógicos de ";
        html += formatCount(record.cfg.analog.size());
        html += " en el informe. El CSV incluye todos.</p>";
    }
    return html;
}

                                                                              
                  
                                                                              

void appendTableRow(std::string& out, std::string_view label,
                    std::string_view value) {
    out += "<tr><th scope=\"row\">";
    out += htmlEscape(label);
    out += "</th><td>";
    out += htmlEscape(value);
    out += "</td></tr>";
}

std::string renderDiagnostics(const std::vector<Diagnostic>* diagnostics) {
    std::string out;
    if (!diagnostics || diagnostics->empty()) {
        out += "<p class=\"ok\">No hay diagnósticos de lectura/análisis.</p>";
        return out;
    }
    out += "<ul class=\"diagnostics\">";
    for (const auto& diagnostic : *diagnostics) {
        out += "<li class=\"diag ";
        out += severityClass(diagnostic.severity);
        out += "\"><strong>";
        out += htmlEscape(severityLabel(diagnostic.severity));
        out += "</strong>";
        if (!diagnostic.code.empty()) {
            out += " <code>";
            out += htmlEscape(diagnostic.code);
            out += "</code>";
        }
        out += "<br>";
        out += htmlEscape(diagnostic.message);
        const std::string location = diagnosticLocation(diagnostic);
        if (!location.empty()) {
            out += "<br><span class=\"muted\">Ubicación: ";
            out += htmlEscape(location);
            out += "</span>";
        }
        if (!diagnostic.context.empty()) {
            out += "<br><span class=\"muted\">Contexto: ";
            out += htmlEscape(diagnostic.context);
            out += "</span>";
        }
        out += "</li>";
    }
    out += "</ul>";
    return out;
}

void appendAnalogTable(std::string& out, const CfgInfo& cfg) {
    if (cfg.analog.empty()) {
        out += "<p class=\"note\">El CFG no declara canales analógicos.</p>";
        return;
    }
    out += "<div class=\"table-wrap\"><table><thead><tr>"
           "<th>#</th><th>Nombre</th><th>Fase</th><th>Componente</th>"
           "<th>Unidad</th><th>A</th><th>B</th><th>P/S</th>"
           "<th>Primario</th><th>Secundario</th><th>Skew (µs)</th>"
           "</tr></thead><tbody>";
    for (const auto& channel : cfg.analog) {
        out += "<tr><td>";
        out += formatCount(channel.position + 1);
        out += "</td><td>";
        out += htmlEscape(channel.name);
        out += "</td><td>";
        out += htmlEscape(channel.phase);
        out += "</td><td>";
        out += htmlEscape(channel.circuit_component);
        out += "</td><td>";
        out += htmlEscape(normalizedUnit(channel.unit));
        out += "</td><td>";
        out += htmlEscape(displayNumber(channel.a));
        out += "</td><td>";
        out += htmlEscape(displayNumber(channel.b));
        out += "</td><td>";
        out += htmlEscape(channel.ps);
        out += "</td><td>";
        out += htmlEscape(displayNumber(channel.primary));
        out += "</td><td>";
        out += htmlEscape(displayNumber(channel.secondary));
        out += "</td><td>";
        out += htmlEscape(displayNumber(channel.skew_us));
        out += "</td></tr>";
    }
    out += "</tbody></table></div>";
}

void appendDigitalTable(std::string& out, const CfgInfo& cfg) {
    if (cfg.digital.empty()) {
        out += "<p class=\"note\">El CFG no declara canales digitales.</p>";
        return;
    }
    out += "<div class=\"table-wrap\"><table><thead><tr>"
           "<th>#</th><th>Nombre</th><th>Fase</th><th>Componente</th>"
           "<th>Estado normal</th></tr></thead><tbody>";
    for (const auto& channel : cfg.digital) {
        out += "<tr><td>";
        out += formatCount(channel.position + 1);
        out += "</td><td>";
        out += htmlEscape(channel.name);
        out += "</td><td>";
        out += htmlEscape(channel.phase);
        out += "</td><td>";
        out += htmlEscape(channel.circuit_component);
        out += "</td><td>";
        out += htmlEscape(channel.normal_state);
        out += "</td></tr>";
    }
    out += "</tbody></table></div>";
}

void appendSamplingTable(std::string& out, const CfgInfo& cfg) {
    if (cfg.rates.empty()) {
        out += "<p class=\"note\">El CFG no declara tasas de muestreo; el "
               "parser no infiere una tasa para el informe.</p>";
        return;
    }
    out += "<div class=\"table-wrap\"><table><thead><tr>"
           "<th>Segmento</th><th>Tasa (Hz)</th><th>Última muestra</th>"
           "</tr></thead><tbody>";
    std::size_t segment = 1;
    for (const auto& rate : cfg.rates) {
        out += "<tr><td>";
        out += formatCount(segment++);
        out += "</td><td>";
        out += htmlEscape(displayNumber(rate.rate_hz));
        out += "</td><td>";
        out += formatCount(rate.end_sample);
        out += "</td></tr>";
    }
    out += "</tbody></table></div>";
}


std::string phaseName(std::size_t slot) {
    static constexpr const char* kNames[] = {"A", "B", "C"};
    return slot < 3 ? kNames[slot] : "?";
}

std::string optionalNumberText(const std::optional<double>& value,
                               int precision = 8) {
    return value ? displayNumber(*value, precision) : "No disponible";
}

std::string phasorText(const analysis::Phasor& phasor) {
    if (!phasor.isFinite()) {
        return "No disponible";
    }
    return displayNumber(phasor.magnitude(), 8) + " ∠ " +
           displayNumber(phasor.angleDegrees(), 5) + "°";
}

std::string sequencePhasorText(const std::complex<double>& value) {
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
        return "No disponible";
    }
    return displayNumber(std::abs(value), 8) + " ∠ " +
           displayNumber(std::arg(value) * 180.0 / analysis::kPi, 5) + "°";
}

                                                                            
                                                               
std::string saturationStateText(
    const std::optional<analysis::SaturationIndicator>& saturation) {
    if (!saturation.has_value() || !saturation->available) {
        return "no evaluado";
    }
    return saturation->possible
               ? "indicios compatibles con saturación o recorte"
               : "sin indicios con este criterio";
}

std::string percentOrNoCalculable(const std::optional<double>& ratio,
                                  bool ratio_is_fraction) {
    if (!ratio.has_value() || !std::isfinite(*ratio)) {
        return "No calculable";
    }
    const double value = ratio_is_fraction ? *ratio * 100.0 : *ratio;
    return displayNumber(value, 8) + " %";
}

std::string breakerSecondsText(const std::optional<double>& value) {
    if (!value.has_value() || !std::isfinite(*value)) {
        return "No observado / no calculable";
    }
    return displayNumber(*value, 12) + " s";
}

std::string breakerMillisecondsText(const std::optional<double>& value) {
    if (!value.has_value() || !std::isfinite(*value)) {
        return "No calculable";
    }
    return displayNumber(*value, 8) + " ms";
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

bool containsDiagnostic(const std::vector<Diagnostic>& diagnostics,
                        const Diagnostic& candidate) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [&](const Diagnostic& existing) {
                           return sameDiagnostic(existing, candidate);
                       });
}

std::string phaseChannelName(const CfgInfo& cfg,
                             const analysis::PhaseSelection& phases,
                             std::size_t slot) {
    if (slot >= 3 || !phases.index[slot].has_value()) {
        return "No identificado";
    }
    const std::size_t index = *phases.index[slot];
    if (index >= cfg.analog.size()) {
        return "Índice fuera de rango";
    }
    return cfg.analog[index].name.empty() ? "Canal sin nombre"
                                          : cfg.analog[index].name;
}

std::string phaseChannelUnit(const CfgInfo& cfg,
                             const analysis::PhaseSelection& phases,
                             std::size_t slot) {
    if (slot >= 3 || !phases.index[slot].has_value()) {
        return "";
    }
    const std::size_t index = *phases.index[slot];
    return index < cfg.analog.size() ? cfg.analog[index].unit : "";
}

constexpr const char* kCurrentPhasorColors[] = {
    "#0057B8", "#D14900", "#007A3D"};

std::string phasorAxisTickText(double value) {
    std::string text = displayNumber(value, 6);
    if (text.size() > 14) {
                                                                          
                                                                                
        text = formatDouble(value, 6);
        if (text.empty()) {
            text = "0";
        }
    }
    return text;
}

double phasorNiceStep(double span) {
    if (!(span > 0.0) || !std::isfinite(span)) {
        return 1.0;
    }
    const double exponent = std::floor(std::log10(span));
    const double base = std::pow(10.0, exponent);
    if (!(base > 0.0) || !std::isfinite(base)) {
        return span;
    }
    const double fraction = span / base;
    const double nice = fraction <= 1.0 ? 1.0
                       : fraction <= 2.0 ? 2.0
                       : fraction <= 5.0 ? 5.0
                                         : 10.0;
    const double step = nice * base;
    return (step > 0.0 && std::isfinite(step)) ? step : span;
}

struct CurrentPhasorPanel {
    std::string unit;
    double extent = 0.0;
    bool any_nonzero = false;
};

                                                                          
                                                                              
std::string renderCurrentPhasorDiagram(
    const analysis::RecordAnalysis& record_analysis, const CfgInfo& cfg) {
    std::vector<CurrentPhasorPanel> panels;
    std::vector<std::string> omitted;

    const auto collect_panel =
        [&](const char* label,
            const std::optional<std::array<analysis::Phasor, 3>>& values) {
            if (!values) {
                return;
            }
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const auto& phasor = (*values)[slot];
                const double re = phasor.value.real();
                const double im = phasor.value.imag();
                if (!phasor.isFinite() || !std::isfinite(re) ||
                    !std::isfinite(im)) {
                    omitted.push_back(std::string(label) + " " +
                                      phaseName(slot));
                    continue;
                }
                const std::string unit =
                    phaseChannelUnit(cfg, record_analysis.phases, slot);
                auto panel = std::find_if(
                    panels.begin(), panels.end(),
                    [&](const CurrentPhasorPanel& candidate) {
                        return candidate.unit == unit;
                    });
                if (panel == panels.end()) {
                    panels.emplace_back();
                    panel = panels.end() - 1;
                    panel->unit = unit;
                }
                const double component = std::max(std::abs(re), std::abs(im));
                if (std::isfinite(component)) {
                    panel->extent = std::max(panel->extent, component);
                    if (component > 0.0) {
                        panel->any_nonzero = true;
                    }
                }
            }
        };
    collect_panel("Prefalla", record_analysis.prefalla_phasors);
    collect_panel("Falla", record_analysis.falla_phasors);

    std::string out;
    out += "<h3>Diagrama fasorial de corriente · prefalla y falla</h3>";
    out += "<p class=\"note\">Fasores RMS de las ventanas de prefalla y falla "
           "seleccionadas. Ejes real e imaginario con "
           "la misma escala en X e Y para conservar los ángulos. Cada panel "
           "usa una sola unidad CFG; no se convierten unidades.</p>";

    if (panels.size() > 1) {
        out += "<p class=\"note\">Las fases usan unidades CFG distintas: se "
               "muestran paneles separados por unidad y no se comparan sus "
               "escalas.</p>";
    }

    const auto svg_number = [](double value) {
                                                                              
        return formatDouble(value, 12);
    };

    constexpr double kViewWidth = 480.0;
    constexpr double kViewHeight = 440.0;
    constexpr double kPlotLeft = 82.0;
    constexpr double kPlotTop = 24.0;
    constexpr double kPlotSize = 340.0;
    constexpr double kHalfPlot = kPlotSize / 2.0;
    const double center_x = kPlotLeft + kHalfPlot;
    const double center_y = kPlotTop + kHalfPlot;
    const double plot_right = kPlotLeft + kPlotSize;
    const double plot_bottom = kPlotTop + kPlotSize;

    for (std::size_t panel_index = 0; panel_index < panels.size();
         ++panel_index) {
        const auto& panel = panels[panel_index];
        const std::string title_id =
            "current-phasor-title-" + std::to_string(panel_index);
        const std::string desc_id =
            "current-phasor-desc-" + std::to_string(panel_index);
        const std::string unit_text = normalizedUnit(panel.unit);

        double data_radius = 1.0;
        if (panel.extent > 0.0 && std::isfinite(panel.extent)) {
            constexpr double kMargin = 1.15;
            if (panel.extent >
                std::numeric_limits<double>::max() / kMargin) {
                data_radius = panel.extent;
            } else {
                data_radius = panel.extent * kMargin;
            }
        }
        if (!(data_radius > 0.0) || !std::isfinite(data_radius)) {
            data_radius = 1.0;
        }
        const double tick_step = phasorNiceStep(data_radius / 2.0);

        out += "<figure class=\"chart\">";
        out += "<figcaption>Diagrama fasorial de corriente · unidad CFG ";
        out += htmlEscape(unit_text);
        out += "</figcaption>";
        out += "<svg viewBox=\"0 0 ";
        out += svg_number(kViewWidth);
        out += " ";
        out += svg_number(kViewHeight);
        out += "\" role=\"img\" aria-labelledby=\"";
        out += title_id;
        out += " ";
        out += desc_id;
        out += "\">";
        out += "<title id=\"";
        out += title_id;
        out += "\">";
        out += htmlEscape("Diagrama fasorial de corriente en unidad CFG " +
                          unit_text +
                          "; ejes real e imaginario; prefalla y falla");
        out += "</title><desc id=\"";
        out += desc_id;
        out += "\">";
        out += htmlEscape(
            "Origen en el centro del área cuadrada. El eje horizontal es la "
            "parte real y el vertical la parte imaginaria, ambos con la misma "
            "escala. Línea continua: prefalla; línea discontinua: falla.");
        out += "</desc>";

        out += "<rect class=\"plot-bg\" x=\"";
        out += svg_number(kPlotLeft);
        out += "\" y=\"";
        out += svg_number(kPlotTop);
        out += "\" width=\"";
        out += svg_number(kPlotSize);
        out += "\" height=\"";
        out += svg_number(kPlotSize);
        out += "\"/>";

        for (int tick_index = -2; tick_index <= 2; ++tick_index) {
            if (tick_index == 0) {
                continue;
            }
            const double tick_value =
                tick_step * static_cast<double>(tick_index);
            if (!std::isfinite(tick_value) ||
                std::abs(tick_value) > data_radius) {
                continue;
            }
            const double offset = (tick_value / data_radius) * kHalfPlot;
            const double x = center_x + offset;
            const double y = center_y - offset;

            out += "<line class=\"grid\" x1=\"";
            out += svg_number(x);
            out += "\" y1=\"";
            out += svg_number(kPlotTop);
            out += "\" x2=\"";
            out += svg_number(x);
            out += "\" y2=\"";
            out += svg_number(plot_bottom);
            out += "\"/>";
            out += "<line class=\"grid\" x1=\"";
            out += svg_number(kPlotLeft);
            out += "\" y1=\"";
            out += svg_number(y);
            out += "\" x2=\"";
            out += svg_number(plot_right);
            out += "\" y2=\"";
            out += svg_number(y);
            out += "\"/>";

            out += "<text class=\"axis\" x=\"";
            out += svg_number(x);
            out += "\" y=\"";
            out += svg_number(plot_bottom + 16.0);
            out += "\" text-anchor=\"middle\">";
            out += htmlEscape(phasorAxisTickText(tick_value));
            out += "</text><text class=\"axis\" x=\"";
            out += svg_number(kPlotLeft - 8.0);
            out += "\" y=\"";
            out += svg_number(y + 4.0);
            out += "\" text-anchor=\"end\">";
            out += htmlEscape(phasorAxisTickText(tick_value));
            out += "</text>";
        }

        out += "<line x1=\"";
        out += svg_number(kPlotLeft);
        out += "\" y1=\"";
        out += svg_number(center_y);
        out += "\" x2=\"";
        out += svg_number(plot_right);
        out += "\" y2=\"";
        out += svg_number(center_y);
        out += "\" stroke=\"#8fa0b3\" stroke-width=\"1.2\"/>";
        out += "<line x1=\"";
        out += svg_number(center_x);
        out += "\" y1=\"";
        out += svg_number(kPlotTop);
        out += "\" x2=\"";
        out += svg_number(center_x);
        out += "\" y2=\"";
        out += svg_number(plot_bottom);
        out += "\" stroke=\"#8fa0b3\" stroke-width=\"1.2\"/>";
        out += "<circle cx=\"";
        out += svg_number(center_x);
        out += "\" cy=\"";
        out += svg_number(center_y);
        out += "\" r=\"3\" fill=\"#18212f\"/>";

        for (std::size_t slot = 0; slot < 3; ++slot) {
            const std::string slot_unit =
                phaseChannelUnit(cfg, record_analysis.phases, slot);
            if (slot_unit != panel.unit) {
                continue;
            }
            const char* color =
                slot < 3 ? kCurrentPhasorColors[slot] : "#5d6b7d";
            for (int series = 0; series < 2; ++series) {
                const auto& values =
                    series == 0 ? record_analysis.prefalla_phasors
                                : record_analysis.falla_phasors;
                if (!values) {
                    continue;
                }
                const auto& phasor = (*values)[slot];
                const double re = phasor.value.real();
                const double im = phasor.value.imag();
                if (!phasor.isFinite() || !std::isfinite(re) ||
                    !std::isfinite(im)) {
                    continue;
                }
                const double x =
                    center_x + (re / data_radius) * kHalfPlot;
                const double y =
                    center_y - (im / data_radius) * kHalfPlot;
                if (!std::isfinite(x) || !std::isfinite(y)) {
                    continue;
                }
                const bool fault = series == 1;
                const double dx = x - center_x;
                const double dy = y - center_y;
                const double length = std::hypot(dx, dy);

                if (length < 0.75) {
                                                                               
                    out += "<circle cx=\"";
                    out += svg_number(x);
                    out += "\" cy=\"";
                    out += svg_number(y);
                    out += "\" r=\"3\" fill=\"none\" stroke=\"";
                    out += color;
                    out += "\" stroke-width=\"2\"/>";
                    continue;
                }

                out += "<line x1=\"";
                out += svg_number(center_x);
                out += "\" y1=\"";
                out += svg_number(center_y);
                out += "\" x2=\"";
                out += svg_number(x);
                out += "\" y2=\"";
                out += svg_number(y);
                out += "\" stroke=\"";
                out += color;
                out += "\" stroke-width=\"2.4\" stroke-linecap=\"round\"";
                if (fault) {
                    out += " stroke-dasharray=\"7 5\"";
                }
                out += "/>";

                const double ux = dx / length;
                const double uy = dy / length;
                const double head_length = std::min(9.0, length * 0.35);
                const double head_half = std::min(4.0, length * 0.18);
                const double base_x = x - ux * head_length;
                const double base_y = y - uy * head_length;
                const double left_x = base_x - uy * head_half;
                const double left_y = base_y + ux * head_half;
                const double right_x = base_x + uy * head_half;
                const double right_y = base_y - ux * head_half;

                out += "<polygon points=\"";
                out += svg_number(x) + "," + svg_number(y) + " " +
                       svg_number(left_x) + "," + svg_number(left_y) + " " +
                       svg_number(right_x) + "," + svg_number(right_y);
                out += "\" fill=\"";
                out += color;
                out += "\"/>";
                out += "<circle cx=\"";
                out += svg_number(x);
                out += "\" cy=\"";
                out += svg_number(y);
                out += "\" r=\"2.8\" fill=\"";
                out += color;
                out += "\"/>";
            }
        }

        out += "<text class=\"axis-title\" x=\"";
        out += svg_number(center_x);
        out += "\" y=\"";
        out += svg_number(plot_bottom + 38.0);
        out += "\" text-anchor=\"middle\">Parte real · ";
        out += htmlEscape(unit_text);
        out += "</text><text class=\"axis-title\" x=\"18\" y=\"";
        out += svg_number(center_y);
        out += "\" text-anchor=\"middle\" transform=\"rotate(-90 18 ";
        out += svg_number(center_y);
        out += ")\">Parte imaginaria · ";
        out += htmlEscape(unit_text);
        out += "</text></svg>";

        out += "<div class=\"legend\"><span><i style=\"background:#18212f\">"
               "</i>Prefalla · línea continua</span><span><i style=\""
               "background:repeating-linear-gradient(90deg,#18212f 0 5px,"
               "transparent 5px 9px)\"></i>Falla · línea discontinua</span>"
               "<span>Escala común del panel, igual en X e Y.</span></div>";

        out += "<div class=\"legend\">";
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const std::string slot_unit =
                phaseChannelUnit(cfg, record_analysis.phases, slot);
            if (slot_unit != panel.unit) {
                continue;
            }
            const char* color =
                slot < 3 ? kCurrentPhasorColors[slot] : "#5d6b7d";
            for (int series = 0; series < 2; ++series) {
                const auto& values =
                    series == 0 ? record_analysis.prefalla_phasors
                                : record_analysis.falla_phasors;
                if (!values) {
                    continue;
                }
                const auto& phasor = (*values)[slot];
                if (!phasor.isFinite() ||
                    !std::isfinite(phasor.value.real()) ||
                    !std::isfinite(phasor.value.imag())) {
                    continue;
                }
                out += "<span><i style=\"";
                if (series == 1) {
                    out += "background:repeating-linear-gradient(90deg,";
                    out += color;
                    out += " 0 5px,transparent 5px 9px)";
                } else {
                    out += "background:";
                    out += color;
                }
                out += "\"></i>Fase ";
                out += phaseName(slot);
                out += " · ";
                out += htmlEscape(
                    phaseChannelName(cfg, record_analysis.phases, slot));
                out += series == 0 ? " · Prefalla · " : " · Falla · ";
                out += htmlEscape(phasorText(phasor));
                out += " ";
                out += htmlEscape(unit_text);
                out += "</span>";
            }
        }
        out += "</div>";
        if (!panel.any_nonzero) {
            out += "<p class=\"note\">Todos los fasores válidos de este panel "
                   "son cero; se marcan en el origen y no definen una "
                   "dirección.</p>";
        }
        out += "</figure>";
    }

    if (panels.empty()) {
        out += "<p class=\"note\">No hay fasores actuales finitos para "
               "dibujar. Se conservan las tablas de magnitud y ángulo como "
               "referencia.</p>";
    }
    if (!record_analysis.prefalla_phasors) {
        out += "<p class=\"note\">Prefalla no disponible para esta ventana; "
               "no se dibuja esa serie.</p>";
    }
    if (!record_analysis.falla_phasors) {
        out += "<p class=\"note\">Falla no disponible para esta ventana; no "
               "se dibuja esa serie.</p>";
    }
    if (!omitted.empty()) {
        out += "<p class=\"note\">Fasores no finitos omitidos del dibujo: ";
        bool first = true;
        for (const auto& item : omitted) {
            if (!first) {
                out += "; ";
            }
            first = false;
            out += htmlEscape(item);
        }
        out += ".</p>";
    }
    if (!record_analysis.prefalla_status.empty()) {
        out += "<p class=\"note\">Referencia prefalla informada por el motor: ";
        out += htmlEscape(record_analysis.prefalla_status);
        out += ". La ventana de falla es la seleccionada por el motor, no un "
               "dictamen de falla.</p>";
    } else {
        out += "<p class=\"note\">La ventana de falla es la seleccionada por "
               "el motor, no un dictamen de falla.</p>";
    }
    return out;
}

std::string breakerAssignmentText(const CfgInfo* cfg,
    const analysis::DigitalSignalAssignment& assignment);

std::string renderAnalysisDetails(
    const analysis::RecordAnalysis& record_analysis, const CfgInfo& cfg) {
    std::string out;
    out.reserve(18 * 1024);
    out += "<details><summary>Detalle técnico del análisis orientativo</summary>";
    out += "<p class=\"note\">Resultados del motor compartido. Son estimaciones "
           "conservadoras; no son tiempos de operación de protección ni un "
           "dictamen de falla.</p>";

    out += "<h3>Alineación temporal</h3><div class=\"table-wrap\"><table><tbody>";
    appendTableRow(out, "Timestamps utilizables", record_analysis.time.ok ? "Sí" : "No");
    appendTableRow(out, "Primer timestamp DAT (s)",
                   displayNumber(record_analysis.time.first_s, 12));
    appendTableRow(out, "Último timestamp DAT (s)",
                   displayNumber(record_analysis.time.last_s, 12));
    appendTableRow(out, "Intervalo mediano (s)",
                   displayNumber(record_analysis.time.median_dt_s, 12));
    appendTableRow(out, "Tasa mediana informativa (Hz)",
                   displayNumber(record_analysis.time.median_rate_hz, 12));
    appendTableRow(out, "Primera muestra no cero",
                   record_analysis.time.first_sample_nonzero ? "Sí; se conserva" : "No");
    appendTableRow(out, "Trigger CFG interpretable",
                   record_analysis.time.trigger_offset_known ? "Sí" : "No");
    appendTableRow(out, "Trigger - inicio CFG (s)",
                   displayNumber(record_analysis.time.trigger_offset_cfg_s, 12));
    appendTableRow(out, "Trigger dentro de la captura",
                   record_analysis.time.trigger_within_capture ? "Sí" : "No");
    appendTableRow(out, "Referencia de análisis (s)",
                   displayNumber(record_analysis.time.reference_s, 12));
    appendTableRow(out, "Etiqueta de referencia",
                   record_analysis.time.reference_label.empty()
                       ? "No disponible"
                       : record_analysis.time.reference_label);
    appendTableRow(out, "Origen de la traza",
                   analysis::waveformOriginName(record_analysis.origin));
    appendTableRow(out, "Etiqueta del origen",
                   record_analysis.origin_label.empty() ? "No disponible"
                                                 : record_analysis.origin_label);
    out += "</tbody></table></div>";

    out += "<h3>Canales de fase usados</h3><div class=\"table-wrap\"><table>"
           "<thead><tr><th>Fase</th><th>Canal</th><th>Unidad</th>"
           "</tr></thead><tbody>";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        out += "<tr><td>";
        out += phaseName(slot);
        out += "</td><td>";
        out += htmlEscape(phaseChannelName(cfg, record_analysis.phases, slot));
        out += "</td><td>";
        out += htmlEscape(normalizedUnit(phaseChannelUnit(cfg, record_analysis.phases, slot)));
        out += "</td></tr>";
    }
    out += "</tbody></table></div>";
    if (!record_analysis.phases.complete()) {
        out += "<p class=\"note\">La selección automática no completó las tres "
               "fases; fasores, componentes simétricas y clasificación pueden "
               "quedar incompletos. No se reutilizan canales entre fases.</p>";
    }
    if (!record_analysis.phases.ambiguous.empty()) {
        out += "<p class=\"note\">Canales ambiguos excluidos: ";
        bool first = true;
        for (const auto& item : record_analysis.phases.ambiguous) {
            if (!first) {
                out += "; ";
            }
            first = false;
            out += htmlEscape(item.second);
        }
        out += ".</p>";
    }

    out += "<h3>Evento candidato</h3><div class=\"table-wrap\"><table><tbody>";
    if (record_analysis.event) {
        const auto& event = *record_analysis.event;
        appendTableRow(out, "Inicio candidato (s)",
                       displayNumber(event.time_s, 12));
        appendTableRow(out, "Dirección",
                       event.direction_text.empty()
                           ? analysis::changeDirectionName(event.direction)
                           : event.direction_text);
        appendTableRow(out, "RMS base", displayNumber(event.baseline_rms, 8));
        appendTableRow(out, "RMS en el cambio", displayNumber(event.rms, 8));
        appendTableRow(out, "Cambio RMS", displayNumber(event.delta, 8));
        appendTableRow(out, "Umbral", displayNumber(event.threshold_delta, 8));
        appendTableRow(out, "Cambio / umbral",
                       displayNumber(event.contrast, 8));
        appendTableRow(out, "Pico / base",
                       optionalNumberText(event.peak_contrast));
        appendTableRow(out, "Muestras de ventana",
                       formatCount(static_cast<std::size_t>(
                           std::max(0, event.window_samples))));
        appendTableRow(out, "Fuente de la base",
                       event.baseline_source.empty() ? "No disponible"
                                                     : event.baseline_source);
        if (event.recovery_detected && event.recovery_s) {
            appendTableRow(out, "Recuperación candidata (s)",
                           displayNumber(*event.recovery_s, 12));
            appendTableRow(out, "Duración observada (ms)",
                           displayNumber(event.duration_s * 1000.0, 8));
        } else {
            appendTableRow(out, "Recuperación",
                           "No observada dentro de la captura");
            appendTableRow(out, "Duración observada",
                           "Cota inferior ≥ " +
                               displayNumber(event.duration_s * 1000.0, 8) +
                               " ms");
        }
    } else {
        out += "<tr><td colspan=\"2\">No se detectó un cambio de régimen RMS "
               "que supere el umbral y persista; esto no descarta una falla, "
               "en especial si comenzó antes de la captura.</td></tr>";
    }
    out += "</tbody></table></div>";

    out += "<h3>Ventanas de análisis</h3><div class=\"table-wrap\"><table><tbody>";
    appendTableRow(out, "Estado de la referencia prefalla", record_analysis.prefalla_status);
    appendTableRow(out, "Selección de referencia", record_analysis.prefalla_manual ? "Manual" : "Automática");
    appendTableRow(out, "Intervalo de armónicos (s desde inicio DAT)",
        displayNumber(record_analysis.falla_center_s - record_analysis.window_half_s, 12) +
        " a " + displayNumber(record_analysis.falla_center_s + record_analysis.window_half_s, 12));
    appendTableRow(out, "Centro prefalla (s)",
                   displayNumber(record_analysis.prefalla_center_s, 12));
    appendTableRow(out, "Centro falla (s)",
                   displayNumber(record_analysis.falla_center_s, 12));
    appendTableRow(out, "Semiventana (s)",
                   displayNumber(record_analysis.window_half_s, 12));
    appendTableRow(out, "Prefalla ajustada al borde",
                   record_analysis.prefalla_clamped ? "Sí" : "No");
    appendTableRow(out, "Falla ajustada al borde",
                   record_analysis.falla_clamped ? "Sí" : "No");
    appendTableRow(out, "Ventanas solapadas",
                   record_analysis.windows_overlap ? "Sí; no se clasifica" : "No");
    out += "</tbody></table></div>";

    out += "<h3>Cambios de régimen por etapas</h3><p class=\"note\">"
           "Candidatos de resolución aproximada de un ciclo; un cambio digital cercano "
           "no acredita disparo ni causa. Los armónicos describen solo la ventana seleccionada.</p>";
    if (!record_analysis.regime_changes.empty()) {
        out += "<div class=\"table-wrap\"><table><thead><tr><th>Tiempo DAT (s)</th>"
               "<th>Fases</th><th>Cambio de tensión cercano</th><th>Transición digital cercana</th>"
               "</tr></thead><tbody>";
        for (const auto& change : record_analysis.regime_changes) {
            std::string phases;
            for (std::size_t phase = 0; phase < 3; ++phase) {
                if (!change.phase_direction[phase]) continue;
                phases += std::string(1, 'A' + static_cast<char>(phase)) +
                    (change.phase_direction[phase] > 0 ? " ascenso; " : " descenso; ");
            }
            out += "<tr><td>" + displayNumber(change.time_s, 12) + "</td><td>" + phases +
                "</td><td>" + (change.voltage_change_nearby ? "Sí" : "No observado / no disponible") +
                "</td><td>" + (change.digital_transition_nearby ? "Sí" : "No observado / no disponible") +
                "</td></tr>";
        }
        out += "</tbody></table></div>";
    } else {
        out += "<p>No se identificaron etapas con este criterio; no descarta una perturbación.</p>";
    }

    const auto appendPhasorTable =
        [&](const char* label,
            const std::optional<std::array<analysis::Phasor, 3>>& values) {
            out += "<h3>Fasores RMS · ";
            out += htmlEscape(label);
            out += "</h3>";
            if (!values) {
                out += "<p class=\"note\">No disponibles para esta ventana.</p>";
                return;
            }
            out += "<div class=\"table-wrap\"><table><thead><tr>"
                   "<th>Fase</th><th>Fasor RMS</th><th>Unidad</th>"
                   "</tr></thead><tbody>";
            for (std::size_t slot = 0; slot < 3; ++slot) {
                out += "<tr><td>";
                out += phaseName(slot);
                out += "</td><td>";
                out += htmlEscape(phasorText((*values)[slot]));
                out += "</td><td>";
                out += htmlEscape(normalizedUnit(
                    phaseChannelUnit(cfg, record_analysis.phases, slot)));
                out += "</td></tr>";
            }
            out += "</tbody></table></div>";
        };
    appendPhasorTable("Prefalla", record_analysis.prefalla_phasors);
    appendPhasorTable("Falla", record_analysis.falla_phasors);

    out += renderCurrentPhasorDiagram(record_analysis, cfg);

    const auto appendSequenceTable =
        [&](const char* label,
            const std::optional<analysis::SequenceSet>& sequence) {
            out += "<h3>Componentes simétricas · ";
            out += htmlEscape(label);
            out += "</h3>";
            if (!sequence) {
                out += "<p class=\"note\">No disponibles.</p>";
                return;
            }
            out += "<div class=\"table-wrap\"><table><thead><tr>"
                   "<th>Secuencia</th><th>Fasor RMS</th>"
                   "</tr></thead><tbody>";
            const auto append_sequence_row =
                [&](const char* label, const std::complex<double>& value) {
                    out += "<tr><td>";
                    out += htmlEscape(label);
                    out += "</td><td>";
                    out += htmlEscape(sequencePhasorText(value));
                    out += "</td></tr>";
                };
            append_sequence_row("I0 cero", sequence->zero);
            append_sequence_row("I1 positiva", sequence->positive);
            append_sequence_row("I2 negativa", sequence->negative);
            out += "</tbody></table></div>";
        };
    appendSequenceTable("Prefalla", record_analysis.prefalla_sequences);
    appendSequenceTable("Falla", record_analysis.falla_sequences);

    if (record_analysis.classification) {
        const auto& classification = *record_analysis.classification;
        out += "<h3>Clasificación orientativa por secuencias</h3><p><strong>";
        out += htmlEscape(classification.label);
        out += "</strong></p><p>";
        out += htmlEscape(classification.explanation);
        out += "</p><div class=\"table-wrap\"><table><tbody>";
        appendTableRow(out, "I0 / I1", displayNumber(classification.i0_i1, 8));
        appendTableRow(out, "I2 / I1", displayNumber(classification.i2_i1, 8));
        appendTableRow(out, "I1 falla / I1 prefalla",
                       displayNumber(classification.i1_fault_pre, 8));
        out += "</tbody></table></div>";
    } else {
        out += "<p class=\"note\">No se emitió clasificación por secuencias. "
               "Puede faltar una fase, las ventanas pueden solaparse o la "
               "coherencia traza ↔ fasor no ser suficiente.</p>";
    }

    out += "<h3>Cocientes de secuencia</h3><div class=\"table-wrap\"><table><tbody>";
    appendTableRow(out, "I0 / I1", optionalNumberText(record_analysis.ratios.i0_i1));
    appendTableRow(out, "I2 / I1", optionalNumberText(record_analysis.ratios.i2_i1));
    appendTableRow(out, "I1 falla / I1 prefalla",
                   optionalNumberText(record_analysis.ratios.i1_fault_pre));
    out += "</tbody></table></div>";

    out += "<h3>Armónicos y THD</h3>";
    bool any_harmonics = false;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& fit = record_analysis.harmonics[slot];
        if (!fit.ok) {
            continue;
        }
        any_harmonics = true;
        out += "<h4>Fase ";
        out += phaseName(slot);
        out += " · ";
        out += htmlEscape(phaseChannelName(cfg, record_analysis.phases, slot));
        out += "</h4><div class=\"table-wrap\"><table><thead><tr>"
               "<th>Orden</th><th>RMS en unidad CFG</th></tr></thead><tbody>";
        for (std::size_t order = 0; order < fit.harmonics_rms.size(); ++order) {
            out += "<tr><td>h";
            out += formatCount(order + 1);
            out += "</td><td>";
            out += htmlEscape(displayNumber(fit.harmonics_rms[order], 8));
            out += "</td></tr>";
        }
        out += "</tbody></table></div><p class=\"note\">Ciclos: ";
        out += formatCount(static_cast<std::size_t>(fit.cycles));
        out += " · Orden máximo soportado: ";
        out += formatCount(static_cast<std::size_t>(fit.supported_order));
        out += " · Nyquist: ";
        out += htmlEscape(displayNumber(fit.nyquist_hz, 8));
        out += " Hz · THD: ";
        out += fit.thd_pct ? htmlEscape(displayNumber(*fit.thd_pct, 8)) + " %"
                           : "No disponible";
        out += " · H2/H1: ";
        out += fit.h2_h1_pct
                   ? htmlEscape(displayNumber(*fit.h2_h1_pct, 8)) + " %"
                   : "No disponible";
        out += "</p>";
    }
    if (!any_harmonics) {
        out += "<p class=\"note\">No se pudo ajustar armónicos para las fases "
               "seleccionadas.</p>";
    }

    out += "<h3>Tensión (unidad CFG, sin conversión)</h3>";
    const auto& voltage = record_analysis.voltage;
    if (!voltage.available &&
        !voltage.prefalla_phasors.has_value() &&
        !voltage.falla_phasors.has_value()) {
        out += "<p class=\"note\">— No hay datos de tensión disponibles: el CFG "
               "no permitió identificar tres canales de tensión A/B/C o el "
               "análisis no los calculó. No se inventaron magnitudes ni "
               "unidades.</p>";
    } else {
        out += "<p class=\"note\">Fasores RMS de tensión en la unidad CFG del "
               "canal. No se aplican relaciones de transformador ni "
               "conversiones P/S. Unidad informada: ";
        out += htmlEscape(voltage.unit.empty() ? std::string("(no declarada)")
                                               : voltage.unit);
        out += voltage.units_consistent
                   ? " (las tres fases declaran la misma unidad normalizada)."
                   : " (unidades no consistentes o incompletas; revise el CFG).";
        out += "</p>";
        out += "<div class=\"table-wrap\"><table><thead><tr><th>Fase</th>"
               "<th>Canal</th><th>Unidad</th></tr></thead><tbody>";
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& index = voltage.selection.index[slot];
            std::string name = "—";
            std::string unit;
            if (index.has_value() && *index < cfg.analog.size()) {
                name = cfg.analog[*index].name.empty()
                           ? "Canal " + formatCount(*index + 1)
                           : cfg.analog[*index].name;
                unit = cfg.analog[*index].unit;
            }
            out += "<tr><td>" + phaseName(slot) + "</td><td>" +
                   htmlEscape(name) + "</td><td>" +
                   htmlEscape(normalizedUnit(unit)) + "</td></tr>";
        }
        out += "</tbody></table></div>";

        const auto append_voltage_table =
            [&](const char* label,
                const std::optional<std::array<analysis::Phasor, 3>>& values) {
                out += "<h4>Fasores de tensión · ";
                out += htmlEscape(label);
                out += "</h4>";
                if (!values) {
                    out += "<p class=\"note\">— No disponibles para esta "
                           "ventana.</p>";
                    return;
                }
                out += "<div class=\"table-wrap\"><table><thead><tr><th>Fase</th>"
                       "<th>Fasor RMS</th><th>Unidad</th></tr></thead><tbody>";
                for (std::size_t slot = 0; slot < 3; ++slot) {
                    std::string unit;
                    const auto& index = voltage.selection.index[slot];
                    if (index.has_value() && *index < cfg.analog.size()) {
                        unit = cfg.analog[*index].unit;
                    }
                    out += "<tr><td>" + phaseName(slot) + "</td><td>" +
                           htmlEscape(phasorText((*values)[slot])) +
                           "</td><td>" + htmlEscape(normalizedUnit(unit)) +
                           "</td></tr>";
                }
                out += "</tbody></table></div>";
            };
        append_voltage_table("Prefalla", voltage.prefalla_phasors);
        append_voltage_table("Falla", voltage.falla_phasors);

        out += "<h4>Cambio de magnitud de tensión (falla frente a prefalla)</h4>"
               "<div class=\"table-wrap\"><table><thead><tr><th>Fase</th>"
               "<th>Cambio (%)</th></tr></thead><tbody>";
        for (std::size_t slot = 0; slot < 3; ++slot) {
            out += "<tr><td>" + phaseName(slot) + "</td><td>";
            out += voltage.change_pct[slot].has_value()
                       ? htmlEscape(displayNumber(*voltage.change_pct[slot], 6)) +
                             " %"
                       : "—";
            out += "</td></tr>";
        }
        out += "</tbody></table></div>";

        const auto append_voltage_sequences =
            [&](const char* label,
                const std::optional<analysis::SequenceSet>& sequence) {
                out += "<h4>Componentes simétricas de tensión · ";
                out += htmlEscape(label);
                out += "</h4>";
                if (!sequence) {
                    out += "<p class=\"note\">— No disponibles.</p>";
                    return;
                }
                out += "<div class=\"table-wrap\"><table><thead><tr>"
                       "<th>Secuencia</th><th>Fasor RMS</th></tr></thead><tbody>";
                const auto append_one =
                    [&](const char* name, const std::complex<double>& value) {
                        out += "<tr><td>" + htmlEscape(name) + "</td><td>" +
                               htmlEscape(sequencePhasorText(value)) +
                               "</td></tr>";
                    };
                append_one("V0 cero", sequence->zero);
                append_one("V1 positiva", sequence->positive);
                append_one("V2 negativa", sequence->negative);
                out += "</tbody></table></div>";
            };
        append_voltage_sequences("Prefalla", voltage.prefalla_sequences);
        append_voltage_sequences("Falla", voltage.falla_sequences);
        if (!voltage.diagnostics.empty()) {
            out += "<p class=\"note\">Diagnósticos de tensión: ";
            for (std::size_t index = 0; index < voltage.diagnostics.size();
                 ++index) {
                if (index != 0) {
                    out += " · ";
                }
                out += htmlEscape(voltage.diagnostics[index].message);
            }
            out += ".</p>";
        }
    }

    out += "<h3>Espectro FFT de inspección (unidad CFG)</h3>";
    bool any_spectrum = false;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& spectrum = record_analysis.spectra[slot];
        if (!spectrum.has_value() || !spectrum->ok) {
            continue;
        }
        any_spectrum = true;
        out += "<h4>Fase " + phaseName(slot) + " · " +
               htmlEscape(phaseChannelName(cfg, record_analysis.phases, slot)) +
               "</h4><div class=\"table-wrap\"><table><tbody>";
        appendTableRow(out, "Unidad CFG",
                       spectrum->unit.empty() ? "(no declarada)" : spectrum->unit);
        appendTableRow(out, "Fundamental (Hz)",
                       displayNumber(spectrum->fundamental_hz, 8));
        appendTableRow(out, "Tasa mediana de la ventana (Hz)",
                       displayNumber(spectrum->sample_rate_hz, 8));
        appendTableRow(out, "Inicio de ventana (s)",
                       displayNumber(spectrum->window_start_s, 12));
        appendTableRow(out, "Fin de ventana (s)",
                       displayNumber(spectrum->window_end_s, 12));
        appendTableRow(out, "Puntos FFT",
                       formatCount(spectrum->fft_points));
        appendTableRow(out, "Orden máximo soportado",
                       formatCount(static_cast<std::size_t>(
                           std::max(0, spectrum->supported_order))));
        appendTableRow(out, "THD",
                       spectrum->thd_pct
                           ? displayNumber(*spectrum->thd_pct, 8) + " %"
                           : "—");
        appendTableRow(out, "H2/H1",
                       spectrum->h2_h1_pct
                           ? displayNumber(*spectrum->h2_h1_pct, 8) + " %"
                           : "—");
        out += "</tbody></table></div>";
        if (!spectrum->harmonics_rms.empty()) {
            out += "<div class=\"table-wrap\"><table><thead><tr><th>Orden</th>"
                   "<th>RMS en unidad CFG</th></tr></thead><tbody>";
            for (std::size_t order = 0;
                 order < spectrum->harmonics_rms.size(); ++order) {
                out += "<tr><td>h" + formatCount(order + 1) + "</td><td>" +
                       htmlEscape(displayNumber(
                           spectrum->harmonics_rms[order], 8)) +
                       "</td></tr>";
            }
            out += "</tbody></table></div>";
        } else {
            out += "<p class=\"note\">— El espectro no expuso armónicos "
                   "resolubles.</p>";
        }
    }
    if (!any_spectrum) {
        out += "<p class=\"note\">— No hay espectro FFT calculado para este "
               "registro; no se muestran magnitudes inventadas.</p>";
    }

    out += "<h3>Indicador heurístico de saturación y diagnóstico avanzado</h3>";
    out += "<p class=\"note\">Tarjeta orientativa construida con los cocientes "
           "de secuencia, fasores e indicadores ya calculados por el motor. "
           "Estados posibles: <strong>indicios compatibles con saturación o "
           "recorte</strong>, <strong>sin indicios con este criterio</strong> "
           "o <strong>no evaluado</strong>. No es un diagnóstico físico, no "
           "declara severidad y no certifica normalidad.</p>";

    out += "<h4>Contexto de secuencia y ventana</h4>";
    out += "<div class=\"table-wrap\"><table><tbody>";
    appendTableRow(out, "Ventana de falla (s desde inicio DAT)",
                   displayNumber(record_analysis.falla_center_s -
                                     record_analysis.window_half_s,
                                 12) +
                       " a " +
                       displayNumber(record_analysis.falla_center_s +
                                         record_analysis.window_half_s,
                                     12));
    appendTableRow(out, "Centro prefalla (s)",
                   displayNumber(record_analysis.prefalla_center_s, 12));
    appendTableRow(out, "Centro falla (s)",
                   displayNumber(record_analysis.falla_center_s, 12));
    appendTableRow(out, "Semiventana (s)",
                   displayNumber(record_analysis.window_half_s, 12));
    appendTableRow(out, "I2 / I1 × 100",
                   percentOrNoCalculable(record_analysis.ratios.i2_i1, true));
    appendTableRow(out, "I0 / I1 × 100",
                   percentOrNoCalculable(record_analysis.ratios.i0_i1, true));
    appendTableRow(out, "I1 falla / I1 prefalla",
                   record_analysis.ratios.i1_fault_pre.has_value() &&
                           std::isfinite(*record_analysis.ratios.i1_fault_pre)
                       ? displayNumber(*record_analysis.ratios.i1_fault_pre, 8)
                       : "No calculable");
    out += "</tbody></table></div>";

    out += "<h4>Canales de secuencia A/B/C</h4>";
    out += "<div class=\"table-wrap\"><table><thead><tr><th>Fase</th>"
           "<th>Canal</th><th>Unidad CFG</th><th>Fasor prefalla</th>"
           "<th>Fasor falla</th></tr></thead><tbody>";
    std::vector<std::string> sequence_units;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const std::string unit =
            normalizedUnit(phaseChannelUnit(cfg, record_analysis.phases, slot));
        sequence_units.push_back(unit);
        out += "<tr><td>" + htmlEscape(phaseName(slot)) + "</td><td>" +
               htmlEscape(phaseChannelName(cfg, record_analysis.phases, slot)) +
               "</td><td>" + htmlEscape(unit) + "</td><td>";
        out += record_analysis.prefalla_phasors
                   ? htmlEscape(phasorText((*record_analysis.prefalla_phasors)[slot]))
                   : "No disponible";
        out += "</td><td>";
        out += record_analysis.falla_phasors
                   ? htmlEscape(phasorText((*record_analysis.falla_phasors)[slot]))
                   : "No disponible";
        out += "</td></tr>";
    }
    out += "</tbody></table></div>";
    const bool same_sequence_unit =
        sequence_units.size() == 3 && sequence_units[0] != "(sin unidad)" &&
        sequence_units[0] == sequence_units[1] &&
        sequence_units[1] == sequence_units[2];
    if (same_sequence_unit) {
        out += "<p class=\"note\">Unidad CFG común de las tres fases: ";
        out += htmlEscape(sequence_units[0]);
        out += ". Los cocientes de secuencia son adimensionales.</p>";
    } else {
        out += "<p class=\"note\">Las fases no comparten una unidad CFG "
               "normalizada única; cada fila conserva la unidad de su canal "
               "y no se asume ninguna unidad común.</p>";
    }

    out += "<h4>Indicadores por canal: aplanamiento, H2 y curvatura</h4>";
    out += "<p class=\"note\">Cada indicador se informa por separado. Un "
           "indicador ausente no equivale a cero; un valor no evaluado no es "
           "un resultado favorable.</p>";
    out += "<div class=\"table-wrap\"><table><thead><tr><th>Fase</th>"
           "<th>Canal</th><th>Unidad CFG</th><th>Estado</th>"
           "<th>Aplanamiento (%)</th><th>Aplanamiento (ms)</th>"
           "<th>Aplanamiento elevado</th><th>H2/H1 (%)</th>"
           "<th>Umbral H2/H1 (%)</th><th>H2 elevada</th>"
           "<th>Hombros</th><th>Curvatura (× mediana)</th>"
           "<th>Fracción alta</th></tr></thead><tbody>";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& saturation = record_analysis.saturation[slot];
        const bool available =
            saturation.has_value() && saturation->available;
        const auto metric_text =
            [&](const std::optional<double>& value, const char* suffix) {
                if (!available) {
                    return std::string("No evaluado");
                }
                if (!value.has_value() || !std::isfinite(*value)) {
                    return std::string("No calculable");
                }
                return displayNumber(*value, 8) + suffix;
            };
        std::optional<double> flat_value;
        std::optional<double> h2_value;
        if (available) {
            flat_value = saturation->flat_top_pct;
            h2_value = saturation->h2_h1_pct;
        }
        out += "<tr><td>" + htmlEscape(phaseName(slot)) + "</td><td>" +
               htmlEscape(phaseChannelName(cfg, record_analysis.phases, slot)) +
               "</td><td>" +
               htmlEscape(normalizedUnit(phaseChannelUnit(
                   cfg, record_analysis.phases, slot))) +
               "</td><td>" + htmlEscape(saturationStateText(saturation)) +
               "</td><td>" +
               htmlEscape(metric_text(flat_value, " %")) + "</td><td>";
        if (!available) {
            out += "No evaluado";
        } else {
            out += htmlEscape(displayNumber(saturation->longest_flat_top_ms, 8) +
                              " ms");
        }
        out += "</td><td>";
        out += !available ? "No evaluado"
                          : (saturation->flat_top_elevated ? "Sí" : "No");
        out += "</td><td>" + htmlEscape(metric_text(h2_value, " %")) +
               "</td><td>";
        if (!available) {
            out += "No evaluado";
        } else {
            out += htmlEscape(displayNumber(saturation->h2_threshold_pct, 6) +
                              " %");
        }
        out += "</td><td>";
        out += !available ? "No evaluado"
                          : (saturation->h2_elevated ? "Sí" : "No");
        out += "</td><td>";
        if (!available) {
            out += "No evaluado";
        } else {
            out += formatCount(static_cast<std::size_t>(
                       std::max(0, saturation->shoulder_hits))) +
                   " de " +
                   formatCount(static_cast<std::size_t>(
                       std::max(0, saturation->shoulder_required_hits)));
        }
        out += "</td><td>";
        out += !available
                   ? "No evaluado"
                   : htmlEscape(displayNumber(saturation->curvature_threshold, 6) +
                                "×");
        out += "</td><td>";
        out += !available
                   ? "No evaluado"
                   : htmlEscape(displayNumber(saturation->high_fraction * 100.0,
                                              6) +
                                " %");
        out += "</td></tr>";
    }
    out += "</tbody></table></div>";

    bool any_saturation = false;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& saturation = record_analysis.saturation[slot];
        if (!saturation.has_value() || !saturation->available) {
            continue;
        }
        any_saturation = true;
        out += "<h4>Detalle evaluado · Fase " + phaseName(slot) + " · " +
               htmlEscape(phaseChannelName(cfg, record_analysis.phases, slot)) +
               "</h4><div class=\"table-wrap\"><table><tbody>";
        appendTableRow(out, "Estado", saturationStateText(saturation));
        appendTableRow(out, "H2/H1",
                       saturation->h2_h1_pct
                           ? displayNumber(*saturation->h2_h1_pct, 8) + " %"
                           : "No calculable");
        appendTableRow(out, "Umbral H2/H1",
                       displayNumber(saturation->h2_threshold_pct, 6) + " %");
        appendTableRow(out, "H2 elevada",
                       saturation->h2_elevated ? "Sí" : "No");
        appendTableRow(out, "Aplanamiento",
                       saturation->flat_top_pct
                           ? displayNumber(*saturation->flat_top_pct, 8) + " %"
                           : "No calculable");
        appendTableRow(out, "Duración de aplanamiento",
                       displayNumber(saturation->longest_flat_top_ms, 8) + " ms");
        appendTableRow(out, "Aplanamiento elevado",
                       saturation->flat_top_elevated ? "Sí" : "No");
        appendTableRow(out, "Hombros detectados",
                       formatCount(static_cast<std::size_t>(
                           std::max(0, saturation->shoulder_hits))) +
                           " de " +
                           formatCount(static_cast<std::size_t>(
                               std::max(0, saturation->shoulder_required_hits))));
        appendTableRow(out, "Umbral de curvatura",
                       displayNumber(saturation->curvature_threshold, 6) + "×");
        appendTableRow(out, "Fracción alta de la ventana",
                       displayNumber(saturation->high_fraction * 100.0, 6) + " %");
        appendTableRow(out, "Pico de la ventana",
                       displayNumber(saturation->shoulder_peak, 8));
        appendTableRow(out, "Unidad CFG del pico",
                       phaseChannelUnit(cfg, record_analysis.phases, slot));
        appendTableRow(out, "Metodología",
                       saturation->methodology.empty()
                           ? "Heurística conservadora; no es un diagnóstico."
                           : saturation->methodology);
        out += "</tbody></table></div>";
        if (!saturation->diagnostics.empty()) {
            out += "<p class=\"note\">Diagnósticos: ";
            for (std::size_t index = 0; index < saturation->diagnostics.size();
                 ++index) {
                if (index != 0) {
                    out += " · ";
                }
                out += htmlEscape(saturation->diagnostics[index].message);
            }
            out += ".</p>";
        }
        out += "<p class=\"note\">Este indicio no confirma saturación de TC, "
               "transformador ni relé, y no explica la causa de la falla.</p>";
    }
    if (!any_saturation) {
        out += "<p class=\"note\">— Ningún canal quedó evaluado con este "
               "criterio; no se infiere a partir de otros datos.</p>";
    }

    out += "<h3>Tiempos de interruptor (opcional)</h3>";
    out += "<p class=\"note\">Resultados del motor compartido a partir de las "
           "asignaciones explícitas de Trip, 52a y 52b. Los contactos indican "
           "posición (52a activo = cerrado; 52b activo = abierto); la apertura "
           "eléctrica es la extinción observada de corriente, no la posición "
           "del contacto. El inicio es un candidato heurístico, no un tiempo "
           "de operación de protección. Lo no observado queda como no "
           "calculable y nunca se rellena con cero.</p>";
    {
        const auto& breaker = record_analysis.breaker;
        const auto resolutionText = [&](double value,
                                        const char* unit) -> std::string {
            if (!breaker.configured) {
                return "No evaluado";
            }
            if (!std::isfinite(value) || value <= 0.0) {
                return "No calculable";
            }
            return displayNumber(value, 8) + " " + unit;
        };
        out += "<p class=\"note\">Las asignaciones Trip/52a/52b y su polaridad "
               "se eligen explícitamente en la interfaz; el resultado no las "
               "adivina ni las repite. El bloque siguiente resume únicamente "
               "los tiempos y resoluciones calculados.</p>";
        out += "<div class=\"table-wrap\"><table><tbody>";
        appendTableRow(out, "Configurado",
                       breaker.configured ? "Sí" : "No evaluado");
        appendTableRow(out, "Inicio candidato",
                       breakerSecondsText(breaker.onset_s));
        appendTableRow(out,"Trip asignado",breakerAssignmentText(&cfg,breaker.applied_options.trip));
        appendTableRow(out,"52a asignado",breakerAssignmentText(&cfg,breaker.applied_options.contact_52a));
        appendTableRow(out,"52b asignado",breakerAssignmentText(&cfg,breaker.applied_options.contact_52b));
        appendTableRow(out,"Debounce aplicado",displayNumber(breaker.applied_options.debounce_ms,8)+" ms");
        appendTableRow(out,"Umbral de extinción por fase",displayNumber(100.0*breaker.applied_options.extinction_fraction,8)+" % del máximo RMS de cada fase");
        appendTableRow(out,"Persistencia de extinción",std::to_string(breaker.applied_options.extinction_cycles)+" ciclo(s)");
        appendTableRow(out,"Incertidumbre del inicio candidato",resolutionText(breaker.onset_resolution_ms,"ms"));
        appendTableRow(out, "Trip observado",
                       breakerSecondsText(breaker.trip_s));
        appendTableRow(out, "Contacto abierto (52a/52b)",
                       breakerSecondsText(breaker.contact_open_s));
        appendTableRow(out, "Extinción de corriente",
                       breakerSecondsText(breaker.current_extinction_s));
        appendTableRow(out, "Protección (Trip - inicio)",
                       breakerMillisecondsText(breaker.protection_ms));
        appendTableRow(out, "Apertura de contacto",
                       breakerMillisecondsText(breaker.contact_opening_ms));
        appendTableRow(out, "Despeje eléctrico",
                       breakerMillisecondsText(breaker.clearing_ms));
        appendTableRow(out, "Resolución digital observada",
                       resolutionText(breaker.digital_resolution_ms, "ms"));
        appendTableRow(out, "Resolución de extinción",
                       resolutionText(breaker.extinction_resolution_ms, "ms"));
        out += "</tbody></table></div>";
        out += "<p class=\"note\">La resolución digital no es la incertidumbre total de las duraciones: los intervalos que usan el inicio candidato incluyen su incertidumbre de ciclo. La extinción es el centro de la primera ventana RMS completa baja; no es un instante físico exacto.</p>";
        out += "<p class=\"note\">Los campos no observados se informan como "
               "«No observado / no calculable»; no equivalen a 0 s ni a una "
               "operación confirmada. Los diagnósticos específicos del "
               "interruptor se incluyen en el detalle completo cuando no "
               "estaban ya integrados por el motor.</p>";
    }

    out += "<h3>Coherencia traza ↔ fasor</h3>";
    if (record_analysis.coherence_rows.empty()) {
        out += "<p class=\"note\">Sin filas de coherencia disponibles.</p>";
    } else {
        out += "<div class=\"table-wrap\"><table><thead><tr>"
               "<th>Ventana</th><th>Canal</th><th>RMS AC</th>"
               "<th>Fundamental RMS</th><th>Cociente</th></tr></thead><tbody>";
        for (const auto& row : record_analysis.coherence_rows) {
            out += "<tr><td>";
            out += htmlEscape(row.window_label);
            out += "</td><td>";
            out += htmlEscape(row.channel_name);
            out += "</td><td>";
            out += htmlEscape(displayNumber(row.ac_rms, 8));
            out += "</td><td>";
            out += htmlEscape(displayNumber(row.fundamental_rms, 8));
            out += "</td><td>";
            out += htmlEscape(displayNumber(row.ratio, 8));
            out += "</td></tr>";
        }
        out += "</tbody></table></div>";
    }

    if (record_analysis.fault_distance) {
        const auto& distance = *record_analysis.fault_distance;
        out += "<h3>Distancia de falla (solo con datos explícitos)</h3>"
               "<div class=\"table-wrap\"><table><tbody>";
        appendTableRow(out, "Estado",
                       analysis::faultDistanceStatusName(distance.status));
        appendTableRow(out, "Distancia (km)",
                       optionalNumberText(distance.distance_km));
        appendTableRow(out, "Longitud de referencia (km)",
                       displayNumber(distance.reference_length_km, 8));
        appendTableRow(out, "Cociente distancia / longitud",
                       optionalNumberText(distance.ratio));
        out += "</tbody></table></div>";
    } else {
        out += "<p class=\"note\">No se recibió un texto de bitácora con "
               "distancia y unidad explícita; no se infiere distancia ni "
               "longitud de línea.</p>";
    }

    out += "</details>";
    return out;
}

std::string analysisSummaryHtml(
    const analysis::RecordAnalysis& record_analysis) {
    std::string out = "<ul class=\"plain\">";
    if (!record_analysis.ok) {
        out += "<li>El motor de análisis no completó todos los cálculos; revise "
               "los diagnósticos antes de usar estas lecturas.</li>";
    }
    if (record_analysis.event) {
        const auto& event = *record_analysis.event;
        out += "<li>Inicio candidato heurístico en ";
        out += htmlEscape(displayNumber(event.time_s, 9));
        out += " s desde el inicio DAT; es una estimación, no un tiempo de "
               "operación de protección.</li><li>El cambio RMS es ";
        out += htmlEscape(displayNumber(event.delta, 7));
        out += " con umbral ";
        out += htmlEscape(displayNumber(event.threshold_delta, 7));
        out += " (contraste ";
        out += htmlEscape(displayNumber(event.contrast, 5));
        out += "×). ";
        if (!event.recovery_detected) {
            out += "No se observó recuperación dentro de la captura; la "
                   "duración es una cota inferior.</li>";
        } else if (event.recovery_s) {
            out += "Recuperación candidata en ";
            out += htmlEscape(displayNumber(*event.recovery_s, 9));
            out += " s.</li>";
        } else {
            out += "Se marcó recuperación, pero no se dispone del instante "
                   "candidato.</li>";
        }
    } else {
        out += "<li>No se detectó un cambio de régimen RMS que supere el "
               "umbral; esto no descarta una falla.</li>";
    }
    if (record_analysis.classification) {
        out += "<li>Lectura conservadora por componentes simétricas: <strong>";
        out += htmlEscape(record_analysis.classification->label);
        out += "</strong>. No sustituye la revisión de ajustes ni el dictamen "
               "de ingeniería.</li>";
    } else {
        out += "<li>No se emitió clasificación por secuencias; faltan fases, "
               "la prefalla no es identificable, las ventanas no son independientes o la coherencia es "
               "insuficiente.</li>";
    }
    if (!record_analysis.phases.complete()) {
        out += "<li>La selección de fases A/B/C está incompleta; fasores y "
               "secuencias deben leerse con cautela.</li>";
    }
    out += "</ul>";
    return out;
}

                                                                         
                                                                                 
std::string managementSummaryHtml(const Context& context,
                                  std::size_t errors,
                                  std::size_t warnings) {
    std::string out;
    std::vector<std::string> review_items;
    const auto contains = [](std::string_view text, std::string_view token) {
        if (token.empty() || text.size() < token.size()) return false;
        return std::search(
                   text.begin(), text.end(), token.begin(), token.end(),
                   [](unsigned char left, unsigned char right) {
                       return std::tolower(left) == std::tolower(right);
                   }) != text.end();
    };
    const auto activeValue = [&](std::string_view value) {
        return !contains(value, "off") && !contains(value, "false") &&
               !contains(value, "inactive") && value != "0";
    };
    const auto loopDescription = [](std::string loop) {
        std::transform(loop.begin(), loop.end(), loop.begin(),
                       [](unsigned char value) {
                           return static_cast<char>(std::toupper(value));
                       });
        if (loop == "AG") return std::string("fase A y tierra (AG)");
        if (loop == "BG") return std::string("fase B y tierra (BG)");
        if (loop == "CG") return std::string("fase C y tierra (CG)");
        if (loop == "AB") return std::string("fases A y B (AB)");
        if (loop == "BC") return std::string("fases B y C (BC)");
        if (loop == "CA") return std::string("fases C y A (CA)");
        if (loop == "ABC") return std::string("las tres fases (ABC)");
        return std::string("circuito ") + loop;
    };

    const analysis::RecordAnalysis* result = context.analysis;
    if (!result) {
        out += "<h3>Qué se observó</h3><p>No se pudo completar el análisis "
               "automático de este registro.</p>";
    } else {
        std::string observation;
        if (result->event) {
            observation = "Se detectó un cambio importante en las mediciones "
                          "alrededor de " +
                          displayNumber(result->event->time_s, 3) +
                          " s después del inicio de la grabación.";
        } else {
            observation = "El programa no detectó un cambio claro en las "
                          "mediciones. Esto no descarta por sí solo una "
                          "perturbación.";
        }

        std::optional<std::string> locator_loop;
        std::vector<std::string> protections;
        if (context.event_log) {
            for (const auto& row : *context.event_log) {
                if (!row.seconds_from_trigger ||
                    !std::isfinite(*row.seconds_from_trigger) ||
                    std::abs(*row.seconds_from_trigger +
                             context.log_clock_offset_s) > 5.0) {
                    continue;
                }
                if (!locator_loop && contains(row.signal, "fault loop") &&
                    !row.value.empty()) {
                    locator_loop = row.value;
                }
                if (!contains(row.signal, "pickup") ||
                    !activeValue(row.value)) {
                    continue;
                }
                const std::string signal = row.structure + " " + row.signal;
                std::string description;
                if (contains(signal, "85-67n")) {
                    description = "protección de tierra con dirección y "
                                  "comunicación (85-67N)";
                } else if (contains(signal, "67n")) {
                    description = "protección direccional de tierra (67N)";
                } else if (contains(signal, "27")) {
                    description = "protección de baja tensión (27)";
                } else if (contains(signal, "87")) {
                    description = "protección diferencial (87)";
                } else if (contains(signal, "59")) {
                    description = "protección de sobretensión (59)";
                } else if (contains(signal, "51")) {
                    description = "protección de sobrecorriente temporizada (51)";
                } else if (contains(signal, "50")) {
                    description = "protección de sobrecorriente instantánea (50)";
                } else {
                    description = "otra protección registrada";
                }
                if (std::find(protections.begin(), protections.end(),
                              description) == protections.end()) {
                    protections.push_back(std::move(description));
                }
            }
        }

        if (locator_loop) {
            observation += " El localizador del relé señaló una posible " +
                           loopDescription(*locator_loop) + ".";
        }
        if (result->classification) {
            const std::string& label = result->classification->label;
            if (contains(label, "sin incremento suficiente") ||
                contains(label, "prefalla posiblemente contaminada")) {
                observation += " Las mediciones no bastaron para que el "
                               "programa confirmara por separado el tipo de falla.";
            } else if (contains(label, "tierra")) {
                observation += " Las mediciones sugieren que la tierra pudo "
                               "estar involucrada; debe confirmarse.";
            } else if (contains(label, "desequilibrio") ||
                       contains(label, "secuencia negativa")) {
                observation += " Las mediciones entre fases fueron desiguales; "
                               "esto no basta por sí solo para precisar qué ocurrió.";
            } else {
                observation += " El análisis automático entrega una "
                               "orientación, no un diagnóstico definitivo.";
            }
        } else if (locator_loop) {
            observation += " El programa no pudo confirmar por separado el "
                           "tipo de falla.";
        }
        out += "<h3>Qué se observó</h3><p>" + htmlEscape(observation) + "</p>";

        if (!protections.empty()) {
            std::string text = "El relé registró detección de ";
            const std::size_t shown =
                std::min<std::size_t>(protections.size(), 3);
            for (std::size_t index = 0; index < shown; ++index) {
                if (index != 0) text += "; ";
                text += protections[index];
            }
            if (protections.size() > shown) text += "; entre otras";
            text += ". Esto no confirma por sí solo que el interruptor haya abierto.";
            out += "<h3>Qué registró el relé</h3><p>" + htmlEscape(text) + "</p>";
        }

        if (result->falla_phasors && result->phases.complete() &&
            context.record) {
            double minimum = std::numeric_limits<double>::infinity();
            double maximum = 0.0;
            std::string unit;
            bool consistent_units = true;
            bool label_mismatch = false;
            std::string label_unit;
            for (std::size_t phase = 0; phase < 3; ++phase) {
                const std::size_t index = *result->phases.index[phase];
                if (index >= context.record->cfg.analog.size()) {
                    consistent_units = false;
                    break;
                }
                const auto& channel = context.record->cfg.analog[index];
                const std::string normalized = analysis::normalizeUnit(channel.unit);
                if (phase == 0) unit = channel.unit;
                else if (normalized != analysis::normalizeUnit(unit))
                    consistent_units = false;
                const std::string named_unit = analysis::labelUnit(channel.name);
                if (!named_unit.empty() &&
                    analysis::normalizeUnit(named_unit) != normalized) {
                    label_mismatch = true;
                    if (label_unit.empty()) label_unit = named_unit;
                }
                const double magnitude =
                    (*result->falla_phasors)[phase].magnitude();
                if (!std::isfinite(magnitude)) {
                    consistent_units = false;
                    break;
                }
                minimum = std::min(minimum, magnitude);
                maximum = std::max(maximum, magnitude);
            }
            if (consistent_units && !unit.empty()) {
                std::string text = "Las corrientes calculadas están entre " +
                                   displayNumber(minimum, 3) + " y " +
                                   displayNumber(maximum, 3) + " " + unit + ".";
                if (label_mismatch) {
                    std::string unit_name = unit;
                    const std::string normalized_unit =
                        analysis::normalizeUnit(unit);
                    if (normalized_unit == "a") unit_name = "amperios (A)";
                    else if (normalized_unit == "ka")
                        unit_name = "kiloamperios (kA)";
                    else if (normalized_unit == "ma")
                        unit_name = "miliamperios (mA)";
                    text += " El archivo declara " + unit_name +
                            ", aunque algunos nombres de canal indican " +
                            label_unit + ". Confirme la escala antes de usar "
                            "estas cifras";
                    if (normalized_unit == "a" &&
                        analysis::normalizeUnit(label_unit) == "ka") {
                        text += " (1 kA equivale a 1000 A)";
                    }
                    text += ".";
                }
                review_items.push_back(std::move(text));
            }
        }

        if (result->fault_distance &&
            result->fault_distance->distance_km.has_value()) {
            const auto& distance = *result->fault_distance;
            std::string text = "El localizador estimó " +
                               displayNumber(*distance.distance_km, 3) + " km";
            if (distance.status == analysis::FaultDistanceStatus::NoReferenceLength) {
                text += ", pero no hay una longitud de línea confirmada para "
                        "comprobar esa estimación.";
            } else if (distance.status ==
                       analysis::FaultDistanceStatus::ExceedsReference) {
                text += ". La cifra supera la longitud de línea ingresada; "
                        "revise los datos de referencia.";
            } else if (distance.ratio) {
                text += " (" + displayNumber(*distance.ratio * 100.0, 3) +
                        "% de la longitud ingresada). Confirme los datos de "
                        "la línea antes de tomarlo como distancia real.";
            } else {
                text += ".";
            }
            review_items.push_back(std::move(text));
        }

        const auto& breaker = result->breaker;
        if (!breaker.configured) {
            review_items.emplace_back(
                "No se pudo saber cuánto tardó el interruptor en abrir porque "
                "faltan las señales que indican la orden y la apertura de sus contactos.");
        } else if (breaker.protection_ms || breaker.contact_opening_ms ||
                   breaker.clearing_ms) {
            std::string text = "Tiempos estimados del interruptor:";
            if (breaker.protection_ms)
                text += " desde el inicio hasta la orden " +
                        displayNumber(*breaker.protection_ms, 3) + " ms;";
            if (breaker.contact_opening_ms)
                text += " hasta la apertura de contactos " +
                        displayNumber(*breaker.contact_opening_ms, 3) + " ms;";
            if (breaker.clearing_ms)
                text += " hasta que dejó de circular corriente " +
                        displayNumber(*breaker.clearing_ms, 3) + " ms;";
            text.pop_back();
            text += ".";
            review_items.push_back(std::move(text));
        } else {
            review_items.emplace_back(
                "Las señales del interruptor están disponibles, pero la "
                "grabación no permitió calcular sus tiempos.");
        }
    }

    if (errors > 0 || warnings > 0) {
        review_items.push_back(
            "La revisión automática encontró " + std::to_string(errors) +
            " error(es) y " + std::to_string(warnings) +
            " aviso(s). Puede ver el detalle técnico más abajo.");
    }
    if (!review_items.empty()) {
        out += "<h3>Qué conviene confirmar</h3><ul class=\"plain\">";
        for (const auto& item : review_items) {
            out += "<li>" + htmlEscape(item) + "</li>";
        }
        out += "</ul>";
    }
    return out;
}


std::string renderTrendSvg(const TrendResult& trend);

std::string buildHtml(const Context& context, const Options& options) {
    const ComtradeRecord& record = *context.record;
    const CfgInfo& cfg = record.cfg;
    const auto& data = record.data;

                                                                               
                                                                                
                                               
    std::vector<Diagnostic> display_diagnostics;
    if (context.diagnostics) {
        display_diagnostics = *context.diagnostics;
    }
    if (context.analysis) {
        for (const auto& diagnostic : context.analysis->breaker.diagnostics) {
            if (!containsDiagnostic(display_diagnostics, diagnostic)) {
                display_diagnostics.push_back(diagnostic);
            }
        }
    }
    const std::vector<Diagnostic>* diagnostics_for_display =
        (context.diagnostics || !display_diagnostics.empty())
            ? &display_diagnostics
            : nullptr;

    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::size_t infos = 0;
    if (diagnostics_for_display) {
        for (const auto& diagnostic : *diagnostics_for_display) {
            if (diagnostic.severity == DiagnosticSeverity::Error) {
                ++errors;
            } else if (diagnostic.severity == DiagnosticSeverity::Warning) {
                ++warnings;
            } else {
                ++infos;
            }
        }
    }

    const std::string title = context.title.empty() ? "Registro COMTRADE"
                                                    : context.title;
    const std::string source = context.source.empty() ? "Archivo local"
                                                      : context.source;
    double duration = 0.0;
    if (data.times_s.size() > 1) {
        duration = data.times_s.back() - data.times_s.front();
    }

    std::string html;
    html.reserve(128 * 1024);
    html += "<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>";
    html += htmlEscape(title);
    html += " · Informe COMTRADE</title><style>";
    html += R"CSS(
:root{color-scheme:light;--ink:#18212f;--muted:#5d6b7d;--line:#d8e0e9;--paper:#fff;--wash:#f4f7fa;--blue:#0b5cad;--ok:#166534;--warn:#9a5b00;--bad:#a61b1b}
*{box-sizing:border-box}body{margin:0;background:var(--wash);color:var(--ink);font:15px/1.55 "Segoe UI",Tahoma,Arial,sans-serif}
main{max-width:1180px;margin:0 auto;padding:28px 22px 60px}.hero{background:linear-gradient(135deg,#0b2d52,#0b5cad);color:#fff;border-radius:14px;padding:26px 30px;margin-bottom:20px}
.hero h1{margin:0 0 6px;font-size:clamp(24px,3vw,36px);line-height:1.15}.hero p{margin:4px 0;color:#dbe8f8}.badge{display:inline-block;background:rgba(255,255,255,.16);border:1px solid rgba(255,255,255,.25);border-radius:999px;padding:3px 10px;font-size:12px;margin-top:10px}
section{background:var(--paper);border:1px solid var(--line);border-radius:12px;padding:20px 22px;margin:16px 0;box-shadow:0 1px 2px rgba(16,24,40,.03)}
h2{font-size:21px;margin:0 0 12px}h3{font-size:17px;margin:20px 0 8px}h4{font-size:15px;margin:14px 0 6px}
p{margin:8px 0}.muted,.note{color:var(--muted)}.note{font-size:13px}.ok{color:var(--ok);font-weight:600}
.facts{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:10px;margin:14px 0}.fact{background:var(--wash);border:1px solid var(--line);border-radius:9px;padding:10px 12px}.fact b{display:block;font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.04em}.fact span{font-size:17px;font-weight:600}
ul{padding-left:22px}li{margin:5px 0}.plain li{margin:8px 0}
.warnbox{border-left:4px solid var(--warn);background:#fff8e8}.errbox{border-left:4px solid var(--bad);background:#fff5f5}.goodbox{border-left:4px solid var(--ok);background:#f3fbf5}
.diagnostics{list-style:none;padding:0}.diag{border:1px solid var(--line);border-left-width:4px;border-radius:8px;padding:10px 12px;margin:8px 0}.diag.error{border-left-color:var(--bad)}.diag.warning{border-left-color:var(--warn)}.diag.info{border-left-color:var(--blue)}
code{background:#edf2f7;border-radius:4px;padding:1px 5px;font:12px/1.4 Consolas,monospace}
.table-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:9px}table{width:100%;border-collapse:collapse;font-size:13px;min-width:620px}th,td{text-align:left;padding:8px 9px;border-bottom:1px solid var(--line);vertical-align:top}thead th{background:var(--wash);font-size:12px;text-transform:uppercase;letter-spacing:.03em;color:var(--muted)}tbody tr:last-child td{border-bottom:0}tbody th{background:#fbfcfe;width:34%}.events-table{table-layout:fixed;min-width:900px}.events-table th:nth-child(1){width:13%}.events-table th:nth-child(2){width:17%}.events-table th:nth-child(3){width:12%}.events-table th:nth-child(4){width:21%}.events-table th:nth-child(5){width:22%}.events-table th:nth-child(6){width:15%}.events-table td{overflow-wrap:anywhere}
details{border:1px solid var(--line);border-radius:10px;padding:12px 15px;margin:12px 0;background:#fbfcfe}summary{cursor:pointer;font-weight:700;color:var(--blue)}
.technical-report{border:0;background:transparent;padding:0;box-shadow:none}.technical-report>summary{background:#fff;border:1px solid var(--line);border-radius:10px;padding:14px 18px}
.chart{margin:22px 0;border:1px solid var(--line);border-radius:10px;padding:12px;background:#fff}.chart figcaption{font-weight:700;margin:2px 0 10px}.chart svg{display:block;width:100%;height:auto}.plot-bg{fill:#fbfdff;stroke:#dbe4ee}.grid{stroke:#e5ebf2;stroke-width:1}.axis{font:11px "Segoe UI",Arial,sans-serif;fill:#5d6b7d}.axis-title{font:11px "Segoe UI",Arial,sans-serif;fill:#5d6b7d}.trace{fill:none;stroke-width:1.5;stroke-linejoin:round;stroke-linecap:round;vector-effect:non-scaling-stroke}.trigger{stroke:#1f2937;stroke-width:1;stroke-dasharray:5 5}.trigger-label{font:11px "Segoe UI",Arial,sans-serif;fill:#1f2937}
.legend{display:flex;flex-wrap:wrap;gap:8px 18px;margin-top:9px;font-size:12px}.legend i{display:inline-block;width:20px;height:3px;border-radius:2px;margin-right:6px;vertical-align:middle}.legend small{color:var(--muted)}
footer{color:var(--muted);font-size:12px;text-align:center;margin-top:26px}@media print{body{background:#fff}section,.chart{break-inside:avoid;box-shadow:none}.hero{border-radius:0}details{open:true}}
@media (max-width:700px){main{padding:12px}.hero{padding:20px}section{padding:16px}.facts{grid-template-columns:1fr}}
)CSS";
    html += "</style></head><body><main>";
    html += "<header class=\"hero\"><p>Analizador COMTRADE nativo</p><h1>";
    html += htmlEscape(title);
    html += "</h1><p>Informe del evento eléctrico</p><p><strong>Fuente:</strong> ";
    html += htmlEscape(source);
    html += "</p><span class=\"badge\">Resumen automático del registro</span></header>";

    html += "<section><h2>Resumen del evento</h2>";
    html += "<div class=\"facts\">";
    html += "<div class=\"fact\"><b>Estación</b><span>";
    html += htmlEscape(cfg.station_name.empty() ? "No informada"
                                                : cfg.station_name);
    html += "</span></div>";
    if (!cfg.device_id.empty()) {
        html += "<div class=\"fact\"><b>Equipo</b><span>";
        html += htmlEscape(cfg.device_id);
        html += "</span></div>";
    }
    if (cfg.start_time.valid) {
        html += "<div class=\"fact\"><b>Inicio de la grabación</b><span>";
        html += htmlEscape(dateTimeText(cfg.start_time));
        html += "</span></div>";
    }
    html += "<div class=\"fact\"><b>Duración</b><span>";
    html += htmlEscape(displayNumber(duration, 8));
    html += " s</span></div></div>";
    html += managementSummaryHtml(context, errors, warnings);
    html += "<p class=\"note\">Este resumen orienta la lectura. Antes de "
            "tomar decisiones, confirme los puntos indicados y revise el "
            "detalle técnico si lo necesita.</p></section>";

    html += "<details class=\"technical-report\"><summary>Ver el detalle "
            "técnico, las señales y las gráficas</summary>";

    if (errors > 0 || warnings > 0) {
        html += "<section class=\"";
        html += errors > 0 ? "errbox" : "warnbox";
        html += "\"><h2>Advertencias y errores visibles</h2><p>Errores: ";
        html += formatCount(errors);
        html += " · Advertencias: ";
        html += formatCount(warnings);
        html += " · Información: ";
        html += formatCount(infos);
        html += "</p>";
        html += renderDiagnostics(diagnostics_for_display);
        html += "</section>";
    } else {
        html += "<section class=\"goodbox\"><h2>Estado de lectura</h2>"
                "<p class=\"ok\">Sin errores ni advertencias de lectura/análisis.</p>"
                "</section>";
    }

    if (context.analysis) {
        html += "<section><h2>Lectura del análisis</h2>";
        html += analysisSummaryHtml(*context.analysis);
        html += renderAnalysisDetails(*context.analysis, cfg);
        html += "</section>";
    }

    if (context.unit_overrides && !context.unit_overrides->empty()) {
        html += "<section><h2>Unidades informadas manualmente</h2>"
                "<p class=\"note\">Estas etiquetas solo cambian la presentación. "
                "Los valores numéricos conservan exactamente la escala y unidad "
                "calculadas desde el CFG; no se aplicó ninguna conversión.</p>"
                "<div class=\"table-wrap\"><table><thead><tr><th>Canal</th>"
                "<th>Unidad CFG</th><th>Unidad informada</th></tr></thead><tbody>";
        for (const auto& item : *context.unit_overrides) {
            const std::string name =
                item.first < cfg.analog.size()
                    ? (cfg.analog[item.first].name.empty()
                           ? "Canal " + formatCount(item.first + 1)
                           : cfg.analog[item.first].name)
                    : "Canal " + formatCount(item.first + 1);
            html += "<tr><td>" + htmlEscape(name) + "</td><td>" +
                    htmlEscape(item.first < cfg.analog.size()
                                   ? normalizedUnit(cfg.analog[item.first].unit)
                                   : std::string("(sin unidad)")) +
                    "</td><td>" + htmlEscape(item.second) + "</td></tr>";
        }
        html += "</tbody></table></div></section>";
    }

    if (context.event_log || context.timeline) {
        html += "<section><h2>Eventos de bitácora y línea de tiempo</h2>";
        html += "<p class=\"note\">La alineación usa la fecha/hora registrada y "
                "resta el inicio/trigger del CFG. No se corrige zona horaria ni "
                "deriva de reloj salvo la corrección manual indicada. La "
                "coincidencia temporal no demuestra causalidad.</p>";
        if (context.log_clock_offset_s != 0.0) {
            html += "<p class=\"note\">Corrección manual de reloj aplicada solo "
                    "a la bitácora: ";
            html += htmlEscape(displayNumber(context.log_clock_offset_s, 8));
            html += " s.</p>";
        }
        if (context.event_log && !context.event_log->empty()) {
            std::vector<const EventLogRow*> nearby_events;
            for (const auto& row : *context.event_log) {
                if (!row.seconds_from_trigger.has_value()) {
                    continue;
                }
                const double relative = *row.seconds_from_trigger +
                                        context.log_clock_offset_s;
                if (std::isfinite(relative) && relative >= -5.0 &&
                    relative <= 5.0) {
                    nearby_events.push_back(&row);
                }
            }
            std::stable_sort(
                nearby_events.begin(), nearby_events.end(),
                [&](const EventLogRow* left, const EventLogRow* right) {
                    return *left->seconds_from_trigger +
                               context.log_clock_offset_s <
                           *right->seconds_from_trigger +
                               context.log_clock_offset_s;
                });
            html += "<h3>Eventos de bitácora cerca del trigger</h3><p class=\"note\">";
            html += formatCount(nearby_events.size());
            html += " de ";
            html += formatCount(context.event_log->size());
            html += " evento(s), ordenados por tiempo y limitados a ±5 s del "
                    "trigger validado.</p>";
            if (!nearby_events.empty()) {
                html += "<div class=\"table-wrap\"><table class=\"events-table\">"
                        "<thead><tr><th>Fuente</th><th>Fecha/hora</th>"
                        "<th>s desde trigger</th><th>Estructura</th><th>Señal</th>"
                        "<th>Valor</th></tr></thead><tbody>";
            }
            for (const EventLogRow* event : nearby_events) {
                const EventLogRow& row = *event;
                const double relative = *row.seconds_from_trigger +
                                        context.log_clock_offset_s;
                html += "<tr><td>" + htmlEscape(row.source) + "</td><td>" +
                        htmlEscape(row.when_text) + "</td><td>" +
                        htmlEscape(displayNumber(relative, 6)) +
                        "</td><td>" + htmlEscape(row.structure) + "</td><td>" +
                        htmlEscape(row.signal) + "</td><td>" +
                        htmlEscape(row.value) + "</td></tr>";
            }
            if (!nearby_events.empty()) {
                html += "</tbody></table></div>";
            } else {
                html += "<p class=\"note\">No hay eventos con fecha/hora "
                        "interpretable dentro de ±5 s del trigger. La bitácora "
                        "completa se conserva en los datos exportados.</p>";
            }
        } else {
            html += "<p class=\"note\">No se cargaron bitácoras CSV con eventos "
                    "interpretables.</p>";
        }
        if (context.timeline && !context.timeline->rows.empty()) {
            html += "<h3>Línea de tiempo unificada</h3><div class=\"table-wrap\">"
                    "<table><thead><tr><th>s desde referencia</th><th>Fuente</th>"
                    "<th>Evento</th><th>Detalle</th></tr></thead><tbody>";
            for (const auto& row : context.timeline->rows) {
                html += "<tr><td>" +
                        htmlEscape(displayNumber(row.seconds_from_reference, 9)) +
                        "</td><td>" + htmlEscape(row.source) + "</td><td>" +
                        htmlEscape(row.event) + "</td><td>" +
                        htmlEscape(row.detail) + "</td></tr>";
            }
            html += "</tbody></table></div>";
            if (context.timeline->excluded_log_events != 0) {
                html += "<p class=\"warnbox\">" +
                        formatCount(context.timeline->excluded_log_events) +
                        " evento(s) de bitácora quedaron fuera de la captura y "
                        "no se incluyeron.</p>";
            }
        } else {
            html += "<p class=\"note\">No hay eventos alineados para mostrar.</p>";
        }
        html += "</section>";
    }

    if (context.trend && !context.trend->points.empty()) {
        html += "<section><h2>Tendencia fasorial móvil</h2>";
        html += "<p class=\"note\">Ventana móvil de ";
        html += formatCount(static_cast<std::size_t>(context.trend->cycles));
        html += " ciclo(s) sobre timestamps DAT reales. Es una tendencia "
                "exploratoria; no clasifica protecciones. Unidad: ";
        html += htmlEscape(normalizedUnit(context.trend->unit));
        html += ".</p>";
        html += renderTrendSvg(*context.trend);
        html += "</section>";
    }

    if (context.comparison &&
        (!context.comparison->rows.empty() ||
         !context.comparison->warnings.empty())) {
        html += "<section><h2>Comparación de fasores entre registros</h2>";
        html += "<p class=\"note\">Mismas ventanas relativas al trigger de cada "
                "registro. Las unidades CFG, frecuencias y bases P/S se "
                "conservan; no se convierten automáticamente.</p>";
        if (!context.comparison->rows.empty()) {
            html += "<div class=\"table-wrap\"><table><thead><tr><th>Registro</th>"
                    "<th>Cantidad</th><th>Ventana</th><th>Fase</th><th>Hz</th>"
                    "<th>Magnitud RMS</th><th>Ángulo (°)</th><th>Unidad</th>"
                    "</tr></thead><tbody>";
            for (const auto& row : context.comparison->rows) {
                html += "<tr><td>" + htmlEscape(row.record) + "</td><td>" +
                        htmlEscape(row.quantity.empty()
                                       ? std::string("Corriente")
                                       : row.quantity) +
                        "</td><td>" + htmlEscape(row.window) + "</td><td>" +
                        htmlEscape(row.phase) + "</td><td>" +
                        htmlEscape(displayNumber(row.frequency_hz, 6)) +
                        "</td><td>" +
                        (row.finite ? htmlEscape(displayNumber(row.magnitude, 8))
                                    : "—") +
                        "</td><td>" +
                        (row.finite ? htmlEscape(displayNumber(row.angle_deg, 5))
                                    : "—") +
                        "</td><td>" + htmlEscape(row.unit) + "</td></tr>";
            }
            html += "</tbody></table></div>";
        } else {
            html += "<p class=\"note\">La comparación no produjo filas "
                    "utilizables.</p>";
        }
        if (!context.comparison->warnings.empty()) {
            html += "<h3>Advertencias de comparación</h3><ul>";
            for (const auto& warning : context.comparison->warnings) {
                html += "<li>" + htmlEscape(warning) + "</li>";
            }
            html += "</ul>";
        }
        html += "</section>";
    }

    html += "<section><h2>Trazas del registro</h2>";
    html += renderSvgCharts(record, options, context.analysis);
    html += "</section>";

    html += "<details><summary>Detalle técnico desplegable</summary>";
    html += "<h3>Identificación y tiempos</h3><div class=\"table-wrap\">"
            "<table><tbody>";
    appendTableRow(html, "Estación", cfg.station_name);
    appendTableRow(html, "Equipo / dispositivo", cfg.device_id);
    appendTableRow(html, "Revisión CFG", cfg.revision);
    appendTableRow(html, "Año de formato reconocido",
                   cfg.format_year == 0 ? "No determinado"
                                        : std::to_string(cfg.format_year));
    appendTableRow(html, "Inicio CFG (texto)", cfg.start_time_text);
    appendTableRow(html, "Inicio CFG (interpretado)",
                   dateTimeText(cfg.start_time));
    appendTableRow(html, "Trigger CFG (texto)", cfg.trigger_time_text);
    appendTableRow(html, "Trigger CFG (interpretado)",
                   dateTimeText(cfg.trigger_time));
    const bool trigger_inside_dat =
        record.trigger_offset_s && !data.times_s.empty() &&
        *record.trigger_offset_s >= data.times_s.front() &&
        *record.trigger_offset_s <= data.times_s.back();
    appendTableRow(
        html, "Trigger dentro del DAT",
        trigger_inside_dat
            ? "Sí; " + displayNumber(*record.trigger_offset_s, 12) +
                  " s desde el inicio"
            : "No disponible o fuera de los timestamps DAT");
    appendTableRow(html, "Primer timestamp DAT (s)",
                   data.times_s.empty()
                       ? "No disponible"
                       : displayNumber(data.times_s.front(), 12));
    appendTableRow(html, "Último timestamp DAT (s)",
                   data.times_s.empty()
                       ? "No disponible"
                       : displayNumber(data.times_s.back(), 12));
    appendTableRow(html, "Primera muestra no cero",
                   record.first_sample_nonzero ? "Sí; se conserva" : "No");
    appendTableRow(html, "Tipo de archivo DAT",
                   dataFileTypeName(cfg.data_type));
    appendTableRow(html, "Codificación CFG",
                   textEncodingName(cfg.encoding));
    appendTableRow(html, "Multiplicador temporal",
                   displayNumber(cfg.time_multiplier, 12));
    appendTableRow(html, "Canales declarados en CFG",
                   formatCount(cfg.declared_total_channels));
    html += "</tbody></table></div>";

    html += "<h3>Tasas de muestreo declaradas</h3>";
    appendSamplingTable(html, cfg);
    html += "<h3>Canales analógicos</h3>";
    appendAnalogTable(html, cfg);
    html += "<h3>Canales digitales</h3>";
    appendDigitalTable(html, cfg);
    html += "<h3>Diagnósticos completos de lectura y análisis</h3>";
    html += renderDiagnostics(diagnostics_for_display);
    html += "</details>";

    html += "<section><h2>Limitaciones y trazabilidad</h2>";
    html += "<ul><li>Los valores numéricos se presentan en la unidad del CFG. "
            "Las etiquetas manuales no forman parte de este informe y no se "
            "aplican conversiones por nombre de canal.</li>";
    html += "<li>La comparación entre equipos exige bases de medición "
            "compatibles; una diferencia de prefijo (por ejemplo A frente a kA) "
            "no se corrige aquí.</li>";
    html += "<li>El informe es autocontenido: no carga CSS, JavaScript, "
            "tipografías ni imágenes desde Internet.</li>";
    html += "<li>Las trazas SVG pueden estar decimadas por tamaño. El archivo "
            "CSV conserva todas las muestras exportadas.</li>";
    html += "<li>Fuente declarada: ";
    html += htmlEscape(source);
    html += ".</li></ul></section>";
    html += "</details>";
    html += "<footer>Generado por Analizador COMTRADE nativo · aplicación "
            "offline · informe sin recursos remotos</footer>";
    html += "</main></body></html>";
    return html;
}

                                                                              
      
                                                                              

void appendCsvField(std::string& out, std::string_view value) {
    out.push_back('"');
    for (const char c : value) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

std::string csvNumber(double value) {
    const std::string text = formatFull(value);
    return text;
}

                                                                              
       
                                                                              

void appendDateTimeJson(std::string& out, const DateTime& value,
                        std::string_view raw_text) {
    out += "{";
    appendJsonKey(out, "valid");
    out += value.valid ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "text");
    appendJsonString(out, raw_text);
    out += ", ";
    appendJsonKey(out, "interpreted");
    if (value.valid) {
        appendJsonString(out, dateTimeText(value));
    } else {
        out += "null";
    }
    if (value.valid) {
        out += ", ";
        appendJsonKey(out, "year");
        out += std::to_string(value.year);
        out += ", ";
        appendJsonKey(out, "month");
        out += std::to_string(value.month);
        out += ", ";
        appendJsonKey(out, "day");
        out += std::to_string(value.day);
        out += ", ";
        appendJsonKey(out, "hour");
        out += std::to_string(value.hour);
        out += ", ";
        appendJsonKey(out, "minute");
        out += std::to_string(value.minute);
        out += ", ";
        appendJsonKey(out, "second");
        out += std::to_string(value.second);
        out += ", ";
        appendJsonKey(out, "microsecond");
        out += std::to_string(value.microsecond);
    }
    out += "}";
}

void appendDiagnosticsJson(std::string& out,
                           const std::vector<Diagnostic>* diagnostics) {
    out += "[";
    if (diagnostics) {
        bool first = true;
        for (const auto& diagnostic : *diagnostics) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += "{";
            appendJsonKey(out, "severity");
            appendJsonString(out, severityLabel(diagnostic.severity));
            out += ", ";
            appendJsonKey(out, "code");
            appendJsonString(out, diagnostic.code);
            out += ", ";
            appendJsonKey(out, "message");
            appendJsonString(out, diagnostic.message);
            out += ", ";
            appendJsonKey(out, "context");
            appendJsonString(out, diagnostic.context);
            out += ", ";
            appendJsonKey(out, "line");
            out += diagnostic.has_line ? std::to_string(diagnostic.line)
                                       : "null";
            out += ", ";
            appendJsonKey(out, "record_index");
            out += diagnostic.has_record ? std::to_string(diagnostic.record)
                                         : "null";
            out += ", ";
            appendJsonKey(out, "sample_number");
            out += diagnostic.has_sample_number
                       ? std::to_string(diagnostic.sample_number)
                       : "null";
            out += "}";
        }
    }
    out += "]";
}


void appendJsonOptionalNumber(std::string& out,
                              const std::optional<double>& value) {
    out += value ? jsonNumber(*value) : "null";
}

void appendPhasorArrayJson(
    std::string& out,
    const std::optional<std::array<analysis::Phasor, 3>>& values,
    const CfgInfo& cfg,
    const analysis::PhaseSelection& phases) {
    if (!values) {
        out += "null";
        return;
    }
    out += "[";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) {
            out += ", ";
        }
        const auto& phasor = (*values)[slot];
        out += "{";
        appendJsonKey(out, "phase");
        appendJsonString(out, phaseName(slot));
        out += ", ";
        appendJsonKey(out, "unit");
        appendJsonString(out, phaseChannelUnit(cfg, phases, slot));
        out += ", ";
        appendJsonKey(out, "finite");
        out += phasor.isFinite() ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "magnitude");
        out += phasor.isFinite() ? jsonNumber(phasor.magnitude()) : "null";
        out += ", ";
        appendJsonKey(out, "angle_deg");
        out += phasor.isFinite() ? jsonNumber(phasor.angleDegrees()) : "null";
        out += "}";
    }
    out += "]";
}

void appendSequenceJson(std::string& out,
                        const std::optional<analysis::SequenceSet>& sequence,
                        const char* prefix = "i") {
    if (!sequence) {
        out += "null";
        return;
    }
    const std::string zero_key = std::string(prefix) + "0";
    const std::string positive_key = std::string(prefix) + "1";
    const std::string negative_key = std::string(prefix) + "2";
    const auto append_one = [&](const char* key,
                                const std::complex<double>& value) {
        appendJsonKey(out, key);
        out += "{";
        appendJsonKey(out, "magnitude");
        out += std::isfinite(std::abs(value)) ? jsonNumber(std::abs(value))
                                              : "null";
        out += ", ";
        appendJsonKey(out, "angle_deg");
        out += std::isfinite(std::arg(value))
                   ? jsonNumber(std::arg(value) * 180.0 / analysis::kPi)
                   : "null";
        out += "}";
    };
    out += "{";
    append_one(zero_key.c_str(), sequence->zero);
    out += ", ";
    append_one(positive_key.c_str(), sequence->positive);
    out += ", ";
    append_one(negative_key.c_str(), sequence->negative);
    out += "}";
}

std::string breakerAssignmentText(const CfgInfo* cfg,
    const analysis::DigitalSignalAssignment& assignment) {
    if (!assignment.channel) return "Sin asignar";
    const auto index=*assignment.channel;
    const std::string name=cfg && index<cfg->digital.size() ? cfg->digital[index].name : "Canal no disponible";
    return name+" [índice 0-based "+std::to_string(index)+"] · "+
        (assignment.active_high ? "activo alto (1)" : "activo bajo (0)");
}

void appendBreakerJson(std::string& out,
                       const analysis::BreakerTimingResult& breaker, const CfgInfo& cfg) {
    out += "{";
    appendJsonKey(out, "configured");
    out += breaker.configured ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "onset_s");
    appendJsonOptionalNumber(out, breaker.onset_s);
    out += ", ";
    appendJsonKey(out, "trip_s");
    appendJsonOptionalNumber(out, breaker.trip_s);
    out += ", ";
    appendJsonKey(out, "contact_open_s");
    appendJsonOptionalNumber(out, breaker.contact_open_s);
    out += ", ";
    appendJsonKey(out, "current_extinction_s");
    appendJsonOptionalNumber(out, breaker.current_extinction_s);
    out += ", ";
    appendJsonKey(out, "protection_ms");
    appendJsonOptionalNumber(out, breaker.protection_ms);
    out += ", ";
    appendJsonKey(out, "contact_opening_ms");
    appendJsonOptionalNumber(out, breaker.contact_opening_ms);
    out += ", ";
    appendJsonKey(out, "clearing_ms");
    appendJsonOptionalNumber(out, breaker.clearing_ms);
    out += ", ";
    appendJsonKey(out, "digital_resolution_ms");
    out += breaker.configured &&
                   std::isfinite(breaker.digital_resolution_ms) &&
                   breaker.digital_resolution_ms > 0.0
               ? jsonNumber(breaker.digital_resolution_ms)
               : "null";
    out += ", ";
    appendJsonKey(out, "extinction_resolution_ms");
    out += breaker.configured &&
                   std::isfinite(breaker.extinction_resolution_ms) &&
                   breaker.extinction_resolution_ms > 0.0
               ? jsonNumber(breaker.extinction_resolution_ms)
               : "null";
    out += ", ";
    appendJsonKey(out, "onset_resolution_ms");
    appendJsonOptionalNumber(out, breaker.onset_resolution_ms>0 ?
        std::optional<double>(breaker.onset_resolution_ms) : std::nullopt);
    out += ", ";
    appendJsonKey(out, "applied_options");
    out += "{";
    struct Item { const char* key; const analysis::DigitalSignalAssignment* assignment; };
    const auto& options=breaker.applied_options;
    const Item items[]={{"trip",&options.trip},{"52a",&options.contact_52a},{"52b",&options.contact_52b}};
    bool first=true;
    for(const auto& item:items) {
        if(!first) out+=", "; first=false;
        appendJsonKey(out,item.key); out+="{";
        appendJsonKey(out,"channel_index_0based");
        out+=item.assignment->channel ? std::to_string(*item.assignment->channel) : "null";
        out+=", "; appendJsonKey(out,"active_high"); out+=item.assignment->active_high ? "true" : "false";
        out+=", "; appendJsonKey(out,"description");
        appendJsonString(out,breakerAssignmentText(&cfg,*item.assignment)); out+="}";
    }
    out+=", "; appendJsonKey(out,"debounce_ms"); out+=jsonNumber(options.debounce_ms);
    out+=", "; appendJsonKey(out,"extinction_fraction_per_phase"); out+=jsonNumber(options.extinction_fraction);
    out+=", "; appendJsonKey(out,"extinction_cycles"); out+=std::to_string(options.extinction_cycles);
    out+="}, ";
    appendJsonKey(out, "diagnostics");
    appendDiagnosticsJson(out, &breaker.diagnostics);
    out += "}";
}

void appendAnalysisJson(std::string& out,
                        const analysis::RecordAnalysis* record_analysis,
                        const CfgInfo& cfg) {
    if (!record_analysis) {
        out += "null";
        return;
    }
    out += "{";
    appendJsonKey(out, "ok");
    out += record_analysis->ok ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "cycles");
    out += std::to_string(record_analysis->cycles);
    out += ", ";
    appendJsonKey(out, "frequency_hz");
    out += jsonNumber(record_analysis->frequency_hz);
    out += ", ";
    appendJsonKey(out, "origin");
    appendJsonString(out, analysis::waveformOriginName(record_analysis->origin));
    out += ", ";
    appendJsonKey(out, "origin_label");
    appendJsonString(out, record_analysis->origin_label);
    out += ", ";
    appendJsonKey(out, "event_origin_s");
    out += jsonNumber(record_analysis->event_origin_s);

    out += ", ";
    appendJsonKey(out, "time");
    out += "{";
    appendJsonKey(out, "ok");
    out += record_analysis->time.ok ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "first_s");
    out += jsonNumber(record_analysis->time.first_s);
    out += ", ";
    appendJsonKey(out, "last_s");
    out += jsonNumber(record_analysis->time.last_s);
    out += ", ";
    appendJsonKey(out, "median_dt_s");
    out += jsonNumber(record_analysis->time.median_dt_s);
    out += ", ";
    appendJsonKey(out, "median_rate_hz");
    out += jsonNumber(record_analysis->time.median_rate_hz);
    out += ", ";
    appendJsonKey(out, "first_sample_nonzero");
    out += record_analysis->time.first_sample_nonzero ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "trigger_offset_known");
    out += record_analysis->time.trigger_offset_known ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "trigger_offset_cfg_s");
    out += jsonNumber(record_analysis->time.trigger_offset_cfg_s);
    out += ", ";
    appendJsonKey(out, "trigger_within_capture");
    out += record_analysis->time.trigger_within_capture ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "trigger_reference_is_midpoint");
    out += record_analysis->time.trigger_reference_is_midpoint ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "reference_s");
    out += jsonNumber(record_analysis->time.reference_s);
    out += ", ";
    appendJsonKey(out, "reference_label");
    appendJsonString(out, record_analysis->time.reference_label);
    out += "}";

    out += ", ";
    appendJsonKey(out, "phases");
    out += "{";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) {
            out += ", ";
        }
        appendJsonString(out, phaseName(slot));
        out += ": {";
        appendJsonKey(out, "channel_index");
        out += record_analysis->phases.index[slot].has_value()
                   ? std::to_string(*record_analysis->phases.index[slot])
                   : "null";
        out += ", ";
        appendJsonKey(out, "name");
        appendJsonString(out, phaseChannelName(cfg, record_analysis->phases, slot));
        out += ", ";
        appendJsonKey(out, "unit");
        appendJsonString(out, phaseChannelUnit(cfg, record_analysis->phases, slot));
        out += "}";
    }
    out += "}";

    out += ", ";
    appendJsonKey(out, "event");
    if (record_analysis->event) {
        const auto& event = *record_analysis->event;
        out += "{";
        appendJsonKey(out, "time_s");
        out += jsonNumber(event.time_s);
        out += ", ";
        appendJsonKey(out, "direction");
        appendJsonString(out, analysis::changeDirectionName(event.direction));
        out += ", ";
        appendJsonKey(out, "direction_text");
        appendJsonString(out, event.direction_text);
        out += ", ";
        appendJsonKey(out, "recovery_detected");
        out += event.recovery_detected ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "recovery_s");
        appendJsonOptionalNumber(out, event.recovery_s);
        out += ", ";
        appendJsonKey(out, "duration_s");
        out += jsonNumber(event.duration_s);
        out += ", ";
        appendJsonKey(out, "duration_is_lower_bound");
        out += event.duration_is_lower_bound ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "threshold_delta");
        out += jsonNumber(event.threshold_delta);
        out += ", ";
        appendJsonKey(out, "baseline_rms");
        out += jsonNumber(event.baseline_rms);
        out += ", ";
        appendJsonKey(out, "rms");
        out += jsonNumber(event.rms);
        out += ", ";
        appendJsonKey(out, "delta");
        out += jsonNumber(event.delta);
        out += ", ";
        appendJsonKey(out, "contrast");
        out += jsonNumber(event.contrast);
        out += ", ";
        appendJsonKey(out, "peak_envelope");
        out += jsonNumber(event.peak_envelope);
        out += ", ";
        appendJsonKey(out, "peak_contrast");
        appendJsonOptionalNumber(out, event.peak_contrast);
        out += ", ";
        appendJsonKey(out, "window_samples");
        out += std::to_string(event.window_samples);
        out += ", ";
        appendJsonKey(out, "baseline_source");
        appendJsonString(out, event.baseline_source);
        out += "}";
    } else {
        out += "null";
    }

    out += ", ";
    appendJsonKey(out, "regime_changes");
    out += "[";
    bool first_change = true;
    for (const auto& change : record_analysis->regime_changes) {
        if (!first_change) out += ", ";
        first_change = false;
        out += "{\"time_s\":" + jsonNumber(change.time_s) + ",\"phase_direction\":[";
        for (std::size_t phase = 0; phase < 3; ++phase) {
            if (phase) out += ",";
            out += std::to_string(change.phase_direction[phase]);
        }
        out += "],\"voltage_change_nearby\":";
        out += change.voltage_change_nearby ? "true" : "false";
        out += ",\"digital_transition_nearby\":";
        out += change.digital_transition_nearby ? "true" : "false";
        out += "}";
    }
    out += "], ";
    appendJsonKey(out, "window");
    out += "{";
    appendJsonKey(out, "prefalla_center_s");
    out += jsonNumber(record_analysis->prefalla_center_s);
    out += ", ";
    appendJsonKey(out, "falla_center_s");
    out += jsonNumber(record_analysis->falla_center_s);
    out += ", ";
    appendJsonKey(out, "half_s");
    out += jsonNumber(record_analysis->window_half_s);
    out += ", ";
    appendJsonKey(out, "harmonic_start_s");
    out += jsonNumber(record_analysis->falla_center_s - record_analysis->window_half_s);
    out += ", ";
    appendJsonKey(out, "harmonic_end_s");
    out += jsonNumber(record_analysis->falla_center_s + record_analysis->window_half_s);
    out += ", ";
    appendJsonKey(out, "prefalla_usable");
    out += record_analysis->prefalla_usable ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "prefalla_manual");
    out += record_analysis->prefalla_manual ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "prefalla_status");
    appendJsonString(out, record_analysis->prefalla_status);
    out += ", ";
    appendJsonKey(out, "prefalla_clamped");
    out += record_analysis->prefalla_clamped ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "falla_clamped");
    out += record_analysis->falla_clamped ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "overlap");
    out += record_analysis->windows_overlap ? "true" : "false";
    out += "}";

    out += ", ";
    appendJsonKey(out, "prefalla_phasors");
    appendPhasorArrayJson(out, record_analysis->prefalla_phasors, cfg,
                          record_analysis->phases);
    out += ", ";
    appendJsonKey(out, "falla_phasors");
    appendPhasorArrayJson(out, record_analysis->falla_phasors, cfg,
                          record_analysis->phases);
    out += ", ";
    appendJsonKey(out, "prefalla_sequences");
    appendSequenceJson(out, record_analysis->prefalla_sequences);
    out += ", ";
    appendJsonKey(out, "falla_sequences");
    appendSequenceJson(out, record_analysis->falla_sequences);

    out += ", ";
    appendJsonKey(out, "ratios");
    out += "{";
    appendJsonKey(out, "i0_i1");
    appendJsonOptionalNumber(out, record_analysis->ratios.i0_i1);
    out += ", ";
    appendJsonKey(out, "i2_i1");
    appendJsonOptionalNumber(out, record_analysis->ratios.i2_i1);
    out += ", ";
    appendJsonKey(out, "i2_i1_pct");
    if (record_analysis->ratios.i2_i1.has_value() &&
        std::isfinite(*record_analysis->ratios.i2_i1)) {
        out += jsonNumber(*record_analysis->ratios.i2_i1 * 100.0);
    } else {
        out += "null";
    }
    out += ", ";
    appendJsonKey(out, "i0_i1_pct");
    if (record_analysis->ratios.i0_i1.has_value() &&
        std::isfinite(*record_analysis->ratios.i0_i1)) {
        out += jsonNumber(*record_analysis->ratios.i0_i1 * 100.0);
    } else {
        out += "null";
    }
    out += ", ";
    appendJsonKey(out, "i1_fault_pre");
    appendJsonOptionalNumber(out, record_analysis->ratios.i1_fault_pre);
    out += "}";

    out += ", ";
    appendJsonKey(out, "classification");
    if (record_analysis->classification) {
        const auto& classification = *record_analysis->classification;
        out += "{";
        appendJsonKey(out, "label");
        appendJsonString(out, classification.label);
        out += ", ";
        appendJsonKey(out, "explanation");
        appendJsonString(out, classification.explanation);
        out += ", ";
        appendJsonKey(out, "i0_i1");
        out += jsonNumber(classification.i0_i1);
        out += ", ";
        appendJsonKey(out, "i2_i1");
        out += jsonNumber(classification.i2_i1);
        out += ", ";
        appendJsonKey(out, "i1_fault_pre");
        out += jsonNumber(classification.i1_fault_pre);
        out += "}";
    } else {
        out += "null";
    }

    out += ", ";
    appendJsonKey(out, "harmonics");
    out += "[";
    bool first_harmonic = true;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& fit = record_analysis->harmonics[slot];
        if (!first_harmonic) {
            out += ", ";
        }
        first_harmonic = false;
        out += "{";
        appendJsonKey(out, "phase");
        appendJsonString(out, phaseName(slot));
        out += ", ";
        appendJsonKey(out, "unit");
        appendJsonString(out, phaseChannelUnit(cfg, record_analysis->phases,
                                               slot));
        out += ", ";
        appendJsonKey(out, "ok");
        out += fit.ok ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "supported_order");
        out += std::to_string(fit.supported_order);
        out += ", ";
        appendJsonKey(out, "nyquist_hz");
        out += jsonNumber(fit.nyquist_hz);
        out += ", ";
        appendJsonKey(out, "thd_pct");
        appendJsonOptionalNumber(out, fit.thd_pct);
        out += ", ";
        appendJsonKey(out, "h2_h1_pct");
        appendJsonOptionalNumber(out, fit.h2_h1_pct);
        out += ", ";
        appendJsonKey(out, "values");
        out += "[";
        for (std::size_t order = 0; order < fit.harmonics_rms.size(); ++order) {
            if (order != 0) {
                out += ", ";
            }
            out += "{\"order\":";
            out += std::to_string(order + 1);
            out += ", \"rms\":";
            out += jsonNumber(fit.harmonics_rms[order]);
            out += "}";
        }
        out += "]}";
    }
    out += "]";

    out += ", ";
    appendJsonKey(out, "coherence");
    out += "[";
    for (std::size_t index = 0; index < record_analysis->coherence_rows.size();
         ++index) {
        if (index != 0) {
            out += ", ";
        }
        const auto& row = record_analysis->coherence_rows[index];
        out += "{";
        appendJsonKey(out, "window");
        appendJsonString(out, row.window_label);
        out += ", ";
        appendJsonKey(out, "channel");
        appendJsonString(out, row.channel_name);
        out += ", ";
        appendJsonKey(out, "ac_rms");
        out += jsonNumber(row.ac_rms);
        out += ", ";
        appendJsonKey(out, "fundamental_rms");
        out += jsonNumber(row.fundamental_rms);
        out += ", ";
        appendJsonKey(out, "ratio");
        out += jsonNumber(row.ratio);
        out += "}";
    }
    out += "]";

    out += ", ";
    appendJsonKey(out, "voltage");
    out += "{";
    appendJsonKey(out, "available");
    out += record_analysis->voltage.available ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "unit");
    appendJsonString(out, record_analysis->voltage.unit);
    out += ", ";
    appendJsonKey(out, "units_consistent");
    out += record_analysis->voltage.units_consistent ? "true" : "false";
    out += ", ";
    appendJsonKey(out, "selection");
    out += "[";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) {
            out += ", ";
        }
        const auto& index = record_analysis->voltage.selection.index[slot];
        out += "{";
        appendJsonKey(out, "phase");
        appendJsonString(out, phaseName(slot));
        out += ", ";
        appendJsonKey(out, "channel_index");
        out += index.has_value() ? std::to_string(*index) : "null";
        out += ", ";
        appendJsonKey(out, "name");
        appendJsonString(out, phaseChannelName(
                                 cfg, record_analysis->voltage.selection, slot));
        out += ", ";
        appendJsonKey(out, "unit");
        appendJsonString(out, phaseChannelUnit(
                                 cfg, record_analysis->voltage.selection, slot));
        out += "}";
    }
    out += "]";
    out += ", ";
    appendJsonKey(out, "prefalla_phasors");
    appendPhasorArrayJson(out, record_analysis->voltage.prefalla_phasors, cfg,
                          record_analysis->voltage.selection);
    out += ", ";
    appendJsonKey(out, "falla_phasors");
    appendPhasorArrayJson(out, record_analysis->voltage.falla_phasors, cfg,
                          record_analysis->voltage.selection);
    out += ", ";
    appendJsonKey(out, "prefalla_sequences");
    appendSequenceJson(out, record_analysis->voltage.prefalla_sequences, "v");
    out += ", ";
    appendJsonKey(out, "falla_sequences");
    appendSequenceJson(out, record_analysis->voltage.falla_sequences, "v");
    out += ", ";
    appendJsonKey(out, "change_pct");
    out += "[";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) {
            out += ", ";
        }
        appendJsonOptionalNumber(out,
                                 record_analysis->voltage.change_pct[slot]);
    }
    out += "]";
    out += ", ";
    appendJsonKey(out, "numeric_conversion_applied");
    out += "false";
    out += ", ";
    appendJsonKey(out, "diagnostics");
    appendDiagnosticsJson(out, &record_analysis->voltage.diagnostics);
    out += "}";

    out += ", ";
    appendJsonKey(out, "spectra");
    out += "[";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) {
            out += ", ";
        }
        const auto& spectrum = record_analysis->spectra[slot];
        if (!spectrum.has_value()) {
            out += "null";
            continue;
        }
        out += "{";
        appendJsonKey(out, "phase");
        appendJsonString(out, phaseName(slot));
        out += ", ";
        appendJsonKey(out, "ok");
        out += spectrum->ok ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "unit");
        appendJsonString(out, spectrum->unit);
        out += ", ";
        appendJsonKey(out, "fundamental_hz");
        out += jsonNumber(spectrum->fundamental_hz);
        out += ", ";
        appendJsonKey(out, "sample_rate_hz");
        out += jsonNumber(spectrum->sample_rate_hz);
        out += ", ";
        appendJsonKey(out, "cycles");
        out += std::to_string(spectrum->cycles);
        out += ", ";
        appendJsonKey(out, "max_order");
        out += std::to_string(spectrum->max_order);
        out += ", ";
        appendJsonKey(out, "supported_order");
        out += std::to_string(spectrum->supported_order);
        out += ", ";
        appendJsonKey(out, "window_start_s");
        out += jsonNumber(spectrum->window_start_s);
        out += ", ";
        appendJsonKey(out, "window_end_s");
        out += jsonNumber(spectrum->window_end_s);
        out += ", ";
        appendJsonKey(out, "fft_points");
        out += std::to_string(spectrum->fft_points);
        out += ", ";
        appendJsonKey(out, "thd_pct");
        appendJsonOptionalNumber(out, spectrum->thd_pct);
        out += ", ";
        appendJsonKey(out, "h2_h1_pct");
        appendJsonOptionalNumber(out, spectrum->h2_h1_pct);
        out += ", ";
        appendJsonKey(out, "harmonics");
        out += "[";
        for (std::size_t order = 0; order < spectrum->harmonics_rms.size();
             ++order) {
            if (order != 0) {
                out += ", ";
            }
            out += "{\"order\":";
            out += std::to_string(order + 1);
            out += ", \"rms\":";
            out += jsonNumber(spectrum->harmonics_rms[order]);
            out += "}";
        }
        out += "]";
        out += ", ";
        appendJsonKey(out, "diagnostics");
        appendDiagnosticsJson(out, &spectrum->diagnostics);
        out += "}";
    }
    out += "]";

    out += ", ";
    appendJsonKey(out, "saturation");
    out += "[";
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (slot != 0) {
            out += ", ";
        }
        const auto& saturation = record_analysis->saturation[slot];
        if (!saturation.has_value()) {
            out += "null";
            continue;
        }
        out += "{";
        appendJsonKey(out, "phase");
        appendJsonString(out, phaseName(slot));
        out += ", ";
        appendJsonKey(out, "available");
        out += saturation->available ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "possible");
        out += saturation->possible ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "heuristic_only");
        out += "true";
        out += ", ";
        appendJsonKey(out, "shoulder_hits");
        out += std::to_string(saturation->shoulder_hits);
        out += ", ";
        appendJsonKey(out, "shoulder_required_hits");
        out += std::to_string(saturation->shoulder_required_hits);
        out += ", ";
        appendJsonKey(out, "shoulder_peak");
        out += jsonNumber(saturation->shoulder_peak);
        out += ", ";
        appendJsonKey(out, "unit");
        appendJsonString(out, phaseChannelUnit(
                                 cfg, record_analysis->phases, slot));
        out += ", ";
        appendJsonKey(out, "h2_h1_pct");
        appendJsonOptionalNumber(out, saturation->h2_h1_pct);
        out += ", ";
        appendJsonKey(out, "h2_threshold_pct");
        out += jsonNumber(saturation->h2_threshold_pct);
        out += ", ";
        appendJsonKey(out, "h2_elevated");
        out += saturation->h2_elevated ? "true" : "false";
        out += ", ";
        appendJsonKey(out, "flat_top_pct");
        appendJsonOptionalNumber(out, saturation->flat_top_pct);
        out += ", ";
        appendJsonKey(out, "longest_flat_top_ms");
        out += saturation->available &&
                       std::isfinite(saturation->longest_flat_top_ms)
                   ? jsonNumber(saturation->longest_flat_top_ms)
                   : "null";
        out += ", ";
        appendJsonKey(out, "flat_top_elevated");
        out += saturation->available
                   ? (saturation->flat_top_elevated ? "true" : "false")
                   : "null";
        out += ", ";
        appendJsonKey(out, "curvature_threshold");
        out += jsonNumber(saturation->curvature_threshold);
        out += ", ";
        appendJsonKey(out, "high_fraction");
        out += jsonNumber(saturation->high_fraction);
        out += ", ";
        appendJsonKey(out, "indicator_state");
        appendJsonString(out, saturationStateText(saturation));
        out += ", ";
        appendJsonKey(out, "methodology");
        appendJsonString(out, saturation->methodology);
        out += ", ";
        appendJsonKey(out, "diagnostics");
        appendDiagnosticsJson(out, &saturation->diagnostics);
        out += "}";
    }
    out += "]";

    out += ", ";
    appendJsonKey(out, "breaker");
    appendBreakerJson(out, record_analysis->breaker, cfg);

    out += ", ";
    appendJsonKey(out, "fault_distance");
    if (record_analysis->fault_distance) {
        const auto& distance = *record_analysis->fault_distance;
        out += "{";
        appendJsonKey(out, "status");
        appendJsonString(out, analysis::faultDistanceStatusName(distance.status));
        out += ", ";
        appendJsonKey(out, "distance_km");
        appendJsonOptionalNumber(out, distance.distance_km);
        out += ", ";
        appendJsonKey(out, "reference_length_km");
        out += jsonNumber(distance.reference_length_km);
        out += ", ";
        appendJsonKey(out, "ratio");
        appendJsonOptionalNumber(out, distance.ratio);
        out += "}";
    } else {
        out += "null";
    }
    out += "}";
}


std::string buildJson(const Context& context) {
    const ComtradeRecord& record = *context.record;
    const CfgInfo& cfg = record.cfg;
    const auto& data = record.data;
    const TimeReference reference = timeReference(record);

    std::vector<Diagnostic> display_diagnostics;
    if (context.diagnostics) {
        display_diagnostics = *context.diagnostics;
    }
    if (context.analysis) {
        for (const auto& diagnostic : context.analysis->breaker.diagnostics) {
            if (!containsDiagnostic(display_diagnostics, diagnostic)) {
                display_diagnostics.push_back(diagnostic);
            }
        }
    }
    const std::vector<Diagnostic>* diagnostics_for_display =
        (context.diagnostics || !display_diagnostics.empty())
            ? &display_diagnostics
            : nullptr;

    std::string out;
    out.reserve(32 * 1024);
    out += "{\n  ";
    appendJsonKey(out, "format");
    appendJsonString(out, "analizador-comtrade-resumen");
    out += ",\n  ";
    appendJsonKey(out, "version");
    out += "1";
    out += ",\n  ";
    appendJsonKey(out, "title");
    appendJsonString(out, context.title);
    out += ",\n  ";
    appendJsonKey(out, "source");
    appendJsonString(out, context.source);
    out += ",\n  ";
    appendJsonKey(out, "disclaimer");
    appendJsonString(out,
                     "Herramienta de apoyo: no identifica por sí sola tipo, zona "
                     "ni causa de falla, ni afirma operación de protecciones.");
    out += ",\n  ";
    appendJsonKey(out, "sample_count");
    out += std::to_string(data.sampleCount());
    out += ",\n  ";
    appendJsonKey(out, "analog_channel_count");
    out += std::to_string(cfg.analogCount());
    out += ",\n  ";
    appendJsonKey(out, "digital_channel_count");
    out += std::to_string(cfg.digitalCount());
    out += ",\n  ";
    appendJsonKey(out, "data_type");
    appendJsonString(out, dataFileTypeName(cfg.data_type));
    out += ",\n  ";
    appendJsonKey(out, "cfg_encoding");
    appendJsonString(out, textEncodingName(cfg.encoding));
    out += ",\n  ";
    appendJsonKey(out, "nominal_frequency_hz");
    out += jsonNumber(cfg.nominal_frequency_hz);
    out += ",\n  ";
    appendJsonKey(out, "time_multiplier");
    out += jsonNumber(cfg.time_multiplier);
    out += ",\n  ";
    appendJsonKey(out, "first_sample_time_s");
    out += data.times_s.empty() ? "null" : jsonNumber(data.times_s.front());
    out += ",\n  ";
    appendJsonKey(out, "last_sample_time_s");
    out += data.times_s.empty() ? "null" : jsonNumber(data.times_s.back());
    out += ",\n  ";
    appendJsonKey(out, "duration_s");
    out += data.times_s.size() > 1
               ? jsonNumber(data.times_s.back() - data.times_s.front())
               : "null";
    out += ",\n  ";
    appendJsonKey(out, "first_sample_nonzero");
    out += record.first_sample_nonzero ? "true" : "false";
    out += ",\n  ";
    appendJsonKey(out, "trigger_offset_s");
    out += record.trigger_offset_s ? jsonNumber(*record.trigger_offset_s)
                                   : "null";
    out += ",\n  ";
    appendJsonKey(out, "time_reference");
    out += "{\"kind\":";
    appendJsonString(out, reference.from_trigger ? "trigger" : "record_start");
    out += ", \"seconds\":";
    out += jsonNumber(reference.seconds);
    out += ", \"axis\":";
    appendJsonString(out, reference.axis_title);
    out += "}";

    out += ",\n  ";
    appendJsonKey(out, "station_name");
    appendJsonString(out, cfg.station_name);
    out += ",\n  ";
    appendJsonKey(out, "device_id");
    appendJsonString(out, cfg.device_id);
    out += ",\n  ";
    appendJsonKey(out, "cfg_revision");
    appendJsonString(out, cfg.revision);
    out += ",\n  ";
    appendJsonKey(out, "cfg_format_year");
    out += cfg.format_year == 0 ? "null" : std::to_string(cfg.format_year);
    out += ",\n  ";
    appendJsonKey(out, "start_time");
    appendDateTimeJson(out, cfg.start_time, cfg.start_time_text);
    out += ",\n  ";
    appendJsonKey(out, "trigger_time");
    appendDateTimeJson(out, cfg.trigger_time, cfg.trigger_time_text);

    out += ",\n  ";
    appendJsonKey(out, "sampling_rates");
    out += "[";
    for (std::size_t index = 0; index < cfg.rates.size(); ++index) {
        if (index != 0) {
            out += ", ";
        }
        out += "{\"rate_hz\":";
        out += jsonNumber(cfg.rates[index].rate_hz);
        out += ", \"end_sample\":";
        out += std::to_string(cfg.rates[index].end_sample);
        out += "}";
    }
    out += "]";

    out += ",\n  ";
    appendJsonKey(out, "analog_channels");
    out += "[";
    for (std::size_t index = 0; index < cfg.analog.size(); ++index) {
        const auto& channel = cfg.analog[index];
        if (index != 0) {
            out += ", ";
        }
        out += "\n    {";
        appendJsonKey(out, "position");
        out += std::to_string(channel.position);
        out += ", ";
        appendJsonKey(out, "declared_index");
        out += std::to_string(channel.declared_index);
        out += ", ";
        appendJsonKey(out, "name");
        appendJsonString(out, channel.name);
        out += ", ";
        appendJsonKey(out, "phase");
        appendJsonString(out, channel.phase);
        out += ", ";
        appendJsonKey(out, "circuit_component");
        appendJsonString(out, channel.circuit_component);
        out += ", ";
        appendJsonKey(out, "unit");
        appendJsonString(out, channel.unit);
        out += ", ";
        appendJsonKey(out, "a");
        out += jsonNumber(channel.a);
        out += ", ";
        appendJsonKey(out, "b");
        out += jsonNumber(channel.b);
        out += ", ";
        appendJsonKey(out, "skew_us");
        out += jsonNumber(channel.skew_us);
        out += ", ";
        appendJsonKey(out, "min_value");
        out += jsonNumber(channel.min_value);
        out += ", ";
        appendJsonKey(out, "max_value");
        out += jsonNumber(channel.max_value);
        out += ", ";
        appendJsonKey(out, "primary");
        out += jsonNumber(channel.primary);
        out += ", ";
        appendJsonKey(out, "secondary");
        out += jsonNumber(channel.secondary);
        out += ", ";
        appendJsonKey(out, "ps");
        appendJsonString(out, channel.ps);
        out += "}";
    }
    if (!cfg.analog.empty()) {
        out += "\n  ";
    }
    out += "]";

    out += ",\n  ";
    appendJsonKey(out, "digital_channels");
    out += "[";
    for (std::size_t index = 0; index < cfg.digital.size(); ++index) {
        const auto& channel = cfg.digital[index];
        if (index != 0) {
            out += ", ";
        }
        out += "\n    {";
        appendJsonKey(out, "position");
        out += std::to_string(channel.position);
        out += ", ";
        appendJsonKey(out, "declared_index");
        out += std::to_string(channel.declared_index);
        out += ", ";
        appendJsonKey(out, "name");
        appendJsonString(out, channel.name);
        out += ", ";
        appendJsonKey(out, "phase");
        appendJsonString(out, channel.phase);
        out += ", ";
        appendJsonKey(out, "circuit_component");
        appendJsonString(out, channel.circuit_component);
        out += ", ";
        appendJsonKey(out, "normal_state");
        appendJsonString(out, channel.normal_state);
        out += "}";
    }
    if (!cfg.digital.empty()) {
        out += "\n  ";
    }
    out += "]";

    out += ",\n  ";
    appendJsonKey(out, "analysis");
    appendAnalysisJson(out, context.analysis, cfg);
    out += ",\n  ";
    appendJsonKey(out, "diagnostics");
    appendDiagnosticsJson(out, diagnostics_for_display);
    out += ",\n  ";
    appendJsonKey(out, "unit_overrides");
    out += "[";
    if (context.unit_overrides) {
        bool first = true;
        for (const auto& item : *context.unit_overrides) {
            if (!first) {
                out += ", ";
            }
            first = false;
            out += "{";
            appendJsonKey(out, "channel_index");
            out += std::to_string(item.first);
            out += ", ";
            appendJsonKey(out, "cfg_unit");
            appendJsonString(out,
                             item.first < cfg.analog.size()
                                 ? cfg.analog[item.first].unit
                                 : std::string());
            out += ", ";
            appendJsonKey(out, "reported_unit");
            appendJsonString(out, item.second);
            out += ", ";
            appendJsonKey(out, "numeric_conversion_applied");
            out += "false";
            out += "}";
        }
    }
    out += "]";
    out += ",\n  ";
    appendJsonKey(out, "reference_length_km");
    out += context.reference_length_km > 0.0
               ? jsonNumber(context.reference_length_km)
               : "null";
    out += ",\n  ";
    appendJsonKey(out, "log_clock_offset_s");
    out += jsonNumber(context.log_clock_offset_s);

    out += ",\n  ";
    appendJsonKey(out, "event_log");
    out += "[";
    if (context.event_log) {
        for (std::size_t index = 0; index < context.event_log->size(); ++index) {
            if (index != 0) {
                out += ", ";
            }
            const auto& row = (*context.event_log)[index];
            out += "{";
            appendJsonKey(out, "source");
            appendJsonString(out, row.source);
            out += ", ";
            appendJsonKey(out, "when");
            appendJsonString(out, row.when_text);
            out += ", ";
            appendJsonKey(out, "seconds_from_start");
            appendJsonOptionalNumber(out, row.seconds_from_start);
            out += ", ";
            appendJsonKey(out, "seconds_from_trigger");
            appendJsonOptionalNumber(out, row.seconds_from_trigger);
            out += ", ";
            appendJsonKey(out, "structure");
            appendJsonString(out, row.structure);
            out += ", ";
            appendJsonKey(out, "signal");
            appendJsonString(out, row.signal);
            out += ", ";
            appendJsonKey(out, "value");
            appendJsonString(out, row.value);
            out += "}";
        }
    }
    out += "]";

    out += ",\n  ";
    appendJsonKey(out, "timeline");
    out += "{";
    appendJsonKey(out, "excluded_log_events");
    out += context.timeline
               ? std::to_string(context.timeline->excluded_log_events)
               : "0";
    out += ", ";
    appendJsonKey(out, "rows");
    out += "[";
    if (context.timeline) {
        for (std::size_t index = 0; index < context.timeline->rows.size();
             ++index) {
            if (index != 0) {
                out += ", ";
            }
            const auto& row = context.timeline->rows[index];
            out += "{";
            appendJsonKey(out, "seconds_from_reference");
            out += jsonNumber(row.seconds_from_reference);
            out += ", ";
            appendJsonKey(out, "source");
            appendJsonString(out, row.source);
            out += ", ";
            appendJsonKey(out, "event");
            appendJsonString(out, row.event);
            out += ", ";
            appendJsonKey(out, "detail");
            appendJsonString(out, row.detail);
            out += "}";
        }
    }
    out += "]}";

    out += ",\n  ";
    appendJsonKey(out, "trend");
    out += "{";
    appendJsonKey(out, "cycles");
    out += context.trend ? std::to_string(context.trend->cycles) : "1";
    out += ", ";
    appendJsonKey(out, "unit");
    appendJsonString(out, context.trend ? context.trend->unit : std::string());
    out += ", ";
    appendJsonKey(out, "points");
    out += "[";
    if (context.trend) {
        for (std::size_t index = 0; index < context.trend->points.size();
             ++index) {
            if (index != 0) {
                out += ", ";
            }
            const auto& point = context.trend->points[index];
            out += "{";
            appendJsonKey(out, "time_s");
            out += jsonNumber(point.time_s);
            out += ", ";
            appendJsonKey(out, "phase_rms");
            out += "[";
            for (std::size_t slot = 0; slot < 3; ++slot) {
                if (slot != 0) {
                    out += ", ";
                }
                out += jsonNumber(point.phase_rms[slot]);
            }
            out += "], ";
            appendJsonKey(out, "sequence_rms");
            out += "[";
            for (std::size_t slot = 0; slot < 3; ++slot) {
                if (slot != 0) {
                    out += ", ";
                }
                out += jsonNumber(point.sequence_rms[slot]);
            }
            out += "]}";
        }
    }
    out += "]}";

    out += ",\n  ";
    appendJsonKey(out, "comparison");
    out += "[";
    if (context.comparison) {
        for (std::size_t index = 0; index < context.comparison->rows.size();
             ++index) {
            if (index != 0) {
                out += ", ";
            }
            const auto& row = context.comparison->rows[index];
            out += "{";
            appendJsonKey(out, "record");
            appendJsonString(out, row.record);
            out += ", ";
            appendJsonKey(out, "window");
            appendJsonString(out, row.window);
            out += ", ";
            appendJsonKey(out, "phase");
            appendJsonString(out, row.phase);
            out += ", ";
            appendJsonKey(out, "quantity");
            appendJsonString(out, row.quantity.empty() ? std::string("Corriente")
                                                       : row.quantity);
            out += ", ";
            appendJsonKey(out, "frequency_hz");
            out += jsonNumber(row.frequency_hz);
            out += ", ";
            appendJsonKey(out, "magnitude");
            out += row.finite ? jsonNumber(row.magnitude) : "null";
            out += ", ";
            appendJsonKey(out, "angle_deg");
            out += row.finite ? jsonNumber(row.angle_deg) : "null";
            out += ", ";
            appendJsonKey(out, "unit");
            appendJsonString(out, row.unit);
            out += "}";
        }
    }
    out += "]";
    out += "\n}\n";
    return out;
}

                                                                              
                                                                 
                                                                              

std::string trimText(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' ||
                           text[begin] == '\r' || text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string lowerText(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::vector<std::string> splitDelimited(std::string_view line, char delimiter) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char c = line[index];
        if (c == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                field.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
            continue;
        }
        if (c == delimiter && !quoted) {
            fields.push_back(trimText(field));
            field.clear();
            continue;
        }
        field.push_back(c);
    }
    fields.push_back(trimText(field));
    return fields;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        if (current.back() == '\r') {
            current.pop_back();
        }
        lines.push_back(std::move(current));
    }
    return lines;
}

std::optional<std::size_t> columnIndex(
    const std::vector<std::string>& header, std::string_view name) {
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (lowerText(header[index]) == name) {
            return index;
        }
    }
    return std::nullopt;
}

std::string cell(const std::vector<std::string>& row,
                 const std::optional<std::size_t>& index) {
    if (!index.has_value() || *index >= row.size()) {
        return {};
    }
    return row[*index];
}

bool validTriggerReference(const ComtradeRecord& record, double& reference) {
    if (!record.trigger_offset_s || record.data.times_s.empty()) {
        return false;
    }
    const double trigger = *record.trigger_offset_s;
    if (!std::isfinite(trigger) || trigger < record.data.times_s.front() ||
        trigger > record.data.times_s.back()) {
        return false;
    }
    reference = trigger;
    return true;
}

double analysisReference(const ComtradeRecord& record) {
    double trigger = 0.0;
    if (validTriggerReference(record, trigger)) {
        return trigger;
    }
    return record.data.times_s.empty() ? 0.0 : record.data.times_s.front();
}

void alignEventRow(const ComtradeRecord& record, EventLogRow& row) {
    row.seconds_from_start.reset();
    row.seconds_from_trigger.reset();
    if (!row.when_valid || !record.cfg.start_time.valid) {
        return;
    }
    const auto from_start =
        dateTimeDifferenceSeconds(record.cfg.start_time, row.when);
    if (!from_start.has_value() || !std::isfinite(*from_start)) {
        return;
    }
    row.seconds_from_start = *from_start;
    double trigger = 0.0;
    if (validTriggerReference(record, trigger)) {
        row.seconds_from_trigger = *from_start - trigger;
    }
}

std::string eventLogCsv(const std::vector<EventLogRow>& rows) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Fuente");
    out += ',';
    appendCsvField(out, "Fecha/hora origen");
    out += ',';
    appendCsvField(out, "Segundos desde inicio");
    out += ',';
    appendCsvField(out, "Segundos desde trigger");
    out += ',';
    appendCsvField(out, "Estructura");
    out += ',';
    appendCsvField(out, "Senal");
    out += ',';
    appendCsvField(out, "Valor");
    out += "\r\n";
    for (const auto& row : rows) {
        appendCsvField(out, row.source);
        out += ',';
        appendCsvField(out, row.when_text);
        out += ',';
        appendCsvField(out, row.seconds_from_start
                                ? formatFull(*row.seconds_from_start)
                                : std::string());
        out += ',';
        appendCsvField(out, row.seconds_from_trigger
                                ? formatFull(*row.seconds_from_trigger)
                                : std::string());
        out += ',';
        appendCsvField(out, row.structure);
        out += ',';
        appendCsvField(out, row.signal);
        out += ',';
        appendCsvField(out, row.value);
        out += "\r\n";
    }
    return out;
}

std::string timelineCsv(const TimelineResult& timeline) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Segundos desde referencia");
    out += ',';
    appendCsvField(out, "Fuente");
    out += ',';
    appendCsvField(out, "Evento");
    out += ',';
    appendCsvField(out, "Detalle");
    out += "\r\n";
    for (const auto& row : timeline.rows) {
        appendCsvField(out, formatFull(row.seconds_from_reference));
        out += ',';
        appendCsvField(out, row.source);
        out += ',';
        appendCsvField(out, row.event);
        out += ',';
        appendCsvField(out, row.detail);
        out += "\r\n";
    }
    return out;
}

std::string trendCsv(const TrendResult& trend) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Tiempo desde inicio (s)");
    for (const char* phase : {"A", "B", "C"}) {
        out += ',';
        appendCsvField(out, std::string("Fase ") + phase + " RMS");
    }
    for (const char* sequence : {"I0", "I1", "I2"}) {
        out += ',';
        appendCsvField(out, std::string(sequence) + " RMS");
    }
    out += "\r\n";
    for (const auto& point : trend.points) {
        appendCsvField(out, formatFull(point.time_s));
        for (double value : point.phase_rms) {
            out += ',';
            appendCsvField(out, formatFull(value));
        }
        for (double value : point.sequence_rms) {
            out += ',';
            appendCsvField(out, formatFull(value));
        }
        out += "\r\n";
    }
    return out;
}

std::string comparisonCsv(const ComparisonResult& comparison) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Registro");
    out += ',';
    appendCsvField(out, "Cantidad");
    out += ',';
    appendCsvField(out, "Ventana");
    out += ',';
    appendCsvField(out, "Fase");
    out += ',';
    appendCsvField(out, "Frecuencia Hz");
    out += ',';
    appendCsvField(out, "Magnitud RMS");
    out += ',';
    appendCsvField(out, "Angulo grados");
    out += ',';
    appendCsvField(out, "Unidad");
    out += "\r\n";
    for (const auto& row : comparison.rows) {
        appendCsvField(out, row.record);
        out += ',';
        appendCsvField(out, row.quantity.empty() ? std::string("Corriente")
                                                  : row.quantity);
        out += ',';
        appendCsvField(out, row.window);
        out += ',';
        appendCsvField(out, row.phase);
        out += ',';
        appendCsvField(out, formatFull(row.frequency_hz));
        out += ',';
        appendCsvField(out, row.finite ? formatFull(row.magnitude)
                                       : std::string());
        out += ',';
        appendCsvField(out, row.finite ? formatFull(row.angle_deg)
                                       : std::string());
        out += ',';
        appendCsvField(out, row.unit);
        out += "\r\n";
    }
    return out;
}

std::string phasorCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Ventana");
    out += ',';
    appendCsvField(out, "Fase");
    out += ',';
    appendCsvField(out, "Magnitud RMS");
    out += ',';
    appendCsvField(out, "Angulo grados");
    out += ',';
    appendCsvField(out, "Unidad CFG");
    out += "\r\n";
    if (context.analysis && context.record) {
        const auto append = [&](const char* window,
                                const std::optional<std::array<
                                    analysis::Phasor, 3>>& values) {
            if (!values) {
                return;
            }
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const auto& phasor = (*values)[slot];
                appendCsvField(out, window);
                out += ',';
                appendCsvField(out, phaseName(slot));
                out += ',';
                appendCsvField(out, phasor.isFinite()
                                        ? formatFull(phasor.magnitude())
                                        : std::string());
                out += ',';
                appendCsvField(out, phasor.isFinite()
                                        ? formatFull(phasor.angleDegrees())
                                        : std::string());
                out += ',';
                appendCsvField(out, phaseChannelUnit(
                                        context.record->cfg,
                                        context.analysis->phases, slot));
                out += "\r\n";
            }
        };
        append("Prefalla", context.analysis->prefalla_phasors);
        append("Falla", context.analysis->falla_phasors);
    }
    return out;
}

std::string sequenceCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Ventana");
    out += ',';
    appendCsvField(out, "Secuencia");
    out += ',';
    appendCsvField(out, "Magnitud RMS");
    out += ',';
    appendCsvField(out, "Angulo grados");
    out += ',';
    appendCsvField(out, "Unidad CFG");
    out += "\r\n";
    if (context.analysis) {
        const auto append = [&](const char* window,
                                const std::optional<analysis::SequenceSet>& set) {
            if (!set) {
                return;
            }
            const std::pair<const char*, std::complex<double>> values[] = {
                {"I0", set->zero}, {"I1", set->positive}, {"I2", set->negative}};
            for (const auto& item : values) {
                appendCsvField(out, window);
                out += ',';
                appendCsvField(out, item.first);
                out += ',';
                appendCsvField(out, std::isfinite(std::abs(item.second))
                                        ? formatFull(std::abs(item.second))
                                        : std::string());
                out += ',';
                appendCsvField(out, std::isfinite(std::abs(item.second))
                                        ? formatFull(std::arg(item.second) *
                                                     180.0 / analysis::kPi)
                                        : std::string());
                out += ',';
                appendCsvField(out, context.record
                                        ? phaseChannelUnit(
                                              context.record->cfg,
                                              context.analysis->phases, 0)
                                        : std::string());
                out += "\r\n";
            }
        };
        append("Prefalla", context.analysis->prefalla_sequences);
        append("Falla", context.analysis->falla_sequences);
    }
    return out;
}

std::string voltageSequenceCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Ventana");
    out += ',';
    appendCsvField(out, "Secuencia");
    out += ',';
    appendCsvField(out, "Magnitud RMS");
    out += ',';
    appendCsvField(out, "Angulo grados");
    out += ',';
    appendCsvField(out, "Unidad CFG");
    out += "\r\n";
    if (context.analysis) {
        const auto append = [&](const char* window,
                                const std::optional<analysis::SequenceSet>& set) {
            if (!set) {
                return;
            }
            const std::pair<const char*, std::complex<double>> values[] = {
                {"V0", set->zero}, {"V1", set->positive}, {"V2", set->negative}};
            for (const auto& item : values) {
                appendCsvField(out, window);
                out += ',';
                appendCsvField(out, item.first);
                out += ',';
                appendCsvField(out, std::isfinite(std::abs(item.second))
                                        ? formatFull(std::abs(item.second))
                                        : std::string());
                out += ',';
                appendCsvField(out, std::isfinite(std::abs(item.second))
                                        ? formatFull(std::arg(item.second) *
                                                     180.0 / analysis::kPi)
                                        : std::string());
                out += ',';
                appendCsvField(out, context.analysis->voltage.unit);
                out += "\r\n";
            }
        };
        append("Prefalla", context.analysis->voltage.prefalla_sequences);
        append("Falla", context.analysis->voltage.falla_sequences);
    }
    return out;
}

std::string coherenceCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Ventana");
    out += ',';
    appendCsvField(out, "Canal");
    out += ',';
    appendCsvField(out, "RMS AC");
    out += ',';
    appendCsvField(out, "Fundamental RMS");
    out += ',';
    appendCsvField(out, "Cociente");
    out += "\r\n";
    if (context.analysis) {
        for (const auto& row : context.analysis->coherence_rows) {
            appendCsvField(out, row.window_label);
            out += ',';
            appendCsvField(out, row.channel_name);
            out += ',';
            appendCsvField(out, formatFull(row.ac_rms));
            out += ',';
            appendCsvField(out, formatFull(row.fundamental_rms));
            out += ',';
            appendCsvField(out, formatFull(row.ratio));
            out += "\r\n";
        }
    }
    return out;
}

std::string harmonicsCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Fase");
    out += ',';
    appendCsvField(out, "Canal");
    out += ',';
    appendCsvField(out, "Orden");
    out += ',';
    appendCsvField(out, "RMS");
    out += ',';
    appendCsvField(out, "THD %");
    out += ',';
    appendCsvField(out, "H2/H1 %");
    out += ',';
    appendCsvField(out, "Unidad CFG");
    out += "\r\n";
    if (context.analysis && context.record) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& fit = context.analysis->harmonics[slot];
            if (!fit.ok) {
                continue;
            }
            for (std::size_t order = 0; order < fit.harmonics_rms.size();
                 ++order) {
                appendCsvField(out, phaseName(slot));
                out += ',';
                appendCsvField(out, phaseChannelName(
                                        context.record->cfg,
                                        context.analysis->phases, slot));
                out += ',';
                appendCsvField(out, std::to_string(order + 1));
                out += ',';
                appendCsvField(out, formatFull(fit.harmonics_rms[order]));
                out += ',';
                appendCsvField(out, fit.thd_pct ? formatFull(*fit.thd_pct)
                                                : std::string());
                out += ',';
                appendCsvField(out, fit.h2_h1_pct
                                        ? formatFull(*fit.h2_h1_pct)
                                        : std::string());
                out += ',';
                appendCsvField(out, phaseChannelUnit(
                                        context.record->cfg,
                                        context.analysis->phases, slot));
                out += "\r\n";
            }
        }
    }
    return out;
}

std::string voltageCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Fase");
    out += ',';
    appendCsvField(out, "Canal");
    out += ',';
    appendCsvField(out, "Unidad CFG");
    out += ',';
    appendCsvField(out, "Ventana");
    out += ',';
    appendCsvField(out, "Magnitud RMS");
    out += ',';
    appendCsvField(out, "Angulo grados");
    out += ',';
    appendCsvField(out, "Cambio %");
    out += "\r\n";
    if (context.analysis && context.record) {
        const auto& voltage = context.analysis->voltage;
        const auto append_set =
            [&](const char* window,
                const std::optional<std::array<analysis::Phasor, 3>>& values) {
                if (!values) {
                    return;
                }
                for (std::size_t slot = 0; slot < 3; ++slot) {
                    appendCsvField(out, phaseName(slot));
                    out += ',';
                    appendCsvField(out, phaseChannelName(
                                            context.record->cfg,
                                            voltage.selection, slot));
                    out += ',';
                    appendCsvField(out, phaseChannelUnit(
                                            context.record->cfg,
                                            voltage.selection, slot));
                    out += ',';
                    appendCsvField(out, window);
                    out += ',';
                    appendCsvField(out,
                                   (*values)[slot].isFinite()
                                       ? formatFull((*values)[slot].magnitude())
                                       : std::string());
                    out += ',';
                    appendCsvField(out,
                                   (*values)[slot].isFinite()
                                       ? formatFull(
                                             (*values)[slot].angleDegrees())
                                       : std::string());
                    out += ',';
                    appendCsvField(out,
                                   voltage.change_pct[slot]
                                       ? formatFull(*voltage.change_pct[slot])
                                       : std::string());
                    out += "\r\n";
                }
            };
        append_set("Prefalla", voltage.prefalla_phasors);
        append_set("Falla", voltage.falla_phasors);
    }
    return out;
}

std::string spectrumCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Fase");
    out += ',';
    appendCsvField(out, "Canal");
    out += ',';
    appendCsvField(out, "Unidad");
    out += ',';
    appendCsvField(out, "Fundamental Hz");
    out += ',';
    appendCsvField(out, "Orden");
    out += ',';
    appendCsvField(out, "RMS");
    out += ',';
    appendCsvField(out, "THD %");
    out += ',';
    appendCsvField(out, "H2/H1 %");
    out += "\r\n";
    if (context.analysis && context.record) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& spectrum = context.analysis->spectra[slot];
            if (!spectrum.has_value() || !spectrum->ok) {
                continue;
            }
            for (std::size_t order = 0; order < spectrum->harmonics_rms.size();
                 ++order) {
                appendCsvField(out, phaseName(slot));
                out += ',';
                appendCsvField(out, phaseChannelName(
                                        context.record->cfg,
                                        context.analysis->phases, slot));
                out += ',';
                appendCsvField(out, spectrum->unit);
                out += ',';
                appendCsvField(out, formatFull(spectrum->fundamental_hz));
                out += ',';
                appendCsvField(out, std::to_string(order + 1));
                out += ',';
                appendCsvField(out, formatFull(spectrum->harmonics_rms[order]));
                out += ',';
                appendCsvField(out, spectrum->thd_pct
                                        ? formatFull(*spectrum->thd_pct)
                                        : std::string());
                out += ',';
                appendCsvField(out, spectrum->h2_h1_pct
                                        ? formatFull(*spectrum->h2_h1_pct)
                                        : std::string());
                out += "\r\n";
            }
        }
    }
    return out;
}

std::string spectrumBinsCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Fase");
    out += ',';
    appendCsvField(out, "Canal");
    out += ',';
    appendCsvField(out, "Unidad CFG");
    out += ',';
    appendCsvField(out, "Frecuencia Hz");
    out += ',';
    appendCsvField(out, "RMS");
    out += "\r\n";
    if (context.analysis && context.record) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& spectrum = context.analysis->spectra[slot];
            if (!spectrum.has_value() || !spectrum->ok) {
                continue;
            }
            const std::string channel = phaseChannelName(
                context.record->cfg, context.analysis->phases, slot);
            for (std::size_t bin = 0;
                 bin < spectrum->bin_frequency_hz.size() &&
                 bin < spectrum->rms.size();
                 ++bin) {
                appendCsvField(out, phaseName(slot));
                out += ',';
                appendCsvField(out, channel);
                out += ',';
                appendCsvField(out, spectrum->unit);
                out += ',';
                appendCsvField(out, formatFull(spectrum->bin_frequency_hz[bin]));
                out += ',';
                appendCsvField(out, formatFull(spectrum->rms[bin]));
                out += "\r\n";
            }
        }
    }
    return out;
}

std::string saturationCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Fase");
    out += ',';
    appendCsvField(out, "Canal");
    out += ',';
    appendCsvField(out, "Disponible");
    out += ',';
    appendCsvField(out, "Indicio posible");
    out += ',';
    appendCsvField(out, "Hombros");
    out += ',';
    appendCsvField(out, "Hombros requeridos");
    out += ',';
    appendCsvField(out, "Pico");
    out += ',';
    appendCsvField(out, "Unidad CFG");
    out += ',';
    appendCsvField(out, "H2/H1 %");
    out += ',';
    appendCsvField(out, "Umbral H2/H1 %");
    out += ',';
    appendCsvField(out, "Metodologia");
    out += ',';
    appendCsvField(out, "Aplanamiento %");
    out += ',';
    appendCsvField(out, "Aplanamiento ms");
    out += ',';
    appendCsvField(out, "Aplanamiento elevado");
    out += ',';
    appendCsvField(out, "Curvatura umbral");
    out += ',';
    appendCsvField(out, "Fraccion alta");
    out += ',';
    appendCsvField(out, "Estado");
    out += "\r\n";
    if (context.analysis && context.record) {
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const auto& saturation = context.analysis->saturation[slot];
            if (!saturation.has_value() || !saturation->available) {
                continue;
            }
            appendCsvField(out, phaseName(slot));
            out += ',';
            appendCsvField(out, phaseChannelName(context.record->cfg,
                                                 context.analysis->phases,
                                                 slot));
            out += ',';
            appendCsvField(out, "si");
            out += ',';
            appendCsvField(out, saturation->possible ? "si" : "no");
            out += ',';
            appendCsvField(out, std::to_string(saturation->shoulder_hits));
            out += ',';
            appendCsvField(out,
                           std::to_string(saturation->shoulder_required_hits));
            out += ',';
            appendCsvField(out, formatFull(saturation->shoulder_peak));
            out += ',';
            appendCsvField(out, phaseChannelUnit(context.record->cfg,
                                                 context.analysis->phases,
                                                 slot));
            out += ',';
            appendCsvField(out, saturation->h2_h1_pct
                                    ? formatFull(*saturation->h2_h1_pct)
                                    : std::string());
            out += ',';
            appendCsvField(out, formatFull(saturation->h2_threshold_pct));
            out += ',';
            appendCsvField(out, saturation->methodology);
            out += ',';
            appendCsvField(out, saturation->flat_top_pct
                                    ? formatFull(*saturation->flat_top_pct)
                                    : std::string());
            out += ',';
            appendCsvField(out, formatFull(saturation->longest_flat_top_ms));
            out += ',';
            appendCsvField(out, saturation->flat_top_elevated ? "si" : "no");
            out += ',';
            appendCsvField(out, formatFull(saturation->curvature_threshold));
            out += ',';
            appendCsvField(out, formatFull(saturation->high_fraction * 100.0));
            out += ',';
            appendCsvField(out, saturationStateText(saturation));
            out += "\r\n";
        }
    }
    return out;
}

std::string breakerCsv(const Context& context) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Registro");
    out += ',';
    appendCsvField(out, "Configurado");
    out += ',';
    appendCsvField(out, "Inicio candidato s");
    out += ',';
    appendCsvField(out, "Trip s");
    out += ',';
    appendCsvField(out, "Contacto abierto s");
    out += ',';
    appendCsvField(out, "Extincion corriente s");
    out += ',';
    appendCsvField(out, "Proteccion ms");
    out += ',';
    appendCsvField(out, "Apertura contacto ms");
    out += ',';
    appendCsvField(out, "Despeje ms");
    out += ',';
    appendCsvField(out, "Resolucion digital ms");
    out += ',';
    appendCsvField(out, "Resolucion extincion ms");
    out += ',';
    appendCsvField(out, "Diagnosticos");
    for(const char* name : {"Trip asignado","52a asignado","52b asignado","Debounce ms","Umbral extincion por fase","Persistencia ciclos","Incertidumbre inicio ms"}) {
        out+=','; appendCsvField(out,name);
    }
    out += "\r\n";
    if (context.analysis) {
        const auto& breaker = context.analysis->breaker;
        const auto resolution_field = [&](double value) -> std::string {
            if (!breaker.configured || !std::isfinite(value) || value <= 0.0) {
                return {};
            }
            return formatFull(value);
        };
        appendCsvField(out, context.title);
        out += ',';
        appendCsvField(out, breaker.configured ? "si" : "no");
        out += ',';
        appendCsvField(out, breaker.onset_s ? formatFull(*breaker.onset_s)
                                            : std::string());
        out += ',';
        appendCsvField(out, breaker.trip_s ? formatFull(*breaker.trip_s)
                                           : std::string());
        out += ',';
        appendCsvField(out, breaker.contact_open_s
                                ? formatFull(*breaker.contact_open_s)
                                : std::string());
        out += ',';
        appendCsvField(out, breaker.current_extinction_s
                                ? formatFull(*breaker.current_extinction_s)
                                : std::string());
        out += ',';
        appendCsvField(out, breaker.protection_ms
                                ? formatFull(*breaker.protection_ms)
                                : std::string());
        out += ',';
        appendCsvField(out, breaker.contact_opening_ms
                                ? formatFull(*breaker.contact_opening_ms)
                                : std::string());
        out += ',';
        appendCsvField(out, breaker.clearing_ms
                                ? formatFull(*breaker.clearing_ms)
                                : std::string());
        out += ',';
        appendCsvField(out, resolution_field(breaker.digital_resolution_ms));
        out += ',';
        appendCsvField(out,
                       resolution_field(breaker.extinction_resolution_ms));
        out += ',';
        std::string diagnostics;
        for (std::size_t index = 0; index < breaker.diagnostics.size();
             ++index) {
            if (index != 0) {
                diagnostics += " | ";
            }
            diagnostics += breaker.diagnostics[index].message;
        }
        appendCsvField(out, diagnostics);
        const CfgInfo* cfg=context.record ? &context.record->cfg : nullptr;
        for(const auto* assignment : {&breaker.applied_options.trip,&breaker.applied_options.contact_52a,&breaker.applied_options.contact_52b}) {
            out+=','; appendCsvField(out,breakerAssignmentText(cfg,*assignment));
        }
        out+=','; appendCsvField(out,formatFull(breaker.applied_options.debounce_ms));
        out+=','; appendCsvField(out,formatFull(breaker.applied_options.extinction_fraction));
        out+=','; appendCsvField(out,std::to_string(breaker.applied_options.extinction_cycles));
        out+=','; appendCsvField(out,resolution_field(breaker.onset_resolution_ms));
        out += "\r\n";
    }
    return out;
}

                                                                               

void appendLe16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void appendLe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}

std::uint32_t crc32Of(std::string_view bytes) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t index = 0; index < 256; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1))
                                     : (value >> 1);
            }
            values[index] = value;
        }
        return values;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const unsigned char c : bytes) {
        crc = table[(crc ^ c) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void dosDateTime(std::uint16_t& dos_date, std::uint16_t& dos_time) {
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    dos_date = static_cast<std::uint16_t>(
        ((now.wYear - 1980) << 9) | (now.wMonth << 5) | now.wDay);
    dos_time = static_cast<std::uint16_t>(
        (now.wHour << 11) | (now.wMinute << 5) | (now.wSecond / 2));
}

struct ZipEntryData {
    std::string name;
    std::string bytes;
};

std::string buildStoredZip(const std::vector<ZipEntryData>& entries) {
    std::uint16_t dos_date = 0;
    std::uint16_t dos_time = 0;
    dosDateTime(dos_date, dos_time);
    std::vector<std::uint8_t> out;
    struct Central {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
    };
    std::vector<Central> central;
    for (const auto& entry : entries) {
        Central item;
        item.name = entry.name;
        item.crc = crc32Of(entry.bytes);
        item.size = static_cast<std::uint32_t>(entry.bytes.size());
        item.offset = static_cast<std::uint32_t>(out.size());
        central.push_back(item);
        appendLe32(out, 0x04034b50u);
        appendLe16(out, 20);
        appendLe16(out, 0);
        appendLe16(out, 0);
        appendLe16(out, dos_time);
        appendLe16(out, dos_date);
        appendLe32(out, item.crc);
        appendLe32(out, item.size);
        appendLe32(out, item.size);
        appendLe16(out, static_cast<std::uint16_t>(entry.name.size()));
        appendLe16(out, 0);
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        out.insert(out.end(), entry.bytes.begin(), entry.bytes.end());
    }
    const std::uint32_t central_offset = static_cast<std::uint32_t>(out.size());
    for (const auto& item : central) {
        appendLe32(out, 0x02014b50u);
        appendLe16(out, 20);
        appendLe16(out, 20);
        appendLe16(out, 0);
        appendLe16(out, 0);
        appendLe16(out, dos_time);
        appendLe16(out, dos_date);
        appendLe32(out, item.crc);
        appendLe32(out, item.size);
        appendLe32(out, item.size);
        appendLe16(out, static_cast<std::uint16_t>(item.name.size()));
        appendLe16(out, 0);
        appendLe16(out, 0);
        appendLe16(out, 0);
        appendLe16(out, 0);
        appendLe32(out, 0);
        appendLe32(out, item.offset);
        out.insert(out.end(), item.name.begin(), item.name.end());
    }
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(out.size()) - central_offset;
    appendLe32(out, 0x06054b50u);
    appendLe16(out, 0);
    appendLe16(out, 0);
    appendLe16(out, static_cast<std::uint16_t>(central.size()));
    appendLe16(out, static_cast<std::uint16_t>(central.size()));
    appendLe32(out, central_size);
    appendLe32(out, central_offset);
    appendLe16(out, 0);
    return std::string(out.begin(), out.end());
}

                                                                               

std::string renderTrendSvg(const TrendResult& trend) {
    if (trend.points.empty()) {
        return "<p class=\"note\">No hay ventanas completas para la tendencia.</p>";
    }
    double x0 = trend.points.front().time_s;
    double x1 = trend.points.back().time_s;
    if (!(x1 > x0)) {
        x0 -= 0.5;
        x1 += 0.5;
    }
    double y_max = 0.0;
    for (const auto& point : trend.points) {
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
    constexpr double width = 960.0;
    constexpr double height = 330.0;
    constexpr double left = 82.0;
    constexpr double top = 22.0;
    constexpr double plot_width = width - left - 26.0;
    constexpr double plot_height = height - top - 48.0;
    const auto x_of = [&](double time) {
        return left + (time - x0) / (x1 - x0) * plot_width;
    };
    const auto y_of = [&](double value) {
        return top + (y_max - value) / y_max * plot_height;
    };
    std::string html;
    html += "<figure class=\"chart\"><figcaption>RMS fundamental y componentes "
            "simétricas · ventana móvil</figcaption><svg viewBox=\"0 0 960 330\" "
            "role=\"img\"><rect class=\"plot-bg\" x=\"82\" y=\"22\" width=\"852\" "
            "height=\"260\"/>";
    for (int tick = 0; tick <= 4; ++tick) {
        const double ratio = static_cast<double>(tick) / 4.0;
        const double y = top + plot_height * ratio;
        const double value = y_max * (1.0 - ratio);
        html += "<line class=\"grid\" x1=\"82\" y1=\"" + formatDouble(y, 2) +
                "\" x2=\"934\" y2=\"" + formatDouble(y, 2) + "\"/>";
        html += "<text class=\"axis\" x=\"73\" y=\"" + formatDouble(y + 4.0, 2) +
                "\" text-anchor=\"end\">" + htmlEscape(displayNumber(value, 5)) +
                "</text>";
    }
    for (int tick = 0; tick <= 5; ++tick) {
        const double ratio = static_cast<double>(tick) / 5.0;
        const double x = left + plot_width * ratio;
        const double time = x0 + (x1 - x0) * ratio;
        html += "<line class=\"grid\" x1=\"" + formatDouble(x, 2) +
                "\" y1=\"22\" x2=\"" + formatDouble(x, 2) + "\" y2=\"282\"/>";
        html += "<text class=\"axis\" x=\"" + formatDouble(x, 2) +
                "\" y=\"302\" text-anchor=\"middle\">" +
                htmlEscape(displayNumber(time, 5)) + "</text>";
    }
    const auto path = [&](int series, bool sequence) {
        std::string d;
        bool open = false;
        for (const auto& point : trend.points) {
            const double value =
                sequence ? point.sequence_rms[static_cast<std::size_t>(series)]
                         : point.phase_rms[static_cast<std::size_t>(series)];
            if (!std::isfinite(value)) {
                open = false;
                continue;
            }
            const double x = x_of(point.time_s);
            const double y = y_of(value);
            if (!open) {
                d += "M";
                open = true;
            } else {
                d += "L";
            }
            d += formatDouble(x, 6);
            d += " ";
            d += formatDouble(y, 6);
        }
        return d;
    };
    const char* colors[3] = {"#0072B2", "#D55E00", "#009E73"};
    for (int series = 0; series < 3; ++series) {
        html += "<path class=\"trace\" d=\"" + path(series, false) +
                "\" stroke=\"" + colors[series] + "\"/>";
    }
    for (int series = 0; series < 3; ++series) {
        html += "<path class=\"trace\" stroke-dasharray=\"6 4\" d=\"" +
                path(series, true) + "\" stroke=\"" + colors[series] + "\"/>";
    }
    html += "<text class=\"axis-title\" x=\"508\" y=\"323\" "
            "text-anchor=\"middle\">Tiempo desde el inicio DAT (s)</text></svg>";
    html += "<div class=\"legend\"><span><i style=\"background:#0072B2\"></i>Fase A</span>"
            "<span><i style=\"background:#D55E00\"></i>Fase B</span>"
            "<span><i style=\"background:#009E73\"></i>Fase C</span>"
            "<span><i style=\"background:#0072B2\"></i>I0 (trazos)</span>"
            "<span><i style=\"background:#D55E00\"></i>I1 (trazos)</span>"
            "<span><i style=\"background:#009E73\"></i>I2 (trazos)</span></div>";
    html += "</figure>";
    return html;
}

}              

                                                                              
               
                                                                              

Result writeHtmlReport(const std::filesystem::path& path,
                       const Context& context,
                       const Options& options) {
    if (!context.record) {
        return {false, "No hay un registro cargado para generar el informe."};
    }
    return writeAll(path, buildHtml(context, options));
}

Result writeSampleCsv(const std::filesystem::path& path,
                      const Context& context) {
    if (!context.record) {
        return {false, "No hay un registro cargado para exportar muestras."};
    }
    const ComtradeRecord& record = *context.record;
    const auto& data = record.data;

    FileWriter writer;
    std::string error;
    if (!writer.open(path, error)) {
        return {false, "No se pudo crear el CSV: " + error};
    }

    double trigger_reference = 0.0;
    bool has_trigger = false;
    if (record.trigger_offset_s && !data.times_s.empty() &&
        *record.trigger_offset_s >= data.times_s.front() &&
        *record.trigger_offset_s <= data.times_s.back()) {
        trigger_reference = *record.trigger_offset_s;
        has_trigger = true;
    }

    std::string line;
    line.reserve(256);
    line.append("\xEF\xBB\xBF");
    appendCsvField(line, "numero_muestra");
    line += ',';
    appendCsvField(line, "tiempo_s");
    line += ',';
    appendCsvField(line, "tiempo_desde_trigger_s");
    for (std::size_t index = 0; index < record.cfg.analog.size(); ++index) {
        line += ',';
        std::string header = "A" + std::to_string(index + 1) + ": " +
                             record.cfg.analog[index].name;
        std::string unit = record.cfg.analog[index].unit;
        if (context.unit_overrides) {
            const auto found = context.unit_overrides->find(index);
            if (found != context.unit_overrides->end()) {
                unit = found->second;
            }
        }
        if (!unit.empty()) {
            header += " [" + unit + "]";
        }
        appendCsvField(line, header);
    }
    for (std::size_t index = 0; index < record.cfg.digital.size(); ++index) {
        line += ',';
        std::string header = "D" + std::to_string(index + 1) + ": " +
                             record.cfg.digital[index].name;
        appendCsvField(line, header);
    }
    line += "\r\n";
    if (!writer.write(line, error)) {
        return {false, "No se pudo escribir el CSV: " + error};
    }

    line.clear();
    line.reserve(160 + (record.cfg.analogCount() +
                        record.cfg.digitalCount()) * 24);
    for (std::size_t sample = 0; sample < data.sampleCount(); ++sample) {
        line.clear();
        appendCsvField(line, std::to_string(
                                  sample < data.sample_numbers.size()
                                      ? data.sample_numbers[sample]
                                      : 0));

        line += ',';
        if (sample < data.times_s.size()) {
            appendCsvField(line, csvNumber(data.times_s[sample]));
        } else {
            appendCsvField(line, "");
        }

        line += ',';
        if (has_trigger && sample < data.times_s.size()) {
            appendCsvField(line,
                           csvNumber(data.times_s[sample] - trigger_reference));
        } else {
            appendCsvField(line, "");
        }

        for (std::size_t channel = 0; channel < record.cfg.analog.size();
             ++channel) {
            line += ',';
            const double value = data.analogValue(sample, channel);
            appendCsvField(line, csvNumber(value));
        }
        for (std::size_t channel = 0; channel < record.cfg.digital.size();
             ++channel) {
            line += ',';
            appendCsvField(line,
                           data.digitalValue(sample, channel) ? "1" : "0");
        }
        line += "\r\n";
        if (!writer.write(line, error)) {
            return {false, "No se pudo escribir el CSV: " + error};
        }
    }

    if (!writer.commit(error)) {
        return {false, "No se pudo finalizar el CSV: " + error};
    }
    return {true, {}};
}

Result writeSummaryJson(const std::filesystem::path& path,
                        const Context& context) {
    if (!context.record) {
        return {false, "No hay un registro cargado para exportar el resumen."};
    }
    return writeAll(path, buildJson(context));
}

EventLogParseResult parseEventLogCsv(const std::vector<std::uint8_t>& bytes,
                                     const std::string& source) {
    EventLogParseResult result;
    const auto decoded = decodeComtradeText(bytes);
    result.diagnostics.insert(result.diagnostics.end(),
                              decoded.diagnostics.begin(),
                              decoded.diagnostics.end());
    if (!decoded.ok) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.code = "log.decode";
        diagnostic.message = "No se pudo decodificar la bitácora CSV.";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    const auto lines = splitLines(decoded.text);
    const char delimiters[] = {';', ',', '\t', '|'};
    std::vector<std::vector<std::string>> table;
    std::vector<std::string> header;
    std::size_t header_index = 0;
    bool found = false;
    for (const char delimiter : delimiters) {
        std::vector<std::vector<std::string>> candidate;
        candidate.reserve(lines.size());
        for (const auto& line : lines) {
            if (!trimText(line).empty()) {
                candidate.push_back(splitDelimited(line, delimiter));
            }
        }
        for (std::size_t index = 0; index < candidate.size(); ++index) {
            const auto& row = candidate[index];
            bool has_date = false;
            bool has_time = false;
            bool has_name = false;
            for (const auto& field : row) {
                const std::string normalized = lowerText(trimText(field));
                has_date = has_date || normalized == "date";
                has_time = has_time || normalized == "time";
                has_name = has_name || normalized == "name" ||
                           normalized == "functionstructure" ||
                           normalized == "functionsstructure";
            }
            if (has_date && has_time && has_name) {
                table = std::move(candidate);
                header = row;
                header_index = index;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    if (!found) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.code = "log.header";
        diagnostic.message =
            "No se reconoció una fila de encabezados Date/Time/Name en los "
            "delimitadores admitidos (; , tabulador |).";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    const auto date_column = columnIndex(header, "date");
    const auto time_column = columnIndex(header, "time");
    const auto name_column = columnIndex(header, "name");
    const auto structure_column = columnIndex(header, "functionstructure")
                                      ? columnIndex(header, "functionstructure")
                                      : columnIndex(header, "functionsstructure");
    const auto value_column = columnIndex(header, "value");
    std::size_t skipped = 0;
    for (std::size_t index = header_index + 1; index < table.size(); ++index) {
        const auto& row = table[index];
        const std::string date = cell(row, date_column);
        const std::string time = cell(row, time_column);
        if (date.empty() || time.empty()) {
            ++skipped;
            continue;
        }
        const auto parsed = parseComtradeDateTime(date + "," + time);
        if (!parsed.ok) {
            ++skipped;
            continue;
        }
        EventLogRow event;
        event.source = source;
        event.when_text = date + " " + time;
        event.when = parsed.value;
        event.when_valid = true;
        event.structure = cell(row, structure_column);
        event.signal = cell(row, name_column);
        event.value = cell(row, value_column);
        result.rows.push_back(std::move(event));
    }
    if (skipped != 0) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "log.rows_skipped";
        diagnostic.message = std::to_string(skipped) +
                             " fila(s) de bitácora se omitieron por fecha/hora "
                             "vacía o no interpretable.";
        result.diagnostics.push_back(std::move(diagnostic));
    }
    if (result.rows.empty()) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "log.empty";
        diagnostic.message =
            "La bitácora no contiene eventos con fecha/hora interpretable.";
        result.diagnostics.push_back(std::move(diagnostic));
    }
    return result;
}

std::optional<std::string> selectFaultDistanceText(
    const std::vector<EventLogRow>& rows) {
    std::optional<std::string> selected;
    int selected_score = 0;
    for (const auto& row : rows) {
        const std::string signal = lowerText(trimText(row.signal));
        if (signal.find("fault distance") == std::string::npos ||
            !analysis::parseFaultDistanceKm(row.value).has_value()) {
            continue;
        }
        const int score = signal == "fault distance" ? 2 : 1;
        if (score > selected_score) {
            selected = row.value;
            selected_score = score;
        }
    }
    return selected;
}

TimelineResult buildTimeline(const ComtradeRecord& record,
                             const analysis::RecordAnalysis* analysis,
                             std::vector<EventLogRow>& log,
                             double clock_offset_s) {
    TimelineResult result;
    if (record.data.times_s.empty()) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.code = "timeline.no_samples";
        diagnostic.message = "No hay muestras DAT para construir la línea de tiempo.";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    const double reference = analysisReference(record);
    double trigger = 0.0;
    const bool trigger_valid = validTriggerReference(record, trigger);
    if (!std::isfinite(clock_offset_s)) {
        clock_offset_s = 0.0;
    }
    if (clock_offset_s != 0.0) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Info;
        diagnostic.code = "timeline.clock_offset";
        diagnostic.message =
            "Se aplicó una corrección manual de reloj de " +
            formatFull(clock_offset_s) +
            " s solo a la bitácora; no se modificó el COMTRADE.";
        result.diagnostics.push_back(std::move(diagnostic));
    }
    if (analysis && analysis->event) {
        TimelineRow row;
        row.seconds_from_reference = analysis->event->time_s - reference;
        row.source = "COMTRADE";
        row.event = "Inicio RMS candidato";
        row.detail = "Cambio " + analysis->event->direction_text +
                     "; no es tiempo de operación de protección.";
        result.rows.push_back(std::move(row));
        if (analysis->event->recovery_detected &&
            analysis->event->recovery_s.has_value()) {
            TimelineRow recovery;
            recovery.seconds_from_reference =
                *analysis->event->recovery_s - reference;
            recovery.source = "COMTRADE";
            recovery.event = "Recuperación candidata";
            recovery.detail = "Referencia automática aproximada; confirmar en trazas.";
            result.rows.push_back(std::move(recovery));
        }
    }
    for (std::size_t channel = 0; channel < record.cfg.digital.size();
         ++channel) {
        for (std::size_t sample = 1; sample < record.data.sampleCount();
             ++sample) {
            const std::uint8_t previous =
                record.data.digitalValue(sample - 1, channel);
            const std::uint8_t current =
                record.data.digitalValue(sample, channel);
            if (previous == current) {
                continue;
            }
            TimelineRow row;
            row.seconds_from_reference =
                record.data.times_s[sample] - reference;
            row.source = "COMTRADE digital";
            row.event = record.cfg.digital[channel].name.empty()
                            ? "Digital " + std::to_string(channel + 1)
                            : record.cfg.digital[channel].name;
            row.detail = "Estado " + std::to_string(current) + ".";
            result.rows.push_back(std::move(row));
        }
    }
    const double capture_lo = record.data.times_s.front() - reference;
    const double capture_hi = record.data.times_s.back() - reference;
    for (auto& row : log) {
        alignEventRow(record, row);
        if (!row.seconds_from_trigger.has_value()) {
            ++result.excluded_log_events;
            continue;
        }
        const double adjusted = *row.seconds_from_trigger + clock_offset_s;
        if (adjusted < capture_lo || adjusted > capture_hi) {
            ++result.excluded_log_events;
            continue;
        }
        TimelineRow timeline;
        timeline.seconds_from_reference = adjusted;
        timeline.source = row.source.empty() ? "Bitácora" : row.source;
        timeline.event = row.signal.empty() ? "Evento de bitácora" : row.signal;
        timeline.detail = row.value;
        if (!row.structure.empty()) {
            timeline.detail += " · " + row.structure;
        }
        result.rows.push_back(std::move(timeline));
    }
    std::sort(result.rows.begin(), result.rows.end(),
              [](const TimelineRow& left, const TimelineRow& right) {
                  return left.seconds_from_reference <
                         right.seconds_from_reference;
              });
    if (!trigger_valid && !log.empty()) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "timeline.trigger_invalid";
        diagnostic.message =
            "El trigger CFG no es válido o cae fuera del DAT; los eventos de "
            "bitácora no se pueden alinear respecto del trigger y se excluyen.";
        result.diagnostics.push_back(std::move(diagnostic));
    }
    if (result.excluded_log_events != 0) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "timeline.log_excluded";
        diagnostic.message = std::to_string(result.excluded_log_events) +
                             " evento(s) de bitácora quedaron fuera de la "
                             "captura o sin trigger válido; no se incluyeron.";
        result.diagnostics.push_back(std::move(diagnostic));
    }
    return result;
}

TrendResult rollingTrend(
    const ComtradeRecord& record,
    const std::array<std::optional<std::size_t>, 3>& phase_indices,
    int cycles,
    std::size_t max_points) {
    TrendResult result;
    result.cycles = std::max(1, cycles);
    std::vector<std::size_t> indices;
    for (const auto& index : phase_indices) {
        if (index.has_value()) {
            if (*index >= record.cfg.analog.size() ||
                *index >= record.data.analog_channels) {
                Diagnostic diagnostic;
                diagnostic.severity = DiagnosticSeverity::Warning;
                diagnostic.code = "trend.phase_range";
                diagnostic.message =
                    "La tendencia recibió un índice de fase fuera de rango.";
                result.diagnostics.push_back(std::move(diagnostic));
                return result;
            }
            indices.push_back(*index);
        }
    }
    if (indices.size() < 3) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "trend.phases";
        diagnostic.message =
            "La tendencia fasorial requiere tres canales A/B/C seleccionados.";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    if (record.cfg.nominal_frequency_hz <= 0.0 ||
        record.data.times_s.size() < 8) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "trend.unavailable";
        diagnostic.message =
            "No hay frecuencia nominal o muestras suficientes para la tendencia.";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    result.unit = record.cfg.analog[indices.front()].unit;
    const double frequency = record.cfg.nominal_frequency_hz;
    const double half = static_cast<double>(result.cycles) / (2.0 * frequency);
    const double first = record.data.times_s.front();
    const double last = record.data.times_s.back();
    std::vector<std::size_t> candidates;
    for (std::size_t index = 0; index < record.data.times_s.size(); ++index) {
        const double time = record.data.times_s[index];
        if (time >= first + half && time <= last - half) {
            candidates.push_back(index);
        }
    }
    if (candidates.empty()) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "trend.no_windows";
        diagnostic.message =
            "No hay ventanas completas de un ciclo dentro del registro.";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    const std::size_t limit = std::max<std::size_t>(2, max_points);
    if (candidates.size() > limit) {
        std::vector<std::size_t> sampled;
        sampled.reserve(limit);
        for (std::size_t index = 0; index < limit; ++index) {
            const std::size_t position =
                static_cast<std::size_t>(
                    (static_cast<double>(index) *
                     static_cast<double>(candidates.size() - 1)) /
                    static_cast<double>(limit - 1));
            sampled.push_back(candidates[position]);
        }
        candidates = std::move(sampled);
    }
    for (const std::size_t sample : candidates) {
        const auto phasors = analysis::windowPhasors(
            record.data, indices, frequency, record.data.times_s[sample],
            result.cycles);
        if (!phasors.has_value() || phasors->size() < 3) {
            continue;
        }
        const auto sequences = analysis::symmetricalComponents(*phasors);
        if (!sequences.has_value()) {
            continue;
        }
        TrendPoint point;
        point.time_s = record.data.times_s[sample];
        for (std::size_t slot = 0; slot < 3; ++slot) {
            point.phase_rms[slot] = std::abs((*phasors)[slot]);
        }
        point.sequence_rms[0] = std::abs(sequences->zero);
        point.sequence_rms[1] = std::abs(sequences->positive);
        point.sequence_rms[2] = std::abs(sequences->negative);
        result.points.push_back(point);
    }
    if (result.points.empty()) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "trend.no_points";
        diagnostic.message =
            "Las ventanas móviles no produjeron fasores utilizables.";
        result.diagnostics.push_back(std::move(diagnostic));
    }
    return result;
}

namespace {

                                                                               
                                                                          
bool validateComparisonSelection(
    const ComtradeRecord& record,
    const std::array<std::optional<std::size_t>, 3>& selection,
    std::string& reason) {
    std::set<std::size_t> seen;
    std::size_t count = 0;
    for (const auto& index : selection) {
        if (!index.has_value()) {
            continue;
        }
        ++count;
        if (*index >= record.cfg.analog.size() ||
            *index >= record.data.analog_channels) {
            reason = "índice de canal fuera de rango";
            return false;
        }
        if (!seen.insert(*index).second) {
            reason = "el mismo canal se reutiliza en dos fases";
            return false;
        }
    }
    if (count != 3) {
        reason = "la selección A/B/C está incompleta";
        return false;
    }
    return true;
}

std::array<std::optional<std::size_t>, 3> resolveComparisonSelection(
    const ComtradeRecord& record,
    const analysis::RecordAnalysis* target_analysis,
    const std::array<std::optional<std::size_t>, 3>& requested,
    bool voltage) {
    bool any_requested = false;
    for (const auto& index : requested) {
        if (index.has_value()) {
            any_requested = true;
            break;
        }
    }
    if (any_requested) {
        return requested;
    }
    if (target_analysis) {
        return voltage ? target_analysis->voltage.selection.index
                       : target_analysis->phases.index;
    }
    if (voltage) {
        return analysis::selectVoltageChannels(record.cfg).index;
    }
    return analysis::selectPhaseChannels(record.cfg).index;
}

}              

ComparisonResult compareRecords(
    const std::string& current_label,
    const ComtradeRecord& current,
    const analysis::RecordAnalysis& current_analysis,
    const std::vector<ComparisonTarget>& others,
    int cycles) {
    ComparisonResult result;
    double current_trigger = 0.0;
    if (!validTriggerReference(current, current_trigger)) {
        result.warnings.push_back(
            "El trigger del registro actual no es válido o cae fuera del DAT; "
            "la comparación queda bloqueada para no alinear el centro "
            "provisional con triggers de otros registros.");
        return result;
    }
    if (!current_analysis.phases.complete()) {
        result.warnings.push_back(
            current_label + ": la selección A/B/C del registro actual está "
            "incompleta; las filas de corriente pueden faltar y las secuencias "
            "no son interpretables.");
    }
    bool current_voltage_partial = false;
    for (const auto& index : current_analysis.voltage.selection.index) {
        current_voltage_partial = current_voltage_partial || index.has_value();
    }
    if (current_voltage_partial && !current_analysis.voltage.selection.complete()) {
        result.warnings.push_back(
            current_label + ": la selección de tensión del registro actual está "
            "incompleta; no se comparan fasores de tensión de ese registro.");
    }
    const int window_cycles = std::max(1, cycles);
    const double pre_offset =
        current_analysis.prefalla_center_s - current_trigger;
    const double fault_offset =
        current_analysis.falla_center_s - current_trigger;
    const double current_frequency =
        std::isfinite(current_analysis.frequency_hz) &&
                current_analysis.frequency_hz > 0.0
            ? current_analysis.frequency_hz
            : current.cfg.nominal_frequency_hz;

    const auto append_rows =
        [&](const std::string& label, const CfgInfo& cfg,
            const std::array<std::optional<std::size_t>, 3>& selection,
            const char* quantity, const char* window,
            const std::array<std::optional<double>, 3>& magnitudes,
            const std::array<std::optional<double>, 3>& angles,
            double frequency) {
            for (std::size_t slot = 0; slot < 3; ++slot) {
                if (!magnitudes[slot].has_value() && !angles[slot].has_value()) {
                    continue;
                }
                ComparisonRow row;
                row.record = label;
                row.window = window;
                row.phase = phaseName(slot);
                row.quantity = quantity;
                row.frequency_hz = frequency;
                row.magnitude = magnitudes[slot].value_or(0.0);
                row.angle_deg = angles[slot].value_or(0.0);
                row.unit = selection[slot].has_value() &&
                                   *selection[slot] < cfg.analog.size()
                               ? cfg.analog[*selection[slot]].unit
                               : std::string();
                row.finite = magnitudes[slot].has_value() &&
                             angles[slot].has_value() &&
                             std::isfinite(row.magnitude) &&
                             std::isfinite(row.angle_deg);
                result.rows.push_back(std::move(row));
            }
        };

    const auto append_current_phasors =
        [&](const char* quantity, const char* window,
            const std::array<std::optional<std::size_t>, 3>& selection,
            const std::optional<std::array<analysis::Phasor, 3>>& values) {
            if (!values) {
                return;
            }
            std::array<std::optional<double>, 3> magnitudes;
            std::array<std::optional<double>, 3> angles;
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const auto& phasor = (*values)[slot];
                if (!phasor.isFinite()) {
                    continue;
                }
                magnitudes[slot] = phasor.magnitude();
                angles[slot] = phasor.angleDegrees();
            }
            append_rows(current_label, current.cfg, selection, quantity, window,
                        magnitudes, angles, current_frequency);
        };

    append_current_phasors("Corriente", "Prefalla",
                           current_analysis.phases.index,
                           current_analysis.prefalla_phasors);
    append_current_phasors("Corriente", "Falla",
                           current_analysis.phases.index,
                           current_analysis.falla_phasors);
    append_current_phasors("Tensión", "Prefalla",
                           current_analysis.voltage.selection.index,
                           current_analysis.voltage.prefalla_phasors);
    append_current_phasors("Tensión", "Falla",
                           current_analysis.voltage.selection.index,
                           current_analysis.voltage.falla_phasors);

    for (const auto& target : others) {
        const std::string label =
            target.label.empty() ? std::string("(registro sin etiqueta)")
                                 : target.label;
        const ComtradeRecord* record = target.record;
        if (!record) {
            result.warnings.push_back(label + ": registro no disponible.");
            continue;
        }
        double trigger = 0.0;
        if (!validTriggerReference(*record, trigger)) {
            result.warnings.push_back(
                label + ": el trigger CFG no es válido o cae fuera del DAT; se "
                        "excluyó de la comparación.");
            continue;
        }
        const double frequency =
            target.analysis && std::isfinite(target.analysis->frequency_hz) &&
                    target.analysis->frequency_hz > 0.0
                ? target.analysis->frequency_hz
                : record->cfg.nominal_frequency_hz;
        if (!std::isfinite(frequency) || frequency <= 0.0) {
            result.warnings.push_back(
                label + ": no hay frecuencia nominal utilizable; se excluyó de "
                        "la comparación.");
            continue;
        }
        if (std::abs(frequency - current_frequency) > 1e-9) {
            result.warnings.push_back(
                label + ": frecuencia nominal distinta (" +
                formatFull(frequency) + " Hz frente a " +
                formatFull(current_frequency) +
                " Hz). Cada fasor se calcula a la frecuencia de su CFG y no se "
                "reinterpola.");
        }

        const auto phase_selection = resolveComparisonSelection(
            *record, target.analysis, target.phase_indices, false);
        std::string phase_reason;
        const bool phase_ok = validateComparisonSelection(
            *record, phase_selection, phase_reason);
        if (!phase_ok) {
            result.warnings.push_back(label + ": corriente: " + phase_reason +
                                      "; no se agregaron filas de corriente.");
        }
        const auto voltage_selection = resolveComparisonSelection(
            *record, target.analysis, target.voltage_indices, true);
        bool any_voltage = false;
        for (const auto& index : voltage_selection) {
            if (index.has_value()) {
                any_voltage = true;
                break;
            }
        }
        std::string voltage_reason;
        const bool voltage_ok =
            any_voltage &&
            validateComparisonSelection(*record, voltage_selection,
                                        voltage_reason);
        if (any_voltage && !voltage_ok) {
            result.warnings.push_back(label + ": tensión: " + voltage_reason +
                                      "; no se agregaron filas de tensión.");
        }

        std::vector<std::size_t> phase_indices;
        if (phase_ok) {
            for (const auto& index : phase_selection) {
                phase_indices.push_back(*index);
            }
        }
        std::vector<std::size_t> voltage_indices;
        if (voltage_ok) {
            for (const auto& index : voltage_selection) {
                voltage_indices.push_back(*index);
            }
        }

        const std::pair<const char*, double> windows[] = {
            {"Prefalla", pre_offset}, {"Falla", fault_offset}};
        for (const auto& window : windows) {
            if (phase_ok) {
                const auto phasors = analysis::windowPhasors(
                    record->data, phase_indices, frequency,
                    trigger + window.second, window_cycles);
                if (!phasors.has_value() || phasors->size() < 3) {
                    result.warnings.push_back(
                        label + ": la ventana " + window.first +
                        " de corriente queda fuera del registro o no produce "
                        "fasores resolubles.");
                } else {
                    std::array<std::optional<double>, 3> magnitudes;
                    std::array<std::optional<double>, 3> angles;
                    for (std::size_t slot = 0; slot < 3; ++slot) {
                        const auto& value = (*phasors)[slot];
                        if (!std::isfinite(value.real()) ||
                            !std::isfinite(value.imag())) {
                            continue;
                        }
                        magnitudes[slot] = std::abs(value);
                        angles[slot] =
                            std::arg(value) * 180.0 / analysis::kPi;
                    }
                    append_rows(label, record->cfg, phase_selection,
                                "Corriente", window.first, magnitudes, angles,
                                frequency);
                }
            }
            if (voltage_ok) {
                const auto phasors = analysis::windowPhasors(
                    record->data, voltage_indices, frequency,
                    trigger + window.second, window_cycles);
                if (!phasors.has_value() || phasors->size() < 3) {
                    result.warnings.push_back(
                        label + ": la ventana " + window.first +
                        " de tensión queda fuera del registro o no produce "
                        "fasores resolubles.");
                } else {
                    std::array<std::optional<double>, 3> magnitudes;
                    std::array<std::optional<double>, 3> angles;
                    for (std::size_t slot = 0; slot < 3; ++slot) {
                        const auto& value = (*phasors)[slot];
                        if (!std::isfinite(value.real()) ||
                            !std::isfinite(value.imag())) {
                            continue;
                        }
                        magnitudes[slot] = std::abs(value);
                        angles[slot] =
                            std::arg(value) * 180.0 / analysis::kPi;
                    }
                    append_rows(label, record->cfg, voltage_selection, "Tensión",
                                window.first, magnitudes, angles, frequency);
                }
            }
        }
    }
    for (const auto& warning : result.warnings) {
        Diagnostic diagnostic;
        diagnostic.severity = DiagnosticSeverity::Warning;
        diagnostic.code = "comparison.warning";
        diagnostic.message = warning;
        result.diagnostics.push_back(std::move(diagnostic));
    }
    return result;
}

ComparisonResult compareRecords(
    const std::string& current_label,
    const ComtradeRecord& current,
    const analysis::RecordAnalysis& current_analysis,
    const std::vector<std::pair<std::string, const ComtradeRecord*>>& others,
    int cycles) {
                                                                                 
                                                                          
    std::vector<ComparisonTarget> targets;
    targets.reserve(others.size());
    for (const auto& item : others) {
        ComparisonTarget target;
        target.label = item.first;
        target.record = item.second;
        targets.push_back(std::move(target));
    }
    return compareRecords(current_label, current, current_analysis, targets,
                          cycles);
}

Result writeBatchHtml(const std::filesystem::path& path,
                      const std::vector<BatchItem>& items,
                      const std::vector<std::string>& failures,
                      int cycles,
                      const ComparisonResult* comparison) {
    std::string html;
    html.reserve(64 * 1024);
    html += "<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
            "<title>Comparación de registros COMTRADE</title><style>"
            "body{margin:0;background:#f4f7fa;color:#18212f;font:15px/1.55 "
            "'Segoe UI',Tahoma,Arial,sans-serif}main{max-width:1180px;margin:0 "
            "auto;padding:28px 22px 60px}.hero{background:linear-gradient(135deg,"
            "#0b2d52,#0b5cad);color:#fff;border-radius:14px;padding:26px 30px}"
            "section{background:#fff;border:1px solid #d8e0e9;border-radius:12px;"
            "padding:20px 22px;margin:16px 0}h2{font-size:21px;margin:0 0 12px}"
            "h3{font-size:17px;margin:20px 0 8px}"
            "table{width:100%;border-collapse:collapse;font-size:13px}"
            "th,td{text-align:left;padding:8px 9px;border-bottom:1px solid #d8e0e9}"
            "thead th{background:#f4f7fa;font-size:12px;text-transform:uppercase}"
            ".note{color:#5d6b7d;font-size:13px}.warn{border-left:4px solid #9a5b00;"
            "background:#fff8e8;padding:10px 14px}.ok{color:#166534;font-weight:600}"
            "footer{color:#5d6b7d;font-size:12px;text-align:center;margin-top:24px}"
            "</style></head><body><main><header class=\"hero\"><h1>Comparación de "
            "registros COMTRADE</h1><p>Análisis por lotes con lectura gerencial y "
            "detalle técnico. Herramienta de apoyo; no dictamina fallas ni "
            "operación de protecciones.</p></header>";

    const auto phase_selection_text =
        [](const BatchItem& item) -> std::string {
        const CfgInfo* cfg = item.record ? &item.record->cfg : nullptr;
        std::string text;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            if (slot != 0) {
                text += " · ";
            }
            text += phaseName(slot);
            text += "=";
            const auto& index = item.phase_indices[slot];
            if (index.has_value() && cfg && *index < cfg->analog.size()) {
                const std::string name = cfg->analog[*index].name.empty()
                                             ? "Canal " + formatCount(*index + 1)
                                             : cfg->analog[*index].name;
                text += name + " (#" + formatCount(*index + 1) + ")";
            } else if (item.analysis && item.analysis->phases.index[slot]
                                              .has_value()) {
                const std::size_t fallback =
                    *item.analysis->phases.index[slot];
                const std::string name =
                    cfg && fallback < cfg->analog.size() &&
                            !cfg->analog[fallback].name.empty()
                        ? cfg->analog[fallback].name
                        : "Canal " + formatCount(fallback + 1);
                text += name + " (#" + formatCount(fallback + 1) + ")";
            } else {
                text += "—";
            }
        }
        return text;
    };

    std::size_t event_count = 0;
    std::size_t voltage_count = 0;
    for (const auto& item : items) {
        if (item.analysis && item.analysis->event) {
            ++event_count;
        }
        if (item.analysis && item.analysis->voltage.available) {
            ++voltage_count;
        }
    }

    ComparisonResult calculated;
    const ComparisonResult* effective = comparison;
    if (!effective) {
        const BatchItem* current_item = nullptr;
        for (const auto& item : items) {
            if (item.record && item.analysis &&
                item.analysis->time.trigger_within_capture) {
                current_item = &item;
                break;
            }
        }
        if (current_item) {
            std::vector<ComparisonTarget> targets;
            for (const auto& item : items) {
                if (&item == current_item || !item.record) {
                    continue;
                }
                ComparisonTarget target;
                target.label = item.label;
                target.record = item.record;
                target.analysis = item.analysis;
                target.phase_indices = item.phase_indices;
                target.voltage_indices = item.voltage_indices;
                targets.push_back(std::move(target));
            }
            calculated = compareRecords(current_item->label,
                                        *current_item->record,
                                        *current_item->analysis, targets,
                                        cycles);
            effective = &calculated;
        }
    }
    const std::size_t comparison_rows =
        effective ? effective->rows.size() : 0;
    const std::size_t comparison_warnings =
        effective ? effective->warnings.size() : 0;
    const bool comparison_complete =
        effective != nullptr &&
        (comparison_rows > 0 || comparison_warnings > 0);

    html += "<section><h2>Resumen para gestión</h2><p>Se analizaron <strong>" +
            formatCount(items.size()) +
            "</strong> registro(s) y <strong>" + formatCount(failures.size()) +
            "</strong> archivo(s) o par(es) no se pudieron procesar. Se detectó "
            "un cambio RMS candidato en <strong>" +
            formatCount(event_count) +
            "</strong> registro(s); <strong>" + formatCount(voltage_count) +
            "</strong> tiene(n) análisis de tensión disponible.</p>";
    html += "<p>La comparación contiene <strong>" +
            formatCount(comparison_rows) + "</strong> fila(s) y <strong>" +
            formatCount(comparison_warnings) +
            "</strong> advertencia(s).</p>";
    html += "<p class=\"note\">Un cambio candidato solo indica una señal para "
            "revisar. Este informe no afirma que exista una falla, no identifica "
            "su causa ni demuestra operación o falla de protecciones. La ausencia "
            "de cambio tampoco descarta un evento. Las magnitudes mantienen la "
            "unidad y escala declaradas en cada CFG; no se aplican relaciones "
            "primario/secundario ni conversiones P/S.</p>";
    html += "<table><thead><tr><th>Registro</th><th>Estación</th><th>Muestras</th>"
            "<th>Trigger válido</th><th>Inicio candidato (s)</th><th>Contraste</th>"
            "<th>THD máx (%)</th><th>Fases usadas (A/B/C)</th>"
            "<th>Advertencias</th></tr></thead><tbody>";
    for (const auto& item : items) {
        const auto* record = item.record;
        const auto* analysis = item.analysis;
        double max_thd = 0.0;
        bool has_thd = false;
        if (analysis) {
            for (const auto& fit : analysis->harmonics) {
                if (fit.thd_pct.has_value() && std::isfinite(*fit.thd_pct)) {
                    max_thd = std::max(max_thd, *fit.thd_pct);
                    has_thd = true;
                }
            }
        }
        html += "<tr><td>" + htmlEscape(item.label) + "</td><td>" +
                htmlEscape(record ? record->cfg.station_name : std::string()) +
                "</td><td>" +
                formatCount(record ? record->data.sampleCount() : 0) +
                "</td><td>" +
                (analysis && analysis->time.trigger_within_capture ? "Sí" : "No") +
                "</td><td>" +
                (analysis && analysis->event
                     ? htmlEscape(displayNumber(analysis->event->time_s, 8))
                     : "—") +
                "</td><td>" +
                (analysis && analysis->event
                     ? htmlEscape(displayNumber(analysis->event->contrast, 5))
                     : "—") +
                "</td><td>" +
                (has_thd ? htmlEscape(displayNumber(max_thd, 5)) : "—") +
                "</td><td>" + htmlEscape(phase_selection_text(item)) +
                "</td><td>" +
                formatCount(item.diagnostics.size()) + "</td></tr>";
    }
    html += "</tbody></table></section>";
    if (!failures.empty()) {
        html += "<section class=\"warn\"><h2>Registros no procesados</h2><ul>";
        for (const auto& failure : failures) {
            html += "<li>" + htmlEscape(failure) + "</li>";
        }
        html += "</ul></section>";
    }
    html += "<section><h2>Detalle técnico por registro</h2>";
    for (const auto& item : items) {
        html += "<h3>" + htmlEscape(item.label) + "</h3>";
        if (!item.record || !item.analysis) {
            html += "<p class=\"note\">Sin análisis disponible.</p>";
            continue;
        }
        html += "<p class=\"note\">" + htmlEscape(item.source) + "</p><ul>";
        html += "<li>Trigger válido: " +
                std::string(item.analysis->time.trigger_within_capture ? "sí"
                                                                       : "no") +
                "; referencia: " +
                htmlEscape(item.analysis->time.reference_label) + ".</li>";
        html += "<li>Fases de corriente usadas: " +
                htmlEscape(phase_selection_text(item)) + ".</li>";
        if (item.analysis->event) {
            html += "<li>Inicio candidato: " +
                    htmlEscape(displayNumber(item.analysis->event->time_s, 8)) +
                    " s; dirección: " +
                    htmlEscape(item.analysis->event->direction_text) +
                    "; contraste: " +
                    htmlEscape(displayNumber(item.analysis->event->contrast, 5)) +
                    "×.</li>";
        } else {
            html += "<li>No se detectó un cambio RMS persistente con el umbral "
                    "automático.</li>";
        }
        if (item.analysis->classification) {
            html += "<li>Lectura orientativa: " +
                    htmlEscape(item.analysis->classification->label) + ".</li>";
        }
        if (!item.analysis->voltage.available) {
            html += "<li>Tensión: no hay tres canales de tensión identificados; "
                    "no se inventaron magnitudes.</li>";
        } else {
            html += "<li>Tensión: unidad ";
            html += htmlEscape(normalizedUnit(item.analysis->voltage.unit));
            html += item.analysis->voltage.units_consistent
                        ? " (unidades consistentes)"
                        : " (unidades no consistentes; revisar CFG)";
            html += ".</li>";
        }
        for (const auto& diagnostic : item.diagnostics) {
            if (diagnostic.severity == DiagnosticSeverity::Warning ||
                diagnostic.severity == DiagnosticSeverity::Error) {
                html += "<li><strong>" +
                        htmlEscape(severityLabel(diagnostic.severity)) + "</strong>: " +
                        htmlEscape(diagnostic.message) + "</li>";
            }
        }
        html += "</ul>";
    }
    html += "</section>";

    if (!comparison_complete) {
        html += "<section><h2>Comparación de fasores relativa al trigger</h2>"
                "<p class=\"note\">No hay una comparación disponible. Se "
                "requiere un registro actual con trigger CFG válido y al menos "
                "otro registro comparable.</p></section>";
    } else {
        html += "<section><h2>Comparación de fasores relativa al trigger</h2>"
                "<p class=\"note\">Mismas ventanas desplazadas desde el trigger "
                "de cada registro. Las unidades CFG y las frecuencias se "
                "conservan; no se convierten bases P/S.</p>";
        if (!effective->rows.empty()) {
            html += "<table><thead><tr><th>Registro</th><th>Cantidad</th>"
                    "<th>Ventana</th><th>Fase</th><th>Hz</th>"
                    "<th>Magnitud RMS</th><th>Ángulo (°)</th><th>Unidad</th>"
                    "</tr></thead><tbody>";
            for (const auto& row : effective->rows) {
                const std::string quantity =
                    row.quantity.empty() ? std::string("Corriente")
                                         : row.quantity;
                html += "<tr><td>" + htmlEscape(row.record) + "</td><td>" +
                        htmlEscape(quantity) + "</td><td>" +
                        htmlEscape(row.window) + "</td><td>" +
                        htmlEscape(row.phase) + "</td><td>" +
                        htmlEscape(displayNumber(row.frequency_hz, 6)) +
                        "</td><td>" +
                        (row.finite ? htmlEscape(displayNumber(row.magnitude, 8))
                                    : "—") +
                        "</td><td>" +
                        (row.finite ? htmlEscape(displayNumber(row.angle_deg, 5))
                                    : "—") +
                        "</td><td>" + htmlEscape(row.unit) + "</td></tr>";
            }
            html += "</tbody></table>";
        } else {
            html += "<p class=\"note\">La comparación no produjo filas "
                    "utilizables; revise las advertencias.</p>";
        }
        html += "</section>";
        if (!effective->warnings.empty()) {
            html += "<section class=\"warn\"><h2>Advertencias de comparación</h2><ul>";
            for (const auto& warning : effective->warnings) {
                html += "<li>" + htmlEscape(warning) + "</li>";
            }
            html += "</ul></section>";
        }
    }
    html += "<footer>Generado por Analizador COMTRADE nativo · offline · sin "
            "recursos remotos</footer></main></body></html>";
    return writeAll(path, html);
}

namespace {

std::string buildSampleCsv(const Context& context) {
    const ComtradeRecord& record = *context.record;
    const auto& data = record.data;
    double trigger_reference = 0.0;
    bool has_trigger = false;
    if (record.trigger_offset_s && !data.times_s.empty() &&
        *record.trigger_offset_s >= data.times_s.front() &&
        *record.trigger_offset_s <= data.times_s.back()) {
        trigger_reference = *record.trigger_offset_s;
        has_trigger = true;
    }
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "numero_muestra");
    out += ',';
    appendCsvField(out, "tiempo_s");
    out += ',';
    appendCsvField(out, "tiempo_desde_trigger_s");
    for (std::size_t index = 0; index < record.cfg.analog.size(); ++index) {
        out += ',';
        std::string header = "A" + std::to_string(index + 1) + ": " +
                             record.cfg.analog[index].name;
        std::string unit = record.cfg.analog[index].unit;
        if (context.unit_overrides) {
            const auto found = context.unit_overrides->find(index);
            if (found != context.unit_overrides->end()) {
                unit = found->second;
            }
        }
        if (!unit.empty()) {
            header += " [" + unit + "]";
        }
        appendCsvField(out, header);
    }
    for (std::size_t index = 0; index < record.cfg.digital.size(); ++index) {
        out += ',';
        appendCsvField(out, "D" + std::to_string(index + 1) + ": " +
                                record.cfg.digital[index].name);
    }
    out += "\r\n";
    for (std::size_t sample = 0; sample < data.sampleCount(); ++sample) {
        appendCsvField(out, std::to_string(
                                  sample < data.sample_numbers.size()
                                      ? data.sample_numbers[sample]
                                      : 0));
        out += ',';
        appendCsvField(out, sample < data.times_s.size()
                                ? csvNumber(data.times_s[sample])
                                : std::string());
        out += ',';
        appendCsvField(out, has_trigger && sample < data.times_s.size()
                                ? csvNumber(data.times_s[sample] -
                                            trigger_reference)
                                : std::string());
        for (std::size_t channel = 0; channel < record.cfg.analog.size();
             ++channel) {
            out += ',';
            appendCsvField(out, csvNumber(data.analogValue(sample, channel)));
        }
        for (std::size_t channel = 0; channel < record.cfg.digital.size();
             ++channel) {
            out += ',';
            appendCsvField(out, data.digitalValue(sample, channel) ? "1" : "0");
        }
        out += "\r\n";
    }
    return out;
}

std::string batchCsv(const std::vector<BatchItem>& items,
                     const std::vector<std::string>& failures) {
    std::string out = "\xEF\xBB\xBF";
    appendCsvField(out, "Registro");
    out += ',';
    appendCsvField(out, "Estacion");
    out += ',';
    appendCsvField(out, "Muestras");
    out += ',';
    appendCsvField(out, "Trigger valido");
    out += ',';
    appendCsvField(out, "Inicio candidato s");
    out += ',';
    appendCsvField(out, "Contraste");
    out += ',';
    appendCsvField(out, "THD max %");
    out += ',';
    appendCsvField(out, "Advertencias");
    out += "\r\n";
    for (const auto& item : items) {
        double max_thd = 0.0;
        bool has_thd = false;
        if (item.analysis) {
            for (const auto& fit : item.analysis->harmonics) {
                if (fit.thd_pct.has_value() && std::isfinite(*fit.thd_pct)) {
                    max_thd = std::max(max_thd, *fit.thd_pct);
                    has_thd = true;
                }
            }
        }
        appendCsvField(out, item.label);
        out += ',';
        appendCsvField(out, item.record ? item.record->cfg.station_name : "");
        out += ',';
        appendCsvField(out, item.record
                                ? std::to_string(item.record->data.sampleCount())
                                : std::string());
        out += ',';
        appendCsvField(out, item.analysis &&
                                   item.analysis->time.trigger_within_capture
                               ? "si"
                               : "no");
        out += ',';
        appendCsvField(out, item.analysis && item.analysis->event
                                ? formatFull(item.analysis->event->time_s)
                                : std::string());
        out += ',';
        appendCsvField(out, item.analysis && item.analysis->event
                                ? formatFull(item.analysis->event->contrast)
                                : std::string());
        out += ',';
        appendCsvField(out, has_thd ? formatFull(max_thd) : std::string());
        out += ',';
        appendCsvField(out, std::to_string(item.diagnostics.size()));
        out += "\r\n";
    }
    for (const auto& failure : failures) {
        appendCsvField(out, "FALLO");
        out += ',';
        appendCsvField(out, failure);
        out += ",,,,,";
        out += "\r\n";
    }
    return out;
}

std::string batchJson(const std::vector<BatchItem>& items,
                      const std::vector<std::string>& failures) {
    std::string out = "{\n  \"record_count\": ";
    out += std::to_string(items.size());
    out += ",\n  \"failure_count\": ";
    out += std::to_string(failures.size());
    out += ",\n  \"records\": [";
    for (std::size_t index = 0; index < items.size(); ++index) {
        const auto& item = items[index];
        if (index != 0) {
            out += ",";
        }
        out += "\n    {\"label\": ";
        appendJsonString(out, item.label);
        out += ", \"station\": ";
        appendJsonString(out, item.record ? item.record->cfg.station_name : "");
        out += ", \"samples\": ";
        out += std::to_string(item.record ? item.record->data.sampleCount() : 0);
        out += ", \"trigger_valid\": ";
        out += item.analysis && item.analysis->time.trigger_within_capture
                   ? "true"
                   : "false";
        out += ", \"event\": ";
        if (item.analysis && item.analysis->event) {
            out += "{\"time_s\": ";
            out += jsonNumber(item.analysis->event->time_s);
            out += ", \"contrast\": ";
            out += jsonNumber(item.analysis->event->contrast);
            out += ", \"direction\": ";
            appendJsonString(out, item.analysis->event->direction_text);
            out += "}";
        } else {
            out += "null";
        }
        out += ", \"warnings\": " + std::to_string(item.diagnostics.size());
        out += "}";
    }
    out += "\n  ],\n  \"failures\": [";
    for (std::size_t index = 0; index < failures.size(); ++index) {
        if (index != 0) {
            out += ", ";
        }
        appendJsonString(out, failures[index]);
    }
    out += "]\n}\n";
    return out;
}

}              

Result writeAnalysisPackage(const std::filesystem::path& path,
                            const Context& context,
                            const Options& options,
                            const PackageData& package) {
    if (!context.record) {
        return {false, "No hay un registro cargado para generar el paquete."};
    }
    std::vector<ZipEntryData> entries;
    entries.push_back({"informe.html", buildHtml(context, options)});
    entries.push_back({"muestras.csv", buildSampleCsv(context)});
    entries.push_back({"resumen.json", buildJson(context)});
    entries.push_back({"fasores.csv", phasorCsv(context)});
    entries.push_back({"coherencia_ventanas.csv", coherenceCsv(context)});
    entries.push_back({"componentes_simetricas.csv", sequenceCsv(context)});
    entries.push_back({"armonicos_thd.csv", harmonicsCsv(context)});
    entries.push_back({"tension_fasores.csv", voltageCsv(context)});
    entries.push_back({"tension_secuencias.csv", voltageSequenceCsv(context)});
    entries.push_back({"espectro_fft.csv", spectrumCsv(context)});
    entries.push_back({"espectro_fft_bins.csv", spectrumBinsCsv(context)});
    entries.push_back({"saturacion_heuristica.csv", saturationCsv(context)});
    entries.push_back({"interruptor_tiempos.csv", breakerCsv(context)});
    if (package.trend) {
        entries.push_back({"tendencia_fasorial.csv", trendCsv(*package.trend)});
    }
    if (package.event_log) {
        entries.push_back({"eventos_alineados.csv",
                           eventLogCsv(*package.event_log)});
    }
    if (package.timeline) {
        entries.push_back({"linea_tiempo.csv",
                           timelineCsv(*package.timeline)});
    }
    if (package.comparison) {
        entries.push_back({"comparacion_fasores.csv",
                           comparisonCsv(*package.comparison)});
    }
    if (package.batch) {
        entries.push_back({"lote_resumen.csv",
                           batchCsv(*package.batch, package.batch_failures)});
        entries.push_back({"lote_resumen.json",
                           batchJson(*package.batch, package.batch_failures)});
    }
    const std::string zip = buildStoredZip(entries);
    return writeAll(path, zip);
}

std::wstring safeFileStem(std::wstring_view text) {
    std::wstring out;
    out.reserve(std::min<std::size_t>(text.size(), 80));
    for (const wchar_t raw : text) {
        if (out.size() >= 80) {
            break;
        }
        wchar_t c = raw;
        const bool invalid = c < 0x20 || c == L'<' || c == L'>' || c == L':' ||
                             c == L'"' || c == L'/' || c == L'\\' ||
                             c == L'|' || c == L'?' || c == L'*';
        if (invalid) {
            c = L'_';
        }
        out.push_back(c);
    }
    while (!out.empty() &&
           (out.back() == L' ' || out.back() == L'.' || out.back() == L'_')) {
        out.pop_back();
    }
    while (!out.empty() && (out.front() == L' ' || out.front() == L'_')) {
        out.erase(out.begin());
    }
    if (out.empty()) {
        return L"registro";
    }

    std::wstring upper = out;
    ::CharUpperBuffW(upper.data(), static_cast<DWORD>(upper.size()));
    static constexpr const wchar_t* kReserved[] = {
        L"CON",  L"PRN",  L"AUX",  L"NUL",  L"COM1", L"COM2", L"COM3",
        L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1",
        L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8",
        L"LPT9"};
    for (const wchar_t* reserved : kReserved) {
        if (upper == reserved) {
            out = L"_" + out;
            break;
        }
    }
    return out;
}

}                                 
