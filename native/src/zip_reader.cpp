#include "analizador/comtrade.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace analizador {
namespace zip {
namespace {

constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054B50u;
constexpr std::uint32_t kCentralDirectorySignature = 0x02014B50u;
constexpr std::uint32_t kLocalHeaderSignature = 0x04034B50u;
constexpr std::uint16_t kFlagEncrypted = 0x0001u;
constexpr std::uint16_t kFlagUtf8 = 0x0800u;
constexpr std::uint16_t kFlagDataDescriptor = 0x0008u;

Diagnostic makeDiagnostic(DiagnosticSeverity severity, std::string code, std::string message) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    return diagnostic;
}

Diagnostic failure(std::string code, std::string message) {
    return makeDiagnostic(DiagnosticSeverity::Error, std::move(code), std::move(message));
}

bool readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        return false;
    }
    value = static_cast<std::uint16_t>(bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
    return true;
}

bool readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t& value) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[offset]) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    return true;
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

std::string decodeCp437(const std::uint8_t* data, std::size_t size) {
    static const std::uint16_t high[128] = {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
        0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
        0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
        0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
        0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
        0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
        0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
        0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
    };
    std::string out;
    out.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint8_t byte = data[i];
        if (byte < 0x80) {
            out.push_back(static_cast<char>(byte));
        } else {
            appendUtf8(out, high[byte - 0x80]);
        }
    }
    return out;
}

std::string decodeZipName(const std::uint8_t* data, std::size_t size, bool marked_utf8) {
    if (marked_utf8 && isValidUtf8(data, size)) {
        return std::string(reinterpret_cast<const char*>(data), size);
    }
    return decodeCp437(data, size);
}

std::string lowerAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c >= 'A' && c <= 'Z') {
            out.push_back(static_cast<char>(c - 'A' + 'a'));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::size_t lastSlash(std::string_view path) {
    return path.rfind('/');
}

bool hasExtension(std::string_view path) {
    const std::size_t slash = lastSlash(path);
    const std::size_t segment_start = slash == std::string_view::npos ? 0 : slash + 1;
    const std::size_t dot = path.rfind('.');
    return dot != std::string_view::npos &&
        dot > segment_start && dot + 1 < path.size();
}

std::string extensionLower(std::string_view path) {
    if (!hasExtension(path)) {
        return {};
    }
    return lowerAscii(path.substr(path.rfind('.')));
}

std::string stripExtension(std::string_view path) {
    if (!hasExtension(path)) {
        return std::string(path);
    }
    return std::string(path.substr(0, path.rfind('.')));
}

std::string replaceExtension(std::string_view path, std::string_view extension) {
    return stripExtension(path) + std::string(extension);
}

std::string parentFolder(std::string_view normalized_path) {
    const std::size_t slash = lastSlash(normalized_path);
    if (slash == std::string_view::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return std::string(normalized_path.substr(0, slash));
}

std::string baseName(std::string_view normalized_path) {
    const std::size_t slash = lastSlash(normalized_path);
    if (slash == std::string_view::npos) {
        return std::string(normalized_path);
    }
    return std::string(normalized_path.substr(slash + 1));
}

bool isInvalidPath(const std::string& normalized, const std::string& original) {
    if (normalized.empty() || original.empty()) {
        return true;
    }
    if (normalized.find('\0') != std::string::npos ||
        original.find('\0') != std::string::npos) {
        return true;
    }
    if (normalized.front() == '/') {
        return true;
    }
    if (normalized.size() >= 2 &&
        ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
         (normalized[0] >= 'a' && normalized[0] <= 'z')) &&
        normalized[1] == ':') {
        return true;
    }
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t slash = normalized.find('/', start);
        const std::string_view component = slash == std::string::npos
            ? std::string_view(normalized).substr(start)
            : std::string_view(normalized).substr(start, slash - start);
        if (component == "..") {
            return true;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    for (const char c : normalized) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            return true;
        }
    }
    return false;
}

                                                                              
        
                                                                              

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0 ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            values[i] = value;
        }
        return values;
    }();
    return table;
}

std::uint32_t computeCrc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    const auto& table = crcTable();
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

                                                                              
                                                 
                                                                              

class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool readBits(unsigned count, std::uint32_t& value) {
        if (count > 24) {
            return false;
        }
        std::uint32_t result = 0;
        for (unsigned i = 0; i < count; ++i) {
            const std::size_t byte_index = bit_position_ >> 3;
            if (byte_index >= size_) {
                return false;
            }
            const unsigned bit_index = static_cast<unsigned>(bit_position_ & 7u);
            const std::uint32_t bit = (data_[byte_index] >> bit_index) & 1u;
            result |= bit << i;
            ++bit_position_;
        }
        value = result;
        return true;
    }

    bool alignToByte() {
        const std::size_t aligned = (bit_position_ + 7u) & ~static_cast<std::size_t>(7u);
        if (aligned > size_ * 8u) {
            return false;
        }
        bit_position_ = aligned;
        return true;
    }

    bool readBytes(std::uint8_t* destination, std::size_t count) {
        if ((bit_position_ & 7u) != 0) {
            return false;
        }
        const std::size_t byte_index = bit_position_ >> 3;
        if (byte_index > size_ || size_ - byte_index < count) {
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            destination[i] = data_[byte_index + i];
        }
        bit_position_ = (byte_index + count) * 8u;
        return true;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t bit_position_ = 0;
};

struct HuffmanTree {
    std::array<std::uint16_t, 16> counts{};
    std::vector<std::uint16_t> symbols;

