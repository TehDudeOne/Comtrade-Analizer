                                                                             
                                                                        
#include "analizador/analysis.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numeric>

namespace analizador::analysis {

namespace {

using analizador::Diagnostic;
using analizador::DiagnosticSeverity;

constexpr double kTwoPi = 2.0 * kPi;

                                                                                
                                                                               
                                                                          
constexpr double kSequencePositiveRelativeFloor = 1e-9;

                                                                             
                                                                           
                                                                                
                                                                        
constexpr double kFlatTopAmplitudeFraction = 0.98;
constexpr double kFlatTopSlopeFraction = 0.10;
constexpr double kFlatTopMinDurationFraction = 0.125;                      
constexpr int kFlatTopMinSamplesPerPeriod = 8;
constexpr int kFlatTopMinCycles = 1;
constexpr std::size_t kFlatTopMinConsecutiveSamples = 3;

bool isFinite(double value) { return std::isfinite(value); }

bool isSpaceChar(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

bool isDigitChar(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

bool isAlphaChar(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool isAsciiAlnum(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z');
}

char lowerAscii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

char upperAscii(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

std::string trimCopy(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isSpaceChar(text[begin])) {
        ++begin;
    }
    while (end > begin && isSpaceChar(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string lowerCopy(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = lowerAscii(c);
    }
    return out;
}

Diagnostic makeDiagnostic(DiagnosticSeverity severity,
                          std::string code,
                          std::string message) {
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    return diagnostic;
}

bool containsText(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string formatValue(double value, const char* format) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, format, value);
    return std::string(buffer);
}

                                                                     
double medianOf(std::vector<double> values) {
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const std::size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    const double high = values[mid];
    if (values.size() % 2 == 1) {
        return high;
    }
    std::nth_element(values.begin(),
                     values.begin() + static_cast<std::ptrdiff_t>(mid - 1),
                     values.begin() + static_cast<std::ptrdiff_t>(mid));
    return 0.5 * (values[mid - 1] + high);
}

                                                                           
double percentile95(std::vector<double> values) {
    values.erase(std::remove_if(values.begin(), values.end(),
                                [](double value) { return !isFinite(value); }),
                 values.end());
    if (values.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::sort(values.begin(), values.end());
    const double position = 0.95 * static_cast<double>(values.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = static_cast<std::size_t>(std::ceil(position));
    if (low == high) {
        return values[low];
    }
    const double fraction = position - static_cast<double>(low);
    return values[low] + (values[high] - values[low]) * fraction;
}

                                                                           
                                                                     
bool gradientOfIndex(const double* times, std::size_t count, std::vector<double>& out) {
    if (count < 2) {
        return false;
    }
    out.assign(count, 0.0);
    out[0] = times[1] - times[0];
    for (std::size_t i = 1; i + 1 < count; ++i) {
        out[i] = (times[i + 1] - times[i - 1]) / 2.0;
    }
    out[count - 1] = times[count - 1] - times[count - 2];
    for (double value : out) {
        if (!isFinite(value) || value <= 0.0) {
            return false;
        }
    }
    return true;
}

                                                                               
                                                                     
bool leastSquares(const std::vector<double>& a_in,
                  std::size_t rows,
                  std::size_t columns,
                  std::vector<double> b,
                  std::vector<double>& solution,
                  std::size_t& rank) {
    rank = 0;
    solution.clear();
    if (rows == 0 || columns == 0 || columns > rows) {
        return false;
    }
    std::vector<double> a = a_in;
    for (std::size_t k = 0; k < columns; ++k) {
        double norm = 0.0;
        for (std::size_t i = k; i < rows; ++i) {
            norm += a[i * columns + k] * a[i * columns + k];
        }
        norm = std::sqrt(norm);
        if (!(norm > 0.0) || !isFinite(norm)) {
            continue;
        }
        const double diagonal = a[k * columns + k];
        const double alpha = (diagonal >= 0.0) ? -norm : norm;
        std::vector<double> reflector(rows - k, 0.0);
        reflector[0] = diagonal - alpha;
        for (std::size_t i = k + 1; i < rows; ++i) {
            reflector[i - k] = a[i * columns + k];
        }
        double reflector_norm = 0.0;
        for (double value : reflector) {
            reflector_norm += value * value;
        }
        if (!(reflector_norm > 0.0)) {
            continue;
        }
        const double beta = 2.0 / reflector_norm;
        for (std::size_t j = k; j < columns; ++j) {
            double dot = 0.0;
            for (std::size_t i = k; i < rows; ++i) {
                dot += reflector[i - k] * a[i * columns + j];
            }
            const double factor = beta * dot;
            for (std::size_t i = k; i < rows; ++i) {
                a[i * columns + j] -= factor * reflector[i - k];
            }
        }
        double dot_b = 0.0;
        for (std::size_t i = k; i < rows; ++i) {
            dot_b += reflector[i - k] * b[i];
        }
        const double factor_b = beta * dot_b;
        for (std::size_t i = k; i < rows; ++i) {
            b[i] -= factor_b * reflector[i - k];
        }
        a[k * columns + k] = alpha;
        for (std::size_t i = k + 1; i < rows; ++i) {
            a[i * columns + k] = 0.0;
        }
    }

    double max_diagonal = 0.0;
    for (std::size_t i = 0; i < columns; ++i) {
        max_diagonal = std::max(max_diagonal, std::fabs(a[i * columns + i]));
    }
    if (!(max_diagonal > 0.0) || !isFinite(max_diagonal)) {
        return false;
    }
    const double tolerance =
        max_diagonal * static_cast<double>(std::max(rows, columns)) * 1e-12;

    std::size_t detected = 0;
    while (detected < columns &&
           std::fabs(a[detected * columns + detected]) > tolerance) {
        ++detected;
    }
    rank = detected;
    if (detected < columns) {
        return false;
    }

    solution.assign(columns, 0.0);
    for (std::size_t ii = columns; ii-- > 0;) {
        double sum = b[ii];
        for (std::size_t j = ii + 1; j < columns; ++j) {
            sum -= a[ii * columns + j] * solution[j];
        }
        const double diagonal = a[ii * columns + ii];
        if (!(std::fabs(diagonal) > 0.0) || !isFinite(diagonal)) {
            return false;
        }
        solution[ii] = sum / diagonal;
        if (!isFinite(solution[ii])) {
            return false;
        }
    }
    return true;
}

std::size_t lowerBoundIndex(const std::vector<double>& values, double target) {
    return static_cast<std::size_t>(
        std::lower_bound(values.begin(), values.end(), target) - values.begin());
}

std::size_t upperBoundIndex(const std::vector<double>& values, double target) {
    return static_cast<std::size_t>(
        std::upper_bound(values.begin(), values.end(), target) - values.begin());
}

int phaseSlot(char phase) {
    switch (upperAscii(phase)) {
        case 'A':
            return 0;
        case 'B':
            return 1;
        case 'C':
            return 2;
        default:
            return -1;
    }
}

bool isPhaseSeparator(char c) {
    return isSpaceChar(c) || c == '_' || c == '.' || c == '-';
}

                                                                          
std::vector<int> matchedPhases(const std::string& name) {
    std::vector<int> matches;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (upperAscii(name[i]) != 'I') {
            continue;
        }
        if (i > 0 && isAsciiAlnum(name[i - 1])) {
            continue;
        }
        std::size_t j = i + 1;
        while (j < name.size() && isPhaseSeparator(name[j])) {
            ++j;
        }
        if (j >= name.size()) {
            continue;
        }
        const int slot = phaseSlot(name[j]);
        if (slot < 0) {
            continue;
        }
        const std::size_t after = j + 1;
        if (after < name.size()) {
            const char next = name[after];
            if (!(isSpaceChar(next) || next == '(' || next == '_' || next == '.' ||
                  next == '-')) {
                continue;
            }
        }
        if (std::find(matches.begin(), matches.end(), slot) == matches.end()) {
            matches.push_back(slot);
        }
    }
    return matches;
}

bool isDistanceUnitToken(const std::string& token) {
    static const char* const kUnits[] = {
        "km", "kilometer", "kilometers", "kilometre", "kilometres",
        "m",  "meter",     "meters",     "metre",      "metres",
        "metro", "metros",
    };
    for (const char* unit : kUnits) {
        if (token == unit) {
            return true;
        }
    }
    return false;
}

                                                                               
                                                                        
                                                                             
                                                                              
                                                              
std::vector<int> matchedVoltagePhases(const std::string& name) {
    std::vector<int> matches;
    const std::size_t size = name.size();
    for (std::size_t i = 0; i < size; ++i) {
        const char lead = upperAscii(name[i]);
        if (lead != 'V' && lead != 'U') {
            continue;
        }
        if (i > 0 && isAsciiAlnum(name[i - 1])) {
            continue;
        }
        std::size_t cursor = i + 1;
        while (cursor < size && isPhaseSeparator(name[cursor])) {
            ++cursor;
        }
        if (cursor >= size || phaseSlot(name[cursor]) < 0) {
            continue;
        }

        std::vector<int> local;
        bool neutral = false;
        bool rejected = false;
        bool separated = false;
        std::size_t k = cursor;
        while (k < size) {
            const char c = name[k];
            if (isPhaseSeparator(c)) {
                separated = true;
                ++k;
                continue;
            }
            const int slot = phaseSlot(c);
            if (slot >= 0) {
                if (std::find(local.begin(), local.end(), slot) == local.end()) {
                    local.push_back(slot);
                }
                separated = false;
                ++k;
                continue;
            }
            if (upperAscii(c) == 'N') {
                                                                            
                if (lead != 'V') {
                    rejected = true;
                    break;
                }
                neutral = true;
                ++k;
                break;
            }
            if ((upperAscii(c) == 'V' || upperAscii(c) == 'U') && separated) {
                                                             
                break;
            }
            if (isAsciiAlnum(c)) {
                rejected = true;
            }
            break;
        }
        if (rejected || local.empty()) {
            continue;
        }
        if (neutral && k < size && isAsciiAlnum(name[k])) {
            const char tail = upperAscii(name[k]);
            if (tail != 'V' && tail != 'U') {
                rejected = true;
            }
        }
        if (rejected) {
            continue;
        }
        for (int slot : local) {
            if (std::find(matches.begin(), matches.end(), slot) == matches.end()) {
                matches.push_back(slot);
            }
        }
    }
    return matches;
}

                                                                              
                                                  
                                                                              

std::size_t nextPowerOfTwo(std::size_t value) {
    std::size_t power = 1;
    while (power < value) {
        if (power > (std::numeric_limits<std::size_t>::max() / 2)) {
            return value;
        }
        power <<= 1;
    }
    return power;
}

std::size_t previousPowerOfTwo(std::size_t value) {
    std::size_t power = 1;
    while (power <= value / 2) {
        power <<= 1;
    }
    return power;
}

void fftInPlace(std::vector<std::complex<double>>& data) {
    const std::size_t n = data.size();
    if (n < 2) {
        return;
    }
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const double angle = -kTwoPi / static_cast<double>(length);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        const std::size_t half = length >> 1;
        for (std::size_t base = 0; base < n; base += length) {
            std::complex<double> twist(1.0, 0.0);
            for (std::size_t j = 0; j < half; ++j) {
                const std::complex<double> even = data[base + j];
                const std::complex<double> odd = data[base + j + half] * twist;
                data[base + j] = even + odd;
                data[base + j + half] = even - odd;
                twist *= step;
            }
        }
        if (length == n) {
            break;
        }
    }
}

std::vector<double> hannWindow(std::size_t points) {
    std::vector<double> window(points, 0.0);
    if (points == 0) {
        return window;
    }
    if (points == 1) {
        window[0] = 1.0;
        return window;
    }
    for (std::size_t i = 0; i < points; ++i) {
        window[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) /
                                         static_cast<double>(points - 1));
    }
    return window;
}

bool hasUsableTimes(const SampleData& data) {
    const std::size_t count = data.sampleCount();
    if (count == 0 || data.times_s.size() != count) {
        return false;
    }
    double previous = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double time = data.times_s[i];
        if (!isFinite(time)) {
            return false;
        }
        if (i > 0 && !(time > previous)) {
            return false;
        }
        previous = time;
    }
    return true;
}

bool windowSourceStats(const SampleData& data,
                       double low,
                       double high,
                       std::size_t& samples,
                       double& median_dt_s) {
    samples = 0;
    median_dt_s = 0.0;
    if (!hasUsableTimes(data) || !isFinite(low) || !isFinite(high) ||
        !(high > low)) {
        return false;
    }
    std::vector<double> differences;
    double previous = 0.0;
    bool has_previous = false;
    for (double time : data.times_s) {
        if (time < low || time >= high) {
            continue;
        }
        if (has_previous) {
            differences.push_back(time - previous);
        }
        previous = time;
        has_previous = true;
        ++samples;
    }
    if (samples < 2 || differences.empty()) {
        return false;
    }
    median_dt_s = medianOf(differences);
    return isFinite(median_dt_s) && median_dt_s > 0.0;
}

                                                                                
                                                                              
bool interpolateUniform(const SampleData& data,
                        std::size_t channel,
                        double low,
                        double high,
                        std::size_t points,
                        std::vector<double>& out) {
    out.clear();
    if (channel >= data.analog_channels || points < 2 || !hasUsableTimes(data)) {
        return false;
    }
    const double duration = high - low;
    if (!isFinite(duration) || !(duration > 0.0) ||
        low < data.times_s.front() || high > data.times_s.back()) {
        return false;
    }
    out.assign(points, 0.0);
    for (std::size_t i = 0; i < points; ++i) {
        double time = low + duration * (static_cast<double>(i) /
                                        static_cast<double>(points));
        if (time >= high) {
            time = std::nextafter(high, low);
        }
        const auto upper =
            std::lower_bound(data.times_s.begin(), data.times_s.end(), time);
        double value = 0.0;
        if (upper != data.times_s.end() && *upper == time) {
            const std::size_t index =
                static_cast<std::size_t>(upper - data.times_s.begin());
            value = data.analog[index * data.analog_channels + channel];
        } else {
            if (upper == data.times_s.begin() || upper == data.times_s.end()) {
                return false;
            }
            const std::size_t right =
                static_cast<std::size_t>(upper - data.times_s.begin());
            const std::size_t left = right - 1;
            const double t0 = data.times_s[left];
            const double t1 = data.times_s[right];
            const double y0 = data.analog[left * data.analog_channels + channel];
            const double y1 = data.analog[right * data.analog_channels + channel];
            if (!isFinite(t0) || !isFinite(t1) || !(t1 > t0) || !isFinite(y0) ||
                !isFinite(y1)) {
                return false;
            }
            value = y0 + (y1 - y0) * ((time - t0) / (t1 - t0));
        }
        if (!isFinite(value)) {
            return false;
        }
        out[i] = value;
    }
    return true;
}

                                                                          
bool finiteSequenceComponents(const SequenceSet& sequences) {
    return isFinite(sequences.zero.real()) &&
           isFinite(sequences.zero.imag()) &&
           isFinite(sequences.positive.real()) &&
           isFinite(sequences.positive.imag()) &&
           isFinite(sequences.negative.real()) &&
           isFinite(sequences.negative.imag());
}

                                                                               
                                                                              
                                                                     
                                                    
bool relativePositiveMagnitude(const SequenceSet& sequences,
                               double& positive_magnitude) {
    positive_magnitude = 0.0;
    if (!finiteSequenceComponents(sequences)) {
        return false;
    }
    const double zero_magnitude = std::abs(sequences.zero);
    const double positive = std::abs(sequences.positive);
    const double negative_magnitude = std::abs(sequences.negative);
    const double scale =
        std::max({zero_magnitude, positive, negative_magnitude});
    if (!isFinite(scale) || !(scale > 0.0) || !isFinite(positive) ||
        !(positive > 0.0)) {
        return false;
    }
    const double relative = positive / scale;
    if (!isFinite(relative) ||
        !(relative > kSequencePositiveRelativeFloor)) {
        return false;
    }
    positive_magnitude = positive;
    return true;
}

                                                                             
                                                                              
                                                                 
struct FlatTopEvidence {
    bool evaluated = false;
    double pct = 0.0;
    double longest_ms = 0.0;
    bool elevated = false;
};

                                                                         
                                                                         
FlatTopEvidence evaluateFlatTop(const std::vector<double>& values,
                                double duration_s,
                                double frequency_hz,
                                int cycles,
                                std::string& reason) {
    FlatTopEvidence out;
    reason.clear();
    const std::size_t points = values.size();
    if (points < kFlatTopMinConsecutiveSamples) {
        reason = "La ventana no contiene muestras suficientes para evaluar una "
                 "meseta.";
        return out;
    }
    if (!isFinite(duration_s) || !(duration_s > 0.0) ||
        !isFinite(frequency_hz) || !(frequency_hz > 0.0) ||
        cycles < kFlatTopMinCycles) {
        reason = "Duración, frecuencia o ciclos insuficientes para evaluar el "
                 "techo plano.";
        return out;
    }
    const double period_s = 1.0 / frequency_hz;
    const double dt_s = duration_s / static_cast<double>(points);
    if (!isFinite(dt_s) || !(dt_s > 0.0)) {
        reason = "No se pudo validar la rejilla uniforme del techo plano.";
        return out;
    }
    const double samples_per_period = period_s / dt_s;
    if (!isFinite(samples_per_period) ||
        samples_per_period < static_cast<double>(kFlatTopMinSamplesPerPeriod)) {
        reason = "La rejilla tiene menos de " +
                 std::to_string(kFlatTopMinSamplesPerPeriod) +
                 " muestras por período nominal; la meseta no es resoluble.";
        return out;
    }

    double peak = 0.0;
    for (double value : values) {
        if (!isFinite(value)) {
            reason = "La ventana contiene valores no finitos; no se evalúa el "
                     "techo plano.";
            return out;
        }
        peak = std::max(peak, std::fabs(value));
    }
    if (!isFinite(peak) || !(peak > 0.0)) {
        reason = "La ventana no tiene variación AC utilizable (pico ~0); el "
                 "techo plano no se evalúa.";
        return out;
    }

                                                                             
    std::vector<double> derivative(points, 0.0);
    derivative.front() = (values[1] - values[0]) / dt_s;
    for (std::size_t i = 1; i + 1 < points; ++i) {
        derivative[i] = (values[i + 1] - values[i - 1]) / (2.0 * dt_s);
    }
    derivative.back() = (values[points - 1] - values[points - 2]) / dt_s;

    double max_slope = 0.0;
    for (double value : derivative) {
        if (!isFinite(value)) {
            reason = "La derivada de la ventana no es finita; no se evalúa el "
                     "techo plano.";
            return out;
        }
        max_slope = std::max(max_slope, std::fabs(value));
    }
    if (!isFinite(max_slope) || !(max_slope > 0.0)) {
        reason = "La ventana no tiene pendiente utilizable; el techo plano no "
                 "se evalúa.";
        return out;
    }

    const double amplitude_threshold = kFlatTopAmplitudeFraction * peak;
    const double slope_threshold = kFlatTopSlopeFraction * max_slope;
    const double min_plateau_s = kFlatTopMinDurationFraction * period_s;

    std::size_t qualifying_samples = 0;
    double longest_plateau_s = 0.0;
    std::size_t index = 0;
    while (index < points) {
        const bool flat = std::fabs(values[index]) >= amplitude_threshold &&
                          std::fabs(derivative[index]) <= slope_threshold;
        if (!flat) {
            ++index;
            continue;
        }
        std::size_t end = index;
        while (end < points && std::fabs(values[end]) >= amplitude_threshold &&
               std::fabs(derivative[end]) <= slope_threshold) {
            ++end;
        }
        const std::size_t run = end - index;
        const double plateau_s =
            static_cast<double>(run - 1) * dt_s;                             
        if (run >= kFlatTopMinConsecutiveSamples &&
            plateau_s >= min_plateau_s) {
            qualifying_samples += run;
            longest_plateau_s = std::max(longest_plateau_s, plateau_s);
        }
        index = end;
    }

    out.evaluated = true;
    out.pct = 100.0 * static_cast<double>(qualifying_samples) /
              static_cast<double>(points);
    out.longest_ms = longest_plateau_s * 1000.0;
    out.elevated = longest_plateau_s >= min_plateau_s;
    return out;
}

}              

                                                                              
                   
                                                                              

const char* changeDirectionName(ChangeDirection direction) noexcept {
    switch (direction) {
        case ChangeDirection::Rise:
            return "ascenso";
        case ChangeDirection::Fall:
            return "descenso";
        case ChangeDirection::Change:
            return "cambio";
        case ChangeDirection::None:
            break;
    }
    return "sin cambio";
}

const char* waveformOriginName(WaveformOrigin origin) noexcept {
    switch (origin) {
        case WaveformOrigin::Onset:
            return "onset";
        case WaveformOrigin::Trigger:
            return "trigger";
        case WaveformOrigin::Midpoint:
            return "midpoint";
    }
    return "midpoint";
}

const char* faultDistanceStatusName(FaultDistanceStatus status) noexcept {
    switch (status) {
        case FaultDistanceStatus::NotInterpretable:
            return "no_interpretable";
        case FaultDistanceStatus::NoReferenceLength:
            return "sin_longitud_referencia";
        case FaultDistanceStatus::WithinReference:
            return "dentro_referencia";
        case FaultDistanceStatus::ExceedsReference:
            return "excede_referencia";
    }
    return "no_interpretable";
}

                                                                              
         
                                                                              

double Phasor::magnitude() const noexcept { return std::abs(value); }

double Phasor::angleDegrees() const noexcept {
    return std::arg(value) * 180.0 / kPi;
}

bool Phasor::isFinite() const noexcept {
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

std::vector<Phasor> makePhasors(const std::vector<std::complex<double>>& values) {
    std::vector<Phasor> out;
    out.reserve(values.size());
    for (const auto& value : values) {
        Phasor phasor;
        phasor.value = value;
        out.push_back(phasor);
    }
    return out;
}

                                                                              
                       
                                                                              

TimeAlignment alignTimestamps(const ComtradeRecord& record) {
    TimeAlignment out;
    const SampleData& data = record.data;
    const std::size_t count = data.sampleCount();

    if (count < 2) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error, "dat.too_few_samples",
            "El DAT tiene menos de dos muestras; no se puede alinear el tiempo."));
        return out;
    }
    if (data.times_s.size() != count) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error, "dat.times_mismatch",
            "Los timestamps DAT no coinciden con el número de muestras."));
        return out;
    }

    out.first_s = data.times_s.front();
    out.last_s = data.times_s.back();

    std::vector<double> differences;
    differences.reserve(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        const double time = data.times_s[i];
        if (!isFinite(time)) {
            out.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "dat.times_not_finite",
                "Timestamps DAT no finitos; no se puede analizar la señal."));
            return out;
        }
        if (i > 0) {
            const double previous = data.times_s[i - 1];
            if (!(time > previous)) {
                out.diagnostics.push_back(makeDiagnostic(
                    DiagnosticSeverity::Error, "dat.times_order",
                    "Timestamps DAT duplicados o fuera de orden; no se puede "
                    "alinear la señal."));
                return out;
            }
            differences.push_back(time - previous);
        }
    }

    out.median_dt_s = medianOf(differences);
    if (!isFinite(out.median_dt_s) || out.median_dt_s <= 0.0) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error, "dat.times_order",
            "No se puede determinar un intervalo de muestreo positivo."));
        return out;
    }
    out.median_rate_hz = 1.0 / out.median_dt_s;

                                                                            
    out.first_sample_nonzero = out.first_s != 0.0;
    if (out.first_sample_nonzero) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "dat.first_sample_nonzero",
            "La primera muestra DAT no es 0 s (" +
                formatValue(out.first_s, "%.6f") +
                " s); se conserva sin rebasar el inicio y los tiempos se "
                "informan relativos al inicio configurado."));
    }

    std::optional<double> offset = record.trigger_offset_s;
    if (offset.has_value() && !isFinite(*offset)) {
        offset.reset();
    }
    if (!offset.has_value()) {
        offset = dateTimeDifferenceSeconds(record.cfg.start_time, record.cfg.trigger_time);
    }

    const double midpoint = 0.5 * (out.first_s + out.last_s);
    if (!offset.has_value()) {
        out.trigger_offset_known = false;
        out.trigger_within_capture = false;
        out.trigger_reference_is_midpoint = true;
        out.reference_s = midpoint;
        out.reference_label = "referencia provisional";
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.trigger_unreadable",
            "No se pudo interpretar la fecha/hora de inicio o de trigger del "
            "CFG; se usa el centro de la traza como referencia provisional y no "
            "se reportan horas absolutas de evento."));
    } else {
        out.trigger_offset_known = true;
        out.trigger_offset_cfg_s = *offset;
        if (*offset >= out.first_s && *offset <= out.last_s) {
            out.trigger_within_capture = true;
            out.trigger_reference_is_midpoint = false;
            out.reference_s = *offset;
            out.reference_label = "trigger";
        } else {
            out.trigger_within_capture = false;
            out.trigger_reference_is_midpoint = true;
            out.reference_s = midpoint;
            out.reference_label = "referencia provisional";
            out.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "cfg.trigger_outside_dat",
                "El trigger CFG (" + formatValue(*offset, "%.6f") +
                    " s desde el inicio) cae fuera de los timestamps DAT; se "
                    "usa el centro de la traza como referencia provisional. La "
                    "alineación temporal no es válida."));
        }
    }

    out.ok = true;
    return out;
}

                                                                              
                         
                                                                              

