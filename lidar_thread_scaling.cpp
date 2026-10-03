#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <immintrin.h>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

struct Transform {
    float r00, r01, r02;
    float r10, r11, r12;
    float r20, r21, r22;
    float tx, ty, tz;
};

struct SoA {
    std::vector<float> x, y, z;
    std::vector<float> ox, oy, oz;

    explicit SoA(std::size_t n)
        : x(n), y(n), z(n), ox(n), oy(n), oz(n) {}
};

volatile float g_sink = 0.0f;

static void pin_this_thread_to_cpu(unsigned logical_cpu) {
    const DWORD_PTR mask = (DWORD_PTR{1} << logical_cpu);
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        std::cerr << "WARNUNG: CPU-Affinitaet fuer CPU "
                  << logical_cpu << " konnte nicht gesetzt werden.\n";
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}

__declspec(noinline)
void transform_soa_avx2_x2(const float* __restrict x,
                           const float* __restrict y,
                           const float* __restrict z,
                           float* __restrict ox,
                           float* __restrict oy,
                           float* __restrict oz,
                           std::size_t count,
                           const Transform& t) {
    const __m256 r00 = _mm256_set1_ps(t.r00);
    const __m256 r01 = _mm256_set1_ps(t.r01);
    const __m256 r02 = _mm256_set1_ps(t.r02);
    const __m256 r10 = _mm256_set1_ps(t.r10);
    const __m256 r11 = _mm256_set1_ps(t.r11);
    const __m256 r12 = _mm256_set1_ps(t.r12);
    const __m256 r20 = _mm256_set1_ps(t.r20);
    const __m256 r21 = _mm256_set1_ps(t.r21);
    const __m256 r22 = _mm256_set1_ps(t.r22);
    const __m256 tx  = _mm256_set1_ps(t.tx);
    const __m256 ty  = _mm256_set1_ps(t.ty);
    const __m256 tz  = _mm256_set1_ps(t.tz);

    std::size_t i = 0;

    for (; i + 16 <= count; i += 16) {
        const __m256 X0 = _mm256_loadu_ps(x + i);
        const __m256 Y0 = _mm256_loadu_ps(y + i);
        const __m256 Z0 = _mm256_loadu_ps(z + i);
        const __m256 X1 = _mm256_loadu_ps(x + i + 8);
        const __m256 Y1 = _mm256_loadu_ps(y + i + 8);
        const __m256 Z1 = _mm256_loadu_ps(z + i + 8);

        __m256 Xp0 = _mm256_mul_ps(r00, X0);
        __m256 Yp0 = _mm256_mul_ps(r10, X0);
        __m256 Zp0 = _mm256_mul_ps(r20, X0);
        __m256 Xp1 = _mm256_mul_ps(r00, X1);
        __m256 Yp1 = _mm256_mul_ps(r10, X1);
        __m256 Zp1 = _mm256_mul_ps(r20, X1);

        Xp0 = _mm256_fmadd_ps(r01, Y0, Xp0);
        Yp0 = _mm256_fmadd_ps(r11, Y0, Yp0);
        Zp0 = _mm256_fmadd_ps(r21, Y0, Zp0);
        Xp1 = _mm256_fmadd_ps(r01, Y1, Xp1);
        Yp1 = _mm256_fmadd_ps(r11, Y1, Yp1);
        Zp1 = _mm256_fmadd_ps(r21, Y1, Zp1);

        Xp0 = _mm256_fmadd_ps(r02, Z0, Xp0);
        Yp0 = _mm256_fmadd_ps(r12, Z0, Yp0);
        Zp0 = _mm256_fmadd_ps(r22, Z0, Zp0);
        Xp1 = _mm256_fmadd_ps(r02, Z1, Xp1);
        Yp1 = _mm256_fmadd_ps(r12, Z1, Yp1);
        Zp1 = _mm256_fmadd_ps(r22, Z1, Zp1);

        Xp0 = _mm256_add_ps(Xp0, tx);
        Yp0 = _mm256_add_ps(Yp0, ty);
        Zp0 = _mm256_add_ps(Zp0, tz);
        Xp1 = _mm256_add_ps(Xp1, tx);
        Yp1 = _mm256_add_ps(Yp1, ty);
        Zp1 = _mm256_add_ps(Zp1, tz);

        _mm256_storeu_ps(ox + i, Xp0);
        _mm256_storeu_ps(oy + i, Yp0);
        _mm256_storeu_ps(oz + i, Zp0);
        _mm256_storeu_ps(ox + i + 8, Xp1);
        _mm256_storeu_ps(oy + i + 8, Yp1);
        _mm256_storeu_ps(oz + i + 8, Zp1);
    }

    for (; i < count; ++i) {
        const float X = x[i], Y = y[i], Z = z[i];
        ox[i] = t.r00 * X + t.r01 * Y + t.r02 * Z + t.tx;
        oy[i] = t.r10 * X + t.r11 * Y + t.r12 * Z + t.ty;
        oz[i] = t.r20 * X + t.r21 * Y + t.r22 * Z + t.tz;
    }
}

