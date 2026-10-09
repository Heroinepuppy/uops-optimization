#include "lidar_cpu_methods.h"
#include "lidar_compute_common.h"
#include <functional>

const std::array<std::string,7> METHODS{"aos","soa","auto","fma","avx2x2","avx2x4","avx2x8"};

struct CpuData {
    SoA soa;
    std::vector<Point> in, out;
    explicit CpuData(std::size_t n, bool aos): soa(n), in(aos?n:0), out(aos?n:0) {
        compute_fill(soa.x,soa.y,soa.z);
        for(std::size_t i=0;i<in.size();++i) in[i]={soa.x[i],soa.y[i],soa.z[i]};
    }
    void run(unsigned method, std::size_t begin, std::size_t end, const Transform& t, unsigned k) {
        const auto n=end-begin;
        if(method==0) {
            if(k==1) transform_aos_scalar(in.data()+begin,out.data()+begin,n,t);
            else transform_aos_chain(in.data()+begin,out.data()+begin,n,t,k);
            return;
        }
        const auto x=soa.x.data()+begin,y=soa.y.data()+begin,z=soa.z.data()+begin;
        const auto ox=soa.ox.data()+begin,oy=soa.oy.data()+begin,oz=soa.oz.data()+begin;
        if(k==1) {
            switch(method) {
            case 1: transform_soa_scalar(x,y,z,ox,oy,oz,n,t); break;
            case 2: transform_soa_auto(x,y,z,ox,oy,oz,n,t); break;
            case 3: transform_soa_avx2(x,y,z,ox,oy,oz,n,t); break;
            case 4: transform_soa_avx2_x2(x,y,z,ox,oy,oz,n,t); break;
            case 5: transform_soa_avx2_x4(x,y,z,ox,oy,oz,n,t); break;
            case 6: transform_soa_avx2_x8(x,y,z,ox,oy,oz,n,t); break;
            }
        } else {
            switch(method) {
            case 1: transform_soa_scalar_chain<false>(x,y,z,ox,oy,oz,n,t,k); break;
            case 2: transform_soa_scalar_chain<true>(x,y,z,ox,oy,oz,n,t,k); break;
            case 3: transform_avx2_chain_x1(x,y,z,ox,oy,oz,n,t,k); break;
            case 4: transform_soa_avx2_x2_chain(x,y,z,ox,oy,oz,n,t,k); break;
            case 5: transform_avx2_chain_x4(x,y,z,ox,oy,oz,n,t,k); break;
            case 6: transform_avx2_chain_x8(x,y,z,ox,oy,oz,n,t,k); break;
            }
        }
    }
    void verify(unsigned method, const Transform& t,unsigned k,bool full) {
        if(method==0) for(std::size_t i=0;i<out.size();++i) {
            soa.ox[i]=out[i].x;soa.oy[i]=out[i].y;soa.oz[i]=out[i].z;
        }
        compute_verify(soa.x,soa.y,soa.z,soa.ox,soa.oy,soa.oz,t,k,full);
    }
};

class ComputeTeam {
    CpuData& data;
    Transform t;
    std::vector<unsigned> cpus;
    std::barrier<> start, done;
    std::vector<std::thread> workers;
    bool stop=false;
    unsigned method=0, transformations=1;
public:
    ComputeTeam(CpuData& d,Transform transform,std::vector<unsigned> cores)
        : data(d),t(transform),cpus(std::move(cores)),start(cpus.size()+1),done(cpus.size()+1) {
        if(cpus.size()==1) { pin_this_thread_to_cpu(cpus[0]); return; }
        for(std::size_t id=0;id<cpus.size();++id) workers.emplace_back([this,id] {
            pin_this_thread_to_cpu(cpus[id]);
            const auto n=data.soa.x.size(), block=n/cpus.size();
            const auto begin=id*block,end=id+1==cpus.size()?n:begin+block;
            for(;;) {
                start.arrive_and_wait();if(stop) break;
                data.run(method,begin,end,t,transformations);
                done.arrive_and_wait();
            }
        });
    }
    ~ComputeTeam() {
        if(workers.empty()) return;
        stop=true;start.arrive_and_wait();for(auto& w:workers) w.join();
    }
    void run(unsigned m,unsigned k) {
        if(workers.empty()) { data.run(m,0,data.soa.x.size(),t,k);return; }
        method=m;transformations=k;start.arrive_and_wait();done.arrive_and_wait();
    }
};

std::vector<unsigned> select_cores(const ComputeOptions& o) {
    if(o.thread_mode=="single") return {o.cpu};
    const auto s=select_cpu_pairs(o.cpu);
    if(o.thread_mode=="smt" && s.smt_found) return {s.base_cpu,s.smt_sibling};
    if(o.thread_mode=="same-ccx" && s.other_core_found) return {s.base_cpu,s.other_core_cpu};
    if(o.thread_mode=="different-ccx" && s.other_ccx_found) return {s.base_cpu,s.other_ccx_cpu};
    if(o.thread_mode=="four-ccx" && s.four_ccx_found) return {s.four_ccx_cpus.begin(),s.four_ccx_cpus.end()};
    throw std::runtime_error("Thread-Modus unbekannt oder auf dieser CPU nicht verfuegbar: "+o.thread_mode);
}

