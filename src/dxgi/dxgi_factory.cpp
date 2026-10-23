#include "com/com_pointer.hpp"
#include "config/config.hpp"
#include "dxgi_interfaces.h"
#include "dxgi_object.hpp"
#include "dxgi_vk_interfaces.h"
#include "com/com_guid.hpp"
#include "log/log.hpp"
#include "util_hotpatch.h"
#include "util_string.hpp"
#include "wsi_window.hpp"
#include "Metal.hpp"
#include "thread.hpp"
#include <mutex>
#include <vector>

namespace dxmt {

Com<IMTLDXGIAdapter> CreateAdapter(WMT::Device Device,
                                   IDXGIFactory2 *pFactory, Config &config);

HRESULT CreateDXGIVkSwapChain(
    IDXGIFactory1 *pFactory, IDXGIVkSwapChainFactory *pSwapChainFactory, IUnknown *pDevice, HWND hWnd,
    const DXGI_SWAP_CHAIN_DESC1 *pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
    IDXGISwapChain1 **ppSwapChain);

/* A fullscreen swap chain needs a valid scanline order and scaling, as in
   Wine's dxgi_validate_swapchain_fullscreen_desc. */
bool ValidateSwapChainFullscreenDesc(const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pDesc) {
  if (pDesc->Windowed)
    return true;

  if (UINT(pDesc->ScanlineOrdering) > DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST) {
    WARN("DXGI: Invalid scanline order ", UINT(pDesc->ScanlineOrdering));
    return false;
  }

  if (UINT(pDesc->Scaling) > DXGI_MODE_SCALING_STRETCHED) {
    WARN("DXGI: Invalid scaling ", UINT(pDesc->Scaling));
    return false;
  }

  return true;
}

/* Window association and Alt+Enter, after Wine's wined3d_register_window and
   wined3d_hook_proc. MakeWindowAssociation records flags per window and
   factory. The factory records each swap chain it creates with its window,
   and a hook on the window's thread (dxgi.altEnterHook) toggles the swap
   chain's fullscreen state on Alt+Enter, unless the window is associated
   with that swap chain's factory with DXGI_MWA_NO_WINDOW_CHANGES or
   DXGI_MWA_NO_ALT_ENTER. Nothing replaces the window procedure. */
namespace {

struct WindowAssociation {
  HWND window;
  const void *factory;
  UINT flags;
};

struct AssociatedSwapChain {
  HWND window;
  IDXGISwapChain *swapchain;
  const void *factory;
  DWORD thread_id;
};

struct MessageHook {
  DWORD thread_id;
  /* tells a thread that exited, whose ID a new thread may reuse */
  HANDLE thread;
  HHOOK hook;
  UINT count;
};

struct WindowAssociationTable {
  dxmt::mutex mutex;
  std::vector<WindowAssociation> associations;
  std::vector<AssociatedSwapChain> swapchains;
  std::vector<MessageHook> hooks;
};

/* Never destroyed: a swap chain released while DLLs detach at process exit
   still unregisters itself here. */
WindowAssociationTable &
GetWindowAssociationTable() {
  static WindowAssociationTable *table = new WindowAssociationTable;
  return *table;
}

void
ToggleFullscreenOnAltEnter(HWND hWnd) {
  auto &table = GetWindowAssociationTable();
  Com<IDXGISwapChain> swapchain;
  {
    std::lock_guard<dxmt::mutex> lock(table.mutex);
    for (auto &entry : table.swapchains) {
      if (entry.window != hWnd)
        continue;

      UINT flags = 0;
      for (auto &association : table.associations) {
        if (association.window == entry.window && association.factory == entry.factory)
          flags = association.flags;
      }
      if (flags & (DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER))
        continue;

      // A swap chain whose last reference is gone is being destroyed and
      // waits for the lock to unregister; the extra reference is then
      // dropped again without a second destruction
      if (entry.swapchain->AddRef() == 1) {
        entry.swapchain->Release();
        continue;
      }
      swapchain = Com<IDXGISwapChain>::transfer(entry.swapchain);
      break;
    }
  }

  // Without the lock: the fullscreen change sends messages to the window,
  // whose procedure may create or release swap chains, or wait for a thread
  // that registers one
  if (swapchain != nullptr) {
    BOOL fullscreen = FALSE;
    swapchain->GetFullscreenState(&fullscreen, nullptr);
    swapchain->SetFullscreenState(!fullscreen, nullptr);
  }
}

/* WH_GETMESSAGE sees every message the thread retrieves, posted ones too,
   as Wine's wined3d_hook_proc does, which costs server calls and a callback
   per message. */
LRESULT CALLBACK
AltEnterMessageHookProc(int code, WPARAM wparam, LPARAM lparam) {
  auto msg = reinterpret_cast<const MSG *>(lparam);

  // Only the message being removed from the queue, so a message that is
  // first peeked at and then retrieved toggles once
  if (code == HC_ACTION && wparam == PM_REMOVE && msg->message == WM_SYSKEYDOWN && msg->wParam == VK_RETURN &&
      (msg->lParam & (KF_ALTDOWN << 16)) && !(msg->lParam & (KF_REPEAT << 16)))
    ToggleFullscreenOnAltEnter(msg->hwnd);

  return CallNextHookEx(nullptr, code, wparam, lparam);
}

/* WH_KEYBOARD is called only for keyboard input the thread retrieves, so
   other messages cost nothing; posted key messages do not reach it. */
LRESULT CALLBACK
AltEnterKeyboardHookProc(int code, WPARAM wparam, LPARAM lparam) {
  WORD flags = HIWORD(lparam);

  // HC_ACTION: the message is being removed from the queue
  if (code == HC_ACTION && wparam == VK_RETURN && (flags & KF_ALTDOWN) && !(flags & (KF_UP | KF_REPEAT)))
    ToggleFullscreenOnAltEnter(GetFocus());

  return CallNextHookEx(nullptr, code, wparam, lparam);
}

enum class AltEnterHook { Keyboard, Message, Off };

/* dxgi.altEnterHook: keyboard (default), message or off */
AltEnterHook
GetAltEnterHook() {
  static const AltEnterHook hook = [] {
    auto value = Config::getInstance().getOption<std::string>("dxgi.altEnterHook", "keyboard");
    if (value == "message")
      return AltEnterHook::Message;
    if (value == "off")
      return AltEnterHook::Off;
    if (value != "keyboard")
      WARN("DXGI: Unknown dxgi.altEnterHook value ", value, ", using keyboard");
    return AltEnterHook::Keyboard;
  }();
  return hook;
}

void
RegisterSwapChainWindow(HWND hWnd, IDXGISwapChain *pSwapChain, const void *pFactory) {
  auto &table = GetWindowAssociationTable();
  std::lock_guard<dxmt::mutex> lock(table.mutex);

  DWORD thread_id = GetWindowThreadProcessId(hWnd, nullptr);
  table.swapchains.push_back({hWnd, pSwapChain, pFactory, thread_id});

  MessageHook *hook = nullptr;
  for (auto &entry : table.hooks) {
    if (entry.thread_id == thread_id) {
      hook = &entry;
      break;
    }
  }

  // A thread that exited took its hook with it, and its ID may now belong
  // to the window's thread
  if (hook && hook->thread && WaitForSingleObject(hook->thread, 0) == WAIT_OBJECT_0) {
    CloseHandle(hook->thread);
    hook->thread = OpenThread(SYNCHRONIZE, FALSE, thread_id);
    hook->hook = nullptr;
  }

  if (!hook) {
    table.hooks.push_back({thread_id, OpenThread(SYNCHRONIZE, FALSE, thread_id), nullptr, 0});
    hook = &table.hooks.back();
  }
  hook->count++;

  // Also retried for a hook that failed before
  AltEnterHook type = GetAltEnterHook();
  if (!hook->hook && type != AltEnterHook::Off) {
    hook->hook = type == AltEnterHook::Message
                     ? SetWindowsHookExW(WH_GETMESSAGE, AltEnterMessageHookProc, nullptr, thread_id)
                     : SetWindowsHookExW(WH_KEYBOARD, AltEnterKeyboardHookProc, nullptr, thread_id);
    if (!hook->hook)
      WARN("DXGIFactory: Failed to hook the messages of thread ", thread_id, " for Alt+Enter");
  }
}

void
UnregisterSwapChainWindow(IDXGISwapChain *pSwapChain) {
  auto &table = GetWindowAssociationTable();
  std::lock_guard<dxmt::mutex> lock(table.mutex);

  for (auto entry = table.swapchains.begin(); entry != table.swapchains.end(); ++entry) {
    if (entry->swapchain != pSwapChain)
      continue;

    for (auto hook = table.hooks.begin(); hook != table.hooks.end(); ++hook) {
      if (hook->thread_id == entry->thread_id) {
        if (!--hook->count) {
          if (hook->hook)
            UnhookWindowsHookEx(hook->hook);
          if (hook->thread)
            CloseHandle(hook->thread);
          table.hooks.erase(hook);
        }
        break;
      }
    }
    table.swapchains.erase(entry);
    return;
  }
}

void
AssociateWindow(HWND hWnd, const void *pFactory, UINT Flags) {
  auto &table = GetWindowAssociationTable();
  std::lock_guard<dxmt::mutex> lock(table.mutex);

  for (auto &association : table.associations) {
    if (association.window == hWnd && association.factory == pFactory) {
      association.flags = Flags;
      return;
    }
  }
  table.associations.push_back({hWnd, pFactory, Flags});
}

void
UnassociateWindows(const void *pFactory) {
  auto &table = GetWindowAssociationTable();
  std::lock_guard<dxmt::mutex> lock(table.mutex);

  std::erase_if(table.associations, [pFactory](const WindowAssociation &association) {
    return association.factory == pFactory;
  });
}

} // namespace

class MTLDXGIFactory : public MTLDXGIObject<IDXGIFactory6> {

