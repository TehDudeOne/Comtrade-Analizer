#include "analizador/comtrade.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace analizador {
namespace {

                                                                              
                                 
                                                                              

std::string_view trimView(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[first]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '\v') {
            break;
        }
        ++first;
    }
    std::size_t last = text.size();
    while (last > first) {
        const unsigned char c = static_cast<unsigned char>(text[last - 1]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f' && c != '\v') {
            break;
        }
        --last;
    }
    return text.substr(first, last - first);
}

std::string trim(std::string_view text) {
    const auto value = trimView(text);
    return std::string(value.begin(), value.end());
}

std::string upperAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c >= 'a' && c <= 'z') {
            out.push_back(static_cast<char>(c - 'a' + 'A'));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

                                                                         
                                                                              
                                                                          
std::vector<std::string> csvFields(std::string_view line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;
    bool at_field_start = true;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                current.push_back(c);
            }
            continue;
        }

        if (at_field_start && (c == ' ' || c == '\t')) {
            continue;
        }
        if (c == '"' && current.empty()) {
            in_quotes = true;
            at_field_start = false;
            continue;
        }
        if (c == ',') {
            fields.push_back(trim(current));
            current.clear();
            at_field_start = true;
            continue;
        }
        current.push_back(c);
        at_field_start = false;
    }
    fields.push_back(trim(current));
    return fields;
}

struct Line {
    std::string text;
    std::size_t number = 0;
};

struct LineSplit {
    std::vector<Line> lines;
    bool line_limit_exceeded = false;
    std::size_t line_limit_line = 0;
    bool line_too_long = false;
    std::size_t long_line = 0;
    std::size_t long_line_bytes = 0;
};

bool isLineSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

                                                                                   
                                                                            
                                                                                
                                                                              
                                                                                
                                                                      
LineSplit splitNonBlankLines(const std::string& text,
                             std::size_t max_lines = std::numeric_limits<std::size_t>::max(),
                             std::size_t max_line_bytes = std::numeric_limits<std::size_t>::max()) {
    LineSplit result;
    if (text.empty()) {
        return result;
    }
    std::size_t start = 0;
    std::size_t line_number = 0;
    std::size_t i = 0;
    while (i <= text.size()) {
        if (i == text.size() || text[i] == '\n' || text[i] == '\r') {
            if (i == text.size() && start == text.size()) {
                break;                                                                 
            }
            const std::size_t length = i - start;
            ++line_number;
            if (length > max_line_bytes) {
                result.line_too_long = true;
                result.long_line = line_number;
                result.long_line_bytes = length;
                return result;
            }
            std::size_t first = start;
            std::size_t last = i;
            while (first < last && isLineSpace(text[first])) {
                ++first;
            }
            while (last > first && isLineSpace(text[last - 1])) {
                --last;
            }
            if (last > first) {
                if (result.lines.size() >= max_lines) {
                    result.line_limit_exceeded = true;
                    result.line_limit_line = line_number;
                    return result;
                }
                result.lines.push_back(Line{text.substr(first, last - first), line_number});
            }
            if (i < text.size() && text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            start = i + 1;
        }
        ++i;
    }
    return result;
}

bool parseDouble(std::string_view text, double& value) {
    text = trimView(text);
    if (text.empty()) {
        return false;
    }
    const std::string upper = upperAscii(text);
    if (upper == "NAN" || upper == "+NAN" || upper == "-NAN") {
        value = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    if (upper == "INF" || upper == "+INF" || upper == "INFINITY" || upper == "+INFINITY") {
        value = std::numeric_limits<double>::infinity();
        return true;
    }
    if (upper == "-INF" || upper == "-INFINITY") {
        value = -std::numeric_limits<double>::infinity();
        return true;
    }
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty()) {
            return false;
        }
    }
                                                                              
                                                                             
    std::string owned;
    if (text.size() >= 3 && text[0] == '-' && text[1] == '.' &&
        text[2] >= '0' && text[2] <= '9') {
        owned = "-0";
        owned.append(text.substr(1));
        text = owned;
    } else if (text.size() >= 2 && text.front() == '.' &&
               text[1] >= '0' && text[1] <= '9') {
        owned = "0";
        owned.append(text);
        text = owned;
    }
    const char* first = text.data();
    const char* last = text.data() + text.size();
    double parsed = 0.0;
    const auto result = std::from_chars(first, last, parsed, std::chars_format::general);
    if (result.ec != std::errc() || result.ptr != last) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseSignedInteger(std::string_view text, std::int64_t& value) {
    text = trimView(text);
    if (text.empty()) {
        return false;
    }
    bool negative = false;
    if (text.front() == '+') {
        text.remove_prefix(1);
    } else if (text.front() == '-') {
        negative = true;
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    std::uint64_t magnitude = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), magnitude);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return false;
    }
    if (negative) {
        if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ull) {
            return false;
        }
        if (magnitude == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1ull) {
            value = std::numeric_limits<std::int64_t>::min();
            return true;
        }
        value = -static_cast<std::int64_t>(magnitude);
        return true;
    }
    if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    value = static_cast<std::int64_t>(magnitude);
    return true;
}

bool parseUnsignedInteger(std::string_view text, std::uint64_t& value) {
    text = trimView(text);
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

bool isAllDigits(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

                                                                            
bool isPlainDecimal(std::string_view text) {
    text = trimView(text);
    if (text.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (text[i] == '+' || text[i] == '-') {
        ++i;
    }
    bool integer_digits = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        integer_digits = true;
        ++i;
    }
    bool fraction_digits = false;
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            fraction_digits = true;
            ++i;
        }
    }
    if (!integer_digits && !fraction_digits) {
        return false;
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        bool exponent_digits = false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            exponent_digits = true;
            ++i;
        }
        if (!exponent_digits) {
            return false;
        }
    }
    return i == text.size();
}

                                                                              
                
                                                                              

Diagnostic makeDiagnostic(DiagnosticSeverity severity, std::string code, std::string message) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    return diagnostic;
}

void addDiagnostic(std::vector<Diagnostic>& out,
                   std::size_t max_diagnostics,
                   Diagnostic diagnostic) {
    if (out.size() < max_diagnostics) {
        out.push_back(std::move(diagnostic));
    }
}

bool hasError(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& item) {
        return item.severity == DiagnosticSeverity::Error;
    });
}

                                                                              
                          
                                                                              

