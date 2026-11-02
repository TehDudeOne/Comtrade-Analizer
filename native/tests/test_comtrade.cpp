                                                                           
  
                                                                           
                                                                        
                                                                               
                                                                            

#include "analizador/comtrade.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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

using analizador::ComtradeParseResult;
using analizador::CfgParseResult;
using analizador::DatParseResult;
using analizador::Diagnostic;
using analizador::DiagnosticSeverity;
using analizador::SampleData;

std::vector<std::uint8_t> bytesOf(std::string_view text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string textOf(const std::vector<std::uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

bool hasCode(const std::vector<Diagnostic>& diagnostics, std::string_view code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& item) {
        return item.code == code;
    });
}

bool hasMessage(const std::vector<Diagnostic>& diagnostics, std::string_view part) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const Diagnostic& item) {
        return item.message.find(part) != std::string::npos;
    });
}

std::string number(double value) {
    return std::to_string(value);
}

std::string analogLine(std::size_t index,
                       const std::string& name,
                       double a = 1.0,
                       double b = 0.0,
                       const std::string& unit = "A",
                       double skew = 0.0,
                       double primary = 1000.0,
                       double secondary = 5.0,
                       const std::string& ps = "P") {
    return std::to_string(index) + "," + name + ",,," + unit + "," +
        number(a) + "," + number(b) + "," + number(skew) + ",-32768,32767," +
        number(primary) + "," + number(secondary) + "," + ps;
}

std::string digitalLine(std::size_t index, const std::string& name) {
    return std::to_string(index) + "," + name + ",,,0";
}

std::string makeCfgText(const std::vector<std::string>& analog_lines,
                        const std::vector<std::string>& digital_lines,
                        const std::vector<std::pair<double, std::uint32_t>>& rates,
                        const std::string& dtype = "ASCII",
                        const std::string& start = "01/01/2026,00:00:00.000000",
                        const std::string& trigger = "01/01/2026,00:00:00.100000",
                        int declared_rates = -1,
                        int rate_lines = -1,
                        const std::string& total_override = "") {
    const std::size_t total = analog_lines.size() + digital_lines.size();
    std::string text = "TESTSTATION\n";
    text += (total_override.empty() ? std::to_string(total) : total_override) + "," +
        std::to_string(analog_lines.size()) + "," + std::to_string(digital_lines.size()) + "\n";
    for (const auto& line : analog_lines) {
        text += line + "\n";
    }
    for (const auto& line : digital_lines) {
        text += line + "\n";
    }
    text += "50\n";
    const int rate_count = declared_rates >= 0 ? declared_rates : static_cast<int>(rates.size());
    const int emitted_rates = rate_lines >= 0
        ? std::min<int>(rate_lines, static_cast<int>(rates.size()))
        : static_cast<int>(rates.size());
    text += std::to_string(rate_count) + "\n";
    for (int i = 0; i < emitted_rates; ++i) {
        text += number(rates[static_cast<std::size_t>(i)].first) + "," +
            std::to_string(rates[static_cast<std::size_t>(i)].second) + "\n";
    }
    text += start + "\n";
    text += trigger + "\n";
    text += dtype + "\n";
    text += "1.0\n";
    return text;
}

std::vector<std::string> threeCurrents(double a = 1.0, double b = 0.0) {
    return {
        analogLine(1, "IA", a, b),
        analogLine(2, "IB", a, b),
        analogLine(3, "IC", a, b),
    };
}

std::string makeAsciiData(const std::vector<std::uint32_t>& sequences,
                          const std::vector<double>& times_us,
                          const std::vector<std::vector<double>>& analog,
                          const std::vector<std::vector<int>>& digital) {
    std::string text;
    for (std::size_t record = 0; record < sequences.size(); ++record) {
        text += std::to_string(sequences[record]) + "," + number(times_us[record]);
        for (const double value : analog[record]) {
            text += "," + number(value);
        }
        for (const int value : digital[record]) {
            text += "," + std::to_string(value);
        }
        text += "\n";
    }
    return text;
}

void putU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void putU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
}

void putF32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putU32(out, bits);
}

void putText(std::vector<std::uint8_t>& out, std::string_view text) {
    out.insert(out.end(), text.begin(), text.end());
}

