/*
 * Copyright 2026 Feifan He for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include "d3d11_surface.hpp"
#include "com/com_object.hpp"
#include "d3d11_resource.hpp"

namespace dxmt {

D3D11DXGISurface::D3D11DXGISurface(ID3D11Resource *pResource) :
    resource_(pResource),
    subresource_(0),
    surface_from_subresource_(false) {}

D3D11DXGISurface::D3D11DXGISurface(ID3D11Resource *pParentResource, UINT Subresource) :
    resource_(pParentResource),
    subresource_(Subresource),
    surface_from_subresource_(true) {}

D3D11DXGISurface::~D3D11DXGISurface() {
  if (dc_) {
    D3DKMT_DESTROYDCFROMMEMORY destroy = {};
    destroy.hDc = dc_;
    destroy.hBitmap = dc_bitmap_;
    D3DKMTDestroyDCFromMemory(&destroy);
  }
}

ULONG STDMETHODCALLTYPE
D3D11DXGISurface::AddRef() {
  return resource_->AddRef();
}

ULONG STDMETHODCALLTYPE
D3D11DXGISurface::Release() {
  return resource_->Release();
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::QueryInterface(REFIID riid, void **ppvObject) {

  InitReturnPtr(ppvObject);

  // Only a subset of interfaces are available for subresource surfaces
  if (surface_from_subresource_) {
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISurface) || riid == __uuidof(IDXGISurface1) || riid == __uuidof(IDXGISurface2)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  return resource_->QueryInterface(riid, ppvObject);
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::GetPrivateData(REFGUID Name, UINT *pDataSize, void *pData) {
  return resource_->GetPrivateData(Name, pDataSize, pData);
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::SetPrivateData(REFGUID Name, UINT DataSize, const void *pData) {
  return resource_->SetPrivateData(Name, DataSize, pData);
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::SetPrivateDataInterface(REFGUID Name, const IUnknown *pUnknown) {
  return resource_->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::GetParent(REFIID riid, void **ppParent) {
  return GetDevice(riid, ppParent);
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::GetDevice(REFIID riid, void **ppDevice) {
  Com<ID3D11Device> device;
  resource_->GetDevice(&device);
  return device->QueryInterface(riid, ppDevice);
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::GetDesc(DXGI_SURFACE_DESC *pDesc) {
  auto d3d11_resource = GetResourceCommon(resource_);

  if (!pDesc)
    return DXGI_ERROR_INVALID_CALL;

  union {
    D3D11_BUFFER_DESC Buffer;
    D3D11_TEXTURE1D_DESC Texture1D;
    D3D11_TEXTURE2D_DESC Texture2D;
    D3D11_TEXTURE3D_DESC Texture3D;
  } desc;
  D3D11_RESOURCE_DIMENSION dimension;
  d3d11_resource->GetDesc(&desc);
  d3d11_resource->GetType(&dimension);

  // Decide by the resource dimension: staging resources have no Metal texture
  // or buffer behind them, but their surfaces have a description all the same.
  // A surface of a whole resource reports mip level 0.
  switch (dimension) {
  case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
    UINT level = surface_from_subresource_ ? subresource_ % desc.Texture1D.MipLevels : 0;
    pDesc->Width = std::max(1u, desc.Texture1D.Width >> level);
    pDesc->Height = 1U;
    pDesc->Format = desc.Texture1D.Format;
    pDesc->SampleDesc = {1, 0};
    return S_OK;
  }
  case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
    UINT level = surface_from_subresource_ ? subresource_ % desc.Texture2D.MipLevels : 0;
    pDesc->Width = std::max(1u, desc.Texture2D.Width >> level);
    pDesc->Height = std::max(1u, desc.Texture2D.Height >> level);
    pDesc->Format = desc.Texture2D.Format;
    pDesc->SampleDesc = desc.Texture2D.SampleDesc;
    return S_OK;
  }
  case D3D11_RESOURCE_DIMENSION_TEXTURE3D: {
    UINT level = surface_from_subresource_ ? subresource_ % desc.Texture3D.MipLevels : 0;
    pDesc->Width = std::max(1u, desc.Texture3D.Width >> level);
    pDesc->Height = std::max(1u, desc.Texture3D.Height >> level);
    pDesc->Format = desc.Texture3D.Format;
    pDesc->SampleDesc = {1, 0};
    return S_OK;
  }
  case D3D11_RESOURCE_DIMENSION_BUFFER:
    // Only a subresource surface describes a buffer.
    if (!surface_from_subresource_)
      return DXGI_ERROR_INVALID_CALL;
    pDesc->Width = desc.Buffer.ByteWidth;
    pDesc->Height = 1;
    pDesc->Format = DXGI_FORMAT_UNKNOWN;
    pDesc->SampleDesc = {1, 0};
    return S_OK;
  default:
    return DXGI_ERROR_INVALID_CALL;
  }
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::Map(DXGI_MAPPED_RECT *pLockedRect, UINT MapFlags) {
  Com<ID3D11Device> device;
  Com<ID3D11DeviceContext> context;

  resource_->GetDevice(&device);
  device->GetImmediateContext(&context);

  if (pLockedRect) {
    pLockedRect->Pitch = 0;
    pLockedRect->pBits = nullptr;
  }

  D3D11_MAP map_type;

  if (MapFlags & DXGI_MAP_READ && MapFlags & DXGI_MAP_WRITE)
    map_type = D3D11_MAP_READ_WRITE;
  else if (MapFlags & DXGI_MAP_READ)
    map_type = D3D11_MAP_READ;
  else if (MapFlags & DXGI_MAP_WRITE && MapFlags & DXGI_MAP_DISCARD)
    map_type = D3D11_MAP_WRITE_DISCARD;
  else if (MapFlags & DXGI_MAP_WRITE)
    map_type = D3D11_MAP_WRITE;
  else
    return DXGI_ERROR_INVALID_CALL;

  D3D11_MAPPED_SUBRESOURCE sr;
  HRESULT hr = context->Map(resource_, subresource_, map_type, 0, pLockedRect ? &sr : nullptr);

  if (hr != S_OK)
    return hr;

  pLockedRect->Pitch = sr.RowPitch;
  pLockedRect->pBits = reinterpret_cast<unsigned char *>(sr.pData);
  return hr;
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::Unmap() {
  Com<ID3D11Device> device;
  Com<ID3D11DeviceContext> context;

  resource_->GetDevice(&device);
  device->GetImmediateContext(&context);

  context->Unmap(resource_, subresource_);
  return S_OK;
}

/* D3DDDIFMT_A8R8G8B8, the layout of the B8G8R8A8 formats */
constexpr UINT kDCFormat = 21;