    bool build(const std::uint8_t* lengths, std::size_t count) {
        counts.fill(0);
        symbols.clear();
        for (std::size_t i = 0; i < count; ++i) {
            if (lengths[i] > 15) {
                return false;
            }
            if (lengths[i] != 0) {
                ++counts[lengths[i]];
            }
        }
        int left = 1;
        for (int length = 1; length <= 15; ++length) {
            left <<= 1;
            left -= static_cast<int>(counts[length]);
            if (left < 0) {
                return false;                           
            }
        }
        std::array<std::uint16_t, 16> offsets{};
        std::uint16_t total = 0;
        for (int length = 1; length <= 15; ++length) {
            offsets[length] = total;
            total = static_cast<std::uint16_t>(total + counts[length]);
        }
        symbols.resize(total);
        std::array<std::uint16_t, 16> cursor = offsets;
        for (std::size_t symbol = 0; symbol < count; ++symbol) {
            const std::uint8_t length = lengths[symbol];
            if (length != 0) {
                symbols[cursor[length]++] = static_cast<std::uint16_t>(symbol);
            }
        }
        return true;
    }

    bool decode(BitReader& reader, std::uint16_t& symbol) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= 15; ++length) {
            std::uint32_t bit = 0;
            if (!reader.readBits(1, bit)) {
                return false;
            }
            code |= static_cast<int>(bit);
            const int count = static_cast<int>(counts[length]);
            if (code - first < count) {
                const int position = index + (code - first);
                if (position < 0 || static_cast<std::size_t>(position) >= symbols.size()) {
                    return false;
                }
                symbol = symbols[static_cast<std::size_t>(position)];
                return true;
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return false;
    }

    bool empty() const noexcept {
        return symbols.empty();
    }
};

class Inflater {
public:
    Inflater(std::vector<std::uint8_t>& output, std::size_t max_output)
        : output_(output), max_output_(max_output) {}

    bool run(const std::uint8_t* data, std::size_t size) {
        BitReader reader(data, size);
        bool final_block = false;
        while (!final_block) {
            std::uint32_t final_bit = 0;
            std::uint32_t type = 0;
            if (!reader.readBits(1, final_bit) || !reader.readBits(2, type)) {
                return setError("flujo Deflate truncado");
            }
            final_block = final_bit != 0;
            if (type == 0) {
                if (!inflateStored(reader)) {
                    return false;
                }
            } else if (type == 1) {
                if (!inflateFixed(reader)) {
                    return false;
                }
            } else if (type == 2) {
                if (!inflateDynamic(reader)) {
                    return false;
                }
            } else {
                return setError("tipo de bloque Deflate reservado");
            }
        }
        return true;
    }

    const std::string& error() const noexcept { return error_; }

private:
    bool setError(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
        return false;
    }

    bool writeByte(std::uint8_t value) {
        if (output_.size() >= max_output_) {
            return setError("la salida descomprimida excede el límite permitido");
        }
        output_.push_back(value);
        return true;
    }

    bool copyFromHistory(std::size_t distance, std::size_t length) {
        if (distance == 0 || distance > output_.size()) {
            return setError("distancia Deflate fuera del historial");
        }
        if (output_.size() > max_output_ || length > max_output_ - output_.size()) {
            return setError("la salida descomprimida excede el límite permitido");
        }
        for (std::size_t i = 0; i < length; ++i) {
            output_.push_back(output_[output_.size() - distance]);
        }
        return true;
    }

    bool inflateStored(BitReader& reader) {
        if (!reader.alignToByte()) {
            return setError("bloque stored sin alineación válida");
        }
        std::uint32_t length = 0;
        std::uint32_t complement = 0;
        if (!reader.readBits(16, length) || !reader.readBits(16, complement)) {
            return setError("cabecera de bloque stored truncada");
        }
        if ((length ^ 0xFFFFu) != complement) {
            return setError("longitud de bloque stored inconsistente");
        }
        if (output_.size() > max_output_ || length > max_output_ - output_.size()) {
            return setError("la salida descomprimida excede el límite permitido");
        }
        const std::size_t old_size = output_.size();
        output_.resize(old_size + static_cast<std::size_t>(length));
        if (length > 0 &&
            !reader.readBytes(output_.data() + old_size, static_cast<std::size_t>(length))) {
            output_.resize(old_size);
            return setError("bloque stored truncado");
        }
        return true;
    }

