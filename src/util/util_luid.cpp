#include "util_luid.hpp"
#include "util_d3dkmt.h"
#include "log/log.hpp"
#include "thread.hpp"
#include <bit>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace dxmt {

static bool
GetWineGpuLuid(const WCHAR *pName, uint32_t DeviceCount, LUID *pLuid) {
  D3DKMT_ENUMADAPTERS2 enum_adapters = {};
  if (D3DKMTEnumAdapters2(&enum_adapters) || !enum_adapters.NumAdapters)
    return false;

  std::vector<D3DKMT_ADAPTERINFO> adapters(enum_adapters.NumAdapters);
  enum_adapters.pAdapters = adapters.data();
  if (D3DKMTEnumAdapters2(&enum_adapters))
    return false;

  for (ULONG i = 0; i < enum_adapters.NumAdapters; i++) {
    D3DKMT_CLOSEADAPTER close = {};
    close.hAdapter = adapters[i].hAdapter;
    D3DKMTCloseAdapter(&close);
  }

  // One GPU and one Metal device, as in every Apple silicon Mac
  if (enum_adapters.NumAdapters == 1 && DeviceCount == 1) {
    *pLuid = adapters[0].AdapterLuid;
    return true;
  }

#ifdef _WIN32
  // Otherwise the GPU of the Metal device's name, found through a display it
  // drives: Wine names a display adapter after its GPU
  DISPLAY_DEVICEW display = {};
  display.cb = sizeof(display);
  for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &display, 0); i++) {
    if (wcscmp(display.DeviceString, pName))
      continue;

    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME open = {};
    std::memcpy(open.DeviceName, display.DeviceName, sizeof(open.DeviceName));
    if (D3DKMTOpenAdapterFromGdiDisplayName(&open))
      continue;

    D3DKMT_CLOSEADAPTER close = {};
    close.hAdapter = open.hAdapter;
    D3DKMTCloseAdapter(&close);

    *pLuid = open.AdapterLuid;
    return true;
  }
#endif

  return false;
}

LUID
GetMetalDeviceLuid(uint64_t RegistryId, const WCHAR *pName, uint32_t DeviceCount) {
  static dxmt::mutex mutex;
  static std::unordered_map<uint64_t, LUID> luids;

  std::lock_guard<dxmt::mutex> lock(mutex);

  auto entry = luids.find(RegistryId);
  if (entry != luids.end())
    return entry->second;

  LUID luid;
  if (!GetWineGpuLuid(pName, DeviceCount, &luid)) {
    Logger::warn(str::format("No Wine GPU found for Metal device ", RegistryId, ", using its registry ID as LUID"));
    // NOTE: use big-endian registryID, be consistent with MVK
    luid = std::bit_cast<LUID>(__builtin_bswap64(RegistryId));
  }

  luids.emplace(RegistryId, luid);
  return luid;
}

} // namespace dxmt
