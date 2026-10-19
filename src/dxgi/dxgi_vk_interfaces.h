/*
 * This file is part of DXMT
 *
 * The interfaces below are those of vkd3d-proton's
 * include/vkd3d_swapchain_factory.idl (LGPL-2.1+),
 * Copyright 2020 Joshua Ashton for Valve Software
 *
 * See <https://github.com/HansKristian-Work/vkd3d-proton>
 */

#pragma once

#include <dxgi1_6.h>
#include <cstdint>
#include "com/com_guid.hpp"

/**
 * Just the parts of the Vulkan API used to create a surface for a window, so
 * that DXGI does not depend on Vulkan headers. Vulkan calls are WINAPI
 * (stdcall) on 32-bit Windows.
 */
typedef struct VkInstance_T *VkInstance;
typedef struct VkPhysicalDevice_T *VkPhysicalDevice;
/* a non-dispatchable handle: 64-bit wide on every architecture */
typedef uint64_t VkSurfaceKHR;
typedef int32_t VkResult;

constexpr VkResult VK_SUCCESS = 0;
constexpr VkResult VK_ERROR_INITIALIZATION_FAILED = -3;
constexpr VkResult VK_ERROR_EXTENSION_NOT_PRESENT = -7;

constexpr int32_t VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR = 1000009000;

struct VkWin32SurfaceCreateInfoKHR {
  int32_t sType;
  const void *pNext;
  uint32_t flags;
  HINSTANCE hinstance;
  HWND hwnd;
};

typedef void(WINAPI *PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction(WINAPI *PFN_vkGetInstanceProcAddr)(VkInstance instance, const char *pName);
typedef VkResult(WINAPI *PFN_vkCreateWin32SurfaceKHR)(
    VkInstance instance, const VkWin32SurfaceCreateInfoKHR *pCreateInfo, const void *pAllocator,
    VkSurfaceKHR *pSurface
);

struct DXGI_VK_HDR_METADATA {
  DXGI_HDR_METADATA_TYPE Type;
  union {
    DXGI_HDR_METADATA_HDR10 HDR10;
  };
};

struct DXGI_VK_FRAME_STATISTICS {
  UINT64 PresentCount;
  UINT64 PresentQPCTime;
};

DEFINE_COM_INTERFACE("1e7895a1-1bc3-4f9c-a670-290a4bc9581a", IDXGIVkSurfaceFactory)
    : public IUnknown {
  virtual VkResult STDMETHODCALLTYPE CreateSurface(
      VkInstance Instance, VkPhysicalDevice Adapter, VkSurfaceKHR *pSurface) = 0;
};

/**
 * The presenter of a D3D12 command queue (or any other device that is not
 * DXMT's own). It owns the back buffers and the Vulkan swapchain; everything
 * about windows, outputs and display modes is left to DXGI.
 */
DEFINE_COM_INTERFACE("e4a9059e-b569-46ab-8de7-501bd2bc7f7a", IDXGIVkSwapChain)
    : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC1 * pDesc) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetAdapter(REFIID riid, void **ppvObject) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void **ppDevice) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetImage(UINT BufferId, REFIID riid, void **ppBuffer) = 0;
  virtual UINT STDMETHODCALLTYPE GetImageIndex() = 0;
  virtual UINT STDMETHODCALLTYPE GetFrameLatency() = 0;
  virtual HANDLE STDMETHODCALLTYPE GetFrameLatencyEvent() = 0;
  virtual HRESULT STDMETHODCALLTYPE ChangeProperties(
      const DXGI_SWAP_CHAIN_DESC1 *pDesc, const UINT *pNodeMasks, IUnknown *const *ppPresentQueues) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPresentRegion(const RECT *pRegion) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetGammaControl(UINT NumControlPoints, const DXGI_RGB *pControlPoints) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetFrameLatency(UINT MaxLatency) = 0;
  virtual HRESULT STDMETHODCALLTYPE Present(
      UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS *pPresentParameters) = 0;
  virtual UINT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetColorSpace(DXGI_COLOR_SPACE_TYPE ColorSpace) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetHDRMetaData(const DXGI_VK_HDR_METADATA *pMetaData) = 0;
};

DEFINE_COM_INTERFACE("785326d4-b77b-4826-ae70-8d08308ee6d1", IDXGIVkSwapChain1)
    : public IDXGIVkSwapChain {
  virtual void STDMETHODCALLTYPE GetLastPresentCount(UINT64 * pLastPresentCount) = 0;
  virtual void STDMETHODCALLTYPE GetFrameStatistics(DXGI_VK_FRAME_STATISTICS * pFrameStatistics) = 0;
};

DEFINE_COM_INTERFACE("aed91093-e02e-458c-bdef-a97da1a7e6d2", IDXGIVkSwapChain2)
    : public IDXGIVkSwapChain1 {
  virtual void STDMETHODCALLTYPE SetTargetFrameRate(double FrameRate) = 0;
};

DEFINE_COM_INTERFACE("e7d6c3ca-23a0-4e08-9f2f-ea5231df6633", IDXGIVkSwapChainFactory)
    : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(
      IDXGIVkSurfaceFactory * pSurfaceFactory, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      IDXGIVkSwapChain **ppSwapChain) = 0;
};