struct CpuPairSelection {
    unsigned base_cpu{};
    unsigned smt_sibling{};
    unsigned other_core_cpu{};
    unsigned other_ccx_cpu{};
    std::array<unsigned, 4> four_ccx_cpus{};
    bool smt_found{false};
    bool other_core_found{false};
    bool other_ccx_found{false};
    bool four_ccx_found{false};
};

static std::vector<unsigned> cpus_from_mask(KAFFINITY mask) {
    std::vector<unsigned> cpus;
    for (unsigned bit = 0; bit < sizeof(KAFFINITY) * 8; ++bit) {
        if (mask & (KAFFINITY{1} << bit)) cpus.push_back(bit);
    }
    return cpus;
}

static CpuPairSelection select_cpu_pairs(unsigned requested_cpu) {
    CpuPairSelection result{};
    result.base_cpu = requested_cpu;

    // --- Physical core topology ---
    DWORD core_len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &core_len);
    if (!core_len) return result;

    std::vector<unsigned char> core_buffer(core_len);
    auto* core_info =
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(core_buffer.data());

    if (!GetLogicalProcessorInformationEx(
            RelationProcessorCore, core_info, &core_len)) {
        return result;
    }

    std::vector<std::vector<unsigned>> cores;
    unsigned offset = 0;

    while (offset < core_len) {
        auto* e = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            core_buffer.data() + offset);

        if (e->Relationship == RelationProcessorCore) {
            std::vector<unsigned> core;

            for (WORD g = 0; g < e->Processor.GroupCount; ++g) {
                const auto& gm = e->Processor.GroupMask[g];
                if (gm.Group == 0) {
                    auto part = cpus_from_mask(gm.Mask);
                    core.insert(core.end(), part.begin(), part.end());
                }
            }

            if (!core.empty()) cores.push_back(std::move(core));
        }

        offset += e->Size;
    }

    std::size_t base_core_index = static_cast<std::size_t>(-1);

    for (std::size_t i = 0; i < cores.size(); ++i) {
        if (std::find(cores[i].begin(), cores[i].end(), requested_cpu)
            != cores[i].end()) {

            base_core_index = i;

            for (unsigned cpu : cores[i]) {
                if (cpu != requested_cpu) {
                    result.smt_sibling = cpu;
                    result.smt_found = true;
                    break;
                }
            }

            break;
        }
    }

    // Pick another physical core without caring about CCX.
    if (base_core_index != static_cast<std::size_t>(-1)) {
        for (std::size_t i = 0; i < cores.size(); ++i) {
            if (i == base_core_index || cores[i].empty()) continue;
            result.other_core_cpu = cores[i].front();
            result.other_core_found = true;
            break;
        }
    }

    // --- L3 / CCX topology ---
    // On Zen 2 desktop Ryzen, each CCX has its own 16 MB L3 slice.
    // Windows exposes the sharing mask for each L3 cache via RelationCache.
    DWORD cache_len = 0;
    GetLogicalProcessorInformationEx(RelationCache, nullptr, &cache_len);

    if (cache_len) {
        std::vector<unsigned char> cache_buffer(cache_len);
        auto* cache_info =
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                cache_buffer.data());

        if (GetLogicalProcessorInformationEx(
                RelationCache, cache_info, &cache_len)) {

            std::vector<KAFFINITY> l3_masks;
            unsigned cache_offset = 0;

            while (cache_offset < cache_len) {
                auto* e =
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
                        cache_buffer.data() + cache_offset);

                if (e->Relationship == RelationCache &&
                    e->Cache.Level == 3 &&
                    e->Cache.GroupMask.Group == 0) {
                    l3_masks.push_back(e->Cache.GroupMask.Mask);
                }

                cache_offset += e->Size;
            }

            KAFFINITY base_l3_mask = 0;
            for (KAFFINITY mask : l3_masks) {
                if (mask & (KAFFINITY{1} << requested_cpu)) {
                    base_l3_mask = mask;
                    break;
                }
            }

            if (base_l3_mask != 0) {
                // Select one logical processor from each of the four distinct
                // L3 sharing groups. On Ryzen 9 3950X these are the four CCXs.
                result.four_ccx_cpus[0] = requested_cpu;
                std::size_t ccx_slot = 1;

                for (KAFFINITY mask : l3_masks) {
                    if (mask == base_l3_mask || ccx_slot >= 4) continue;

                    const auto ccx_cpus = cpus_from_mask(mask);
                    if (!ccx_cpus.empty()) {
                        result.four_ccx_cpus[ccx_slot++] = ccx_cpus.front();
                    }
                }

                result.four_ccx_found = (ccx_slot == 4);

                // Choose a logical CPU that belongs to a DIFFERENT L3 sharing
                // group and is therefore on another Zen-2 CCX.
                for (KAFFINITY mask : l3_masks) {
                    if (mask == base_l3_mask) continue;

                    const auto cpus = cpus_from_mask(mask);

                    for (unsigned cpu : cpus) {
                        // Prefer one hardware thread from a physical core.
                        bool different_physical_core = true;

                        if (base_core_index != static_cast<std::size_t>(-1)) {
                            if (std::find(
                                    cores[base_core_index].begin(),
                                    cores[base_core_index].end(),
                                    cpu) != cores[base_core_index].end()) {
                                different_physical_core = false;
                            }
                        }

                        if (different_physical_core) {
                            result.other_ccx_cpu = cpu;
                            result.other_ccx_found = true;
                            break;
                        }
                    }

                    if (result.other_ccx_found) break;
                }
            }
        }
    }

    return result;
}

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

