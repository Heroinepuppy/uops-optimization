#include "lidar_compute_common.h"
#include <vulkan/vulkan.h>
#include <cstring>
#include <cstdint>
#include <limits>
#include "lidar_vulkan_shader.h"

struct Transform { float r00,r01,r02,r10,r11,r12,r20,r21,r22,tx,ty,tz; };
struct Parameters { Transform transform; std::uint32_t count, transforms; };
static_assert(sizeof(Parameters)==56);

// Check a Vulkan result and keep the failing operation in the error message.
// result: Vulkan status; operation: name of the operation being checked.
static void check(VkResult result, const char* operation) {
    if (result!=VK_SUCCESS)
        throw std::runtime_error(std::string(operation)+": Vulkan error "+std::to_string(result));
}
#define VK_CHECK(call) check((call), #call)

// Own a headless compute device, mapped buffers and reusable command resources.
// Default constructor: no parameters; initialize() acquires resources.
class VulkanCompute {
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    std::uint32_t family{}, timestamp_bits{};
    VkPhysicalDeviceProperties properties{};
    VkDescriptorSetLayout descriptors{};
    VkPipelineLayout layout{};
    VkShaderModule shader{};
    VkPipeline pipeline{};
    VkDescriptorPool descriptor_pool{};
    VkCommandPool command_pool{};
    VkCommandBuffer command{};
    VkFence fence{};
    VkQueryPool queries{};
    std::array<VkBuffer,6> buffers{};
    std::array<VkDeviceMemory,6> memories{};
    std::array<void*,6> mapped{};
    std::vector<VkDescriptorSet> sets;
    std::size_t points{}, chunk_points{};
public:
    VulkanCompute() = default;
    VulkanCompute(const VulkanCompute&) = delete;
    VulkanCompute& operator=(const VulkanCompute&) = delete;
    ~VulkanCompute() {
        if (device) {
            (void)vkDeviceWaitIdle(device);
            if (fence) vkDestroyFence(device,fence,nullptr);
            if (queries) vkDestroyQueryPool(device,queries,nullptr);
            if (command_pool) vkDestroyCommandPool(device,command_pool,nullptr);
            if (pipeline) vkDestroyPipeline(device,pipeline,nullptr);
            if (shader) vkDestroyShaderModule(device,shader,nullptr);
            if (descriptor_pool) vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
            if (layout) vkDestroyPipelineLayout(device,layout,nullptr);
            if (descriptors) vkDestroyDescriptorSetLayout(device,descriptors,nullptr);
            for (unsigned i=0;i<6;++i) {
                if (mapped[i]) vkUnmapMemory(device,memories[i]);
                if (buffers[i]) vkDestroyBuffer(device,buffers[i],nullptr);
                if (memories[i]) vkFreeMemory(device,memories[i],nullptr);
            }
            vkDestroyDevice(device,nullptr);
        }
        if (instance) vkDestroyInstance(instance,nullptr);
    }
    // Select hardware with a compute queue; never use a CPU software renderer.
    // No parameters; throws if no usable hardware GPU is visible.
    void initialize_device() {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName="LiDAR Vulkan compute"; app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo=&app;
        VK_CHECK(vkCreateInstance(&create,nullptr,&instance));
        std::uint32_t count=0;
        VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        VK_CHECK(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
        for (auto candidate:devices) {
            VkPhysicalDeviceProperties prop{};
            vkGetPhysicalDeviceProperties(candidate,&prop);
            if (prop.deviceType!=VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
                prop.deviceType!=VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) continue;
            std::uint32_t families=0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,nullptr);
            std::vector<VkQueueFamilyProperties> queues(families);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&families,queues.data());
            for (std::uint32_t i=0;i<families;++i) if (queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT) {
                physical=candidate; properties=prop; family=i;
                timestamp_bits=queues[i].timestampValidBits; break;
            }
            if (physical) break;
        }
        if (!physical) throw std::runtime_error("No hardware Vulkan compute GPU available (software renderers excluded)");
        float priority=1;
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex=family; q.queueCount=1; q.pQueuePriorities=&priority;
        VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        d.queueCreateInfoCount=1; d.pQueueCreateInfos=&q;
        VK_CHECK(vkCreateDevice(physical,&d,nullptr,&device));
        vkGetDeviceQueue(device,family,0,&queue);
    }
    // Report available timing modes; kernel timing requires GPU timestamps.
    // No parameters; returned labels match the shared CSV format.
    std::vector<std::string> methods() const {
        std::vector<std::string> labels;
        if (timestamp_bits) labels.push_back("GPU kernel only");
        labels.insert(labels.end(),{"GPU resident (host sync)","Upload + GPU","Upload + GPU + Download"});
        return labels;
    }
    // Return the selected physical GPU name (initialize_device must have run).
    const char* name() const { return properties.deviceName; }
    // Allocate six coherent mapped SoA arrays and the compute pipeline.
    // n: positive point count; buffer bindings are chunked at device limits.
    void initialize(std::size_t n) {
        points=n;
        const VkDeviceSize bytes=n*sizeof(float);
        const auto alignment=std::max<VkDeviceSize>(sizeof(float),properties.limits.minStorageBufferOffsetAlignment);
        chunk_points=(properties.limits.maxStorageBufferRange/alignment)*alignment/sizeof(float);
        if (!chunk_points) throw std::runtime_error("Vulkan storage buffer range too small");
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        for (unsigned i=0;i<6;++i) {
            VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            b.size=bytes; b.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            // Exclusive buffers are accessed by our single compute queue.
            b.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            VK_CHECK(vkCreateBuffer(device,&b,nullptr,&buffers[i]));
            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(device,buffers[i],&requirements);
            unsigned type=memory.memoryTypeCount; int best=-1;
            for (unsigned j=0;j<memory.memoryTypeCount;++j) {
                const auto flags=memory.memoryTypes[j].propertyFlags;
                const auto required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                if (!(requirements.memoryTypeBits&(1u<<j)) || (flags&required)!=required) continue;
                const int score=bool(flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)+2*bool(flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
                if (score>best) { type=j; best=score; }
            }
            if (type==memory.memoryTypeCount) throw std::runtime_error("No coherent host-visible Vulkan buffer memory");
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocate.allocationSize=requirements.size; allocate.memoryTypeIndex=type;
            VK_CHECK(vkAllocateMemory(device,&allocate,nullptr,&memories[i]));
            VK_CHECK(vkBindBufferMemory(device,buffers[i],memories[i],0));
            // Map once; coherent memory avoids explicit flush/invalidate calls.
            VK_CHECK(vkMapMemory(device,memories[i],0,VK_WHOLE_SIZE,0,&mapped[i]));
        }
        std::array<VkDescriptorSetLayoutBinding,6> bindings{};
        for (unsigned i=0;i<6;++i) bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo ds{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ds.bindingCount=6; ds.pBindings=bindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(device,&ds,nullptr,&descriptors));
        VkPushConstantRange constants{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount=1; pl.pSetLayouts=&descriptors; pl.pushConstantRangeCount=1; pl.pPushConstantRanges=&constants;
        VK_CHECK(vkCreatePipelineLayout(device,&pl,nullptr,&layout));
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        sm.codeSize=sizeof(lidar_shader); sm.pCode=lidar_shader;
        VK_CHECK(vkCreateShaderModule(device,&sm,nullptr,&shader));
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        cp.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module=shader; cp.stage.pName="main"; cp.layout=layout;
        VK_CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline));
        const auto chunks=static_cast<std::uint32_t>((n+chunk_points-1)/chunk_points);
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,6*chunks};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets=chunks; pool.poolSizeCount=1; pool.pPoolSizes=&size;
        VK_CHECK(vkCreateDescriptorPool(device,&pool,nullptr,&descriptor_pool));
        sets.resize(chunks);
        std::vector<VkDescriptorSetLayout> layouts(chunks,descriptors);
        VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        sa.descriptorPool=descriptor_pool; sa.descriptorSetCount=chunks; sa.pSetLayouts=layouts.data();
        VK_CHECK(vkAllocateDescriptorSets(device,&sa,sets.data()));
        for (unsigned c=0;c<chunks;++c) {
            std::array<VkDescriptorBufferInfo,6> infos{};
            std::array<VkWriteDescriptorSet,6> writes{};
            const auto offset=c*chunk_points;
            for (unsigned i=0;i<6;++i) {
                infos[i]={buffers[i],offset*sizeof(float),std::min(chunk_points,n-offset)*sizeof(float)};
                writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet=sets[c]; writes[i].dstBinding=i;
                writes[i].descriptorCount=1; writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo=&infos[i];
            }
            vkUpdateDescriptorSets(device,6,writes.data(),0,nullptr);
        }
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pc.queueFamilyIndex=family;
        VK_CHECK(vkCreateCommandPool(device,&pc,nullptr,&command_pool));
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool=command_pool; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount=1;
        VK_CHECK(vkAllocateCommandBuffers(device,&ca,&command));
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VK_CHECK(vkCreateFence(device,&fc,nullptr,&fence));
        if (timestamp_bits) {
            VkQueryPoolCreateInfo qc{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qc.queryType=VK_QUERY_TYPE_TIMESTAMP; qc.queryCount=2;
            VK_CHECK(vkCreateQueryPool(device,&qc,nullptr,&queries));
        }
    }
    // Copy between host vectors and mapped GPU arrays, outside resident timing.
    // arrays: three vectors of points floats; output: false uploads, true downloads.
    void copy(std::array<std::vector<float>,3>& arrays, bool output) {
        for (unsigned i=0;i<3;++i) {
            if (output) std::memcpy(arrays[i].data(),mapped[i+3],points*sizeof(float));
            else std::memcpy(mapped[i],arrays[i].data(),points*sizeof(float));
        }
    }
    // Record a reusable transform dispatch and explicit host/compute dependencies.
    // t: affine transform; k: 1..65536 transformations per point.
    void record(const Transform& t, unsigned k) {
        VK_CHECK(vkResetCommandBuffer(command,0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(command,&begin));
        VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        before.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_HOST_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,nullptr,0,nullptr);
        if (queries) { vkCmdResetQueryPool(command,queries,0,2); vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,queries,0); }
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        for (unsigned c=0;c<sets.size();++c) {
            const auto count=std::min(chunk_points,points-c*chunk_points);
            Parameters params{t,static_cast<std::uint32_t>(count),k};
            vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&sets[c],0,nullptr);
            vkCmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),&params);
            const auto groups=(count+63)/64;
            const auto gx=std::min<std::size_t>(groups,properties.limits.maxComputeWorkGroupCount[0]);
            const auto gy=(groups+gx-1)/gx;
            if (gy>properties.limits.maxComputeWorkGroupCount[1]) throw std::runtime_error("Vulkan dispatch exceeds grid limits");
            vkCmdDispatch(command,static_cast<std::uint32_t>(gx),static_cast<std::uint32_t>(gy),1);
        }
        if (queries) vkCmdWriteTimestamp(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,queries,1);
        VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        after.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,nullptr,0,nullptr);
        VK_CHECK(vkEndCommandBuffer(command));
    }
    // Submit recorded work and wait for host-visible results; no buffer copies.
    // No parameters; synchronization overhead is included in host measurements.
    void run() {
        VK_CHECK(vkResetFences(device,1,&fence));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount=1; submit.pCommandBuffers=&command;
        VK_CHECK(vkQueueSubmit(queue,1,&submit,fence));
        VK_CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,std::numeric_limits<std::uint64_t>::max()));
    }
    // Return GPU timestamp duration in microseconds, handling counter wraparound.
    // No parameters; requires completed work and timestamp support.
    double kernel_us() {
        std::uint64_t times[2]{};
        VK_CHECK(vkGetQueryPoolResults(device,queries,0,2,sizeof(times),times,sizeof(times[0]),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT));
        const auto mask=timestamp_bits==64 ? ~std::uint64_t{0} : (std::uint64_t{1}<<timestamp_bits)-1;
        return ((times[1]-times[0])&mask)*properties.limits.timestampPeriod/1000.0;
    }
};

