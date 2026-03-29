#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

const char* shaderSource = R"(
struct VS_INPUT {    float3 pos : POSITION;    float4 col : COLOR;};
struct PS_INPUT {    float4 pos : SV_POSITION;    float4 col : COLOR;};
PS_INPUT VS(VS_INPUT input) {
    PS_INPUT output;
    output.pos = float4(input.pos, 1.0f);
    output.col = input.col;
    return output;
}
float4 PS(PS_INPUT input) : SV_Target {
    return input.col;
}
)";

float g_playerX = 0.0f;
float g_playerY = 0.0f;
bool g_keys[256] = { false };

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN:
        if (wParam < 256) g_keys[wParam] = true;
        break;
    case WM_KEYUP:
        if (wParam < 256) g_keys[wParam] = false;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"DX11Win32Class";
    RegisterClassExW(&wcex);

    RECT rc = { 0, 0, 600, 600 };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindowW(
        L"DX11Win32Class",
        L"DirectX11 Hexagram 600x600",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) return -1;
    ShowWindow(hWnd, nCmdShow);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = 600;
    sd.BufferDesc.Height = 600;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        nullptr,
        &g_pImmediateContext
    );
    if (FAILED(hr)) return -1;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return -1;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return -1;

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr,
        "VS", "vs_4_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            printf("VS Compile Error: %s\n", (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return -1;
    }

    hr = D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr,
        "PS", "ps_4_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            printf("PS Compile Error: %s\n", (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        vsBlob->Release();
        return -1;
    }

    ID3D11VertexShader* vShader = nullptr;
    ID3D11PixelShader* pShader = nullptr;
    ID3D11InputLayout* pInputLayout = nullptr;
    ID3D11Buffer* pVBuffer = nullptr;
    ID3D11RasterizerState* pRasterState = nullptr;

    hr = g_pd3dDevice->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vShader);
    if (FAILED(hr)) return -1;

    hr = g_pd3dDevice->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pShader);
    if (FAILED(hr)) return -1;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_pd3dDevice->CreateInputLayout(
        layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &pInputLayout);
    vsBlob->Release();
    psBlob->Release();
    if (FAILED(hr)) return -1;

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.FrontCounterClockwise = FALSE;
    rsDesc.DepthClipEnable = TRUE;

    hr = g_pd3dDevice->CreateRasterizerState(&rsDesc, &pRasterState);
    if (FAILED(hr)) return -1;

    Vertex baseHexagram[6] = {
        {  0.0f,    0.42f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        { -0.3637f, -0.21f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        {  0.3637f, -0.21f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },

        {  0.0f,   -0.42f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        {  0.3637f, 0.21f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { -0.3637f, 0.21f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(baseHexagram);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &pVBuffer);
    if (FAILED(hr)) return -1;

    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = 600.0f;
    vp.Height = 600.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pImmediateContext->RSSetViewports(1, &vp);

    g_pImmediateContext->RSSetState(pRasterState);

    g_pImmediateContext->IASetInputLayout(pInputLayout);
    g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pImmediateContext->VSSetShader(vShader, nullptr, 0);
    g_pImmediateContext->PSSetShader(pShader, nullptr, 0);

    LARGE_INTEGER frequency;
    LARGE_INTEGER loopStart, loopEnd;
    QueryPerformanceFrequency(&frequency);

    double fpsAccumTime = 0.0;
    int frameCount = 0;

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        QueryPerformanceCounter(&loopStart);

        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (msg.message == WM_QUIT)
            break;

        static double previousDeltaTime = 0.016;
        float moveAmount = (float)(0.8 * previousDeltaTime);

        if (g_keys['W']) g_playerY += moveAmount;
        if (g_keys['S']) g_playerY -= moveAmount;
        if (g_keys['A']) g_playerX -= moveAmount;
        if (g_keys['D']) g_playerX += moveAmount;

        if (g_playerX > 0.55f) g_playerX = 0.55f;
        if (g_playerX < -0.55f) g_playerX = -0.55f;
        if (g_playerY > 0.55f) g_playerY = 0.55f;
        if (g_playerY < -0.55f) g_playerY = -0.55f;

        Vertex currentVertices[6];
        for (int i = 0; i < 6; i++) {
            currentVertices[i] = baseHexagram[i];
            currentVertices[i].x += g_playerX;
            currentVertices[i].y += g_playerY;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        hr = g_pImmediateContext->Map(pVBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(hr)) {
            memcpy(mapped.pData, currentVertices, sizeof(currentVertices));
            g_pImmediateContext->Unmap(pVBuffer, 0);
        }

        float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);

        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        g_pImmediateContext->IASetVertexBuffers(0, 1, &pVBuffer, &stride, &offset);

        g_pImmediateContext->Draw(6, 0);

        g_pSwapChain->Present(0, 0);

        QueryPerformanceCounter(&loopEnd);
        double deltaTime = (double)(loopEnd.QuadPart - loopStart.QuadPart) / (double)frequency.QuadPart;
        previousDeltaTime = deltaTime;

        fpsAccumTime += deltaTime;
        frameCount++;

        if (fpsAccumTime >= 1.0) {
            double fps = frameCount / fpsAccumTime;

            system("cls");
            printf("DeltaTime: %.6f sec | FPS: %.2f\n", deltaTime, fps);

            frameCount = 0;
            fpsAccumTime -= 1.0;
        }
    }

    if (pVBuffer) pVBuffer->Release();
    if (pRasterState) pRasterState->Release();
    if (pInputLayout) pInputLayout->Release();
    if (vShader) vShader->Release();
    if (pShader) pShader->Release();
    if (g_pRenderTargetView) g_pRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pImmediateContext) g_pImmediateContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();

    return (int)msg.wParam;
}