static std::array<CaseResult, 4> run_size(
    std::size_t points, int rounds, double target_ms,
    const CpuPairSelection& cpus, const Transform& t,
    bool include_single) {

    SoA soa(points);
    std::mt19937 rng(static_cast<unsigned>(0x3950u + points));
    fill_points(soa, rng);

    std::unique_ptr<ParallelTeam> smt;
    std::unique_ptr<ParallelTeam> two_core;
    std::unique_ptr<ParallelTeam> two_core_other_ccx;

    if (cpus.smt_found)
        smt = std::make_unique<ParallelTeam>(
            soa, t, points, cpus.base_cpu, cpus.smt_sibling);

    if (cpus.other_core_found)
        two_core = std::make_unique<ParallelTeam>(
            soa, t, points, cpus.base_cpu, cpus.other_core_cpu);

    if (cpus.other_ccx_found)
        two_core_other_ccx = std::make_unique<ParallelTeam>(
            soa, t, points, cpus.base_cpu, cpus.other_ccx_cpu);

    std::array<Mode, 4> modes = {
        Mode::Single,
        Mode::SMT,
        Mode::TwoCore,
        Mode::TwoCoreOtherCCX
    };

    std::array<std::string, 4> names = {
        "1 Thread",
        "2 Threads / 1 Core SMT",
        "2 Threads / 2 Cores same CCX",
        "2 Threads / 2 Cores different CCX"
    };

    std::array<std::size_t, 4> repeats{};
    std::array<CaseResult, 4> results = {{
        {names[0], points, {}},
        {names[1], points, {}},
        {names[2], points, {}},
        {names[3], points, {}}
    }};

    std::cout << "\n" << points << " Punkte\n";
    std::cout << "Kalibriere Threading-Blocklaengen...\n";

    for (int i = 0; i < 4; ++i) {
        if (i == 0 && !include_single) continue;
        if (i == 1 && !smt) continue;
        if (i == 2 && !two_core) continue;
        if (i == 3 && !two_core_other_ccx) continue;

        repeats[i] = calibrate(
            modes[i], target_ms, soa, t,
            smt.get(), two_core.get(), two_core_other_ccx.get(), nullptr);

        const double ms = measure_block_ms(
            modes[i], repeats[i], soa, t,
            smt.get(), two_core.get(), two_core_other_ccx.get(), nullptr);

        std::cout << "  " << std::setw(24) << std::left << names[i]
                  << ": " << repeats[i] << " Clouds/Block"
                  << " (~" << std::fixed << std::setprecision(1) << ms << " ms)\n";

        results[i].samples.reserve(rounds);
    }

    std::vector<int> active;
    for (int i = 0; i < 4; ++i) {
        if (repeats[i] != 0) active.push_back(i);
    }

    std::mt19937 order_rng(static_cast<unsigned>(0xA000u + points));

    std::cout << "Benchmark laeuft...\n";
    for (int round = 0; round < rounds; ++round) {
        std::shuffle(active.begin(), active.end(), order_rng);

        for (int i : active) {
            const double total_ms = measure_block_ms(
                modes[i], repeats[i], soa, t,
                smt.get(), two_core.get(), two_core_other_ccx.get(), nullptr);

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


static CaseResult run_800k_four_ccx(
    int rounds,
    double target_ms,
    const CpuPairSelection& cpus,
    const Transform& t) {

    constexpr std::size_t points = 800'000;
    CaseResult result{"4 Threads / 4 Cores / 4 CCX", points, {}};

    if (!cpus.four_ccx_found) {
        std::cerr << "4-CCX-Test uebersprungen: vier verschiedene L3/CCX-Gruppen "
                     "konnten nicht gefunden werden.\n";
        return result;
    }

    SoA soa(points);
    std::mt19937 rng(0x4CC800u);
    fill_points(soa, rng);

    ParallelTeam4 team(soa, t, points, cpus.four_ccx_cpus);

    for (int i = 0; i < 8; ++i) {
        team.run_once();
    }

    auto measure = [&](std::size_t repeats) {
        using clock = std::chrono::steady_clock;
        const auto begin = clock::now();

        for (std::size_t i = 0; i < repeats; ++i) {
            team.run_once();
            g_sink = g_sink + soa.ox[points / 2];
        }

        const auto end = clock::now();
        return std::chrono::duration<double, std::milli>(end - begin).count();
    };

    std::size_t repeats = 1;
    while (repeats < 100000) {
        const double ms = measure(repeats);
        if (ms >= 5.0) {
            repeats = std::max<std::size_t>(
                1,
                static_cast<std::size_t>(
                    std::llround(static_cast<double>(repeats) * target_ms / ms)));
            break;
        }
        repeats *= 2;
    }

    const double cal_ms = measure(repeats);
    std::cout << "\n800000 Punkte - 4 Cores / 4 CCX\n";
    std::cout << "Kalibriere 4-CCX-Blocklaenge...\n";
    std::cout << "  4 Threads / 4 Cores / 4 CCX : "
              << repeats << " Clouds/Block"
              << " (~" << std::fixed << std::setprecision(1)
              << cal_ms << " ms)"
              << " = " << std::setprecision(2)
              << (cal_ms * 1000.0 / static_cast<double>(repeats))
              << " us/Cloud\n";

    result.samples.reserve(rounds);

    std::cout << "Benchmark laeuft...\n";
    for (int round = 0; round < rounds; ++round) {
        const double total_ms = measure(repeats);
        result.samples.push_back(
            total_ms * 1000.0 / static_cast<double>(repeats));

        if ((round + 1) % 50 == 0 || round + 1 == rounds) {
            std::cout << "\r  Runde " << std::setw(4) << (round + 1)
                      << " / " << rounds << std::flush;
        }
    }
    std::cout << "\n";

    return result;
}

static void print_results(const std::array<CaseResult, 4>& results) {
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
        std::filesystem::current_path() / "result";
    std::filesystem::create_directories(result_dir);

    const std::filesystem::path png_path =
        result_dir / "thread_scaling.png";
    const std::filesystem::path script =
        std::filesystem::temp_directory_path() /
        "lidar_thread_scaling.gp";

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

    auto emit_layout = [&]() {
        gp << "set multiplot layout 6,2 rowsfirst title "
              "'LiDAR Thread Scaling - same CCX vs different CCX' font ',14'\n";
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
            gp << "set autoscale x\n";
            gp << "set autoscale y\n";
            gp << "plot $H" << plot_block++
               << " using 1:2 with boxes\n";
        }

        gp << "unset multiplot\n";
    };

    // Save a persistent result image.
    gp << "set term pngcairo size 1920,1900 enhanced font 'Segoe UI,9'\n";
    gp << "set output '" << png_path.generic_string() << "'\n";
    emit_layout();
    gp << "unset output\n\n";

    // Keep the interactive Gnuplot window too.
    gp << "set term qt size 1600,1900 enhanced font 'Segoe UI,9'\n";
    emit_layout();

    gp.close();

    std::cout << "PNG gespeichert: " << png_path << "\n";
    std::cout << "Gnuplot-Skript: " << script << "\n";

    const std::string cmd =
        "gnuplot -persist \"" + script.string() + "\"";
    std::system(cmd.c_str());
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
              << "Version       : 4CCX-800K-v1\n"
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

    // 200k: only the two new threading cases. The single-thread reference
    // already exists in the uop benchmark.
    auto r200 = run_size(200'000, rounds, target_ms, cpus, t, false);

    // 400k: fresh 1-thread baseline + SMT + 2 physical cores.
    auto r400 = run_size(400'000, rounds, target_ms, cpus, t, true);

    // 800k: double the workload again. This helps reveal whether the fixed
    // barrier/synchronization overhead becomes less important as useful work grows.
    auto r800 = run_size(800'000, rounds, target_ms, cpus, t, true);

    // Final topology test: ONLY for 800k points.
    auto r800_four_ccx = run_800k_four_ccx(rounds, target_ms, cpus, t);

    std::cout << "\n=== 200k Threading ===\n";
    print_results(r200);

    std::cout << "\n=== 400k Threading ===\n";
    print_results(r400);

    std::cout << "\n=== 800k Threading ===\n";
    print_results(r800);

    if (!r800_four_ccx.samples.empty()) {
        const Stats s4 = compute_stats(r800_four_ccx.samples);
        std::cout << std::left << std::setw(32)
                  << r800_four_ccx.name
                  << " median " << std::right << std::fixed << std::setprecision(2)
                  << std::setw(8) << s4.median << " us"
                  << " | p95 " << std::setw(8) << s4.p95
                  << " | stddev " << std::setw(8) << s4.stddev << "\n";
    }

    auto print_speedup = [](const char* label,
                            const std::array<CaseResult, 4>& results) {
        if (results[0].samples.empty()) return;

        const double base = compute_stats(results[0].samples).median;
        std::cout << "\n" << label << " Speedup gegen 1 Thread:\n";

        for (int i = 1; i < 4; ++i) {
            if (results[i].samples.empty()) continue;
            const double med = compute_stats(results[i].samples).median;
            std::cout << "  " << std::setw(24) << std::left << results[i].name
                      << ": " << std::fixed << std::setprecision(3)
                      << base / med << "x\n";
        }
    };

    print_speedup("400k", r400);
    print_speedup("800k", r800);

    if (show_plot) {
        std::vector<CaseResult> plot_cases;
        // 200k: SMT + same-CCX + different-CCX.
        plot_cases.push_back(std::move(r200[1]));
        plot_cases.push_back(std::move(r200[2]));
        plot_cases.push_back(std::move(r200[3]));

        // 400k all four.
        plot_cases.push_back(std::move(r400[0]));
        plot_cases.push_back(std::move(r400[1]));
        plot_cases.push_back(std::move(r400[2]));
        plot_cases.push_back(std::move(r400[3]));

        // 800k all four.
        plot_cases.push_back(std::move(r800[0]));
        plot_cases.push_back(std::move(r800[1]));
        plot_cases.push_back(std::move(r800[2]));
        plot_cases.push_back(std::move(r800[3]));
        if (!r800_four_ccx.samples.empty()) {
            plot_cases.push_back(std::move(r800_four_ccx));
        }

        std::cout << "\nOeffne separaten Threading-Plot...\n";
        plot_threading(plot_cases);
    }

    std::cout << "\nSink: " << g_sink << "\n";
    return 0;
}