void appendUtf8(std::string& out, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

bool isValidUtf8(const std::uint8_t* data, std::size_t size) {
    std::size_t i = 0;
    while (i < size) {
        const std::uint8_t c = data[i];
        if (c <= 0x7F) {
            ++i;
            continue;
        }
        std::size_t extra = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if ((c & 0xE0) == 0xC0) {
            extra = 1;
            code_point = c & 0x1F;
            minimum = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            code_point = c & 0x0F;
            minimum = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            code_point = c & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (i + extra >= size) {
            return false;
        }
        for (std::size_t j = 1; j <= extra; ++j) {
            const std::uint8_t next = data[i + j];
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (next & 0x3F);
        }
        if (code_point < minimum || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

const char* cp1252Character(std::uint8_t byte) {
                                                                      
    static const char* const table[32] = {
        "\xE2\x82\xAC",             
        nullptr,                
        "\xE2\x80\x9A",             
        "\xC6\x92",                
        "\xE2\x80\x9E",             
        "\xE2\x80\xA6",             
        "\xE2\x80\xA0",             
        "\xE2\x80\xA1",             
        "\xCB\x86",                
        "\xE2\x80\xB0",             
        "\xC5\xA0",                
        "\xE2\x80\xB9",             
        "\xC5\x92",                
        nullptr,                
        "\xC5\xBD",                
        nullptr,                
        nullptr,                
        "\xE2\x80\x98",             
        "\xE2\x80\x99",             
        "\xE2\x80\x9C",             
        "\xE2\x80\x9D",             
        "\xE2\x80\xA2",             
        "\xE2\x80\x93",             
        "\xE2\x80\x94",             
        "\xCB\x9C",                
        "\xE2\x84\xA2",             
        "\xC5\xA1",                
        "\xE2\x80\xBA",             
        "\xC5\x93",                
        nullptr,                
        "\xC5\xBE",                
        "\xC5\xB8",                
    };
    return table[byte - 0x80];
}

std::string decodeCp1252(const std::vector<std::uint8_t>& raw,
                         std::size_t start,
                         bool& replacement_used) {
    std::string out;
    out.reserve(raw.size() - start);
    replacement_used = false;
    for (std::size_t i = start; i < raw.size(); ++i) {
        const std::uint8_t byte = raw[i];
        if (byte < 0x80) {
            out.push_back(static_cast<char>(byte));
        } else if (byte < 0xA0) {
            const char* mapped = cp1252Character(byte);
            if (mapped != nullptr) {
                out.append(mapped);
            } else {
                appendUtf8(out, 0xFFFD);
                replacement_used = true;
            }
        } else {
            appendUtf8(out, byte);
        }
    }
    return out;
}

}              

const char* textEncodingName(TextEncoding encoding) noexcept {
    switch (encoding) {
        case TextEncoding::Utf8:
            return "UTF-8";
        case TextEncoding::Utf8Bom:
            return "UTF-8 con BOM";
        case TextEncoding::Cp1252:
            return "CP1252";
        case TextEncoding::Utf16Le:
            return "UTF-16LE";
        case TextEncoding::Utf16Be:
            return "UTF-16BE";
    }
    return "desconocida";
}

const char* dataFileTypeName(DataFileType type) noexcept {
    switch (type) {
        case DataFileType::Ascii:
            return "ASCII";
        case DataFileType::Binary16:
            return "BINARY";
        case DataFileType::Binary32:
            return "BINARY32";
        case DataFileType::Float32:
            return "FLOAT32";
    }
    return "desconocido";
}

TextDecodeResult decodeComtradeText(const std::vector<std::uint8_t>& raw) {
    TextDecodeResult result;
    const std::size_t size = raw.size();
    if (size >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF) {
        if (isValidUtf8(raw.data() + 3, size - 3)) {
            result.text.assign(reinterpret_cast<const char*>(raw.data() + 3), size - 3);
            result.encoding = TextEncoding::Utf8Bom;
            result.ok = true;
        } else {
            bool replaced = false;
            result.text = decodeCp1252(raw, 3, replaced);
            result.encoding = TextEncoding::Cp1252;
            result.ok = true;
            auto diagnostic = makeDiagnostic(
                DiagnosticSeverity::Warning,
                "text.utf8_bom_invalid",
                "El archivo declara BOM UTF-8 pero contiene bytes inválidos; se interpretó como CP1252.");
            result.diagnostics.push_back(std::move(diagnostic));
        }
        auto diagnostic = makeDiagnostic(
            DiagnosticSeverity::Info,
            "text.utf8_bom",
            "Se detectó BOM UTF-8 y se eliminó del texto.");
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }

    if (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) {
        const bool big_endian = false;
        if ((size - 2) % 2 != 0) {
            result.encoding = TextEncoding::Utf16Le;
            result.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error,
                "text.utf16_odd_length",
                "El archivo UTF-16LE tiene una cantidad impar de bytes y está truncado."));
            return result;
        }
        bool bad_surrogate = false;
        for (std::size_t i = 2; i + 1 < size; i += 2) {
            const std::uint16_t unit = big_endian
                ? static_cast<std::uint16_t>((raw[i] << 8) | raw[i + 1])
                : static_cast<std::uint16_t>(raw[i] | (raw[i + 1] << 8));
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                if (i + 3 >= size) {
                    appendUtf8(result.text, 0xFFFD);
                    bad_surrogate = true;
                    break;
                }
                const std::uint16_t low = big_endian
                    ? static_cast<std::uint16_t>((raw[i + 2] << 8) | raw[i + 3])
                    : static_cast<std::uint16_t>(raw[i + 2] | (raw[i + 3] << 8));
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    const std::uint32_t code_point = 0x10000u +
                        ((static_cast<std::uint32_t>(unit) - 0xD800u) << 10) +
                        (static_cast<std::uint32_t>(low) - 0xDC00u);
                    appendUtf8(result.text, code_point);
                    i += 2;
                } else {
                    appendUtf8(result.text, 0xFFFD);
                    bad_surrogate = true;
                }
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                appendUtf8(result.text, 0xFFFD);
                bad_surrogate = true;
            } else {
                appendUtf8(result.text, unit);
            }
        }
        result.encoding = TextEncoding::Utf16Le;
        result.ok = true;
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info,
            "text.utf16le_bom",
            "Se detectó BOM UTF-16LE y se convirtió explícitamente a UTF-8."));
        if (bad_surrogate) {
            result.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning,
                "text.utf16_surrogate",
                "El texto UTF-16 contiene sustitutos no emparejados; se reemplazaron con U+FFFD."));
        }
        return result;
    }

    if (size >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        if ((size - 2) % 2 != 0) {
            result.encoding = TextEncoding::Utf16Be;
            result.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error,
                "text.utf16_odd_length",
                "El archivo UTF-16BE tiene una cantidad impar de bytes y está truncado."));
            return result;
        }
        bool bad_surrogate = false;
        for (std::size_t i = 2; i + 1 < size; i += 2) {
            const std::uint16_t unit = static_cast<std::uint16_t>((raw[i] << 8) | raw[i + 1]);
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                if (i + 3 >= size) {
                    appendUtf8(result.text, 0xFFFD);
                    bad_surrogate = true;
                    break;
                }
                const std::uint16_t low = static_cast<std::uint16_t>((raw[i + 2] << 8) | raw[i + 3]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    const std::uint32_t code_point = 0x10000u +
                        ((static_cast<std::uint32_t>(unit) - 0xD800u) << 10) +
                        (static_cast<std::uint32_t>(low) - 0xDC00u);
                    appendUtf8(result.text, code_point);
                    i += 2;
                } else {
                    appendUtf8(result.text, 0xFFFD);
                    bad_surrogate = true;
                }
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                appendUtf8(result.text, 0xFFFD);
                bad_surrogate = true;
            } else {
                appendUtf8(result.text, unit);
            }
        }
        result.encoding = TextEncoding::Utf16Be;
        result.ok = true;
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info,
            "text.utf16be_bom",
            "Se detectó BOM UTF-16BE y se convirtió explícitamente a UTF-8."));
        if (bad_surrogate) {
            result.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning,
                "text.utf16_surrogate",
                "El texto UTF-16 contiene sustitutos no emparejados; se reemplazaron con U+FFFD."));
        }
        return result;
    }

    if (isValidUtf8(raw.data(), size)) {
        result.text.assign(reinterpret_cast<const char*>(raw.data()), size);
        result.encoding = TextEncoding::Utf8;
        result.ok = true;
        return result;
    }

    bool replaced = false;
    result.text = decodeCp1252(raw, 0, replaced);
    result.encoding = TextEncoding::Cp1252;
    result.ok = true;
    result.diagnostics.push_back(makeDiagnostic(
        DiagnosticSeverity::Warning,
        "text.cp1252_fallback",
        "El archivo no es UTF-8 válido; se interpretó como CP1252 (Windows-1252)."));
    if (replaced) {
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning,
            "text.cp1252_replacement",
            "CP1252 contiene bytes no asignados; se reemplazaron con U+FFFD."));
    }
    return result;
}

                                                                              
         
                                                                              

