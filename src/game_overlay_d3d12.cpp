#include "game_overlay_runtime.h"
#include "game_overlay_shaders.h"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <memory>
#include <unordered_map>

namespace GameOverlay {
using Microsoft::WRL::ComPtr;
namespace {
auto& chainQueues = *new std::unordered_map<void*, ComPtr<ID3D12CommandQueue>>;
auto& observedQueues = *new std::vector<ComPtr<ID3D12CommandQueue>>;
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) { D3D12_HEAP_PROPERTIES h{}; h.Type = type; h.CreationNodeMask = h.VisibleNodeMask = 1; return h; }
D3D12_RESOURCE_DESC Buffer(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = bytes;
    d.Height = d.DepthOrArraySize = d.MipLevels = 1; d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; return d;
}
D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource; b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from; b.Transition.StateAfter = to; return b;
}
struct FrameResources {
    ComPtr<ID3D12CommandAllocator> allocator;
    // The swapchain owns its buffers. Holding an extra COM reference here would
    // keep its HWND association alive after the game releases the swapchain.
    ID3D12Resource* backBuffer = nullptr;
    ComPtr<ID3D12Resource> texture, upload;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 fence = 0, generation = 0;
    uint32_t owner = 0, width = 0, height = 0;
};
struct Renderer {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12DescriptorHeap> targets, images;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    UINT64 nextFence = 0;
    UINT targetStep = 0, imageStep = 0;
    UINT width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    HWND window = nullptr;
    std::vector<FrameResources> frames;
    ~Renderer() {
        if (fence && nextFence && fence->GetCompletedValue() < nextFence && event &&
            SUCCEEDED(fence->SetEventOnCompletion(nextFence, event))) WaitForSingleObject(event, INFINITE);
        if (event) CloseHandle(event);
    }
    HRESULT Init(IDXGISwapChain3* chain, ID3D12Device* d, ID3D12CommandQueue* q) {
        device = d; queue = q;
        DXGI_SWAP_CHAIN_DESC desc{}; HRESULT hr = chain->GetDesc(&desc); if (FAILED(hr)) return hr;
        window = desc.OutputWindow;
        if (!desc.BufferCount || desc.BufferCount > 16 || desc.SampleDesc.Count != 1) return E_NOTIMPL;
        frames.resize(desc.BufferCount);
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors = desc.BufferCount;
        if (FAILED(hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&targets)))) return hr;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(hr = device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&images)))) return hr;
        targetStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        imageStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto target = targets->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < desc.BufferCount; ++i) {
            ComPtr<ID3D12Resource> buffer;
            if (FAILED(hr = chain->GetBuffer(i, IID_PPV_ARGS(&buffer))) ||
                FAILED(hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames[i].allocator)))) return hr;
            frames[i].backBuffer = buffer.Get();
            device->CreateRenderTargetView(buffer.Get(), nullptr, target); target.ptr += targetStep;
        }
        const auto bufferDesc = frames[0].backBuffer->GetDesc(); width = static_cast<UINT>(bufferDesc.Width); height = bufferDesc.Height; format = bufferDesc.Format;
        if (FAILED(hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&commands)))) return hr;
        commands->Close();
        if (FAILED(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return hr;
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr); if (!event) return HRESULT_FROM_WIN32(GetLastError());
        D3D12_DESCRIPTOR_RANGE range{}; range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER params[2]{}; params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = 4; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &range; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS; sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rs{}; rs.NumParameters = ARRAYSIZE(params); rs.pParameters = params;
        rs.NumStaticSamplers = 1; rs.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> signature;
        if (FAILED(hr = D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr)) ||
            FAILED(hr = device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)))) return hr;
        ComPtr<ID3DBlob> vs, ps; if (FAILED(hr = CompileQuad(vs, ps))) return hr;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{}; p.pRootSignature = root.Get();
        p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()}; p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        p.BlendState.RenderTarget[0].BlendEnable = TRUE;
        auto& blend = p.BlendState.RenderTarget[0]; blend.SrcBlend = D3D12_BLEND_ONE; blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOp = D3D12_BLEND_OP_ADD; blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD; blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        p.RasterizerState.DepthClipEnable = TRUE; p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        p.DepthStencilState.FrontFace = p.DepthStencilState.BackFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
            D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
        p.SampleMask = UINT_MAX; p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.NumRenderTargets = 1; p.RTVFormats[0] = bufferDesc.Format; p.SampleDesc.Count = 1;
        return device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&pipeline));
    }
    HRESULT Upload(FrameResources& frame, const Frame& source, UINT index) {
        if (!frame.texture || frame.width != source.header.width || frame.height != source.header.height) {
            frame.texture.Reset(); frame.upload.Reset();
            D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = source.header.width; desc.Height = source.header.height;
            desc.DepthOrArraySize = desc.MipLevels = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
            auto defaultHeap = Heap(D3D12_HEAP_TYPE_DEFAULT);
            HRESULT hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&frame.texture));
            if (FAILED(hr)) return hr;
            UINT64 bytes = 0; device->GetCopyableFootprints(&desc, 0, 1, 0, &frame.footprint, nullptr, nullptr, &bytes);
            auto uploadHeap = Heap(D3D12_HEAP_TYPE_UPLOAD); const auto uploadDesc = Buffer(bytes);
            if (FAILED(hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.upload)))) return hr;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = desc.Format; view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.Texture2D.MipLevels = 1;
            auto handle = images->GetCPUDescriptorHandleForHeapStart(); handle.ptr += static_cast<SIZE_T>(index) * imageStep;
            device->CreateShaderResourceView(frame.texture.Get(), &view, handle);
            frame.width = source.header.width; frame.height = source.header.height; frame.generation = 0;
        }
        if (frame.generation == source.header.generation && frame.owner == source.header.ownerPid) return S_OK;
        void* mapped = nullptr; D3D12_RANGE readRange{0, 0}; HRESULT hr = frame.upload->Map(0, &readRange, &mapped);
        if (FAILED(hr)) return hr;
        for (uint32_t y = 0; y < source.header.height; ++y)
            memcpy(static_cast<uint8_t*>(mapped) + frame.footprint.Offset + static_cast<size_t>(y) * frame.footprint.Footprint.RowPitch,
                source.pixels.data() + static_cast<size_t>(y) * source.header.stride, source.header.stride);
        frame.upload->Unmap(0, nullptr);
        auto barrier = Transition(frame.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        commands->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION from{}; from.pResource = frame.upload.Get(); from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; from.PlacedFootprint = frame.footprint;
        D3D12_TEXTURE_COPY_LOCATION to{}; to.pResource = frame.texture.Get(); to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        barrier = Transition(frame.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commands->ResourceBarrier(1, &barrier); frame.generation = source.header.generation; frame.owner = source.header.ownerPid;
        return S_OK;
    }
};
// GPU cleanup belongs to explicit swapchain lifecycle callbacks, not DLL detach.
auto& renderers = *new std::unordered_map<void*, std::unique_ptr<Renderer>>;
}
void RegisterSwapChainQueue(void* chain, void* object) {
    if (!object || !chain) return;
    ComPtr<ID3D12CommandQueue> queue;
    if (SUCCEEDED(static_cast<IUnknown*>(object)->QueryInterface(IID_PPV_ARGS(&queue))) &&
        queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) chainQueues[chain] = queue;
}
void TrackCommandQueue(void* object) {
    auto* queue = static_cast<ID3D12CommandQueue*>(object);
    if (!queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
    auto found = std::find_if(observedQueues.begin(), observedQueues.end(), [queue](const auto& q) { return q.Get() == queue; });
    if (found != observedQueues.end()) observedQueues.erase(found);
    observedQueues.emplace_back(queue);
    if (observedQueues.size() > 32) observedQueues.erase(observedQueues.begin());
}
void* FindCommandQueue(void* chain, void* device) {
    auto matches = [device](ID3D12CommandQueue* queue) {
        ComPtr<ID3D12Device> d; return SUCCEEDED(queue->GetDevice(IID_PPV_ARGS(&d))) && d.Get() == device;
    };
    const auto it = chainQueues.find(chain);
    if (it != chainQueues.end() && matches(it->second.Get())) return it->second.Get();
    for (auto q = observedQueues.rbegin(); q != observedQueues.rend(); ++q) if (matches(q->Get())) return q->Get();
    return nullptr;
}
void ResetD3D12(void* chain) { renderers.erase(chain); }
bool DrawD3D12(void* pointer) {
    const auto* f = ReadFrame(D3D12); if (!f) return false;
    auto* chain = static_cast<IDXGISwapChain*>(pointer);
    ComPtr<IDXGISwapChain3> sc; ComPtr<ID3D12Device> device;
    if (FAILED(chain->QueryInterface(IID_PPV_ARGS(&sc))) || FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) return false;
    auto* queue = static_cast<ID3D12CommandQueue*>(FindCommandQueue(pointer, device.Get()));
    if (!queue) { ReportError(D3D12, HRESULT_FROM_WIN32(ERROR_NOT_READY)); return false; }
    auto& r = renderers[pointer];
    DXGI_SWAP_CHAIN_DESC current{}; if (FAILED(chain->GetDesc(&current))) return false;
    if (r && (r->frames.size() != current.BufferCount || r->format != current.BufferDesc.Format)) r.reset();
    if (!r || r->device.Get() != device.Get() || r->queue.Get() != queue) {
        r = std::make_unique<Renderer>(); const HRESULT hr = r->Init(sc.Get(), device.Get(), queue);
        if (FAILED(hr)) { ReportError(D3D12, hr); r.reset(); return false; }
    }
    if (!BeginDraw(*f, D3D12)) return true;
    const UINT index = sc->GetCurrentBackBufferIndex(); if (index >= r->frames.size()) return false;
    auto& frame = r->frames[index];
    ComPtr<ID3D12Resource> backBuffer;
    if (FAILED(sc->GetBuffer(index, IID_PPV_ARGS(&backBuffer)))) return false;
    frame.backBuffer = backBuffer.Get();
    const auto bufferDescription = backBuffer->GetDesc(); r->width = static_cast<UINT>(bufferDescription.Width); r->height = bufferDescription.Height;
    if (frame.fence && r->fence->GetCompletedValue() < frame.fence) return false;
    HRESULT hr = frame.allocator->Reset(); if (FAILED(hr)) return false;
    if (FAILED(hr = r->commands->Reset(frame.allocator.Get(), r->pipeline.Get()))) return false;
    if (FAILED(hr = r->Upload(frame, *f, index))) { r->commands->Close(); ReportError(D3D12, hr); return false; }
    auto* c = r->commands.Get();
    auto barrier = Transition(frame.backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    c->ResourceBarrier(1, &barrier);
    auto target = r->targets->GetCPUDescriptorHandleForHeapStart(); target.ptr += static_cast<SIZE_T>(index) * r->targetStep;
    r->device->CreateRenderTargetView(backBuffer.Get(), nullptr, target);
    c->OMSetRenderTargets(1, &target, FALSE, nullptr); c->SetGraphicsRootSignature(r->root.Get());
    auto* heap = r->images.Get(); c->SetDescriptorHeaps(1, &heap);
    const auto rect = WindowPlacement(*f, r->window, r->width, r->height);
    const float constants[]{rect.x * 2 / r->width - 1, 1 - rect.y * 2 / r->height,
        rect.width * 2 / r->width, -rect.height * 2 / r->height};
    c->SetGraphicsRoot32BitConstants(0, 4, constants, 0);
    auto image = r->images->GetGPUDescriptorHandleForHeapStart(); image.ptr += static_cast<UINT64>(index) * r->imageStep;
    c->SetGraphicsRootDescriptorTable(1, image);
    const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(r->width), static_cast<float>(r->height), 0, 1};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(r->width), static_cast<LONG>(r->height)};
    c->RSSetViewports(1, &viewport); c->RSSetScissorRects(1, &scissor);
    c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP); c->DrawInstanced(4, 1, 0, 0);
    barrier = Transition(frame.backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    c->ResourceBarrier(1, &barrier);
    if (FAILED(hr = c->Close())) { ReportError(D3D12, hr); return false; }
    ID3D12CommandList* list = c; queue->ExecuteCommandLists(1, &list);
    if (FAILED(hr = queue->Signal(r->fence.Get(), ++r->nextFence))) { ReportError(D3D12, hr); return false; }
    frame.fence = r->nextFence;
    ReportDraw(D3D12, r->width, r->height); return true;
}
}