// Parse shared benchmark options, validate GPU results and save timing samples.
// argc/argv: standard CLI, plus --capabilities for a hardware-only query.
int main(int argc,char** argv) try {
    VulkanCompute gpu;
    gpu.initialize_device();
    const auto labels=gpu.methods();
    for (int i=1;i<argc;++i) if (std::string(argv[i])=="--capabilities") {
        std::cout << "{\"methods\":[";
        for (unsigned m=0;m<labels.size();++m) { if(m) std::cout << ','; std::cout << '"' << labels[m] << '"'; }
        std::cout << "]}\n"; return 0;
    }
    const auto o=compute_options(argc,argv);
    const auto t=compute_transform<Transform>();
    std::cout << "GPU: " << gpu.name() << "; Vulkan shared mapped memory; points: " << o.points << '\n' << std::flush;
    std::array<std::vector<float>,3> input,output;
    for(auto& a:input) a.resize(o.points);
    for(auto& a:output) a.resize(o.points);
    compute_fill(input[0],input[1],input[2]);
    gpu.initialize(o.points); gpu.copy(input,false);
    std::vector<ComputeResult> results;
    for (unsigned k:compute_counts(o)) {
        gpu.record(t,k); gpu.run(); gpu.copy(output,true);
        compute_verify(input[0],input[1],input[2],output[0],output[1],output[2],t,k,o.verify_only);
        if(o.verify_only) continue;
        for(int i=0;i<8;++i) gpu.run();
        std::vector<ComputeResult> measurements;
        for(const auto& label:labels) measurements.push_back({k,label,{}});
        unsigned rounds=o.rounds;
        const auto first=std::chrono::steady_clock::now();
        for(unsigned round=0;round<rounds;++round) {
            for(unsigned offset=0;offset<labels.size();++offset) {
                auto& m=measurements[(round+offset)%labels.size()];
                const bool kernel=m.method=="GPU kernel only";
                bool valid=false;
                for(unsigned attempt=0;attempt<(kernel?10u:1u) && !valid;++attempt) {
                    const auto begin=std::chrono::steady_clock::now();
                    if(m.method.rfind("Upload",0)==0) gpu.copy(input,false);
                    gpu.run();
                    if(m.method=="Upload + GPU + Download") gpu.copy(output,true);
                    const double us=kernel?gpu.kernel_us():std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
                    valid=std::isfinite(us) && us>0;
                    if(valid) m.samples.push_back(us); else ++m.invalid_samples;
                }
                if(!valid) throw std::runtime_error("Invalid Vulkan timing samples");
            }
            if(round==0) rounds=compute_round_limit(o.rounds,std::chrono::duration<double>(std::chrono::steady_clock::now()-first).count(),o.max_seconds);
            if((round+1)%10==0 || round+1==rounds)
                std::cout << "Round " << round+1 << " / " << rounds << '\n' << std::flush;
        }
        for(auto& m:measurements) results.push_back(std::move(m));
    }
    if(!o.verify_only) compute_save(o,"gpu",results);
    std::cout << "Vulkan GPU validation passed.\n"; return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
