#include <hip/hip_runtime.h>
#include "lidar_compute_common.h"
#define HIP_CHECK(call) do { auto e=(call); if(e!=hipSuccess) throw std::runtime_error(hipGetErrorString(e)); } while(0)
struct Transform { float r00,r01,r02,r10,r11,r12,r20,r21,r22,tx,ty,tz; };
__global__ void compute_kernel(const float* x,const float* y,const float* z,
    float* ox,float* oy,float* oz,std::size_t n,Transform t,unsigned transformations) {
    const std::size_t i=static_cast<std::size_t>(blockIdx.x)*blockDim.x+threadIdx.x;
    if(i>=n) return;
    float X=x[i],Y=y[i],Z=z[i];
    for(unsigned step=0;step<transformations;++step) {
        const float nx=fmaf(t.r02,Z,fmaf(t.r01,Y,t.r00*X))+t.tx;
        const float ny=fmaf(t.r12,Z,fmaf(t.r11,Y,t.r10*X))+t.ty;
        const float nz=fmaf(t.r22,Z,fmaf(t.r21,Y,t.r20*X))+t.tz;
        X=nx; Y=ny; Z=nz;
    }
    ox[i]=X; oy[i]=Y; oz[i]=Z;
}
struct DeviceBuffers {
    std::array<float*,6> data{};
    ~DeviceBuffers() { for(auto p:data) if(p) (void)hipFree(p); }
};
struct Events {
    hipEvent_t start{},stop{};
    ~Events() { if(start) (void)hipEventDestroy(start); if(stop) (void)hipEventDestroy(stop); }
};
int main(int argc,char** argv) try {
    const auto o=compute_options(argc,argv);
    const auto t=compute_transform<Transform>();
    hipDeviceProp_t prop{}; HIP_CHECK(hipGetDeviceProperties(&prop,0));
    std::cout << "GPU: " << prop.name << "; feste Punktzahl: " << o.points << '\n';
    std::vector<float> x(o.points),y(o.points),z(o.points),ox(o.points),oy(o.points),oz(o.points);
    compute_fill(x,y,z);
    const auto bytes=o.points*sizeof(float);
    DeviceBuffers buffers;
    for(auto& p:buffers.data) HIP_CHECK(hipMalloc(&p,bytes));
    const auto [dx,dy,dz,dox,doy,doz]=buffers.data;
    auto upload=[&] {
        HIP_CHECK(hipMemcpy(dx,x.data(),bytes,hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(dy,y.data(),bytes,hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(dz,z.data(),bytes,hipMemcpyHostToDevice));
    };
    auto download=[&] {
        HIP_CHECK(hipMemcpy(ox.data(),dox,bytes,hipMemcpyDeviceToHost));
        HIP_CHECK(hipMemcpy(oy.data(),doy,bytes,hipMemcpyDeviceToHost));
        HIP_CHECK(hipMemcpy(oz.data(),doz,bytes,hipMemcpyDeviceToHost));
    };
    auto launch=[&](unsigned k) {
        hipLaunchKernelGGL(compute_kernel,dim3(static_cast<unsigned>((o.points+255)/256)),dim3(256),0,0,
                           dx,dy,dz,dox,doy,doz,o.points,t,k);
        HIP_CHECK(hipGetLastError());
    };
    Events events; HIP_CHECK(hipEventCreate(&events.start)); HIP_CHECK(hipEventCreate(&events.stop));
    upload();
    std::vector<ComputeResult> results;
    for(unsigned k:compute_counts(o)) {
        launch(k); download(); HIP_CHECK(hipDeviceSynchronize());
        compute_verify(x,y,z,ox,oy,oz,t,k,o.verify_only);
        if(o.verify_only) continue;
        for(int i=0;i<8;++i) launch(k);
        HIP_CHECK(hipDeviceSynchronize());
        ComputeResult kernel{k,"GPU kernel only",{}},host{k,"GPU resident (host sync)",{}},
                      uploaded{k,"Upload + GPU",{}},roundtrip{k,"Upload + GPU + Download",{}};
        for(unsigned round=0;round<o.rounds;++round) {
            // Rotate modes to reduce ordering/clock bias. Each chain uses one launch.
            for(unsigned offset=0;offset<4;++offset) {
                const unsigned mode=(round+offset)%4;
                if(mode==0) {
                    bool valid=false;
                    for(int attempt=0;attempt<10 && !valid;++attempt) {
                        HIP_CHECK(hipEventRecord(events.start,nullptr));
                        launch(k);
                        HIP_CHECK(hipEventRecord(events.stop,nullptr));
                        HIP_CHECK(hipEventSynchronize(events.stop));
                        float ms=0; HIP_CHECK(hipEventElapsedTime(&ms,events.start,events.stop));
                        valid=std::isfinite(ms) && ms>0;
                        if(valid) kernel.samples.push_back(ms*1000.0);
                        else ++kernel.invalid_samples;
                    }
                    if(!valid) throw std::runtime_error("10 ungueltige GPU-Eventzeiten hintereinander");
                    continue;
                }
                const auto start=std::chrono::steady_clock::now();
                if(mode>=2) upload();
                launch(k);
                if(mode==3) download();
                HIP_CHECK(hipDeviceSynchronize());
                const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
                (mode==1 ? host : mode==2 ? uploaded : roundtrip).samples.push_back(us);
            }
        }
        std::cout << k << " Transformationen: Kernel " << kernel.samples.back()
                  << ", Roundtrip " << roundtrip.samples.back() << " us\n";
        results.push_back(std::move(kernel)); results.push_back(std::move(host));
        results.push_back(std::move(uploaded)); results.push_back(std::move(roundtrip));
    }
    if(!o.verify_only) compute_save(o,"gpu",results);
    std::cout << "GPU-Validierung erfolgreich.\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