    bool inflateLiterals(BitReader& reader,
                         const HuffmanTree& literal_tree,
                         const HuffmanTree& distance_tree) {
        static const std::uint16_t length_base[29] = {
            3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
            35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
        };
        static const std::uint8_t length_extra[29] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
            3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
        };
        static const std::uint16_t distance_base[30] = {
            1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
            257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
            8193, 12289, 16385, 24577,
        };
        static const std::uint8_t distance_extra[30] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
            7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
        };

        while (true) {
            std::uint16_t symbol = 0;
            if (!literal_tree.decode(reader, symbol)) {
                return setError("código literal/longitud Deflate inválido");
            }
            if (symbol < 256) {
                if (!writeByte(static_cast<std::uint8_t>(symbol))) {
                    return false;
                }
                continue;
            }
            if (symbol == 256) {
                return true;
            }
            if (symbol < 257 || symbol > 285) {
                return setError("símbolo de longitud Deflate inválido");
            }
            const std::size_t length_index = static_cast<std::size_t>(symbol - 257);
            std::uint32_t extra = 0;
            if (length_extra[length_index] != 0 &&
                !reader.readBits(length_extra[length_index], extra)) {
                return setError("bits extra de longitud truncados");
            }
            const std::size_t length = static_cast<std::size_t>(length_base[length_index]) + extra;

            std::uint16_t distance_symbol = 0;
            if (distance_tree.empty() || !distance_tree.decode(reader, distance_symbol)) {
                return setError("código de distancia Deflate inválido");
            }
            if (distance_symbol >= 30) {
                return setError("símbolo de distancia Deflate reservado");
            }
            std::uint32_t distance_extra_bits = 0;
            if (distance_extra[distance_symbol] != 0 &&
                !reader.readBits(distance_extra[distance_symbol], distance_extra_bits)) {
                return setError("bits extra de distancia truncados");
            }
            const std::size_t distance = static_cast<std::size_t>(distance_base[distance_symbol]) +
                distance_extra_bits;
            if (!copyFromHistory(distance, length)) {
                return false;
            }
        }
    }

    bool inflateFixed(BitReader& reader) {
        std::array<std::uint8_t, 288> literal_lengths{};
        for (std::size_t i = 0; i <= 143; ++i) {
            literal_lengths[i] = 8;
        }
        for (std::size_t i = 144; i <= 255; ++i) {
            literal_lengths[i] = 9;
        }
        for (std::size_t i = 256; i <= 279; ++i) {
            literal_lengths[i] = 7;
        }
        for (std::size_t i = 280; i <= 287; ++i) {
            literal_lengths[i] = 8;
        }
        std::array<std::uint8_t, 32> distance_lengths{};
        distance_lengths.fill(5);

        HuffmanTree literal_tree;
        HuffmanTree distance_tree;
        if (!literal_tree.build(literal_lengths.data(), literal_lengths.size()) ||
            !distance_tree.build(distance_lengths.data(), distance_lengths.size())) {
            return setError("árbol fijo Deflate inválido");
        }
        return inflateLiterals(reader, literal_tree, distance_tree);
    }

    bool inflateDynamic(BitReader& reader) {
        std::uint32_t hlit = 0;
        std::uint32_t hdist = 0;
        std::uint32_t hclen = 0;
        if (!reader.readBits(5, hlit) || !reader.readBits(5, hdist) || !reader.readBits(4, hclen)) {
            return setError("cabecera dinámica Deflate truncada");
        }
        const std::size_t literal_count = static_cast<std::size_t>(hlit) + 257;
        const std::size_t distance_count = static_cast<std::size_t>(hdist) + 1;
        const std::size_t code_length_count = static_cast<std::size_t>(hclen) + 4;
        if (literal_count > 286 || distance_count > 32 || code_length_count > 19) {
            return setError("conteos dinámicos Deflate fuera de rango");
        }

        static const std::array<std::size_t, 19> order = {
            16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
        };
        std::array<std::uint8_t, 19> code_lengths{};
        for (std::size_t i = 0; i < code_length_count; ++i) {
            std::uint32_t value = 0;
            if (!reader.readBits(3, value)) {
                return setError("longitudes de código Deflate truncadas");
            }
            code_lengths[order[i]] = static_cast<std::uint8_t>(value);
        }
        HuffmanTree code_length_tree;
        if (!code_length_tree.build(code_lengths.data(), code_lengths.size())) {
            return setError("árbol de longitudes Deflate inválido");
        }

        const std::size_t total = literal_count + distance_count;
        std::vector<std::uint8_t> lengths(total, 0);
        std::size_t index = 0;
        while (index < total) {
            std::uint16_t symbol = 0;
            if (!code_length_tree.decode(reader, symbol)) {
                return setError("código de longitud Deflate inválido");
            }
            if (symbol <= 15) {
                lengths[index++] = static_cast<std::uint8_t>(symbol);
            } else if (symbol == 16) {
                if (index == 0) {
                    return setError("repetición Deflate sin longitud previa");
                }
                std::uint32_t extra = 0;
                if (!reader.readBits(2, extra)) {
                    return setError("repetición Deflate truncada");
                }
                const std::size_t repeat = 3 + static_cast<std::size_t>(extra);
                if (repeat > total - index) {
                    return setError("repetición Deflate excede el árbol");
                }
                const std::uint8_t previous = lengths[index - 1];
                for (std::size_t i = 0; i < repeat; ++i) {
                    lengths[index++] = previous;
                }
            } else if (symbol == 17) {
                std::uint32_t extra = 0;
                if (!reader.readBits(3, extra)) {
                    return setError("ceros Deflate truncados");
                }
                const std::size_t repeat = 3 + static_cast<std::size_t>(extra);
                if (repeat > total - index) {
                    return setError("ceros Deflate exceden el árbol");
                }
                index += repeat;
            } else if (symbol == 18) {
                std::uint32_t extra = 0;
                if (!reader.readBits(7, extra)) {
                    return setError("ceros Deflate truncados");
                }
                const std::size_t repeat = 11 + static_cast<std::size_t>(extra);
                if (repeat > total - index) {
                    return setError("ceros Deflate exceden el árbol");
                }
                index += repeat;
            } else {
                return setError("símbolo de longitud Deflate inválido");
            }
        }

        if (lengths[256] == 0) {
            return setError("árbol dinámico sin código de fin de bloque");
        }
        HuffmanTree literal_tree;
        HuffmanTree distance_tree;
        if (!literal_tree.build(lengths.data(), literal_count)) {
            return setError("árbol literal/longitud Deflate inválido");
        }
        if (!distance_tree.build(lengths.data() + literal_count, distance_count)) {
            return setError("árbol de distancia Deflate inválido");
        }
        return inflateLiterals(reader, literal_tree, distance_tree);
    }

    std::vector<std::uint8_t>& output_;
    std::size_t max_output_ = 0;
    std::string error_;
};

