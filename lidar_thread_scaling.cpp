#include <cstdlib>
#include "lidar_cpu_core.h"

class ParallelTeam {
public:
    ParallelTeam(SoA& soa, const Transform& t, std::size_t count,
                 unsigned cpu0, unsigned cpu1)
        : soa_(soa), t_(t), count_(count),
          cpu0_(cpu0), cpu1_(cpu1), start_(3), done_(3) {
        a_ = std::thread([this] { worker(0); });
        b_ = std::thread([this] { worker(1); });
    }

    ~ParallelTeam() {
        stop_.store(true, std::memory_order_release);
        start_.arrive_and_wait();
        a_.join();
        b_.join();
    }

    ParallelTeam(const ParallelTeam&) = delete;
    ParallelTeam& operator=(const ParallelTeam&) = delete;

    void run_once() {
        start_.arrive_and_wait();
        done_.arrive_and_wait();
    }

private:
    void worker(int id) {
        pin_this_thread_to_cpu(id == 0 ? cpu0_ : cpu1_);
        const std::size_t half = count_ / 2;
        const std::size_t begin = id == 0 ? 0 : half;
        const std::size_t end   = id == 0 ? half : count_;

        while (true) {
            start_.arrive_and_wait();
            if (stop_.load(std::memory_order_acquire)) break;

            transform_soa_avx2_x2(
                soa_.x.data() + begin, soa_.y.data() + begin, soa_.z.data() + begin,
                soa_.ox.data() + begin, soa_.oy.data() + begin, soa_.oz.data() + begin,
                end - begin, t_);

            done_.arrive_and_wait();
        }
    }

    SoA& soa_;
    const Transform& t_;
    std::size_t count_;
    unsigned cpu0_, cpu1_;
    std::atomic<bool> stop_{false};
    std::barrier<> start_, done_;
    std::thread a_, b_;
};


class ParallelTeam4 {
public:
    ParallelTeam4(SoA& soa,
                  const Transform& t,
                  std::size_t count,
                  const std::array<unsigned, 4>& cpus)
        : soa_(soa), t_(t), count_(count), cpus_(cpus),
          start_(5), done_(5) {
        for (int i = 0; i < 4; ++i) {
            workers_[i] = std::thread([this, i] { worker(i); });
        }
    }

    ~ParallelTeam4() {
        stop_.store(true, std::memory_order_release);
        start_.arrive_and_wait();
        for (auto& w : workers_) w.join();
    }

    ParallelTeam4(const ParallelTeam4&) = delete;
    ParallelTeam4& operator=(const ParallelTeam4&) = delete;

    void run_once() {
        start_.arrive_and_wait();
        done_.arrive_and_wait();
    }

private:
    void worker(int id) {
        pin_this_thread_to_cpu(cpus_[id]);

        const std::size_t quarter = count_ / 4;
        const std::size_t begin = static_cast<std::size_t>(id) * quarter;
        const std::size_t end = (id == 3) ? count_ : begin + quarter;

        while (true) {
            start_.arrive_and_wait();

            if (stop_.load(std::memory_order_acquire)) {
                break;
            }

            transform_soa_avx2_x2(
                soa_.x.data() + begin,
                soa_.y.data() + begin,
                soa_.z.data() + begin,
                soa_.ox.data() + begin,
                soa_.oy.data() + begin,
                soa_.oz.data() + begin,
                end - begin,
                t_);

            done_.arrive_and_wait();
        }
    }

    SoA& soa_;
    const Transform& t_;
    std::size_t count_;
    std::array<unsigned, 4> cpus_;
    std::atomic<bool> stop_{false};
    std::barrier<> start_;
    std::barrier<> done_;
    std::array<std::thread, 4> workers_;
};

enum class Mode { Single, SMT, TwoCore, TwoCoreOtherCCX, FourCoreFourCCX };

struct Stats {
    double minimum{}, mean{}, median{}, p90{}, p95{}, p99{}, stddev{};
};

static double percentile_sorted(const std::vector<double>& sorted, double p) {
    const double pos = p * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = static_cast<std::size_t>(std::ceil(pos));
    const double f = pos - static_cast<double>(lo);
    return sorted[lo] * (1.0 - f) + sorted[hi] * f;
}