std::optional<std::vector<std::complex<double>>> windowPhasors(
    const SampleData& data,
    const std::vector<std::size_t>& channels,
    double frequency_hz,
    double center_s,
    int cycles) {
    if (channels.empty()) {
        return std::nullopt;
    }
    if (!isFinite(frequency_hz) || frequency_hz <= 0.0 || cycles < 1) {
        return std::nullopt;
    }
    const std::size_t count = data.sampleCount();
    if (count == 0 || data.times_s.size() != count) {
        return std::nullopt;
    }
    const double half = static_cast<double>(cycles) / (2.0 * frequency_hz);
    const double low = center_s - half;
    const double high = center_s + half;
    if (low < data.times_s.front() || high > data.times_s.back()) {
        return std::nullopt;
    }

    std::vector<std::size_t> selected;
    selected.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const double time = data.times_s[i];
        if (time >= low && time < high) {
            selected.push_back(i);
        }
    }
    const std::size_t minimum =
        std::max<std::size_t>(8, 4 * channels.size());
    if (selected.size() < minimum) {
        return std::nullopt;
    }

    for (std::size_t channel : channels) {
        if (channel >= data.analog_channels) {
            return std::nullopt;
        }
        for (std::size_t row : selected) {
            if (!isFinite(data.analog[row * data.analog_channels + channel])) {
                return std::nullopt;
            }
        }
    }

    const std::size_t window = selected.size();
    std::vector<double> window_times(window, 0.0);
    for (std::size_t i = 0; i < window; ++i) {
        window_times[i] = data.times_s[selected[i]];
    }
    std::vector<double> weights;
    if (!gradientOfIndex(window_times.data(), window, weights)) {
        return std::nullopt;
    }
    double mean_weight = 0.0;
    for (double weight : weights) {
        mean_weight += weight;
    }
    mean_weight /= static_cast<double>(window);
    if (!(mean_weight > 0.0) || !isFinite(mean_weight)) {
        return std::nullopt;
    }
    std::vector<double> root_weights(window, 0.0);
    for (std::size_t i = 0; i < window; ++i) {
        root_weights[i] = std::sqrt(weights[i] / mean_weight);
    }

    std::vector<double> design(window * 4, 0.0);
    for (std::size_t i = 0; i < window; ++i) {
        const double tau = window_times[i] - center_s;
        const double angle = kTwoPi * frequency_hz * tau;
        design[i * 4 + 0] = root_weights[i];
        design[i * 4 + 1] = tau * root_weights[i];
        design[i * 4 + 2] = std::cos(angle) * root_weights[i];
        design[i * 4 + 3] = std::sin(angle) * root_weights[i];
    }

    std::vector<std::complex<double>> result;
    result.reserve(channels.size());
    for (std::size_t channel : channels) {
        std::vector<double> values(window, 0.0);
        for (std::size_t i = 0; i < window; ++i) {
            values[i] =
                data.analog[selected[i] * data.analog_channels + channel] *
                root_weights[i];
        }
        std::vector<double> solution;
        std::size_t rank = 0;
        if (!leastSquares(design, window, 4, values, solution, rank) || rank < 4) {
            return std::nullopt;
        }
        result.push_back(std::complex<double>(solution[2], -solution[3]) /
                         std::sqrt(2.0));
    }
    return result;
}

static std::optional<AcRmsStats> windowAcRmsImpl(const SampleData& data,
                                      std::size_t channel,
                                      double center_s,
                                      double half_s, bool times_validated) {
    if (channel >= data.analog_channels || !isFinite(center_s) ||
        !isFinite(half_s) || half_s <= 0.0) {
        return std::nullopt;
    }
    const std::size_t count = data.sampleCount();
    if (count == 0 || data.times_s.size() != count || data.analog_channels == 0 ||
        count > data.analog.size() / data.analog_channels) {
        return std::nullopt;
    }
    const double low = center_s - half_s;
    const double high = center_s + half_s;
    if ((!times_validated && !hasUsableTimes(data)) ||
        data.times_s.front() > low || data.times_s.back() < high) {
        return std::nullopt;
    }
    auto valueAt = [&](double time) -> std::optional<double> {
        const auto upper = std::lower_bound(data.times_s.begin(),
                                            data.times_s.end(), time);
        if (upper != data.times_s.end() && *upper == time) {
            const std::size_t index =
                static_cast<std::size_t>(upper - data.times_s.begin());
            const double value = data.analog[index * data.analog_channels + channel];
            return isFinite(value) ? std::optional<double>(value) : std::nullopt;
        }
        if (upper == data.times_s.begin() || upper == data.times_s.end()) {
            return std::nullopt;
        }
        const std::size_t right =
            static_cast<std::size_t>(upper - data.times_s.begin());
        const std::size_t left = right - 1;
        const double t0 = data.times_s[left];
        const double t1 = data.times_s[right];
        const double y0 = data.analog[left * data.analog_channels + channel];
        const double y1 = data.analog[right * data.analog_channels + channel];
        if (!isFinite(t0) || !isFinite(t1) || !(t1 > t0) ||
            !isFinite(y0) || !isFinite(y1)) {
            return std::nullopt;
        }
        const double value = y0 + (y1 - y0) * ((time - t0) / (t1 - t0));
        return isFinite(value) ? std::optional<double>(value) : std::nullopt;
    };

    const auto low_value = valueAt(low);
    const auto high_value = valueAt(high);
    if (!low_value || !high_value) {
        return std::nullopt;
    }
    std::vector<double> window_times{low};
    std::vector<double> values{*low_value};
    const auto begin = std::upper_bound(data.times_s.begin(), data.times_s.end(), low);
    const auto end = std::lower_bound(begin, data.times_s.end(), high);
    for (auto it = begin; it != end; ++it) {
        const std::size_t i = static_cast<std::size_t>(it - data.times_s.begin());
        const double time = *it;
        const double value = data.analog[i * data.analog_channels + channel];
        if (!isFinite(time) || !isFinite(value)) {
            return std::nullopt;
        }
        window_times.push_back(time);
        values.push_back(value);
    }
    window_times.push_back(high);
    values.push_back(*high_value);
    if (window_times.size() < 8) {
        return std::nullopt;
    }

                                                                            
                                                                          
    std::vector<double> weights(window_times.size(), 0.0);
    weights.front() = (window_times[1] - window_times[0]) / 2.0;
    for (std::size_t i = 1; i + 1 < window_times.size(); ++i) {
        weights[i] = (window_times[i + 1] - window_times[i - 1]) / 2.0;
    }
    weights.back() =
        (window_times.back() - window_times[window_times.size() - 2]) / 2.0;
    for (double weight : weights) {
        if (!isFinite(weight) || weight <= 0.0) {
            return std::nullopt;
        }
    }
    double total = 0.0;
    for (double weight : weights) {
        total += weight;
    }
    if (!(total > 0.0) || !isFinite(total)) {
        return std::nullopt;
    }
    double weighted_sum = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        weighted_sum += values[i] * weights[i];
    }
    const double mean = weighted_sum / total;
    double squared = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double centered = values[i] - mean;
        squared += centered * centered * weights[i];
    }
    const double variance = squared / total;
    if (variance < 0.0 || !isFinite(variance)) {
        return std::nullopt;
    }

    AcRmsStats stats;
    stats.mean = mean;
    stats.rms_ac = std::sqrt(variance);
    stats.samples = values.size();
    return stats;
}

std::optional<AcRmsStats> windowAcRms(const SampleData& data,
                                      std::size_t channel,
                                      double center_s,
                                      double half_s) {
    return windowAcRmsImpl(data, channel, center_s, half_s, false);
}

                                                                              
                          
                                                                              

