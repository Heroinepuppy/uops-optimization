#pragma once

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
#include <sstream>
#include <stdexcept>
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

// AVX2 x2 transformation chain.
__declspec(noinline)
void transform_soa_avx2_x2_chain(const float* __restrict x,
                           const float* __restrict y,
                           const float* __restrict z,
                           float* __restrict ox,
                           float* __restrict oy,
                           float* __restrict oz,
                           std::size_t count,
                           const Transform& t, unsigned transformations) {
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
        __m256 X0 = _mm256_loadu_ps(x + i);
        __m256 Y0 = _mm256_loadu_ps(y + i);
        __m256 Z0 = _mm256_loadu_ps(z + i);
        __m256 X1 = _mm256_loadu_ps(x + i + 8);
        __m256 Y1 = _mm256_loadu_ps(y + i + 8);
        __m256 Z1 = _mm256_loadu_ps(z + i + 8);

        for (unsigned step = 0; step < transformations; ++step) {
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
    
            X0 = Xp0; Y0 = Yp0; Z0 = Zp0;
            X1 = Xp1; Y1 = Yp1; Z1 = Zp1;
        }

        _mm256_storeu_ps(ox + i, X0);
        _mm256_storeu_ps(oy + i, Y0);
        _mm256_storeu_ps(oz + i, Z0);
        _mm256_storeu_ps(ox + i + 8, X1);
        _mm256_storeu_ps(oy + i + 8, Y1);
        _mm256_storeu_ps(oz + i + 8, Z1);
    }

    for (; i < count; ++i) {
        float X = x[i], Y = y[i], Z = z[i];
        for (unsigned step = 0; step < transformations; ++step) {
            const float nx = std::fma(t.r02, Z, std::fma(t.r01, Y, t.r00 * X)) + t.tx;
            const float ny = std::fma(t.r12, Z, std::fma(t.r11, Y, t.r10 * X)) + t.ty;
            const float nz = std::fma(t.r22, Z, std::fma(t.r21, Y, t.r20 * X)) + t.tz;
            X = nx; Y = ny; Z = nz;
        }
        ox[i] = X; oy[i] = Y; oz[i] = Z;
    }
}

// Additional CPU methods.
struct Point { float x, y, z; };

// Original single-transform kernels, shared by the unified runner.
__declspec(noinline)
void transform_aos_scalar(const Point* in, Point* out, std::size_t count,
                          const Transform& t) {
#pragma loop(no_vector)
    for (std::size_t i = 0; i < count; ++i) {
        const float x = in[i].x;
        const float y = in[i].y;
        const float z = in[i].z;

        out[i].x = t.r00 * x + t.r01 * y + t.r02 * z + t.tx;
        out[i].y = t.r10 * x + t.r11 * y + t.r12 * z + t.ty;
        out[i].z = t.r20 * x + t.r21 * y + t.r22 * z + t.tz;
    }
}

__declspec(noinline)
void transform_soa_scalar(const float* __restrict x,
                          const float* __restrict y,
                          const float* __restrict z,
                          float* __restrict ox,
                          float* __restrict oy,
                          float* __restrict oz,
                          std::size_t count,
                          const Transform& t) {
#pragma loop(no_vector)
    for (std::size_t i = 0; i < count; ++i) {
        const float X = x[i];
        const float Y = y[i];
        const float Z = z[i];

        ox[i] = t.r00 * X + t.r01 * Y + t.r02 * Z + t.tx;
        oy[i] = t.r10 * X + t.r11 * Y + t.r12 * Z + t.ty;
        oz[i] = t.r20 * X + t.r21 * Y + t.r22 * Z + t.tz;
    }
}

__declspec(noinline)
void transform_soa_auto(const float* __restrict x,
                        const float* __restrict y,
                        const float* __restrict z,
                        float* __restrict ox,
                        float* __restrict oy,
                        float* __restrict oz,
                        std::size_t count,
                        const Transform& t) {
    for (std::size_t i = 0; i < count; ++i) {
        const float X = x[i];
        const float Y = y[i];
        const float Z = z[i];

        ox[i] = t.r00 * X + t.r01 * Y + t.r02 * Z + t.tx;
        oy[i] = t.r10 * X + t.r11 * Y + t.r12 * Z + t.ty;
        oz[i] = t.r20 * X + t.r21 * Y + t.r22 * Z + t.tz;
    }
}

