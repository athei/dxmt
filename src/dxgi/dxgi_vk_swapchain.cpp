/*
 * This file is part of DXMT, Copyright (c) 2023 Feifan He
 *
 * Derived from a part of DXVK (originally under zlib License),
 * Copyright (c) 2017 Philip Rebohle
 * Copyright (c) 2019 Joshua Ashton
 *
 * See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>
 */

/*
 * ID3D12CommandQueue::GetDesc returns a struct, which the MSVC ABI passes
 * through a hidden pointer after `this`; mingw's headers only declare it that
 * way with this defined.
 */
#define WIDL_EXPLICIT_AGGREGATE_RETURNS

#include "com/com_guid.hpp"
#include "com/com_pointer.hpp"
#include "config/config.hpp"
#include "dxgi_object.hpp"
#include "dxgi_options.hpp"
#include "dxgi_vk_interfaces.h"
#include "log/log.hpp"
#include "util_hotpatch.h"
#include "util_string.hpp"
#include "wsi_monitor.hpp"
#include "wsi_window.hpp"
#include <d3d12.h>
#include <algorithm>
#include <atomic>
#include <utility>

namespace dxmt {

uint32_t GetMonitorFormatBpp(DXGI_FORMAT Format);

bool ValidateSwapChainFullscreenDesc(const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc);

static bool
IsFlipSwapEffect(DXGI_SWAP_EFFECT SwapEffect) {
  return SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

/* The back buffer formats of flip model swap chains: not the _SRGB ones */
static bool
IsFlipFormat(DXGI_FORMAT Format) {
  switch (Format) {
  case DXGI_FORMAT_R16G16B16A16_FLOAT:
  case DXGI_FORMAT_R10G10B10A2_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
    return true;
  default:
    return false;
  }
}

static PFN_vkGetInstanceProcAddr
LoadVulkanLoader() {
  // The surface must be created through the loader vkd3d-proton's Vulkan
  // instance came from, so this order must match the one d3d12core picks its
  // loader in (load_modules_once() in libs/d3d12core/main.c).
  static PFN_vkGetInstanceProcAddr loader = []() -> PFN_vkGetInstanceProcAddr {
    for (const char *name : {"winevulkan.dll", "vulkan-1.dll"}) {
      HMODULE module = LoadLibraryA(name);
      if (!module)
        continue;
      auto get_instance_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
          reinterpret_cast<void *>(GetProcAddress(module, "vkGetInstanceProcAddr"))
      );
      if (get_instance_proc_addr)
        return get_instance_proc_addr;
      FreeLibrary(module);
    }
    return nullptr;
  }();
  return loader;
}

class MTLDXGIVkSurfaceFactory final : public ComObjectWithInitialRef<IDXGIVkSurfaceFactory> {
public:
  MTLDXGIVkSurfaceFactory(PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr, HWND hWnd) :
      get_instance_proc_addr_(pfnGetInstanceProcAddr),
      hWnd(hWnd) {}

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIVkSurfaceFactory)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    return E_NOINTERFACE;
  }

  VkResult
  STDMETHODCALLTYPE
  CreateSurface(VkInstance Instance, VkPhysicalDevice Adapter, VkSurfaceKHR *pSurface) final {
    auto create_surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        get_instance_proc_addr_(Instance, "vkCreateWin32SurfaceKHR")
    );
    if (!create_surface)
      return VK_ERROR_EXTENSION_NOT_PRESENT;

    VkWin32SurfaceCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    info.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hWnd, GWLP_HINSTANCE));
    info.hwnd = hWnd;
    return create_surface(Instance, &info, nullptr, pSurface);
  }

private:
  PFN_vkGetInstanceProcAddr get_instance_proc_addr_;
  HWND hWnd;
};

class ModeSetGuard {
  std::atomic_flag in_progress_;
public:
  class ModeSetInProgress {
    ModeSetGuard &guard_;
    bool protected_;

  public:
    ModeSetInProgress(ModeSetGuard &guard) : guard_(guard) {
      protected_ = guard_.in_progress_.test_and_set();
    }
    ~ModeSetInProgress() {
      if (!protected_) {
        guard_.in_progress_.clear();
      }
    }
    operator bool() {
      return protected_;
    }
  };
};

/**
 * Swap chains of devices that are not DXMT's own, namely vkd3d-proton's D3D12
 * command queues. Such a device creates the presenter (IDXGIVkSwapChain),
 * which owns the back buffers and a Vulkan swapchain on the window; this
 * class adds what DXGI has on top of that: windows, outputs, fullscreen and
 * display modes.
 */
class MTLDXGIVkSwapChain final : public MTLDXGIObject<IDXGISwapChain4> {
public:
  MTLDXGIVkSwapChain(
      IDXGIFactory1 *pFactory, IDXGIVkSwapChain *pPresenter, IDXGIAdapter *pAdapter, bool IsD3D12, HWND hWnd,
      const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc
  ) :
      factory_(pFactory),
      presenter_(pPresenter),
      adapter_(pAdapter),
      is_d3d12_(IsD3D12),
      desc_(*pDesc),
      fullscreen_desc_(*pFullscreenDesc),
      hWnd(hWnd),
      monitor_(wsi::getWindowMonitor(hWnd)) {
    // Both may be missing from older vkd3d-proton builds
    presenter_->QueryInterface(IID_PPV_ARGS(&presenter1_));
    presenter_->QueryInterface(IID_PPV_ARGS(&presenter2_));

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    creation_qpc_time_ = now.QuadPart;

    handle_alt_tab_ = Config::getInstance().getOption<bool>("dxgi.handleAltTab", false);

    // dxgi.vsync: "game" (default) passes the game's sync interval to the presenter,
    // "on" passes at least 1 and "off" passes 0.
    auto vsync = Config::getInstance().getOption<std::string>("dxgi.vsync", "game");
    vsync_policy_ = vsync == "on" ? VsyncPolicy::On : vsync == "off" ? VsyncPolicy::Off : VsyncPolicy::Game;

    force_sdr_ = DxgiOptions(Config::getInstance()).forceSDR;

    UpdateRefreshRate(monitor_);

    if (!fullscreen_desc_.Windowed && FAILED(EnterFullscreenMode(nullptr))) {
      WARN("DXGI: EnterFullscreenMode failed, creating a windowed swap chain instead");
      fullscreen_desc_.Windowed = TRUE;
    }

    // RGBA16 swap chains are scRGB, if the presenter supports it
    UpdateColorSpace(desc_.Format, colorspace_);
  };