bool inflateDeflate(const std::uint8_t* data,
                    std::size_t size,
                    std::vector<std::uint8_t>& output,
                    std::size_t max_output,
                    std::string& error) {
    output.clear();
    Inflater inflater(output, max_output);
    if (!inflater.run(data, size)) {
        error = inflater.error();
        output.clear();
        return false;
    }
    return true;
}

                                                                              
                            
                                                                              

struct IndexParseResult {
    bool ok = false;
    std::vector<Entry> entries;
    std::vector<Diagnostic> diagnostics;
    bool total_limit_exceeded = false;
};

IndexParseResult parseCentralDirectory(const std::vector<std::uint8_t>& bytes,
                                       const Limits& limits) {
    IndexParseResult result;
    if (bytes.size() < 22) {
        result.diagnostics.push_back(failure("zip.too_small", "El archivo es demasiado pequeño para ser un ZIP."));
        return result;
    }

    std::size_t search_start = 0;
    if (bytes.size() > 22 + 0xFFFF) {
        search_start = bytes.size() - 22 - 0xFFFF;
    }
    std::size_t eocd = 0;
    bool found_eocd = false;
    std::size_t position = bytes.size() - 22;
    while (true) {
        std::uint32_t signature = 0;
        if (readU32(bytes, position, signature) && signature == kEndOfCentralDirectorySignature) {
            std::uint16_t comment_length = 0;
            if (readU16(bytes, position + 20, comment_length) &&
                position + 22 + comment_length <= bytes.size()) {
                eocd = position;
                found_eocd = true;
                break;
            }
        }
        if (position == search_start) {
            break;
        }
        --position;
    }
    if (!found_eocd) {
        result.diagnostics.push_back(failure("zip.eocd_missing",
                                             "No se encontró el fin del directorio central del ZIP."));
        return result;
    }

    std::uint16_t disk_number = 0;
    std::uint16_t central_disk = 0;
    std::uint16_t entries_this_disk = 0;
    std::uint16_t entries_total16 = 0;
    std::uint32_t central_size32 = 0;
    std::uint32_t central_offset32 = 0;
    if (!readU16(bytes, eocd + 4, disk_number) ||
        !readU16(bytes, eocd + 6, central_disk) ||
        !readU16(bytes, eocd + 8, entries_this_disk) ||
        !readU16(bytes, eocd + 10, entries_total16) ||
        !readU32(bytes, eocd + 12, central_size32) ||
        !readU32(bytes, eocd + 16, central_offset32)) {
        result.diagnostics.push_back(failure("zip.eocd_truncated",
                                             "El fin del directorio central está truncado."));
        return result;
    }
    if (disk_number != 0 || central_disk != 0 || entries_this_disk != entries_total16) {
        result.diagnostics.push_back(failure("zip.multidisk",
                                             "ZIP multi-volumen no soportado."));
        return result;
    }
    if (entries_total16 == 0xFFFFu || central_size32 == 0xFFFFFFFFu ||
        central_offset32 == 0xFFFFFFFFu) {
        result.diagnostics.push_back(failure("zip.zip64_unsupported",
                                             "ZIP64 no soportado; no se degrada silenciosamente."));
        return result;
    }
    if (entries_total16 > limits.max_entries) {
        result.diagnostics.push_back(failure(
            "zip.entry_limit",
            "El ZIP contiene " + std::to_string(entries_total16) +
                " entradas; el límite es " + std::to_string(limits.max_entries) + "."));
        return result;
    }

    const std::uint64_t central_offset = central_offset32;
    const std::uint64_t central_size = central_size32;
    if (central_offset > bytes.size() || central_size > bytes.size() ||
        central_offset + central_size > bytes.size()) {
        result.diagnostics.push_back(failure("zip.central_bounds",
                                             "El directorio central apunta fuera del archivo."));
        return result;
    }
    const std::size_t central_end = static_cast<std::size_t>(central_offset + central_size);
    std::size_t cursor = static_cast<std::size_t>(central_offset);
    std::uint64_t declared_total = 0;

    result.entries.reserve(entries_total16);
    for (std::uint16_t index = 0; index < entries_total16; ++index) {
        std::uint32_t signature = 0;
        if (!readU32(bytes, cursor, signature) || signature != kCentralDirectorySignature) {
            result.diagnostics.push_back(failure(
                "zip.central_entry",
                "Entrada " + std::to_string(index + 1) + " del directorio central inválida."));
            return result;
        }
        if (central_end - cursor < 46) {
            result.diagnostics.push_back(failure("zip.central_truncated",
                                                 "Directorio central truncado."));
            return result;
        }

        std::uint16_t flags = 0;
        std::uint16_t method = 0;
        std::uint16_t mod_time = 0;
        std::uint16_t mod_date = 0;
        std::uint32_t crc = 0;
        std::uint32_t compressed32 = 0;
        std::uint32_t uncompressed32 = 0;
        std::uint16_t name_length = 0;
        std::uint16_t extra_length = 0;
        std::uint16_t comment_length = 0;
        std::uint16_t disk_start = 0;
        std::uint32_t local_offset32 = 0;
        readU16(bytes, cursor + 8, flags);
        readU16(bytes, cursor + 10, method);
        readU16(bytes, cursor + 12, mod_time);
        readU16(bytes, cursor + 14, mod_date);
        readU32(bytes, cursor + 16, crc);
        readU32(bytes, cursor + 20, compressed32);
        readU32(bytes, cursor + 24, uncompressed32);
        readU16(bytes, cursor + 28, name_length);
        readU16(bytes, cursor + 30, extra_length);
        readU16(bytes, cursor + 32, comment_length);
        readU16(bytes, cursor + 34, disk_start);
        readU32(bytes, cursor + 42, local_offset32);
        (void)mod_time;
        (void)mod_date;

        const std::size_t record_end = cursor + 46 +
            static_cast<std::size_t>(name_length) +
            static_cast<std::size_t>(extra_length) +
            static_cast<std::size_t>(comment_length);
        if (record_end > central_end) {
            result.diagnostics.push_back(failure("zip.central_bounds",
                                                 "Entrada del directorio central fuera de límites."));
            return result;
        }
        if (disk_start != 0) {
            result.diagnostics.push_back(failure("zip.multidisk",
                                                 "ZIP multi-volumen no soportado."));
            return result;
        }
        if (compressed32 == 0xFFFFFFFFu || uncompressed32 == 0xFFFFFFFFu ||
            local_offset32 == 0xFFFFFFFFu) {
            result.diagnostics.push_back(failure("zip.zip64_unsupported",
                                                 "ZIP64 no soportado en una entrada."));
            return result;
        }

        Entry entry;
        entry.index = index;
        entry.flags = flags;
        entry.method = method;
        entry.crc32 = crc;
        entry.compressed_size = compressed32;
        entry.uncompressed_size = uncompressed32;
        entry.local_header_offset = local_offset32;
        entry.encrypted = (flags & kFlagEncrypted) != 0;
        entry.supported_method = (method == 0 || method == 8);
        entry.name = decodeZipName(bytes.data() + cursor + 46, name_length, (flags & kFlagUtf8) != 0);
        entry.normalized_name = normalizeMemberName(entry.name);
        entry.normalized_key = lowerAscii(entry.normalized_name);
        entry.is_directory = !entry.name.empty() &&
            (entry.name.back() == '/' || entry.name.back() == '\\');
        entry.path_valid = !isInvalidPath(entry.normalized_name, entry.name);
        entry.exceeds_member_limit = entry.uncompressed_size > limits.max_member_bytes;
        if (entry.uncompressed_size > 0) {
            if (entry.compressed_size == 0) {
                entry.compression_ratio_invalid = true;
            } else {
                const long double ratio =
                    static_cast<long double>(entry.uncompressed_size) /
                    static_cast<long double>(entry.compressed_size);
                entry.compression_ratio_invalid =
                    ratio > static_cast<long double>(limits.max_compression_ratio);
            }
        }
        if (!entry.is_directory && !isIgnoredMember(entry.normalized_name)) {
            if (declared_total > std::numeric_limits<std::uint64_t>::max() -
                    entry.uncompressed_size) {
                result.diagnostics.push_back(failure("zip.total_overflow",
                                                     "El tamaño declarado del ZIP desborda el límite."));
                return result;
            }
            declared_total += entry.uncompressed_size;
        }
        result.entries.push_back(std::move(entry));
        cursor = record_end;
    }

    if (declared_total > limits.max_total_bytes) {
        result.total_limit_exceeded = true;
        result.diagnostics.push_back(failure(
            "zip.total_limit",
            "El ZIP declara " + std::to_string(declared_total) +
                " bytes sin comprimir; el límite total es " +
                std::to_string(limits.max_total_bytes) + "."));
    }
                                                                            
                                                                
    result.ok = !result.total_limit_exceeded;
    return result;
}