__declspec(noinline)
void transform_soa_avx2(const float* __restrict x,
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

    for (; i + 8 <= count; i += 8) {
        const __m256 X = _mm256_loadu_ps(x + i);
        const __m256 Y = _mm256_loadu_ps(y + i);
        const __m256 Z = _mm256_loadu_ps(z + i);

        __m256 Xp = _mm256_mul_ps(r00, X);
        Xp = _mm256_fmadd_ps(r01, Y, Xp);
        Xp = _mm256_fmadd_ps(r02, Z, Xp);
        Xp = _mm256_add_ps(Xp, tx);

        __m256 Yp = _mm256_mul_ps(r10, X);
        Yp = _mm256_fmadd_ps(r11, Y, Yp);
        Yp = _mm256_fmadd_ps(r12, Z, Yp);
        Yp = _mm256_add_ps(Yp, ty);

        __m256 Zp = _mm256_mul_ps(r20, X);
        Zp = _mm256_fmadd_ps(r21, Y, Zp);
        Zp = _mm256_fmadd_ps(r22, Z, Zp);
        Zp = _mm256_add_ps(Zp, tz);

        _mm256_storeu_ps(ox + i, Xp);
        _mm256_storeu_ps(oy + i, Yp);
        _mm256_storeu_ps(oz + i, Zp);
    }

#pragma loop(no_vector)
    for (; i < count; ++i) {
        const float X = x[i];
        const float Y = y[i];
        const float Z = z[i];

        ox[i] = t.r00 * X + t.r01 * Y + t.r02 * Z + t.tx;
        oy[i] = t.r10 * X + t.r11 * Y + t.r12 * Z + t.ty;
        oz[i] = t.r20 * X + t.r21 * Y + t.r22 * Z + t.tz;
    }
}


__declspec(noinline)
void transform_soa_avx2_x4(const float* __restrict x,
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

    // Zen-2-oriented variant:
    // 4x unrolling = 32 points per iteration.
    // Multiple independent AVX2/FMA chains provide more instruction-level
    // parallelism so the CPU can hide FMA/load latency more effectively.
    for (; i + 32 <= count; i += 32) {
        // Batch 0
        const __m256 X0 = _mm256_loadu_ps(x + i);
        const __m256 Y0 = _mm256_loadu_ps(y + i);
        const __m256 Z0 = _mm256_loadu_ps(z + i);

        // Batch 1
        const __m256 X1 = _mm256_loadu_ps(x + i + 8);
        const __m256 Y1 = _mm256_loadu_ps(y + i + 8);
        const __m256 Z1 = _mm256_loadu_ps(z + i + 8);

        // Start independent chains early.
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

        // Batch 2
        const __m256 X2 = _mm256_loadu_ps(x + i + 16);
        const __m256 Y2 = _mm256_loadu_ps(y + i + 16);
        const __m256 Z2 = _mm256_loadu_ps(z + i + 16);

        // Batch 3
        const __m256 X3 = _mm256_loadu_ps(x + i + 24);
        const __m256 Y3 = _mm256_loadu_ps(y + i + 24);
        const __m256 Z3 = _mm256_loadu_ps(z + i + 24);

        __m256 Xp2 = _mm256_mul_ps(r00, X2);
        __m256 Yp2 = _mm256_mul_ps(r10, X2);
        __m256 Zp2 = _mm256_mul_ps(r20, X2);

        __m256 Xp3 = _mm256_mul_ps(r00, X3);
        __m256 Yp3 = _mm256_mul_ps(r10, X3);
        __m256 Zp3 = _mm256_mul_ps(r20, X3);

        // Finish batches 0/1 while 2/3 are independent and available to scheduler.
        Xp0 = _mm256_fmadd_ps(r02, Z0, Xp0);
        Yp0 = _mm256_fmadd_ps(r12, Z0, Yp0);
        Zp0 = _mm256_fmadd_ps(r22, Z0, Zp0);

        Xp1 = _mm256_fmadd_ps(r02, Z1, Xp1);
        Yp1 = _mm256_fmadd_ps(r12, Z1, Yp1);
        Zp1 = _mm256_fmadd_ps(r22, Z1, Zp1);

        Xp2 = _mm256_fmadd_ps(r01, Y2, Xp2);
        Yp2 = _mm256_fmadd_ps(r11, Y2, Yp2);
        Zp2 = _mm256_fmadd_ps(r21, Y2, Zp2);

        Xp3 = _mm256_fmadd_ps(r01, Y3, Xp3);
        Yp3 = _mm256_fmadd_ps(r11, Y3, Yp3);
        Zp3 = _mm256_fmadd_ps(r21, Y3, Zp3);

        Xp0 = _mm256_add_ps(Xp0, tx);
        Yp0 = _mm256_add_ps(Yp0, ty);
        Zp0 = _mm256_add_ps(Zp0, tz);

        Xp1 = _mm256_add_ps(Xp1, tx);
        Yp1 = _mm256_add_ps(Yp1, ty);
        Zp1 = _mm256_add_ps(Zp1, tz);

        Xp2 = _mm256_fmadd_ps(r02, Z2, Xp2);
        Yp2 = _mm256_fmadd_ps(r12, Z2, Yp2);
        Zp2 = _mm256_fmadd_ps(r22, Z2, Zp2);

        Xp3 = _mm256_fmadd_ps(r02, Z3, Xp3);
        Yp3 = _mm256_fmadd_ps(r12, Z3, Yp3);
        Zp3 = _mm256_fmadd_ps(r22, Z3, Zp3);

        _mm256_storeu_ps(ox + i,      Xp0);
        _mm256_storeu_ps(oy + i,      Yp0);
        _mm256_storeu_ps(oz + i,      Zp0);
        _mm256_storeu_ps(ox + i + 8,  Xp1);
        _mm256_storeu_ps(oy + i + 8,  Yp1);
        _mm256_storeu_ps(oz + i + 8,  Zp1);

        Xp2 = _mm256_add_ps(Xp2, tx);
        Yp2 = _mm256_add_ps(Yp2, ty);
        Zp2 = _mm256_add_ps(Zp2, tz);

        Xp3 = _mm256_add_ps(Xp3, tx);
        Yp3 = _mm256_add_ps(Yp3, ty);
        Zp3 = _mm256_add_ps(Zp3, tz);

        _mm256_storeu_ps(ox + i + 16, Xp2);
        _mm256_storeu_ps(oy + i + 16, Yp2);
        _mm256_storeu_ps(oz + i + 16, Zp2);
        _mm256_storeu_ps(ox + i + 24, Xp3);
        _mm256_storeu_ps(oy + i + 24, Yp3);
        _mm256_storeu_ps(oz + i + 24, Zp3);
    }

    // Reuse the normal AVX2 version for the tail.
    if (i < count) {
        transform_soa_avx2(
            x + i, y + i, z + i,
            ox + i, oy + i, oz + i,
            count - i, t
        );
    }
}