std::optional<SequenceSet> symmetricalComponents(
    const std::vector<std::complex<double>>& phasors) {
    if (phasors.size() < 3) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!isFinite(phasors[i].real()) || !isFinite(phasors[i].imag())) {
            return std::nullopt;
        }
    }
    const std::complex<double> a(std::cos(kTwoPi / 3.0), std::sin(kTwoPi / 3.0));
    const std::complex<double> a2 = a * a;
    const std::complex<double> va = phasors[0];
    const std::complex<double> vb = phasors[1];
    const std::complex<double> vc = phasors[2];

    SequenceSet out;
    out.zero = (va + vb + vc) / 3.0;
    out.positive = (va + a * vb + a2 * vc) / 3.0;
    out.negative = (va + a2 * vb + a * vc) / 3.0;
                                                                            
                                                                        
    if (!finiteSequenceComponents(out)) {
        return std::nullopt;
    }
    return out;
}

std::array<double, 3> sequenceMagnitudes(const SequenceSet& sequences) noexcept {
    return {std::abs(sequences.zero), std::abs(sequences.positive),
            std::abs(sequences.negative)};
}

SequenceRatios computeSequenceRatios(const std::optional<SequenceSet>& pre,
                                     const std::optional<SequenceSet>& fault) {
    SequenceRatios ratios;
    if (!fault.has_value()) {
        return ratios;
    }

                                                                         
                                                                         
                                                                 
    double i1_fault = 0.0;
    const bool fault_positive_usable =
        relativePositiveMagnitude(*fault, i1_fault);
    if (fault_positive_usable) {
        const double r0 = std::abs(fault->zero) / i1_fault;
        const double r2 = std::abs(fault->negative) / i1_fault;
        if (isFinite(r0) && r0 >= 0.0) {
            ratios.i0_i1 = r0;
        }
        if (isFinite(r2) && r2 >= 0.0) {
            ratios.i2_i1 = r2;
        }
    }

                                                                               
    if (fault_positive_usable && pre.has_value()) {
        double i1_pre = 0.0;
        if (relativePositiveMagnitude(*pre, i1_pre)) {
            const double increase = i1_fault / i1_pre;
            if (isFinite(increase) && increase > 0.0) {
                ratios.i1_fault_pre = increase;
            }
        }
    }
    return ratios;
}

                                                                              
                          
                                                                              

WindowCheckResult checkPhasorWindows(const SampleData& data,
                                     const std::vector<std::size_t>& channels,
                                     double frequency_hz,
                                     const std::vector<WindowSpec>& windows,
                                     int cycles,
                                     const std::vector<std::string>& channel_names) {
    WindowCheckResult out;
    if (channels.empty()) {
        out.errors.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.window.no_channels",
            "No hay canales de corriente seleccionados."));
        return out;
    }
    if (!isFinite(frequency_hz) || frequency_hz <= 0.0 || cycles < 1) {
        out.errors.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.window.invalid",
            "Frecuencia o número de ciclos inválido para verificar ventanas."));
        return out;
    }
    const std::size_t count = data.sampleCount();
    if (count == 0 || data.times_s.size() != count) {
        out.errors.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.window.no_data",
            "No hay timestamps DAT utilizables para verificar ventanas."));
        return out;
    }

    const double half = static_cast<double>(cycles) / (2.0 * frequency_hz);
    const double first = data.times_s.front();
    const double last = data.times_s.back();
    const std::size_t minimum =
        std::max<std::size_t>(8, 4 * channels.size());

    struct ValidWindow {
        std::string label;
        double low = 0.0;
        double high = 0.0;
    };
    std::vector<ValidWindow> valid;

    for (const WindowSpec& window : windows) {
        const double low = window.center_s - half;
        const double high = window.center_s + half;
        if (low < first || high > last) {
            out.errors.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "analysis.window.outside",
                "La ventana " + window.label + " (" + formatValue(low, "%.6f") +
                    "–" + formatValue(high, "%.6f") +
                    " s) sale del registro; no debe interpretarse."));
            continue;
        }
        valid.push_back({window.label, low, high});

        std::size_t samples = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const double time = data.times_s[i];
            if (time >= low && time < high) {
                ++samples;
            }
        }
        if (samples < minimum) {
            out.errors.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "analysis.window.too_few_samples",
                "La ventana " + window.label +
                    " contiene muy pocas muestras para calcular fasores."));
            continue;
        }

        const auto phasors =
            windowPhasors(data, channels, frequency_hz, window.center_s, cycles);
        if (!phasors.has_value() || phasors->size() != channels.size()) {
            out.errors.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "analysis.window.phasor_failed",
                "No se pudieron calcular todos los fasores de la ventana " +
                    window.label + "."));
            continue;
        }

        for (std::size_t position = 0; position < channels.size(); ++position) {
            const std::size_t channel = channels[position];
            std::string name;
            if (channel < channel_names.size()) {
                name = channel_names[channel];
            } else {
                name = std::to_string(channel + 1);
            }
            const auto rms = windowAcRms(data, channel, window.center_s, half);
            if (!rms.has_value()) {
                out.errors.push_back(makeDiagnostic(
                    DiagnosticSeverity::Warning, "analysis.window.ac_rms_failed",
                    "Canal " + name + ", ventana " + window.label +
                        ": no se pudo calcular la RMS AC de la traza."));
                continue;
            }
            const double fundamental = std::abs((*phasors)[position]);
            const double safe_fundamental = std::max(fundamental, 1e-12);

            WindowCoherenceRow row;
            row.window_label = window.label;
            row.channel_name = name;
            row.ac_rms = rms->rms_ac;
            row.fundamental_rms = fundamental;
            row.ratio = rms->rms_ac / safe_fundamental;
            out.rows.push_back(row);

            if (rms->rms_ac > 1e-9 && fundamental < rms->rms_ac * 0.10) {
                out.errors.push_back(makeDiagnostic(
                    DiagnosticSeverity::Warning, "analysis.window.fundamental_low",
                    "Canal " + name + ", ventana " + window.label +
                        ": la fundamental es menor que 10% de la RMS AC de la "
                        "misma traza (>10:1); revisar escala, canal, frecuencia y "
                        "ventana. La distorsión también puede explicar la diferencia."));
            } else if (fundamental > rms->rms_ac * 1.10) {
                out.errors.push_back(makeDiagnostic(
                    DiagnosticSeverity::Warning,
                    "analysis.window.fundamental_above_trace",
                    "Canal " + name + ", ventana " + window.label +
                        ": la RMS fundamental supera la RMS AC de la misma señal; "
                        "revisar cálculo y coherencia de datos."));
            }
        }
    }

    for (std::size_t i = 0; i < valid.size(); ++i) {
        for (std::size_t j = i + 1; j < valid.size(); ++j) {
            if (valid[i].low < valid[j].high && valid[j].low < valid[i].high) {
                out.errors.push_back(makeDiagnostic(
                    DiagnosticSeverity::Warning, "analysis.window.overlap",
                    "Las ventanas " + valid[i].label + " y " + valid[j].label +
                        " se solapan (" + formatValue(valid[i].low, "%.6f") + "–" +
                        formatValue(valid[i].high, "%.6f") + " s y " +
                        formatValue(valid[j].low, "%.6f") + "–" +
                        formatValue(valid[j].high, "%.6f") +
                        " s); no representan regímenes independientes."));
            }
        }
    }

                                                                     
    std::vector<Diagnostic> unique;
    std::vector<std::string> seen;
    for (const Diagnostic& diagnostic : out.errors) {
        if (std::find(seen.begin(), seen.end(), diagnostic.message) == seen.end()) {
            seen.push_back(diagnostic.message);
            unique.push_back(diagnostic);
        }
    }
    out.errors = std::move(unique);
    return out;
}

                                                                              
                                      
                                                                              

static std::optional<OnsetEvent> detectOnsetAggregate(const SampleData& data,
                                      const std::vector<std::size_t>& channels,
                                      double frequency_hz,
                                      double trigger_s,
                                      double ratio) {
    const std::size_t count = data.sampleCount();
    if (channels.size() < 3 || !isFinite(frequency_hz) || frequency_hz <= 0.0 ||
        count < 16) {
        return std::nullopt;
    }
    std::array<std::size_t, 3> selected = {channels[0], channels[1], channels[2]};
    for (std::size_t channel : selected) {
        if (channel >= data.analog_channels) {
            return std::nullopt;
        }
    }
    if (data.times_s.size() != count) {
        return std::nullopt;
    }

    const double period = 1.0 / frequency_hz;
    std::vector<double> differences(count - 1, 0.0);
    for (std::size_t i = 1; i < count; ++i) {
        differences[i - 1] = data.times_s[i] - data.times_s[i - 1];
    }
    const double median_dt = medianOf(differences);
    if (!isFinite(median_dt) || median_dt <= 0.0) {
        return std::nullopt;
    }
    const long long stride_value =
        std::llround(period / (20.0 * median_dt));
    const std::size_t stride =
        static_cast<std::size_t>(std::max<long long>(1, stride_value));

    std::vector<std::size_t> centers;
    for (std::size_t index = 0; index < count; index += stride) {
        centers.push_back(index);
    }
    const std::size_t m = centers.size();
    if (m < 8) {
        return std::nullopt;
    }
    std::vector<double> ct(m, 0.0);
    for (std::size_t i = 0; i < m; ++i) {
        ct[i] = data.times_s[centers[i]];
    }

                                                             
    const std::size_t ac = data.analog_channels;
    std::vector<double> prefix((count + 1) * 3, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t phase = 0; phase < 3; ++phase) {
            const double value = data.analog[i * ac + selected[phase]];
            prefix[(i + 1) * 3 + phase] =
                prefix[i * 3 + phase] + value * value;
        }
    }

    std::vector<double> envelope(m, std::numeric_limits<double>::quiet_NaN());
    std::vector<int> counts(m, 1);
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t left = lowerBoundIndex(data.times_s, ct[i] - period / 2.0);
        const std::size_t right = lowerBoundIndex(data.times_s, ct[i] + period / 2.0);
        std::size_t width = (right > left) ? right - left : 1;
        counts[i] = static_cast<int>(width);

        double mean_square = 0.0;
        for (std::size_t phase = 0; phase < 3; ++phase) {
            const double sum = prefix[right * 3 + phase] - prefix[left * 3 + phase];
            const double phase_rms =
                std::sqrt(std::max(sum, 0.0) / static_cast<double>(width));
            mean_square += phase_rms * phase_rms;
        }
        double value = std::sqrt(mean_square / 3.0);
        const bool full_cycle = (ct[i] - period / 2.0 >= data.times_s.front()) &&
                                (ct[i] + period / 2.0 <= data.times_s.back());
        if (!full_cycle) {
            value = std::numeric_limits<double>::quiet_NaN();
        }
        envelope[i] = value;
    }

                                                            
    std::vector<double> smooth(m, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < m; ++i) {
        const std::size_t begin = (i > 0) ? i - 1 : 0;
        const std::size_t end = std::min(i + 2, m);
        std::vector<double> segment;
        for (std::size_t j = begin; j < end; ++j) {
            if (isFinite(envelope[j])) {
                segment.push_back(envelope[j]);
            }
        }
        if (!segment.empty()) {
            smooth[i] = medianOf(segment);
        }
    }

    const double first_time = data.times_s.front();
    const double last_time = data.times_s.back();
                                                                             
    (void)trigger_s;
    std::vector<double> baseline_values;
    const double initial_limit = first_time + 3.0 * period;
    for (std::size_t i = 0; i < m; ++i) {
        if (ct[i] <= initial_limit && isFinite(envelope[i]) && isFinite(smooth[i])) {
            baseline_values.push_back(smooth[i]);
        }
    }
    if (baseline_values.size() < 8 || last_time - first_time < 4.0 * period) {
        return std::nullopt;
    }
    const double baseline = medianOf(baseline_values);
    std::vector<double> initial_deviations;
    for (const double value : baseline_values) {
        initial_deviations.push_back(std::fabs(value - baseline));
    }
    const double initial_noise = 1.4826 * medianOf(initial_deviations);
    const auto range = std::minmax_element(baseline_values.begin(), baseline_values.end());
    const double level = std::max(std::fabs(baseline), 1e-9);
    if (!isFinite(baseline) || initial_noise > 0.05 * level ||
        *range.second - *range.first > 0.20 * level) {
        return std::nullopt;
    }
    const std::string baseline_source =
        "segmento inicial estable de tres ciclos (referencia candidata, no acredita prefalla sana)";
    std::vector<double> deviations;
    deviations.reserve(baseline_values.size());
    for (double value : baseline_values) {
        deviations.push_back(std::fabs(value - baseline));
    }
    const double noise = 1.4826 * medianOf(deviations);
    const double peak = percentile95(smooth);
    if (!isFinite(baseline)) {
        return std::nullopt;
    }

    const double threshold = std::max(
        {(ratio - 1.0) * std::max(baseline, 1e-9), 6.0 * noise,
         0.02 * std::max(peak, 1e-9)});

    std::vector<char> active(m, 0);
    for (std::size_t i = 0; i < m; ++i) {
        if (isFinite(smooth[i]) && std::fabs(smooth[i] - baseline) > threshold) {
            active[i] = 1;
        }
    }

    std::vector<std::size_t> active_prefix(m + 1, 0);
    for (std::size_t i = 0; i < m; ++i) {
        active_prefix[i + 1] = active_prefix[i] + (active[i] ? 1 : 0);
    }

    std::vector<char> persistent(m, 0);
    for (std::size_t k = 0; k < m; ++k) {
        if (!active[k]) {
            continue;
        }
        const std::size_t end = upperBoundIndex(ct, ct[k] + period);
        const double elapsed =
            (end > k) ? ct[std::min(end - 1, m - 1)] - ct[k] : 0.0;
        if (end - k >= 4 && elapsed >= 0.75 * period) {
            const double fraction =
                static_cast<double>(active_prefix[end] - active_prefix[k]) /
                static_cast<double>(end - k);
            if (fraction >= 0.8) {
                persistent[k] = 1;
            }
        }
    }

    std::size_t first = m;
    for (std::size_t i = 0; i < m; ++i) {
        if (persistent[i] && (i == 0 || !persistent[i - 1])) {
            first = i;
            break;
        }
    }
    if (first == m) {
        return std::nullopt;
    }

    const std::size_t end = upperBoundIndex(ct, ct[first] + period);
    const std::size_t last_inclusive = std::min(end, m - 1);
    double best_slope = -1.0;
    std::size_t best_index = 0;
    for (std::size_t i = first; i < last_inclusive; ++i) {
        const double slope = std::fabs(smooth[i + 1] - smooth[i]);
        if (isFinite(slope) && slope > best_slope) {
            best_slope = slope;
            best_index = i;
        }
    }
    if (!(best_slope >= 0.0)) {
        return std::nullopt;
    }
    const std::size_t k = best_index;
    const double candidate = ct[k];
    if (candidate <= first_time + period || candidate >= last_time - period) {
        return std::nullopt;
    }

    const double required_inactive = 0.75 * period;
    std::optional<std::size_t> recovery_index;
    std::size_t last_active_index = first;
    for (std::size_t index = std::max(first, k); index < m; ++index) {
        if (active[index]) {
            last_active_index = index;
            continue;
        }
        const std::size_t recovery_end =
            upperBoundIndex(ct, ct[index] + required_inactive);
        if (recovery_end > m) {
            break;
        }
        const std::size_t last_index =
            std::min(recovery_end - 1, m - 1);
        const double elapsed = ct[last_index] - ct[index];
        if (elapsed >= required_inactive) {
            const double inactive_fraction =
                1.0 -
                static_cast<double>(active_prefix[recovery_end] -
                                    active_prefix[index]) /
                    static_cast<double>(recovery_end - index);
            if (inactive_fraction >= 0.8) {
                recovery_index = index;
                break;
            }
        }
    }

    OnsetEvent event;
    event.time_s = candidate;
    event.persistent = true;
    event.threshold_delta = threshold;
    event.baseline_rms = baseline;
    event.baseline_source = baseline_source;
    std::vector<double> count_values;
    count_values.reserve(counts.size());
    for (int value : counts) {
        count_values.push_back(static_cast<double>(value));
    }
    event.window_samples = static_cast<int>(medianOf(count_values));

    if (recovery_index.has_value()) {
        event.recovery_detected = true;
        event.recovery_censored = false;
        event.recovery_s = ct[*recovery_index];
        event.duration_s = std::max(0.0, *event.recovery_s - candidate);
        event.duration_is_lower_bound = false;
    } else {
        event.recovery_detected = false;
        event.recovery_censored = true;
        event.duration_s = std::max(0.0, ct[last_active_index] - candidate);
        event.duration_is_lower_bound = true;
    }

    const std::size_t peak_end =
        recovery_index.has_value() ? *recovery_index : m - 1;
    const std::size_t peak_start = std::max(first, k);
    double peak_envelope = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t i = peak_start; i <= peak_end && i < m; ++i) {
        if (isFinite(envelope[i]) &&
            !(peak_envelope >= envelope[i])) {
            peak_envelope = envelope[i];
        }
    }
    event.peak_envelope = peak_envelope;
    if (baseline > 1e-9 && isFinite(peak_envelope)) {
        event.peak_contrast = peak_envelope / baseline;
    }

    event.rms = isFinite(envelope[k]) ? envelope[k] : 0.0;
    event.delta = std::fabs(smooth[k] - baseline);
    event.contrast = event.delta / std::max(threshold, 1e-9);
    const double signed_change = smooth[k] - baseline;
    if (signed_change > 0.0) {
        event.direction = ChangeDirection::Rise;
    } else if (signed_change < 0.0) {
        event.direction = ChangeDirection::Fall;
    } else {
        event.direction = ChangeDirection::Change;
    }
    event.direction_text = changeDirectionName(event.direction);
    return event;
}