/* Only single-sampled 2D textures created with
   D3D11_RESOURCE_MISC_GDI_COMPATIBLE and one of the B8G8R8A8 formats have a
   DC; a swap chain's back buffers get the flag without CreateTexture2D's
   checks */
HRESULT
D3D11DXGISurface::GetDCDesc(D3D11_TEXTURE2D_DESC *pDesc, UINT *pWidth, UINT *pHeight) {
  D3D11_RESOURCE_DIMENSION dimension;
  resource_->GetType(&dimension);
  if (dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    return DXGI_ERROR_INVALID_CALL;

  GetResourceCommon(resource_)->GetDesc(pDesc);
  if (!(pDesc->MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE) || pDesc->SampleDesc.Count > 1)
    return DXGI_ERROR_INVALID_CALL;
  switch (pDesc->Format) {
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    break;
  default:
    return DXGI_ERROR_INVALID_CALL;
  }

  UINT level = subresource_ % pDesc->MipLevels;
  *pWidth = std::max(1u, pDesc->Width >> level);
  *pHeight = std::max(1u, pDesc->Height >> level);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::GetDC(BOOL Discard, HDC *phdc) {
  // A failed call leaves *phdc as it is, as on Windows
  if (!phdc)
    return E_INVALIDARG;

  D3D11_TEXTURE2D_DESC desc;
  UINT width, height;
  if (FAILED(GetDCDesc(&desc, &width, &height)) || dc_acquired_)
    return DXGI_ERROR_INVALID_CALL;

  if (!dc_) {
    dc_bits_.resize(width * height);

    D3DKMT_CREATEDCFROMMEMORY create = {};
    create.pMemory = dc_bits_.data();
    create.Format = kDCFormat;
    create.Width = width;
    create.Height = height;
    create.Pitch = width * sizeof(uint32_t);
    create.hDeviceDc = CreateCompatibleDC(nullptr);
    NTSTATUS status = D3DKMTCreateDCFromMemory(&create);
    DeleteDC(create.hDeviceDc);
    if (status) {
      ERR("DXGISurface::GetDC: Failed to create DC, status ", status);
      return E_FAIL;
    }
    dc_ = create.hDc;
    dc_bitmap_ = create.hBitmap;
  }

  if (!Discard) {
    Com<ID3D11Device> device;
    Com<ID3D11DeviceContext> context;
    resource_->GetDevice(&device);
    device->GetImmediateContext(&context);

    if (!dc_readback_) {
      D3D11_TEXTURE2D_DESC readback_desc = {};
      readback_desc.Width = width;
      readback_desc.Height = height;
      readback_desc.MipLevels = 1;
      readback_desc.ArraySize = 1;
      readback_desc.Format = desc.Format;
      readback_desc.SampleDesc = {1, 0};
      readback_desc.Usage = D3D11_USAGE_STAGING;
      readback_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
      if (FAILED(device->CreateTexture2D(&readback_desc, nullptr, &dc_readback_))) {
        ERR("DXGISurface::GetDC: Failed to create readback texture");
        return E_FAIL;
      }
    }

    context->CopySubresourceRegion(dc_readback_.ptr(), 0, 0, 0, 0, resource_, subresource_, nullptr);
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context->Map(dc_readback_.ptr(), 0, D3D11_MAP_READ, 0, &mapped)))
      return E_FAIL;
    for (UINT row = 0; row < height; row++) {
      memcpy(
          dc_bits_.data() + row * width, static_cast<const char *>(mapped.pData) + row * mapped.RowPitch,
          width * sizeof(uint32_t)
      );
    }
    context->Unmap(dc_readback_.ptr(), 0);
  }

  dc_acquired_ = true;
  *phdc = dc_;
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::ReleaseDC(RECT *pDirtyRect) {
  D3D11_TEXTURE2D_DESC desc;
  UINT width, height;
  if (!dc_acquired_ || FAILED(GetDCDesc(&desc, &width, &height)))
    return DXGI_ERROR_INVALID_CALL;
  dc_acquired_ = false;

  RECT rect = {0, 0, LONG(width), LONG(height)};
  if (pDirtyRect) {
    rect.left = std::max<LONG>(pDirtyRect->left, 0);
    rect.top = std::max<LONG>(pDirtyRect->top, 0);
    rect.right = std::min<LONG>(pDirtyRect->right, width);
    rect.bottom = std::min<LONG>(pDirtyRect->bottom, height);
  }
  if (rect.left >= rect.right || rect.top >= rect.bottom)
    return S_OK;

  Com<ID3D11Device> device;
  Com<ID3D11DeviceContext> context;
  resource_->GetDevice(&device);
  device->GetImmediateContext(&context);

  D3D11_BOX box = {UINT(rect.left), UINT(rect.top), 0, UINT(rect.right), UINT(rect.bottom), 1};
  context->UpdateSubresource(
      resource_, subresource_, &box, dc_bits_.data() + rect.top * width + rect.left, width * sizeof(uint32_t),
      width * height * sizeof(uint32_t)
  );
  return S_OK;
}

HRESULT STDMETHODCALLTYPE
D3D11DXGISurface::GetResource(REFIID riid, void **ppParentResource, UINT *pSubresourceIndex) {
  HRESULT hr;

  if (!ppParentResource)
    return E_POINTER;

  InitReturnPtr(ppParentResource);
  hr = resource_->QueryInterface(riid, ppParentResource);
  if (SUCCEEDED(hr))
    *pSubresourceIndex = subresource_;

  return hr;
}

}; // namespace dxmt