__declspec(noinline)
void transform_soa_avx2_x8(const float* __restrict x,
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

    // 8x unrolling = 64 points per iteration.
    // This intentionally pushes register pressure harder than x4.
    for (; i + 64 <= count; i += 64) {
        __m256 X[8], Y[8], Z[8];
        __m256 Xp[8], Yp[8], Zp[8];

        for (int b = 0; b < 8; ++b) {
            const std::size_t off = i + static_cast<std::size_t>(b) * 8;
            X[b] = _mm256_loadu_ps(x + off);
            Y[b] = _mm256_loadu_ps(y + off);
            Z[b] = _mm256_loadu_ps(z + off);

            Xp[b] = _mm256_mul_ps(r00, X[b]);
            Yp[b] = _mm256_mul_ps(r10, X[b]);
            Zp[b] = _mm256_mul_ps(r20, X[b]);
        }

        for (int b = 0; b < 8; ++b) {
            Xp[b] = _mm256_fmadd_ps(r01, Y[b], Xp[b]);
            Yp[b] = _mm256_fmadd_ps(r11, Y[b], Yp[b]);
            Zp[b] = _mm256_fmadd_ps(r21, Y[b], Zp[b]);
        }

        for (int b = 0; b < 8; ++b) {
            Xp[b] = _mm256_fmadd_ps(r02, Z[b], Xp[b]);
            Yp[b] = _mm256_fmadd_ps(r12, Z[b], Yp[b]);
            Zp[b] = _mm256_fmadd_ps(r22, Z[b], Zp[b]);
        }

        for (int b = 0; b < 8; ++b) {
            Xp[b] = _mm256_add_ps(Xp[b], tx);
            Yp[b] = _mm256_add_ps(Yp[b], ty);
            Zp[b] = _mm256_add_ps(Zp[b], tz);

            const std::size_t off = i + static_cast<std::size_t>(b) * 8;
            _mm256_storeu_ps(ox + off, Xp[b]);
            _mm256_storeu_ps(oy + off, Yp[b]);
            _mm256_storeu_ps(oz + off, Zp[b]);
        }
    }

    if (i < count) {
        transform_soa_avx2(
            x + i, y + i, z + i,
            ox + i, oy + i, oz + i,
            count - i, t
        );
    }
}