std::uint32_t crc32Of(const std::vector<std::uint8_t>& data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0 ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

struct ZipItem {
    std::string name;
    std::uint16_t method = 0;
    std::uint16_t flags = 0;
    std::vector<std::uint8_t> payload;                                                      
    std::vector<std::uint8_t> content;                                   
    std::optional<std::uint32_t> compressed_override;
    std::optional<std::uint32_t> uncompressed_override;
    std::optional<std::uint32_t> crc_override;
    std::uint32_t local_offset = 0;
};

std::vector<std::uint8_t> buildZip(std::vector<ZipItem> items) {
    std::vector<std::uint8_t> out;
    for (auto& item : items) {
        if (item.payload.empty() && item.method == 0) {
            item.payload = item.content;
        }
        item.local_offset = static_cast<std::uint32_t>(out.size());
        const std::uint32_t compressed = item.compressed_override.has_value()
            ? *item.compressed_override
            : static_cast<std::uint32_t>(item.payload.size());
        const std::uint32_t uncompressed = item.uncompressed_override.has_value()
            ? *item.uncompressed_override
            : static_cast<std::uint32_t>(item.content.size());
        const std::uint32_t crc = item.crc_override.has_value()
            ? *item.crc_override
            : crc32Of(item.content);

        putU32(out, 0x04034B50u);
        putU16(out, 20);
        putU16(out, item.flags);
        putU16(out, item.method);
        putU16(out, 0);
        putU16(out, 0);
        putU32(out, crc);
        putU32(out, compressed);
        putU32(out, uncompressed);
        putU16(out, static_cast<std::uint16_t>(item.name.size()));
        putU16(out, 0);
        putText(out, item.name);
        out.insert(out.end(), item.payload.begin(), item.payload.end());
    }

    const std::uint32_t central_offset = static_cast<std::uint32_t>(out.size());
    for (const auto& item : items) {
        const std::uint32_t compressed = item.compressed_override.has_value()
            ? *item.compressed_override
            : static_cast<std::uint32_t>(item.payload.size());
        const std::uint32_t uncompressed = item.uncompressed_override.has_value()
            ? *item.uncompressed_override
            : static_cast<std::uint32_t>(item.content.size());
        const std::uint32_t crc = item.crc_override.has_value()
            ? *item.crc_override
            : crc32Of(item.content);
        putU32(out, 0x02014B50u);
        putU16(out, 20);
        putU16(out, 20);
        putU16(out, item.flags);
        putU16(out, item.method);
        putU16(out, 0);
        putU16(out, 0);
        putU32(out, crc);
        putU32(out, compressed);
        putU32(out, uncompressed);
        putU16(out, static_cast<std::uint16_t>(item.name.size()));
        putU16(out, 0);
        putU16(out, 0);
        putU16(out, 0);
        putU16(out, 0);
        putU32(out, 0);
        putU32(out, item.local_offset);
        putText(out, item.name);
    }
    const std::uint32_t central_size = static_cast<std::uint32_t>(out.size()) - central_offset;

    putU32(out, 0x06054B50u);
    putU16(out, 0);
    putU16(out, 0);
    putU16(out, static_cast<std::uint16_t>(items.size()));
    putU16(out, static_cast<std::uint16_t>(items.size()));
    putU32(out, central_size);
    putU32(out, central_offset);
    putU16(out, 0);
    return out;
}

std::vector<std::uint8_t> storedDeflate(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out;
    out.push_back(0x01);                        
    putU16(out, static_cast<std::uint16_t>(data.size()));
    putU16(out, static_cast<std::uint16_t>(~data.size()));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<std::uint8_t> hexBytes(std::string_view hex) {
    std::vector<std::uint8_t> out;
    int high = -1;
    for (const char c : hex) {
        int value = -1;
        if (c >= '0' && c <= '9') {
            value = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            value = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            value = c - 'A' + 10;
        }
        if (value < 0) {
            continue;
        }
        if (high < 0) {
            high = value;
        } else {
            out.push_back(static_cast<std::uint8_t>((high << 4) | value));
            high = -1;
        }
    }
    return out;
}

std::string dynamicFixtureData() {
    const std::string sentence =
        "The quick brown fox jumps over the lazy dog. COMTRADE 0123456789 abcdefghijklmnopqrstuvwxyz\n";
    std::string data;
    while (data.size() < 5000) {
        data += sentence;
    }
    data.resize(5000);
    return data;
}

                                                                              
                 
                                                                              

void testTextDecoding() {
    const auto utf8 = analizador::decodeComtradeText(bytesOf("ABC"));
    CHECK(utf8.ok);
    CHECK(utf8.encoding == analizador::TextEncoding::Utf8);
    CHECK(utf8.text == "ABC");

    const std::vector<std::uint8_t> bom = {0xEF, 0xBB, 0xBF, 'A', 'B'};
    const auto utf8_bom = analizador::decodeComtradeText(bom);
    CHECK(utf8_bom.ok);
    CHECK(utf8_bom.encoding == analizador::TextEncoding::Utf8Bom);
    CHECK(utf8_bom.text == "AB");
    CHECK(hasCode(utf8_bom.diagnostics, "text.utf8_bom"));

    const std::vector<std::uint8_t> cp1252 = {0x43, 0x61, 0x66, 0xE9};
    const auto cp = analizador::decodeComtradeText(cp1252);
    CHECK(cp.ok);
    CHECK(cp.encoding == analizador::TextEncoding::Cp1252);
    CHECK(cp.text == std::string("Caf") + "\xC3\xA9");
    CHECK(hasCode(cp.diagnostics, "text.cp1252_fallback"));

    const std::vector<std::uint8_t> utf16le = {0xFF, 0xFE, 'H', 0x00, 0xE9, 0x00};
    const auto utf16 = analizador::decodeComtradeText(utf16le);
    CHECK(utf16.ok);
    CHECK(utf16.encoding == analizador::TextEncoding::Utf16Le);
    CHECK(utf16.text == std::string("H") + "\xC3\xA9");

    const std::vector<std::uint8_t> truncated_utf16 = {0xFF, 0xFE, 'A'};
    const auto truncated = analizador::decodeComtradeText(truncated_utf16);
    CHECK(!truncated.ok);
    CHECK(hasCode(truncated.diagnostics, "text.utf16_odd_length"));
}

void testDateTimeParsing() {
    const auto dotted = analizador::parseComtradeDateTime("01/01/2026,00:00:00.123456");
    CHECK(dotted.ok);
    CHECK(dotted.value.year == 2026);
    CHECK(dotted.value.microsecond == 123456);
    CHECK(dotted.ambiguous);
    CHECK(hasCode(dotted.diagnostics, "datetime.ambiguous_day_month"));

    const auto comma_fraction = analizador::parseComtradeDateTime("01/01/2026,00:00:00,123456");
    CHECK(comma_fraction.ok);
    CHECK(comma_fraction.value.microsecond == 123456);

    const auto iso = analizador::parseComtradeDateTime("2026-01-01,12:34:56.5");
    CHECK(iso.ok);
    CHECK(iso.value.hour == 12);
    CHECK(iso.value.minute == 34);
    CHECK(iso.value.microsecond == 500000);

    const auto fallback = analizador::parseComtradeDateTime("02/13/2026,00:00:00");
    CHECK(fallback.ok);
    CHECK(fallback.value.day == 13);
    CHECK(fallback.value.month == 2);
    CHECK(hasCode(fallback.diagnostics, "datetime.month_first_fallback"));

    const auto invalid = analizador::parseComtradeDateTime("not-a-date,not-a-time");
    CHECK(!invalid.ok);

    const auto first = analizador::parseComtradeDateTime("01/01/2026,00:00:00.000000");
    const auto second = analizador::parseComtradeDateTime("01/01/2026,00:00:00.100000");
    const auto difference = analizador::dateTimeDifferenceSeconds(first.value, second.value);
    CHECK(difference.has_value());
    CHECK(std::abs(*difference - 0.1) < 1e-9);
}

                                                                              
      
                                                                              

void testCfgValidationAndMetadata() {
    const auto ok = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 2}})));
    CHECK(ok.ok);
    CHECK(ok.cfg.station_name == "TESTSTATION");
    CHECK(ok.cfg.analog.size() == 3);
    CHECK(ok.cfg.nominal_frequency_hz == 50.0);
    CHECK(ok.cfg.rates.size() == 1);
    CHECK(ok.cfg.rates[0].end_sample == 2);
    CHECK(ok.cfg.data_type == analizador::DataFileType::Ascii);
    CHECK(ok.cfg.start_time.valid);
    CHECK(ok.cfg.trigger_time.valid);
    CHECK(ok.cfg.analog[0].ps == "P");

    std::string cp1252_cfg = makeCfgText(threeCurrents(), {}, {{1000.0, 2}});
    const std::size_t station = cp1252_cfg.find("TESTSTATION");
    CHECK(station != std::string::npos);
    cp1252_cfg.replace(station, 11, std::string("CAF") + "\xE9");
    const auto cp1252 = analizador::parseCfg(bytesOf(cp1252_cfg));
    CHECK(cp1252.ok);
    CHECK(cp1252.cfg.encoding == analizador::TextEncoding::Cp1252);
    CHECK(cp1252.cfg.station_name == std::string("CAF") + "\xC3\xA9");

    const auto total = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 2}}, "ASCII",
        "01/01/2026,00:00:00.000000", "01/01/2026,00:00:00.100000",
        -1, -1, "7")));
    CHECK(!total.ok);
    CHECK(hasCode(total.diagnostics, "cfg.total_mismatch"));

    const auto non_numeric_total = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 2}}, "ASCII",
        "01/01/2026,00:00:00.000000", "01/01/2026,00:00:00.100000",
        -1, -1, "X")));
    CHECK(!non_numeric_total.ok);
    CHECK(hasCode(non_numeric_total.diagnostics, "cfg.total_not_integer"));

    const auto zero_rate = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{0.0, 2}})));
    CHECK(!zero_rate.ok);
    CHECK(hasCode(zero_rate.diagnostics, "cfg.rate_positive"));

    const auto decreasing = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 10}, {500.0, 10}})));
    CHECK(!decreasing.ok);
    CHECK(hasCode(decreasing.diagnostics, "cfg.rate_order"));

    const auto missing_rate_line = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 2}}, "ASCII",
        "01/01/2026,00:00:00.000000", "01/01/2026,00:00:00.100000",
        2, 1)));
    CHECK(!missing_rate_line.ok);

    const auto zero_rates = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {}, "ASCII",
        "01/01/2026,00:00:00.000000", "01/01/2026,00:00:00.100000")));
    CHECK(zero_rates.ok);
    CHECK(zero_rates.cfg.rates.empty());
}

