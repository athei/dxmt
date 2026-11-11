#pragma once
#include "d3d11_device.hpp"
#include <vector>

namespace dxmt {

class D3D11DXGISurface : public IDXGISurface2 {

public:
  D3D11DXGISurface(ID3D11Resource *pResource);

  D3D11DXGISurface(ID3D11Resource *pParentResource, UINT Subresource);

  ~D3D11DXGISurface();

  ULONG STDMETHODCALLTYPE AddRef();

  ULONG STDMETHODCALLTYPE Release();

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject);

  HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData);

  HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void *pData);

  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown);

  HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent);

  HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppDevice);

  HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SURFACE_DESC *pDesc);

  HRESULT STDMETHODCALLTYPE Map(DXGI_MAPPED_RECT *pLockedRect, UINT MapFlags);

  HRESULT STDMETHODCALLTYPE Unmap();

  HRESULT STDMETHODCALLTYPE GetDC(BOOL Discard, HDC *phdc);

  HRESULT STDMETHODCALLTYPE ReleaseDC(RECT *pDirtyRect);

  HRESULT STDMETHODCALLTYPE GetResource(REFIID riid, void **ppParentResource, UINT *pSubresourceIndex);

private:
  HRESULT GetDCDesc(D3D11_TEXTURE2D_DESC *pDesc, UINT *pWidth, UINT *pHeight);

  ID3D11Resource *resource_;
  UINT subresource_;
  bool surface_from_subresource_;

  /* What GetDC() hands out: a DIB section on dc_bits_, which ReleaseDC()
     writes back to the subresource */
  HDC dc_ = nullptr;
  HANDLE dc_bitmap_ = nullptr;
  std::vector<uint32_t> dc_bits_;
  Com<ID3D11Texture2D> dc_readback_;
  bool dc_acquired_ = false;
};

} // namespace dxmt