// Repeated transforms stay in registers; only the final result is stored.
template<bool AutoVectorize>
__declspec(noinline) void transform_soa_scalar_chain(
    const float* __restrict x, const float* __restrict y, const float* __restrict z,
    float* __restrict ox, float* __restrict oy, float* __restrict oz,
    std::size_t count, const Transform& t, unsigned k) {
    auto point = [&](std::size_t i) {
        float X=x[i], Y=y[i], Z=z[i];
        for(unsigned step=0;step<k;++step) {
            const float nx=t.r00*X+t.r01*Y+t.r02*Z+t.tx;
            const float ny=t.r10*X+t.r11*Y+t.r12*Z+t.ty;
            const float nz=t.r20*X+t.r21*Y+t.r22*Z+t.tz;
            X=nx; Y=ny; Z=nz;
        }
        ox[i]=X; oy[i]=Y; oz[i]=Z;
    };
    if constexpr (AutoVectorize) {
        for(std::size_t i=0;i<count;++i) point(i);
    } else {
#pragma loop(no_vector)
        for(std::size_t i=0;i<count;++i) point(i);
    }
}

__declspec(noinline) void transform_aos_chain(const Point* in, Point* out,
    std::size_t count, const Transform& t, unsigned k) {
#pragma loop(no_vector)
    for(std::size_t i=0;i<count;++i) {
        float X=in[i].x,Y=in[i].y,Z=in[i].z;
        for(unsigned step=0;step<k;++step) {
            const float nx=t.r00*X+t.r01*Y+t.r02*Z+t.tx;
            const float ny=t.r10*X+t.r11*Y+t.r12*Z+t.ty;
            const float nz=t.r20*X+t.r21*Y+t.r22*Z+t.tz;
            X=nx;Y=ny;Z=nz;
        }
        out[i]={X,Y,Z};
    }
}

__declspec(noinline) void transform_avx2_chain_x1(const float* x,const float* y,const float* z, float* ox,float* oy,float* oz, std::size_t count,const Transform& t,unsigned k) {
    const __m256 r00=_mm256_set1_ps(t.r00);
    const __m256 r01=_mm256_set1_ps(t.r01);
    const __m256 r02=_mm256_set1_ps(t.r02);
    const __m256 r10=_mm256_set1_ps(t.r10);
    const __m256 r11=_mm256_set1_ps(t.r11);
    const __m256 r12=_mm256_set1_ps(t.r12);
    const __m256 r20=_mm256_set1_ps(t.r20);
    const __m256 r21=_mm256_set1_ps(t.r21);
    const __m256 r22=_mm256_set1_ps(t.r22);
    const __m256 tx=_mm256_set1_ps(t.tx);
    const __m256 ty=_mm256_set1_ps(t.ty);
    const __m256 tz=_mm256_set1_ps(t.tz);
    std::size_t i=0;
    for(;i+8<=count;i+=8) {
        __m256 X0=_mm256_loadu_ps(x+i+0);
        __m256 Y0=_mm256_loadu_ps(y+i+0);
        __m256 Z0=_mm256_loadu_ps(z+i+0);
        for(unsigned step=0;step<k;++step) {
            __m256 Xp0=_mm256_mul_ps(r00,X0);
            __m256 Yp0=_mm256_mul_ps(r10,X0);
            __m256 Zp0=_mm256_mul_ps(r20,X0);
            Xp0=_mm256_fmadd_ps(r01,Y0,Xp0);
            Yp0=_mm256_fmadd_ps(r11,Y0,Yp0);
            Zp0=_mm256_fmadd_ps(r21,Y0,Zp0);
            Xp0=_mm256_fmadd_ps(r02,Z0,Xp0);
            Yp0=_mm256_fmadd_ps(r12,Z0,Yp0);
            Zp0=_mm256_fmadd_ps(r22,Z0,Zp0);
            X0=_mm256_add_ps(Xp0,tx);
            Y0=_mm256_add_ps(Yp0,ty);
            Z0=_mm256_add_ps(Zp0,tz);
        }
        _mm256_storeu_ps(ox+i+0,X0);
        _mm256_storeu_ps(oy+i+0,Y0);
        _mm256_storeu_ps(oz+i+0,Z0);
    }
    if(i<count) transform_soa_avx2_x2_chain(x+i,y+i,z+i,ox+i,oy+i,oz+i,count-i,t,k);
}