bool hasIndexLimitError(const std::vector<Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error &&
            (diagnostic.code == "zip.total_limit" || diagnostic.code == "zip.entry_limit")) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t> findEntryIndex(const std::vector<Entry>& entries,
                                          const std::string& name) {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return entry.index < entries.size() ? std::optional<std::size_t>(entry.index) : std::nullopt;
        }
    }
    return std::nullopt;
}

std::vector<std::size_t> filteredEntries(const std::vector<Entry>& entries) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry.is_directory || isIgnoredMember(entry.normalized_name)) {
            continue;
        }
        result.push_back(i);
    }
    return result;
}

Diagnostic pairFailure(const std::string& cfg_name, const std::string& message) {
    auto diagnostic = failure("zip.pair", message);
    diagnostic.context = cfg_name;
    return diagnostic;
}

}              

std::string normalizeMemberName(const std::string& name) {
    std::string replaced;
    replaced.reserve(name.size());
    for (const char c : name) {
        replaced.push_back(c == '\\' ? '/' : c);
    }
    const bool absolute = !replaced.empty() && replaced.front() == '/';
    std::vector<std::string> components;
    std::size_t start = 0;
    while (start <= replaced.size()) {
        const std::size_t slash = replaced.find('/', start);
        const std::string component = slash == std::string::npos
            ? replaced.substr(start)
            : replaced.substr(start, slash - start);
        if (!component.empty() && component != ".") {
            if (component == "..") {
                if (!components.empty() && components.back() != "..") {
                    components.pop_back();
                } else if (!absolute) {
                    components.push_back(component);
                }
            } else {
                components.push_back(component);
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    std::string normalized;
    if (absolute) {
        normalized.push_back('/');
    }
    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i != 0) {
            normalized.push_back('/');
        }
        normalized += components[i];
    }
    if (normalized == "." || normalized == "/") {
        return normalized == "/" ? "/" : std::string();
    }
    return normalized;
}

bool isIgnoredMember(const std::string& name) {
    const std::string normalized = normalizeMemberName(name);
    if (normalized.empty()) {
        return false;
    }
    const std::string lower = lowerAscii(normalized);
    std::size_t start = 0;
    while (start <= lower.size()) {
        const std::size_t slash = lower.find('/', start);
        const std::string component = slash == std::string::npos
            ? lower.substr(start)
            : lower.substr(start, slash - start);
        if (component == "__macosx") {
            return true;
        }
        if (slash == std::string::npos) {
            break;
        }
        start = slash + 1;
    }
    const std::string base = lowerAscii(baseName(normalized));
    return base == ".ds_store" || base == "thumbs.db" || base == "desktop.ini" ||
        (base.size() >= 2 && base.rfind("._", 0) == 0);
}

Archive::Archive(std::vector<std::uint8_t> bytes, Limits limits)
    : bytes_(std::move(bytes)), limits_(limits) {}

OpenResult Archive::openFromMemory(std::vector<std::uint8_t> bytes, const Limits& limits) {
    OpenResult result;
    if (limits.max_entries == 0 || limits.max_member_bytes == 0 ||
        limits.max_total_bytes == 0 || limits.max_compression_ratio == 0) {
        result.diagnostics.push_back(failure("zip.limits",
                                             "Los límites ZIP deben ser positivos."));
        return result;
    }
    auto parsed = parseCentralDirectory(bytes, limits);
    if (!parsed.ok) {
        result.diagnostics = std::move(parsed.diagnostics);
        result.ok = false;
        result.archive.reset();
        return result;
    }
    auto archive = std::unique_ptr<Archive>(new Archive(std::move(bytes), limits));
    archive->entries_ = std::move(parsed.entries);
    archive->index_diagnostics_ = std::move(parsed.diagnostics);
    result.archive = std::move(archive);
    result.diagnostics = result.archive->index_diagnostics_;
    result.ok = true;
    return result;
}

OpenResult Archive::openFromFile(const std::filesystem::path& path, const Limits& limits) {
    OpenResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.diagnostics.push_back(failure("zip.open_file",
                                             "No se pudo abrir el archivo ZIP indicado."));
        return result;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0) {
        result.diagnostics.push_back(failure("zip.file_size",
                                             "No se pudo determinar el tamaño del archivo ZIP."));
        return result;
    }
    const std::uint64_t maximum_input =
        limits.max_total_bytes > std::numeric_limits<std::uint64_t>::max() - (64ull * 1024ull * 1024ull)
        ? std::numeric_limits<std::uint64_t>::max()
        : limits.max_total_bytes + 64ull * 1024ull * 1024ull;
    if (static_cast<std::uint64_t>(size) > maximum_input) {
        result.diagnostics.push_back(failure("zip.file_size_limit",
                                             "El archivo ZIP excede el presupuesto de memoria del índice."));
        return result;
    }
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            result.diagnostics.push_back(failure("zip.file_read",
                                                 "No se pudo leer el archivo ZIP completo."));
            return result;
        }
    }
    return openFromMemory(std::move(bytes), limits);
}