static Stats compute_stats(const std::vector<double>& v) {
    auto srt = v;
    std::sort(srt.begin(), srt.end());
    const double mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();

    double var = 0.0;
    for (double x : v) {
        const double d = x - mean;
        var += d * d;
    }
    var /= v.size();

    return {
        srt.front(), mean, percentile_sorted(srt, .50),
        percentile_sorted(srt, .90), percentile_sorted(srt, .95),
        percentile_sorted(srt, .99), std::sqrt(var)
    };
}

struct CaseResult {
    std::string name;
    std::size_t points;
    std::vector<double> samples;
};

struct HistogramPeak {
    double center_us{};
    std::size_t count{};
};

static HistogramPeak histogram_peak(
    const std::vector<double>& samples,
    int bins = 60) {

    if (samples.empty()) return {};

    const auto [min_it, max_it] =
        std::minmax_element(samples.begin(), samples.end());

    double lo = *min_it;
    double hi = *max_it;
    if (hi <= lo) hi = lo + 1.0;

    const double width = (hi - lo) / static_cast<double>(bins);
    std::vector<std::size_t> counts(static_cast<std::size_t>(bins), 0);

    for (double v : samples) {
        int b = static_cast<int>((v - lo) / width);
        b = std::clamp(b, 0, bins - 1);
        counts[static_cast<std::size_t>(b)]++;
    }

    const auto max_it_count =
        std::max_element(counts.begin(), counts.end());
    const std::size_t index =
        static_cast<std::size_t>(std::distance(counts.begin(), max_it_count));

    return {
        lo + (static_cast<double>(index) + 0.5) * width,
        *max_it_count
    };
}

static std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

static void replace_results_section(
    const std::filesystem::path& path,
    const std::string& section_name,
    const std::string& section_body) {

    std::filesystem::create_directories(path.parent_path());

    std::string text = read_text_file(path);
    const std::string begin = "### BEGIN " + section_name + "\n";
    const std::string end   = "### END " + section_name + "\n";

    const auto p0 = text.find(begin);
    if (p0 != std::string::npos) {
        const auto p1 = text.find(end, p0);
        if (p1 != std::string::npos) {
            text.erase(p0, p1 + end.size() - p0);
        }
    }

    if (!text.empty() && text.back() != '\n') text.push_back('\n');
    if (!text.empty()) text.push_back('\n');

    text += begin;
    text += section_body;
    if (!section_body.empty() && section_body.back() != '\n') text.push_back('\n');
    text += end;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("CPU-Ergebnisdatei konnte nicht geoeffnet werden.");
    }
    out << text;
}