std::optional<OnsetEvent> detectOnset(const SampleData& data,
                                      const std::vector<std::size_t>& channels,
                                      double frequency_hz,
                                      double trigger_s,
                                      double ratio) {
    if (channels.size() < 3) return std::nullopt;
                                                                             
                                                                            
    std::optional<OnsetEvent> earliest;
    for (std::size_t phase = 0; phase < 3; ++phase) {
        const std::vector<std::size_t> one_phase(3, channels[phase]);
        auto event = detectOnsetAggregate(data, one_phase, frequency_hz,
                                         trigger_s, ratio);
        if (event && (!earliest || event->time_s < earliest->time_s)) {
            event->baseline_source += " · fase " + std::string(1, 'A' + static_cast<char>(phase));
            earliest = std::move(event);
        }
    }
    return earliest;
}

                                                                            
static bool stableReference(const SampleData& data,
                            const std::vector<std::size_t>& channels,
                            double center, double period,
                            std::vector<double>* levels = nullptr) {
    if (channels.size() != 3 || data.times_s.empty() ||
        center - period < data.times_s.front() ||
        center + period > data.times_s.back()) return false;
    for (const auto channel : channels) {
        std::vector<double> values;
        for (int step = -1; step <= 1; ++step) {
            const auto rms = windowAcRms(data, channel,
                center + static_cast<double>(step) * period / 2.0, period / 2.0);
            if (!rms || !isFinite(rms->rms_ac) || rms->samples < 8) return false;
            values.push_back(rms->rms_ac);
        }
        const double median = medianOf(values);
        const auto range = std::minmax_element(values.begin(), values.end());
        if (*range.second - *range.first > 0.20 * std::max(median, 1e-9)) return false;
        if (levels) levels->push_back(median);
    }
    return true;
}

static std::vector<RegimeChange> findRegimeChanges(
    const ComtradeRecord& record, const std::vector<std::size_t>& currents,
    const std::vector<std::size_t>& voltages, double period) {
    std::vector<RegimeChange> changes;
    const auto& data = record.data;
    if (currents.size() != 3 || !hasUsableTimes(data) || data.analog_channels == 0 ||
        data.sampleCount() > data.analog.size() / data.analog_channels) return changes;
                                                                                 
    const std::size_t frames = static_cast<std::size_t>(std::min(200000.0,
        std::floor((data.times_s.back() - data.times_s.front()) / period)));
    if (frames < 6) return changes;
    std::vector<std::array<double, 3>> rms(frames);
    std::array<double, 3> peak{};
    for (std::size_t i = 0; i < frames; ++i) {
        const double center = data.times_s.front() + (static_cast<double>(i) + 0.5) * period;
        for (std::size_t phase = 0; phase < 3; ++phase) {
            const auto stats = windowAcRmsImpl(data, currents[phase], center, period / 2.0, true);
            rms[i][phase] = stats && stats->samples >= 8 ? stats->rms_ac :
                std::numeric_limits<double>::quiet_NaN();
            if (isFinite(rms[i][phase])) peak[phase] = std::max(peak[phase], rms[i][phase]);
        }
    }
    double last_change = -std::numeric_limits<double>::infinity();
    std::array<double, 3> reference{};
    for (std::size_t phase = 0; phase < 3; ++phase)
        reference[phase] = (rms[0][phase] + rms[1][phase]) / 2.0;
    for (std::size_t i = 2; i + 2 < frames; ++i) {
        const double time = data.times_s.front() + static_cast<double>(i) * period;
        RegimeChange candidate;
        candidate.time_s = time;
        bool changed = false;
        for (std::size_t phase = 0; phase < 3; ++phase) {
            const double pre = reference[phase];
            const double post = (rms[i+1][phase] + rms[i+2][phase]) / 2.0;
            if (!isFinite(pre) || !isFinite(post)) continue;
            const double threshold = std::max(0.20 * std::max(pre, 1e-9),
                0.02 * peak[phase]);
            const bool post_stable = std::fabs(rms[i+1][phase] - rms[i+2][phase]) <=
                0.15 * std::max({post, peak[phase] * 0.02, 1e-9});
            if (post_stable && std::fabs(post - pre) > threshold) {
                candidate.phase_direction[phase] = post > pre ? 1 : -1;
                changed = true;
                reference[phase] = post;
            }
        }
        if (!changed) continue;
                                                                                 
        for (const auto channel : voltages) {
            const auto pre = windowAcRms(data, channel, time - period, period / 2.0);
            const auto post = windowAcRms(data, channel, time + period, period / 2.0);
            if (pre && post && std::fabs(post->rms_ac - pre->rms_ac) >
                0.15 * std::max(pre->rms_ac, 1e-9)) candidate.voltage_change_nearby = true;
        }
        candidate.digital_transition_nearby =
            !digitalTransitions(record, time, 2.0 * period).empty();
        if (!changes.empty() && time - last_change < 2.5 * period) {
            auto& previous = changes.back();
            for (std::size_t phase = 0; phase < 3; ++phase) {
                if (!previous.phase_direction[phase])
                    previous.phase_direction[phase] = candidate.phase_direction[phase];
            }
            previous.voltage_change_nearby |= candidate.voltage_change_nearby;
            previous.digital_transition_nearby |= candidate.digital_transition_nearby;
            continue;
        }
        changes.push_back(candidate);
        last_change = time;
    }
    return changes;
}

std::optional<OnsetEvent> detectEvent(const SampleData& data,
                                      const std::vector<std::size_t>& channels,
                                      double frequency_hz,
                                      double trigger_s,
                                      double ratio) {
    return detectOnset(data, channels, frequency_hz, trigger_s, ratio);
}

                                                                              
           
                                                                              

WindowCenters defaultWindowCenters(std::optional<double> onset_s,
                                   double first_s,
                                   double last_s,
                                   double frequency_hz,
                                   int cycles,
                                   std::optional<double> trigger_s) {
    WindowCenters centers;
    const double midpoint = 0.5 * (first_s + last_s);
    if (!isFinite(frequency_hz) || frequency_hz <= 0.0 || cycles < 1) {
        centers.prefalla_s = midpoint;
        centers.falla_s = midpoint;
        return centers;
    }
    const double half = static_cast<double>(cycles) / (2.0 * frequency_hz);
    const double low = first_s + half;
    const double high = last_s - half;
    if (low > high) {
        centers.prefalla_s = midpoint;
        centers.falla_s = midpoint;
        return centers;
    }
    double anchor = midpoint;
    if (onset_s.has_value()) {
        anchor = *onset_s;
    } else if (trigger_s.has_value()) {
        anchor = *trigger_s;
    }
    const double offset = (1.0 + static_cast<double>(cycles) / 2.0) / frequency_hz;
    centers.prefalla_s = std::min(std::max(anchor - offset, low), high);
    centers.falla_s = std::min(std::max(anchor + offset, low), high);
    return centers;
}

ClampedCenter clampCenter(double center_s,
                          double first_s,
                          double last_s,
                          double half_s) {
    ClampedCenter out;
    const double low = first_s + half_s;
    const double high = last_s - half_s;
    if (low > high) {
        out.center_s = 0.5 * (first_s + last_s);
        out.clamped = true;
        return out;
    }
    const double bounded = std::min(std::max(center_s, low), high);
    out.center_s = bounded;
    const double tolerance =
        1e-12 + 1e-9 * std::max(std::fabs(bounded), std::fabs(center_s));
    out.clamped = !(std::fabs(bounded - center_s) <= tolerance);
    return out;
}

                                                                              
                             
                                                                              

std::optional<FaultInterpretation> classifyFault(
    const std::vector<std::complex<double>>& pre,
    const std::vector<std::complex<double>>& fault,
    const FaultClassifyContext& context) {
    if (pre.size() < 3 || fault.size() < 3) {
        return std::nullopt;
    }
    const auto pre_sequences = symmetricalComponents(pre);
    const auto fault_sequences = symmetricalComponents(fault);
    if (!pre_sequences.has_value() || !fault_sequences.has_value()) {
        return std::nullopt;
    }
                                                                           
                                                                          
                                              
    const SequenceRatios ratios =
        computeSequenceRatios(pre_sequences, fault_sequences);
    if (!ratios.i0_i1.has_value() || !ratios.i2_i1.has_value() ||
        !ratios.i1_fault_pre.has_value()) {
        return std::nullopt;
    }
    const double r0 = *ratios.i0_i1;
    const double r2 = *ratios.i2_i1;
    const double increase = *ratios.i1_fault_pre;
    if (!isFinite(r0) || !isFinite(r2) || !isFinite(increase)) {
        return std::nullopt;
    }

    FaultInterpretation out;
    out.i0_i1 = r0;
    out.i2_i1 = r2;
    out.i1_fault_pre = increase;

    if (context.onset_s.has_value() && context.pre_center_s.has_value() &&
        context.window_duration_s.has_value() &&
        *context.pre_center_s + *context.window_duration_s / 2.0 >=
            *context.onset_s) {
        out.label = "Prefalla posiblemente contaminada por el inicio candidato";
        out.explanation =
            "La ventana prefalla se solapa con el inicio automático candidato. Si "
            "ese inicio es válido, estas dos ventanas no representan una "
            "comparación prefalla/falla limpia; revise el registro y ajuste las "
            "ventanas.";
    } else if (increase < 1.5) {
        out.label = "Sin incremento suficiente de secuencia positiva para clasificar";
        out.explanation =
            "I1 cambia menos de 1,5× entre ventanas. Puede ser un cambio pequeño "
            "real, una variación de carga o una ventana prefalla que ya contiene "
            "parte del evento; revise la tendencia temporal antes de concluir.";
    } else if (r0 >= 0.30 && r2 >= 0.30) {
        out.label = "Desequilibrio con secuencias cero y negativa elevadas";
        out.explanation =
            "Compatible con un evento desequilibrado con posible participación de "
            "tierra; no identifica por sí solo el tipo exacto ni la fase.";
    } else if (r0 >= 0.30) {
        out.label = "Secuencia cero elevada; posible participación de tierra";
        out.explanation =
            "Es un indicio que debe confirmarse con conexión, polaridad y escala "
            "del residual/neutro y con las tensiones.";
    } else if (r2 >= 0.30) {
        out.label = "Secuencia negativa elevada; evento desequilibrado posible";
        out.explanation =
            "Puede ser compatible con una falla entre fases, pero también con "
            "desbalance de carga, canales mal asignados o ventanas transitorias.";
    } else {
        out.label = "Secuencia positiva dominante";
        out.explanation =
            "Puede corresponder a un evento trifásico o a un cambio de carga; "
            "estas relaciones no permiten distinguirlos.";
    }
    return out;
}

                                                                              
             
                                                                              

HarmonicFit harmonicFit(const SampleData& data,
                        std::size_t channel,
                        double frequency_hz,
                        double center_s,
                        int cycles,
                        int max_order) {
    HarmonicFit fit;
    fit.cycles = cycles;
    if (channel >= data.analog_channels) {
        fit.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error, "analysis.harmonic.channel",
            "El canal solicitado no existe en el DAT."));
        return fit;
    }
    if (!isFinite(frequency_hz) || frequency_hz <= 0.0 || cycles < 1 ||
        !isFinite(center_s)) {
        fit.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.harmonic.invalid",
            "Frecuencia, centro o ciclos inválidos; no se ajustan armónicos."));
        return fit;
    }
    const std::size_t count = data.sampleCount();
    if (count == 0 || data.times_s.size() != count) {
        return fit;
    }
    const double half = static_cast<double>(cycles) / (2.0 * frequency_hz);
    if (center_s - half < data.times_s.front() ||
        center_s + half > data.times_s.back()) {
        fit.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.harmonic.out_of_record",
            "La ventana de armónicos sale del registro; no se ajusta."));
        return fit;
    }

    std::vector<double> times;
    std::vector<double> values;
    for (std::size_t i = 0; i < count; ++i) {
        const double time = data.times_s[i];
        if (time >= center_s - half && time < center_s + half) {
            const double value = data.analog[i * data.analog_channels + channel];
            if (!isFinite(value)) {
                fit.diagnostics.push_back(makeDiagnostic(
                    DiagnosticSeverity::Info, "analysis.harmonic.not_finite",
                    "La ventana contiene valores no finitos; no se ajusta."));
                return fit;
            }
            times.push_back(time);
            values.push_back(value);
        }
    }
    if (times.size() < 8) {
        fit.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.harmonic.too_few_samples",
            "La ventana de armónicos contiene menos de 8 muestras."));
        return fit;
    }

    double max_dt = 0.0;
    for (std::size_t i = 1; i < times.size(); ++i) {
        max_dt = std::max(max_dt, times[i] - times[i - 1]);
    }
    if (!(max_dt > 0.0) || !isFinite(max_dt)) {
        return fit;
    }
    const double nyquist = (1.0 / max_dt) / 2.0;
    fit.nyquist_hz = nyquist;
                                                                            
                                                                      
    if (nyquist <= frequency_hz) {
        fit.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.harmonic.nyquist",
            "La frecuencia de Nyquist no supera la fundamental; no se ajustan "
            "armónicos."));
        return fit;
    }
                                                                          
                                                               
    const double raw_orders = std::floor(nyquist / frequency_hz - 1e-9);
    const int order_cap = std::min(max_order, kMaxHarmonicOrder);
    int supported = 1;
    if (raw_orders >= 1.0 && order_cap >= 1) {
        supported = static_cast<int>(
            std::max(1.0, std::min<double>(raw_orders, order_cap)));
    }

    const std::size_t n = times.size();
    while (supported >= 1) {
        const std::size_t columns = 2 + 2 * static_cast<std::size_t>(supported);
        if (columns > n) {
            --supported;
            continue;
        }
        std::vector<double> design(n * columns, 0.0);
        for (std::size_t row = 0; row < n; ++row) {
            const double tau = times[row] - center_s;
            design[row * columns + 0] = 1.0;
            design[row * columns + 1] = tau;
            for (int order = 1; order <= supported; ++order) {
                const double angle =
                    kTwoPi * frequency_hz * static_cast<double>(order) * tau;
                const std::size_t base =
                    2 + 2 * static_cast<std::size_t>(order - 1);
                design[row * columns + base] = std::cos(angle);
                design[row * columns + base + 1] = std::sin(angle);
            }
        }
        std::vector<double> solution;
        std::size_t rank = 0;
        if (!leastSquares(design, n, columns, values, solution, rank) ||
            rank != columns) {
            --supported;
            continue;
        }

        std::vector<double> harmonics;
        harmonics.reserve(static_cast<std::size_t>(supported));
        for (int order = 1; order <= supported; ++order) {
            const std::size_t base = 2 + 2 * static_cast<std::size_t>(order - 1);
            harmonics.push_back(
                std::hypot(solution[base], solution[base + 1]) / std::sqrt(2.0));
        }
        fit.ok = true;
        fit.supported_order = supported;
        fit.harmonics_rms = std::move(harmonics);
        if (supported >= 2 && fit.harmonics_rms[0] > 0.0) {
            double sum_squares = 0.0;
            for (std::size_t i = 1; i < fit.harmonics_rms.size(); ++i) {
                sum_squares += fit.harmonics_rms[i] * fit.harmonics_rms[i];
            }
            fit.thd_pct =
                100.0 * std::sqrt(sum_squares) / fit.harmonics_rms[0];
            fit.h2_h1_pct = 100.0 * fit.harmonics_rms[1] / fit.harmonics_rms[0];
        }
        return fit;
    }

    fit.diagnostics.push_back(makeDiagnostic(
        DiagnosticSeverity::Info, "analysis.harmonic.rank",
        "No se encontró un ajuste armónico con rango completo en esta ventana."));
    return fit;
}

                                                                              
                                                                    
                                                                              

