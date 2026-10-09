#include "huelith/validation/gpu_probe.h"
#define NOMINMAX
#include <bit>
#include <chrono>
#include <cstring>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <memory>
#include <qfloat16.h>
#include <stdexcept>
#include <windows.h>
#include <wrl/client.h>

namespace huelith::validation {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error("D3D12 probe failure: " +
                                 std::to_string(static_cast<unsigned long>(result)));
}
struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle)
            CloseHandle(handle);
    }
};
using Event = std::unique_ptr<void, HandleCloser>;
constexpr char shader[] = R"(
StructuredBuffer<float4> source : register(t0);
StructuredBuffer<float4> backdrop : register(t1);
#ifdef HALF_STORAGE
RWStructuredBuffer<uint2> result : register(u0);
#else
RWStructuredBuffer<float4> result : register(u0);
#endif
cbuffer Config : register(b0) { uint count; uint mode; uint use_half; uint offset; };
[numthreads(64,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint i = tid.x + offset;
    if (i >= count) return;
    float4 s = source[i], b = backdrop[i];
    if (use_half) { s = f16tof32(f32tof16(s)); b = f16tof32(f32tof16(b)); }
    if (s.a == 0) s.rgb = 0;
    if (b.a == 0) b.rgb = 0;
    float3 c = s.rgb + b.rgb * (1-s.a);
    if (mode == 1) c = s.rgb*(1-b.a) + b.rgb*(1-s.a) + s.rgb*b.rgb;
    if (mode == 2) c = s.rgb + b.rgb - s.rgb*b.rgb;
    float4 output = float4(c, s.a+b.a*(1-s.a));
#ifdef HALF_STORAGE
    uint4 bits = f32tof16(output);
    result[i] = uint2(bits.x | (bits.y << 16), bits.z | (bits.w << 16));
#else
    result[i] = output;
#endif
})";
ComPtr<ID3D12Resource> buffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE heap,
                              D3D12_RESOURCE_STATES state,
                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = heap;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    ComPtr<ID3D12Resource> resource;
    check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                          IID_PPV_ARGS(&resource)));
    return resource;
}
void upload(ID3D12Resource* resource, std::span<const Pixel> pixels) {
    void* destination{};
    const D3D12_RANGE range{0, 0};
    check(resource->Map(0, &range, &destination));
    std::memcpy(destination, pixels.data(), pixels.size_bytes());
    resource->Unmap(0, nullptr);
}
} // namespace
GpuResult gpu_composite(std::span<const Pixel> source, std::span<const Pixel> backdrop,
                        BlendMode mode, bool half_storage, bool simulate_device_loss) {
    if (source.empty() || source.size() != backdrop.size() || source.size() > 64'000'000)
        throw std::invalid_argument("invalid GPU image size");
    // Validate before dispatch, retaining the same error contract as the reference.
    for (std::size_t i = 0; i < source.size(); ++i)
        static_cast<void>(composite(source[i], backdrop[i], mode));
    const auto start = std::chrono::steady_clock::now();
    ComPtr<IDXGIFactory6> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    ComPtr<IDXGIAdapter1> adapter;
    check(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                              IID_PPV_ARGS(&adapter)));
    DXGI_ADAPTER_DESC1 adapter_desc{};
    check(adapter->GetDesc1(&adapter_desc));
    if ((adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        throw std::runtime_error("hardware GPU required");
    ComPtr<ID3D12Device> device;
    check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
    bool recovered{};
    if (simulate_device_loss) {
        ComPtr<ID3D12Device5> removable;
        check(device.As(&removable));
        removable->RemoveDevice();
        if (SUCCEEDED(device->GetDeviceRemovedReason()))
            throw std::runtime_error("device removal not observed");
        removable.Reset();
        device.Reset();
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        recovered = true;
    }
    ComPtr<ID3DBlob> bytecode, errors;
    const D3D_SHADER_MACRO macros[]{{"HALF_STORAGE", "1"}, {nullptr, nullptr}};
    const auto compiled =
        D3DCompile(shader, sizeof(shader) - 1, "m0-composite", half_storage ? macros : nullptr,
                   nullptr, "main", "cs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &bytecode, &errors);
    if (FAILED(compiled))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "shader compilation failed");
    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[1].Descriptor.ShaderRegister = 1;
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[2].Descriptor.ShaderRegister = 0;
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[3].Constants.Num32BitValues = 4;
    const D3D12_ROOT_SIGNATURE_DESC root_desc{4, parameters, 0, nullptr,
                                              D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ComPtr<ID3DBlob> serialized;
    check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized,
                                      &errors));
    ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                      serialized->GetBufferSize(), IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = root.Get();
    pipeline_desc.CS = {bytecode->GetBufferPointer(), bytecode->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline;
    check(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline)));
    const auto bytes = static_cast<UINT64>(source.size_bytes());
    const auto s =
        buffer(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    const auto b =
        buffer(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    const auto output_bytes = half_storage ? bytes / 2 : bytes;
    const auto output =
        buffer(device.Get(), output_bytes, D3D12_HEAP_TYPE_DEFAULT,
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    const auto readback = buffer(device.Get(), output_bytes, D3D12_HEAP_TYPE_READBACK,
                                 D3D12_RESOURCE_STATE_COPY_DEST);
    upload(s.Get(), source);
    upload(b.Get(), backdrop);
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12CommandAllocator> allocator;
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
    ComPtr<ID3D12GraphicsCommandList> commands;
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(),
                                    pipeline.Get(), IID_PPV_ARGS(&commands)));
    commands->SetComputeRootSignature(root.Get());
    commands->SetComputeRootShaderResourceView(0, s->GetGPUVirtualAddress());
    commands->SetComputeRootShaderResourceView(1, b->GetGPUVirtualAddress());
    commands->SetComputeRootUnorderedAccessView(2, output->GetGPUVirtualAddress());
    constexpr UINT tile_pixels{256 * 256};
    const auto count = static_cast<UINT>(source.size());
    for (UINT offset = 0; offset < count; offset += tile_pixels) {
        const UINT config[]{count, static_cast<UINT>(mode), half_storage ? 1U : 0U, offset};
        commands->SetComputeRoot32BitConstants(3, 4, config, 0);
        const auto remaining = (count - offset) < tile_pixels ? (count - offset) : tile_pixels;
        commands->Dispatch((remaining + 63) / 64, 1, 1);
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = output.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commands->ResourceBarrier(1, &barrier);
    commands->CopyResource(readback.Get(), output.Get());
    check(commands->Close());
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    Event event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!event)
        throw std::runtime_error("CreateEvent failed");
    check(queue->Signal(fence.Get(), 1));
    check(fence->SetEventOnCompletion(1, event.get()));
    if (WaitForSingleObject(event.get(), 30000) != WAIT_OBJECT_0)
        throw std::runtime_error("GPU fence timeout");
    check(device->GetDeviceRemovedReason());
    GpuResult result;
    result.pixels.resize(source.size());
    result.adapter = QString::fromWCharArray(adapter_desc.Description);
    result.recovered_after_removal = recovered;
    void* mapped{};
    const D3D12_RANGE read_range{0, static_cast<SIZE_T>(output_bytes)};
    check(readback->Map(0, &read_range, &mapped));
    if (half_storage) {
        const auto* packed = static_cast<const std::uint16_t*>(mapped);
        for (std::size_t i = 0; i < source.size(); ++i)
            for (std::size_t c = 0; c < 4; ++c)
                result.pixels[i][c] =
                    static_cast<float>(std::bit_cast<qfloat16>(packed[i * 4 + c]));
    } else
        std::memcpy(result.pixels.data(), mapped, source.size_bytes());
    const D3D12_RANGE written{0, 0};
    readback->Unmap(0, &written);
    result.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
} // namespace huelith::validation
