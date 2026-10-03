#pragma once
#include "lidar_cpu_core.h"

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
