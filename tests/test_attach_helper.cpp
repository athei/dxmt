// Helper DLL for test_native_thread: sets a flag on DLL_THREAD_ATTACH
// so the test can verify whether the callback was invoked.

#include <windows.h>

typedef volatile LONG *(*get_attach_flag_t)();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  if (fdwReason == DLL_THREAD_ATTACH) {
    // Find the flag exported by the test exe
    HMODULE exe = GetModuleHandleA(NULL);
    if (exe) {
      auto get_flag =
          (get_attach_flag_t)GetProcAddress(exe, "get_attach_flag");
      if (get_flag) {
        InterlockedExchange(get_flag(), 1);
      }
    }
  }
  return TRUE;
}