namespace {

bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) {
        return 29;
    }
    return days[month - 1];
}

std::int64_t daysFromCivil(int year, int month, int day) {
    year -= month <= 2 ? 1 : 0;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t yoe = static_cast<std::int64_t>(year) - era * 400;
    const std::int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int normalizeYear(std::uint64_t year) {
    if (year <= 68) {
        return static_cast<int>(2000 + year);
    }
    if (year <= 99) {
        return static_cast<int>(1900 + year);
    }
    return static_cast<int>(year);
}

bool parseDatePart(const std::string& text,
                   DateTime& value,
                   bool& ambiguous,
                   bool& used_month_first,
                   std::string& error) {
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), '-', '/');
    std::vector<std::string> pieces;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= normalized.size(); ++i) {
        if (i == normalized.size() || normalized[i] == '/') {
            pieces.push_back(trim(std::string_view(normalized).substr(start, i - start)));
            start = i + 1;
        }
    }
    if (pieces.size() != 3) {
        error = "La fecha no tiene tres componentes.";
        return false;
    }
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::uint64_t third = 0;
    if (!parseUnsignedInteger(pieces[0], first) ||
        !parseUnsignedInteger(pieces[1], second) ||
        !parseUnsignedInteger(pieces[2], third)) {
        error = "La fecha contiene componentes no numéricos.";
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    const bool year_first = pieces[0].size() == 4;
    if (year_first) {
        year = static_cast<int>(first);
        month = static_cast<int>(second);
        day = static_cast<int>(third);
    } else {
        const int day_first = static_cast<int>(first);
        const int month_second = static_cast<int>(second);
        const int year_third = normalizeYear(third);
        const bool day_first_valid =
            month_second >= 1 && month_second <= 12 &&
            day_first >= 1 && day_first <= 31;
        if (day_first_valid) {
            year = year_third;
            month = month_second;
            day = day_first;
            ambiguous = day_first <= 12 && month_second <= 12;
        } else {
            const bool month_first_valid =
                day_first >= 1 && day_first <= 12 &&
                month_second >= 1 && month_second <= 31;
            if (!month_first_valid) {
                error = "El orden día/mes de la fecha no es válido.";
                return false;
            }
            year = year_third;
            month = day_first;
            day = month_second;
            ambiguous = true;
            used_month_first = true;
        }
    }

    if (year < 1 || year > 9999 || month < 1 || month > 12 ||
        day < 1 || day > daysInMonth(year, month)) {
        error = "La fecha no existe en el calendario.";
        return false;
    }
    value.year = year;
    value.month = month;
    value.day = day;
    return true;
}

bool parseTimePart(const std::string& text, DateTime& value, std::vector<Diagnostic>& diagnostics) {
    std::string time_text = text;
    const std::size_t comma = time_text.find(',');
    if (comma != std::string::npos) {
        const std::string whole = time_text.substr(0, comma);
        const std::string fraction = time_text.substr(comma + 1);
        if (whole.find('.') == std::string::npos && !fraction.empty()) {
            time_text = whole + "." + fraction;
        } else {
            time_text = whole;
        }
    }
    std::vector<std::string> pieces;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= time_text.size(); ++i) {
        if (i == time_text.size() || time_text[i] == ':') {
            pieces.push_back(trim(std::string_view(time_text).substr(start, i - start)));
            start = i + 1;
        }
    }
    if (pieces.size() != 3) {
        return false;
    }
    std::uint64_t hour = 0;
    std::uint64_t minute = 0;
    if (!parseUnsignedInteger(pieces[0], hour) || !parseUnsignedInteger(pieces[1], minute)) {
        return false;
    }
    std::string second_text = pieces[2];
    std::string fraction;
    const std::size_t dot = second_text.find('.');
    if (dot != std::string::npos) {
        fraction = second_text.substr(dot + 1);
        second_text = second_text.substr(0, dot);
    }
    std::uint64_t second = 0;
    if (!parseUnsignedInteger(second_text, second)) {
        return false;
    }
    if (hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    if (!fraction.empty()) {
        if (!isAllDigits(fraction)) {
            return false;
        }
        if (fraction.size() > 6) {
            diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Info,
                "datetime.fraction_truncated",
                "La fracción de segundo tiene más de seis dígitos; se truncó a microsegundos."));
            fraction.resize(6);
        }
        while (fraction.size() < 6) {
            fraction.push_back('0');
        }
        std::uint64_t microsecond = 0;
        parseUnsignedInteger(fraction, microsecond);
        value.microsecond = static_cast<int>(microsecond);
    } else {
        value.microsecond = 0;
    }
    value.hour = static_cast<int>(hour);
    value.minute = static_cast<int>(minute);
    value.second = static_cast<int>(second);
    return true;
}

}              

std::optional<double> dateTimeDifferenceSeconds(const DateTime& from, const DateTime& to) noexcept {
    if (!from.valid || !to.valid) {
        return std::nullopt;
    }
    const auto from_days = daysFromCivil(from.year, from.month, from.day);
    const auto to_days = daysFromCivil(to.year, to.month, to.day);
    const double day_seconds = static_cast<double>(to_days - from_days) * 86400.0;
    const double time_seconds =
        (to.hour - from.hour) * 3600.0 +
        (to.minute - from.minute) * 60.0 +
        (to.second - from.second) +
        (to.microsecond - from.microsecond) / 1'000'000.0;
    return day_seconds + time_seconds;
}

DateTimeParseResult parseComtradeDateTime(const std::string& text) {
    DateTimeParseResult result;
    const auto fields = csvFields(text);
    if (fields.size() < 2) {
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error,
            "datetime.incomplete",
            "Fecha/hora incompleta: se esperaban fecha y hora separadas por coma."));
        return result;
    }
    std::string date_text = fields[0];
    std::string time_text = fields[1];
    if (fields.size() > 2) {
        const std::string fraction = trim(fields[2]);
        if (isAllDigits(fraction) && time_text.find('.') == std::string::npos &&
            time_text.find(',') == std::string::npos) {
            time_text += "." + fraction;
        }
    }

    DateTime value;
    bool ambiguous = false;
    bool month_first = false;
    std::string date_error;
    if (!parseDatePart(date_text, value, ambiguous, month_first, date_error)) {
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error,
            "datetime.date_invalid",
            "Fecha inválida: " + date_error));
        return result;
    }
    if (!parseTimePart(time_text, value, result.diagnostics)) {
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error,
            "datetime.time_invalid",
            "Hora inválida: se esperaba hh:mm:ss con fracción opcional."));
        return result;
    }
    value.valid = true;
    result.value = value;
    result.ok = true;
    result.ambiguous = ambiguous;
    if (month_first) {
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning,
            "datetime.month_first_fallback",
            "La fecha no era válida como día/mes; se interpretó como mes/día. No se inventó zona horaria."));
    } else if (ambiguous) {
        result.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning,
            "datetime.ambiguous_day_month",
            "Fecha ambigua: se aplicó la precedencia día/mes del proyecto. No se inventó zona horaria."));
    }
    return result;
}

                                                                              
             
                                                                              

