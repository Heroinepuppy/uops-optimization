#include <hip/hip_runtime.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <utility>
#include <vector>

#define HIP_CHECK(call)                                                         \
    do {                                                                        \
        hipError_t err__ = (call);                                              \
        if (err__ != hipSuccess) {                                              \
            std::cerr << "HIP error: " << hipGetErrorString(err__)              \
                      << " at " << __FILE__ << ":" << __LINE__ << "\n";          \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

struct Transform {
    float r00, r01, r02;
    float r10, r11, r12;
    float r20, r21, r22;
    float tx, ty, tz;
};

__global__ void transform_kernel(
    const float* __restrict__ x,
    const float* __restrict__ y,
    const float* __restrict__ z,
    float* __restrict__ ox,
    float* __restrict__ oy,
    float* __restrict__ oz,
    std::size_t count,
    Transform t)
{
    const std::size_t i =
        static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;

    if (i >= count) {
        return;
    }

    const float X = x[i];
    const float Y = y[i];
    const float Z = z[i];

    ox[i] = fmaf(t.r02, Z, fmaf(t.r01, Y, fmaf(t.r00, X, t.tx)));
    oy[i] = fmaf(t.r12, Z, fmaf(t.r11, Y, fmaf(t.r10, X, t.ty)));
    oz[i] = fmaf(t.r22, Z, fmaf(t.r21, Y, fmaf(t.r20, X, t.tz)));
}

struct Timing {
    double kernel_us = 0.0;
    double h2d_kernel_us = 0.0;
    double roundtrip_us = 0.0;

    std::vector<double> kernel_samples;
    std::vector<double> h2d_kernel_samples;
    std::vector<double> roundtrip_samples;
    std::size_t invalid_kernel_samples = 0;
};

static double median(std::vector<double> v)
{
    if (v.empty()) {
        return 0.0;
    }

    std::sort(v.begin(), v.end());

    const std::size_t n = v.size();
    if (n % 2 == 1) {
        return v[n / 2];
    }

    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static void fill_points(
    std::vector<float>& x,
    std::vector<float>& y,
    std::vector<float>& z)
{
    std::mt19937 rng(0x7900u);
    std::uniform_real_distribution<float> dist(-50.0f, 50.0f);

    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] = dist(rng);
        y[i] = dist(rng);
        z[i] = dist(rng);
    }
}

static Timing benchmark_size(
    std::size_t count,
    int rounds,
    int warmup_rounds,
    int threads_per_block,
    const Transform& transform)
{
    const std::size_t bytes = count * sizeof(float);

    std::vector<float> x(count);
    std::vector<float> y(count);
    std::vector<float> z(count);
    std::vector<float> ox(count);
    std::vector<float> oy(count);
    std::vector<float> oz(count);

    fill_points(x, y, z);

    float *dx = nullptr, *dy = nullptr, *dz = nullptr;
    float *dox = nullptr, *doy = nullptr, *doz = nullptr;

    HIP_CHECK(hipMalloc(&dx, bytes));
    HIP_CHECK(hipMalloc(&dy, bytes));
    HIP_CHECK(hipMalloc(&dz, bytes));
    HIP_CHECK(hipMalloc(&dox, bytes));
    HIP_CHECK(hipMalloc(&doy, bytes));
    HIP_CHECK(hipMalloc(&doz, bytes));

    HIP_CHECK(hipMemcpy(dx, x.data(), bytes, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(dy, y.data(), bytes, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(dz, z.data(), bytes, hipMemcpyHostToDevice));

    const int blocks =
        static_cast<int>((count + threads_per_block - 1) / threads_per_block);

    // Warm-up: compile/load kernels, populate caches, stabilize clocks.
    for (int i = 0; i < warmup_rounds; ++i) {
        hipLaunchKernelGGL(
            transform_kernel,
            dim3(blocks),
            dim3(threads_per_block),
            0,
            0,
            dx, dy, dz,
            dox, doy, doz,
            count,
            transform);
    }
    HIP_CHECK(hipDeviceSynchronize());

    std::vector<double> kernel_samples;
    std::vector<double> h2d_kernel_samples;
    std::vector<double> roundtrip_samples;

    std::size_t invalid_kernel_samples = 0;

    kernel_samples.reserve(rounds);
    h2d_kernel_samples.reserve(rounds);
    roundtrip_samples.reserve(rounds);

    hipEvent_t start_event{};
    hipEvent_t stop_event{};
    HIP_CHECK(hipEventCreate(&start_event));
    HIP_CHECK(hipEventCreate(&stop_event));

    for (int round = 0; round < rounds; ++round) {
        // ---------------------------------------------------------------
        // 1) Kernel only
        // ---------------------------------------------------------------
        HIP_CHECK(hipEventRecord(start_event, nullptr));

        hipLaunchKernelGGL(
            transform_kernel,
            dim3(blocks),
            dim3(threads_per_block),
            0,
            0,
            dx, dy, dz,
            dox, doy, doz,
            count,
            transform);

        HIP_CHECK(hipEventRecord(stop_event, nullptr));
        HIP_CHECK(hipEventSynchronize(stop_event));

        float kernel_ms = 0.0f;
        HIP_CHECK(hipEventElapsedTime(&kernel_ms, start_event, stop_event));

        const double kernel_us =
            static_cast<double>(kernel_ms) * 1000.0;

        if (std::isfinite(kernel_us) && kernel_us >= 0.0) {
            kernel_samples.push_back(kernel_us);
        } else {
            ++invalid_kernel_samples;
        }

        // ---------------------------------------------------------------
        // 2) Host -> GPU + kernel
        // ---------------------------------------------------------------
        auto t0 = std::chrono::steady_clock::now();

        HIP_CHECK(hipMemcpy(dx, x.data(), bytes, hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(dy, y.data(), bytes, hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(dz, z.data(), bytes, hipMemcpyHostToDevice));

        hipLaunchKernelGGL(
            transform_kernel,
            dim3(blocks),
            dim3(threads_per_block),
            0,
            0,
            dx, dy, dz,
            dox, doy, doz,
            count,
            transform);

        HIP_CHECK(hipDeviceSynchronize());

        auto t1 = std::chrono::steady_clock::now();

        h2d_kernel_samples.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());

        // ---------------------------------------------------------------
        // 3) Host -> GPU + kernel + GPU -> Host
        // ---------------------------------------------------------------
        t0 = std::chrono::steady_clock::now();

        HIP_CHECK(hipMemcpy(dx, x.data(), bytes, hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(dy, y.data(), bytes, hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(dz, z.data(), bytes, hipMemcpyHostToDevice));

        hipLaunchKernelGGL(
            transform_kernel,
            dim3(blocks),
            dim3(threads_per_block),
            0,
            0,
            dx, dy, dz,
            dox, doy, doz,
            count,
            transform);

        HIP_CHECK(hipMemcpy(ox.data(), dox, bytes, hipMemcpyDeviceToHost));
        HIP_CHECK(hipMemcpy(oy.data(), doy, bytes, hipMemcpyDeviceToHost));
        HIP_CHECK(hipMemcpy(oz.data(), doz, bytes, hipMemcpyDeviceToHost));

        HIP_CHECK(hipDeviceSynchronize());

        t1 = std::chrono::steady_clock::now();

        roundtrip_samples.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    HIP_CHECK(hipEventDestroy(start_event));
    HIP_CHECK(hipEventDestroy(stop_event));

    HIP_CHECK(hipFree(dx));
    HIP_CHECK(hipFree(dy));
    HIP_CHECK(hipFree(dz));
    HIP_CHECK(hipFree(dox));
    HIP_CHECK(hipFree(doy));
    HIP_CHECK(hipFree(doz));

    Timing result;
    result.kernel_us = median(kernel_samples);
    result.h2d_kernel_us = median(h2d_kernel_samples);
    result.roundtrip_us = median(roundtrip_samples);
    result.kernel_samples = std::move(kernel_samples);
    result.h2d_kernel_samples = std::move(h2d_kernel_samples);
    result.roundtrip_samples = std::move(roundtrip_samples);
    result.invalid_kernel_samples = invalid_kernel_samples;
    return result;
}


static void create_histograms(
    const std::vector<std::size_t>& point_counts,
    const std::vector<Timing>& results)
{
    const std::filesystem::path result_dir =
        std::filesystem::current_path() / "result";
    std::filesystem::create_directories(result_dir);

    const std::filesystem::path png_path =
        result_dir / "gpu_benchmark.png";
    const std::filesystem::path script_path =
        std::filesystem::temp_directory_path() / "hip_lidar_gpu_histograms.gp";

    std::ofstream gp(script_path, std::ios::trunc);
    if (!gp) {
        std::cerr << "Gnuplot-Skript konnte nicht geschrieben werden.\n";
        return;
    }

    constexpr int bins = 60;

    const char* method_names[3] = {
        "GPU kernel only",
        "PCIe upload + GPU kernel",
        "PCIe upload + GPU kernel + PCIe download"
    };

    auto emit_histogram_data =
        [&](const std::vector<double>& values,
            std::size_t size_index,
            int method_index)
    {
        if (values.empty()) {
            return;
        }

        const auto [min_it, max_it] =
            std::minmax_element(values.begin(), values.end());

        double min_v = *min_it;
        double max_v = *max_it;

        if (max_v <= min_v) {
            max_v = min_v + 1.0;
        }

        const double width =
            (max_v - min_v) / static_cast<double>(bins);

        std::array<std::size_t, bins> counts{};

        for (double v : values) {
            int bin = static_cast<int>((v - min_v) / width);
            bin = std::clamp(bin, 0, bins - 1);
            counts[bin]++;
        }

        gp << "$H_" << size_index << "_" << method_index << " << EOD\n";

        for (int i = 0; i < bins; ++i) {
            const double center =
                min_v + (static_cast<double>(i) + 0.5) * width;

            gp << std::fixed << std::setprecision(9)
               << center << " " << counts[i] << "\n";
        }

        gp << "EOD\n\n";
    };

    for (std::size_t s = 0; s < results.size(); ++s) {
        emit_histogram_data(results[s].kernel_samples, s, 0);
        emit_histogram_data(results[s].h2d_kernel_samples, s, 1);
        emit_histogram_data(results[s].roundtrip_samples, s, 2);
    }

    auto emit_layout = [&]() {
        gp << "set multiplot layout "
           << point_counts.size()
           << ",3 rowsfirst title "
              "'HIP LiDAR Benchmark - Laufzeitverteilungen' font ',14'\n";

        gp << "set border\n";
        gp << "set xtics\n";
        gp << "set ytics\n";
        gp << "set tics out\n";
        gp << "set style fill solid 0.70 border -1\n";
        gp << "set boxwidth 0.90 relative\n";
        gp << "set grid ytics\n";
        gp << "set key off\n";

        for (std::size_t s = 0; s < point_counts.size(); ++s) {
            for (int method = 0; method < 3; ++method) {
                gp << "set title '"
                   << point_counts[s] << " Punkte - "
                   << method_names[method] << "'\n";

                gp << "set xlabel 'us pro Cloud'\n";
                gp << "set ylabel 'Anzahl Messungen'\n";
                gp << "set xrange [0:*]\n";
                gp << "set autoscale y\n";

                gp << "plot $H_" << s << "_" << method
                   << " using 1:2 with boxes\n";
            }
        }

        gp << "unset multiplot\n";
    };

    // PNG
    gp << "set term pngcairo size 2400,3200 enhanced font 'Segoe UI,9'\n";
    gp << "set output '" << png_path.generic_string() << "'\n";
    emit_layout();
    gp << "unset output\n\n";

    // Interactive histogram window.
    gp << "set term qt size 1800,1200 enhanced font 'Segoe UI,9'\n";
    emit_layout();

    gp.close();

    const std::string cmd =
        "gnuplot -persist \"" + script_path.string() + "\"";

    const int rc = std::system(cmd.c_str());

    if (rc == 0) {
        std::cout << "Histogramm gespeichert:\n"
                  << "  .\\result\\gpu_benchmark.png\n";
    } else {
        std::cerr << "Gnuplot wurde mit Fehlercode "
                  << rc << " beendet.\n";
    }
}

int main(int argc, char** argv)
{
    int rounds = 200;
    int warmup_rounds = 20;
    int threads_per_block = 256;

    if (argc > 1) {
        rounds = std::max(10, std::stoi(argv[1]));
    }

    if (argc > 2) {
        threads_per_block = std::max(32, std::stoi(argv[2]));
    }

    int device_count = 0;
    HIP_CHECK(hipGetDeviceCount(&device_count));

    if (device_count == 0) {
        std::cerr << "Keine HIP-GPU gefunden.\n";
        return 1;
    }

    hipDeviceProp_t props{};
    HIP_CHECK(hipGetDeviceProperties(&props, 0));
    HIP_CHECK(hipSetDevice(0));

    std::cout << "HIP LiDAR GPU Benchmark\n";
    std::cout << "-----------------------\n";
    std::cout << "GPU             : " << props.name << "\n";
    std::cout << "Compute Units   : " << props.multiProcessorCount << "\n";
    std::cout << "Global Memory   : "
              << (static_cast<double>(props.totalGlobalMem) / (1024.0 * 1024.0 * 1024.0))
              << " GiB\n";
    std::cout << "Rounds          : " << rounds << "\n";
    std::cout << "Threads/Block   : " << threads_per_block << "\n\n";

    const Transform transform{
         0.8660254038f, -0.5f,          0.0f,
         0.5f,           0.8660254038f, 0.0f,
         0.0f,           0.0f,          1.0f,
         1.25f,         -3.50f,         0.75f
    };

    const std::vector<std::size_t> point_counts{
        200'000,
        400'000,
        800'000,
        1'600'000,
        3'200'000,
        6'400'000
    };

    std::vector<Timing> all_results;
    all_results.reserve(point_counts.size());

    std::ofstream csv("gpu_benchmark.csv", std::ios::trunc);
    csv << "points,kernel_us,h2d_kernel_us,roundtrip_us,"
           "kernel_gpoints_s,h2d_kernel_gpoints_s,roundtrip_gpoints_s\n";

    std::cout << std::fixed << std::setprecision(2);

    for (const auto count : point_counts) {
        std::cout << "Benchmark " << count << " Punkte ...\n";

        Timing t = benchmark_size(
            count,
            rounds,
            warmup_rounds,
            threads_per_block,
            transform);

        const double kernel_gpps =
            static_cast<double>(count) / t.kernel_us / 1000.0;
        const double h2d_gpps =
            static_cast<double>(count) / t.h2d_kernel_us / 1000.0;
        const double roundtrip_gpps =
            static_cast<double>(count) / t.roundtrip_us / 1000.0;

        std::cout
            << "  Invalid GPU kernel samples: "
            << t.invalid_kernel_samples << " / " << rounds << "\n";

        std::cout
            << "  Kernel only               : "
            << std::setw(10) << t.kernel_us << " us  | "
            << kernel_gpps << " Gpoints/s\n"
            << "  H2D + kernel              : "
            << std::setw(10) << t.h2d_kernel_us << " us  | "
            << h2d_gpps << " Gpoints/s\n"
            << "  H2D + kernel + D2H        : "
            << std::setw(10) << t.roundtrip_us << " us  | "
            << roundtrip_gpps << " Gpoints/s\n\n";

        csv
            << count << ","
            << t.kernel_us << ","
            << t.h2d_kernel_us << ","
            << t.roundtrip_us << ","
            << kernel_gpps << ","
            << h2d_gpps << ","
            << roundtrip_gpps << "\n";

        all_results.push_back(std::move(t));
    }

    csv.close();
    std::cout << "CSV gespeichert: .\\gpu_benchmark.csv\n";

    create_histograms(point_counts, all_results);

    return 0;
}