  /* aggregated, like MTLDXGIResource */
  class FactoryPrivate : public IMTLDXGIFactoryPrivate {
  public:
    FactoryPrivate(MTLDXGIFactory *pFactory) : factory_(pFactory) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppvObject) final {
      return factory_->QueryInterface(riid, ppvObject);
    }
    ULONG STDMETHODCALLTYPE AddRef() final { return factory_->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() final { return factory_->Release(); }

    void STDMETHODCALLTYPE UnregisterSwapChain(IDXGISwapChain *pSwapChain) final {
      UnregisterSwapChainWindow(pSwapChain);
    }

  private:
    MTLDXGIFactory *factory_;
  };

public:
  MTLDXGIFactory(UINT Flags) : flags_(Flags), private_(this) {};

  ~MTLDXGIFactory() { UnassociateWindows(this); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                           void **ppvObject) final {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) ||
        riid == __uuidof(IDXGIFactory) || riid == __uuidof(IDXGIFactory1) ||
        riid == __uuidof(IDXGIFactory2) || riid == __uuidof(IDXGIFactory2) ||
        riid == __uuidof(IDXGIFactory3) || riid == __uuidof(IDXGIFactory4) ||
        riid == __uuidof(IDXGIFactory5) || riid == __uuidof(IDXGIFactory6)) {
      *ppvObject = ref(this);
      return S_OK;
    }