namespace {

bool parseCountField(std::string_view text, std::uint64_t& value) {
    text = trimView(text);
    if (text.empty()) {
        return false;
    }
    const char last = text.back();
    if (last == 'A' || last == 'a' || last == 'D' || last == 'd') {
        text.remove_suffix(1);
        text = trimView(text);
    }
    return parseUnsignedInteger(text, value);
}

bool cfgChannelCountOk(const std::vector<Line>& lines,
                       std::size_t na,
                       std::size_t nd,
                       std::size_t start_index) {
    return lines.size() >= start_index + na + nd;
}

std::string formatSize(std::uint64_t value) {
    return std::to_string(value);
}

}              

CfgParseResult parseCfg(const std::vector<std::uint8_t>& cfg_bytes,
                        const CfgParseOptions& options) {
    CfgParseResult result;
                                                                           
                                                                              
                                                                      
    if (cfg_bytes.size() > options.max_text_bytes) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.text_too_large",
                                     "El CFG ocupa " + std::to_string(cfg_bytes.size()) +
                                         " bytes y el límite de texto es " +
                                         std::to_string(options.max_text_bytes) + " bytes."));
        return result;
    }
    const auto decoded = decodeComtradeText(cfg_bytes);
    result.diagnostics = decoded.diagnostics;
    result.cfg.encoding = decoded.encoding;
    if (!decoded.ok) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.text_decode",
                                     "No se pudo decodificar el texto CFG."));
        return result;
    }
    if (decoded.text.size() > options.max_text_bytes) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.text_too_large",
                                     "El texto CFG decodificado ocupa " +
                                         std::to_string(decoded.text.size()) +
                                         " bytes y el límite es " +
                                         std::to_string(options.max_text_bytes) + " bytes."));
        return result;
    }

    const auto split = splitNonBlankLines(decoded.text, options.max_lines);
    if (split.line_limit_exceeded) {
        auto diagnostic = makeDiagnostic(
            DiagnosticSeverity::Error,
            "cfg.line_limit",
            "El CFG excede el límite de " + std::to_string(options.max_lines) +
                " líneas no vacías.");
        diagnostic.line = split.line_limit_line;
        diagnostic.has_line = true;
        addDiagnostic(result.diagnostics, options.max_diagnostics, std::move(diagnostic));
        return result;
    }
    const auto& lines = split.lines;
    if (lines.size() < 8) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.incomplete",
                                     "CFG incompleto: faltan líneas de cabecera, canales, muestreo o timestamps."));
        return result;
    }

    const auto header = csvFields(lines[0].text);
    result.cfg.station_name = header.empty() ? std::string() : header[0];
    if (header.size() > 1) {
        result.cfg.device_id = header[1];
    }
    if (header.size() > 2) {
        result.cfg.revision = header[2];
    }
    if (result.cfg.revision.find("1991") != std::string::npos) {
        result.cfg.format_year = 1991;
    } else if (result.cfg.revision.find("1999") != std::string::npos) {
        result.cfg.format_year = 1999;
    } else if (result.cfg.revision.find("2013") != std::string::npos) {
        result.cfg.format_year = 2013;
    }

    const auto counts = csvFields(lines[1].text);
    if (counts.size() < 3) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.count_invalid",
                                     "Conteo de canales CFG inválido."));
        return result;
    }

    std::uint64_t na = 0;
    std::uint64_t nd = 0;
    if (!parseCountField(counts[1], na) || !parseCountField(counts[2], nd)) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.count_not_integer",
                                     "Conteo de canales analógicos/digitales no válido."));
        return result;
    }

    const std::string total_text = trim(counts[0]);
    if (!total_text.empty()) {
        if (!isAllDigits(total_text)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.total_not_integer",
                                         "CFG inválido: el total de canales no es un entero: '" +
                                             total_text + "'."));
            return result;
        }
        std::uint64_t declared_total = 0;
        parseUnsignedInteger(total_text, declared_total);
        result.cfg.declared_total_channels = static_cast<std::size_t>(declared_total);
        if (declared_total != na + nd) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.total_mismatch",
                                         "CFG inválido: el total declarado (" + formatSize(declared_total) +
                                             ") no coincide con " + formatSize(na) + " analógicas + " +
                                             formatSize(nd) + " digitales."));
            return result;
        }
    } else {
        result.cfg.declared_total_channels = static_cast<std::size_t>(na + nd);
    }

    if (na < 1) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.no_analog",
                                     "El CFG no declara canales analógicos."));
        return result;
    }
                                                                             
                                                                         
    constexpr std::uint64_t kMaxCfgChannels = 100'000;
    if (na > kMaxCfgChannels || nd > kMaxCfgChannels || na + nd > kMaxCfgChannels) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.count_too_large",
                                     "El CFG declara una cantidad de canales inmanejable."));
        return result;
    }
    const std::size_t na_size = static_cast<std::size_t>(na);
    const std::size_t nd_size = static_cast<std::size_t>(nd);
    if (!cfgChannelCountOk(lines, na_size, nd_size, 2)) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.channels_truncated",
                                     "CFG truncado: las líneas de canales no coinciden con los conteos declarados."));
        return result;
    }

    result.cfg.analog.reserve(na_size);
    for (std::size_t i = 0; i < na_size; ++i) {
        const Line& line = lines[2 + i];
        const auto fields = csvFields(line.text);
        if (fields.size() < 7) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.analog_fields",
                                         "Canal analógico " + std::to_string(i + 1) +
                                             ": se esperaban al menos 7 campos y hay " +
                                             std::to_string(fields.size()) + "."));
            return result;
        }
        AnalogChannel channel;
        channel.position = i;
        std::uint64_t declared_index = 0;
        if (parseUnsignedInteger(fields[0], declared_index) &&
            declared_index <= std::numeric_limits<std::uint32_t>::max()) {
            channel.declared_index = static_cast<std::uint32_t>(declared_index);
        } else {
            channel.declared_index = static_cast<std::uint32_t>(i + 1);
        }
        channel.name = fields[1];
        channel.phase = fields.size() > 2 ? fields[2] : std::string();
        channel.circuit_component = fields.size() > 3 ? fields[3] : std::string();
        channel.unit = fields.size() > 4 ? fields[4] : std::string();

        double a = 0.0;
        double b = 0.0;
        if (!parseDouble(fields[5], a) || !parseDouble(fields[6], b)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.analog_scale",
                                         "Escala inválida en canal " + std::to_string(i + 1) +
                                             " (" + channel.name + ")."));
            return result;
        }
        channel.a = a;
        channel.b = b;
        double skew = 0.0;
        if (fields.size() > 7 && !trim(fields[7]).empty() && !parseDouble(fields[7], skew)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.analog_skew",
                                         "Skew inválido en canal " + std::to_string(i + 1) + "."));
            return result;
        }
        channel.skew_us = skew;
        double min_value = 0.0;
        double max_value = 0.0;
        if (fields.size() > 8 && !trim(fields[8]).empty() && parseDouble(fields[8], min_value)) {
            channel.min_value = min_value;
        }
        if (fields.size() > 9 && !trim(fields[9]).empty() && parseDouble(fields[9], max_value)) {
            channel.max_value = max_value;
        }
        double primary = 1.0;
        double secondary = 1.0;
        if (fields.size() > 10 && !trim(fields[10]).empty() && !parseDouble(fields[10], primary)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.analog_primary",
                                         "Relación primaria inválida en canal " + std::to_string(i + 1) + "."));
            return result;
        }
        if (fields.size() > 11 && !trim(fields[11]).empty() && !parseDouble(fields[11], secondary)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.analog_secondary",
                                         "Relación secundaria inválida en canal " + std::to_string(i + 1) + "."));
            return result;
        }
        channel.primary = primary;
        channel.secondary = secondary;
        if (fields.size() > 12 && !trim(fields[12]).empty()) {
            channel.ps = upperAscii(fields[12]);
        }
        if (!std::isfinite(channel.a) || !std::isfinite(channel.b) ||
            !std::isfinite(channel.primary) || !std::isfinite(channel.secondary) ||
            !std::isfinite(channel.skew_us)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.analog_nonfinite",
                                         "Canal analógico " + std::to_string(i + 1) + " (" + channel.name +
                                             "): A/B, skew o relación primaria/secundaria no finitos."));
            return result;
        }
        result.cfg.analog.push_back(std::move(channel));
    }

    result.cfg.digital.reserve(nd_size);
    for (std::size_t i = 0; i < nd_size; ++i) {
        const Line& line = lines[2 + na_size + i];
        const auto fields = csvFields(line.text);
        if (fields.size() < 2) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.digital_fields",
                                         "Canal digital " + std::to_string(i + 1) + ": nombre ausente."));
            return result;
        }
        DigitalChannel channel;
        channel.position = i;
        std::uint64_t declared_index = 0;
        if (parseUnsignedInteger(fields[0], declared_index) &&
            declared_index <= std::numeric_limits<std::uint32_t>::max()) {
            channel.declared_index = static_cast<std::uint32_t>(declared_index);
        } else {
            channel.declared_index = static_cast<std::uint32_t>(i + 1);
        }
        channel.name = fields[1].empty() ? ("Digital " + std::to_string(i + 1)) : fields[1];
        channel.phase = fields.size() > 2 ? fields[2] : std::string();
        channel.circuit_component = fields.size() > 3 ? fields[3] : std::string();
        channel.normal_state = fields.size() > 4 ? fields[4] : std::string();
        result.cfg.digital.push_back(std::move(channel));
    }

    const std::size_t i = 2 + na_size + nd_size;
    if (lines.size() < i + 2) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.sampling_missing",
                                     "CFG truncado: falta la frecuencia nominal o el número de tasas."));
        return result;
    }
    double frequency = 0.0;
    std::int64_t nr = 0;
    if (!parseDouble(lines[i].text, frequency) || !parseSignedInteger(lines[i + 1].text, nr)) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.sampling_invalid",
                                     "CFG inválido: frecuencia nominal o número de tasas de muestreo no legible."));
        return result;
    }
    if (!std::isfinite(frequency) || frequency <= 0.0 || nr < 0) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.sampling_value",
                                     "Frecuencia o número de tasas de muestreo inválido."));
        return result;
    }
    result.cfg.nominal_frequency_hz = frequency;
    if (static_cast<std::uint64_t>(nr) > static_cast<std::uint64_t>(lines.size())) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.rates_truncated",
                                     "CFG truncado: el número de tasas excede las líneas disponibles."));
        return result;
    }
    const std::size_t rate_count = static_cast<std::size_t>(nr);
    if (lines.size() < i + 2 + rate_count) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.rates_truncated",
                                     "CFG truncado: declara " + std::to_string(rate_count) +
                                         " tasa(s) de muestreo y no las contiene."));
        return result;
    }

    result.cfg.rates.reserve(rate_count);
    for (std::size_t r = 0; r < rate_count; ++r) {
        const Line& line = lines[i + 2 + r];
        const auto fields = csvFields(line.text);
        if (fields.size() < 2) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.rate_fields",
                                         "Línea de tasa de muestreo incompleta."));
            return result;
        }
        double rate = 0.0;
        std::int64_t end_sample = 0;
        if (!parseDouble(fields[0], rate) || !parseSignedInteger(fields[1], end_sample)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.rate_invalid",
                                         "Tasa de muestreo no válida: '" + line.text + "'."));
            return result;
        }
        if (!std::isfinite(rate) || rate <= 0.0) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.rate_positive",
                                         "Tasa de muestreo debe ser positiva: '" + line.text + "'."));
            return result;
        }
        if (end_sample <= 0) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.rate_end_positive",
                                         "Muestra final de la tasa debe ser positiva: '" + line.text + "'."));
            return result;
        }
        if (end_sample > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.rate_end_range",
                                         "Muestra final de la tasa excede el rango DAT admitido."));
            return result;
        }
        if (!result.cfg.rates.empty() &&
            end_sample <= static_cast<std::int64_t>(result.cfg.rates.back().end_sample)) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.rate_order",
                                         "Las muestras finales de las tasas CFG deben ser estrictamente crecientes."));
            return result;
        }
        SamplingRate sampling;
        sampling.rate_hz = rate;
        sampling.end_sample = static_cast<std::uint32_t>(end_sample);
        result.cfg.rates.push_back(sampling);
    }

    const std::size_t p = i + 2 + rate_count;
    static const std::array<const char*, 4> type_tokens = {"ASCII", "BINARY", "BINARY32", "FLOAT32"};
    std::optional<std::size_t> dtype_index;
    for (std::size_t j = p; j < std::min(lines.size(), p + 6); ++j) {
        const std::string token = upperAscii(lines[j].text);
        for (const char* candidate : type_tokens) {
            if (token == candidate) {
                dtype_index = j;
                break;
            }
        }
        if (dtype_index) {
            break;
        }
    }
    if (!dtype_index || *dtype_index < 2) {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Error,
                                     "cfg.dtype_missing",
                                     "CFG truncado: no se encontró tipo DAT después de tasas/fechas."));
        return result;
    }

    const std::size_t start_index = *dtype_index - 2;
    const std::size_t trigger_index = *dtype_index - 1;
    result.cfg.start_time_text = lines[start_index].text;
    result.cfg.trigger_time_text = lines[trigger_index].text;
    const auto start_parsed = parseComtradeDateTime(result.cfg.start_time_text);
    const auto trigger_parsed = parseComtradeDateTime(result.cfg.trigger_time_text);
                                                                              
                                                                              
                                                                             
                                                                               
    auto appendDateDiagnostics = [&result](const DateTimeParseResult& parsed) {
        for (auto diagnostic : parsed.diagnostics) {
            if (!parsed.ok && diagnostic.severity == DiagnosticSeverity::Error) {
                diagnostic.severity = DiagnosticSeverity::Warning;
            }
            result.diagnostics.push_back(std::move(diagnostic));
        }
    };
    appendDateDiagnostics(start_parsed);
    appendDateDiagnostics(trigger_parsed);
    if (start_parsed.ok) {
        result.cfg.start_time = start_parsed.value;
    } else {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Warning,
                                     "cfg.start_time_unreadable",
                                     "No se pudo interpretar la fecha/hora de inicio CFG; se conserva el texto."));
    }
    if (trigger_parsed.ok) {
        result.cfg.trigger_time = trigger_parsed.value;
    } else {
        addDiagnostic(result.diagnostics, options.max_diagnostics,
                      makeDiagnostic(DiagnosticSeverity::Warning,
                                     "cfg.trigger_time_unreadable",
                                     "No se pudo interpretar la fecha/hora de trigger CFG; se conserva el texto."));
    }

    const std::string dtype = upperAscii(lines[*dtype_index].text);
    if (dtype == "ASCII") {
        result.cfg.data_type = DataFileType::Ascii;
    } else if (dtype == "BINARY") {
        result.cfg.data_type = DataFileType::Binary16;
    } else if (dtype == "BINARY32") {
        result.cfg.data_type = DataFileType::Binary32;
    } else {
        result.cfg.data_type = DataFileType::Float32;
    }

    if (*dtype_index + 1 < lines.size() && isPlainDecimal(lines[*dtype_index + 1].text)) {
        double multiplier = 1.0;
        if (!parseDouble(lines[*dtype_index + 1].text, multiplier) ||
            !std::isfinite(multiplier) || multiplier <= 0.0) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Error,
                                         "cfg.multiplier",
                                         "Multiplicador temporal no válido."));
            return result;
        }
        result.cfg.time_multiplier = multiplier;
    }

    result.ok = !hasError(result.diagnostics);
    return result;
}

                                                                              
             
                                                                              