SpectrumResult fftSpectrum(const SampleData& data,
                           std::size_t channel,
                           const std::string& unit,
                           double frequency_hz,
                           double center_s,
                           int cycles,
                           int max_order,
                           std::size_t max_points) {
    SpectrumResult out;
    out.cycles = cycles;
    out.fundamental_hz = frequency_hz;
    out.unit = unit;
    const int order_cap = std::clamp(max_order, 0, kMaxHarmonicOrder);
    out.max_order = order_cap;

    auto fail = [&out](std::string code, std::string message) {
        out.diagnostics.push_back(
            makeDiagnostic(DiagnosticSeverity::Warning, std::move(code),
                           std::move(message)));
        return out;
    };

    if (channel >= data.analog_channels) {
        return fail("analysis.fft.channel",
                    "El canal solicitado para la FFT no existe en el DAT.");
    }
    if (!isFinite(frequency_hz) || frequency_hz <= 0.0 || cycles < 1 ||
        !isFinite(center_s)) {
        return fail("analysis.fft.invalid",
                    "Frecuencia, centro o ciclos inválidos; no se calcula el "
                    "espectro FFT.");
    }
    const std::size_t total = data.sampleCount();
    if (total == 0 || data.times_s.size() != total) {
        return fail("analysis.fft.no_data",
                    "No hay timestamps DAT utilizables para el espectro FFT.");
    }
    if (!hasUsableTimes(data)) {
        return fail("analysis.fft.timestamps",
                    "Los timestamps DAT no son finitos o no son crecientes; no "
                    "se calcula el espectro FFT.");
    }

    const double half = static_cast<double>(cycles) / (2.0 * frequency_hz);
    const double low = center_s - half;
    const double high = center_s + half;
    if (!isFinite(low) || !isFinite(high) || low < data.times_s.front() ||
        high > data.times_s.back()) {
        return fail("analysis.fft.out_of_record",
                    "La ventana FFT sale del registro; no se calcula el "
                    "espectro ni se extrapolan muestras.");
    }

    std::size_t source_samples = 0;
    double median_dt = 0.0;
    if (!windowSourceStats(data, low, high, source_samples, median_dt)) {
        return fail("analysis.fft.invalid_rate",
                    "No se pudo determinar una tasa de muestreo positiva en la "
                    "ventana FFT a partir de los timestamps DAT.");
    }
    if (source_samples < static_cast<std::size_t>(kFftMinSourceSamples)) {
        return fail("analysis.fft.too_few_samples",
                    "La ventana FFT contiene menos de " +
                        std::to_string(kFftMinSourceSamples) +
                        " muestras fuente; no se interpola ni se inventa "
                        "resolución.");
    }
    const double sample_rate = 1.0 / median_dt;
    if (!isFinite(sample_rate) || sample_rate <= 0.0) {
        return fail("analysis.fft.invalid_rate",
                    "La tasa de muestreo de la ventana FFT no es válida.");
    }
    out.sample_rate_hz = sample_rate;
    out.window_start_s = low;
    out.window_end_s = high;

    const double raw_points =
        static_cast<double>(cycles) * sample_rate / frequency_hz;
    if (!isFinite(raw_points) || raw_points <= 0.0) {
        return fail("analysis.fft.invalid_grid",
                    "No se pudo dimensionar la rejilla uniforme de la FFT.");
    }
    const double bounded_raw = std::min(raw_points, 1.0e9);
    std::size_t natural_points =
        static_cast<std::size_t>(kFftMinSourceSamples);
    if (bounded_raw > static_cast<double>(kFftMinSourceSamples)) {
        natural_points =
            static_cast<std::size_t>(std::llround(bounded_raw));
    }

    const std::size_t cap =
        max_points == 0 ? kFftMaxPoints : std::min(max_points, kFftMaxPoints);
    if (cap < 2) {
        return fail("analysis.fft.max_points",
                    "max_points es demasiado pequeño para construir una FFT; "
                    "use al menos 2 puntos.");
    }
    bool reduced = false;
    std::size_t grid_points = natural_points;
    if (grid_points > cap) {
        grid_points = cap;
        reduced = true;
    }
    std::size_t fft_points = nextPowerOfTwo(grid_points);
    if (fft_points > cap) {
        fft_points = previousPowerOfTwo(cap);
        if (fft_points < 2) {
            fft_points = 2;
        }
        if (grid_points > fft_points) {
            grid_points = fft_points;
        }
        reduced = true;
    }
    if (grid_points > fft_points) {
        grid_points = fft_points;
    }
    if (grid_points < 2) {
        return fail("analysis.fft.invalid_grid",
                    "La rejilla FFT resultante es demasiado corta.");
    }
    if (reduced) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.fft.reduced_resolution",
            "La rejilla FFT se redujo a " + std::to_string(grid_points) +
                " muestras y " + std::to_string(fft_points) +
                " puntos FFT para respetar max_points=" + std::to_string(cap) +
                "; se pierde resolución espectral. No se añaden bins ni "
                "armónicos fuera de Nyquist."));
    }

    std::vector<double> values;
    if (!interpolateUniform(data, channel, low, high, grid_points, values)) {
        return fail("analysis.fft.interpolation",
                    "No se pudo interpolar el canal en la rejilla uniforme de la "
                    "FFT; revise timestamps y valores no finitos.");
    }
    double mean = 0.0;
    for (double value : values) {
        mean += value;
    }
    mean /= static_cast<double>(values.size());
    if (!isFinite(mean)) {
        return fail("analysis.fft.interpolation",
                    "La media de la ventana FFT no es finita.");
    }
    for (double& value : values) {
        value -= mean;
    }
    const std::vector<double> window = hannWindow(grid_points);
    double window_sum = 0.0;
    for (double value : window) {
        window_sum += value;
    }
    if (!(window_sum > 0.0) || !isFinite(window_sum)) {
        return fail("analysis.fft.window",
                    "La ventana Hann no tiene suma utilizable.");
    }

    std::vector<std::complex<double>> buffer(
        fft_points, std::complex<double>(0.0, 0.0));
    for (std::size_t i = 0; i < grid_points; ++i) {
        buffer[i] = std::complex<double>(values[i] * window[i], 0.0);
    }
    fftInPlace(buffer);

    const double duration = high - low;
    const double grid_dt = duration / static_cast<double>(grid_points);
    if (!isFinite(grid_dt) || !(grid_dt > 0.0)) {
        return fail("analysis.fft.invalid_grid",
                    "El intervalo de la rejilla FFT no es válido.");
    }
    const double df = 1.0 / (static_cast<double>(fft_points) * grid_dt);
    const double nyquist = 0.5 / grid_dt;
    if (!isFinite(df) || !(df > 0.0) || !isFinite(nyquist) ||
        !(nyquist > 0.0)) {
        return fail("analysis.fft.invalid_grid",
                    "No se pudo establecer el eje de frecuencia de la FFT.");
    }

    out.fft_points = fft_points;
    const std::size_t bins = fft_points / 2 + 1;
    out.bin_frequency_hz.resize(bins, 0.0);
    out.rms.assign(bins, 0.0);
    for (std::size_t k = 0; k < bins; ++k) {
        const double frequency = static_cast<double>(k) * df;
        out.bin_frequency_hz[k] = isFinite(frequency) ? frequency : 0.0;
        if (k == 0) {
            out.rms[k] = 0.0;                                          
            continue;
        }
        double value = 0.0;
        const double magnitude = std::abs(buffer[k]);
        if (isFinite(magnitude)) {
            value = 2.0 * magnitude / (window_sum * std::sqrt(2.0));
        }
        out.rms[k] = isFinite(value) ? value : 0.0;
    }

    out.harmonics_rms.assign(static_cast<std::size_t>(order_cap), 0.0);
    const double tolerance = std::max(frequency_hz / 2.0, df);
    int supported = 0;
    for (int order = 1; order <= order_cap; ++order) {
        const double target = frequency_hz * static_cast<double>(order);
        bool valid = false;
        double value = 0.0;
        if (isFinite(target) && target > 0.0 && target < nyquist) {
            const long long bin =
                static_cast<long long>(std::llround(target / df));
            if (bin >= 1 && bin < static_cast<long long>(bins)) {
                const double bin_frequency = static_cast<double>(bin) * df;
                if (bin_frequency < nyquist &&
                    std::fabs(bin_frequency - target) <= tolerance) {
                    valid = true;
                    value = out.rms[static_cast<std::size_t>(bin)];
                }
            }
        }
        if (!valid) {
            break;
        }
        out.harmonics_rms[static_cast<std::size_t>(order - 1)] = value;
        supported = order;
    }
    out.supported_order = supported;

    if (supported >= 2 && out.harmonics_rms[0] > 0.0) {
        double sum_squares = 0.0;
        for (std::size_t i = 1; i < static_cast<std::size_t>(supported); ++i) {
            sum_squares += out.harmonics_rms[i] * out.harmonics_rms[i];
        }
        const double thd =
            100.0 * std::sqrt(sum_squares) / out.harmonics_rms[0];
        if (isFinite(thd)) {
            out.thd_pct = thd;
        }
        const double h2 =
            100.0 * out.harmonics_rms[1] / out.harmonics_rms[0];
        if (isFinite(h2)) {
            out.h2_h1_pct = h2;
        }
    }

    out.diagnostics.push_back(makeDiagnostic(
        DiagnosticSeverity::Info, "analysis.fft.methodology",
        "FFT radix-2 sobre " + std::to_string(grid_points) +
            " muestras interpoladas linealmente en la ventana centrada de " +
            std::to_string(cycles) + " ciclo(s), con media DC eliminada, ventana "
            "Hann y zero-padding a " + std::to_string(fft_points) +
            " puntos. La escala es RMS = 2|X|/(suma(ventana)*sqrt(2)); el eje "
            "usa la duración original de la ventana (" +
            formatValue(duration, "%.6f") + " s, df=" + formatValue(df, "%.6f") +
            " Hz, Nyquist=" + formatValue(nyquist, "%.3f") + " Hz)."));
    if (order_cap > 0 && supported == 0) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.fft.nyquist",
            "La rejilla FFT no permite ubicar la fundamental estrictamente por "
            "debajo de Nyquist (" +
                formatValue(nyquist, "%.3f") + " Hz); no se informan armónicos, "
                "H2/H1 ni THD."));
    } else if (order_cap > 0 && supported < order_cap) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.fft.order_limit",
            "Solo " + std::to_string(supported) +
                " orden(es) armónico(s) tienen un bin válido estrictamente bajo "
                "Nyquist; el resto queda en 0 y no participa de la THD."));
    }
    out.ok = true;
    return out;
}

                                                                              
                                                                                  
                                                                              

SaturationIndicator shoulderSaturation(const SampleData& data,
                                       std::size_t channel,
                                       double frequency_hz,
                                       double center_s,
                                       int cycles,
                                       const HarmonicFit* fit) {
    SaturationIndicator out;
    out.shoulder_required_hits = kSaturationShoulderHits;
    out.curvature_threshold = kSaturationCurvatureRatio;
    out.high_fraction = kSaturationHighFraction;
    out.h2_threshold_pct = kSaturationH2ThresholdPct;
    out.methodology =
        "Indicador heurístico: ventana centrada de N ciclos interpolada "
        "linealmente, media DC eliminada, pico = max|y|, zona alta = |y| > "
        "55 % del pico, curvatura = |Δ²y| y umbral = 4× la mediana de la "
        "curvatura. Se cuentan los puntos altos con curvatura superior al "
        "umbral. Techo plano (evidencia separada): |y| >= 98 % del pico y "
        "|dy/dt| <= 10 % del máximo |dy/dt| durante >= 1/8 del período nominal "
        "(12.5 %), con >= 3 muestras consecutivas, al menos un ciclo y >= 8 "
        "muestras por período; flat_top_pct es el porcentaje de muestras de la "
        "ventana en mesetas que cumplen y longest_flat_top_ms la meseta más "
        "larga. Un pico senoidal aislado no cumple la duración. H2/H1 solo se "
        "toma del ajuste armónico entregado. No confirma saturación de "
        "TC/transformador ni recorte del registrador, ni identifica su causa.";

    auto fail = [&out](std::string code, std::string message) {
        out.diagnostics.push_back(
            makeDiagnostic(DiagnosticSeverity::Warning, std::move(code),
                           std::move(message)));
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.saturation.methodology",
            out.methodology));
        return out;
    };

    if (channel >= data.analog_channels) {
        return fail("analysis.saturation.channel",
                    "El canal solicitado para el indicador de saturación no "
                    "existe en el DAT.");
    }
    if (!isFinite(frequency_hz) || frequency_hz <= 0.0 || cycles < 1 ||
        !isFinite(center_s)) {
        return fail("analysis.saturation.invalid",
                    "Frecuencia, centro o ciclos inválidos; no se evalúa el "
                    "indicador de saturación.");
    }
    const std::size_t total = data.sampleCount();
    if (total == 0 || data.times_s.size() != total) {
        return fail("analysis.saturation.no_data",
                    "No hay timestamps DAT utilizables para el indicador de "
                    "saturación.");
    }
    if (!hasUsableTimes(data)) {
        return fail("analysis.saturation.timestamps",
                    "Los timestamps DAT no son finitos o no son crecientes; no "
                    "se evalúa el indicador de saturación.");
    }
    const double half = static_cast<double>(cycles) / (2.0 * frequency_hz);
    const double low = center_s - half;
    const double high = center_s + half;
    if (!isFinite(low) || !isFinite(high) || low < data.times_s.front() ||
        high > data.times_s.back()) {
        return fail("analysis.saturation.out_of_record",
                    "La ventana del indicador de saturación sale del registro; "
                    "no se extrapolan muestras.");
    }

    std::size_t source_samples = 0;
    double median_dt = 0.0;
    if (!windowSourceStats(data, low, high, source_samples, median_dt)) {
        return fail("analysis.saturation.invalid_rate",
                    "No se pudo determinar una tasa de muestreo positiva en la "
                    "ventana del indicador de saturación.");
    }
    if (source_samples < static_cast<std::size_t>(kSaturationMinSamples)) {
        return fail("analysis.saturation.too_few_samples",
                    "La ventana del indicador de saturación contiene menos de " +
                        std::to_string(kSaturationMinSamples) +
                        " muestras fuente; no se evalúa.");
    }

    std::size_t points = std::max<std::size_t>(
        static_cast<std::size_t>(kSaturationMinSamples), source_samples);
    bool reduced = false;
    if (points > kFftMaxPoints) {
        points = kFftMaxPoints;
        reduced = true;
    }
    std::vector<double> values;
    if (!interpolateUniform(data, channel, low, high, points, values)) {
        return fail("analysis.saturation.interpolation",
                    "No se pudo interpolar el canal en la rejilla del indicador "
                    "de saturación; revise timestamps y valores no finitos.");
    }
    double mean = 0.0;
    for (double value : values) {
        mean += value;
    }
    mean /= static_cast<double>(values.size());
    if (!isFinite(mean)) {
        return fail("analysis.saturation.interpolation",
                    "La media de la ventana del indicador de saturación no es "
                    "finita.");
    }
    for (double& value : values) {
        value -= mean;
    }

    double peak = 0.0;
    for (double value : values) {
        peak = std::max(peak, std::fabs(value));
    }
    out.shoulder_peak = peak;
    if (!isFinite(peak) || !(peak > 1e-12)) {
        return fail("analysis.saturation.flat",
                    "La ventana del indicador de saturación no tiene variación "
                    "AC utilizable (pico ~0); no se evalúan hombros.");
    }

    std::vector<double> curvature(points - 2, 0.0);
    for (std::size_t i = 0; i + 2 < points; ++i) {
        const double first = values[i + 1] - values[i];
        const double second = values[i + 2] - values[i + 1];
        curvature[i] = std::fabs(second - first);
    }
    const double median_curvature = medianOf(curvature);
    if (!isFinite(median_curvature)) {
        return fail("analysis.saturation.curvature",
                    "No se pudo calcular la mediana de curvatura del indicador "
                    "de saturación.");
    }
    const double scale = median_curvature + 1e-12;
    int hits = 0;
    for (std::size_t i = 0; i < curvature.size(); ++i) {
        const bool high_zone =
            std::fabs(values[i + 1]) > out.high_fraction * peak;
        if (high_zone &&
            curvature[i] > out.curvature_threshold * scale) {
            ++hits;
        }
    }
    out.shoulder_hits = hits;
    out.available = true;

                                                                              
                                                                          
                                                              
    std::string flat_top_reason;
    const FlatTopEvidence flat_top =
        evaluateFlatTop(values, high - low, frequency_hz, cycles,
                        flat_top_reason);
    if (flat_top.evaluated) {
        out.flat_top_pct = flat_top.pct;
        out.longest_flat_top_ms = flat_top.longest_ms;
        out.flat_top_elevated = flat_top.elevated;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.saturation.flat_top",
            "Techo plano: " +
                formatValue(*out.flat_top_pct, "%.2f") +
                " % de las muestras en mesetas válidas; meseta más larga " +
                formatValue(out.longest_flat_top_ms, "%.3f") +
                " ms (umbral 12.5 % del período nominal). " +
                (out.flat_top_elevated
                     ? std::string("Señal compatible con recorte del "
                                   "registrador o saturación; es una "
                                   "sospecha, no una prueba.")
                     : std::string("Sin meseta sostenida; un pico senoidal "
                                   "no se marca como recorte."))));
    } else {
        out.flat_top_pct.reset();
        out.longest_flat_top_ms = 0.0;
        out.flat_top_elevated = false;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info,
            "analysis.saturation.flat_top_unavailable",
            "No se evaluó el techo plano: " + flat_top_reason));
    }

    if (fit != nullptr && fit->h2_h1_pct.has_value() &&
        isFinite(*fit->h2_h1_pct)) {
        out.h2_h1_pct = *fit->h2_h1_pct;
        out.h2_elevated = *out.h2_h1_pct >= out.h2_threshold_pct;
    } else {
        out.h2_h1_pct.reset();
        out.h2_elevated = false;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.saturation.h2_not_available",
            "No se recibió un ajuste armónico con H2/H1 para esta ventana; el "
            "indicador solo evalúa hombros."));
    }

    out.possible = out.h2_elevated || hits >= kSaturationShoulderHits ||
                   out.flat_top_elevated;
    out.methodology =
        "Indicador heurístico: ventana centrada de " + std::to_string(cycles) +
        " ciclo(s) e " + std::to_string(points) +
        " puntos interpolados, media DC eliminada, pico = " +
        formatValue(peak, "%.6g") +
        ", zona alta = |y| > 55 % del pico, curvatura = |Δ²y| y umbral = 4× "
        "la mediana de curvatura. Se cuentan los puntos altos con curvatura "
        "superior al umbral. Techo plano: |y| >= 98 % del pico y |dy/dt| <= "
        "10 % del máximo |dy/dt| durante >= 1/8 del período nominal (12.5 %), "
        "con >= 3 muestras consecutivas, al menos un ciclo y >= 8 muestras por "
        "período; flat_top_pct = " +
        (out.flat_top_pct.has_value()
             ? formatValue(*out.flat_top_pct, "%.2f") + " %"
             : std::string("no disponible")) +
        ", longest_flat_top_ms = " +
        formatValue(out.longest_flat_top_ms, "%.3f") +
        " ms. Un pico senoidal aislado no cumple la duración. H2/H1 solo se "
        "toma del ajuste armónico entregado. No confirma saturación de "
        "TC/transformador ni recorte del registrador, ni identifica su causa.";
    out.diagnostics.push_back(makeDiagnostic(
        DiagnosticSeverity::Info, "analysis.saturation.methodology",
        out.methodology));
    if (reduced) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning,
            "analysis.saturation.reduced_resolution",
            "La rejilla del indicador de saturación se recortó a " +
                std::to_string(points) + " puntos para acotar memoria; la "
                "curvatura puede suavizarse y los hombros deben interpretarse "
                "con cautela."));
    }
    if (out.possible) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.saturation.possible",
            "El indicador heurístico marcó posible saturación o recorte del "
            "registrador (H2/H1=" +
                (out.h2_h1_pct.has_value()
                     ? formatValue(*out.h2_h1_pct, "%.2f") + " %"
                     : std::string("no disponible")) +
                ", hombros=" + std::to_string(hits) + ", techo plano=" +
                (out.flat_top_pct.has_value()
                     ? formatValue(*out.flat_top_pct, "%.2f") + " % / " +
                           formatValue(out.longest_flat_top_ms, "%.3f") + " ms"
                     : std::string("no evaluado")) +
                "). Es un indicio; no confirma saturación de TC/transformador "
                "ni recorte, no identifica la causa y puede deberse a "
                "transitorios, ruido o componente DC."));
    }
    return out;
}

                                                                              
                  
                                                                              