void testCfg2013ZeroRatesAndTimeMultiplier() {
    std::string cfg = "TESTSTATION,DEVICE,2013\n";
    cfg += "3,3,0\n";
    for (const auto& line : threeCurrents()) {
        cfg += line + "\n";
    }
    cfg += "50\n";
    cfg += "0\n";                                                        
    cfg += "01/01/2026,00:00:00.000000\n";
    cfg += "01/01/2026,00:00:00.100000\n";
    cfg += "ASCII\n";
    cfg += "2.0\n";

    const auto parsed = analizador::parseCfg(bytesOf(cfg));
    CHECK(parsed.ok);
    CHECK(parsed.cfg.format_year == 2013);
    CHECK(parsed.cfg.rates.empty());
    CHECK(std::abs(parsed.cfg.time_multiplier - 2.0) < 1e-12);
    CHECK(parsed.cfg.start_time.valid);
    CHECK(parsed.cfg.start_time_text == "01/01/2026,00:00:00.000000");

    const auto dat = analizador::parseDat(
        bytesOf("1,0,0,0,0\n2,1000,0,0,0\n"), parsed.cfg);
    CHECK(dat.ok);
    CHECK(std::abs(dat.data.times_s[0]) < 1e-12);
    CHECK(std::abs(dat.data.times_s[1] - 0.002) < 1e-12);

                                                                           
                                                                              
                                                         
    std::string with_count = "TESTSTATION,DEVICE,2013\n3,3,0\n";
    for (const auto& line : threeCurrents()) {
        with_count += line + "\n";
    }
    with_count += "50\n0\n2\n01/01/2026,00:00:00.000000\n"
        "01/01/2026,00:00:00.100000\nASCII\n1.0\n";
    const auto counted = analizador::parseCfg(bytesOf(with_count));
    CHECK(counted.ok);
    CHECK(counted.cfg.start_time_text == "01/01/2026,00:00:00.000000");
    CHECK(counted.cfg.trigger_time_text == "01/01/2026,00:00:00.100000");
    CHECK(std::abs(counted.cfg.time_multiplier - 1.0) < 1e-12);

                                                                            
                                                         
    std::string dotted = cfg;
    const std::size_t multiplier = dotted.rfind("2.0");
    CHECK(multiplier != std::string::npos);
    if (multiplier != std::string::npos) {
        dotted.replace(multiplier, 3, ".5");
    }
    const auto dotted_parsed = analizador::parseCfg(bytesOf(dotted));
    CHECK(dotted_parsed.ok);
    CHECK(std::abs(dotted_parsed.cfg.time_multiplier - 0.5) < 1e-12);
}

void testCfgUnreadableDatesAreWarnings() {
    const auto parsed = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 2}}, "ASCII",
        "fecha-ilegible", "01/01/2026,00:00:00.100000")));
    CHECK(parsed.ok);
    CHECK(!parsed.cfg.start_time.valid);
    CHECK(parsed.cfg.start_time_text == "fecha-ilegible");
    CHECK(parsed.cfg.trigger_time.valid);
    CHECK(hasCode(parsed.diagnostics, "cfg.start_time_unreadable"));
    CHECK(parsed.firstError() == nullptr);
}

                                                                              
                     
                                                                              