namespace {

std::uint16_t readLe16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
}

std::uint32_t readLe32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

float readLeFloat(const std::uint8_t* data) {
    const std::uint32_t bits = readLe32(data);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::int32_t toSigned16(std::uint16_t value) {
    return (value & 0x8000u) != 0
        ? static_cast<std::int32_t>(value) - 65536
        : static_cast<std::int32_t>(value);
}

std::int64_t toSigned32(std::uint32_t value) {
    return (value & 0x80000000u) != 0
        ? static_cast<std::int64_t>(value) - 4294967296ll
        : static_cast<std::int64_t>(value);
}

double medianOf(std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle), values.end());
    double median = values[middle];
    if (values.size() % 2 == 0) {
        const auto lower = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(middle));
        median = (*lower + median) / 2.0;
    }
    return median;
}

Diagnostic recordDiagnostic(DiagnosticSeverity severity,
                            std::string code,
                            std::string message,
                            std::size_t record_index,
                            std::uint32_t sample_number,
                            bool has_sample_number,
                            std::size_t line = 0,
                            bool has_line = false) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.record = record_index;
    diagnostic.has_record = true;
    diagnostic.sample_number = sample_number;
    diagnostic.has_sample_number = has_sample_number;
    diagnostic.line = line;
    diagnostic.has_line = has_line;
    return diagnostic;
}

