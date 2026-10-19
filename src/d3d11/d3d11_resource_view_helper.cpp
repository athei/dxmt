#include "dxmt_format.hpp"
#include "d3d11_resource.hpp"

namespace dxmt {

void FixDepthStencilFormat(Texture *pTexture, MTL_DXGI_FORMAT_DESC &Desc) {
  switch (pTexture->pixelFormat()) {
  case WMTPixelFormatDepth16Unorm:
    // DXGI_FORMAT_R16_TYPELESS
    // DXGI_FORMAT_R16_UNORM
    Desc.PixelFormat = WMTPixelFormatDepth16Unorm;
    break;
  case WMTPixelFormatDepth32Float:
    // DXGI_FORMAT_R32_TYPELESS
    // DXGI_FORMAT_R32_FLOAT
    Desc.PixelFormat = WMTPixelFormatDepth32Float;
    break;
  default:
    break;
  }
}

template <>
HRESULT
InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture* pTexture,
    D3D11_SHADER_RESOURCE_VIEW_DESC1 &ViewDesc, TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format, metal_format))) {
    ERR("Failed to create SRV due to unsupported format ", ViewDesc.Format);
    return E_FAIL;
  }

  FixDepthStencilFormat(pTexture, metal_format);
  WMTTextureType TextureType = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D_SRV_DIMENSION_TEXTURE1D: {
    if (~ViewDesc.Texture1D.MipLevels == 0)
      ViewDesc.Texture1D.MipLevels = MiplevelCount - ViewDesc.Texture1D.MostDetailedMip;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture1D.MipLevels;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.MipLevels == 0)
      ViewDesc.Texture1DArray.MipLevels = MiplevelCount - ViewDesc.Texture1DArray.MostDetailedMip;
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture1DArray.MipLevels;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2D: {
    if (~ViewDesc.Texture2D.MipLevels == 0)
      ViewDesc.Texture2D.MipLevels = MiplevelCount - ViewDesc.Texture2D.MostDetailedMip;

    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture2D.MipLevels;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.MipLevels == 0)
      ViewDesc.Texture2DArray.MipLevels = MiplevelCount - ViewDesc.Texture2DArray.MostDetailedMip;
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture2DArray.MipLevels;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMS: {
    if (TextureType == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisample;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      // cursed use case: MS view on non-MS texture (SampleCount = 1)
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY: {
    if (~ViewDesc.Texture2DMSArray.ArraySize == 0)
      ViewDesc.Texture2DMSArray.ArraySize = ArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice;
    if (TextureType == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisampleArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    if (TextureType == WMTTextureType2DMultisampleArray) {
      Descriptor.type = WMTTextureType2DMultisampleArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DMSArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DMSArray.ArraySize;
      return S_OK;
    }
    if (TextureType == WMTTextureType2D) {
      // cursed use case: MS view on non-MS texture (SampleCount = 1)
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    if (TextureType == WMTTextureType2DArray) {
      // cursed use case: MS view on non-MS texture (SampleCount = 1)
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DMSArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DMSArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE3D: {
    if (~ViewDesc.Texture3D.MipLevels == 0)
      ViewDesc.Texture3D.MipLevels = MiplevelCount - ViewDesc.Texture3D.MostDetailedMip;
    if (TextureType == WMTTextureType3D) {
      Descriptor.type = WMTTextureType3D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture3D.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.Texture3D.MipLevels;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURECUBE: {
    if (~ViewDesc.TextureCube.MipLevels == 0)
      ViewDesc.TextureCube.MipLevels = MiplevelCount - ViewDesc.TextureCube.MostDetailedMip;
    if (TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureTypeCube;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.TextureCube.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.TextureCube.MipLevels;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 6;
      return S_OK;
    }
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: {
    D3D11_ASSERT((ViewDesc.TextureCubeArray.First2DArrayFace % 6) == 0);
    D3D11_ASSERT((ArraySize % 6) == 0);
    if (~ViewDesc.TextureCubeArray.MipLevels == 0)
      ViewDesc.TextureCubeArray.MipLevels = MiplevelCount - ViewDesc.TextureCubeArray.MostDetailedMip;
    if (~ViewDesc.TextureCubeArray.NumCubes == 0)
      ViewDesc.TextureCubeArray.NumCubes = (ArraySize - ViewDesc.TextureCubeArray.First2DArrayFace) / 6;
    if (TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureTypeCubeArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.TextureCubeArray.MostDetailedMip;
      Descriptor.miplevelCount = ViewDesc.TextureCubeArray.MipLevels;
      Descriptor.firstArraySlice = ViewDesc.TextureCubeArray.First2DArrayFace;
      Descriptor.arraySize = ViewDesc.TextureCubeArray.NumCubes * 6;
      return S_OK;
    }
    break;
  }
  }
  ERR("Unhandled srv creation: \n Source: ", uint32_t(TextureType), "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

template <>
HRESULT
InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture *pTexture,
    D3D11_UNORDERED_ACCESS_VIEW_DESC1 &ViewDesc, TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format,
                                metal_format))) {
    return E_FAIL;
  }

  FixDepthStencilFormat(pTexture, metal_format);
  WMTTextureType TextureType = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1D: {
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2D: {
    D3D11_ASSERT(ViewDesc.Texture2D.PlaneSlice == 0);
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (TextureType == WMTTextureType2D || TextureType == WMTTextureType2DArray ||
        TextureType == WMTTextureTypeCube || TextureType == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      return S_OK;
    }
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE3D: {
    ArraySize = std::max(ArraySize >> ViewDesc.Texture3D.MipSlice, 1u); 
    if (~ViewDesc.Texture3D.WSize == 0)
      ViewDesc.Texture3D.WSize = ArraySize - ViewDesc.Texture3D.FirstWSlice;
    if (TextureType == WMTTextureType3D) {
      // Metal cannot view a range of depth slices of a 3D texture, so the
      // view covers them all, and shaders and clears address them from the
      // first slice instead of FirstWSlice
      if (ViewDesc.Texture3D.WSize != ArraySize) {
        WARN("Created a subview of 3D texture: W slices ", ViewDesc.Texture3D.FirstWSlice, ":",
             ViewDesc.Texture3D.WSize, " of ", ArraySize, " are viewed as all slices");
      }
      Descriptor.type = WMTTextureType3D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture3D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      return S_OK;
    }
    break;
  }
  }
  ERR("Unhandled uav creation: \n Source: ", TextureType,
      "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

inline void
GetRenderTargetSize(Texture *pTexture, uint32_t level, MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc) {
  AttachmentDesc.Width = std::max(1u, pTexture->width() >> level);
  AttachmentDesc.Height = std::max(1u, pTexture->height() >> level);
}

template <>
HRESULT InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture* pTexture,
    D3D11_RENDER_TARGET_VIEW_DESC1 &ViewDesc, MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc, TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format,
                                metal_format))) {
    return E_FAIL;
  }

  AttachmentDesc.DepthPlane = 0;
  AttachmentDesc.RenderTargetArrayLength = 0;
  AttachmentDesc.SampleCount = pTexture->sampleCount();

  auto texture_type = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1D: {
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2D: {
    D3D11_ASSERT(ViewDesc.Texture2D.PlaneSlice == 0);
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMS: {
    if (texture_type == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisample;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      // cursed use case: MS view on non-MS texture (SampleCount = 1)
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY: {
    if (~ViewDesc.Texture2DMSArray.ArraySize == 0)
      ViewDesc.Texture2DMSArray.ArraySize = ArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice;
    if (texture_type == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisampleArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0; // FIXME: we only handle 2dms here!
      Descriptor.arraySize = 1;
      AttachmentDesc.RenderTargetArrayLength = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    if (texture_type == WMTTextureType2DMultisampleArray) {
      Descriptor.type = WMTTextureType2DMultisampleArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DMSArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DMSArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      // cursed use case: MS view on non-MS texture (SampleCount = 1)
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DMSArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DMSArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE3D: {
    ArraySize = std::max(ArraySize >> ViewDesc.Texture3D.MipSlice, 1u); 
    if (~ViewDesc.Texture3D.WSize == 0)
      ViewDesc.Texture3D.WSize = ArraySize - ViewDesc.Texture3D.FirstWSlice;
    if (texture_type == WMTTextureType3D) {
      if (ViewDesc.Texture3D.WSize == 1) {
        Descriptor.type = WMTTextureType3D;
        Descriptor.format = metal_format.PixelFormat;
        Descriptor.firstMiplevel = ViewDesc.Texture3D.MipSlice;
        Descriptor.miplevelCount = 1;
        Descriptor.firstArraySlice = 0;
        Descriptor.arraySize = 1;
        AttachmentDesc.DepthPlane = ViewDesc.Texture3D.FirstWSlice;
        GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
        return S_OK;
      }
      // As for unordered access views, a range of several depth slices is
      // rendered as all slices of the level, from the first one
      if (ViewDesc.Texture3D.WSize != ArraySize) {
        WARN("Created a subview of 3D texture: W slices ", ViewDesc.Texture3D.FirstWSlice, ":",
             ViewDesc.Texture3D.WSize, " of ", ArraySize, " are rendered as all slices");
      }
      Descriptor.type = WMTTextureType3D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture3D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      AttachmentDesc.RenderTargetArrayLength = ArraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  }
  ERR("Unhandled rtv creation: \n Source: ", uint32_t(texture_type),
      "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

template <>
HRESULT
InitializeAndNormalizeViewDescriptor(
    MTLD3D11Device *pDevice, unsigned MiplevelCount, unsigned ArraySize, Texture *pTexture,
    D3D11_DEPTH_STENCIL_VIEW_DESC &ViewDesc, MTL_RENDER_PASS_ATTACHMENT_DESC &AttachmentDesc,
    TextureViewDescriptor &Descriptor
) {
  MTL_DXGI_FORMAT_DESC metal_format;
  if (FAILED(MTLQueryDXGIFormat(pDevice->GetMTLDevice(), ViewDesc.Format, metal_format))) {
    ERR("Failed to create DSV due to unsupported format ", ViewDesc.Format);
    return E_FAIL;
  }

  ViewDesc.Flags = ViewDesc.Flags & 3;
  AttachmentDesc.DepthPlane = 0;
  AttachmentDesc.RenderTargetArrayLength = 0;
  AttachmentDesc.SampleCount = pTexture->sampleCount();

  auto texture_type = pTexture->textureType();
  switch (ViewDesc.ViewDimension) {
  default:
    break;
  case D3D11_DSV_DIMENSION_TEXTURE1D: {
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE1DARRAY: {
    if (~ViewDesc.Texture1DArray.ArraySize == 0)
      ViewDesc.Texture1DArray.ArraySize = ArraySize - ViewDesc.Texture1DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture1DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture1DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture1DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2D: {
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2D.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: {
    if (~ViewDesc.Texture2DArray.ArraySize == 0)
      ViewDesc.Texture2DArray.ArraySize = ArraySize - ViewDesc.Texture2DArray.FirstArraySlice;
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube || texture_type == WMTTextureTypeCubeArray) {
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = ViewDesc.Texture2DArray.MipSlice;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2DMS: {
    if (texture_type == WMTTextureType2DMultisample || texture_type == WMTTextureType2DMultisampleArray) {
      Descriptor.type = WMTTextureType2DMultisample;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray ||
        texture_type == WMTTextureTypeCube) {
      // cursed use case: MS view on non-MS texture (SampleCount = 1)
      Descriptor.type = WMTTextureType2D;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY: {
    if (~ViewDesc.Texture2DMSArray.ArraySize == 0)
      ViewDesc.Texture2DMSArray.ArraySize = ArraySize - ViewDesc.Texture2DMSArray.FirstArraySlice;
    if (texture_type == WMTTextureType2DMultisample) {
      Descriptor.type = WMTTextureType2DMultisampleArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = 0;
      Descriptor.arraySize = 1;
      AttachmentDesc.RenderTargetArrayLength = 1;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    if (texture_type == WMTTextureType2DMultisampleArray) {
      Descriptor.type = WMTTextureType2DMultisampleArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DMSArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DMSArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    if (texture_type == WMTTextureType2D || texture_type == WMTTextureType2DArray) {
      // cursed use case: MS view on non-MS texture (SampleCount = 1)
      Descriptor.type = WMTTextureType2DArray;
      Descriptor.format = metal_format.PixelFormat;
      Descriptor.firstMiplevel = 0;
      Descriptor.miplevelCount = 1;
      Descriptor.firstArraySlice = ViewDesc.Texture2DMSArray.FirstArraySlice;
      Descriptor.arraySize = ViewDesc.Texture2DMSArray.ArraySize;
      AttachmentDesc.RenderTargetArrayLength = Descriptor.arraySize;
      GetRenderTargetSize(pTexture, Descriptor.firstMiplevel, AttachmentDesc);
      return S_OK;
    }
    break;
  }
  }
  ERR("Unhandled dsv creation: \n Source: ", uint32_t(texture_type), "\n Desired: ", ViewDesc.ViewDimension);
  return E_FAIL;
}

namespace {

struct ViewResourceInfo {
  D3D11_RESOURCE_DIMENSION dimension;
  DXGI_FORMAT format;
  UINT mip_levels;
  /* array size, or the depth of a 3D texture */
  UINT layers;
  UINT bind_flags;
};

bool
GetViewResourceInfo(ID3D11Resource *pResource, ViewResourceInfo &Info) {
  auto resource = static_cast<D3D11ResourceCommon *>(pResource);
  pResource->GetType(&Info.dimension);
  switch (Info.dimension) {
  case D3D11_RESOURCE_DIMENSION_BUFFER: {
    D3D11_BUFFER_DESC desc;
    resource->GetDesc(&desc);
    Info = {Info.dimension, DXGI_FORMAT_UNKNOWN, 1, 1, desc.BindFlags};
    return true;
  }
  case D3D11_RESOURCE_DIMENSION_TEXTURE1D: {
    D3D11_TEXTURE1D_DESC desc;
    resource->GetDesc(&desc);
    Info = {Info.dimension, desc.Format, desc.MipLevels, desc.ArraySize, desc.BindFlags};
    return true;
  }
  case D3D11_RESOURCE_DIMENSION_TEXTURE2D: {
    D3D11_TEXTURE2D_DESC desc;
    resource->GetDesc(&desc);
    Info = {Info.dimension, desc.Format, desc.MipLevels, desc.ArraySize, desc.BindFlags};
    return true;
  }
  case D3D11_RESOURCE_DIMENSION_TEXTURE3D: {
    D3D11_TEXTURE3D_DESC desc;
    resource->GetDesc(&desc);
    Info = {Info.dimension, desc.Format, desc.MipLevels, desc.Depth, desc.BindFlags};
    return true;
  }
  default:
    return false;
  }
}

/* The formats that only name a family; R24_UNORM_X8_TYPELESS and the like
   are usable view formats. */
bool
IsTypelessFormat(DXGI_FORMAT Format) {
  switch (Format) {
  case DXGI_FORMAT_R32G32B32A32_TYPELESS:
  case DXGI_FORMAT_R32G32B32_TYPELESS:
  case DXGI_FORMAT_R16G16B16A16_TYPELESS:
  case DXGI_FORMAT_R32G32_TYPELESS:
  case DXGI_FORMAT_R32G8X24_TYPELESS:
  case DXGI_FORMAT_R10G10B10A2_TYPELESS:
  case DXGI_FORMAT_R8G8B8A8_TYPELESS:
  case DXGI_FORMAT_R16G16_TYPELESS:
  case DXGI_FORMAT_R32_TYPELESS:
  case DXGI_FORMAT_R24G8_TYPELESS:
  case DXGI_FORMAT_R8G8_TYPELESS:
  case DXGI_FORMAT_R16_TYPELESS:
  case DXGI_FORMAT_R8_TYPELESS:
  case DXGI_FORMAT_BC1_TYPELESS:
  case DXGI_FORMAT_BC2_TYPELESS:
  case DXGI_FORMAT_BC3_TYPELESS:
  case DXGI_FORMAT_BC4_TYPELESS:
  case DXGI_FORMAT_BC5_TYPELESS:
  case DXGI_FORMAT_B8G8R8A8_TYPELESS:
  case DXGI_FORMAT_B8G8R8X8_TYPELESS:
  case DXGI_FORMAT_BC6H_TYPELESS:
  case DXGI_FORMAT_BC7_TYPELESS:
    return true;
  default:
    return false;
  }
}

/* Video formats are viewed through formats of their planes. */
bool
IsVideoFormat(DXGI_FORMAT Format) {
  return (Format >= DXGI_FORMAT_AYUV && Format <= DXGI_FORMAT_A8P8) ||
         (Format >= DXGI_FORMAT_P208 && Format <= DXGI_FORMAT_V408);
}

bool
AreSrgbVariants(DXGI_FORMAT A, DXGI_FORMAT B) {
  auto linear = [](DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
      return DXGI_FORMAT_B8G8R8X8_UNORM;
    default:
      return format;
    }
  };
  return linear(A) == linear(B);
}

/* A typed resource is viewed in its own format only. Render target views may
   switch between the linear and sRGB variant, which DXGI allows on swap chain
   back buffers; DXMT does not tell those apart from other textures. */
bool
IsValidViewFormat(const ViewResourceInfo &Info, DXGI_FORMAT Format, bool AllowSrgbToggle) {
  if (Format == DXGI_FORMAT_UNKNOWN)
    Format = Info.format;
  if (IsTypelessFormat(Format))
    return false;
  if (Format == Info.format || IsTypelessFormat(Info.format) || IsVideoFormat(Info.format))
    return true;
  return AllowSrgbToggle && AreSrgbVariants(Format, Info.format);
}

/* ~0u selects everything from the first mip level or slice on. */
UINT
ResolveCount(UINT Count, UINT First, UINT Total) {
  return (Count == ~0u && First < Total) ? Total - First : Count;
}

bool
IsInRange(UINT First, UINT Count, UINT Total) {
  return Count && First < Total && Count <= Total - First;
}

struct ViewRange {
  UINT first_level;
  UINT level_count;
  UINT first_layer;
  UINT layer_count;
};

/* The depth of a 3D texture's mip level, 0 past the last level */
UINT
GetLevelDepth(const ViewResourceInfo &Info, UINT Level) {
  return Level < Info.mip_levels ? std::max(1u, Info.layers >> Level) : 0;
}

bool
IsValidRange(const ViewResourceInfo &Info, const ViewRange &Range, bool MipSlice) {
  if (!IsInRange(Range.first_level, Range.level_count, Info.mip_levels))
    return false;
  UINT layers = Info.layers;
  if (MipSlice && Info.dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
    layers = GetLevelDepth(Info, Range.first_level);
  return IsInRange(Range.first_layer, Range.layer_count, layers);
}

bool
IsDefaultViewValid(const ViewResourceInfo &Info, bool AllowSrgbToggle) {
  return Info.dimension == D3D11_RESOURCE_DIMENSION_BUFFER ||
         IsValidViewFormat(Info, DXGI_FORMAT_UNKNOWN, AllowSrgbToggle);
}

} // namespace

HRESULT
ValidateViewDescription(ID3D11Resource *pResource, const D3D11_SHADER_RESOURCE_VIEW_DESC1 *pDesc) {
  ViewResourceInfo info;
  if (!GetViewResourceInfo(pResource, info) || !(info.bind_flags & D3D11_BIND_SHADER_RESOURCE))
    return E_INVALIDARG;
  if (!pDesc)
    return IsDefaultViewValid(info, false) ? S_OK : E_INVALIDARG;
  // The element range of buffer views is checked when they are created
  if (info.dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
    return (pDesc->ViewDimension == D3D11_SRV_DIMENSION_BUFFER ||
            pDesc->ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX) ? S_OK : E_INVALIDARG;
  if (!IsValidViewFormat(info, pDesc->Format, false))
    return E_INVALIDARG;

  ViewRange range;
  D3D11_RESOURCE_DIMENSION dimension;
  switch (pDesc->ViewDimension) {
  case D3D11_SRV_DIMENSION_TEXTURE1D: {
    auto &v = pDesc->Texture1D;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {v.MostDetailedMip, ResolveCount(v.MipLevels, v.MostDetailedMip, info.mip_levels), 0, 1};
    break;
  }
  case D3D11_SRV_DIMENSION_TEXTURE1DARRAY: {
    auto &v = pDesc->Texture1DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {v.MostDetailedMip, ResolveCount(v.MipLevels, v.MostDetailedMip, info.mip_levels), v.FirstArraySlice,
             ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_SRV_DIMENSION_TEXTURE2D: {
    auto &v = pDesc->Texture2D;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {v.MostDetailedMip, ResolveCount(v.MipLevels, v.MostDetailedMip, info.mip_levels), 0, 1};
    break;
  }
  case D3D11_SRV_DIMENSION_TEXTURE2DARRAY: {
    auto &v = pDesc->Texture2DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {v.MostDetailedMip, ResolveCount(v.MipLevels, v.MostDetailedMip, info.mip_levels), v.FirstArraySlice,
             ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_SRV_DIMENSION_TEXTURE2DMS:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {0, 1, 0, 1};
    break;
  case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY: {
    auto &v = pDesc->Texture2DMSArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {0, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_SRV_DIMENSION_TEXTURE3D: {
    auto &v = pDesc->Texture3D;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
    range = {v.MostDetailedMip, ResolveCount(v.MipLevels, v.MostDetailedMip, info.mip_levels), 0, 1};
    break;
  }
  case D3D11_SRV_DIMENSION_TEXTURECUBE: {
    auto &v = pDesc->TextureCube;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {v.MostDetailedMip, ResolveCount(v.MipLevels, v.MostDetailedMip, info.mip_levels), 0, 6};
    break;
  }
  case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY: {
    auto &v = pDesc->TextureCubeArray;
    UINT cubes = v.NumCubes;
    if (cubes == ~0u && v.First2DArrayFace < info.layers)
      cubes = (info.layers - v.First2DArrayFace) / 6;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {v.MostDetailedMip, ResolveCount(v.MipLevels, v.MostDetailedMip, info.mip_levels), v.First2DArrayFace,
             cubes > ~0u / 6 ? ~0u : cubes * 6};
    break;
  }
  default:
    return E_INVALIDARG;
  }

  if (dimension != info.dimension || !IsValidRange(info, range, false))
    return E_INVALIDARG;
  return S_OK;
}

HRESULT
ValidateViewDescription(ID3D11Resource *pResource, const D3D11_UNORDERED_ACCESS_VIEW_DESC1 *pDesc) {
  ViewResourceInfo info;
  if (!GetViewResourceInfo(pResource, info) || !(info.bind_flags & D3D11_BIND_UNORDERED_ACCESS))
    return E_INVALIDARG;
  if (!pDesc)
    return IsDefaultViewValid(info, false) ? S_OK : E_INVALIDARG;
  if (info.dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
    return pDesc->ViewDimension == D3D11_UAV_DIMENSION_BUFFER ? S_OK : E_INVALIDARG;
  if (!IsValidViewFormat(info, pDesc->Format, false))
    return E_INVALIDARG;

  ViewRange range;
  D3D11_RESOURCE_DIMENSION dimension;
  switch (pDesc->ViewDimension) {
  case D3D11_UAV_DIMENSION_TEXTURE1D:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {pDesc->Texture1D.MipSlice, 1, 0, 1};
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1DARRAY: {
    auto &v = pDesc->Texture1DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {v.MipSlice, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2D:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {pDesc->Texture2D.MipSlice, 1, 0, 1};
    break;
  case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: {
    auto &v = pDesc->Texture2DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {v.MipSlice, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE3D: {
    auto &v = pDesc->Texture3D;
    UINT depth = GetLevelDepth(info, v.MipSlice);
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
    range = {v.MipSlice, 1, v.FirstWSlice, ResolveCount(v.WSize, v.FirstWSlice, depth)};
    break;
  }
  default:
    return E_INVALIDARG;
  }

  if (dimension != info.dimension || !IsValidRange(info, range, true))
    return E_INVALIDARG;
  return S_OK;
}

HRESULT
ValidateViewDescription(ID3D11Resource *pResource, const D3D11_RENDER_TARGET_VIEW_DESC1 *pDesc) {
  ViewResourceInfo info;
  if (!GetViewResourceInfo(pResource, info) || !(info.bind_flags & D3D11_BIND_RENDER_TARGET))
    return E_INVALIDARG;
  if (!pDesc)
    return IsDefaultViewValid(info, true) ? S_OK : E_INVALIDARG;
  if (info.dimension == D3D11_RESOURCE_DIMENSION_BUFFER)
    return pDesc->ViewDimension == D3D11_RTV_DIMENSION_BUFFER ? S_OK : E_INVALIDARG;
  if (!IsValidViewFormat(info, pDesc->Format, true))
    return E_INVALIDARG;

  ViewRange range;
  D3D11_RESOURCE_DIMENSION dimension;
  switch (pDesc->ViewDimension) {
  case D3D11_RTV_DIMENSION_TEXTURE1D:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {pDesc->Texture1D.MipSlice, 1, 0, 1};
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY: {
    auto &v = pDesc->Texture1DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {v.MipSlice, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2D:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {pDesc->Texture2D.MipSlice, 1, 0, 1};
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: {
    auto &v = pDesc->Texture2DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {v.MipSlice, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMS:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {0, 1, 0, 1};
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY: {
    auto &v = pDesc->Texture2DMSArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {0, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE3D: {
    auto &v = pDesc->Texture3D;
    UINT depth = GetLevelDepth(info, v.MipSlice);
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
    range = {v.MipSlice, 1, v.FirstWSlice, ResolveCount(v.WSize, v.FirstWSlice, depth)};
    break;
  }
  default:
    return E_INVALIDARG;
  }

  if (dimension != info.dimension || !IsValidRange(info, range, true))
    return E_INVALIDARG;
  return S_OK;
}

HRESULT
ValidateViewDescription(ID3D11Resource *pResource, const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc) {
  ViewResourceInfo info;
  if (!GetViewResourceInfo(pResource, info) || !(info.bind_flags & D3D11_BIND_DEPTH_STENCIL))
    return E_INVALIDARG;
  if (info.dimension != D3D11_RESOURCE_DIMENSION_TEXTURE1D && info.dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    return E_INVALIDARG;
  if (!pDesc)
    return IsDefaultViewValid(info, false) ? S_OK : E_INVALIDARG;
  if (!IsValidViewFormat(info, pDesc->Format, false))
    return E_INVALIDARG;

  ViewRange range;
  D3D11_RESOURCE_DIMENSION dimension;
  switch (pDesc->ViewDimension) {
  case D3D11_DSV_DIMENSION_TEXTURE1D:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {pDesc->Texture1D.MipSlice, 1, 0, 1};
    break;
  case D3D11_DSV_DIMENSION_TEXTURE1DARRAY: {
    auto &v = pDesc->Texture1DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    range = {v.MipSlice, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2D:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {pDesc->Texture2D.MipSlice, 1, 0, 1};
    break;
  case D3D11_DSV_DIMENSION_TEXTURE2DARRAY: {
    auto &v = pDesc->Texture2DArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {v.MipSlice, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  case D3D11_DSV_DIMENSION_TEXTURE2DMS:
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {0, 1, 0, 1};
    break;
  case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY: {
    auto &v = pDesc->Texture2DMSArray;
    dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    range = {0, 1, v.FirstArraySlice, ResolveCount(v.ArraySize, v.FirstArraySlice, info.layers)};
    break;
  }
  default:
    return E_INVALIDARG;
  }

  if (dimension != info.dimension || !IsValidRange(info, range, true))
    return E_INVALIDARG;
  return S_OK;
}

template <>
void DowngradeViewDescription(const D3D11_SHADER_RESOURCE_VIEW_DESC1 &src,
                              D3D11_SHADER_RESOURCE_VIEW_DESC *pDst) {
  pDst->ViewDimension = src.ViewDimension;
  pDst->Format = src.Format;
  switch (src.ViewDimension) {
  case D3D_SRV_DIMENSION_UNKNOWN:
    break;
  case D3D_SRV_DIMENSION_BUFFER:
    pDst->Buffer = src.Buffer;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1D:
    pDst->Texture1D = src.Texture1D;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
    pDst->Texture1DArray = src.Texture1DArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2D: {
    pDst->Texture2D.MipLevels = src.Texture2D.MipLevels;
    pDst->Texture2D.MostDetailedMip = src.Texture2D.MostDetailedMip;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DARRAY: {
    pDst->Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
    pDst->Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
    pDst->Texture2DArray.MipLevels = src.Texture2DArray.MipLevels;
    pDst->Texture2DArray.MostDetailedMip = src.Texture2DArray.MostDetailedMip;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMS:
    pDst->Texture2DMS = src.Texture2DMS;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
    pDst->Texture2DMSArray = src.Texture2DMSArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE3D:
    pDst->Texture3D = src.Texture3D;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBE:
    pDst->TextureCube = src.TextureCube;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
    pDst->TextureCubeArray = src.TextureCubeArray;
    break;
  case D3D_SRV_DIMENSION_BUFFEREX:
    pDst->BufferEx = src.BufferEx;
    break;
  }
}

template <>
void DowngradeViewDescription(const D3D11_RENDER_TARGET_VIEW_DESC1 &src,
                              D3D11_RENDER_TARGET_VIEW_DESC *pDst) {
  pDst->ViewDimension = src.ViewDimension;
  pDst->Format = src.Format;
  switch (src.ViewDimension) {
  case D3D11_RTV_DIMENSION_UNKNOWN:
    break;
  case D3D11_RTV_DIMENSION_BUFFER:
    pDst->Buffer = src.Buffer;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1D:
    pDst->Texture1D = src.Texture1D;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
    pDst->Texture1DArray = src.Texture1DArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2D: {
    pDst->Texture2D.MipSlice = src.Texture2D.MipSlice;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: {
    pDst->Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
    pDst->Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
    pDst->Texture2DArray.MipSlice = src.Texture2DArray.MipSlice;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMS:
    pDst->Texture2DMS = src.Texture2DMS;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
    pDst->Texture2DMSArray = src.Texture2DMSArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE3D:
    pDst->Texture3D = src.Texture3D;
    break;
  }
}

template <>
void DowngradeViewDescription(const D3D11_UNORDERED_ACCESS_VIEW_DESC1 &src,
                              D3D11_UNORDERED_ACCESS_VIEW_DESC *pDst) {
  pDst->ViewDimension = src.ViewDimension;
  pDst->Format = src.Format;
  switch (src.ViewDimension) {
  case D3D11_UAV_DIMENSION_UNKNOWN:
    break;
  case D3D11_UAV_DIMENSION_BUFFER:
    pDst->Buffer = src.Buffer;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1D:
    pDst->Texture1D = src.Texture1D;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
    pDst->Texture1DArray = src.Texture1DArray;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE2D: {
    pDst->Texture2D.MipSlice = src.Texture2D.MipSlice;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: {
    pDst->Texture2DArray.MipSlice = src.Texture2DArray.MipSlice;
    pDst->Texture2DArray.FirstArraySlice = src.Texture2DArray.FirstArraySlice;
    pDst->Texture2DArray.ArraySize = src.Texture2DArray.ArraySize;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE3D:
    pDst->Texture3D = src.Texture3D;
    break;
  }
}

template <>
void UpgradeViewDescription(const D3D11_SHADER_RESOURCE_VIEW_DESC *pSrc,
                            D3D11_SHADER_RESOURCE_VIEW_DESC1 &dst) {
  dst.Format = pSrc->Format;
  dst.ViewDimension = pSrc->ViewDimension;
  switch (pSrc->ViewDimension) {
  case D3D_SRV_DIMENSION_UNKNOWN:
    break;
  case D3D_SRV_DIMENSION_BUFFER:
    dst.Buffer = pSrc->Buffer;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1D:
    dst.Texture1D = pSrc->Texture1D;
    break;
  case D3D_SRV_DIMENSION_TEXTURE1DARRAY:
    dst.Texture1DArray = pSrc->Texture1DArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2D: {
    dst.Texture2D.MipLevels = pSrc->Texture2D.MipLevels;
    dst.Texture2D.MostDetailedMip = pSrc->Texture2D.MostDetailedMip;
    dst.Texture2D.PlaneSlice = 0;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DARRAY: {
    dst.Texture2DArray.MipLevels = pSrc->Texture2DArray.MipLevels;
    dst.Texture2DArray.MostDetailedMip = pSrc->Texture2DArray.MostDetailedMip;
    dst.Texture2DArray.ArraySize = pSrc->Texture2DArray.ArraySize;
    dst.Texture2DArray.FirstArraySlice = pSrc->Texture2DArray.FirstArraySlice;
    dst.Texture2DArray.PlaneSlice = 0;
    break;
  }
  case D3D_SRV_DIMENSION_TEXTURE2DMS:
    dst.Texture2DMS = pSrc->Texture2DMS;
    break;
  case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY:
    dst.Texture2DMSArray = pSrc->Texture2DMSArray;
    break;
  case D3D_SRV_DIMENSION_TEXTURE3D:
    dst.Texture3D = pSrc->Texture3D;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBE:
    dst.TextureCube = pSrc->TextureCube;
    break;
  case D3D_SRV_DIMENSION_TEXTURECUBEARRAY:
    dst.TextureCubeArray = pSrc->TextureCubeArray;
    break;
  case D3D_SRV_DIMENSION_BUFFEREX:
    dst.BufferEx = pSrc->BufferEx;
    break;
  }
};

template <>
void UpgradeViewDescription(const D3D11_UNORDERED_ACCESS_VIEW_DESC *pSrc,
                            D3D11_UNORDERED_ACCESS_VIEW_DESC1 &dst) {
  dst.Format = pSrc->Format;
  dst.ViewDimension = pSrc->ViewDimension;
  switch (pSrc->ViewDimension) {
  case D3D11_UAV_DIMENSION_UNKNOWN:
    break;
  case D3D11_UAV_DIMENSION_BUFFER:
    dst.Buffer = pSrc->Buffer;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1D:
    dst.Texture1D = pSrc->Texture1D;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
    dst.Texture1DArray = pSrc->Texture1DArray;
    break;
  case D3D11_UAV_DIMENSION_TEXTURE2D: {
    dst.Texture2D.MipSlice = pSrc->Texture2D.MipSlice;
    dst.Texture2D.PlaneSlice = 0;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE2DARRAY: {
    dst.Texture2DArray.MipSlice = pSrc->Texture2DArray.MipSlice;
    dst.Texture2DArray.ArraySize = pSrc->Texture2DArray.ArraySize;
    dst.Texture2DArray.FirstArraySlice = pSrc->Texture2DArray.FirstArraySlice;
    dst.Texture2DArray.PlaneSlice = 0;
    break;
  }
  case D3D11_UAV_DIMENSION_TEXTURE3D:
    dst.Texture3D = pSrc->Texture3D;
    break;
  }
};

template <>
void UpgradeViewDescription(const D3D11_RENDER_TARGET_VIEW_DESC *pSrc,
                            D3D11_RENDER_TARGET_VIEW_DESC1 &dst) {
  dst.Format = pSrc->Format;
  dst.ViewDimension = pSrc->ViewDimension;
  switch (pSrc->ViewDimension) {
  case D3D11_RTV_DIMENSION_UNKNOWN:
    break;
  case D3D11_RTV_DIMENSION_BUFFER:
    dst.Buffer = pSrc->Buffer;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1D:
    dst.Texture1D = pSrc->Texture1D;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
    dst.Texture1DArray = pSrc->Texture1DArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2D: {
    dst.Texture2D.MipSlice = pSrc->Texture2D.MipSlice;
    dst.Texture2D.PlaneSlice = 0;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DARRAY: {
    dst.Texture2DArray.MipSlice = pSrc->Texture2DArray.MipSlice;
    dst.Texture2DArray.ArraySize = pSrc->Texture2DArray.ArraySize;
    dst.Texture2DArray.FirstArraySlice = pSrc->Texture2DArray.FirstArraySlice;
    dst.Texture2DArray.PlaneSlice = 0;
    break;
  }
  case D3D11_RTV_DIMENSION_TEXTURE2DMS:
    dst.Texture2DMS = pSrc->Texture2DMS;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
    dst.Texture2DMSArray = pSrc->Texture2DMSArray;
    break;
  case D3D11_RTV_DIMENSION_TEXTURE3D:
    dst.Texture3D = pSrc->Texture3D;
    break;
  }
};

} // namespace dxmt