void testAsciiScalingAndActualTimes() {
    const auto cfg = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(0.5, 10.0), {}, {{1000.0, 2}})));
    CHECK(cfg.ok);

    const std::string dat =
        "1,50,4,4,4\n"
        "2,1050,4,4,4\n";
    const auto parsed = analizador::parseDat(bytesOf(dat), cfg.cfg);
    CHECK(parsed.ok);
    CHECK(parsed.encoding == analizador::TextEncoding::Utf8);
    CHECK(parsed.data.sampleCount() == 2);
    CHECK(std::abs(parsed.data.dat_timestamp_us[0] - 50.0) < 1e-9);
    CHECK(std::abs(parsed.data.times_s[0] - 0.00005) < 1e-12);
    CHECK(std::abs(parsed.data.analogValue(0, 0) - 12.0) < 1e-12);
    CHECK(hasCode(parsed.diagnostics, "dat.first_sample_nonzero"));

    const auto complete = analizador::parseComtrade(
        bytesOf(makeCfgText(threeCurrents(0.5, 10.0), {}, {{1000.0, 2}})),
        bytesOf(dat));
    CHECK(complete.ok);
    CHECK(complete.record.trigger_offset_s.has_value());
    CHECK(std::abs(*complete.record.trigger_offset_s - 0.1) < 1e-9);
    CHECK(complete.record.first_sample_nonzero);
    CHECK(complete.record.dat_encoding == analizador::TextEncoding::Utf8);
    CHECK(std::abs(complete.record.first_sample_time_s - 0.00005) < 1e-12);
}

void testAsciiDiagnosticsAndValidation() {
    const auto cfg = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 2}})));
    CHECK(cfg.ok);

    const std::string duplicate_time =
        "1,0,0,0,0\n"
        "2,0,0,0,0\n";
    const auto duplicate = analizador::parseDat(bytesOf(duplicate_time), cfg.cfg);
    CHECK(!duplicate.ok);
    CHECK(hasCode(duplicate.diagnostics, "dat.timestamp_order"));

    const std::string negative_time =
        "1,-1,0,0,0\n"
        "2,1000,0,0,0\n";
    const auto negative = analizador::parseDat(bytesOf(negative_time), cfg.cfg);
    CHECK(!negative.ok);
    CHECK(hasCode(negative.diagnostics, "dat.timestamp_invalid"));

    const auto one_rate = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 1}})));
    const auto one_sample = analizador::parseDat(bytesOf("1,0,0,0,0\n"), one_rate.cfg);
    CHECK(!one_sample.ok);
    CHECK(hasCode(one_sample.diagnostics, "dat.too_few_samples"));

    const auto with_digital = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {digitalLine(1, "TRIP")}, {{1000.0, 2}})));
    const auto bad_digital = analizador::parseDat(
        bytesOf("1,0,0,0,0,2\n2,1000,0,0,0,0\n"), with_digital.cfg);
    CHECK(!bad_digital.ok);
    CHECK(hasCode(bad_digital.diagnostics, "dat.digital_state"));
    CHECK(bad_digital.diagnostics.back().has_line);
    CHECK(bad_digital.diagnostics.back().has_record);

    const auto fractional = analizador::parseDat(
        bytesOf("1,0,0,0,0,1.5\n2,1000,0,0,0,0\n"), with_digital.cfg);
    CHECK(!fractional.ok);
    CHECK(hasCode(fractional.diagnostics, "dat.digital_state"));

    const auto nonconsecutive = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {}, {{1000.0, 4}})));
    const auto gap = analizador::parseDat(
        bytesOf("1,0,0,0,0\n2,1000,0,0,0\n4,2000,0,0,0\n5,3000,0,0,0\n"),
        nonconsecutive.cfg);
    CHECK(gap.ok);
    CHECK(hasCode(gap.diagnostics, "dat.nonconsecutive_samples"));

    const auto overflow = analizador::parseCfg(bytesOf(makeCfgText(
        {analogLine(1, "IA", 1e308, 1e308),
         analogLine(2, "IB"), analogLine(3, "IC")},
        {}, {{1000.0, 2}})));
    const auto scaled_overflow = analizador::parseDat(
        bytesOf("1,0,1,0,0\n2,1000,1,0,0\n"), overflow.cfg);
    CHECK(!scaled_overflow.ok);
    CHECK(hasCode(scaled_overflow.diagnostics, "dat.scaling_nonfinite"));

    const auto packed_cfg = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), {digitalLine(1, "D1")}, {{1000.0, 2}})));
    (void)packed_cfg;
}

void testAsciiPackedDigital() {
    std::vector<std::string> digital;
    for (int i = 1; i <= 17; ++i) {
        digital.push_back(digitalLine(static_cast<std::size_t>(i), "D" + std::to_string(i)));
    }
    const auto cfg = analizador::parseCfg(bytesOf(makeCfgText(
        threeCurrents(), digital, {{1000.0, 2}})));
    CHECK(cfg.ok);
    CHECK(cfg.cfg.digital.size() == 17);

    const auto packed = analizador::parseDat(
        bytesOf("1,0,0,0,0,5,1\n2,1000,0,0,0,0,0\n"), cfg.cfg);
    CHECK(packed.ok);
    CHECK(packed.data.digitalValue(0, 0) == 1);
    CHECK(packed.data.digitalValue(0, 1) == 0);
    CHECK(packed.data.digitalValue(0, 2) == 1);
    CHECK(packed.data.digitalValue(0, 16) == 1);
    CHECK(packed.data.digitalValue(1, 16) == 0);

    const auto out_of_range = analizador::parseDat(
        bytesOf("1,0,0,0,0,0,65536\n2,1000,0,0,0,0,0\n"), cfg.cfg);
    CHECK(!out_of_range.ok);
    CHECK(hasCode(out_of_range.diagnostics, "dat.digital_word"));
}