__declspec(noinline) void transform_avx2_chain_x4(const float* x,const float* y,const float* z, float* ox,float* oy,float* oz, std::size_t count,const Transform& t,unsigned k) {
    const __m256 r00=_mm256_set1_ps(t.r00);
    const __m256 r01=_mm256_set1_ps(t.r01);
    const __m256 r02=_mm256_set1_ps(t.r02);
    const __m256 r10=_mm256_set1_ps(t.r10);
    const __m256 r11=_mm256_set1_ps(t.r11);
    const __m256 r12=_mm256_set1_ps(t.r12);
    const __m256 r20=_mm256_set1_ps(t.r20);
    const __m256 r21=_mm256_set1_ps(t.r21);
    const __m256 r22=_mm256_set1_ps(t.r22);
    const __m256 tx=_mm256_set1_ps(t.tx);
    const __m256 ty=_mm256_set1_ps(t.ty);
    const __m256 tz=_mm256_set1_ps(t.tz);
    std::size_t i=0;
    for(;i+32<=count;i+=32) {
        __m256 X0=_mm256_loadu_ps(x+i+0);
        __m256 Y0=_mm256_loadu_ps(y+i+0);
        __m256 Z0=_mm256_loadu_ps(z+i+0);
        __m256 X1=_mm256_loadu_ps(x+i+8);
        __m256 Y1=_mm256_loadu_ps(y+i+8);
        __m256 Z1=_mm256_loadu_ps(z+i+8);
        __m256 X2=_mm256_loadu_ps(x+i+16);
        __m256 Y2=_mm256_loadu_ps(y+i+16);
        __m256 Z2=_mm256_loadu_ps(z+i+16);
        __m256 X3=_mm256_loadu_ps(x+i+24);
        __m256 Y3=_mm256_loadu_ps(y+i+24);
        __m256 Z3=_mm256_loadu_ps(z+i+24);
        for(unsigned step=0;step<k;++step) {
            __m256 Xp0=_mm256_mul_ps(r00,X0);
            __m256 Yp0=_mm256_mul_ps(r10,X0);
            __m256 Zp0=_mm256_mul_ps(r20,X0);
            __m256 Xp1=_mm256_mul_ps(r00,X1);
            __m256 Yp1=_mm256_mul_ps(r10,X1);
            __m256 Zp1=_mm256_mul_ps(r20,X1);
            __m256 Xp2=_mm256_mul_ps(r00,X2);
            __m256 Yp2=_mm256_mul_ps(r10,X2);
            __m256 Zp2=_mm256_mul_ps(r20,X2);
            __m256 Xp3=_mm256_mul_ps(r00,X3);
            __m256 Yp3=_mm256_mul_ps(r10,X3);
            __m256 Zp3=_mm256_mul_ps(r20,X3);
            Xp0=_mm256_fmadd_ps(r01,Y0,Xp0);
            Yp0=_mm256_fmadd_ps(r11,Y0,Yp0);
            Zp0=_mm256_fmadd_ps(r21,Y0,Zp0);
            Xp1=_mm256_fmadd_ps(r01,Y1,Xp1);
            Yp1=_mm256_fmadd_ps(r11,Y1,Yp1);
            Zp1=_mm256_fmadd_ps(r21,Y1,Zp1);
            Xp2=_mm256_fmadd_ps(r01,Y2,Xp2);
            Yp2=_mm256_fmadd_ps(r11,Y2,Yp2);
            Zp2=_mm256_fmadd_ps(r21,Y2,Zp2);
            Xp3=_mm256_fmadd_ps(r01,Y3,Xp3);
            Yp3=_mm256_fmadd_ps(r11,Y3,Yp3);
            Zp3=_mm256_fmadd_ps(r21,Y3,Zp3);
            Xp0=_mm256_fmadd_ps(r02,Z0,Xp0);
            Yp0=_mm256_fmadd_ps(r12,Z0,Yp0);
            Zp0=_mm256_fmadd_ps(r22,Z0,Zp0);
            Xp1=_mm256_fmadd_ps(r02,Z1,Xp1);
            Yp1=_mm256_fmadd_ps(r12,Z1,Yp1);
            Zp1=_mm256_fmadd_ps(r22,Z1,Zp1);
            Xp2=_mm256_fmadd_ps(r02,Z2,Xp2);
            Yp2=_mm256_fmadd_ps(r12,Z2,Yp2);
            Zp2=_mm256_fmadd_ps(r22,Z2,Zp2);
            Xp3=_mm256_fmadd_ps(r02,Z3,Xp3);
            Yp3=_mm256_fmadd_ps(r12,Z3,Yp3);
            Zp3=_mm256_fmadd_ps(r22,Z3,Zp3);
            X0=_mm256_add_ps(Xp0,tx);
            Y0=_mm256_add_ps(Yp0,ty);
            Z0=_mm256_add_ps(Zp0,tz);
            X1=_mm256_add_ps(Xp1,tx);
            Y1=_mm256_add_ps(Yp1,ty);
            Z1=_mm256_add_ps(Zp1,tz);
            X2=_mm256_add_ps(Xp2,tx);
            Y2=_mm256_add_ps(Yp2,ty);
            Z2=_mm256_add_ps(Zp2,tz);
            X3=_mm256_add_ps(Xp3,tx);
            Y3=_mm256_add_ps(Yp3,ty);
            Z3=_mm256_add_ps(Zp3,tz);
        }
        _mm256_storeu_ps(ox+i+0,X0);
        _mm256_storeu_ps(oy+i+0,Y0);
        _mm256_storeu_ps(oz+i+0,Z0);
        _mm256_storeu_ps(ox+i+8,X1);
        _mm256_storeu_ps(oy+i+8,Y1);
        _mm256_storeu_ps(oz+i+8,Z1);
        _mm256_storeu_ps(ox+i+16,X2);
        _mm256_storeu_ps(oy+i+16,Y2);
        _mm256_storeu_ps(oz+i+16,Z2);
        _mm256_storeu_ps(ox+i+24,X3);
        _mm256_storeu_ps(oy+i+24,Y3);
        _mm256_storeu_ps(oz+i+24,Z3);
    }
    if(i<count) transform_soa_avx2_x2_chain(x+i,y+i,z+i,ox+i,oy+i,oz+i,count-i,t,k);
}