int main(int argc,char** argv) try {
    for(int i=1;i<argc;++i) if(std::string(argv[i])=="--help") {
        std::cout << "--points N --transforms K[,K...] --method aos|soa|auto|fma|avx2x2|avx2x4|avx2x8|all\n"
                     "--thread-mode single|smt|same-ccx|different-ccx|four-ccx --cpu N\n"
                     "--rounds N --block-ms N --max-transforms N --output-dir PATH --output-file CSV --output-prefix NAME --verify-only\n";
        return 0;
    }
    auto o=compute_options(argc,argv);
    bool explicit_counts=false;
    for(int i=1;i<argc;++i) if(std::string(argv[i])=="--transforms" || std::string(argv[i])=="--max-transforms") explicit_counts=true;
    if(!explicit_counts) o.transformations={1};
    std::vector<unsigned> methods;
    for(unsigned m=0;m<METHODS.size();++m) if(o.method==METHODS[m] || o.method=="all") methods.push_back(m);
    if(methods.empty()) throw std::runtime_error("Unbekannte Methode: "+o.method);
    const auto cores=select_cores(o);
    const auto t=compute_transform<Transform>();
    std::cout << "CPU: " << o.method << ", " << o.thread_mode << "; Punkte: " << o.points << "; CPUs:";
    for(auto c:cores) std::cout << ' ' << c;
    std::cout << '\n' << std::flush;
    CpuData data(o.points,o.method=="aos" || o.method=="all");
    ComputeTeam team(data,t,cores);
    std::vector<ComputeResult> results;
    std::mt19937 rng(0x3950);
    for(unsigned k:compute_counts(o)) {
        if(!o.verify_only) std::cout << "Vorbereitung: Validierung, Warm-up und Kalibrierung (" << k << " Transformationen)\n" << std::flush;
        std::array<std::size_t,7> repeats{};
        std::array<ComputeResult,7> measurements;
        auto measure=[&](unsigned m,std::size_t count) {
            const auto begin=std::chrono::steady_clock::now();
            for(std::size_t i=0;i<count;++i) team.run(m,k);
            return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
        };
        for(auto m:methods) {
            team.run(m,k);data.verify(m,t,k,o.verify_only);
            if(o.verify_only) continue;
            for(int warm=0;warm<8;++warm) team.run(m,k);
            repeats[m]=1;
            // Retain the final calibration timing, including at the repeat limit.
            double calibration_us=measure(m,repeats[m]);
            while(repeats[m]<65536 && calibration_us<o.block_ms*1000) {
                repeats[m]*=2;
                calibration_us=measure(m,repeats[m]);
            }
            const auto label=METHODS[m]+" / "+o.thread_mode;
            // Normalize over repeats and transformations of the whole point set.
            std::cout << "Kalibrierung " << label << ": " << repeats[m]
                      << " Durchlaeufe; Ziel " << o.block_ms << " ms; Block "
                      << calibration_us/1000 << " ms; "
                      << calibration_us/repeats[m]/k << " us/Transformation (gesamte Punktmenge)\n"
                      << std::flush;
            measurements[m]={k,label,{}};
        }
        if(o.verify_only) continue;
        std::cout << "Messrunde 0 / " << o.rounds << '\n' << std::flush;
        unsigned rounds=o.rounds;
        const auto first_round_start=std::chrono::steady_clock::now();
        for(unsigned round=0;round<rounds;++round) {
            std::shuffle(methods.begin(),methods.end(),rng);
            for(auto m:methods) {
                measurements[m].samples.push_back(measure(m,repeats[m])/repeats[m]);
                g_sink=m==0?data.out[o.points/2].x:data.soa.ox[o.points/2];
            }
            if(round==0) {
                const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-first_round_start).count();
                rounds=compute_round_limit(o.rounds,seconds,o.max_seconds);
                std::cout << "Erste Messrunde: " << seconds << " s; Rundenlimit: " << o.rounds
                          << " -> " << rounds << " (" << o.max_seconds << " s Hochrechnung)\n" << std::flush;
            }
            if((round+1)%10==0 || round+1==rounds)
                std::cout << "Messrunde " << round+1 << " / " << rounds << '\n' << std::flush;
        }
        for(auto m:methods) results.push_back(std::move(measurements[m]));
    }
    const auto device=o.method=="avx2x2" && o.thread_mode=="four-ccx" ? "cpu" : "cpu_"+o.method+"_"+o.thread_mode;
    if(!o.verify_only) compute_save(o,device,results);
    std::cout << "CPU-Validierung erfolgreich.\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