bool PhaseSelection::has(char phase) const noexcept {
    const int slot = phaseSlot(phase);
    return slot >= 0 && index[static_cast<std::size_t>(slot)].has_value();
}

bool PhaseSelection::complete() const noexcept {
    return index[0].has_value() && index[1].has_value() && index[2].has_value();
}

std::vector<std::size_t> PhaseSelection::inOrder() const {
    std::vector<std::size_t> out;
    for (const auto& entry : index) {
        if (entry.has_value()) {
            out.push_back(*entry);
        }
    }
    return out;
}

std::vector<std::size_t> PhaseSelection::prefix() const {
    std::vector<std::size_t> out;
    for (const auto& entry : index) {
        if (!entry.has_value()) {
            break;
        }
        out.push_back(*entry);
    }
    return out;
}

PhaseSelection selectPhaseChannels(const CfgInfo& cfg) {
    PhaseSelection selection;
    for (std::size_t channel = 0; channel < cfg.analog.size(); ++channel) {
        const std::vector<int> matches = matchedPhases(cfg.analog[channel].name);
        if (matches.size() > 1) {
            std::string phases;
            for (std::size_t i = 0; i < matches.size(); ++i) {
                if (i > 0) {
                    phases += "/";
                }
                phases += static_cast<char>('A' + matches[i]);
            }
            selection.ambiguous.emplace_back(channel, phases);
        } else if (matches.size() == 1) {
            const std::size_t slot = static_cast<std::size_t>(matches[0]);
            selection.candidates[slot].push_back(channel);
        }
    }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (!selection.candidates[slot].empty()) {
            selection.index[slot] = selection.candidates[slot].front();
        }
    }
    return selection;
}

std::vector<Diagnostic> phaseChannelDiagnostics(
    const CfgInfo& cfg,
    const PhaseSelection& selection,
    bool include_missing) {
    std::vector<Diagnostic> out;
    if (!selection.ambiguous.empty()) {
        std::string detail;
        const std::size_t limit = std::min<std::size_t>(selection.ambiguous.size(), 6);
        for (std::size_t i = 0; i < limit; ++i) {
            if (i > 0) {
                detail += ", ";
            }
            const auto& entry = selection.ambiguous[i];
            detail += entry.first < cfg.analog.size()
                          ? cfg.analog[entry.first].name
                          : std::to_string(entry.first + 1);
            detail += " (" + entry.second + ")";
        }
        if (selection.ambiguous.size() > limit) {
            detail += " y " +
                      std::to_string(selection.ambiguous.size() - limit) +
                      " canal(es) más";
        }
        out.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.phase_ambiguous",
            "Se excluyeron de la asignación automática canales cuyo nombre "
            "coincide con más de una fase (" +
                detail +
                "); usar el mismo canal para dos fases invalidaría las "
                "componentes simétricas. Selecciona o renombra los canales antes "
                "de interpretar secuencias."));
    }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& candidates = selection.candidates[slot];
        if (candidates.size() > 1) {
            std::vector<std::string> names;
            for (std::size_t i = 0; i < std::min<std::size_t>(candidates.size(), 5);
                 ++i) {
                const std::size_t index = candidates[i];
                names.push_back(index < cfg.analog.size()
                                    ? cfg.analog[index].name
                                    : std::to_string(index + 1));
            }
            std::string list;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i > 0) {
                    list += ", ";
                }
                list += names[i];
            }
            if (candidates.size() > 5) {
                list += " y " + std::to_string(candidates.size() - 5) + " más";
            }
            const std::size_t chosen = candidates.front();
            out.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "cfg.phase_multiple",
                std::string("Hay varios canales candidatos para la fase ") +
                    static_cast<char>('A' + slot) + " (" + list +
                    "); la asignación automática usa " +
                    (chosen < cfg.analog.size() ? cfg.analog[chosen].name
                                                : std::to_string(chosen + 1)) +
                    ". Confirma que sea el canal correcto."));
        }
    }
    if (include_missing) {
        std::string missing;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            if (!selection.index[slot].has_value()) {
                if (!missing.empty()) {
                    missing += ", ";
                }
                missing += static_cast<char>('A' + slot);
            }
        }
        if (!missing.empty()) {
            out.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "cfg.phase_missing",
                "La asignación automática no encontró un canal único para la(s) "
                "fase(s) " +
                    missing +
                    "; no se calcularán componentes simétricas trifásicas con esa "
                    "selección."));
        }
    }
    return out;
}

std::vector<Diagnostic> phaseMeasurementDiagnostics(
    const CfgInfo& cfg,
    const PhaseSelection& selection) {
    std::vector<Diagnostic> out;
    std::vector<std::string> units;
    std::vector<std::string> phases;
    std::vector<const AnalogChannel*> selected;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (!selection.index[slot].has_value()) {
            continue;
        }
        const std::size_t index = *selection.index[slot];
        if (index >= cfg.analog.size()) {
            continue;
        }
        selected.push_back(&cfg.analog[index]);
        phases.push_back(std::string(1, static_cast<char>('A' + slot)));
        units.push_back(normalizeUnit(cfg.analog[index].unit));
    }
    if (selected.size() < 2) {
        return out;
    }

    std::vector<std::string> unique_units;
    for (const std::string& unit : units) {
        if (std::find(unique_units.begin(), unique_units.end(), unit) ==
            unique_units.end()) {
            unique_units.push_back(unit);
        }
    }
    if (unique_units.size() > 1) {
        std::string detail;
        for (std::size_t i = 0; i < selected.size(); ++i) {
            if (i > 0) {
                detail += ", ";
            }
            detail += phases[i] + "=" +
                      (selected[i]->unit.empty() ? "sin unidad"
                                                 : selected[i]->unit);
        }
        out.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.phase_unit_mismatch",
            "Las fases usadas declaran unidades CFG distintas (" + detail +
                "); los cocientes de secuencia pueden mezclar escalas. Verifique "
                "la configuración antes de interpretar magnitudes."));
    }

    std::vector<std::string> ps_values;
    for (const AnalogChannel* channel : selected) {
        if (std::find(ps_values.begin(), ps_values.end(), channel->ps) ==
            ps_values.end()) {
            ps_values.push_back(channel->ps);
        }
    }
    if (ps_values.size() > 1) {
        out.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.phase_ps_mismatch",
            "Las fases usadas tienen marcas P/S distintas; P/S se conserva como "
            "metadato y no se aplica automáticamente, pero conviene confirmar que "
            "las tres corrientes están en la misma base."));
    }
    return out;
}

                                                                              
                                                                             
                                                                              

PhaseSelection selectVoltageChannels(const CfgInfo& cfg) {
    PhaseSelection selection;
    for (std::size_t channel = 0; channel < cfg.analog.size(); ++channel) {
        const std::vector<int> matches =
            matchedVoltagePhases(cfg.analog[channel].name);
        if (matches.size() > 1) {
            std::string phases;
            for (std::size_t i = 0; i < matches.size(); ++i) {
                if (i > 0) {
                    phases += "/";
                }
                phases += static_cast<char>('A' + matches[i]);
            }
            selection.ambiguous.emplace_back(channel, phases);
        } else if (matches.size() == 1) {
            const std::size_t slot = static_cast<std::size_t>(matches[0]);
            selection.candidates[slot].push_back(channel);
        }
    }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (!selection.candidates[slot].empty()) {
            selection.index[slot] = selection.candidates[slot].front();
        }
    }
    return selection;
}

std::vector<Diagnostic> voltageChannelDiagnostics(
    const CfgInfo& cfg,
    const PhaseSelection& selection,
    bool include_missing) {
    std::vector<Diagnostic> out;
    if (!selection.ambiguous.empty()) {
        std::string detail;
        const std::size_t limit =
            std::min<std::size_t>(selection.ambiguous.size(), 6);
        for (std::size_t i = 0; i < limit; ++i) {
            if (i > 0) {
                detail += ", ";
            }
            const auto& entry = selection.ambiguous[i];
            detail += entry.first < cfg.analog.size()
                          ? cfg.analog[entry.first].name
                          : std::to_string(entry.first + 1);
            detail += " (" + entry.second + ")";
        }
        if (selection.ambiguous.size() > limit) {
            detail += " y " +
                      std::to_string(selection.ambiguous.size() - limit) +
                      " canal(es) más";
        }
        out.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.voltage_ambiguous",
            "Se excluyeron de la asignación automática de tensión canales cuyo "
            "nombre coincide con más de una fase (" +
                detail +
                "); usar el mismo canal para dos fases invalidaría las "
                "componentes simétricas de tensión. Selecciona o renombra los "
                "canales antes de interpretar secuencias."));
    }
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto& candidates = selection.candidates[slot];
        if (candidates.size() > 1) {
            std::vector<std::string> names;
            for (std::size_t i = 0;
                 i < std::min<std::size_t>(candidates.size(), 5); ++i) {
                const std::size_t index = candidates[i];
                names.push_back(index < cfg.analog.size()
                                    ? cfg.analog[index].name
                                    : std::to_string(index + 1));
            }
            std::string list;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i > 0) {
                    list += ", ";
                }
                list += names[i];
            }
            if (candidates.size() > 5) {
                list += " y " + std::to_string(candidates.size() - 5) + " más";
            }
            const std::size_t chosen = candidates.front();
            out.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "cfg.voltage_multiple",
                std::string("Hay varios canales candidatos de tensión para la "
                            "fase ") +
                    static_cast<char>('A' + slot) + " (" + list +
                    "); la asignación automática usa " +
                    (chosen < cfg.analog.size() ? cfg.analog[chosen].name
                                                : std::to_string(chosen + 1)) +
                    ". Confirma que sea el canal correcto."));
        }
    }
    if (include_missing) {
        std::string missing;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            if (!selection.index[slot].has_value()) {
                if (!missing.empty()) {
                    missing += ", ";
                }
                missing += static_cast<char>('A' + slot);
            }
        }
        if (!missing.empty()) {
            out.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "cfg.voltage_missing",
                "La asignación automática de tensión no encontró un canal único "
                "para la(s) fase(s) " +
                    missing +
                    "; no se calcularán fasores ni secuencias trifásicas de "
                    "tensión con esa selección. El análisis de corriente no se "
                    "ve afectado."));
        }
    }
    return out;
}

std::vector<Diagnostic> voltageMeasurementDiagnostics(
    const CfgInfo& cfg,
    const PhaseSelection& selection) {
    std::vector<Diagnostic> out;
    std::vector<std::string> units;
    std::vector<std::string> phases;
    std::vector<const AnalogChannel*> selected;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (!selection.index[slot].has_value()) {
            continue;
        }
        const std::size_t index = *selection.index[slot];
        if (index >= cfg.analog.size()) {
            continue;
        }
        selected.push_back(&cfg.analog[index]);
        phases.push_back(std::string(1, static_cast<char>('A' + slot)));
        units.push_back(normalizeUnit(cfg.analog[index].unit));
    }
    if (selected.size() < 2) {
        return out;
    }

    std::vector<std::string> unique_units;
    for (const std::string& unit : units) {
        if (std::find(unique_units.begin(), unique_units.end(), unit) ==
            unique_units.end()) {
            unique_units.push_back(unit);
        }
    }
    if (unique_units.size() > 1) {
        std::string detail;
        for (std::size_t i = 0; i < selected.size(); ++i) {
            if (i > 0) {
                detail += ", ";
            }
            detail += phases[i] + "=" +
                      (selected[i]->unit.empty() ? "sin unidad"
                                                 : selected[i]->unit);
        }
        out.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.voltage_unit_mismatch",
            "Las fases de tensión declaran unidades CFG distintas (" + detail +
                "); los cocientes de secuencia pueden mezclar escalas. Se "
                "conserva la unidad y escala del CFG y no se aplica conversión "
                "P/S."));
    }

    std::vector<std::string> ps_values;
    for (const AnalogChannel* channel : selected) {
        if (std::find(ps_values.begin(), ps_values.end(), channel->ps) ==
            ps_values.end()) {
            ps_values.push_back(channel->ps);
        }
    }
    if (ps_values.size() > 1) {
        out.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.voltage_ps_mismatch",
            "Las fases de tensión tienen marcas P/S distintas; P/S se conserva "
            "como metadato y no se aplica automáticamente, pero conviene "
            "confirmar que las tres tensiones están en la misma base."));
    }
    return out;
}

                                                                              
                  
                                                                              

std::optional<double> unitFactorToAmps(const std::string& unit) {
    std::string key;
    for (char c : unit) {
        if (!isSpaceChar(c)) {
            key += lowerAscii(c);
        }
    }
    if (key == "a") {
        return 1.0;
    }
    if (key == "ka") {
        return 1000.0;
    }
    if (key == "ma") {
        return 0.001;
    }
    return std::nullopt;
}

std::string labelUnit(const std::string& channel_name) {
    std::string last;
    std::size_t index = 0;
    while (index < channel_name.size()) {
        const std::size_t open = channel_name.find('(', index);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t close = channel_name.find(')', open + 1);
        if (close == std::string::npos) {
            break;
        }
        last = trimCopy(channel_name.substr(open + 1, close - open - 1));
        index = close + 1;
    }
    return last;
}