void testParserMemoryLimits() {
    const std::string cfg_text = makeCfgText(threeCurrents(), {}, {{1000.0, 2}});

    analizador::CfgParseOptions text_options;
    text_options.max_text_bytes = cfg_text.size() - 1;
    const auto too_large = analizador::parseCfg(bytesOf(cfg_text), text_options);
    CHECK(!too_large.ok);
    CHECK(hasCode(too_large.diagnostics, "cfg.text_too_large"));
    CHECK(too_large.cfg.analog.empty());

    analizador::CfgParseOptions exact_text;
    exact_text.max_text_bytes = cfg_text.size();
    const auto exact = analizador::parseCfg(bytesOf(cfg_text), exact_text);
    CHECK(exact.ok);

    analizador::CfgParseOptions line_options;
    line_options.max_lines = 6;
    const auto too_many_lines = analizador::parseCfg(bytesOf(cfg_text), line_options);
    CHECK(!too_many_lines.ok);
    CHECK(hasCode(too_many_lines.diagnostics, "cfg.line_limit"));
    const auto* line_error = too_many_lines.firstError();
    CHECK(line_error != nullptr);
    if (line_error != nullptr) {
        CHECK(line_error->code == "cfg.line_limit");
        CHECK(line_error->has_line);
        CHECK(line_error->line == 7);
    }

    const auto single = analizador::parseCfg(bytesOf(makeCfgText(
        {analogLine(1, "IA")}, {}, {{1000.0, 2}})));
    CHECK(single.ok);
    const std::string dat_text = "1,0,0\n2,1000,0\n";

    analizador::DatParseOptions ascii_line_options;
    ascii_line_options.max_ascii_line_bytes = 6;
    const auto long_line = analizador::parseDat(bytesOf(dat_text), single.cfg,
                                                ascii_line_options);
    CHECK(!long_line.ok);
    CHECK(hasCode(long_line.diagnostics, "dat.ascii_line_too_long"));
    const auto* long_error = long_line.firstError();
    CHECK(long_error != nullptr);
    if (long_error != nullptr) {
        CHECK(long_error->has_line);
        CHECK(long_error->line == 2);
    }

    analizador::DatParseOptions sample_options;
    sample_options.max_samples = 1;
    const auto too_many_samples = analizador::parseDat(bytesOf(dat_text), single.cfg,
                                                       sample_options);
    CHECK(!too_many_samples.ok);
    CHECK(hasCode(too_many_samples.diagnostics, "dat.line_limit"));
}

void testBinaryFormats() {
    {
        std::vector<std::string> digital;
        for (int i = 1; i <= 17; ++i) {
            digital.push_back(digitalLine(static_cast<std::size_t>(i), "D" + std::to_string(i)));
        }
        const auto cfg = analizador::parseCfg(bytesOf(makeCfgText(
            threeCurrents(), digital, {{1000.0, 2}}, "BINARY")));
        CHECK(cfg.ok);

        std::vector<std::uint8_t> dat;
        putU32(dat, 1);
        putU32(dat, 0);
        putU16(dat, 1);
        putU16(dat, 2);
        putU16(dat, 3);
        putU16(dat, 0x0005);
        putU16(dat, 0x0001);
        putU32(dat, 2);
        putU32(dat, 1000);
        putU16(dat, static_cast<std::uint16_t>(-1));
        putU16(dat, 0);
        putU16(dat, 1);
        putU16(dat, 0);
        putU16(dat, 0);

        const auto parsed = analizador::parseDat(dat, cfg.cfg);
        CHECK(parsed.ok);
        CHECK(parsed.data.sampleCount() == 2);
        CHECK(std::abs(parsed.data.times_s[0]) < 1e-12);
        CHECK(std::abs(parsed.data.times_s[1] - 0.001) < 1e-12);
        CHECK(parsed.data.analogValue(0, 0) == 1.0);
        CHECK(parsed.data.analogValue(1, 0) == -1.0);
        CHECK(parsed.data.digitalValue(0, 0) == 1);
        CHECK(parsed.data.digitalValue(0, 1) == 0);
        CHECK(parsed.data.digitalValue(0, 2) == 1);
        CHECK(parsed.data.digitalValue(0, 16) == 1);
    }
    {
        const auto cfg = analizador::parseCfg(bytesOf(makeCfgText(
            {analogLine(1, "IA", 2.0, 1.0)}, {}, {{1000.0, 2}}, "BINARY32")));
        CHECK(cfg.ok);
        std::vector<std::uint8_t> dat;
        putU32(dat, 1);
        putU32(dat, 0);
        putU32(dat, 0xFFFFFFFBu);       
        putU32(dat, 2);
        putU32(dat, 1000);
        putU32(dat, 10);
        const auto parsed = analizador::parseDat(dat, cfg.cfg);
        CHECK(parsed.ok);
        CHECK(parsed.data.analogValue(0, 0) == -9.0);
        CHECK(parsed.data.analogValue(1, 0) == 21.0);
    }
    {
        const auto cfg = analizador::parseCfg(bytesOf(makeCfgText(
            {analogLine(1, "IA", 2.0, 0.5)}, {}, {{1000.0, 2}}, "FLOAT32")));
        CHECK(cfg.ok);
        std::vector<std::uint8_t> dat;
        putU32(dat, 1);
        putU32(dat, 0);
        putF32(dat, 1.5f);
        putU32(dat, 2);
        putU32(dat, 1000);
        putF32(dat, -2.0f);
        const auto parsed = analizador::parseDat(dat, cfg.cfg);
        CHECK(parsed.ok);
        CHECK(std::abs(parsed.data.analogValue(0, 0) - 3.5) < 1e-9);
        CHECK(std::abs(parsed.data.analogValue(1, 0) + 3.5) < 1e-9);
    }
}

                                                                              
      
                                                                              

void testZipPathRules() {
    CHECK(analizador::zip::normalizeMemberName(".\\folder//sub/../record.cfg") ==
          "folder/record.cfg");
    CHECK(analizador::zip::normalizeMemberName("folder/./record.dat") ==
          "folder/record.dat");
    CHECK(analizador::zip::normalizeMemberName(".") == "");

    CHECK(analizador::zip::isIgnoredMember("__MACOSX/folder/._record.cfg"));
    CHECK(analizador::zip::isIgnoredMember("folder/.DS_Store"));
    CHECK(analizador::zip::isIgnoredMember("folder/Thumbs.db"));
    CHECK(analizador::zip::isIgnoredMember("folder/desktop.ini"));
    CHECK(!analizador::zip::isIgnoredMember("folder/record.cfg"));
}