ReadResult Archive::readEntry(const Entry& entry) const {
    ReadResult result;
    if (entry.index >= entries_.size()) {
        result.diagnostics.push_back(failure("zip.entry_range",
                                             "La entrada ZIP no pertenece a este archivo."));
        return result;
    }
    if (entry.path_valid == false) {
        result.diagnostics.push_back(failure("zip.path_invalid",
                                             "La ruta ZIP es absoluta o intenta escapar del directorio."));
        return result;
    }
    if (entry.encrypted) {
        result.diagnostics.push_back(failure("zip.encrypted",
                                             "El miembro ZIP está cifrado; no se admite lectura automática."));
        return result;
    }
    if (!entry.supported_method) {
        result.diagnostics.push_back(failure("zip.method_unsupported",
                                             "Método de compresión ZIP no soportado; solo se admite stored y Deflate."));
        return result;
    }
    if (entry.exceeds_member_limit) {
        result.diagnostics.push_back(failure(
            "zip.member_limit",
            "El miembro declara " + std::to_string(entry.uncompressed_size) +
                " bytes sin comprimir; el límite por archivo es " +
                std::to_string(limits_.max_member_bytes) + "."));
        return result;
    }
    if (entry.compression_ratio_invalid) {
        result.diagnostics.push_back(failure(
            "zip.compression_ratio",
            "El miembro tiene una relación de compresión superior a " +
                std::to_string(limits_.max_compression_ratio) + ":1; se rechaza."));
        return result;
    }
    if (entry.local_header_offset > bytes_.size() ||
        bytes_.size() - static_cast<std::size_t>(entry.local_header_offset) < 30) {
        result.diagnostics.push_back(failure("zip.local_header_bounds",
                                             "La cabecera local ZIP apunta fuera del archivo."));
        return result;
    }
    const std::size_t local_offset = static_cast<std::size_t>(entry.local_header_offset);
    std::uint32_t signature = 0;
    std::uint16_t local_flags = 0;
    std::uint16_t local_method = 0;
    std::uint16_t name_length = 0;
    std::uint16_t extra_length = 0;
    readU32(bytes_, local_offset, signature);
    readU16(bytes_, local_offset + 6, local_flags);
    readU16(bytes_, local_offset + 8, local_method);
    readU16(bytes_, local_offset + 26, name_length);
    readU16(bytes_, local_offset + 28, extra_length);
    if (signature != kLocalHeaderSignature) {
        result.diagnostics.push_back(failure("zip.local_header_signature",
                                             "Cabecera local ZIP inválida."));
        return result;
    }
    if (local_method != entry.method) {
        result.diagnostics.push_back(failure("zip.method_mismatch",
                                             "El método local y central del ZIP no coincide."));
        return result;
    }
    if ((local_flags & kFlagEncrypted) != 0) {
        result.diagnostics.push_back(failure("zip.encrypted",
                                             "El miembro ZIP está cifrado; no se admite lectura automática."));
        return result;
    }
    (void)kFlagDataDescriptor;

    const std::uint64_t data_offset64 = static_cast<std::uint64_t>(local_offset) + 30ull +
        static_cast<std::uint64_t>(name_length) + static_cast<std::uint64_t>(extra_length);
    if (data_offset64 > bytes_.size() ||
        entry.compressed_size > bytes_.size() - static_cast<std::size_t>(data_offset64)) {
        result.diagnostics.push_back(failure("zip.data_bounds",
                                             "Los datos del miembro ZIP apuntan fuera del archivo."));
        return result;
    }
    if (entry.compressed_size > std::numeric_limits<std::size_t>::max()) {
        result.diagnostics.push_back(failure("zip.data_range",
                                             "El tamaño comprimido excede el rango del proceso."));
        return result;
    }
    const std::size_t data_offset = static_cast<std::size_t>(data_offset64);
    const std::size_t compressed_size = static_cast<std::size_t>(entry.compressed_size);

    std::vector<std::uint8_t> output;
    if (entry.method == 0) {
        if (entry.compressed_size != entry.uncompressed_size) {
            result.diagnostics.push_back(failure(
                "zip.stored_size",
                "El miembro stored declara tamaños comprimido y sin comprimir distintos."));
            return result;
        }
        if (entry.uncompressed_size > std::numeric_limits<std::size_t>::max()) {
            result.diagnostics.push_back(failure("zip.data_range",
                                                 "El tamaño sin comprimir excede el rango del proceso."));
            return result;
        }
        output.assign(bytes_.begin() + static_cast<std::ptrdiff_t>(data_offset),
                      bytes_.begin() + static_cast<std::ptrdiff_t>(data_offset + compressed_size));
    } else {
        if (entry.uncompressed_size == 0 && entry.compressed_size == 0) {
            output.clear();
        } else {
            const std::uint64_t cap64 = std::min<std::uint64_t>(
                limits_.max_member_bytes,
                static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()));
            std::string inflate_error;
            if (!inflateDeflate(bytes_.data() + data_offset,
                                compressed_size,
                                output,
                                static_cast<std::size_t>(cap64),
                                inflate_error)) {
                auto diagnostic = failure("zip.deflate_invalid",
                                          "Deflate inválido: " + inflate_error + ".");
                result.diagnostics.push_back(std::move(diagnostic));
                return result;
            }
        }
    }

    if (output.size() > limits_.max_member_bytes) {
        result.diagnostics.push_back(failure(
            "zip.member_limit",
            "El miembro produjo más de " + std::to_string(limits_.max_member_bytes) +
                " bytes; se rechaza para evitar agotamiento de memoria."));
        return result;
    }
    if (output.size() != entry.uncompressed_size) {
        result.diagnostics.push_back(failure(
            "zip.size_mismatch",
            "El miembro produjo " + std::to_string(output.size()) +
                " bytes y el ZIP declara " + std::to_string(entry.uncompressed_size) + "."));
        return result;
    }
    const std::uint32_t crc = computeCrc32(output.data(), output.size());
    if (crc != entry.crc32) {
        result.diagnostics.push_back(failure(
            "zip.crc_mismatch",
            "CRC32 incorrecto en el miembro ZIP; los datos están corruptos."));
        return result;
    }
    result.data = std::move(output);
    result.ok = true;
    return result;
}

