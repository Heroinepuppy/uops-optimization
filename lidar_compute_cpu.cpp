#include "lidar_cpu_chain.h"
#include "lidar_compute_common.h"

class ComputeTeam {
    SoA& s;
    Transform t;
    std::array<unsigned,4> cpus;
    std::barrier<> start{5}, done{5};
    std::array<std::thread,4> workers;
    bool stop = false;
    unsigned transformations = 1;
public:
    ComputeTeam(SoA& data, Transform transform, std::array<unsigned,4> cores)
        : s(data), t(transform), cpus(cores) {
        for (unsigned id=0; id<4; ++id) workers[id] = std::thread([this,id] {
            pin_this_thread_to_cpu(cpus[id]);
            const std::size_t begin = id*(s.x.size()/4);
            const std::size_t end = id==3 ? s.x.size() : begin+s.x.size()/4;
            for (;;) {
                start.arrive_and_wait();
                if (stop) break;
                if (transformations == 1)
                    transform_soa_avx2_x2(s.x.data()+begin,s.y.data()+begin,s.z.data()+begin,
                        s.ox.data()+begin,s.oy.data()+begin,s.oz.data()+begin,end-begin,t);
                else
                    transform_soa_avx2_x2_chain(s.x.data()+begin,s.y.data()+begin,s.z.data()+begin,
                        s.ox.data()+begin,s.oy.data()+begin,s.oz.data()+begin,end-begin,t,transformations);
                done.arrive_and_wait();
            }
        });
    }
    ~ComputeTeam() { stop=true; start.arrive_and_wait(); for(auto& w:workers) w.join(); }
    void run(unsigned k) { transformations=k; start.arrive_and_wait(); done.arrive_and_wait(); }
};

int main(int argc, char** argv) try {
    const auto o = compute_options(argc,argv);
    const auto t = compute_transform<Transform>();
    for (std::size_t n : {1u, 15u, 16u, 17u, 65u, 259u}) {
        SoA test(n); compute_fill(test.x,test.y,test.z);
        for (unsigned k : {1u,2u,7u,1024u}) {
            transform_soa_avx2_x2_chain(test.x.data(),test.y.data(),test.z.data(),
                test.ox.data(),test.oy.data(),test.oz.data(),n,t,k);
            compute_verify(test.x,test.y,test.z,test.ox,test.oy,test.oz,t,k,true);
        }
    }
    const auto selection = select_cpu_pairs(o.cpu);
    if (!selection.four_ccx_found) throw std::runtime_error("Vier verschiedene L3/CCX-Gruppen nicht gefunden.");
    std::cout << "CPU: AVX2 x2, 4 Threads / 4 Cores / 4 CCX; CPUs:";
    for (auto cpu:selection.four_ccx_cpus) std::cout << ' ' << cpu;
    std::cout << "\nFeste Punktzahl: " << o.points << "\n";
    SoA s(o.points); compute_fill(s.x,s.y,s.z);
    ComputeTeam team(s,t,selection.four_ccx_cpus);
    std::vector<ComputeResult> results;
    for (unsigned k:compute_counts(o)) {
        team.run(k);
        compute_verify(s.x,s.y,s.z,s.ox,s.oy,s.oz,t,k);
        if(o.verify_only) continue;
        for(int warm=0;warm<8;++warm) team.run(k);
        auto measure = [&](std::size_t repeats) {
            const auto begin=std::chrono::steady_clock::now();
            for(std::size_t i=0;i<repeats;++i) team.run(k);
            return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
        };
        std::size_t repeats=1;
        while(repeats<65536 && measure(repeats)<o.block_ms*1000) repeats*=2;
        ComputeResult result{k,"CPU 4T / 4 CCX",{}};
        for(unsigned round=0;round<o.rounds;++round) result.samples.push_back(measure(repeats)/repeats);
        g_sink = s.ox[o.points/2];
        std::cout << k << " Transformationen: " << result.samples.back() << " us/Cloud (" << repeats << " Clouds/Block)\n";
        results.push_back(std::move(result));
    }
    if(!o.verify_only) compute_save(o,"cpu",results);
    std::cout << "CPU-Validierung erfolgreich.\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