  ~MTLDXGIVkSwapChain() {
    // A swap chain released in fullscreen mode restores the display mode and
    // the window, as wined3d does
    if (!fullscreen_desc_.Windowed) {
      RestoreDisplayMode(monitor_);
      if (wsi::isWindow(hWnd))
        RestoreWindow();
    }
  };

  struct RestoreWindowData {
    HWND hWnd;
    wsi::DXMTWindowState state;
    HMODULE module;
  };

  static DWORD WINAPI RestoreWindowThread(void *pData) {
    auto data = static_cast<RestoreWindowData *>(pData);
    HMODULE module = data->module;
    wsi::leaveFullscreenMode(data->hWnd, &data->state, true);
    delete data;
    if (module)
      FreeLibraryAndExitThread(module, 0);
    return 0;
  }

  /* Restoring the window of another thread waits for that thread, which may
     be waiting for this one, so as in wined3d's set_window_state() it is
     done on a new thread then, which keeps this module loaded until it is
     done. */
  void RestoreWindow() {
    if (GetWindowThreadProcessId(hWnd, nullptr) == GetCurrentThreadId()) {
      wsi::leaveFullscreenMode(hWnd, &window_state_, true);
      return;
    }

    auto data = new RestoreWindowData{hWnd, window_state_, nullptr};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(&RestoreWindowThread), &data->module))
      data->module = nullptr;

    HANDLE thread = CreateThread(nullptr, 0, RestoreWindowThread, data, 0, nullptr);
    if (!thread) {
      ERR("DXGI: Failed to create a thread to restore the window");
      if (data->module)
        FreeLibrary(data->module);
      delete data;
      return;
    }
    CloseHandle(thread);
  }

  HRESULT
  STDMETHODCALLTYPE
  QueryInterface(REFIID riid, void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) ||
        riid == __uuidof(IDXGISwapChain2) ||
        riid == __uuidof(IDXGISwapChain3) ||
        riid == __uuidof(IDXGISwapChain4)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IDXGISwapChain1), riid)) {
      WARN("DXGIVkSwapChain: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetParent(REFIID riid, void **parent) final {
    return factory_->QueryInterface(riid, parent);
  };

  HRESULT
  STDMETHODCALLTYPE
  GetDevice(REFIID riid, void **device) final {
    return presenter_->GetDevice(riid, device);
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  Present(UINT SyncInterval, UINT PresentFlags) final {
    return Present1(SyncInterval, PresentFlags, nullptr);
  };

  HRESULT
  STDMETHODCALLTYPE
  GetBuffer(UINT Buffer, REFIID riid, void **ppSurface) final {
    return presenter_->GetImage(Buffer, riid, ppSurface);
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  SetFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) final {
    if (!Fullscreen && pTarget)
      return DXGI_ERROR_INVALID_CALL;

    // An invalid scanline order or scaling, which made the swap chain
    // windowed at creation, keeps it from entering fullscreen mode
    if (Fullscreen) {
      DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc;
      {
        std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
        fullscreen_desc = fullscreen_desc_;
      }
      fullscreen_desc.Windowed = FALSE;
      if (!ValidateSwapChainFullscreenDesc(&fullscreen_desc))
        return DXGI_ERROR_INVALID_CALL;
    }

    // The window procedure can call this while a fullscreen change waits for
    // the window's thread; as in Wine, such a nested call, or one made while
    // another thread changes the mode, returns at once
    ModeSetGuard::ModeSetInProgress modeset_inprogress(modeset_guard_);
    if (modeset_inprogress) {
      WARN("DXGI: SetFullscreenState during a mode change");
      return IsWindowed() == !Fullscreen ? S_OK : DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;
    }

    return ChangeFullscreenState(Fullscreen, pTarget);
  };

  HRESULT ChangeFullscreenState(BOOL Fullscreen, IDXGIOutput *pTarget) {
    Com<IDXGIOutput1> target;

    if (pTarget) {
      DXGI_OUTPUT_DESC desc;

      pTarget->QueryInterface(IID_PPV_ARGS(&target));
      target->GetDesc(&desc);

      bool other_monitor;
      {
        std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
        other_monitor = !fullscreen_desc_.Windowed && monitor_ != desc.Monitor;
      }

      if (Fullscreen && other_monitor) {
        HRESULT hr = LeaveFullscreenMode();
        if (FAILED(hr))
          return hr;
      }
    }

    bool windowed = IsWindowed();
    if (windowed && Fullscreen)
      return EnterFullscreenMode(target.ptr());
    else if (!windowed && !Fullscreen)
      return LeaveFullscreenMode();

    return S_OK;
  }

  /* Called with the mode set guard held, or from the constructor. The window
     lock is not held across window or display mode changes: they wait for
     the window's thread, whose window procedure may call into the swap
     chain. */
  HRESULT EnterFullscreenMode(IDXGIOutput1* pTarget) {
    Com<IDXGIOutput1> output = pTarget;

    if (!wsi::isWindow(hWnd))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    if (output == nullptr) {
      if (FAILED(GetOutputFromMonitor(wsi::getWindowMonitor(hWnd), &output))) {
        ERR("DXGI: EnterFullscreenMode: Cannot query containing output");
        return E_FAIL;
      }
    }

    DXGI_RATIONAL refresh_rate;
    {
      std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
      refresh_rate = fullscreen_desc_.RefreshRate;
    }

    DXGI_MODE_DESC1 preferred_display_mode = {
        desc_.Width,
        desc_.Height,
        refresh_rate,
        desc_.Format,
        DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
        DXGI_MODE_SCALING_UNSPECIFIED
    };
    if (FAILED(ChangeDisplayMode(output.ptr(), &preferred_display_mode))) {
      ERR("DXGI: EnterFullscreenMode: Failed to change display mode");
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    // Move the window so that it covers the entire output
    bool modeSwitch = (desc_.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) != 0u;

    DXGI_OUTPUT_DESC desc;
    output->GetDesc(&desc);

    // Update swap chain description, together with the refresh rate, so
    // that a present in between does not reset the frame limiter
    {
      std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
      fullscreen_desc_.Windowed = FALSE;
      monitor_ = desc.Monitor;
      SetModeRefreshRate(preferred_display_mode);
    }
    {
      std::lock_guard<dxmt::mutex> target_lock(target_mutex_);
      target_ = std::move(output);
    }

    wsi::DXMTWindowState window_state;
    if (!wsi::enterFullscreenMode(desc.Monitor, hWnd, &window_state, modeSwitch)) {
      ERR("DXGI: EnterFullscreenMode: Failed to enter fullscreen mode");
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    {
      std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
      window_state_ = window_state;
    }

    return S_OK;
  }

  /* Called with the mode set guard held, see EnterFullscreenMode. */
  HRESULT LeaveFullscreenMode() {
    HMONITOR monitor;
    {
      std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
      monitor = monitor_;
    }

    if (FAILED(RestoreDisplayMode(monitor)))
      WARN("DXGI: LeaveFullscreenMode: Failed to restore display mode");

    // Restore internal state
    wsi::DXMTWindowState window_state;
    {
      std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
      fullscreen_desc_.Windowed = TRUE;
      window_state = window_state_;
      monitor_ = wsi::getWindowMonitor(hWnd);
      UpdateRefreshRate(monitor_);
    }
    Com<IDXGIOutput1> target;
    {
      std::lock_guard<dxmt::mutex> target_lock(target_mutex_);
      target = std::move(target_);
    }

    if (!wsi::isWindow(hWnd))
      return S_OK;

    if (!wsi::leaveFullscreenMode(hWnd, &window_state, true)) {
      ERR("DXGI: LeaveFullscreenMode: Failed to exit fullscreen mode");
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    return S_OK;
  }

  bool IsWindowed() {
    std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
    return fullscreen_desc_.Windowed;
  }

  HRESULT
  ChangeDisplayMode(IDXGIOutput1 *pOutput, DXGI_MODE_DESC1 *pDisplayMode) {
    if (!pOutput)
      return DXGI_ERROR_INVALID_CALL;

    // Find a mode that the output supports

    DXGI_MODE_DESC1 preferred_mode = *pDisplayMode;
    DXGI_MODE_DESC1 selected_mode = {};

    if (!(desc_.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)) {
      preferred_mode.Width = 0;
      preferred_mode.Height = 0;
    }

    if (preferred_mode.Format == DXGI_FORMAT_UNKNOWN)
      preferred_mode.Format = desc_.Format;

    HRESULT hr = pOutput->FindClosestMatchingMode1(&preferred_mode, &selected_mode, nullptr);

    if (FAILED(hr)) {
      ERR("DXGI: Failed to query closest mode:"
          "\n"
          "  Format: ",
          preferred_mode.Format,
          "\n"
          "  Mode:   ",
          preferred_mode.Width, "x", preferred_mode.Height, "@",
          preferred_mode.RefreshRate.Numerator / std::max(preferred_mode.RefreshRate.Denominator, 1u));
      return hr;
    }

    if (!selected_mode.RefreshRate.Denominator)
      selected_mode.RefreshRate.Denominator = 1;

    DXGI_OUTPUT_DESC output_desc;
    pOutput->GetDesc(&output_desc);
    wsi::WsiMode wsi_mode{
        selected_mode.Width,
        selected_mode.Height,
        {selected_mode.RefreshRate.Numerator, selected_mode.RefreshRate.Denominator},
        GetMonitorFormatBpp(selected_mode.Format),
        selected_mode.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST ||
            selected_mode.ScanlineOrdering == DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST
    };
    if (!wsi::setWindowMode(output_desc.Monitor, hWnd, wsi_mode))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    *pDisplayMode = selected_mode;
    return S_OK;
  }

  /* Takes the refresh rate of a display mode ChangeDisplayMode set, under
     the window lock. */
  void SetModeRefreshRate(const DXGI_MODE_DESC1 &Mode) {
    frame_rate_refresh_ = double(Mode.RefreshRate.Numerator) / double(Mode.RefreshRate.Denominator);
    refresh_rate_ = frame_rate_refresh_;
  }

  HRESULT
  RestoreDisplayMode(HMONITOR hMonitor) {
    if (!hMonitor)
      return DXGI_ERROR_INVALID_CALL;

    if (!wsi::restoreDisplayMode(hMonitor))
      return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
    frame_rate_refresh_ = 0.0;
    UpdateRefreshRate(hMonitor);
    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetFullscreenState(BOOL *pFullscreen, IDXGIOutput **ppTarget) final {
    HRESULT hr = S_OK;

    if (handle_alt_tab_ && !IsWindowed() && !wsi::isForeground(hWnd))
      SetFullscreenState(FALSE, nullptr);

    if (pFullscreen != nullptr)
      *pFullscreen = !IsWindowed();

    if (ppTarget != nullptr) {
      std::lock_guard<dxmt::mutex> target_lock(target_mutex_);
      *ppTarget = target_.ref();
    }

    return hr;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetDesc(DXGI_SWAP_CHAIN_DESC *pDesc) final {
    if (!pDesc)
      return E_INVALIDARG;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc;
    GetFullscreenDesc(&fullscreen_desc);

    pDesc->BufferDesc.Width = desc_.Width;
    pDesc->BufferDesc.Height = desc_.Height;
    pDesc->BufferDesc.RefreshRate = fullscreen_desc.RefreshRate;
    pDesc->BufferDesc.Format = desc_.Format;
    pDesc->BufferDesc.ScanlineOrdering = fullscreen_desc.ScanlineOrdering;
    pDesc->BufferDesc.Scaling = fullscreen_desc.Scaling;
    pDesc->SampleDesc = desc_.SampleDesc;
    pDesc->BufferUsage = desc_.BufferUsage;
    pDesc->BufferCount = desc_.BufferCount;
    pDesc->OutputWindow = hWnd;
    pDesc->Windowed = fullscreen_desc.Windowed;
    pDesc->SwapEffect = desc_.SwapEffect;
    pDesc->Flags = desc_.Flags;
    return S_OK;
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT SwapChainFlags) final {
    return ResizeBuffersBase(BufferCount, Width, Height, Format, SwapChainFlags, nullptr, nullptr);
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  ResizeTarget(const DXGI_MODE_DESC *pDesc) final {
    if (!pDesc)
      return DXGI_ERROR_INVALID_CALL;

    if (!wsi::isWindow(hWnd))
      return DXGI_ERROR_INVALID_CALL;

    // Promote display mode
    DXGI_MODE_DESC1 newDisplayMode = {};
    newDisplayMode.Width = pDesc->Width;
    newDisplayMode.Height = pDesc->Height;
    newDisplayMode.RefreshRate = pDesc->RefreshRate;
    newDisplayMode.Format = pDesc->Format;
    newDisplayMode.ScanlineOrdering = pDesc->ScanlineOrdering;
    newDisplayMode.Scaling = pDesc->Scaling;

    // Serialized with fullscreen changes, which the window procedure may
    // call this from while they wait for the window's thread
    ModeSetGuard::ModeSetInProgress modeset_inprogress(modeset_guard_);
    if (modeset_inprogress) {
      WARN("DXGI: ResizeTarget during a mode change");
      return DXGI_STATUS_MODE_CHANGE_IN_PROGRESS;
    }

    bool windowed;
    HMONITOR monitor;
    {
      std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);

      // Update the swap chain description. An invalid scanline order or
      // scaling is not kept, so that it does not keep the swap chain from
      // entering fullscreen mode later.
      if (newDisplayMode.RefreshRate.Numerator != 0)
        fullscreen_desc_.RefreshRate = newDisplayMode.RefreshRate;

      DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc = fullscreen_desc_;
      fullscreen_desc.ScanlineOrdering = newDisplayMode.ScanlineOrdering;
      fullscreen_desc.Scaling = newDisplayMode.Scaling;
      fullscreen_desc.Windowed = FALSE;
      if (ValidateSwapChainFullscreenDesc(&fullscreen_desc)) {
        fullscreen_desc_.ScanlineOrdering = newDisplayMode.ScanlineOrdering;
        fullscreen_desc_.Scaling = newDisplayMode.Scaling;
      }

      windowed = fullscreen_desc_.Windowed;
      monitor = monitor_;

      // A window that the application moved or resized in fullscreen mode
      // returns there when leaving fullscreen mode, as in wined3d
      if (!windowed) {
        RECT desktop_rect, window_rect;
        wsi::getDesktopCoordinates(monitor, &desktop_rect);
        ::GetWindowRect(hWnd, &window_rect);
        if (window_rect.right - window_rect.left != desktop_rect.right - desktop_rect.left ||
            window_rect.bottom - window_rect.top != desktop_rect.bottom - desktop_rect.top)
          window_state_.rect = window_rect;
      }
    }

    // Window and display mode changes are made without the window lock, see
    // EnterFullscreenMode
    if (windowed) {
      wsi::resizeWindow(hWnd, nullptr, newDisplayMode.Width,
                        newDisplayMode.Height);
    } else {
      Com<IDXGIOutput1> target;
      {
        std::lock_guard<dxmt::mutex> target_lock(target_mutex_);
        target = target_;
      }
      if (SUCCEEDED(ChangeDisplayMode(target.ptr(), &newDisplayMode))) {
        std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
        SetModeRefreshRate(newDisplayMode);
      }
      wsi::updateFullscreenWindow(monitor, hWnd, false);
    }

    return S_OK;
  };

  HRESULT GetOutputFromMonitor(
          HMONITOR                  Monitor,
          IDXGIOutput1**            ppOutput) {
    if (!ppOutput)
      return DXGI_ERROR_INVALID_CALL;

    Com<IDXGIOutput> output;

    for (uint32_t i = 0; SUCCEEDED(adapter_->EnumOutputs(i, &output)); i++) {
      DXGI_OUTPUT_DESC outputDesc;
      output->GetDesc(&outputDesc);

      if (outputDesc.Monitor == Monitor)
        return output->QueryInterface(IID_PPV_ARGS(ppOutput));

      output = nullptr;
    }

    return DXGI_ERROR_NOT_FOUND;
  }

  HRESULT
  STDMETHODCALLTYPE
  GetContainingOutput(IDXGIOutput **ppOutput) final {
    InitReturnPtr(ppOutput);

    if (!wsi::isWindow(hWnd))
      return DXGI_ERROR_INVALID_CALL;

    Com<IDXGIOutput1> output;
    {
      std::lock_guard<dxmt::mutex> target_lock(target_mutex_);
      output = target_;
    }

    if (output == nullptr) {
      HRESULT hr = GetOutputFromMonitor(wsi::getWindowMonitor(hWnd), &output);

      if (FAILED(hr))
        return hr;
    }

    *ppOutput = output.ref();
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetFrameStatistics(DXGI_FRAME_STATISTICS *pStats) final {
    std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);

    if (!pStats)
      return E_INVALIDARG;

    // A windowed swap chain follows its window to other monitors
    if (fullscreen_desc_.Windowed && wsi::isWindow(hWnd)) {
      HMONITOR monitor = wsi::getWindowMonitor(hWnd);
      if (monitor != monitor_) {
        monitor_ = monitor;
        UpdateRefreshRate(monitor_);
      }
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    DXGI_VK_FRAME_STATISTICS statistics = {present_count_, UINT64(now.QuadPart)};
    if (presenter1_ != nullptr)
      presenter1_->GetFrameStatistics(&statistics);

    // There is no vblank counter to sample, so the last present is put on the
    // refresh of the current display mode it falls into, counted from the
    // swap chain's creation.
    UINT refresh_count = CountRefreshes(statistics.PresentQPCTime);
    pStats->PresentCount = statistics.PresentCount;
    pStats->PresentRefreshCount = refresh_count;
    pStats->SyncRefreshCount = refresh_count;
    pStats->SyncQPCTime.QuadPart = statistics.PresentQPCTime;
    pStats->SyncGPUTime.QuadPart = 0;

    // DISJOINT is returned on the first call and around mode changes; only
    // the former is tracked.
    if (std::exchange(frame_statistics_disjoint_, false))
      return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetLastPresentCount(UINT *pLastPresentCount) final {
    if (pLastPresentCount == nullptr)
      return E_INVALIDARG;

    UINT64 present_count = present_count_;
    if (presenter1_ != nullptr)
      presenter1_->GetLastPresentCount(&present_count);

    *pLastPresentCount = UINT(present_count);
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetDesc1(DXGI_SWAP_CHAIN_DESC1 *pDesc) final {
    if (pDesc == NULL) {
      return E_INVALIDARG;
    }
    *pDesc = desc_;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) final {
    if (pDesc == NULL) {
      return E_INVALIDARG;
    }
    std::lock_guard<dxmt::recursive_mutex> lock(window_mutex_);
    *pDesc = fullscreen_desc_;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetHwnd(HWND *pHwnd) final {
    if (pHwnd == NULL) {
      return E_POINTER;
    }
    *pHwnd = hWnd;
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetCoreWindow(REFIID refiid, void **ppUnk) final {
    InitReturnPtr(ppUnk);
    ERR("Not implemented");
    return E_NOTIMPL;
  };

  DXMT_HOTPATCHABLE
  HRESULT
  STDMETHODCALLTYPE
  Present1(UINT SyncInterval, UINT PresentFlags,
           const DXGI_PRESENT_PARAMETERS *pPresentParameters) final {
    std::unique_lock<dxmt::recursive_mutex> lock(window_mutex_);

    if (SyncInterval > 4)
      return DXGI_ERROR_INVALID_CALL;

    // Tearing needs the swap chain flag, sync interval 0 and windowed mode
    if ((PresentFlags & DXGI_PRESENT_ALLOW_TEARING) &&
        (!(desc_.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) || SyncInterval != 0 || !fullscreen_desc_.Windowed))
      return DXGI_ERROR_INVALID_CALL;

    bool should_exit_fs = handle_alt_tab_ // At the moment this is still broken for certain games
                          && !fullscreen_desc_.Windowed && !wsi::isMinimized(hWnd) && !wsi::isForeground(hWnd);

    switch (vsync_policy_) {
    case VsyncPolicy::On:
      SyncInterval = std::max(SyncInterval, 1u);
      break;
    case VsyncPolicy::Off:
      SyncInterval = 0;
      break;
    case VsyncPolicy::Game:
      break;
    }

    if (!(PresentFlags & DXGI_PRESENT_TEST))
      UpdateTargetFrameRate(SyncInterval);

    HRESULT hr = S_OK;

    if (wsi::isWindow(hWnd)) {
      std::lock_guard<dxmt::mutex> buffer_lock(buffer_mutex_);
      hr = presenter_->Present(SyncInterval, PresentFlags, pPresentParameters);
    }

    if (PresentFlags & DXGI_PRESENT_TEST)
      return hr == S_OK && should_exit_fs ? DXGI_STATUS_OCCLUDED : hr;

    if (hr == S_OK) {
      present_count_ += 1;

      if (should_exit_fs) {
        // SetFullscreenState makes window calls, which must not happen under
        // the window lock
        lock.unlock();
        SetFullscreenState(FALSE, nullptr);
        hr = DXGI_STATUS_OCCLUDED;
      }
    }

    return hr;
  };

  BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() final { return FALSE; };

  HRESULT
  STDMETHODCALLTYPE
  GetRestrictToOutput(IDXGIOutput **ppRestrictToOutput) final {
    InitReturnPtr(ppRestrictToOutput);
    ERR("DXGISwapChain1::GetRestrictToOutput: not implemented");
    return E_NOTIMPL;
  };

  HRESULT
  STDMETHODCALLTYPE
  SetBackgroundColor(const DXGI_RGBA *pColor) final {
    ERR("DXGISwapChain1::SetBackgroundColor: not implemented");
    return E_NOTIMPL;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetBackgroundColor(DXGI_RGBA *pColor) final {
    if (!pColor)
      return E_INVALIDARG;
    *pColor = {0, 0, 0, 0};
    return S_OK;
  };

  HRESULT
  STDMETHODCALLTYPE
  SetRotation(DXGI_MODE_ROTATION Rotation) final {
    ERR("DXGISwapChain1::SetRotation: not implemented");
    return E_NOTIMPL;
  };

  HRESULT
  STDMETHODCALLTYPE
  GetRotation(DXGI_MODE_ROTATION *pRotation) final {
    if (!pRotation)
      return E_INVALIDARG;
    *pRotation = DXGI_MODE_ROTATION_IDENTITY;
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE
  SetSourceSize(UINT Width, UINT Height) final {
    ERR("DXGISwapChain2::SetSourceSize: not implemented");
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  GetSourceSize(UINT *pWidth, UINT *pHeight) final {
    if (pWidth)
      *pWidth = desc_.Width;
    if (pHeight)
      *pHeight = desc_.Height;
    return S_OK;
  };

  HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) final {
    if (!(desc_.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
      return DXGI_ERROR_INVALID_CALL;

    return presenter_->SetFrameLatency(MaxLatency);
  };

  HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT *pMaxLatency) final {
    if (!(desc_.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
      return DXGI_ERROR_INVALID_CALL;

    if (!pMaxLatency)
      return E_INVALIDARG;

    *pMaxLatency = presenter_->GetFrameLatency();
    return S_OK;
  };

  HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() final {
    if (!(desc_.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT))
      return nullptr;

    // already a duplicate, owned by the caller
    return presenter_->GetFrameLatencyEvent();
  };

  HRESULT STDMETHODCALLTYPE
  SetMatrixTransform(const DXGI_MATRIX_3X2_F *matrix) final {
    ERR("DXGISwapChain2::SetMatrixTransform: not implemented");
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE
  GetMatrixTransform(DXGI_MATRIX_3X2_F *matrix) final {
    ERR("DXGISwapChain2::GetMatrixTransform: not implemented");
    return E_NOTIMPL;
  };

  HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(
      DXGI_COLOR_SPACE_TYPE ColorSpace, UINT *pColorSpaceSupport) final {
    if (!pColorSpaceSupport)
      return E_INVALIDARG;

    std::lock_guard<dxmt::mutex> lock(buffer_mutex_);

    *pColorSpaceSupport = ValidateColorSpaceSupport(desc_.Format, ColorSpace)
            ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT
            : 0;
    return S_OK;
  };

  UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() final {
    return presenter_->GetImageIndex();
  }

  DXMT_HOTPATCHABLE
  HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                         DXGI_FORMAT Format, UINT SwapChainFlags,
                         const UINT *pCreationNodeMask,
                         IUnknown *const *ppPresentQueue) final {
    if (BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
      return DXGI_ERROR_INVALID_CALL;

    // D3D12 swap chains take a node mask and a present queue for each buffer,
    // and there is only one node
    if (is_d3d12_) {
      if (!pCreationNodeMask || !ppPresentQueue)
        return DXGI_ERROR_INVALID_CALL;

      UINT buffer_count = BufferCount;
      if (!buffer_count) {
        std::lock_guard<dxmt::mutex> lock(buffer_mutex_);
        buffer_count = desc_.BufferCount;
      }
      for (UINT i = 0; i < buffer_count; i++) {
        if (pCreationNodeMask[i] > 1)
          return DXGI_ERROR_INVALID_CALL;
      }
    }

    return ResizeBuffersBase(BufferCount, Width, Height, Format, SwapChainFlags,
                             pCreationNodeMask, ppPresentQueue);
  }

  HRESULT ResizeBuffersBase(UINT BufferCount, UINT Width, UINT Height,
                            DXGI_FORMAT Format, UINT SwapChainFlags,
                            const UINT *pCreationNodeMask,
                            IUnknown *const *ppPresentQueue) {
    if (!wsi::isWindow(hWnd))
      return DXGI_ERROR_INVALID_CALL;

    if (BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
      return DXGI_ERROR_INVALID_CALL;

    std::lock_guard<dxmt::mutex> lock(buffer_mutex_);

    if (IsFlipSwapEffect(desc_.SwapEffect)) {
      if (BufferCount == 1)
        return DXGI_ERROR_INVALID_CALL;
      if (Format != DXGI_FORMAT_UNKNOWN && !IsFlipFormat(Format))
        return DXGI_ERROR_INVALID_CALL;
    }

    // Neither the waitable object nor tearing can be turned on or off after
    // creation
    constexpr UINT PreserveFlags =
        DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    if ((desc_.Flags ^ SwapChainFlags) & PreserveFlags)
      return E_INVALIDARG;

    DXGI_SWAP_CHAIN_DESC1 desc = desc_;
    desc.Width = Width;
    desc.Height = Height;
    desc.Flags = SwapChainFlags;

    wsi::getWindowSize(hWnd, desc.Width ? nullptr : &desc.Width,
                       desc.Height ? nullptr : &desc.Height);

    // A window without a client area gets 8x8 buffers, as on Windows
    if (!desc.Width)
      desc.Width = 8;
    if (!desc.Height)
      desc.Height = 8;

    if (BufferCount != 0)
      desc.BufferCount = BufferCount;

    if (Format != DXGI_FORMAT_UNKNOWN)
      desc.Format = Format;

    HRESULT hr = presenter_->ChangeProperties(&desc, pCreationNodeMask, ppPresentQueue);
    if (FAILED(hr))
      return hr;

    desc_ = desc;
    UpdateColorSpace(desc_.Format, colorspace_);
    return hr;
  }

  HRESULT STDMETHODCALLTYPE
  SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) final {
    std::lock_guard<dxmt::mutex> lock(buffer_mutex_);

    if (!ValidateColorSpaceSupport(desc_.Format, ColorSpace))
      return E_INVALIDARG;

    // Keep the color space even if a later back buffer format does not
    // support it, so that it applies again when one does
    HRESULT hr = UpdateColorSpace(desc_.Format, ColorSpace);
    if (SUCCEEDED(hr))
      colorspace_ = ColorSpace;
    return hr;
  }

  HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type,
                                           UINT Size,
                                           void *pMetaData) final {
    if (Size && !pMetaData)
      return E_INVALIDARG;

    DXGI_VK_HDR_METADATA metadata = {};
    metadata.Type = Type;

    switch (Type) {
    case DXGI_HDR_METADATA_TYPE_NONE:
      break;
    case DXGI_HDR_METADATA_TYPE_HDR10:
      if (Size != sizeof(DXGI_HDR_METADATA_HDR10))
        return E_INVALIDARG;
      metadata.HDR10 = *static_cast<const DXGI_HDR_METADATA_HDR10 *>(pMetaData);
      break;
    default:
      ERR("DXGI: Unsupported HDR metadata type: ", Type);
      return E_INVALIDARG;
    }

    std::lock_guard<dxmt::mutex> lock(buffer_mutex_);
    return presenter_->SetHDRMetaData(&metadata);
  }

private:
  bool ValidateColorSpaceSupport(DXGI_FORMAT Format, DXGI_COLOR_SPACE_TYPE ColorSpace) {
    // RGBA16 swap chains are treated as scRGB even on SDR displays,
    // and regular sRGB is not exposed when this format is used.
    if (Format == DXGI_FORMAT_R16G16B16A16_FLOAT)
      return ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;

    // For everything else, plain sRGB is always exposed
    if (ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)
      return true;

    if (ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020)
      return !force_sdr_ && presenter_->CheckColorSpaceSupport(ColorSpace);

    return false;
  }

  HRESULT UpdateColorSpace(DXGI_FORMAT Format, DXGI_COLOR_SPACE_TYPE ColorSpace) {
    if (!ValidateColorSpaceSupport(Format, ColorSpace)) {
      ColorSpace = Format == DXGI_FORMAT_R16G16B16A16_FLOAT
          ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709
          : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }

    // scRGB falls back to sRGB on SDR setups, as on Windows
    if (!presenter_->CheckColorSpaceSupport(ColorSpace))
      ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    return presenter_->SetColorSpace(ColorSpace);
  }

  void UpdateTargetFrameRate(UINT SyncInterval) {
    if (presenter2_ == nullptr)
      return;

    // A FIFO present only waits for one refresh, so sync intervals above 1
    // engage the presenter's frame limiter in windowed mode. In fullscreen
    // mode the refresh rate is that of the display mode set.
    if (SyncInterval != frame_rate_sync_interval_ && fullscreen_desc_.Windowed) {
      frame_rate_sync_interval_ = SyncInterval;
      frame_rate_refresh_ = 0.0;

      wsi::WsiMode mode;
      if (SyncInterval > 1 && wsi::isWindow(hWnd) &&
          wsi::getCurrentDisplayMode(wsi::getWindowMonitor(hWnd), &mode) &&
          mode.refreshRate.numerator && mode.refreshRate.denominator) {
        frame_rate_refresh_ = double(mode.refreshRate.numerator) / double(mode.refreshRate.denominator);
      }
    } else if (!fullscreen_desc_.Windowed) {
      frame_rate_sync_interval_ = 0;
    }

    // A negative rate only engages the limiter if it is actually exceeded
    double frame_rate = SyncInterval ? -frame_rate_refresh_ / double(SyncInterval) : 0.0;

    if (frame_rate != frame_rate_limit_) {
      frame_rate_limit_ = frame_rate;
      presenter2_->SetTargetFrameRate(frame_rate);
    }
  }

  /* The refresh rate frame statistics count with, cached because querying the
     display mode is slow. A windowed swap chain does not pick up refresh rate
     changes made outside the game, only moves to another monitor. */
  void UpdateRefreshRate(HMONITOR hMonitor) {
    refresh_rate_ = 60.0;

    wsi::WsiMode mode;
    if (wsi::getCurrentDisplayMode(hMonitor, &mode) && mode.refreshRate.numerator && mode.refreshRate.denominator)
      refresh_rate_ = double(mode.refreshRate.numerator) / double(mode.refreshRate.denominator);
  }

  UINT CountRefreshes(UINT64 qpc_time) {
    if (qpc_time <= creation_qpc_time_)
      return 0;

    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return UINT(double(qpc_time - creation_qpc_time_) * refresh_rate_ / double(frequency.QuadPart));
  }

  Com<IDXGIFactory1> factory_;
  Com<IDXGIVkSwapChain> presenter_;
  Com<IDXGIVkSwapChain1> presenter1_;
  Com<IDXGIVkSwapChain2> presenter2_;
  Com<IDXGIAdapter> adapter_;
  bool is_d3d12_;
  DXGI_SWAP_CHAIN_DESC1 desc_;
  DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc_;
  HWND hWnd;
  HMONITOR monitor_;
  Com<IDXGIOutput1> target_;
  wsi::DXMTWindowState window_state_;
  DXGI_COLOR_SPACE_TYPE colorspace_ = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
  bool force_sdr_ = false;
  UINT64 present_count_ = 0;
  UINT64 creation_qpc_time_ = 0;
  bool frame_statistics_disjoint_ = true;
  UINT frame_rate_sync_interval_ = 0;
  double frame_rate_refresh_ = 0.0;
  double frame_rate_limit_ = 0.0;
  double refresh_rate_ = 60.0;
  /* Held while SetFullscreenState or ResizeTarget changes the mode, for
     nested calls from the window procedure and concurrent ones. */
  ModeSetGuard modeset_guard_;
  /* Guards the window and display mode state (fullscreen_desc_, monitor_,
     window_state_, refresh and frame rates) and serializes presents with it,
     as DXVK does. Never held across window or display mode changes, which
     wait for the window's thread. */
  dxmt::recursive_mutex window_mutex_;
  /* Guards target_; never held across wsi calls. */
  dxmt::mutex target_mutex_;
  /* Serializes calls that change the presenter's buffers. */
  dxmt::mutex buffer_mutex_;

  bool handle_alt_tab_;

  enum class VsyncPolicy { Game, On, Off };
  VsyncPolicy vsync_policy_ = VsyncPolicy::Game;
};

HRESULT
CreateDXGIVkSwapChain(
    IDXGIFactory1 *pFactory, IDXGIVkSwapChainFactory *pSwapChainFactory, IUnknown *pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
    IDXGISwapChain1 **ppSwapChain
) {
  InitReturnPtr(ppSwapChain);

  // D3D12 swap chains are validated the way Windows does it
  Com<ID3D12CommandQueue> queue;
  if (SUCCEEDED(pDevice->QueryInterface(IID_PPV_ARGS(&queue)))) {
    if (pDesc->Format == DXGI_FORMAT_UNKNOWN)
      return DXGI_ERROR_INVALID_CALL;

    if (queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
      WARN("CreateSwapChain: not a direct command queue");
      return DXGI_ERROR_INVALID_CALL;
    }

    if (hWnd == GetDesktopWindow()) {
      WARN("CreateSwapChain: D3D12 swap chains cannot present to the desktop window");
      return E_ACCESSDENIED;
    }

    if (!IsFlipSwapEffect(pDesc->SwapEffect)) {
      WARN("CreateSwapChain: D3D12 swap chains need a flip swap effect, not ", pDesc->SwapEffect);
      return DXGI_ERROR_INVALID_CALL;
    }
  }

  if (IsFlipSwapEffect(pDesc->SwapEffect)) {
    if (pDesc->BufferCount < 2 || pDesc->BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS ||
        pDesc->SampleDesc.Count != 1 || pDesc->SampleDesc.Quality != 0) {
      WARN("CreateSwapChain: invalid buffer count ", pDesc->BufferCount, " or sample count ",
           pDesc->SampleDesc.Count, " for a flip swap effect");
      return DXGI_ERROR_INVALID_CALL;
    }

    if (pDesc->Format != DXGI_FORMAT_UNKNOWN && !IsFlipFormat(pDesc->Format)) {
      WARN("CreateSwapChain: invalid format ", pDesc->Format, " for a flip swap effect");
      return DXGI_ERROR_INVALID_CALL;
    }
  }

  // A window without a client area gets 8x8 buffers, as on Windows
  DXGI_SWAP_CHAIN_DESC1 desc = *pDesc;
  if (!desc.Width)
    desc.Width = 8;
  if (!desc.Height)
    desc.Height = 8;

  auto get_instance_proc_addr = LoadVulkanLoader();
  if (!get_instance_proc_addr) {
    ERR("CreateSwapChain: failed to load winevulkan.dll or vulkan-1.dll");
    return DXGI_ERROR_UNSUPPORTED;
  }

  auto surface_factory =
      Com<IDXGIVkSurfaceFactory>::transfer(new MTLDXGIVkSurfaceFactory(get_instance_proc_addr, hWnd));

  Com<IDXGIVkSwapChain> presenter;
  HRESULT hr = pSwapChainFactory->CreateSwapChain(surface_factory.ptr(), &desc, &presenter);
  if (FAILED(hr)) {
    ERR("CreateSwapChain: failed to create presenter, hr ", hr);
    return hr;
  }

  // The adapter the device was created on, for its outputs
  Com<IDXGIAdapter> adapter;
  if (FAILED(presenter->GetAdapter(IID_PPV_ARGS(&adapter)))) {
    ERR("CreateSwapChain: failed to get the presenter's adapter");
    return E_FAIL;
  }

  *ppSwapChain = new MTLDXGIVkSwapChain(
      pFactory, presenter.ptr(), adapter.ptr(), queue != nullptr, hWnd, &desc, pFullscreenDesc
  );
  return S_OK;
}

} // namespace dxmt