void appendScaledAnalog(double& destination,
                        double raw,
                        const AnalogChannel& channel) {
    destination = channel.a * raw + channel.b;
}

}              

DatParseResult parseDat(const std::vector<std::uint8_t>& dat_bytes,
                        const CfgInfo& cfg,
                        const DatParseOptions& options) {
    DatParseResult result;
    const std::size_t na = cfg.analogCount();
    const std::size_t nd = cfg.digitalCount();
    const std::size_t packed_words = (nd + 15) / 16;

    result.data.analog_channels = na;
    result.data.digital_channels = nd;

    auto fail = [&](Diagnostic diagnostic) {
        addDiagnostic(result.diagnostics, options.max_diagnostics, std::move(diagnostic));
        result.ok = false;
        return result;
    };

    if (na == 0) {
        return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                   "dat.no_analog_channels",
                                   "El CFG no declara canales analógicos; no se puede interpretar el DAT."));
    }

    if (dat_bytes.empty()) {
        return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                   "dat.empty",
                                   "DAT vacío."));
    }

    if (cfg.data_type == DataFileType::Ascii) {
        const auto decoded = decodeComtradeText(dat_bytes);
        result.encoding = decoded.encoding;
        result.diagnostics.insert(result.diagnostics.end(),
                                  decoded.diagnostics.begin(), decoded.diagnostics.end());
        if (!decoded.ok) {
            return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                       "dat.text_decode",
                                       "No se pudo decodificar el texto DAT ASCII."));
        }
                                                                           
                                                                              
                                                                         
        const auto split = splitNonBlankLines(decoded.text,
                                              options.max_samples,
                                              options.max_ascii_line_bytes);
        if (split.line_too_long) {
            auto diagnostic = makeDiagnostic(
                DiagnosticSeverity::Error,
                "dat.ascii_line_too_long",
                "Una línea física DAT ASCII tiene " + std::to_string(split.long_line_bytes) +
                    " bytes y el límite es " +
                    std::to_string(options.max_ascii_line_bytes) + " bytes.");
            diagnostic.line = split.long_line;
            diagnostic.has_line = true;
            return fail(std::move(diagnostic));
        }
        if (split.line_limit_exceeded) {
            auto diagnostic = makeDiagnostic(
                DiagnosticSeverity::Error,
                "dat.line_limit",
                "El DAT ASCII excede el límite de " + std::to_string(options.max_samples) +
                    " muestras/líneas no vacías.");
            diagnostic.line = split.line_limit_line;
            diagnostic.has_line = true;
            return fail(std::move(diagnostic));
        }
        const auto& lines = split.lines;
        result.data.sample_numbers.reserve(lines.size());
        result.data.dat_timestamp_us.reserve(lines.size());
        result.data.times_s.reserve(lines.size());

        for (std::size_t record = 0; record < lines.size(); ++record) {
            const Line& line = lines[record];
            if (record >= options.max_samples) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.sample_limit",
                                             "El DAT excede el límite de muestras admitido.",
                                             record, 0, false, line.number, true));
            }
            const auto fields = csvFields(line.text);
            const std::size_t base = 2 + na;
            if (fields.size() < base) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.ascii_columns",
                                             "DAT ASCII línea: faltan columnas analógicas.",
                                             record, 0, false, line.number, true));
            }
            std::int64_t sequence = 0;
            if (!parseSignedInteger(fields[0], sequence)) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.sample_number_invalid",
                                             "Número de muestra DAT no válido.",
                                             record, 0, false, line.number, true));
            }
            if (sequence < 0) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.sample_number_negative",
                                             "Número de muestra negativo.",
                                             record, 0, false, line.number, true));
            }
            if (sequence > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.sample_number_range",
                                             "Número de muestra fuera del rango de 32 bits.",
                                             record, 0, false, line.number, true));
            }
            double micros = 0.0;
            if (!parseDouble(fields[1], micros) || !std::isfinite(micros) || micros < 0.0) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.timestamp_invalid",
                                             "Timestamp negativo o no finito.",
                                             record, static_cast<std::uint32_t>(sequence), true,
                                             line.number, true));
            }

            std::vector<double> raw_analog(na);
            bool analog_nonfinite = false;
            for (std::size_t channel = 0; channel < na; ++channel) {
                double raw = 0.0;
                if (!parseDouble(fields[2 + channel], raw)) {
                    return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                 "dat.analog_value",
                                                 "Valor analógico DAT no válido en canal " +
                                                     std::to_string(channel + 1) + ".",
                                                 record, static_cast<std::uint32_t>(sequence), true,
                                                 line.number, true));
                }
                if (!std::isfinite(raw)) {
                    analog_nonfinite = true;
                }
                raw_analog[channel] = raw;
            }
            if (analog_nonfinite) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.analog_nonfinite",
                                             "El DAT contiene valores analógicos no finitos (NaN/Inf).",
                                             record, static_cast<std::uint32_t>(sequence), true,
                                             line.number, true));
            }

            std::vector<std::uint8_t> digital_row(nd, 0);
            const std::size_t digital_fields = fields.size() - base;
            if (digital_fields == nd) {
                for (std::size_t channel = 0; channel < nd; ++channel) {
                    double raw = 0.0;
                    if (!parseDouble(fields[base + channel], raw) ||
                        !std::isfinite(raw) || (raw != 0.0 && raw != 1.0)) {
                        return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                     "dat.digital_state",
                                                     "Estado digital ASCII distinto de 0/1.",
                                                     record, static_cast<std::uint32_t>(sequence), true,
                                                     line.number, true));
                    }
                    digital_row[channel] = static_cast<std::uint8_t>(raw);
                }
            } else if (digital_fields == packed_words) {
                std::vector<std::uint16_t> words(packed_words, 0);
                for (std::size_t word = 0; word < packed_words; ++word) {
                    double raw = 0.0;
                    if (!parseDouble(fields[base + word], raw) ||
                        !std::isfinite(raw) || raw != std::trunc(raw) ||
                        raw < 0.0 || raw > 65535.0) {
                        return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                     "dat.digital_word",
                                                     "Palabra digital ASCII fuera del rango entero 0..65535.",
                                                     record, static_cast<std::uint32_t>(sequence), true,
                                                     line.number, true));
                    }
                    words[word] = static_cast<std::uint16_t>(raw);
                }
                for (std::size_t channel = 0; channel < nd; ++channel) {
                    digital_row[channel] = static_cast<std::uint8_t>(
                        (words[channel / 16] >> (channel % 16)) & 1u);
                }
            } else {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.ascii_digital_columns",
                                             "Cantidad de columnas digitales incoherente: se esperaban " +
                                                 std::to_string(nd) + " individuales o " +
                                                 std::to_string(packed_words) + " empaquetadas.",
                                             record, static_cast<std::uint32_t>(sequence), true,
                                             line.number, true));
            }

            const double seconds = micros * cfg.time_multiplier / 1'000'000.0;
            if (!std::isfinite(seconds)) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.timestamp_nonfinite",
                                             "El timestamp DAT produjo un valor no finito al aplicar el multiplicador.",
                                             record, static_cast<std::uint32_t>(sequence), true,
                                             line.number, true));
            }
            if (record > 0) {
                const auto previous_sequence = result.data.sample_numbers.back();
                if (static_cast<std::uint32_t>(sequence) <= previous_sequence) {
                    return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                 "dat.sample_order",
                                                 "Números de muestra duplicados o fuera de orden.",
                                                 record, static_cast<std::uint32_t>(sequence), true,
                                                 line.number, true));
                }
                if (seconds <= result.data.times_s.back()) {
                    return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                 "dat.timestamp_order",
                                                 "Timestamps duplicados o fuera de orden.",
                                                 record, static_cast<std::uint32_t>(sequence), true,
                                                 line.number, true));
                }
            }

            if (result.data.analog.size() + na > options.max_analog_cells ||
                result.data.digital.size() + nd > options.max_digital_cells) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.memory_limit",
                                             "El DAT excede el límite interno de celdas para análisis.",
                                             record, static_cast<std::uint32_t>(sequence), true,
                                             line.number, true));
            }
            result.data.sample_numbers.push_back(static_cast<std::uint32_t>(sequence));
            result.data.dat_timestamp_us.push_back(micros);
            result.data.times_s.push_back(seconds);
            for (std::size_t channel = 0; channel < na; ++channel) {
                double scaled = 0.0;
                appendScaledAnalog(scaled, raw_analog[channel], cfg.analog[channel]);
                if (!std::isfinite(scaled)) {
                    return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                 "dat.scaling_nonfinite",
                                                 "El escalamiento CFG produjo valores no finitos.",
                                                 record, static_cast<std::uint32_t>(sequence), true,
                                                 line.number, true));
                }
                result.data.analog.push_back(scaled);
            }
            result.data.digital.insert(result.data.digital.end(),
                                       digital_row.begin(), digital_row.end());
        }
    } else {
        const std::size_t analog_bytes =
            (cfg.data_type == DataFileType::Binary32 || cfg.data_type == DataFileType::Float32) ? 4 : 2;
        const std::uint64_t row_size64 = 8ull +
            static_cast<std::uint64_t>(analog_bytes) * static_cast<std::uint64_t>(na) +
            2ull * static_cast<std::uint64_t>(packed_words);
        if (row_size64 <= 8ull || row_size64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                       "dat.binary_row_size",
                                       "CFG inválido: tamaño de fila binaria no positivo."));
        }
        const std::size_t row_size = static_cast<std::size_t>(row_size64);
        if (dat_bytes.size() % row_size != 0) {
            return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                       "dat.binary_incoherent",
                                       "DAT binario incoherente: " + std::to_string(dat_bytes.size()) +
                                           " bytes, tamaño esperado por fila " + std::to_string(row_size) + "."));
        }
        const std::size_t sample_count = dat_bytes.size() / row_size;
        if (sample_count > options.max_samples) {
            return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                       "dat.sample_limit",
                                       "El DAT excede el límite de muestras admitido."));
        }
        const std::uint64_t analog_cells =
            static_cast<std::uint64_t>(sample_count) * static_cast<std::uint64_t>(na);
        const std::uint64_t digital_cells =
            static_cast<std::uint64_t>(sample_count) * static_cast<std::uint64_t>(nd);
        if (analog_cells > options.max_analog_cells ||
            digital_cells > options.max_digital_cells) {
            return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                       "dat.memory_limit",
                                       "El DAT excede el límite interno de celdas para análisis."));
        }

        result.data.sample_numbers.reserve(sample_count);
        result.data.dat_timestamp_us.reserve(sample_count);
        result.data.times_s.reserve(sample_count);
        result.data.analog.reserve(static_cast<std::size_t>(analog_cells));
        result.data.digital.reserve(static_cast<std::size_t>(digital_cells));

        const std::size_t analog_offset = 8;
        const std::size_t digital_offset = analog_offset + analog_bytes * na;
        for (std::size_t record = 0; record < sample_count; ++record) {
            const std::uint8_t* row = dat_bytes.data() + record * row_size;
            const std::uint32_t sequence = readLe32(row);
            const std::uint32_t micros_raw = readLe32(row + 4);
            const double micros = static_cast<double>(micros_raw);
            const double seconds = micros * cfg.time_multiplier / 1'000'000.0;
            if (!std::isfinite(seconds)) {
                return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                             "dat.timestamp_nonfinite",
                                             "El timestamp DAT produjo un valor no finito al aplicar el multiplicador.",
                                             record, sequence, true));
            }
            if (record > 0) {
                if (sequence <= result.data.sample_numbers.back()) {
                    return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                 "dat.sample_order",
                                                 "Números de muestra duplicados o fuera de orden.",
                                                 record, sequence, true));
                }
                if (seconds <= result.data.times_s.back()) {
                    return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                 "dat.timestamp_order",
                                                 "Timestamps duplicados o fuera de orden.",
                                                 record, sequence, true));
                }
            }

            result.data.sample_numbers.push_back(sequence);
            result.data.dat_timestamp_us.push_back(micros);
            result.data.times_s.push_back(seconds);

            for (std::size_t channel = 0; channel < na; ++channel) {
                double raw = 0.0;
                if (cfg.data_type == DataFileType::Binary16) {
                    raw = static_cast<double>(toSigned16(readLe16(row + analog_offset + channel * 2)));
                } else if (cfg.data_type == DataFileType::Binary32) {
                    raw = static_cast<double>(toSigned32(readLe32(row + analog_offset + channel * 4)));
                } else {
                    const float value = readLeFloat(row + analog_offset + channel * 4);
                    if (!std::isfinite(value)) {
                        return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                     "dat.analog_nonfinite",
                                                     "El DAT contiene valores analógicos no finitos (NaN/Inf).",
                                                     record, sequence, true));
                    }
                    raw = static_cast<double>(value);
                }
                double scaled = 0.0;
                appendScaledAnalog(scaled, raw, cfg.analog[channel]);
                if (!std::isfinite(scaled)) {
                    return fail(recordDiagnostic(DiagnosticSeverity::Error,
                                                 "dat.scaling_nonfinite",
                                                 "El escalamiento CFG produjo valores no finitos.",
                                                 record, sequence, true));
                }
                result.data.analog.push_back(scaled);
            }

            for (std::size_t channel = 0; channel < nd; ++channel) {
                const std::uint16_t word = readLe16(row + digital_offset + (channel / 16) * 2);
                result.data.digital.push_back(static_cast<std::uint8_t>(
                    (word >> (channel % 16)) & 1u));
            }
        }
    }

    if (result.data.sampleCount() == 0) {
        return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                   "dat.no_samples",
                                   "DAT sin muestras."));
    }
    if (result.data.sampleCount() < 2) {
        return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                   "dat.too_few_samples",
                                   "DAT contiene menos de 2 muestras; se requieren al menos dos para validar tiempos y tasas."));
    }

                                                                              
    for (std::size_t record = 1; record < result.data.sampleCount(); ++record) {
        if (result.data.sample_numbers[record] != result.data.sample_numbers[record - 1] + 1u) {
            auto diagnostic = recordDiagnostic(
                DiagnosticSeverity::Warning,
                "dat.nonconsecutive_samples",
                "Números de muestra DAT no consecutivos; hay muestras ausentes o saltos de secuencia.",
                record, result.data.sample_numbers[record], true);
            addDiagnostic(result.diagnostics, options.max_diagnostics, std::move(diagnostic));
            break;
        }
    }

    std::size_t start = 0;
    for (const auto& rate : cfg.rates) {
        const std::size_t end = static_cast<std::size_t>(rate.end_sample);
        if (end <= start || end > result.data.sampleCount()) {
            return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                       "dat.rate_bounds",
                                       "Límite de tasa " + std::to_string(end) + " fuera del DAT de " +
                                           std::to_string(result.data.sampleCount()) + " muestras."));
        }
        std::vector<double> diffs;
        diffs.reserve(end - start);
        for (std::size_t index = start + 1; index < end; ++index) {
            diffs.push_back(result.data.times_s[index] - result.data.times_s[index - 1]);
        }
        if (!diffs.empty()) {
            const double median = medianOf(diffs);
            const double expected = 1.0 / rate.rate_hz;
            const double tolerance = std::max(expected * 0.05, 2e-6);
            if (std::abs(median - expected) > tolerance) {
                auto diagnostic = makeDiagnostic(
                    DiagnosticSeverity::Warning,
                    "dat.rate_mismatch",
                    "Tasa CFG " + std::to_string(rate.rate_hz) +
                        " Hz no coincide con los timestamps DAT en muestras " +
                        std::to_string(start + 1) + "–" + std::to_string(end) + ".");
                addDiagnostic(result.diagnostics, options.max_diagnostics, std::move(diagnostic));
            }
        }
        start = end;
    }
    if (!cfg.rates.empty() && start != result.data.sampleCount()) {
        return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                   "dat.rate_coverage",
                                   "Las tasas CFG cubren " + std::to_string(start) +
                                       " muestras y DAT contiene " +
                                       std::to_string(result.data.sampleCount()) + "."));
    }
    if (cfg.rates.empty()) {
        std::vector<double> diffs;
        diffs.reserve(result.data.sampleCount() - 1);
        for (std::size_t index = 1; index < result.data.sampleCount(); ++index) {
            diffs.push_back(result.data.times_s[index] - result.data.times_s[index - 1]);
        }
        const double median = medianOf(diffs);
        if (median <= 0.0 || !std::isfinite(median)) {
            return fail(makeDiagnostic(DiagnosticSeverity::Error,
                                       "dat.rate_unknown",
                                       "No se puede inferir la tasa de muestreo desde los timestamps DAT."));
        }
        double worst = 0.0;
        for (const double diff : diffs) {
            worst = std::max(worst, std::abs(diff - median));
        }
        const double tolerance = std::max(median * 0.05, 2e-6);
        if (worst > tolerance) {
            addDiagnostic(result.diagnostics, options.max_diagnostics,
                          makeDiagnostic(DiagnosticSeverity::Warning,
                                         "dat.nonuniform_timestamps",
                                         "CFG no declara tasas; timestamps DAT no son uniformes. "
                                         "Se usa la tasa mediana para ventanas y espectro."));
        }
    }

    if (result.data.times_s.front() != 0.0) {
        auto diagnostic = makeDiagnostic(
            DiagnosticSeverity::Warning,
            "dat.first_sample_nonzero",
            "La primera muestra DAT no es cero; se conserva su timestamp real sin rebasar el inicio.");
        addDiagnostic(result.diagnostics, options.max_diagnostics, std::move(diagnostic));
    }

    result.ok = !hasError(result.diagnostics);
    return result;
}

