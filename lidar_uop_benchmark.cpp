#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <immintrin.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

constexpr std::size_t POINT_COUNT = 200'000;

struct Point {
    float x;
    float y;
    float z;
};

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

static void pin_current_thread(unsigned logical_cpu) {
    const DWORD_PTR mask = (DWORD_PTR{1} << logical_cpu);
    if (SetThreadAffinityMask(GetCurrentThread(), mask) == 0) {
        std::cerr << "WARNUNG: CPU-Affinitaet konnte nicht gesetzt werden.\n";
    }

    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) {
        std::cerr << "WARNUNG: Thread-Prioritaet konnte nicht erhoeht werden.\n";
    }
}

__declspec(noinline)
void transform_aos_scalar(const std::vector<Point>& in,
                          std::vector<Point>& out,
                          const Transform& t) {
#pragma loop(no_vector)
    for (std::size_t i = 0; i < in.size(); ++i) {
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

enum class Method : int {
    AosScalar = 0,
    SoaScalar = 1,
    SoaAuto   = 2,
    Avx2      = 3,
    Avx2x2    = 4,
    Avx2x4    = 5,
    Avx2x8    = 6
};

constexpr std::array<const char*, 7> METHOD_NAMES = {
    "AoS scalar",
    "SoA scalar",
    "SoA auto",
    "AVX2/FMA",
    "AVX2 Zen2 x2",
    "AVX2 Zen2 x4",
    "AVX2 Zen2 x8"
};

struct BenchContext {
    std::vector<Point> aos_in;
    std::vector<Point> aos_out;
    SoA soa;
    Transform t;

    BenchContext(std::size_t n)
        : aos_in(n), aos_out(n), soa(n) {}
};

static void run_once(Method method, BenchContext& ctx) {
    switch (method) {
    case Method::AosScalar:
        transform_aos_scalar(ctx.aos_in, ctx.aos_out, ctx.t);
        g_sink = g_sink + ctx.aos_out[POINT_COUNT / 2].x;
        break;

    case Method::SoaScalar:
        transform_soa_scalar(
            ctx.soa.x.data(), ctx.soa.y.data(), ctx.soa.z.data(),
            ctx.soa.ox.data(), ctx.soa.oy.data(), ctx.soa.oz.data(),
            POINT_COUNT, ctx.t);
        g_sink = g_sink + ctx.soa.ox[POINT_COUNT / 2];
        break;

    case Method::SoaAuto:
        transform_soa_auto(
            ctx.soa.x.data(), ctx.soa.y.data(), ctx.soa.z.data(),
            ctx.soa.ox.data(), ctx.soa.oy.data(), ctx.soa.oz.data(),
            POINT_COUNT, ctx.t);
        g_sink = g_sink + ctx.soa.ox[POINT_COUNT / 2];
        break;

    case Method::Avx2:
        transform_soa_avx2(
            ctx.soa.x.data(), ctx.soa.y.data(), ctx.soa.z.data(),
            ctx.soa.ox.data(), ctx.soa.oy.data(), ctx.soa.oz.data(),
            POINT_COUNT, ctx.t);
        g_sink = g_sink + ctx.soa.ox[POINT_COUNT / 2];
        break;

    case Method::Avx2x2:
        transform_soa_avx2_x2(
            ctx.soa.x.data(), ctx.soa.y.data(), ctx.soa.z.data(),
            ctx.soa.ox.data(), ctx.soa.oy.data(), ctx.soa.oz.data(),
            POINT_COUNT, ctx.t);
        g_sink = g_sink + ctx.soa.ox[POINT_COUNT / 2];
        break;

    case Method::Avx2x4:
        transform_soa_avx2_x4(
            ctx.soa.x.data(), ctx.soa.y.data(), ctx.soa.z.data(),
            ctx.soa.ox.data(), ctx.soa.oy.data(), ctx.soa.oz.data(),
            POINT_COUNT, ctx.t);
        g_sink = g_sink + ctx.soa.ox[POINT_COUNT / 2];
        break;

    case Method::Avx2x8:
        transform_soa_avx2_x8(
            ctx.soa.x.data(), ctx.soa.y.data(), ctx.soa.z.data(),
            ctx.soa.ox.data(), ctx.soa.oy.data(), ctx.soa.oz.data(),
            POINT_COUNT, ctx.t);
        g_sink = g_sink + ctx.soa.ox[POINT_COUNT / 2];
        break;
    }
}

static double measure_block_ms(Method method, BenchContext& ctx, std::size_t repeats) {
    using clock = std::chrono::steady_clock;
    const auto begin = clock::now();

    for (std::size_t i = 0; i < repeats; ++i) {
        run_once(method, ctx);
    }

    const auto end = clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

static std::size_t calibrate_repeats(Method method,
                                     BenchContext& ctx,
                                     double target_block_ms) {
    for (int i = 0; i < 8; ++i) {
        run_once(method, ctx);
    }

    std::size_t repeats = 1;

    while (repeats < 100000) {
        const double ms = measure_block_ms(method, ctx, repeats);
        if (ms >= 5.0) {
            const double estimated =
                static_cast<double>(repeats) * target_block_ms / ms;
            return std::max<std::size_t>(
                1,
                static_cast<std::size_t>(std::llround(estimated)));
        }
        repeats *= 2;
    }

    return repeats;
}

struct Stats {
    double minimum{};
    double mean{};
    double median{};
    double p90{};
    double p95{};
    double p99{};
    double stddev{};
};

static double percentile_sorted(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted.front();

    const double pos = p * static_cast<double>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    const double frac = pos - static_cast<double>(lo);

    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

static Stats compute_stats(const std::vector<double>& values) {
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    const double mean =
        std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());

    double variance = 0.0;
    for (const double v : values) {
        const double d = v - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(values.size());

    Stats s;
    s.minimum = sorted.front();
    s.mean    = mean;
    s.median  = percentile_sorted(sorted, 0.50);
    s.p90     = percentile_sorted(sorted, 0.90);
    s.p95     = percentile_sorted(sorted, 0.95);
    s.p99     = percentile_sorted(sorted, 0.99);
    s.stddev  = std::sqrt(variance);
    return s;
}

static void print_stats(const std::array<std::vector<double>, 7>& samples) {
    std::cout << "\nZeit pro 200.000-Punkte-Cloud [us]\n\n";
    std::cout
        << std::left  << std::setw(15) << "Methode"
        << std::right << std::setw(11) << "Min"
        << std::setw(11) << "Mean"
        << std::setw(11) << "Median"
        << std::setw(11) << "p90"
        << std::setw(11) << "p95"
        << std::setw(11) << "p99"
        << std::setw(11) << "StdDev"
        << '\n';

    std::cout << std::string(92, '-') << '\n';

    std::array<Stats, 7> all_stats{};

    for (std::size_t i = 0; i < samples.size(); ++i) {
        const Stats s = compute_stats(samples[i]);
        all_stats[i] = s;

        std::cout
            << std::left  << std::setw(15) << METHOD_NAMES[i]
            << std::right << std::fixed << std::setprecision(2)
            << std::setw(11) << s.minimum
            << std::setw(11) << s.mean
            << std::setw(11) << s.median
            << std::setw(11) << s.p90
            << std::setw(11) << s.p95
            << std::setw(11) << s.p99
            << std::setw(11) << s.stddev
            << '\n';
    }

    const double baseline = all_stats[0].median;

    std::cout << "\nMedian-Speedup gegen AoS scalar:\n";
    for (std::size_t i = 1; i < all_stats.size(); ++i) {
        std::cout
            << "  " << std::setw(12) << std::left << METHOD_NAMES[i]
            << ": " << std::fixed << std::setprecision(3)
            << baseline / all_stats[i].median << "x\n";
    }

    std::cout << "\nDurchsatz aus Median:\n";
    for (std::size_t i = 0; i < all_stats.size(); ++i) {
        const double seconds = all_stats[i].median * 1e-6;
        const double mpoints = (POINT_COUNT / seconds) / 1e6;

        std::cout
            << "  " << std::setw(12) << std::left << METHOD_NAMES[i]
            << ": " << std::fixed << std::setprecision(2)
            << mpoints << " Mpoints/s\n";
    }
}

static void plot_histograms(const std::array<std::vector<double>, 7>& samples) {
    const std::filesystem::path result_dir =
        std::filesystem::path("results") / "data";
    std::filesystem::create_directories(result_dir);
    std::filesystem::create_directories(result_dir.parent_path() / "pics");

    const std::filesystem::path png_path =
        result_dir.parent_path() / "pics" / "uop_benchmark.png";
    const std::filesystem::path script_path =
        result_dir / "uop_benchmark.gp";

    std::ofstream gp(script_path, std::ios::trunc);
    if (!gp) {
        std::cerr << "Gnuplot-Skript konnte nicht geschrieben werden.\n";
        return;
    }

    constexpr int bins = 60;

    for (std::size_t method = 0; method < samples.size(); ++method) {
        const auto& values = samples[method];
        const auto [min_it, max_it] =
            std::minmax_element(values.begin(), values.end());

        double min_v = *min_it;
        double max_v = *max_it;
        if (max_v <= min_v) max_v = min_v + 1.0;

        const double width =
            (max_v - min_v) / static_cast<double>(bins);

        std::array<std::size_t, bins> counts{};

        for (double v : values) {
            int bin = static_cast<int>((v - min_v) / width);
            bin = std::clamp(bin, 0, bins - 1);
            counts[bin]++;
        }

        gp << "$H" << method << " << EOD\n";
        for (int i = 0; i < bins; ++i) {
            const double center =
                min_v + (static_cast<double>(i) + 0.5) * width;
            gp << std::fixed << std::setprecision(9)
               << center << " " << counts[i] << "\n";
        }
        gp << "EOD\n\n";
    }

    auto emit_layout = [&]() {
        gp << "set multiplot layout 4,2 rowsfirst title "
              "'LiDAR SIMD Benchmark - Unrolling' font ',14'\n";

        // Restore plot frame/tics explicitly. The methods panel below unsets
        // them; without resetting them here, the second rendering (qt after
        // pngcairo) inherits that state and loses the boxes/axis ticks.
        gp << "set border\n";
        gp << "set xtics\n";
        gp << "set ytics\n";
        gp << "set tics out\n";

        gp << "set style fill solid 0.70 border -1\n";
        gp << "set boxwidth 0.90 relative\n";
        gp << "set grid ytics\n";
        gp << "set key off\n";

        for (std::size_t method = 0; method < samples.size(); ++method) {
            gp << "set title '" << METHOD_NAMES[method] << "'\n";
            gp << "set xlabel 'us pro 200k-Punkte-Cloud'\n";
            gp << "set ylabel 'Anzahl Messbloecke'\n";
            gp << "set autoscale x\n";
            gp << "set autoscale y\n";
            gp << "plot $H" << method << " using 1:2 with boxes\n";
        }

        gp << "unset grid\n";
        gp << "unset border\n";
        gp << "unset xtics\n";
        gp << "unset ytics\n";
        gp << "unset xlabel\n";
        gp << "unset ylabel\n";
        gp << "set title 'Methoden'\n";
        gp << "set xrange [0:1]\n";
        gp << "set yrange [0:1]\n";

        const char* labels[] = {
            "1. AoS scalar", "AoS, skalar",
            "2. SoA scalar", "SoA, Vektorisierung aus",
            "3. SoA auto", "SoA, Compiler vektorisiert",
            "4. AVX2/FMA", "8 Punkte/Iteration",
            "5. AVX2 Zen2 x2", "16 Punkte/Iteration; 2x Unrolling",
            "6. AVX2 Zen2 x4", "32 Punkte/Iteration; 4x Unrolling",
            "7. AVX2 Zen2 x8",
            "64 Punkte/Iteration; 8x Unrolling; hoher Registerdruck"
        };

        double y = 0.96;
        int id = 1;

        for (int i = 0; i < 14; i += 2) {
            gp << "set label " << id++ << " '" << labels[i]
               << "' at graph 0.02," << y
               << " left front font ',8'\n";

            gp << "set label " << id++ << " '" << labels[i + 1]
               << "' at graph 0.05," << (y - 0.055)
               << " left front font ',8'\n";

            y -= 0.135;
        }

        gp << "plot NaN notitle\n";

        for (int i = 1; i < id; ++i)
            gp << "unset label " << i << "\n";

        gp << "unset title\n";
        gp << "unset multiplot\n";
    };

    // PNG first.
    gp << "set term pngcairo size 1920,1400 enhanced font 'Segoe UI,9'\n";
    gp << "set output '" << png_path.generic_string() << "'\n";
    emit_layout();
    gp << "unset output\n\n";

    // Then open the interactive window as before.
    if (!std::getenv("LIDAR_BATCH")) {
    gp << "set term qt size 1600,1100 enhanced font 'Segoe UI,9'\n";
    emit_layout();

    }
    gp.close();

    std::cout << "PNG gespeichert: " << png_path << "\n";
    std::cout << "Gnuplot-Skript: " << script_path << "\n";

    const std::string cmd =
        "gnuplot -persist \"" + script_path.string() + "\"";
    std::system(cmd.c_str());
}

int main(int argc, char** argv) {
    unsigned logical_cpu = 4;
    int rounds = 600;
    double target_block_ms = 50.0;
    bool show_plot = true;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--no-plot") {
            show_plot = false;
        } else if (i == 1) {
            rounds = std::max(20, std::stoi(arg));
        } else if (i == 2) {
            target_block_ms = std::max(5.0, std::stod(arg));
        } else if (i == 3) {
            logical_cpu = static_cast<unsigned>(std::stoul(arg));
        }
    }

    std::cout
        << "LiDAR Transform Benchmark\n"
        << "-------------------------\n"
        << "Version       : UNROLL-7TEST-v3\n"
        << "Testanzahl    : 7\n"
        << "Punkte/Cloud : " << POINT_COUNT << '\n'
        << "Messrunden   : " << rounds << '\n'
        << "Blockziel    : " << target_block_ms << " ms/Methode\n"
        << "Logische CPU : " << logical_cpu << "\n\n";

    pin_current_thread(logical_cpu);

    BenchContext ctx(POINT_COUNT);

    ctx.t = Transform{
         0.8660254038f, -0.5f,          0.0f,
         0.5f,           0.8660254038f, 0.0f,
         0.0f,           0.0f,          1.0f,
         1.25f,         -3.50f,         0.75f
    };

    std::mt19937 rng(0xBADC0DE);
    std::uniform_real_distribution<float> dist(-50.0f, 50.0f);

    for (std::size_t i = 0; i < POINT_COUNT; ++i) {
        const float x = dist(rng);
        const float y = dist(rng);
        const float z = dist(rng);

        ctx.aos_in[i] = Point{x, y, z};
        ctx.soa.x[i] = x;
        ctx.soa.y[i] = y;
        ctx.soa.z[i] = z;
    }

    std::array<std::size_t, 7> repeats{};

    std::cout << "Kalibriere Blocklaengen...\n";

    for (int m = 0; m < 7; ++m) {
        repeats[m] =
            calibrate_repeats(static_cast<Method>(m), ctx, target_block_ms);

        const double ms =
            measure_block_ms(static_cast<Method>(m), ctx, repeats[m]);

        std::cout
            << "  " << std::setw(12) << std::left << METHOD_NAMES[m]
            << ": " << repeats[m] << " Clouds/Block"
            << "  (~" << std::fixed << std::setprecision(1)
            << ms << " ms)\n";
    }

    std::array<std::vector<double>, 7> samples;
    for (auto& v : samples) {
        v.reserve(rounds);
    }

    std::array<Method, 7> order = {
        Method::AosScalar,
        Method::SoaScalar,
        Method::SoaAuto,
        Method::Avx2,
        Method::Avx2x2,
        Method::Avx2x4,
        Method::Avx2x8
    };

    std::mt19937 order_rng(0x3950);

    std::cout << "\nBenchmark laeuft...\n";

    for (int round = 0; round < rounds; ++round) {
        std::shuffle(order.begin(), order.end(), order_rng);

        for (const Method method : order) {
            const std::size_t idx = static_cast<std::size_t>(method);
            const double total_ms =
                measure_block_ms(method, ctx, repeats[idx]);

            const double us_per_cloud =
                (total_ms * 1000.0) /
                static_cast<double>(repeats[idx]);

            samples[idx].push_back(us_per_cloud);
        }

        if ((round + 1) % 50 == 0 || round + 1 == rounds) {
            std::cout << "\r  Runde "
                      << std::setw(4) << (round + 1)
                      << " / " << rounds
                      << std::flush;
        }
    }

    std::cout << "\n";
    print_stats(samples);

    std::cout
        << "\nHinweis: Die Cloud wird wiederholt verarbeitet und liegt daher nach dem\n"
        << "Warm-up weitgehend im Cache. Der Test vergleicht damit primaer Datenlayout,\n"
        << "Compiler-Vektorisierung und AVX2/FMA unter identischen Bedingungen.\n";

    if (show_plot) {
        std::cout << "\nOeffne Histogramme...\n";
        plot_histograms(samples);
    }

    std::cout << "\nSink: " << g_sink << '\n';
    return 0;
}