    if (riid == __uuidof(IMTLDXGIFactoryPrivate)) {
      *ppvObject = ref(&private_);
      return S_OK;
    }

    if (logQueryInterfaceError(__uuidof(IDXGIFactory2), riid)) {
      WARN("DXGIFactory: Unknown interface query ", str::format(riid));
    }

    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void **ppParent) final {
    InitReturnPtr(ppParent);

    WARN("DXGIFactory::GetParent: Unknown interface query ", str::format(riid));
    return E_NOINTERFACE;
  }

  BOOL STDMETHODCALLTYPE IsWindowedStereoEnabled() final {
    // We don't support Stereo 3D at the moment
    return FALSE;
  }

  HRESULT STDMETHODCALLTYPE
  CreateSoftwareAdapter(HMODULE Module, IDXGIAdapter **ppAdapter) final {
    InitReturnPtr(ppAdapter);

    if (ppAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    ERR("Software adapters not supported");
    return DXGI_ERROR_UNSUPPORTED;
  }

  DXMT_HOTPATCHABLE
  HRESULT STDMETHODCALLTYPE
  CreateSwapChain(IUnknown *pDevice, DXGI_SWAP_CHAIN_DESC *pDesc,
                  IDXGISwapChain **ppSwapChain) final {
    if (ppSwapChain == nullptr || pDesc == nullptr || pDevice == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    DXGI_SWAP_CHAIN_DESC1 desc;
    desc.Width = pDesc->BufferDesc.Width;
    desc.Height = pDesc->BufferDesc.Height;
    desc.Format = pDesc->BufferDesc.Format;
    desc.Stereo = FALSE;
    desc.SampleDesc = pDesc->SampleDesc;
    desc.BufferUsage = pDesc->BufferUsage;
    desc.BufferCount = pDesc->BufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = pDesc->SwapEffect;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = pDesc->Flags;

    DXGI_SWAP_CHAIN_FULLSCREEN_DESC descFs;
    descFs.RefreshRate = pDesc->BufferDesc.RefreshRate;
    descFs.ScanlineOrdering = pDesc->BufferDesc.ScanlineOrdering;
    descFs.Scaling = pDesc->BufferDesc.Scaling;
    descFs.Windowed = pDesc->Windowed;

    IDXGISwapChain1 *swapChain = nullptr;
    HRESULT hr = CreateSwapChainForHwnd(pDevice, pDesc->OutputWindow, &desc,
                                        &descFs, nullptr, &swapChain);

    *ppSwapChain = swapChain;
    return hr;
  }

  DXMT_HOTPATCHABLE
  HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(
      IUnknown *pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *pFullscreenDesc,
      IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) final {
    InitReturnPtr(ppSwapChain);

    if (!ppSwapChain || !pDesc || !hWnd || !pDevice)
      return DXGI_ERROR_INVALID_CALL;

    // Devices that are not DXMT's own, such as vkd3d-proton's D3D12 command
    // queues, create a presenter that DXGI wraps in a swap chain
    Com<IMTLSwapChainFactory> swapchain_factory;
    Com<IDXGIVkSwapChainFactory> vk_swapchain_factory;
    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&swapchain_factory))) &&
        FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&vk_swapchain_factory)))) {
      ERR("Unsupported device type");
      return DXGI_ERROR_UNSUPPORTED;
    }

    // Make sure the back buffer size is not zero
    DXGI_SWAP_CHAIN_DESC1 desc = *pDesc;

    wsi::getWindowSize(hWnd, desc.Width ? nullptr : &desc.Width,
                       desc.Height ? nullptr : &desc.Height);

    // If necessary, set up a default set of
    // fullscreen parameters for the swap chain
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc;

    if (pFullscreenDesc) {
      fsDesc = *pFullscreenDesc;
      // An invalid scanline order or scaling creates a windowed swap chain,
      // as on Windows
      if (!ValidateSwapChainFullscreenDesc(&fsDesc))
        fsDesc.Windowed = TRUE;
    } else {
      fsDesc.RefreshRate = {0, 0};
      fsDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
      fsDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
      fsDesc.Windowed = TRUE;
    }

    HRESULT hr;
    if (vk_swapchain_factory != nullptr)
      hr = CreateDXGIVkSwapChain(this, vk_swapchain_factory.ptr(), pDevice,
                                 hWnd, &desc, &fsDesc, ppSwapChain);
    else
      hr = swapchain_factory->CreateSwapChain(this, hWnd, &desc, &fsDesc,
                                              ppSwapChain);

    if (SUCCEEDED(hr) && *ppSwapChain)
      RegisterSwapChainWindow(hWnd, *ppSwapChain, this);
    return hr;
  }

  DXMT_HOTPATCHABLE
  HRESULT STDMETHODCALLTYPE CreateSwapChainForCoreWindow(
      IUnknown *pDevice, IUnknown *pWindow, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) final {
    InitReturnPtr(ppSwapChain);

    ERR("Not implemented");
    return E_NOTIMPL;
  }

  DXMT_HOTPATCHABLE
  HRESULT STDMETHODCALLTYPE CreateSwapChainForComposition(
      IUnknown *pDevice, const DXGI_SWAP_CHAIN_DESC1 *pDesc,
      IDXGIOutput *pRestrictToOutput, IDXGISwapChain1 **ppSwapChain) final {
    InitReturnPtr(ppSwapChain);

    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE EnumAdapters(UINT Adapter,
                                         IDXGIAdapter **ppAdapter) final {
    InitReturnPtr(ppAdapter);

    if (ppAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    IDXGIAdapter1 *handle = nullptr;
    HRESULT hr = this->EnumAdapters1(Adapter, &handle);
    *ppAdapter = handle;
    return hr;
  }

  HRESULT STDMETHODCALLTYPE EnumAdapters1(UINT Adapter,
                                          IDXGIAdapter1 **ppAdapter) final {
    InitReturnPtr(ppAdapter);

    auto devices = WMT::CopyAllDevices();
    UINT adapter_count = devices.count();

    if (Adapter >= adapter_count)
      return DXGI_ERROR_NOT_FOUND;

    UINT adjusted_adapter = Adapter;
    if (adapter_count > 1) {
      UINT preferred_adapter = 0;
      for (unsigned i = 0; i < adapter_count; i++) {
        if (!devices.object(i).hasUnifiedMemory())
          preferred_adapter = i;
      }
      if (Adapter == 0)
        adjusted_adapter = preferred_adapter;
      else
        adjusted_adapter = Adapter <= preferred_adapter ? Adapter - 1 : Adapter;
    }

    auto device = devices.object(adjusted_adapter);

    *ppAdapter = CreateAdapter(device, this, Config::getInstance());
    // devices->release(); // no you should not release it...
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetWindowAssociation(HWND *pWindowHandle) final {
    if (pWindowHandle == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    // Windows returns no window even after MakeWindowAssociation, as Wine's
    // dxgi tests show
    *pWindowHandle = nullptr;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetSharedResourceAdapterLuid(HANDLE hResource,
                                                         LUID *pLuid) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle,
                                                  UINT Flags) final {
    if (Flags > DXGI_MWA_VALID)
      return DXGI_ERROR_INVALID_CALL;

    // No window removes all associations of this factory
    if (!WindowHandle)
      UnassociateWindows(this);
    else
      AssociateWindow(WindowHandle, this, Flags);
    return S_OK;
  }

  BOOL STDMETHODCALLTYPE IsCurrent() final { return TRUE; }

  HRESULT STDMETHODCALLTYPE RegisterOcclusionStatusWindow(
      HWND WindowHandle, UINT wMsg, DWORD *pdwCookie) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE RegisterStereoStatusEvent(HANDLE hEvent,
                                                      DWORD *pdwCookie) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE RegisterStereoStatusWindow(HWND WindowHandle,
                                                       UINT wMsg,
                                                       DWORD *pdwCookie) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE
  RegisterOcclusionStatusEvent(HANDLE hEvent, DWORD *pdwCookie) final {
    ERR("Not implemented");
    return E_NOTIMPL;
  }

  void STDMETHODCALLTYPE UnregisterStereoStatus(DWORD dwCookie) final {
    ERR("Not implemented");
  }

  void STDMETHODCALLTYPE UnregisterOcclusionStatus(DWORD dwCookie) final {
    ERR("Not implemented");
  }

  UINT STDMETHODCALLTYPE GetCreationFlags() override { return flags_; }

  HRESULT STDMETHODCALLTYPE EnumAdapterByLuid(LUID luid, REFIID riid,
                                              void **ppAdapter) override {
    InitReturnPtr(ppAdapter);

    if (ppAdapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    // Adapters report the LUIDs Wine gives its GPUs, the ones D3DKMT, DXCore
    // and Vulkan report
    Com<IDXGIAdapter1> adapter;
    for (UINT i = 0; SUCCEEDED(this->EnumAdapters1(i, &adapter)); i++) {
      DXGI_ADAPTER_DESC desc;
      adapter->GetDesc(&desc);
      if (desc.AdapterLuid.HighPart == luid.HighPart && desc.AdapterLuid.LowPart == luid.LowPart)
        return adapter->QueryInterface(riid, ppAdapter);
      adapter = nullptr;
    }

    WARN("EnumAdapterByLuid: no adapter with LUID ", luid.HighPart, ":", luid.LowPart);
    return DXGI_ERROR_NOT_FOUND;
  }

  HRESULT STDMETHODCALLTYPE EnumWarpAdapter(REFIID iid,
                                            void **adapter) override {
    InitReturnPtr(adapter);

    if (adapter == nullptr)
      return DXGI_ERROR_INVALID_CALL;

    // There is no software rasterizer. Callers expect an adapter on success,
    // so hand out the hardware one, as DXVK and Wine's DXGI do.
    WARN("DXGIFactory::EnumWarpAdapter: WARP not supported, returning the hardware adapter");

    Com<IDXGIAdapter1> hardware_adapter;
    HRESULT hr = this->EnumAdapters1(0, &hardware_adapter);
    if (FAILED(hr))
      return hr;

    return hardware_adapter->QueryInterface(iid, adapter);
  };

  HRESULT STDMETHODCALLTYPE
  CheckFeatureSupport(DXGI_FEATURE Feature, void *pFeatureSupportData,
                      UINT FeatureSupportDataSize) override {
    switch (Feature) {
    case DXGI_FEATURE_PRESENT_ALLOW_TEARING: {
      auto info = static_cast<BOOL *>(pFeatureSupportData);

      if (FeatureSupportDataSize != sizeof(*info))
        return E_INVALIDARG;

      *info = TRUE;
      return S_OK;
    }
    default: {
      ERR("DXGIFactory::CheckFeatureSupport: unknown feature ", Feature);
      return E_INVALIDARG;
    }
    }
  };

  HRESULT STDMETHODCALLTYPE
  EnumAdapterByGpuPreference(UINT Adapter, DXGI_GPU_PREFERENCE GpuPreference,
                             REFIID riid, void **ppvAdapter) override {
    // GpuPreference ignored, since Apple Silicon has only 1 GPU anyway
    // FIXME: support Intel Mac with dedicated GPU
    Com<IDXGIAdapter1> adapter;
    HRESULT hr = this->EnumAdapters1(Adapter, &adapter);

    if (FAILED(hr))
      return hr;
    return adapter->QueryInterface(riid, ppvAdapter);
  };

private:
  UINT flags_;
  FactoryPrivate private_;
};

extern "C" HRESULT __stdcall CreateDXGIFactory2(UINT Flags, REFIID riid,
                                                void **ppFactory) {
  try {
    MTLDXGIFactory* factory = new MTLDXGIFactory(Flags);
    HRESULT hr = factory->QueryInterface(riid, ppFactory);
    factory->Release();

    if (FAILED(hr))
      return hr;

    return S_OK;
  } catch (const MTLD3DError &e) {
    Logger::err(e.message());
    return E_FAIL;
  }
}

extern "C" HRESULT __stdcall CreateDXGIFactory1(REFIID riid, void **ppFactory) {
  return CreateDXGIFactory2(0, riid, ppFactory);
}

extern "C" HRESULT __stdcall CreateDXGIFactory(REFIID riid, void **factory) {
  return CreateDXGIFactory2(0, riid, factory);
}

} // namespace dxmt