ComtradeParseResult parseComtrade(const std::vector<std::uint8_t>& cfg_bytes,
                                  const std::vector<std::uint8_t>& dat_bytes,
                                  const ParseOptions& options) {
    ComtradeParseResult result;
    CfgParseResult cfg_result = parseCfg(cfg_bytes, options.cfg);
    result.record.cfg = cfg_result.cfg;
    result.diagnostics = std::move(cfg_result.diagnostics);
    if (!cfg_result.ok) {
        result.ok = false;
        return result;
    }

    DatParseResult dat_result = parseDat(dat_bytes, result.record.cfg, options.dat);
    result.record.data = std::move(dat_result.data);
    result.record.dat_encoding = dat_result.encoding;
    result.diagnostics.insert(result.diagnostics.end(),
                              dat_result.diagnostics.begin(), dat_result.diagnostics.end());
    if (!dat_result.ok) {
        result.ok = false;
        return result;
    }

    if (result.record.cfg.start_time.valid && result.record.cfg.trigger_time.valid) {
        const auto offset = dateTimeDifferenceSeconds(result.record.cfg.start_time,
                                                      result.record.cfg.trigger_time);
        if (offset) {
            result.record.trigger_offset_s = *offset;
            if (*offset < 0.0) {
                auto diagnostic = makeDiagnostic(
                    DiagnosticSeverity::Warning,
                    "cfg.trigger_before_start",
                    "La fecha/hora de trigger es anterior al inicio CFG; se conserva la diferencia negativa.");
                result.diagnostics.push_back(std::move(diagnostic));
            }
        }
    }
    if (!result.record.data.times_s.empty()) {
        result.record.first_sample_time_s = result.record.data.times_s.front();
        result.record.first_sample_nonzero = result.record.first_sample_time_s != 0.0;
    }

    result.ok = !hasError(result.diagnostics);
    return result;
}

}                         
