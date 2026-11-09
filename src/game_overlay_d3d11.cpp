#include "game_overlay_runtime.h"
#include "game_overlay_shaders.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <memory>
#include <unordered_map>

namespace GameOverlay {
using Microsoft::WRL::ComPtr;
namespace {
template<class T> struct ShaderState {
    ComPtr<T> shader;
    ID3D11ClassInstance* classes[256]{};
    UINT count = 256;
    ~ShaderState() { for (UINT i = 0; i < count; ++i) if (classes[i]) classes[i]->Release(); }
};
// Save precisely the state changed by the quad pass, including dynamic shader linkage.
struct SavedState {
    ID3D11DeviceContext* c;
    ComPtr<ID3D11RenderTargetView> targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT];
    ComPtr<ID3D11DepthStencilView> depthView;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11InputLayout> input;
    ComPtr<ID3D11Buffer> constant;
    ComPtr<ID3D11ShaderResourceView> texture;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11Predicate> predicate;
    ShaderState<ID3D11VertexShader> vs;
    ShaderState<ID3D11PixelShader> ps;
    ShaderState<ID3D11GeometryShader> gs;
    ShaderState<ID3D11HullShader> hs;
    ShaderState<ID3D11DomainShader> ds;
    FLOAT factors[4]{};
    UINT sampleMask = 0, stencilRef = 0;
    BOOL predicateValue = FALSE;
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
    UINT viewportCount = ARRAYSIZE(viewports);
    SavedState(ID3D11DeviceContext* context) : c(context) {
        ID3D11RenderTargetView* raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        c->OMGetRenderTargets(ARRAYSIZE(raw), raw, &depthView);
        for (UINT i = 0; i < ARRAYSIZE(raw); ++i) targets[i].Attach(raw[i]);
        c->OMGetBlendState(&blend, factors, &sampleMask); c->OMGetDepthStencilState(&depth, &stencilRef);
        c->RSGetState(&raster); c->RSGetViewports(&viewportCount, viewports);
        c->IAGetInputLayout(&input); c->IAGetPrimitiveTopology(&topology);
        c->VSGetConstantBuffers(0, 1, &constant); c->PSGetShaderResources(0, 1, &texture); c->PSGetSamplers(0, 1, &sampler);
        c->GetPredication(&predicate, &predicateValue);
        c->VSGetShader(&vs.shader, vs.classes, &vs.count); c->PSGetShader(&ps.shader, ps.classes, &ps.count);
        c->GSGetShader(&gs.shader, gs.classes, &gs.count); c->HSGetShader(&hs.shader, hs.classes, &hs.count);
        c->DSGetShader(&ds.shader, ds.classes, &ds.count);
    }
    ~SavedState() {
        ID3D11RenderTargetView* raw[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        for (UINT i = 0; i < ARRAYSIZE(raw); ++i) raw[i] = targets[i].Get();
        c->OMSetRenderTargets(ARRAYSIZE(raw), raw, depthView.Get());
        c->OMSetBlendState(blend.Get(), factors, sampleMask); c->OMSetDepthStencilState(depth.Get(), stencilRef);
        c->RSSetState(raster.Get()); c->RSSetViewports(viewportCount, viewports);
        c->IASetInputLayout(input.Get()); c->IASetPrimitiveTopology(topology);
        auto* cb = constant.Get(); c->VSSetConstantBuffers(0, 1, &cb);
        auto* srv = texture.Get(); c->PSSetShaderResources(0, 1, &srv);
        auto* smp = sampler.Get(); c->PSSetSamplers(0, 1, &smp);
        c->SetPredication(predicate.Get(), predicateValue);
        c->VSSetShader(vs.shader.Get(), vs.classes, vs.count); c->PSSetShader(ps.shader.Get(), ps.classes, ps.count);
        c->GSSetShader(gs.shader.Get(), gs.classes, gs.count); c->HSSetShader(hs.shader.Get(), hs.classes, hs.count);
        c->DSSetShader(ds.shader.Get(), ds.classes, ds.count);
    }
};
struct Renderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Texture2D> image;
    ComPtr<ID3D11ShaderResourceView> imageView;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11SamplerState> sampler;
    uint64_t generation = 0;
    uint32_t owner = 0, imageWidth = 0, imageHeight = 0;
    HRESULT Init(ID3D11Device* d) {
        device = d; device->GetImmediateContext(&context);
        ComPtr<ID3DBlob> v, p;
        HRESULT result = CompileQuad(v, p);
        if (FAILED(result)) return result;
        if (FAILED(result = device->CreateVertexShader(v->GetBufferPointer(), v->GetBufferSize(), nullptr, &vertex)) ||
            FAILED(result = device->CreatePixelShader(p->GetBufferPointer(), p->GetBufferSize(), nullptr, &pixel))) return result;
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth = 16; cb.Usage = D3D11_USAGE_DEFAULT; cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (FAILED(result = device->CreateBuffer(&cb, nullptr, &constants))) return result;
        D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(result = device->CreateBlendState(&bd, &blend))) return result;
        D3D11_DEPTH_STENCIL_DESC dd{}; dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        if (FAILED(result = device->CreateDepthStencilState(&dd, &depth))) return result;
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
        if (FAILED(result = device->CreateRasterizerState(&rd, &raster))) return result;
        D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS; sd.MaxLOD = D3D11_FLOAT32_MAX;
        return device->CreateSamplerState(&sd, &sampler);
    }
    HRESULT Upload(const Frame& f) {
        if (!image || imageWidth != f.header.width || imageHeight != f.header.height) {
            imageView.Reset(); image.Reset();
            D3D11_TEXTURE2D_DESC desc{}; desc.Width = f.header.width; desc.Height = f.header.height;
            desc.MipLevels = desc.ArraySize = 1; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            HRESULT hr = device->CreateTexture2D(&desc, nullptr, &image);
            if (FAILED(hr)) return hr;
            if (FAILED(hr = device->CreateShaderResourceView(image.Get(), nullptr, &imageView))) return hr;
            imageWidth = desc.Width; imageHeight = desc.Height; generation = 0;
        }
        if (owner != f.header.ownerPid || generation != f.header.generation) {
            context->UpdateSubresource(image.Get(), 0, nullptr, f.pixels.data(), f.header.stride, 0);
            owner = f.header.ownerPid; generation = f.header.generation;
        }
        return S_OK;
    }
};
// Process teardown runs under the loader lock, after driver DLLs may have detached.
// Release caches on swapchain resize, never through C++ static GPU destructors.
auto& renderers = *new std::unordered_map<void*, std::unique_ptr<Renderer>>;
}
void ResetD3D11(void* chain) { renderers.erase(chain); }
bool DrawD3D11(void* pointer) {
    const Frame* f = ReadFrame(D3D11); if (!f) return false;
    auto* chain = static_cast<IDXGISwapChain*>(pointer);
    ComPtr<ID3D11Device> device;
    if (FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) return false;
    auto& r = renderers[pointer];
    if (!r || r->device.Get() != device.Get()) {
        r = std::make_unique<Renderer>();
        const auto hr = r->Init(device.Get());
        if (FAILED(hr)) { ReportError(D3D11, hr); r.reset(); return false; }
    }
    ComPtr<ID3D11Texture2D> buffer;
    HRESULT hr = chain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    if (FAILED(hr)) return false;
    D3D11_TEXTURE2D_DESC desc{}; buffer->GetDesc(&desc);
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(hr = device->CreateRenderTargetView(buffer.Get(), nullptr, &target))) { ReportError(D3D11, hr); return false; }
    DXGI_SWAP_CHAIN_DESC sc{}; chain->GetDesc(&sc);
    const auto rect = WindowPlacement(*f, sc.OutputWindow, desc.Width, desc.Height);
    SavedState saved(r->context.Get());
    if (FAILED(hr = r->Upload(*f))) { ReportError(D3D11, hr); return false; }
    const float cb[]{rect.x * 2 / desc.Width - 1, 1 - rect.y * 2 / desc.Height,
        rect.width * 2 / desc.Width, -rect.height * 2 / desc.Height};
    auto* c = r->context.Get();
    c->UpdateSubresource(r->constants.Get(), 0, nullptr, cb, 0, 0);
    auto* rt = target.Get(); c->OMSetRenderTargets(1, &rt, nullptr);
    const float blendFactors[4]{}; c->OMSetBlendState(r->blend.Get(), blendFactors, 0xffffffff);
    c->OMSetDepthStencilState(r->depth.Get(), 0); c->RSSetState(r->raster.Get());
    D3D11_VIEWPORT viewport{0, 0, static_cast<float>(desc.Width), static_cast<float>(desc.Height), 0, 1};
    c->RSSetViewports(1, &viewport); c->SetPredication(nullptr, FALSE);
    c->IASetInputLayout(nullptr); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    c->VSSetShader(r->vertex.Get(), nullptr, 0); c->PSSetShader(r->pixel.Get(), nullptr, 0);
    c->GSSetShader(nullptr, nullptr, 0); c->HSSetShader(nullptr, nullptr, 0); c->DSSetShader(nullptr, nullptr, 0);
    auto* constants = r->constants.Get(); c->VSSetConstantBuffers(0, 1, &constants);
    auto* imageView = r->imageView.Get(); c->PSSetShaderResources(0, 1, &imageView);
    auto* sampler = r->sampler.Get(); c->PSSetSamplers(0, 1, &sampler);
    c->Draw(4, 0);
    ReportDraw(D3D11, desc.Width, desc.Height);
    return true;
}
}