ReadResult Archive::readEntryByName(const std::string& name) const {
    const auto index = findEntryIndex(entries_, name);
    if (!index) {
        ReadResult result;
        result.diagnostics.push_back(failure("zip.member_not_found",
                                             "El miembro indicado no existe en el ZIP."));
        return result;
    }
    return readEntry(entries_[*index]);
}

PairingResult Archive::pairRecords() const {
    PairingResult result;
    if (hasIndexLimitError(index_diagnostics_)) {
        for (const auto& diagnostic : index_diagnostics_) {
            if (diagnostic.severity == DiagnosticSeverity::Error) {
                result.failures.push_back(diagnostic);
            }
        }
        return result;
    }

    const auto candidates = filteredEntries(entries_);
    std::uint64_t declared_total = 0;
    for (const std::size_t index : candidates) {
        if (declared_total > std::numeric_limits<std::uint64_t>::max() -
                entries_[index].uncompressed_size) {
            result.failures.push_back(failure("zip.total_overflow",
                                              "El tamaño declarado del ZIP desborda el límite."));
            return result;
        }
        declared_total += entries_[index].uncompressed_size;
    }
    if (declared_total > limits_.max_total_bytes) {
        result.failures.push_back(failure(
            "zip.total_limit",
            "El ZIP declara " + std::to_string(declared_total) +
                " bytes sin comprimir; el límite total es " +
                std::to_string(limits_.max_total_bytes) + "."));
        return result;
    }

    std::map<std::string, std::vector<std::size_t>> by_name;
    for (const std::size_t index : candidates) {
        const Entry& entry = entries_[index];
        if (entry.normalized_key.empty()) {
            continue;
        }
        by_name[entry.normalized_key].push_back(index);
    }

    std::vector<std::size_t> cfgs;
    for (const std::size_t index : candidates) {
        if (extensionLower(entries_[index].normalized_name) == ".cfg") {
            cfgs.push_back(index);
        }
    }
    std::sort(cfgs.begin(), cfgs.end(), [&](std::size_t left, std::size_t right) {
        const auto& a = entries_[left];
        const auto& b = entries_[right];
        if (a.normalized_key != b.normalized_key) {
            return a.normalized_key < b.normalized_key;
        }
        return a.index < b.index;
    });
    if (cfgs.empty()) {
        result.failures.push_back(failure("zip.no_cfg",
                                          "El ZIP no contiene archivos CFG."));
        return result;
    }

    std::uint64_t pair_total = 0;
    for (const std::size_t cfg_index : cfgs) {
        const Entry& cfg = entries_[cfg_index];
        const std::string cfg_name = cfg.name;
        if (!cfg.path_valid) {
            result.failures.push_back(pairFailure(cfg_name,
                "Ruta CFG inválida; se rechaza por seguridad."));
            continue;
        }
        if (by_name[cfg.normalized_key].size() > 1) {
            result.failures.push_back(pairFailure(
                cfg_name,
                "Ruta CFG duplicada en el ZIP; no se puede asociar sin ambigüedad."));
            continue;
        }
        if (cfg.encrypted) {
            result.failures.push_back(pairFailure(cfg_name,
                "CFG cifrado; no se admite lectura automática."));
            continue;
        }
        const std::string dat_normalized = replaceExtension(cfg.normalized_name, ".dat");
        const std::string dat_key = lowerAscii(dat_normalized);
        const auto found = by_name.find(dat_key);
        if (found == by_name.end() || found->second.empty()) {
            result.failures.push_back(pairFailure(
                cfg_name, "No existe el DAT hermano " + dat_normalized + "."));
            continue;
        }
        if (found->second.size() > 1) {
            result.failures.push_back(pairFailure(
                cfg_name, "Hay varios DAT para " + dat_normalized + "; asociación ambigua."));
            continue;
        }
        const std::size_t dat_index = found->second.front();
        const Entry& dat = entries_[dat_index];
        if (!dat.path_valid) {
            result.failures.push_back(pairFailure(cfg_name,
                "Ruta DAT inválida; se rechaza por seguridad."));
            continue;
        }
        if (dat.encrypted) {
            result.failures.push_back(pairFailure(cfg_name,
                "DAT cifrado; no se admite lectura automática."));
            continue;
        }
        if (cfg.exceeds_member_limit || dat.exceeds_member_limit) {
            result.failures.push_back(pairFailure(
                cfg_name,
                "CFG/DAT supera el límite por archivo (" +
                    std::to_string(limits_.max_member_bytes) + " bytes)."));
            continue;
        }
        if (cfg.compression_ratio_invalid || dat.compression_ratio_invalid) {
            result.failures.push_back(pairFailure(
                cfg_name,
                "CFG/DAT tiene una relación de compresión superior a " +
                    std::to_string(limits_.max_compression_ratio) + ":1; se rechaza."));
            continue;
        }
        const std::uint64_t pair_bytes = cfg.uncompressed_size + dat.uncompressed_size;
        if (pair_total > limits_.max_total_bytes ||
            pair_bytes > limits_.max_total_bytes - pair_total) {
            result.failures.push_back(pairFailure(
                cfg_name,
                "El conjunto CFG/DAT supera el límite total de " +
                    std::to_string(limits_.max_total_bytes) + " bytes."));
            continue;
        }
        pair_total += pair_bytes;

        Pair pair;
        pair.cfg_entry = cfg_index;
        pair.dat_entry = dat_index;
        pair.cfg_name = cfg.name;
        pair.dat_name = dat.name;
        pair.record_name = stripExtension(cfg.normalized_name);
        result.pairs.push_back(std::move(pair));
    }
    return result;
}

