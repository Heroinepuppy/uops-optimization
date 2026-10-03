#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

struct ComputeOptions {
    std::size_t points = 200000;
    unsigned rounds = 100, max_transforms = 1024, cpu = 4;
    double block_ms = 5;
    std::filesystem::path output = "result";
    bool verify_only = false;
};
inline ComputeOptions compute_options(int argc, char** argv) {
    ComputeOptions o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--verify-only") { o.verify_only = true; continue; }
        if (i + 1 == argc) throw std::runtime_error("Fehlender Wert: " + arg);
        const std::string value = argv[++i];
        if (arg == "--output-dir") { o.output = value; continue; }
        std::size_t consumed = 0;
        if (value.empty() || value[0] == '-') throw std::runtime_error("Ungueltiger Wert: " + value);
        const auto n = std::stoull(value, &consumed);
        if (consumed != value.size() || (n == 0 && arg != "--cpu") || n > 100000000)
            throw std::runtime_error("Ungueltiger Wert: " + value);
        if (arg == "--points") o.points = static_cast<std::size_t>(n);
        else if (arg == "--rounds") o.rounds = static_cast<unsigned>(n);
        else if (arg == "--max-transforms" && n <= 65536) o.max_transforms = static_cast<unsigned>(n);
        else if (arg == "--cpu" && n < 64) o.cpu = static_cast<unsigned>(n);
        else if (arg == "--block-ms" && n <= 1000) o.block_ms = static_cast<double>(n);
        else throw std::runtime_error("Unbekannte Option oder Wert ausserhalb des Bereichs: " + arg);
    }
    return o;
}
inline std::vector<unsigned> compute_counts(unsigned maximum) {
    std::vector<unsigned> counts;
    for (unsigned k = 1; k <= maximum; k *= 2) counts.push_back(k);
    if (counts.back() != maximum) counts.push_back(maximum);
    return counts;
}
template<class T> T compute_transform() {
    return T{0.8660254038f, -0.5f, 0.0f, 0.5f, 0.8660254038f, 0.0f,
             0.0f, 0.0f, 1.0f, 1.25f, -3.50f, 0.75f};
}
inline void compute_fill(std::vector<float>& x, std::vector<float>& y, std::vector<float>& z) {
    std::mt19937 rng(0x3950u);
    std::uniform_real_distribution<float> dist(-50, 50);
    for (std::size_t i = 0; i < x.size(); ++i) { x[i] = dist(rng); y[i] = dist(rng); z[i] = dist(rng); }
}
template<class T> void compute_verify(const std::vector<float>& x, const std::vector<float>& y,
    const std::vector<float>& z, const std::vector<float>& ox, const std::vector<float>& oy,
    const std::vector<float>& oz, const T& t, unsigned k, bool full = false) {
    // Double precision reference. Sample the cloud including all partition edges;
    // small self-tests check every point, including SIMD and GPU block tails.
    const std::size_t stride = full ? 1 : std::max<std::size_t>(1, x.size() / 97);
    auto check = [&](std::size_t i) {
        double X = x[i], Y = y[i], Z = z[i];
        for (unsigned j = 0; j < k; ++j) {
            const double nx = t.r00*X + t.r01*Y + t.r02*Z + t.tx;
            const double ny = t.r10*X + t.r11*Y + t.r12*Z + t.ty;
            const double nz = t.r20*X + t.r21*Y + t.r22*Z + t.tz;
            X = nx; Y = ny; Z = nz;
        }
        const std::array<double, 3> expected{X,Y,Z}, actual{ox[i],oy[i],oz[i]};
        for (int axis = 0; axis < 3; ++axis) {
            const double tolerance = 0.0001 + 0.00001*k + 0.00002*std::abs(expected[axis]);
            if (!std::isfinite(actual[axis]) || std::abs(actual[axis]-expected[axis]) > tolerance)
                throw std::runtime_error("Validierung fehlgeschlagen: Punkt " + std::to_string(i)
                    + ", Transformationen " + std::to_string(k));
        }
    };
    for (std::size_t i = 0; i < x.size(); i += stride) check(i);
    check(x.size()-1);
    for (std::size_t part = 1; part < 4; ++part) {
        const auto edge = part*(x.size()/4);
        if (edge) { check(edge-1); check(edge); }
    }
}
struct ComputeResult { unsigned transforms; std::string method; std::vector<double> samples; unsigned invalid_samples = 0; };
inline void compute_save(const ComputeOptions& o, const std::string& device,
                         const std::vector<ComputeResult>& results) {
    std::filesystem::create_directories(o.output);
    std::ofstream out(o.output / ("compute_" + device + "_results.csv"));
    std::ofstream raw(o.output / ("compute_" + device + "_samples.csv"));
    if (!out || !raw) throw std::runtime_error("Ergebnisdatei nicht schreibbar");
    out << "points;transformations;method;median_us;peak_us;samples;us_per_transform;invalid_samples\n";
    raw << "points;transformations;method;sample;us\n";
    out << std::setprecision(12); raw << std::setprecision(12);
    for (const auto& r : results) {
        if (r.samples.empty()) throw std::runtime_error("Keine gueltigen Messungen: " + r.method);
        auto sorted = r.samples;
        std::sort(sorted.begin(), sorted.end());
        const double median = (sorted[(sorted.size()-1)/2] + sorted[sorted.size()/2])/2;
        const double width = std::max(1e-9, (sorted.back()-sorted.front())/60);
        std::array<unsigned,60> bins{};
        for (double v : sorted) ++bins[std::min(59, static_cast<int>((v-sorted.front())/width))];
        const auto peak_bin = std::max_element(bins.begin(), bins.end())-bins.begin();
        const double peak = sorted.front()+(static_cast<double>(peak_bin)+0.5)*width;
        out << o.points << ';' << r.transforms << ';' << r.method << ';' << median << ';'
            << peak << ';' << sorted.size() << ';' << median/r.transforms << ';' << r.invalid_samples << '\n';
        for (std::size_t i=0; i<r.samples.size(); ++i)
            raw << o.points << ';' << r.transforms << ';' << r.method << ';' << i << ';' << r.samples[i] << '\n';
    }
    if (!out || !raw) throw std::runtime_error("Fehler beim Schreiben der Ergebnisse");
}