void testZipPairingAndBundles() {
    auto zip = buildZip({
        {"__MACOSX/folder/._record.cfg", 0, 0, bytesOf("meta"), bytesOf("meta")},
        {"__MACOSX/folder/._record.dat", 0, 0, bytesOf("meta"), bytesOf("meta")},
        {"folder/record.cfg", 0, 0, bytesOf("cfg"), bytesOf("cfg")},
        {"folder/record.dat", 0, 0, bytesOf("dat"), bytesOf("dat")},
        {"folder/events.csv", 0, 0, bytesOf("log"), bytesOf("log")},
        {"folder/.DS_Store", 0, 0, bytesOf("meta"), bytesOf("meta")},
        {"folder/Thumbs.db", 0, 0, bytesOf("meta"), bytesOf("meta")},
    });
    auto opened = analizador::zip::Archive::openFromMemory(zip);
    CHECK(opened.ok);
    CHECK(opened.archive != nullptr);
    if (opened.archive) {
        const auto pairs = opened.archive->pairRecords();
        CHECK(pairs.failures.empty());
        CHECK(pairs.pairs.size() == 1);
        CHECK(pairs.pairs[0].record_name == "folder/record");
        CHECK(pairs.pairs[0].cfg_name == "folder/record.cfg");
        CHECK(pairs.pairs[0].dat_name == "folder/record.dat");

        const auto bundles = opened.archive->recordBundles();
        CHECK(bundles.failures.empty());
        CHECK(bundles.pairs.size() == 1);
        CHECK(bundles.pairs[0].aux_names.size() == 1);
        CHECK(bundles.pairs[0].aux_names[0] == "folder/events.csv");

        const auto read_cfg = opened.archive->readEntryByName("folder/record.cfg");
        CHECK(read_cfg.ok);
        CHECK(textOf(read_cfg.data) == "cfg");
    }

    auto multi = buildZip({
        {"folderA/record.cfg", 0, 0, bytesOf("a"), bytesOf("a")},
        {"folderA/record.dat", 0, 0, bytesOf("a"), bytesOf("a")},
        {"folderA/events.csv", 0, 0, bytesOf("a-log"), bytesOf("a-log")},
        {"folderB/record.cfg", 0, 0, bytesOf("b"), bytesOf("b")},
        {"folderB/record.dat", 0, 0, bytesOf("b"), bytesOf("b")},
        {"folderB/events.csv", 0, 0, bytesOf("b-log"), bytesOf("b-log")},
        {"folderC/RECORD.CFG", 0, 0, bytesOf("c"), bytesOf("c")},
        {"folderC/record.DAT", 0, 0, bytesOf("c"), bytesOf("c")},
        {"folderD\\rec.cfg", 0, 0, bytesOf("d"), bytesOf("d")},
        {"folderD\\rec.dat", 0, 0, bytesOf("d"), bytesOf("d")},
        {"folderE/only.cfg", 0, 0, bytesOf("e"), bytesOf("e")},
    });
    auto multi_open = analizador::zip::Archive::openFromMemory(multi);
    CHECK(multi_open.ok);
    if (multi_open.archive) {
        const auto bundles = multi_open.archive->recordBundles();
        CHECK(bundles.pairs.size() == 4);
        CHECK(hasMessage(bundles.failures, "No existe el DAT"));
        std::vector<std::string> names;
        for (const auto& pair : bundles.pairs) {
            names.push_back(pair.record_name);
        }
        CHECK(std::find(names.begin(), names.end(), "folderA/record") != names.end());
        CHECK(std::find(names.begin(), names.end(), "folderB/record") != names.end());
        CHECK(std::find(names.begin(), names.end(), "folderC/RECORD") != names.end());
        CHECK(std::find(names.begin(), names.end(), "folderD/rec") != names.end());
        std::size_t folder_a_aux = 0;
        std::size_t folder_b_aux = 0;
        for (const auto& pair : bundles.pairs) {
            if (pair.record_name == "folderA/record") {
                folder_a_aux = pair.aux_names.size();
                CHECK(pair.aux_names[0] == "folderA/events.csv");
            }
            if (pair.record_name == "folderB/record") {
                folder_b_aux = pair.aux_names.size();
                CHECK(pair.aux_names[0] == "folderB/events.csv");
            }
        }
        CHECK(folder_a_aux == 1);
        CHECK(folder_b_aux == 1);
    }
}

