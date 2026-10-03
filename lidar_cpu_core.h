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