__declspec(noinline) void transform_avx2_chain_x8(const float* x,const float* y,const float* z, float* ox,float* oy,float* oz, std::size_t count,const Transform& t,unsigned k) {
    const __m256 r00=_mm256_set1_ps(t.r00);
    const __m256 r01=_mm256_set1_ps(t.r01);
    const __m256 r02=_mm256_set1_ps(t.r02);
    const __m256 r10=_mm256_set1_ps(t.r10);
    const __m256 r11=_mm256_set1_ps(t.r11);
    const __m256 r12=_mm256_set1_ps(t.r12);
    const __m256 r20=_mm256_set1_ps(t.r20);
    const __m256 r21=_mm256_set1_ps(t.r21);
    const __m256 r22=_mm256_set1_ps(t.r22);
    const __m256 tx=_mm256_set1_ps(t.tx);
    const __m256 ty=_mm256_set1_ps(t.ty);
    const __m256 tz=_mm256_set1_ps(t.tz);
    std::size_t i=0;
    for(;i+64<=count;i+=64) {
        __m256 X0=_mm256_loadu_ps(x+i+0);
        __m256 Y0=_mm256_loadu_ps(y+i+0);
        __m256 Z0=_mm256_loadu_ps(z+i+0);
        __m256 X1=_mm256_loadu_ps(x+i+8);
        __m256 Y1=_mm256_loadu_ps(y+i+8);
        __m256 Z1=_mm256_loadu_ps(z+i+8);
        __m256 X2=_mm256_loadu_ps(x+i+16);
        __m256 Y2=_mm256_loadu_ps(y+i+16);
        __m256 Z2=_mm256_loadu_ps(z+i+16);
        __m256 X3=_mm256_loadu_ps(x+i+24);
        __m256 Y3=_mm256_loadu_ps(y+i+24);
        __m256 Z3=_mm256_loadu_ps(z+i+24);
        __m256 X4=_mm256_loadu_ps(x+i+32);
        __m256 Y4=_mm256_loadu_ps(y+i+32);
        __m256 Z4=_mm256_loadu_ps(z+i+32);
        __m256 X5=_mm256_loadu_ps(x+i+40);
        __m256 Y5=_mm256_loadu_ps(y+i+40);
        __m256 Z5=_mm256_loadu_ps(z+i+40);
        __m256 X6=_mm256_loadu_ps(x+i+48);
        __m256 Y6=_mm256_loadu_ps(y+i+48);
        __m256 Z6=_mm256_loadu_ps(z+i+48);
        __m256 X7=_mm256_loadu_ps(x+i+56);
        __m256 Y7=_mm256_loadu_ps(y+i+56);
        __m256 Z7=_mm256_loadu_ps(z+i+56);
        for(unsigned step=0;step<k;++step) {
            __m256 Xp0=_mm256_mul_ps(r00,X0);
            __m256 Yp0=_mm256_mul_ps(r10,X0);
            __m256 Zp0=_mm256_mul_ps(r20,X0);
            __m256 Xp1=_mm256_mul_ps(r00,X1);
            __m256 Yp1=_mm256_mul_ps(r10,X1);
            __m256 Zp1=_mm256_mul_ps(r20,X1);
            __m256 Xp2=_mm256_mul_ps(r00,X2);
            __m256 Yp2=_mm256_mul_ps(r10,X2);
            __m256 Zp2=_mm256_mul_ps(r20,X2);
            __m256 Xp3=_mm256_mul_ps(r00,X3);
            __m256 Yp3=_mm256_mul_ps(r10,X3);
            __m256 Zp3=_mm256_mul_ps(r20,X3);
            __m256 Xp4=_mm256_mul_ps(r00,X4);
            __m256 Yp4=_mm256_mul_ps(r10,X4);
            __m256 Zp4=_mm256_mul_ps(r20,X4);
            __m256 Xp5=_mm256_mul_ps(r00,X5);
            __m256 Yp5=_mm256_mul_ps(r10,X5);
            __m256 Zp5=_mm256_mul_ps(r20,X5);
            __m256 Xp6=_mm256_mul_ps(r00,X6);
            __m256 Yp6=_mm256_mul_ps(r10,X6);
            __m256 Zp6=_mm256_mul_ps(r20,X6);
            __m256 Xp7=_mm256_mul_ps(r00,X7);
            __m256 Yp7=_mm256_mul_ps(r10,X7);
            __m256 Zp7=_mm256_mul_ps(r20,X7);
            Xp0=_mm256_fmadd_ps(r01,Y0,Xp0);
            Yp0=_mm256_fmadd_ps(r11,Y0,Yp0);
            Zp0=_mm256_fmadd_ps(r21,Y0,Zp0);
            Xp1=_mm256_fmadd_ps(r01,Y1,Xp1);
            Yp1=_mm256_fmadd_ps(r11,Y1,Yp1);
            Zp1=_mm256_fmadd_ps(r21,Y1,Zp1);
            Xp2=_mm256_fmadd_ps(r01,Y2,Xp2);
            Yp2=_mm256_fmadd_ps(r11,Y2,Yp2);
            Zp2=_mm256_fmadd_ps(r21,Y2,Zp2);
            Xp3=_mm256_fmadd_ps(r01,Y3,Xp3);
            Yp3=_mm256_fmadd_ps(r11,Y3,Yp3);
            Zp3=_mm256_fmadd_ps(r21,Y3,Zp3);
            Xp4=_mm256_fmadd_ps(r01,Y4,Xp4);
            Yp4=_mm256_fmadd_ps(r11,Y4,Yp4);
            Zp4=_mm256_fmadd_ps(r21,Y4,Zp4);
            Xp5=_mm256_fmadd_ps(r01,Y5,Xp5);
            Yp5=_mm256_fmadd_ps(r11,Y5,Yp5);
            Zp5=_mm256_fmadd_ps(r21,Y5,Zp5);
            Xp6=_mm256_fmadd_ps(r01,Y6,Xp6);
            Yp6=_mm256_fmadd_ps(r11,Y6,Yp6);
            Zp6=_mm256_fmadd_ps(r21,Y6,Zp6);
            Xp7=_mm256_fmadd_ps(r01,Y7,Xp7);
            Yp7=_mm256_fmadd_ps(r11,Y7,Yp7);
            Zp7=_mm256_fmadd_ps(r21,Y7,Zp7);
            Xp0=_mm256_fmadd_ps(r02,Z0,Xp0);
            Yp0=_mm256_fmadd_ps(r12,Z0,Yp0);
            Zp0=_mm256_fmadd_ps(r22,Z0,Zp0);
            Xp1=_mm256_fmadd_ps(r02,Z1,Xp1);
            Yp1=_mm256_fmadd_ps(r12,Z1,Yp1);
            Zp1=_mm256_fmadd_ps(r22,Z1,Zp1);
            Xp2=_mm256_fmadd_ps(r02,Z2,Xp2);
            Yp2=_mm256_fmadd_ps(r12,Z2,Yp2);
            Zp2=_mm256_fmadd_ps(r22,Z2,Zp2);
            Xp3=_mm256_fmadd_ps(r02,Z3,Xp3);
            Yp3=_mm256_fmadd_ps(r12,Z3,Yp3);
            Zp3=_mm256_fmadd_ps(r22,Z3,Zp3);
            Xp4=_mm256_fmadd_ps(r02,Z4,Xp4);
            Yp4=_mm256_fmadd_ps(r12,Z4,Yp4);
            Zp4=_mm256_fmadd_ps(r22,Z4,Zp4);
            Xp5=_mm256_fmadd_ps(r02,Z5,Xp5);
            Yp5=_mm256_fmadd_ps(r12,Z5,Yp5);
            Zp5=_mm256_fmadd_ps(r22,Z5,Zp5);
            Xp6=_mm256_fmadd_ps(r02,Z6,Xp6);
            Yp6=_mm256_fmadd_ps(r12,Z6,Yp6);
            Zp6=_mm256_fmadd_ps(r22,Z6,Zp6);
            Xp7=_mm256_fmadd_ps(r02,Z7,Xp7);
            Yp7=_mm256_fmadd_ps(r12,Z7,Yp7);
            Zp7=_mm256_fmadd_ps(r22,Z7,Zp7);
            X0=_mm256_add_ps(Xp0,tx);
            Y0=_mm256_add_ps(Yp0,ty);
            Z0=_mm256_add_ps(Zp0,tz);
            X1=_mm256_add_ps(Xp1,tx);
            Y1=_mm256_add_ps(Yp1,ty);
            Z1=_mm256_add_ps(Zp1,tz);
            X2=_mm256_add_ps(Xp2,tx);
            Y2=_mm256_add_ps(Yp2,ty);
            Z2=_mm256_add_ps(Zp2,tz);
            X3=_mm256_add_ps(Xp3,tx);
            Y3=_mm256_add_ps(Yp3,ty);
            Z3=_mm256_add_ps(Zp3,tz);
            X4=_mm256_add_ps(Xp4,tx);
            Y4=_mm256_add_ps(Yp4,ty);
            Z4=_mm256_add_ps(Zp4,tz);
            X5=_mm256_add_ps(Xp5,tx);
            Y5=_mm256_add_ps(Yp5,ty);
            Z5=_mm256_add_ps(Zp5,tz);
            X6=_mm256_add_ps(Xp6,tx);
            Y6=_mm256_add_ps(Yp6,ty);
            Z6=_mm256_add_ps(Zp6,tz);
            X7=_mm256_add_ps(Xp7,tx);
            Y7=_mm256_add_ps(Yp7,ty);
            Z7=_mm256_add_ps(Zp7,tz);
        }
        _mm256_storeu_ps(ox+i+0,X0);
        _mm256_storeu_ps(oy+i+0,Y0);
        _mm256_storeu_ps(oz+i+0,Z0);
        _mm256_storeu_ps(ox+i+8,X1);
        _mm256_storeu_ps(oy+i+8,Y1);
        _mm256_storeu_ps(oz+i+8,Z1);
        _mm256_storeu_ps(ox+i+16,X2);
        _mm256_storeu_ps(oy+i+16,Y2);
        _mm256_storeu_ps(oz+i+16,Z2);
        _mm256_storeu_ps(ox+i+24,X3);
        _mm256_storeu_ps(oy+i+24,Y3);
        _mm256_storeu_ps(oz+i+24,Z3);
        _mm256_storeu_ps(ox+i+32,X4);
        _mm256_storeu_ps(oy+i+32,Y4);
        _mm256_storeu_ps(oz+i+32,Z4);
        _mm256_storeu_ps(ox+i+40,X5);
        _mm256_storeu_ps(oy+i+40,Y5);
        _mm256_storeu_ps(oz+i+40,Z5);
        _mm256_storeu_ps(ox+i+48,X6);
        _mm256_storeu_ps(oy+i+48,Y6);
        _mm256_storeu_ps(oz+i+48,Z6);
        _mm256_storeu_ps(ox+i+56,X7);
        _mm256_storeu_ps(oy+i+56,Y7);
        _mm256_storeu_ps(oz+i+56,Z7);
    }
    if(i<count) transform_soa_avx2_x2_chain(x+i,y+i,z+i,ox+i,oy+i,oz+i,count-i,t,k);
}
