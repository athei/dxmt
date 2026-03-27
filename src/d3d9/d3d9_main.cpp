
#include "d3d9_interface.hpp"
#include "log/log.hpp"

namespace dxmt {
Logger Logger::s_instance("d3d9.log");

extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion) {
  Logger::info("Direct3DCreate9 called");
  return new D3D9Interface();
}

} // namespace dxmt

#ifndef DXMT_NATIVE

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
  if (reason != DLL_PROCESS_ATTACH)
    return TRUE;

  DisableThreadLibraryCalls(instance);
  return TRUE;
}

#endif