std::string normalizeUnit(const std::string& unit) {
    std::string token;
    for (char c : unit) {
        if (!isSpaceChar(c)) {
            token += lowerAscii(c);
        }
    }
    if (token == "kkv") {
        return "kv";
    }
    if (token == "mmw") {
        return "mw";
    }
    if (token == "mmvar") {
        return "mvar";
    }
    if (token == "mmva") {
        return "mva";
    }
    return token;
}

std::optional<std::string> unitFamily(const std::string& unit) {
    const std::string token = normalizeUnit(unit);
    if (token == "a" || token == "ka" || token == "ma") {
        return "corriente";
    }
    if (token == "v" || token == "kv" || token == "mv") {
        return "tensión";
    }
    if (token == "w" || token == "kw" || token == "mw") {
        return "potencia activa";
    }
    if (token == "var" || token == "kvar" || token == "mvar") {
        return "potencia reactiva";
    }
    if (token == "va" || token == "kva" || token == "mva") {
        return "potencia aparente";
    }
    if (token == "hz") {
        return "frecuencia";
    }
    if (token == "%") {
        return "porcentaje";
    }
    return std::nullopt;
}

std::vector<Diagnostic> unitDiagnostics(const CfgInfo& cfg) {
    struct Group {
        std::string label;
        std::string declared;
        bool same_family = false;
        std::vector<std::string> names;
    };
    std::vector<Group> groups;
    for (const AnalogChannel& channel : cfg.analog) {
        const std::string label = labelUnit(channel.name);
        const std::string declared = trimCopy(channel.unit);
        if (label.empty() || declared.empty()) {
            continue;
        }
        if (normalizeUnit(label) == normalizeUnit(declared)) {
            continue;
        }
        const auto label_family = unitFamily(label);
        const bool same_family =
            label_family.has_value() && label_family == unitFamily(declared);
        auto found = std::find_if(groups.begin(), groups.end(),
                                  [&](const Group& group) {
                                      return group.label == label &&
                                             group.declared == declared &&
                                             group.same_family == same_family;
                                  });
        if (found == groups.end()) {
            groups.push_back(Group{label, declared, same_family, {}});
            groups.back().names.push_back(channel.name);
        } else {
            found->names.push_back(channel.name);
        }
    }

    std::vector<Diagnostic> out;
    for (const Group& group : groups) {
        std::string names;
        for (std::size_t i = 0; i < group.names.size(); ++i) {
            if (i > 0) {
                names += ", ";
            }
            names += group.names[i];
        }
        const std::string prefix =
            group.same_family ? "Discrepancia de prefijo en "
                              : "Discrepancia de unidades en ";
        out.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "cfg.unit_label_mismatch",
            prefix + std::to_string(group.names.size()) + " canal(es) [" +
                names + "]: etiqueta '" + group.label +
                "' frente a unidad CFG '" + group.declared +
                "'; se conserva la unidad y escala del CFG."));
    }
    return out;
}

std::vector<Diagnostic> skewDiagnostics(const CfgInfo& cfg) {
    std::vector<const AnalogChannel*> skewed;
    for (const AnalogChannel& channel : cfg.analog) {
        if (std::fabs(channel.skew_us) > 0.0) {
            skewed.push_back(&channel);
        }
    }
    std::vector<Diagnostic> out;
    if (skewed.empty()) {
        return out;
    }
    std::string details;
    const std::size_t limit = std::min<std::size_t>(skewed.size(), 8);
    for (std::size_t i = 0; i < limit; ++i) {
        if (i > 0) {
            details += ", ";
        }
        details += skewed[i]->name + "=" + formatValue(skewed[i]->skew_us, "%g") +
                   " µs";
    }
    if (skewed.size() > limit) {
        details += " y " + std::to_string(skewed.size() - limit) + " canal(es) más";
    }
    out.push_back(makeDiagnostic(
        DiagnosticSeverity::Warning, "cfg.channel_skew",
        "El CFG declara skew de canal distinto de cero (" + details +
            "); esta herramienta no aplica corrección temporal por canal. Los "
            "fasores y superposiciones pueden quedar desalineados si el skew es "
            "relevante."));
    return out;
}

                                                                              
                     
                                                                              

std::optional<double> parseFaultDistanceKm(const std::string& text) {
    const std::size_t size = text.size();
    std::size_t index = 0;
    while (index < size) {
        const std::size_t start = index;
        std::size_t cursor = index;
        if (text[cursor] == '+' || text[cursor] == '-') {
            ++cursor;
        }
        const std::size_t digits = cursor;
        while (cursor < size && isDigitChar(text[cursor])) {
            ++cursor;
        }
        if (cursor == digits) {
            ++index;
            continue;
        }
        if (cursor < size && (text[cursor] == '.' || text[cursor] == ',')) {
            std::size_t fraction = cursor + 1;
            const std::size_t fraction_start = fraction;
            while (fraction < size && isDigitChar(text[fraction])) {
                ++fraction;
            }
            if (fraction > fraction_start) {
                cursor = fraction;
            }
        }

        std::size_t unit_start = cursor;
        while (unit_start < size && isSpaceChar(text[unit_start])) {
            ++unit_start;
        }
        std::size_t unit_end = unit_start;
        while (unit_end < size && isAlphaChar(text[unit_end])) {
            ++unit_end;
        }
        const std::string token =
            lowerCopy(text.substr(unit_start, unit_end - unit_start));
        const bool boundary =
            unit_end >= size ||
            !(isAsciiAlnum(text[unit_end]) || text[unit_end] == '_');
        if (boundary && isDistanceUnitToken(token)) {
            std::string number = text.substr(start, cursor - start);
            std::replace(number.begin(), number.end(), ',', '.');
            char* end_pointer = nullptr;
            const double value = std::strtod(number.c_str(), &end_pointer);
            if (end_pointer != nullptr && *end_pointer == '\0' && isFinite(value) &&
                value >= 0.0) {
                if (token == "km" || token.rfind("kilo", 0) == 0) {
                    return value;
                }
                return value / 1000.0;
            }
        }
        index = (cursor > index) ? cursor : index + 1;
    }
    return std::nullopt;
}

FaultDistanceResult interpretFaultDistance(const std::string& text,
                                           double reference_length_km) {
    FaultDistanceResult out;
    out.reference_length_km = reference_length_km;
    const auto parsed = parseFaultDistanceKm(text);
    if (!parsed.has_value()) {
        out.status = FaultDistanceStatus::NotInterpretable;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.distance.unparsable",
            "No se pudo interpretar una distancia con unidad explícita (km o m) "
            "en el texto «" +
                text + "»; no se usa para ubicar la falla."));
        return out;
    }
    out.distance_km = *parsed;

    if (!isFinite(reference_length_km) || reference_length_km <= 0.0) {
        out.status = FaultDistanceStatus::NoReferenceLength;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.distance.no_reference",
            "El localizador reporta " + formatValue(*parsed, "%.2f") +
                " km, pero no se ingresó una longitud de línea verificable; no se "
                "compara la distancia ni se infiere una longitud."));
        return out;
    }

    out.ratio = *parsed / reference_length_km;
    if (*parsed > reference_length_km * 1.25) {
        out.status = FaultDistanceStatus::ExceedsReference;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.distance.exceeds",
            "El localizador reporta " + formatValue(*parsed, "%.2f") +
                " km; supera la longitud de referencia de " +
                formatValue(reference_length_km, "%.2f") + " km (relación " +
                formatValue(*out.ratio, "%.1f") +
                "×, margen 25 %). Revisar circuito, unidades, parámetros del "
                "localizador y asociación de la bitácora antes de usar esa "
                "distancia."));
    } else {
        out.status = FaultDistanceStatus::WithinReference;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.distance.within",
            "La distancia reportada (" + formatValue(*parsed, "%.2f") +
                " km) queda dentro de la longitud de referencia (" +
                formatValue(reference_length_km, "%.2f") + " km; relación " +
                formatValue(*out.ratio, "%.2f") +
                "). Estar dentro no demuestra calibración del localizador ni una "
                "ubicación correcta."));
    }
    return out;
}

                                                                              
                         
                                                                              

std::vector<DigitalTransition> digitalTransitions(const ComtradeRecord& record,
                                                 double reference_s,
                                                 double half_window_s) {
    std::vector<DigitalTransition> out;
    const SampleData& data = record.data;
    const std::size_t count = data.sampleCount();
    if (data.digital_channels == 0 || count == 0 ||
        data.times_s.size() != count || data.digital.size() <
                                             count * data.digital_channels) {
        return out;
    }
    for (std::size_t channel = 0; channel < data.digital_channels; ++channel) {
        std::uint8_t previous =
            data.digital[channel];                            
        for (std::size_t sample = 1; sample < count; ++sample) {
            const std::uint8_t current =
                data.digital[sample * data.digital_channels + channel];
            if (current == previous) {
                continue;
            }
            const double relative = data.times_s[sample] - reference_s;
            if (std::fabs(relative) <= half_window_s) {
                DigitalTransition transition;
                transition.channel = channel;
                transition.name = channel < record.cfg.digital.size()
                                      ? record.cfg.digital[channel].name
                                      : std::string();
                transition.time_s = data.times_s[sample];
                transition.relative_to_reference_s = relative;
                transition.state = current;
                out.push_back(transition);
            }
            previous = current;
        }
    }
    return out;
}

                                                                              
                     
                                                                              