static double measure_block_ms(Mode mode, std::size_t repeats,
                               SoA& soa, const Transform& t,
                               ParallelTeam* smt,
                               ParallelTeam* two_core,
                               ParallelTeam* two_core_other_ccx,
                               ParallelTeam4* four_core_four_ccx) {
    using clock = std::chrono::steady_clock;
    const auto begin = clock::now();

    for (std::size_t i = 0; i < repeats; ++i) {
        if (mode == Mode::Single) {
            transform_soa_avx2_x2(
                soa.x.data(), soa.y.data(), soa.z.data(),
                soa.ox.data(), soa.oy.data(), soa.oz.data(),
                soa.x.size(), t);
        } else if (mode == Mode::SMT) {
            smt->run_once();
        } else if (mode == Mode::TwoCore) {
            two_core->run_once();
        } else if (mode == Mode::TwoCoreOtherCCX) {
            two_core_other_ccx->run_once();
        } else {
            four_core_four_ccx->run_once();
        }
        g_sink = g_sink + soa.ox[soa.x.size() / 2];
    }

    const auto end = clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

static std::size_t calibrate(Mode mode, double target_ms,
                             SoA& soa, const Transform& t,
                             ParallelTeam* smt,
                             ParallelTeam* two_core,
                             ParallelTeam* two_core_other_ccx,
                             ParallelTeam4* four_core_four_ccx) {
    for (int i = 0; i < 8; ++i) {
        measure_block_ms(mode, 1, soa, t, smt, two_core, two_core_other_ccx, four_core_four_ccx);
    }

    std::size_t repeats = 1;
    while (repeats < 100000) {
        const double ms = measure_block_ms(mode, repeats, soa, t, smt, two_core, two_core_other_ccx, four_core_four_ccx);
        if (ms >= 5.0) {
            return std::max<std::size_t>(
                1, static_cast<std::size_t>(
                    std::llround(static_cast<double>(repeats) * target_ms / ms)));
        }
        repeats *= 2;
    }
    return repeats;
}

static void fill_points(SoA& soa, std::mt19937& rng) {
    std::uniform_real_distribution<float> dist(-50.0f, 50.0f);
    for (std::size_t i = 0; i < soa.x.size(); ++i) {
        soa.x[i] = dist(rng);
        soa.y[i] = dist(rng);
        soa.z[i] = dist(rng);
    }
}

static std::array<CaseResult, 5> run_size(
    std::size_t points, int rounds, double target_ms,
    const CpuPairSelection& cpus, const Transform& t) {

    SoA soa(points);
    std::mt19937 rng(static_cast<unsigned>(0x3950u + points));
    fill_points(soa, rng);

    std::unique_ptr<ParallelTeam> smt;
    std::unique_ptr<ParallelTeam> two_core;
    std::unique_ptr<ParallelTeam> two_core_other_ccx;
    std::unique_ptr<ParallelTeam4> four_core_four_ccx;

    if (cpus.smt_found)
        smt = std::make_unique<ParallelTeam>(
            soa, t, points, cpus.base_cpu, cpus.smt_sibling);

    if (cpus.other_core_found)
        two_core = std::make_unique<ParallelTeam>(
            soa, t, points, cpus.base_cpu, cpus.other_core_cpu);

    if (cpus.other_ccx_found)
        two_core_other_ccx = std::make_unique<ParallelTeam>(
            soa, t, points, cpus.base_cpu, cpus.other_ccx_cpu);

    if (cpus.four_ccx_found)
        four_core_four_ccx = std::make_unique<ParallelTeam4>(
            soa, t, points, cpus.four_ccx_cpus);

    std::array<Mode, 5> modes = {
        Mode::Single,
        Mode::SMT,
        Mode::TwoCore,
        Mode::TwoCoreOtherCCX,
        Mode::FourCoreFourCCX
    };

    std::array<std::string, 5> names = {
        "1 Thread",
        "2 Threads / 1 Core SMT",
        "2 Threads / 2 Cores same CCX",
        "2 Threads / 2 Cores different CCX",
        "4 Threads / 4 Cores / 4 CCX"
    };

    std::array<std::size_t, 5> repeats{};
    std::array<CaseResult, 5> results = {{
        {names[0], points, {}},
        {names[1], points, {}},
        {names[2], points, {}},
        {names[3], points, {}},
        {names[4], points, {}}
    }};

    std::cout << "\n" << points << " Punkte\n";
    std::cout << "Kalibriere CPU-Blocklaengen...\n";

    for (int i = 0; i < 5; ++i) {
        if (points > 800'000 && i != 4) continue;

        if (i == 1 && !smt) continue;
        if (i == 2 && !two_core) continue;
        if (i == 3 && !two_core_other_ccx) continue;
        if (i == 4 && !four_core_four_ccx) continue;

        repeats[i] = calibrate(
            modes[i], target_ms, soa, t,
            smt.get(), two_core.get(), two_core_other_ccx.get(),
            four_core_four_ccx.get());

        const double ms = measure_block_ms(
            modes[i], repeats[i], soa, t,
            smt.get(), two_core.get(), two_core_other_ccx.get(),
            four_core_four_ccx.get());

        std::cout << "  " << std::setw(32) << std::left << names[i]
                  << ": " << repeats[i] << " Clouds/Block"
                  << " (~" << std::fixed << std::setprecision(1) << ms << " ms)"
                  << " = " << std::setprecision(2)
                  << (ms * 1000.0 / static_cast<double>(repeats[i]))
                  << " us/Cloud\n";

        results[i].samples.reserve(rounds);
    }

    std::vector<int> active;

    // Bis einschließlich 800k messen wir weiterhin alle CPU-Varianten.
    // Oberhalb davon verwenden wir nur noch den bisher schnellsten bekannten
    // CPU-Pfad: 4 Threads / 4 Cores / 4 CCX.
    if (points <= 800'000) {
        for (int i = 0; i < 5; ++i) {
            if (repeats[i] != 0) active.push_back(i);
        }
    } else {
        if (repeats[4] != 0) {
            active.push_back(4);
        }
    }

    std::mt19937 order_rng(static_cast<unsigned>(0xA000u + points));

    std::cout << "Benchmark laeuft...\n";
    for (int round = 0; round < rounds; ++round) {
        std::shuffle(active.begin(), active.end(), order_rng);

        for (int i : active) {
            const double total_ms = measure_block_ms(
                modes[i], repeats[i], soa, t,
                smt.get(), two_core.get(), two_core_other_ccx.get(),
                four_core_four_ccx.get());

            results[i].samples.push_back(
                total_ms * 1000.0 / static_cast<double>(repeats[i]));
        }

        if ((round + 1) % 50 == 0 || round + 1 == rounds) {
            std::cout << "\r  Runde " << std::setw(4) << (round + 1)
                      << " / " << rounds << std::flush;
        }
    }
    std::cout << "\n";

    return results;
}



static void print_results(const std::array<CaseResult, 5>& results) {
    for (const auto& r : results) {
        if (r.samples.empty()) continue;
        const Stats s = compute_stats(r.samples);
        std::cout << std::left << std::setw(24) << r.name
                  << " median " << std::right << std::fixed << std::setprecision(2)
                  << std::setw(8) << s.median << " us"
                  << " | p95 " << std::setw(8) << s.p95
                  << " | stddev " << std::setw(8) << s.stddev << "\n";
    }
}

static void plot_threading(const std::vector<CaseResult>& all) {
    const std::filesystem::path result_dir =
        std::filesystem::path("results") / "data";
    std::filesystem::create_directories(result_dir);
    std::filesystem::create_directories(result_dir.parent_path() / "pics");

    const std::filesystem::path png_path =
        result_dir.parent_path() / "pics" / "thread_scaling.png";
    const std::filesystem::path script =
        result_dir / "thread_scaling.gp";

    std::ofstream gp(script, std::ios::trunc);
    if (!gp) {
        std::cerr << "Gnuplot-Skript konnte nicht geschrieben werden.\n";
        return;
    }

    constexpr int bins = 60;
    int block = 0;

    for (const auto& r : all) {
        if (r.samples.empty()) continue;

        const auto mm =
            std::minmax_element(r.samples.begin(), r.samples.end());

        double lo = *mm.first;
        double hi = *mm.second;
        if (hi <= lo) hi = lo + 1.0;

        const double width = (hi - lo) / bins;
        std::array<std::size_t, bins> counts{};

        for (double v : r.samples) {
            int b = static_cast<int>((v - lo) / width);
            b = std::clamp(b, 0, bins - 1);
            counts[b]++;
        }

        gp << "$H" << block << " << EOD\n";

        for (int i = 0; i < bins; ++i) {
            gp << std::fixed << std::setprecision(9)
               << lo + (i + 0.5) * width
               << " " << counts[i] << "\n";
        }

        gp << "EOD\n\n";
        ++block;
    }

    if (block == 0) return;

    constexpr int columns = 5;
    const int rows = (block + columns - 1) / columns;

    auto emit_layout = [&]() {
        // Start each terminal with a clean full-canvas layout. Otherwise the
        // interactive plot can inherit the last PNG panel's origin and labels.
        gp << "reset\n";
        gp << "set origin 0,0\nset size 1,1\n";
        gp << "set multiplot layout " << rows << "," << columns
           << " rowsfirst title "
              "'LiDAR CPU Benchmark - Laufzeitverteilungen' font ',14'\n";
        gp << "set style fill solid 0.70 border -1\n";
        gp << "set boxwidth 0.90 relative\n";
        gp << "set grid ytics\n";
        gp << "set key off\n";

        int plot_block = 0;

        for (const auto& r : all) {
            if (r.samples.empty()) continue;

            gp << "set title '" << r.points / 1000
               << "k - " << r.name << "'\n";
            gp << "set xlabel 'us pro komplette Cloud'\n";
            gp << "set ylabel 'Anzahl Messbloecke'\n";
            if (r.points == 25'600'000)
                gp << "set xrange [0:30000]\n";
            else
                gp << "set xrange [0:*]\n";
            gp << "set yrange [0:*]\n";
            gp << "plot $H" << plot_block++
               << " using 1:2 with boxes\n";
        }

        gp << "unset multiplot\n";
    };

    // Save a persistent result image.
    gp << "set term pngcairo size 3200," << rows * 500
       << " enhanced font 'Segoe UI,8'\n";
    gp << "set output '" << png_path.generic_string() << "'\n";
    emit_layout();
    gp << "unset output\n\n";

    // Keep the interactive Gnuplot window too.
    if (!std::getenv("LIDAR_BATCH")) {
    gp << "set term qt size 1900,1200 enhanced font 'Segoe UI,8'\n";
    emit_layout();

    }
    gp.close();

    std::cout << "PNG gespeichert: " << png_path << "\n";
    std::cout << "Gnuplot-Skript: " << script << "\n";

    const std::string cmd =
        "gnuplot -persist \"" + script.string() + "\"";
    std::system(cmd.c_str());
}

static void write_cpu_results(const std::vector<CaseResult>& all) {
    const std::filesystem::path path =
        std::filesystem::path("results") / "data" / "cpu_results.txt";

    std::ostringstream body;
    body << "Histogram bins: 60\n";
    body << "Peak = center of histogram bin with highest sample count\n";
    body << "points;method;peak_us;peak_count;samples\n";

    for (const auto& r : all) {
        if (r.samples.empty()) continue;
        const auto peak = histogram_peak(r.samples, 60);
        body << r.points << ";"
             << r.name << ";"
             << std::fixed << std::setprecision(3) << peak.center_us << ";"
             << peak.count << ";"
             << r.samples.size() << "\n";
    }

    replace_results_section(path, "CPU", body.str());
    std::cout << "CPU-Peaks gespeichert: " << path << "\n";
}


int main(int argc, char** argv) {
    int rounds = 1000;
    double target_ms = 50.0;
    unsigned logical_cpu = 4;
    bool show_plot = true;

    if (argc > 1) rounds = std::max(20, std::stoi(argv[1]));
    if (argc > 2) target_ms = std::max(5.0, std::stod(argv[2]));
    if (argc > 3) logical_cpu = static_cast<unsigned>(std::stoul(argv[3]));
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--no-plot") show_plot = false;

    pin_this_thread_to_cpu(logical_cpu);
    const auto cpus = select_cpu_pairs(logical_cpu);

    std::cout << "LiDAR Thread Scaling Benchmark\n"
              << "Version       : CPU-SIZE-SWEEP-FASTEST-v2\n"
              << "------------------------------\n"
              << "Kernel        : AVX2 Zen2 x2\n"
              << "Messrunden    : " << rounds << "\n"
              << "Blockziel     : " << target_ms << " ms\n"
              << "Basis CPU     : " << cpus.base_cpu << "\n";

    if (cpus.smt_found)
        std::cout << "SMT-Sibling   : " << cpus.smt_sibling << "\n";
    if (cpus.other_core_found)
        std::cout << "2. Core CPU   : " << cpus.other_core_cpu << "\n";

    const Transform t{
         0.8660254038f, -0.5f,          0.0f,
         0.5f,           0.8660254038f, 0.0f,
         0.0f,           0.0f,          1.0f,
         1.25f,         -3.50f,         0.75f
    };

    const std::array<std::size_t, 8> point_counts = {
        200'000,
        400'000,
        800'000,
        1'600'000,
        3'200'000,
        6'400'000,
        12'800'000,
        25'600'000
    };

    std::vector<std::array<CaseResult, 5>> all_sizes;
    all_sizes.reserve(point_counts.size());

    for (std::size_t points : point_counts) {
        all_sizes.push_back(
            run_size(points, rounds, target_ms, cpus, t));
    }

    for (std::size_t i = 0; i < point_counts.size(); ++i) {
        std::cout << "\n=== " << point_counts[i] << " Punkte CPU ===\n";
        print_results(all_sizes[i]);

        if (all_sizes[i][0].samples.empty()) continue;

        const double base =
            compute_stats(all_sizes[i][0].samples).median;

        std::cout << "Speedup gegen 1 Thread:\n";
        for (int c = 1; c < 5; ++c) {
            if (all_sizes[i][c].samples.empty()) continue;
            const double med =
                compute_stats(all_sizes[i][c].samples).median;

            std::cout << "  "
                      << std::setw(32) << std::left
                      << all_sizes[i][c].name
                      << ": " << std::fixed << std::setprecision(3)
                      << base / med << "x\n";
        }
    }

    std::vector<CaseResult> result_cases;
    for (auto& size_results : all_sizes) {
        for (auto& r : size_results) {
            if (!r.samples.empty()) {
                result_cases.push_back(std::move(r));
            }
        }
    }

    write_cpu_results(result_cases);

    if (show_plot) {
        std::cout << "\nOeffne CPU-Histogramme...\n";
        plot_threading(result_cases);
    }

    std::cout << "\nSink: " << g_sink << "\n";
    return 0;
}