void testZipDuplicateAndAuxRules() {
    auto duplicates = buildZip({
        {"dup/record.cfg", 0, 0, bytesOf("one"), bytesOf("one")},
        {"dup/record.cfg", 0, 0, bytesOf("two"), bytesOf("two")},
        {"dup/record.dat", 0, 0, bytesOf("dat"), bytesOf("dat")},
        {"dup2/record.cfg", 0, 0, bytesOf("cfg"), bytesOf("cfg")},
        {"dup2/record.dat", 0, 0, bytesOf("one"), bytesOf("one")},
        {"dup2/record.dat", 0, 0, bytesOf("two"), bytesOf("two")},
    });
    auto duplicate_open = analizador::zip::Archive::openFromMemory(duplicates);
    CHECK(duplicate_open.ok);
    if (duplicate_open.archive) {
        const auto pairs = duplicate_open.archive->pairRecords();
        CHECK(pairs.pairs.empty());
        CHECK(hasMessage(pairs.failures, "Ruta CFG duplicada"));
        CHECK(hasMessage(pairs.failures, "varios DAT"));
    }

    auto aux_duplicate = buildZip({
        {"folder/record.cfg", 0, 0, bytesOf("cfg"), bytesOf("cfg")},
        {"folder/record.dat", 0, 0, bytesOf("dat"), bytesOf("dat")},
        {"folder/events.csv", 0, 0, bytesOf("first"), bytesOf("first")},
        {"folder\\EVENTS.CSV", 0, 0, bytesOf("second"), bytesOf("second")},
    });
    auto aux_open = analizador::zip::Archive::openFromMemory(aux_duplicate);
    CHECK(aux_open.ok);
    if (aux_open.archive) {
        const auto bundles = aux_open.archive->recordBundles();
        CHECK(bundles.pairs.size() == 1);
        CHECK(bundles.pairs[0].aux_names.empty());
        CHECK(hasMessage(bundles.failures, "Ruta auxiliar duplicada"));
    }

    auto single_root_aux = buildZip({
        {"only/record.cfg", 0, 0, bytesOf("a"), bytesOf("a")},
        {"only/record.dat", 0, 0, bytesOf("a"), bytesOf("a")},
        {"root.csv", 0, 0, bytesOf("root-log"), bytesOf("root-log")},
    });
    auto root_open = analizador::zip::Archive::openFromMemory(single_root_aux);
    CHECK(root_open.ok);
    if (root_open.archive) {
        const auto bundles = root_open.archive->recordBundles();
        CHECK(bundles.failures.empty());
        CHECK(bundles.pairs.size() == 1);
        CHECK(bundles.pairs[0].aux_names.size() == 1);
        CHECK(bundles.pairs[0].aux_names[0] == "root.csv");
    }

    auto case_folder = buildZip({
        {"Folder/record.cfg", 0, 0, bytesOf("cfg"), bytesOf("cfg")},
        {"Folder/record.dat", 0, 0, bytesOf("dat"), bytesOf("dat")},
        {"folder/events.csv", 0, 0, bytesOf("log"), bytesOf("log")},
    });
    auto case_open = analizador::zip::Archive::openFromMemory(case_folder);
    CHECK(case_open.ok);
    if (case_open.archive) {
        const auto bundles = case_open.archive->recordBundles();
        CHECK(bundles.failures.empty());
        CHECK(bundles.pairs[0].aux_names.size() == 1);
        CHECK(bundles.pairs[0].aux_names[0] == "folder/events.csv");
    }
}

void testZipLimitsEncryptionAndPaths() {
    auto encrypted_zip = buildZip({
        {"a.cfg", 0, 1, bytesOf("cfg"), bytesOf("cfg")},
        {"a.dat", 0, 0, bytesOf("dat"), bytesOf("dat")},
    });
    auto encrypted_open = analizador::zip::Archive::openFromMemory(encrypted_zip);
    CHECK(encrypted_open.ok);
    if (encrypted_open.archive) {
        const auto pairs = encrypted_open.archive->pairRecords();
        CHECK(pairs.pairs.empty());
        CHECK(hasMessage(pairs.failures, "cifrado"));
        const auto read = encrypted_open.archive->readEntryByName("a.cfg");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.encrypted"));
    }

    auto ratio_zip = buildZip({
        {"b.cfg", 0, 0, bytesOf("x"), bytesOf("x"), 1u, 10000u, std::nullopt},
        {"b.dat", 0, 0, bytesOf("x"), bytesOf("x")},
    });
    auto ratio_open = analizador::zip::Archive::openFromMemory(ratio_zip);
    CHECK(ratio_open.ok);
    if (ratio_open.archive) {
        const auto pairs = ratio_open.archive->pairRecords();
        CHECK(pairs.pairs.empty());
        CHECK(hasMessage(pairs.failures, "relación de compresión"));
        const auto read = ratio_open.archive->readEntryByName("b.cfg");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.compression_ratio"));
    }

    auto many = buildZip({
        {"f0.cfg", 0, 0, bytesOf("a"), bytesOf("a")},
        {"f1.cfg", 0, 0, bytesOf("b"), bytesOf("b")},
    });
    analizador::zip::Limits small_entries;
    small_entries.max_entries = 1;
    auto many_open = analizador::zip::Archive::openFromMemory(many, small_entries);
    CHECK(!many_open.ok);
    CHECK(hasCode(many_open.diagnostics, "zip.entry_limit"));

    auto total_zip = buildZip({
        {"c.cfg", 0, 0, bytesOf("012345"), bytesOf("012345")},
        {"c.dat", 0, 0, bytesOf("012345"), bytesOf("012345")},
    });
    analizador::zip::Limits small_total;
    small_total.max_total_bytes = 10;
    auto total_open = analizador::zip::Archive::openFromMemory(total_zip, small_total);
    CHECK(!total_open.ok);
    CHECK(total_open.archive == nullptr);
    CHECK(hasCode(total_open.diagnostics, "zip.total_limit"));

    auto member_zip = buildZip({
        {"d.cfg", 0, 0, bytesOf("01234567890"), bytesOf("01234567890")},
        {"d.dat", 0, 0, bytesOf("x"), bytesOf("x")},
    });
    analizador::zip::Limits small_member;
    small_member.max_member_bytes = 10;
    auto member_open = analizador::zip::Archive::openFromMemory(member_zip, small_member);
    CHECK(member_open.ok);
    if (member_open.archive) {
        const auto pairs = member_open.archive->pairRecords();
        CHECK(pairs.pairs.empty());
        CHECK(hasMessage(pairs.failures, "límite por archivo"));
        const auto read = member_open.archive->readEntryByName("d.cfg");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.member_limit"));
    }

    auto invalid_path = buildZip({
        {"../evil.cfg", 0, 0, bytesOf("cfg"), bytesOf("cfg")},
        {"../evil.dat", 0, 0, bytesOf("dat"), bytesOf("dat")},
    });
    auto invalid_open = analizador::zip::Archive::openFromMemory(invalid_path);
    CHECK(invalid_open.ok);
    if (invalid_open.archive) {
        const auto pairs = invalid_open.archive->pairRecords();
        CHECK(pairs.pairs.empty());
        CHECK(hasMessage(pairs.failures, "Ruta CFG inválida"));
        const auto read = invalid_open.archive->readEntryByName("../evil.cfg");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.path_invalid"));
    }

    auto unsupported = buildZip({
        {"e.cfg", 12, 0, bytesOf("x"), bytesOf("x")},
    });
    auto unsupported_open = analizador::zip::Archive::openFromMemory(unsupported);
    CHECK(unsupported_open.ok);
    if (unsupported_open.archive) {
        const auto read = unsupported_open.archive->readEntryByName("e.cfg");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.method_unsupported"));
    }

    auto zip64 = buildZip({{"f.cfg", 0, 0, bytesOf("x"), bytesOf("x")}});
    const std::size_t eocd = zip64.size() - 22;
    zip64[eocd + 12] = 0xFF;
    zip64[eocd + 13] = 0xFF;
    zip64[eocd + 14] = 0xFF;
    zip64[eocd + 15] = 0xFF;
    auto zip64_open = analizador::zip::Archive::openFromMemory(zip64);
    CHECK(!zip64_open.ok);
    CHECK(hasCode(zip64_open.diagnostics, "zip.zip64_unsupported"));
}