bool RecordAnalysis::hasError() const noexcept {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

RecordAnalysis analyzeRecord(const ComtradeRecord& record,
                             const AnalysisOptions& options) {
    RecordAnalysis out;
    out.cycles = options.cycles;
    out.sample_count = record.data.sampleCount();
    out.analog_channels = record.cfg.analogCount();

    out.diagnostics.push_back(makeDiagnostic(
        DiagnosticSeverity::Info, "analysis.assumptions",
        "Magnitudes en unidades de ingeniería del CFG (sin conversión P/S); "
        "tiempos DAT relativos al inicio configurado sin rebase; el skew de canal "
        "no se corrige; la distancia de falla solo se contrasta con una longitud "
        "ingresada por el usuario."));

    if (out.analog_channels == 0) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error, "analysis.no_analog_channels",
            "El CFG no declara canales analógicos; no hay señales para analizar."));
        return out;
    }
    if (options.cycles < 1) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error, "analysis.invalid_cycles",
            "El número de ciclos debe ser al menos 1."));
        return out;
    }

    double frequency = options.frequency_hz;
    if (!(frequency > 0.0)) {
        frequency = record.cfg.nominal_frequency_hz;
    }
    out.frequency_hz = frequency;
    if (!isFinite(frequency) || frequency <= 0.0) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Error, "analysis.invalid_frequency",
            "El registro no tiene una frecuencia nominal válida; no se pueden "
            "calcular fasores ni ventanas."));
        return out;
    }

    out.time = alignTimestamps(record);
    out.diagnostics.insert(out.diagnostics.end(), out.time.diagnostics.begin(),
                           out.time.diagnostics.end());

    out.phases = selectPhaseChannels(record.cfg);

                                                                                 
                                                             
    bool manual_phases = false;
    for (const auto& index : options.phase_indices) {
        if (index.has_value()) {
            manual_phases = true;
            break;
        }
    }
    if (manual_phases) {
        std::array<std::optional<std::size_t>, 3> selected{};
        bool duplicate = false;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            if (!options.phase_indices[slot].has_value()) {
                continue;
            }
            const std::size_t index = *options.phase_indices[slot];
            if (index >= record.cfg.analog.size()) {
                out.diagnostics.push_back(makeDiagnostic(
                    DiagnosticSeverity::Error, "analysis.phase_override_range",
                    "La selección manual de fase apunta a un canal analógico "
                    "inexistente."));
                return out;
            }
            for (std::size_t previous = 0; previous < slot; ++previous) {
                if (selected[previous].has_value() &&
                    *selected[previous] == index) {
                    duplicate = true;
                }
            }
            selected[slot] = index;
        }
        if (duplicate) {
            out.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Error, "analysis.phase_override_duplicate",
                "La selección manual reutiliza el mismo canal en dos fases; no "
                "se puede calcular secuencias sin ambigüedad."));
            return out;
        }
        out.phases.index = selected;
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.phase_override",
            "Se usó la selección manual de canales A/B/C; los valores numéricos "
            "conservan la unidad y escala del CFG."));
    }

    auto append = [&out](std::vector<Diagnostic> items) {
        out.diagnostics.insert(out.diagnostics.end(),
                               std::make_move_iterator(items.begin()),
                               std::make_move_iterator(items.end()));
    };
    append(phaseChannelDiagnostics(record.cfg, out.phases));
    append(phaseMeasurementDiagnostics(record.cfg, out.phases));
    append(unitDiagnostics(record.cfg));
    append(skewDiagnostics(record.cfg));

                                                                                 
                                                                                   
    out.voltage.selection = selectVoltageChannels(record.cfg);
    bool manual_voltage = false;
    for (const auto& index : options.voltage_indices) {
        if (index.has_value()) {
            manual_voltage = true;
            break;
        }
    }
    bool voltage_override_valid = false;
    if (manual_voltage) {
        std::array<std::optional<std::size_t>, 3> selected{};
        bool duplicate = false;
        bool range_error = false;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            if (!options.voltage_indices[slot].has_value()) {
                continue;
            }
            const std::size_t index = *options.voltage_indices[slot];
            if (index >= record.cfg.analog.size()) {
                range_error = true;
                break;
            }
            for (std::size_t previous = 0; previous < slot; ++previous) {
                if (selected[previous].has_value() &&
                    *selected[previous] == index) {
                    duplicate = true;
                }
            }
            selected[slot] = index;
        }
        if (range_error) {
            out.voltage.selection = PhaseSelection{};
            out.voltage.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "analysis.voltage_override_range",
                "La selección manual de tensión apunta a un canal analógico "
                "inexistente; se desactiva el análisis de tensión y se conserva "
                "el análisis de corriente."));
        } else if (duplicate) {
            out.voltage.selection = PhaseSelection{};
            out.voltage.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning,
                "analysis.voltage_override_duplicate",
                "La selección manual de tensión reutiliza el mismo canal en dos "
                "fases; se desactiva el análisis de tensión y se conserva el "
                "análisis de corriente."));
        } else {
            out.voltage.selection = PhaseSelection{};
            out.voltage.selection.index = selected;
            voltage_override_valid = true;
            Diagnostic override_diagnostic = makeDiagnostic(
                DiagnosticSeverity::Info, "analysis.voltage_override",
                "Se usó la selección manual de canales de tensión A/B/C; los "
                "valores numéricos conservan la unidad y escala del CFG.");
            out.voltage.diagnostics.push_back(override_diagnostic);
            out.diagnostics.push_back(std::move(override_diagnostic));
        }
    }
    if (!manual_voltage) {
        std::vector<Diagnostic> channel_diagnostics =
            voltageChannelDiagnostics(record.cfg, out.voltage.selection, true);
        std::vector<Diagnostic> measurement_diagnostics =
            voltageMeasurementDiagnostics(record.cfg, out.voltage.selection);
        out.voltage.diagnostics.insert(
            out.voltage.diagnostics.end(),
            std::make_move_iterator(channel_diagnostics.begin()),
            std::make_move_iterator(channel_diagnostics.end()));
        out.voltage.diagnostics.insert(
            out.voltage.diagnostics.end(),
            std::make_move_iterator(measurement_diagnostics.begin()),
            std::make_move_iterator(measurement_diagnostics.end()));
    } else if (voltage_override_valid) {
        std::vector<Diagnostic> measurement_diagnostics =
            voltageMeasurementDiagnostics(record.cfg, out.voltage.selection);
        out.voltage.diagnostics.insert(
            out.voltage.diagnostics.end(),
            std::make_move_iterator(measurement_diagnostics.begin()),
            std::make_move_iterator(measurement_diagnostics.end()));
    }

    Diagnostic voltage_assumptions = makeDiagnostic(
        DiagnosticSeverity::Info, "analysis.voltage.assumptions",
        "Tensión: fasores RMS en unidad CFG, mismas ventanas y ciclos que la "
        "corriente, sin conversión P/S ni relación de tensión. `unit` solo se "
        "informa si las tres fases comparten unidad normalizada; las secuencias "
        "requieren las tres fases y una selección parcial queda como tensión no "
        "disponible para secuencias.");
    out.voltage.diagnostics.push_back(voltage_assumptions);
    out.diagnostics.push_back(std::move(voltage_assumptions));

    std::size_t voltage_flushed = 0;
    auto flushVoltageDiagnostics = [&out, &voltage_flushed]() {
        for (; voltage_flushed < out.voltage.diagnostics.size();
             ++voltage_flushed) {
            if (out.voltage.diagnostics[voltage_flushed].severity !=
                DiagnosticSeverity::Info) {
                out.diagnostics.push_back(
                    out.voltage.diagnostics[voltage_flushed]);
            }
        }
    };
    flushVoltageDiagnostics();

    if (!out.time.ok) {
        return out;
    }

    const std::vector<std::size_t> indices = out.phases.inOrder();
    const double period = 1.0 / frequency;
    bool current_units_compatible = indices.size() == 3;
    std::optional<std::string> current_unit;
    for (const auto channel : indices) {
        if (channel >= record.cfg.analog.size()) { current_units_compatible = false; break; }
        const std::string unit = normalizeUnit(record.cfg.analog[channel].unit);
        const auto family = unitFamily(unit);
        if (!family || *family != "corriente" || (current_unit && *current_unit != unit))
            current_units_compatible = false;
        current_unit = unit;
    }
    if (!current_units_compatible && indices.size() == 3) {
        out.diagnostics.push_back(makeDiagnostic(DiagnosticSeverity::Warning,
            "analysis.sequence.units_incompatible",
            "Las secuencias de corriente requieren A/B/C con la misma unidad CFG de corriente; no se combinan A, kA, tensiones o porcentajes sin conversión explícita. Se conservan los fasores individuales."));
    }

    const double onset_ratio =
        std::isfinite(options.onset_ratio) && options.onset_ratio > 1.0
            ? options.onset_ratio
            : 1.5;
    out.event = detectOnset(record.data, indices, frequency,
                            out.time.reference_s, onset_ratio);
    out.regime_changes = findRegimeChanges(record, indices,
        out.voltage.selection.inOrder(), period);
    if (out.event.has_value() && out.event->recovery_censored) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.event.recovery_censored",
            "No se confirmó una recuperación candidata dentro de la captura. La "
            "duración reportada es una cota inferior (censurada)."));
    }

    const std::optional<double> onset =
        out.event.has_value() ? std::optional<double>(out.event->time_s)
                              : std::nullopt;

    const WindowCenters centers =
        defaultWindowCenters(onset, out.time.first_s, out.time.last_s, frequency,
                             out.cycles, out.time.reference_s);
    out.window_half_s = static_cast<double>(out.cycles) * period / 2.0;
    const double requested_prefalla =
        options.prefalla_center_s.value_or(centers.prefalla_s);
    const double requested_falla =
        options.falla_center_s.value_or(centers.falla_s);
    if (options.prefalla_center_s.has_value() ||
        options.falla_center_s.has_value()) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.window.override",
            "Se usaron centros de ventana elegidos por el usuario; si quedan "
            "fuera del registro se limitan al borde y se informa."));
    }
    const ClampedCenter prefalla =
        clampCenter(requested_prefalla, out.time.first_s, out.time.last_s,
                    out.window_half_s);
    const ClampedCenter falla =
        clampCenter(requested_falla, out.time.first_s, out.time.last_s,
                    out.window_half_s);
    out.prefalla_center_s = prefalla.center_s;
    out.falla_center_s = falla.center_s;
    out.prefalla_clamped = prefalla.clamped;
    out.falla_clamped = falla.clamped;
    out.windows_overlap = out.prefalla_center_s + out.window_half_s >
                          out.falla_center_s - out.window_half_s + 1e-12;
    if (out.windows_overlap) {
        out.diagnostics.push_back(makeDiagnostic(
            DiagnosticSeverity::Warning, "analysis.window.overlap",
            "Las ventanas prefalla y falla se solapan; no representan regímenes "
            "independientes. No se emite clasificación por secuencias para este "
            "registro."));
    }

    out.prefalla_manual = options.prefalla_center_s.has_value();
    const double reference_extent = std::max(period, out.window_half_s);
    std::vector<double> initial_levels, pre_levels;
    const bool initial_stable = stableReference(record.data, indices,
        out.time.first_s + 1.5 * period, period, &initial_levels);
    const bool pre_stable = stableReference(record.data, indices,
        out.prefalla_center_s, period, &pre_levels);
    bool same_level = initial_levels.size() == 3 && pre_levels.size() == 3;
    if (same_level) {
        for (std::size_t phase = 0; phase < 3; ++phase) {
            if (std::fabs(initial_levels[phase] - pre_levels[phase]) >
                0.20 * std::max(initial_levels[phase], 1e-9)) same_level = false;
        }
    }
    const bool before_change = out.event &&
        out.prefalla_center_s + reference_extent <= out.event->time_s + 1e-9;
    out.prefalla_usable = current_units_compatible && initial_stable && pre_stable && same_level &&
        before_change && !out.prefalla_clamped && !out.windows_overlap &&
        out.event->direction == ChangeDirection::Rise;
    if (out.prefalla_usable) {
        out.prefalla_status = out.prefalla_manual ?
            "Referencia manual estable anterior al ascenso; no acredita estado sano." :
            "Referencia automática candidata: estable por fase y anterior al ascenso; no acredita estado sano.";
    } else {
        out.prefalla_status =
            "Prefalla no identificable con suficiente evidencia. Ventanas de comparación provisionales; sin clasificación automática.";
    }
    out.diagnostics.push_back(makeDiagnostic(
        out.prefalla_usable ? DiagnosticSeverity::Info : DiagnosticSeverity::Warning,
        out.prefalla_usable ? "analysis.reference.candidate" : "analysis.reference.unverified",
        out.prefalla_status));
    if (!out.regime_changes.empty()) {
        out.diagnostics.push_back(makeDiagnostic(DiagnosticSeverity::Info,
            "analysis.event.stages", std::to_string(out.regime_changes.size()) +
            " cambio(s) de régimen candidato(s) en la captura completa. Resolución de un ciclo; consulte etapas en HTML/JSON. Armónicos y THD corresponden solo a la ventana seleccionada."));
        for (const auto& change : out.regime_changes) {
            if (std::fabs(change.time_s - out.falla_center_s) <= out.window_half_s + period) {
                out.diagnostics.push_back(makeDiagnostic(DiagnosticSeverity::Warning,
                    "analysis.harmonics.transition_nearby",
                    "La ventana de armónicos está próxima a un cambio de régimen (resolución de un ciclo). THD puede describir un transitorio; no extrapolar a todo el evento."));
                break;
            }
        }
    }
    if (!out.event) {
        out.diagnostics.push_back(makeDiagnostic(DiagnosticSeverity::Warning,
            "analysis.event.unidentified",
            "Inicio no identificable: ausencia de detección no significa ausencia de falla; puede comenzar antes de la captura o faltar una referencia estable."));
    }

    if (!indices.empty()) {
        std::vector<std::string> channel_names;
        channel_names.reserve(record.cfg.analog.size());
        for (const AnalogChannel& channel : record.cfg.analog) {
            channel_names.push_back(channel.name);
        }
        const std::vector<WindowSpec> windows = {
            {"Prefalla", out.prefalla_center_s},
            {"Falla", out.falla_center_s},
        };
        WindowCheckResult checks = checkPhasorWindows(
            record.data, indices, frequency, windows, out.cycles, channel_names);
        out.coherence_rows = std::move(checks.rows);
        out.coherence_errors = std::move(checks.errors);
        for (const Diagnostic& error : out.coherence_errors) {
                                                                                
                                          
            if (containsText(error.message, "se solapan")) {
                continue;
            }
            Diagnostic prefixed = error;
            prefixed.message = "Coherencia traza ↔ fasor: " + error.message;
            out.diagnostics.push_back(std::move(prefixed));
        }
    }

    if (out.phases.complete()) {
        const std::vector<std::size_t> abc = {
            *out.phases.index[0], *out.phases.index[1], *out.phases.index[2]};
        const auto pre_phasors = windowPhasors(
            record.data, abc, frequency, out.prefalla_center_s, out.cycles);
        const auto fault_phasors = windowPhasors(
            record.data, abc, frequency, out.falla_center_s, out.cycles);

        if (pre_phasors.has_value() && pre_phasors->size() == 3) {
            out.prefalla_phasors = std::array<Phasor, 3>{
                Phasor{(*pre_phasors)[0]}, Phasor{(*pre_phasors)[1]},
                Phasor{(*pre_phasors)[2]}};
            out.prefalla_sequences = symmetricalComponents(*pre_phasors);
        }
        if (fault_phasors.has_value() && fault_phasors->size() == 3) {
            out.falla_phasors = std::array<Phasor, 3>{
                Phasor{(*fault_phasors)[0]}, Phasor{(*fault_phasors)[1]},
                Phasor{(*fault_phasors)[2]}};
            out.falla_sequences = symmetricalComponents(*fault_phasors);
        }
        if (!pre_phasors.has_value() || !fault_phasors.has_value()) {
            std::string missing;
            if (!pre_phasors.has_value()) {
                missing = "prefalla";
            }
            if (!fault_phasors.has_value()) {
                if (!missing.empty()) {
                    missing += " y ";
                }
                missing += "falla";
            }
            out.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning, "analysis.phasor.unavailable",
                "No se pudieron calcular los fasores de la ventana de " + missing +
                    " (fuera del registro, sin muestras suficientes o ajuste no "
                    "resoluble); no se reportan cocientes ni clasificación por "
                    "secuencias para este registro."));
        }

        if (!current_units_compatible) {
            out.prefalla_sequences.reset();
            out.falla_sequences.reset();
        }
        out.ratios = computeSequenceRatios(out.prefalla_sequences,
                                           out.falla_sequences);
        if (!out.prefalla_usable) out.ratios.i1_fault_pre.reset();
        if (out.falla_sequences.has_value()) {
            if (!out.ratios.i0_i1.has_value()) {
                out.diagnostics.push_back(makeDiagnostic(
                    DiagnosticSeverity::Warning, "analysis.ratio.i0_i1",
                    "La secuencia positiva de la ventana de falla es prácticamente "
                    "nula; los cocientes de secuencia no son interpretables."));
            }
            if (!out.ratios.i1_fault_pre.has_value()) {
                out.diagnostics.push_back(makeDiagnostic(
                    DiagnosticSeverity::Warning, "analysis.ratio.i1_increase",
                    "No se pudo calcular un cociente válido I1 falla/prefalla; "
                    "revisa la ventana prefalla y la escala del registro."));
            }
        }

        if (out.prefalla_phasors.has_value() && out.falla_phasors.has_value() &&
            !out.windows_overlap && out.coherence_errors.empty() && out.prefalla_usable) {
            FaultClassifyContext context;
            context.pre_center_s = out.prefalla_center_s;
            context.onset_s = onset;
            context.window_duration_s = static_cast<double>(out.cycles) / frequency;
            std::vector<std::complex<double>> pre_values;
            std::vector<std::complex<double>> fault_values;
            for (const Phasor& phasor : *out.prefalla_phasors) {
                pre_values.push_back(phasor.value);
            }
            for (const Phasor& phasor : *out.falla_phasors) {
                fault_values.push_back(phasor.value);
            }
            out.classification = classifyFault(pre_values, fault_values, context);
        }
    }

                                                                           
    if (out.voltage.selection.complete()) {
        std::array<std::string, 3> normalized_units{};
        for (std::size_t slot = 0; slot < 3; ++slot) {
            const std::size_t index = *out.voltage.selection.index[slot];
            if (index < record.cfg.analog.size()) {
                normalized_units[slot] =
                    normalizeUnit(record.cfg.analog[index].unit);
            }
        }
        out.voltage.units_consistent =
            !normalized_units[0].empty() &&
            normalized_units[0] == normalized_units[1] &&
            normalized_units[1] == normalized_units[2];
        out.voltage.unit =
            out.voltage.units_consistent ? normalized_units[0] : std::string();

        const std::vector<std::size_t> voltage_abc = {
            *out.voltage.selection.index[0], *out.voltage.selection.index[1],
            *out.voltage.selection.index[2]};
        const auto pre_phasors = windowPhasors(
            record.data, voltage_abc, frequency, out.prefalla_center_s,
            out.cycles);
        const auto fault_phasors = windowPhasors(
            record.data, voltage_abc, frequency, out.falla_center_s,
            out.cycles);
        if (pre_phasors.has_value() && pre_phasors->size() == 3) {
            out.voltage.prefalla_phasors = std::array<Phasor, 3>{
                Phasor{(*pre_phasors)[0]}, Phasor{(*pre_phasors)[1]},
                Phasor{(*pre_phasors)[2]}};
            out.voltage.prefalla_sequences = symmetricalComponents(*pre_phasors);
        }
        if (fault_phasors.has_value() && fault_phasors->size() == 3) {
            out.voltage.falla_phasors = std::array<Phasor, 3>{
                Phasor{(*fault_phasors)[0]}, Phasor{(*fault_phasors)[1]},
                Phasor{(*fault_phasors)[2]}};
            out.voltage.falla_sequences = symmetricalComponents(*fault_phasors);
        }
        out.voltage.available = out.voltage.prefalla_phasors.has_value() &&
                                out.voltage.falla_phasors.has_value();
        if (out.voltage.prefalla_phasors.has_value() &&
            out.voltage.falla_phasors.has_value()) {
            for (std::size_t slot = 0; slot < 3; ++slot) {
                const double pre =
                    (*out.voltage.prefalla_phasors)[slot].magnitude();
                const double fault =
                    (*out.voltage.falla_phasors)[slot].magnitude();
                if (isFinite(pre) && pre > 0.0 && isFinite(fault)) {
                    const double change = (fault / pre - 1.0) * 100.0;
                    if (isFinite(change)) {
                        out.voltage.change_pct[slot] = change;
                    }
                }
            }
        } else {
            out.voltage.diagnostics.push_back(makeDiagnostic(
                DiagnosticSeverity::Warning,
                "analysis.voltage.phasor.unavailable",
                "No se pudieron calcular los fasores de tensión de ambas "
                "ventanas (fuera del registro, sin muestras suficientes o ajuste "
                "no resoluble); no se informan secuencias ni cambio porcentual de "
                "tensión. El análisis de corriente no se ve afectado."));
        }
    } else if (out.voltage.selection.inOrder().empty()) {
        Diagnostic unavailable = makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.voltage.unavailable",
            "No hay canales de tensión seleccionados; no se calculan fasores ni "
            "secuencias de tensión.");
        out.voltage.diagnostics.push_back(unavailable);
        out.diagnostics.push_back(std::move(unavailable));
    } else {
        Diagnostic partial = makeDiagnostic(
            DiagnosticSeverity::Info, "analysis.voltage.partial",
            "La selección de tensión está incompleta; no se calculan fasores "
            "trifásicos ni secuencias de tensión. El análisis de corriente "
            "continúa.");
        out.voltage.diagnostics.push_back(partial);
        out.diagnostics.push_back(std::move(partial));
    }

    for (std::size_t slot = 0; slot < 3; ++slot) {
        if (out.phases.index[slot].has_value()) {
            const std::size_t channel = *out.phases.index[slot];
            out.harmonics[slot] =
                harmonicFit(record.data, channel, frequency,
                            out.falla_center_s, out.cycles,
                            options.max_harmonic_order);
            const std::string channel_unit =
                channel < record.cfg.analog.size()
                    ? record.cfg.analog[channel].unit
                    : std::string();
            out.spectra[slot] = fftSpectrum(
                record.data, channel, channel_unit, frequency,
                out.falla_center_s, out.cycles, options.max_harmonic_order,
                kFftMaxPoints);
            out.saturation[slot] = shoulderSaturation(
                record.data, channel, frequency, out.falla_center_s, out.cycles,
                &out.harmonics[slot]);
                                                                              
                                                                           
            auto appendRelevant = [&out](const std::vector<Diagnostic>& items) {
                for (const Diagnostic& diagnostic : items) {
                    if (diagnostic.severity != DiagnosticSeverity::Info) {
                        out.diagnostics.push_back(diagnostic);
                    }
                }
            };
            appendRelevant(out.spectra[slot]->diagnostics);
            appendRelevant(out.saturation[slot]->diagnostics);
        } else {
            out.harmonics[slot].cycles = out.cycles;
        }
    }

    flushVoltageDiagnostics();

    if (options.fault_distance_text.has_value()) {
        out.fault_distance = interpretFaultDistance(
            *options.fault_distance_text, options.reference_length_km);
        append(out.fault_distance->diagnostics);
    }

    if (out.event.has_value()) {
        out.origin = WaveformOrigin::Onset;
        out.origin_label = "inicio candidato";
        out.event_origin_s = out.event->time_s;
    } else if (out.time.trigger_within_capture) {
        out.origin = WaveformOrigin::Trigger;
        out.origin_label = "trigger";
        out.event_origin_s = out.time.reference_s;
    } else {
        out.origin = WaveformOrigin::Midpoint;
        out.origin_label = "referencia provisional";
        out.event_origin_s = out.time.reference_s;
    }

    out.breaker = analyzeBreakerTiming(record, out, options.breaker);
    out.diagnostics.insert(out.diagnostics.end(), out.breaker.diagnostics.begin(),
                           out.breaker.diagnostics.end());
    out.ok = !out.hasError();
    return out;
}

}                                   