PairingResult Archive::recordBundles() const {
    PairingResult result = pairRecords();
    if (result.pairs.empty()) {
        return result;
    }
    const auto candidates = filteredEntries(entries_);
    const bool single_pair = result.pairs.size() == 1;
    for (auto& pair : result.pairs) {
        const std::string parent = lowerAscii(parentFolder(entries_[pair.cfg_entry].normalized_name));
        std::map<std::string, std::vector<std::size_t>> aux_groups;
        for (const std::size_t index : candidates) {
            const Entry& entry = entries_[index];
            if (!entry.path_valid) {
                continue;
            }
            const std::string extension = extensionLower(entry.normalized_name);
            if (extension != ".hdr" && extension != ".inf" && extension != ".csv") {
                continue;
            }
            const std::string entry_parent = lowerAscii(parentFolder(entry.normalized_name));
            if (!single_pair && entry_parent != parent) {
                continue;
            }
            aux_groups[entry.normalized_key].push_back(index);
        }
        std::vector<std::pair<std::string, std::string>> aux;
        for (auto& group : aux_groups) {
            if (group.second.size() > 1) {
                const std::string display = entries_[group.second.front()].normalized_name;
                auto diagnostic = pairFailure(
                    pair.cfg_name,
                    "Ruta auxiliar duplicada en el ZIP (" + display + "); hay " +
                        std::to_string(group.second.size()) +
                        " entradas con la misma ruta y no se adjunta ninguna para evitar "
                        "leer dos veces el mismo miembro.");
                diagnostic.context = pair.cfg_name;
                result.failures.push_back(std::move(diagnostic));
                continue;
            }
            const Entry& entry = entries_[group.second.front()];
            aux.emplace_back(entry.normalized_key, entry.name);
        }
        std::sort(aux.begin(), aux.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        pair.aux_names.clear();
        for (const auto& item : aux) {
            pair.aux_names.push_back(item.second);
        }
    }
    return result;
}

}                  
}                         