void testZipCrcAndDeflate() {
    auto crc_zip = buildZip({
        {"bad.txt", 0, 0, bytesOf("hello"), bytesOf("hello"), std::nullopt, std::nullopt, 0u},
    });
    auto crc_open = analizador::zip::Archive::openFromMemory(crc_zip);
    CHECK(crc_open.ok);
    if (crc_open.archive) {
        const auto read = crc_open.archive->readEntryByName("bad.txt");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.crc_mismatch"));
    }

    const std::string fixed_text = "hello comtrade";
    const auto fixed_raw = hexBytes("cb48cdc9c95748cecf2d294a4c490500");
    auto fixed_zip = buildZip({
        {"fixed.dat", 8, 0, fixed_raw, bytesOf(fixed_text)},
    });
    auto fixed_open = analizador::zip::Archive::openFromMemory(fixed_zip);
    CHECK(fixed_open.ok);
    if (fixed_open.archive) {
        const auto read = fixed_open.archive->readEntryByName("fixed.dat");
        CHECK(read.ok);
        CHECK(textOf(read.data) == fixed_text);
    }

    const std::string dynamic_text = dynamicFixtureData();
    const auto dynamic_raw = hexBytes(
        "edcc490e82401405c0bda778273088132c89ba3426a42fc0d0ccf0a1a1994ea"
        "ff11c6f5d49a94263b065522336b274c8644565db7e84ccda60fa7113ed1b52c"
        "98f787cde2a0c9e2f3827f77cb9deee9e8f284e529de54559d54ddb493f9871b"
        "2f3b26efb41f1e6cd9b376fdebc79f3e6cd9b376fdebc79f3e6cdfb7f7f01");
    auto dynamic_zip = buildZip({
        {"dynamic.dat", 8, 0, dynamic_raw, bytesOf(dynamic_text)},
    });
    auto dynamic_open = analizador::zip::Archive::openFromMemory(dynamic_zip);
    CHECK(dynamic_open.ok);
    if (dynamic_open.archive) {
        const auto read = dynamic_open.archive->readEntryByName("dynamic.dat");
        CHECK(read.ok);
        CHECK(read.data.size() == 5000);
        CHECK(textOf(read.data) == dynamic_text);
    }

    const std::string stored_text = "stored deflate payload";
    auto stored_raw = storedDeflate(bytesOf(stored_text));
    auto stored_zip = buildZip({
        {"stored.dat", 8, 0, stored_raw, bytesOf(stored_text)},
    });
    auto stored_open = analizador::zip::Archive::openFromMemory(stored_zip);
    CHECK(stored_open.ok);
    if (stored_open.archive) {
        const auto read = stored_open.archive->readEntryByName("stored.dat");
        CHECK(read.ok);
        CHECK(textOf(read.data) == stored_text);
    }

    auto truncated_raw = fixed_raw;
    truncated_raw.resize(6);
    auto truncated_zip = buildZip({
        {"truncated.dat", 8, 0, truncated_raw, bytesOf(fixed_text)},
    });
    auto truncated_open = analizador::zip::Archive::openFromMemory(truncated_zip);
    CHECK(truncated_open.ok);
    if (truncated_open.archive) {
        const auto read = truncated_open.archive->readEntryByName("truncated.dat");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.deflate_invalid"));
    }

    auto lying_zip = buildZip({
        {"lying.dat", 8, 0, fixed_raw, bytesOf("short")},
    });
    auto lying_open = analizador::zip::Archive::openFromMemory(lying_zip);
    CHECK(lying_open.ok);
    if (lying_open.archive) {
        const auto read = lying_open.archive->readEntryByName("lying.dat");
        CHECK(!read.ok);
        CHECK(hasCode(read.diagnostics, "zip.size_mismatch"));
    }
}

void testZipOpenFromFile() {
    const auto zip = buildZip({{"file.txt", 0, 0, bytesOf("file"), bytesOf("file")}});
    const std::filesystem::path path = std::filesystem::current_path() / ".zip_reader_test_tmp.zip";
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(zip.data()),
                     static_cast<std::streamsize>(zip.size()));
    }
    auto opened = analizador::zip::Archive::openFromFile(path);
    CHECK(opened.ok);
    if (opened.archive) {
        const auto read = opened.archive->readEntryByName("file.txt");
        CHECK(read.ok);
        CHECK(textOf(read.data) == "file");
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

}              

int main() {
    testTextDecoding();
    testDateTimeParsing();
    testCfgValidationAndMetadata();
    testCfg2013ZeroRatesAndTimeMultiplier();
    testCfgUnreadableDatesAreWarnings();
    testAsciiScalingAndActualTimes();
    testAsciiDiagnosticsAndValidation();
    testAsciiPackedDigital();
    testParserMemoryLimits();
    testBinaryFormats();
    testZipPathRules();
    testZipPairingAndBundles();
    testZipDuplicateAndAuxRules();
    testZipLimitsEncryptionAndPaths();
    testZipCrcAndDeflate();
    testZipOpenFromFile();

    if (g_failures == 0) {
        std::cout << "OK: pruebas nativas COMTRADE/ZIP completadas.\n";
        return 0;
    }
    std::cerr << "FALLOS: " << g_failures << "\n";
    return 1;
}
