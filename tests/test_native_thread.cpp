// Test that NtCreateThreadEx with THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH
// actually skips DLL_THREAD_ATTACH callbacks on the new thread.
//
// We load a helper DLL whose DLL_THREAD_ATTACH sets a shared flag.
// A thread created with SKIP_THREAD_ATTACH should NOT trigger the callback,
// while a normal thread SHOULD. This is the real-world scenario: our Metal
// worker threads must avoid DLL_THREAD_ATTACH callbacks that could block
// on the main thread and cause a deadlock.

#include <windows.h>
#include <stdio.h>

typedef LONG(WINAPI *NtCreateThreadEx_t)(
    HANDLE *, ACCESS_MASK, void *, HANDLE,
    DWORD(WINAPI *)(void *), void *,
    ULONG, ULONG_PTR, SIZE_T, SIZE_T, void *);

// Shared flag set by the helper DLL's DLL_THREAD_ATTACH
static volatile LONG g_attach_called = 0;

// Exported so the helper DLL can find it
extern "C" __declspec(dllexport) volatile LONG *get_attach_flag() {
  return &g_attach_called;
}

static DWORD WINAPI thread_func(void *arg) {
  // Just run and exit
  return 0;
}

int main() {
  HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  if (!ntdll) {
    printf("FAIL: could not get ntdll\n");
    return 1;
  }

  auto pNtCreateThreadEx =
      (NtCreateThreadEx_t)GetProcAddress(ntdll, "NtCreateThreadEx");
  if (!pNtCreateThreadEx) {
    printf("FAIL: could not resolve NtCreateThreadEx\n");
    return 1;
  }

  // Load helper DLL that sets g_attach_called on DLL_THREAD_ATTACH
  HMODULE helper = LoadLibraryA("test_attach_helper.dll");
  if (!helper) {
    printf("FAIL: could not load test_attach_helper.dll (error %lu)\n",
           GetLastError());
    return 1;
  }
  printf("Helper DLL loaded\n");

  // --- Test 1: normal thread SHOULD trigger DLL_THREAD_ATTACH ---
  g_attach_called = 0;
  HANDLE hNormal = CreateThread(NULL, 0, thread_func, NULL, 0, NULL);
  if (!hNormal) {
    printf("FAIL: CreateThread failed\n");
    return 1;
  }
  WaitForSingleObject(hNormal, 5000);
  CloseHandle(hNormal);

  if (!g_attach_called) {
    printf("FAIL: normal thread did not trigger DLL_THREAD_ATTACH\n");
    return 1;
  }
  printf("OK: normal thread triggered DLL_THREAD_ATTACH\n");

  // --- Test 2: SKIP_THREAD_ATTACH thread should NOT trigger callback ---
  g_attach_called = 0;
  HANDLE hSkip = NULL;
  LONG status = pNtCreateThreadEx(
      &hSkip, THREAD_ALL_ACCESS, NULL, GetCurrentProcess(), thread_func, NULL,
      0x2 /* THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH */, 0, 0x100000, 0x100000,
      NULL);

  if (status != 0) {
    printf("FAIL: NtCreateThreadEx returned 0x%lx\n", (unsigned long)status);
    return 1;
  }

  DWORD wait = WaitForSingleObject(hSkip, 5000);
  CloseHandle(hSkip);

  if (wait == WAIT_TIMEOUT) {
    printf("FAIL: thread with SKIP_THREAD_ATTACH timed out\n");
    return 1;
  }

  if (g_attach_called) {
    printf("FAIL: SKIP_THREAD_ATTACH thread still triggered DLL_THREAD_ATTACH\n");
    return 1;
  }
  printf("OK: SKIP_THREAD_ATTACH thread skipped DLL_THREAD_ATTACH\n");

  FreeLibrary(helper);
  printf("PASS: SKIP_THREAD_ATTACH correctly prevents DLL_THREAD_ATTACH callbacks\n");
  return 0;
}
