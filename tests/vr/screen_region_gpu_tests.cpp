#include <std_include.hpp>
#include "spatial_panel_renderer.hpp"
#include "reference_panel.hpp"
#include "screen_layer_region.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <random>

using Microsoft::WRL::ComPtr;
using namespace vr::spatial_panel;
void require(HRESULT hr) { if(FAILED(hr)) { std::cerr<<"HRESULT "<<std::hex<<hr<<'\n'; std::abort(); } }
struct target { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11RenderTargetView> view; };
target make_target(ID3D11Device* device,unsigned w,unsigned h,DXGI_FORMAT format) {
 D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
 d.Format=format;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
 target t;require(device->CreateTexture2D(&d,nullptr,&t.texture));require(device->CreateRenderTargetView(t.texture.Get(),nullptr,&t.view));return t;
}
std::vector<std::byte> read(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* texture,unsigned bpp) {
 D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
 ComPtr<ID3D11Texture2D> staging;require(device->CreateTexture2D(&d,nullptr,&staging));context->CopyResource(staging.Get(),texture);
 D3D11_MAPPED_SUBRESOURCE m{};require(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m));
 std::vector<std::byte> result(d.Width*d.Height*bpp);
 for(unsigned y=0;y<d.Height;++y)std::memcpy(result.data()+y*d.Width*bpp,static_cast<const std::byte*>(m.pData)+y*m.RowPitch,d.Width*bpp);
 context->Unmap(staging.Get(),0);return result;
}
int main(int argc,char**) {
 const auto extreme=screen_layer_region({-1e30f,0,1e30f,1},{},1024,1024,false);
 assert(extreme.draw.left==0 && extreme.draw.right==1024 && extreme.copy.bottom==1024);
 ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
 require(D3D11CreateDevice(nullptr,argc>1?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
 renderer optimized;vr::spatial_panel_reference::renderer reference;std::mt19937 random(17);
 unsigned cases{};std::uint64_t copied{},full_copied{};
 for(auto format:{DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R8G8B8A8_UNORM}) {
  const unsigned bpp=format==DXGI_FORMAT_R16G16B16A16_FLOAT?8:4;
  for(unsigned trial=0;trial<60;++trial) {
   unsigned w=trial%2?257:320,h=trial%2?193:240;
   auto old=make_target(device.Get(),w,h,format),now=make_target(device.Get(),w,h,format);
   std::vector<std::uint16_t> initial(w*h*bpp/2);
   for(auto& v:initial)v=format==DXGI_FORMAT_R16G16B16A16_FLOAT?std::uint16_t(0x2800+random()%0x1400):std::uint16_t(random());
   context->UpdateSubresource(old.texture.Get(),0,nullptr,initial.data(),w*bpp,0);
   context->UpdateSubresource(now.texture.Get(),0,nullptr,initial.data(),w*bpp,0);
   auto ink=make_target(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM),shade=make_target(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM);
   std::vector<std::uint32_t> pixels(64*64);for(auto& p:pixels)p=random();
   context->UpdateSubresource(ink.texture.Get(),0,nullptr,pixels.data(),64*4,0);
   const float shadow_color[]{.05f,.02f,.01f,.2f};context->ClearRenderTargetView(shade.view.Get(),shadow_color);
   ComPtr<ID3D11ShaderResourceView> ink_view,shade_view;
   require(device->CreateShaderResourceView(ink.texture.Get(),nullptr,&ink_view));require(device->CreateShaderResourceView(shade.texture.Get(),nullptr,&shade_view));
   for(unsigned layer=0;layer<3;++layer) {
    float x=trial%10==0?1.5f:float(int(random()%400)-100)/400.f;
    float y=float(int(random()%400)-100)/400.f;
    float sx=.15f+float(random()%300)/400.f,sy=.12f+float(random()%250)/400.f;
    if(trial%11==0){x=0;y=0;sx=sy=1;}
    projected_quad canvas{{{2*x-1,1-2*y,.5f,1},{2*(x+sx)-1,1-2*y,.5f,1},{2*x-1,1-2*(y+sy),.5f,1},{2*(x+sx)-1,1-2*(y+sy),.5f,1}}};
    vec4 fade=trial%7==0?vec4{.05f,.02f,.01f,.2f}:vec4{};
    std::array<blur_region,2> blur{{{{0,0,1,1},.8f},{{.2f,.3f,.9f,.8f},.7f}}};
    std::span<const blur_region> regions=trial%3?std::span<const blur_region>(blur):std::span<const blur_region>{};
    auto* shadow=trial%2?shade_view.Get():nullptr;
    assert(reference.draw_screen_layer(context.Get(),ink_view.Get(),old.view.Get(),canvas,w,h,fade,regions,shadow));
    assert(optimized.draw_screen_layer(context.Get(),ink_view.Get(),now.view.Get(),canvas,w,h,fade,regions,shadow));
    const auto a=read(device.Get(),context.Get(),old.texture.Get(),bpp),b=read(device.Get(),context.Get(),now.texture.Get(),bpp);
    if(a!=b){std::cerr<<"pixel mismatch format="<<format<<" trial="<<trial<<" layer="<<layer<<'\n';return 1;}
    auto region=screen_layer_region({x,y,sx,sy},fade,w,h,!regions.empty());
    if(!region.empty())copied+=std::uint64_t(region.copy.right-region.copy.left)*(region.copy.bottom-region.copy.top)*bpp;
    full_copied+=std::uint64_t(w)*h*bpp;++cases;
   }
  }
 }
 assert(copied<full_copied);
 std::cout<<cases<<" rendered outputs bit-identical; modeled copy bytes "<<full_copied<<" -> "<<copied<<'\n';
 // Query actual shader work in an isolated, warmed-up draw. This is not a
 // campaign FPS benchmark and adds no queries to the shipping renderer.
 auto output=make_target(device.Get(),1024,1024,DXGI_FORMAT_R16G16B16A16_FLOAT);
 auto ink=make_target(device.Get(),64,64,DXGI_FORMAT_R8G8B8A8_UNORM);
 const float color[]{.2f,.1f,.05f,.5f};context->ClearRenderTargetView(ink.view.Get(),color);
 context->ClearRenderTargetView(output.view.Get(),color);
 ComPtr<ID3D11ShaderResourceView> ink_view;require(device->CreateShaderResourceView(ink.texture.Get(),nullptr,&ink_view));
 const projected_quad center{{{-.5f,.5f,.5f,1},{.5f,.5f,.5f,1},{-.5f,-.5f,.5f,1},{.5f,-.5f,.5f,1}}};
 D3D11_RECT preserved{7,9,30,40};context->RSSetScissorRects(1,&preserved);
 auto count=[&](auto& renderer) {
  D3D11_QUERY_DESC d{D3D11_QUERY_PIPELINE_STATISTICS,0};ComPtr<ID3D11Query> q;require(device->CreateQuery(&d,&q));
  context->Begin(q.Get());assert(renderer.draw_screen_layer(context.Get(),ink_view.Get(),output.view.Get(),center,1024,1024,{}));context->End(q.Get());
  D3D11_QUERY_DATA_PIPELINE_STATISTICS data{};const auto deadline=GetTickCount64()+10000;
  HRESULT hr{};while((hr=context->GetData(q.Get(),&data,sizeof(data),0))==S_FALSE){assert(GetTickCount64()<deadline);Sleep(1);}require(hr);
  UINT n=1;D3D11_RECT actual{};context->RSGetScissorRects(&n,&actual);assert(n==1 && std::memcmp(&actual,&preserved,sizeof(actual))==0);
  return data.PSInvocations;
 };
 const auto original_invocations=count(reference),optimized_invocations=count(optimized);
 assert(optimized_invocations<original_invocations);
 std::cout<<"Measured pixel shader invocations (center half-width/height canvas): "<<original_invocations<<" -> "<<optimized_invocations<<'\n';
}
