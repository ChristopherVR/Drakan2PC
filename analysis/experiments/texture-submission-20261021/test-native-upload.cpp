#include "rt_native_upload.h"
#include <cstdio>
#include <vector>
#include <cassert>
using Microsoft::WRL::ComPtr;
int main(){
 ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> ctx;
 assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&ctx)));
 const char code[]="[numthreads(1,1,1)] void main(uint3 p:SV_DispatchThreadID) {}";
 ComPtr<ID3DBlob> binary;ComPtr<ID3D11ComputeShader> keepShader;
 assert(SUCCEEDED(D3DCompile(code,sizeof(code)-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&binary,nullptr)));
 assert(SUCCEEDED(dev->CreateComputeShader(binary->GetBufferPointer(),binary->GetBufferSize(),nullptr,&keepShader)));
 D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
 ComPtr<ID3D11Buffer> keepConstants;assert(SUCCEEDED(dev->CreateBuffer(&cb,nullptr,&keepConstants)));
 auto* buffer=keepConstants.Get();ctx->CSSetShader(keepShader.Get(),nullptr,0);ctx->CSSetConstantBuffers(0,1,&buffer);
 D3D11_TEXTURE2D_DESC keepDesc{};keepDesc.Width=keepDesc.Height=8;keepDesc.MipLevels=keepDesc.ArraySize=keepDesc.SampleDesc.Count=1;
 keepDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;keepDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
 ComPtr<ID3D11Texture2D> keepInput,keepOutput;ComPtr<ID3D11ShaderResourceView> keepSrv;ComPtr<ID3D11UnorderedAccessView> keepUav;
 assert(SUCCEEDED(dev->CreateTexture2D(&keepDesc,nullptr,&keepInput)));assert(SUCCEEDED(dev->CreateShaderResourceView(keepInput.Get(),nullptr,&keepSrv)));
 keepDesc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
 assert(SUCCEEDED(dev->CreateTexture2D(&keepDesc,nullptr,&keepOutput)));assert(SUCCEEDED(dev->CreateUnorderedAccessView(keepOutput.Get(),nullptr,&keepUav)));
 auto* srv=keepSrv.Get();auto* uav=keepUav.Get();ctx->CSSetShaderResources(0,1,&srv);ctx->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
 gsr::NativeUpload upload;size_t checked=0;
 for(unsigned scale=1;scale<=8;++scale)for(unsigned w:{37u,128u,7u})for(unsigned rows:{19u,32u,3u})for(unsigned y0:{0u,3u}) {
  const unsigned W=w*scale,H=(rows+y0+2)*scale;
  std::vector<unsigned> pixels(size_t(w)*rows),initial(size_t(W)*H,0x13fe5704u);
  for(size_t i=0;i<pixels.size();++i)pixels[i]=unsigned(i*2654435761u);
  D3D11_TEXTURE2D_DESC d{};d.Width=W;d.Height=H;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
  d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET;
  D3D11_SUBRESOURCE_DATA init{initial.data(),W*4,0};ComPtr<ID3D11Texture2D> target;ComPtr<ID3D11RenderTargetView> rtv;
  assert(SUCCEEDED(dev->CreateTexture2D(&d,&init,&target)));assert(SUCCEEDED(dev->CreateRenderTargetView(target.Get(),nullptr,&rtv)));
  auto* view=rtv.Get();ctx->OMSetRenderTargets(1,&view,nullptr);
  auto* result=upload.expand(dev.Get(),ctx.Get(),pixels.data(),w,rows,scale);assert(result);
  D3D11_BOX box{0,0,0,W,rows*scale,1};ctx->CopySubresourceRegion(target.Get(),0,0,y0*scale,0,result,0,&box);
  ComPtr<ID3D11ComputeShader> restored;ComPtr<ID3D11Buffer> restoredCb;ComPtr<ID3D11RenderTargetView> restoredRtv;
  ctx->CSGetShader(&restored,nullptr,nullptr);ctx->CSGetConstantBuffers(0,1,&restoredCb);ctx->OMGetRenderTargets(1,&restoredRtv,nullptr);
  assert(restored.Get()==keepShader.Get() && restoredCb.Get()==keepConstants.Get() && restoredRtv.Get()==rtv.Get());
  ComPtr<ID3D11ShaderResourceView> restoredSrv;ComPtr<ID3D11UnorderedAccessView> restoredUav;
  ctx->CSGetShaderResources(0,1,&restoredSrv);ctx->CSGetUnorderedAccessViews(0,1,&restoredUav);
  assert(restoredSrv.Get()==keepSrv.Get() && restoredUav.Get()==keepUav.Get());
  d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ComPtr<ID3D11Texture2D> staging;
  assert(SUCCEEDED(dev->CreateTexture2D(&d,nullptr,&staging)));ctx->CopyResource(staging.Get(),target.Get());
  D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
  for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
   auto actual=reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(map.pData)+y*map.RowPitch)[x];
   unsigned expected=(y>=y0*scale && y<(y0+rows)*scale)?pixels[size_t(y/scale-y0)*w+x/scale]:0x13fe5704u;
   if(actual!=expected){std::printf("FAIL %ux%u scale%u y0%u pixel%u,%u %08x != %08x\n",w,rows,scale,y0,x,y,actual,expected);return 1;}++checked;
  }
  ctx->Unmap(staging.Get(),0);
 }
 assert(!upload.expand(dev.Get(),ctx.Get(),nullptr,0,1,4));
 std::printf("GPU upload expansion: %zu exact RGBA checks; scale 1..8, offsets, resource reuse, untouched rows and pipeline state passed\n",checked);
}
