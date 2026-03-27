#include "dx9_test_utils.h"
#include <d3d9.h>

// Test scissor rect: enable scissor, draw fullscreen quad, verify clipping
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int)
{
    [[maybe_unused]] bool autoExit = (lpCmdLine && strstr(lpCmdLine, "--auto"));
    WNDCLASSEXW winClass = {};
    winClass.cbSize = sizeof(WNDCLASSEXW);
    winClass.style = CS_HREDRAW | CS_VREDRAW;
    winClass.lpfnWndProc = &WndProc;
    winClass.hInstance = hInstance;
    winClass.hIcon = LoadIconW(0, IDI_APPLICATION);
    winClass.hCursor = LoadCursorW(0, IDC_ARROW);
    winClass.lpszClassName = L"D3D9ScissorTestClass";
    winClass.hIconSm = LoadIconW(0, IDI_APPLICATION);
    RegisterClassExW(&winClass);

    RECT wr = { 0, 0, 1024, 768 };
    AdjustWindowRectEx(&wr, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_OVERLAPPEDWINDOW);
    HWND hwnd = CreateWindowExW(WS_EX_OVERLAPPEDWINDOW, winClass.lpszClassName,
                                L"D3D9 Scissor Test (DXMT)",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                wr.right - wr.left, wr.bottom - wr.top,
                                0, 0, hInstance, 0);
    if (!hwnd) { fprintf(stderr, "FATAL: CreateWindow failed\n"); return 1; }

    IDirect3D9 *d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 1024;
    pp.BackBufferHeight = 768;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = hwnd;

    IDirect3DDevice9 *device = nullptr;
    HRESULT hr = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                     D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr)) { fprintf(stderr, "FATAL: CreateDevice failed\n"); return 1; }

    DWORD fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
    struct Vertex { float x, y, z; DWORD color; };
    Vertex verts[] = {
        { -1.0f,  1.0f, 0.5f, 0xFFFF0000 },
        {  1.0f,  1.0f, 0.5f, 0xFFFF0000 },
        { -1.0f, -1.0f, 0.5f, 0xFFFF0000 },
        {  1.0f,  1.0f, 0.5f, 0xFFFF0000 },
        {  1.0f, -1.0f, 0.5f, 0xFFFF0000 },
        { -1.0f, -1.0f, 0.5f, 0xFFFF0000 },
    };

    IDirect3DVertexBuffer9 *vb = nullptr;
    device->CreateVertexBuffer(sizeof(verts), 0, fvf, D3DPOOL_DEFAULT, &vb, nullptr);
    void *data; vb->Lock(0, sizeof(verts), &data, 0);
    memcpy(data, verts, sizeof(verts)); vb->Unlock();

    D3DMATRIX identity = {};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    device->SetTransform(D3DTS_WORLD, &identity);
    device->SetTransform(D3DTS_VIEW, &identity);
    device->SetTransform(D3DTS_PROJECTION, &identity);

    // Clear to blue, then draw red fullscreen quad with scissor clipping to center 512x384
    device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF0000FF, 1.0f, 0);
    device->BeginScene();
    device->SetFVF(fvf);
    device->SetStreamSource(0, vb, 0, sizeof(Vertex));
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);

    // Scissor to center region: x=256..768, y=192..576
    RECT scissor = { 256, 192, 768, 576 };
    device->SetScissorRect(&scissor);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);

    device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 2);

    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->EndScene();

    // Readback
    IDirect3DSurface9 *backbuffer = nullptr;
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
    IDirect3DSurface9 *offscreen = nullptr;
    device->CreateOffscreenPlainSurface(1024, 768, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &offscreen, nullptr);
    device->GetRenderTargetData(backbuffer, offscreen);

    D3DLOCKED_RECT lr;
    offscreen->LockRect(&lr, nullptr, D3DLOCK_READONLY);

    auto getPixel = [&](int x, int y) -> uint32_t {
        return ((uint32_t *)((uint8_t *)lr.pBits + y * lr.Pitch))[x];
    };

    // Center (512, 384) should be red (inside scissor)
    uint32_t centerPix = getPixel(512, 384);
    uint8_t cR = (centerPix >> 16) & 0xFF;
    uint8_t cB = centerPix & 0xFF;

    // Corner (10, 10) should be blue (outside scissor)
    uint32_t cornerPix = getPixel(10, 10);
    uint8_t oR = (cornerPix >> 16) & 0xFF;
    uint8_t oB = cornerPix & 0xFF;

    char bmpPath[MAX_PATH];
    GetOutputPath("dx9_scissor.bmp", bmpPath, MAX_PATH);
    WriteBMP(bmpPath, lr.pBits, 1024, 768, lr.Pitch);
    offscreen->UnlockRect();

    bool centerRed = cR > 200 && cB < 50;
    bool cornerBlue = oB > 200 && oR < 50;

    fprintf(stderr, "TEST scissor_center_red: %s (0x%08x)\n", centerRed ? "PASS" : "FAIL", centerPix);
    fprintf(stderr, "TEST scissor_corner_blue: %s (0x%08x)\n", cornerBlue ? "PASS" : "FAIL", cornerPix);

    bool allPass = centerRed && cornerBlue;
    fprintf(stderr, "\n%s\n", allPass ? "ALL TESTS PASSED" : "SOME TESTS FAILED");

    vb->Release();
    backbuffer->Release(); offscreen->Release();
    device->Release(); d3d9->Release();
    return allPass ? 0 : 1;
}
