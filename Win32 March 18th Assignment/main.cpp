#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console")

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// --- 전역 변수 ---
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;

const int WINDOWED_WIDTH = 800;
const int WINDOWED_HEIGHT = 600;

struct VideoConfig
{
    int Width = WINDOWED_WIDTH;
    int Height = WINDOWED_HEIGHT;
    bool IsFullscreen = false;
    bool NeedsResize = false;
    int VSync = 1;
};
VideoConfig g_Config;

bool g_keys[256] = { false };
bool g_prevFKey = false;

struct Vertex {
    float x, y, z;
    float r, g, b, a;
};

const char* shaderSource = R"(
struct Input
{
    float3 pos : POSITION;
    float4 col : COLOR;
};

struct Output
{
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};

Output VSMain(Input input)
{
    Output output;
    output.pos = float4(input.pos, 1.0f);
    output.col = input.col;
    return output;
}

float4 PSMain(Output input) : SV_Target
{
    return input.col;
}
)";

class GameObject;

class Component
{
public:
    virtual ~Component() = default;

    virtual void Start() {}
    virtual void Input() {}
    virtual void Update(float dt) {}
    virtual void Render() {}

    void SetOwner(GameObject* owner) { m_owner = owner; }

protected:
    GameObject* m_owner = nullptr;
};

class GameObject
{
public:
    std::string Name;
    float X = 0.0f;
    float Y = 0.0f;

    void AddComponent(Component* component)
    {
        component->SetOwner(this);
        m_components.push_back(component);
    }

    void Start() { for (Component* c : m_components) c->Start(); }
    void Input() { for (Component* c : m_components) c->Input(); }
    void Update(float dt) { for (Component* c : m_components) c->Update(dt); }
    void Render() { for (Component* c : m_components) c->Render(); }

    ~GameObject()
    {
        for (Component* c : m_components) delete c;
        m_components.clear();
    }

private:
    std::vector<Component*> m_components;
};

class RendererComponent : public Component
{
public:
    RendererComponent(const std::vector<Vertex>& vertices)
        : m_vertices(vertices)
    {
    }

    bool Initialize()
    {
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;

        HRESULT hr = D3DCompile(
            shaderSource, strlen(shaderSource),
            nullptr, nullptr, nullptr,
            "VSMain", "vs_4_0",
            0, 0, &vsBlob, &errorBlob
        );

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                printf("VS Compile Error: %s\n", (char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            return false;
        }
        if (errorBlob) { errorBlob->Release(); errorBlob = nullptr; }

        hr = D3DCompile(
            shaderSource, strlen(shaderSource),
            nullptr, nullptr, nullptr,
            "PSMain", "ps_4_0",
            0, 0, &psBlob, &errorBlob
        );

        if (FAILED(hr))
        {
            if (errorBlob)
            {
                printf("PS Compile Error: %s\n", (char*)errorBlob->GetBufferPointer());
                errorBlob->Release();
            }
            if (vsBlob) vsBlob->Release();
            return false;
        }
        if (errorBlob) { errorBlob->Release(); errorBlob = nullptr; }

        hr = g_pd3dDevice->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            &m_pVertexShader
        );
        if (FAILED(hr))
        {
            vsBlob->Release();
            psBlob->Release();
            return false;
        }

        hr = g_pd3dDevice->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            &m_pPixelShader
        );
        if (FAILED(hr))
        {
            vsBlob->Release();
            psBlob->Release();
            return false;
        }

        D3D11_INPUT_ELEMENT_DESC layout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        hr = g_pd3dDevice->CreateInputLayout(
            layout, 2,
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            &m_pInputLayout
        );

        vsBlob->Release();
        psBlob->Release();

        if (FAILED(hr))
            return false;

        D3D11_RASTERIZER_DESC rsDesc = {};
        rsDesc.FillMode = D3D11_FILL_SOLID;
        rsDesc.CullMode = D3D11_CULL_NONE;
        rsDesc.FrontCounterClockwise = FALSE;
        rsDesc.DepthClipEnable = TRUE;

        hr = g_pd3dDevice->CreateRasterizerState(&rsDesc, &m_pRasterState);
        if (FAILED(hr))
            return false;

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.ByteWidth = (UINT)(sizeof(Vertex) * m_vertices.size());
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = g_pd3dDevice->CreateBuffer(&bd, nullptr, &m_pVertexBuffer);
        if (FAILED(hr))
            return false;

        return true;
    }

    virtual void Render() override
    {
        if (!m_owner || m_vertices.empty())
            return;

        std::vector<Vertex> currentVertices = m_vertices;

        for (size_t i = 0; i < currentVertices.size(); i++)
        {
            currentVertices[i].x += m_owner->X;
            currentVertices[i].y += m_owner->Y;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        HRESULT hr = g_pImmediateContext->Map(
            m_pVertexBuffer, 0,
            D3D11_MAP_WRITE_DISCARD, 0, &mapped
        );

        if (SUCCEEDED(hr))
        {
            memcpy(mapped.pData, currentVertices.data(), sizeof(Vertex) * currentVertices.size());
            g_pImmediateContext->Unmap(m_pVertexBuffer, 0);
        }

        UINT stride = sizeof(Vertex);
        UINT offset = 0;

        g_pImmediateContext->RSSetState(m_pRasterState);
        g_pImmediateContext->IASetInputLayout(m_pInputLayout);
        g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_pImmediateContext->IASetVertexBuffers(0, 1, &m_pVertexBuffer, &stride, &offset);
        g_pImmediateContext->VSSetShader(m_pVertexShader, nullptr, 0);
        g_pImmediateContext->PSSetShader(m_pPixelShader, nullptr, 0);

        g_pImmediateContext->Draw((UINT)m_vertices.size(), 0);
    }

    virtual ~RendererComponent()
    {
        if (m_pVertexBuffer) { m_pVertexBuffer->Release(); m_pVertexBuffer = nullptr; }
        if (m_pRasterState) { m_pRasterState->Release(); m_pRasterState = nullptr; }
        if (m_pInputLayout) { m_pInputLayout->Release(); m_pInputLayout = nullptr; }
        if (m_pVertexShader) { m_pVertexShader->Release(); m_pVertexShader = nullptr; }
        if (m_pPixelShader) { m_pPixelShader->Release(); m_pPixelShader = nullptr; }
        if (m_pTexture) { m_pTexture->Release(); m_pTexture = nullptr; }
        if (m_pSamplerState) { m_pSamplerState->Release(); m_pSamplerState = nullptr; }
    }

private:
    ID3D11VertexShader* m_pVertexShader = nullptr;
    ID3D11PixelShader* m_pPixelShader = nullptr;
    ID3D11InputLayout* m_pInputLayout = nullptr;
    ID3D11Buffer* m_pVertexBuffer = nullptr;
    ID3D11RasterizerState* m_pRasterState = nullptr;

    ID3D11ShaderResourceView* m_pTexture = nullptr;
    ID3D11SamplerState* m_pSamplerState = nullptr;

    std::vector<Vertex> m_vertices;
};


class WASDController : public Component
{
public:
    virtual void Update(float dt) override
    {
        if (!m_owner) return;

        float moveAmount = 0.8f * dt;

        if (g_keys['W']) m_owner->Y += moveAmount;
        if (g_keys['S']) m_owner->Y -= moveAmount;
        if (g_keys['A']) m_owner->X -= moveAmount;
        if (g_keys['D']) m_owner->X += moveAmount;

        ClampPosition();
    }

private:
    void ClampPosition()
    {
        if (m_owner->X > 0.8f) m_owner->X = 0.8f;
        if (m_owner->X < -0.8f) m_owner->X = -0.8f;
        if (m_owner->Y > 0.8f) m_owner->Y = 0.8f;
        if (m_owner->Y < -0.8f) m_owner->Y = -0.8f;
    }
};

class ArrowKeyController : public Component
{
public:
    virtual void Update(float dt) override
    {
        if (!m_owner) return;

        float moveAmount = 0.8f * dt;

        if (g_keys[VK_UP])    m_owner->Y += moveAmount;
        if (g_keys[VK_DOWN])  m_owner->Y -= moveAmount;
        if (g_keys[VK_LEFT])  m_owner->X -= moveAmount;
        if (g_keys[VK_RIGHT]) m_owner->X += moveAmount;

        ClampPosition();
    }

private:
    void ClampPosition()
    {
        if (m_owner->X > 0.8f) m_owner->X = 0.8f;
        if (m_owner->X < -0.8f) m_owner->X = -0.8f;
        if (m_owner->Y > 0.8f) m_owner->Y = 0.8f;
        if (m_owner->Y < -0.8f) m_owner->Y = -0.8f;
    }
};


// --- GameLoop 선언 ---
class GameLoop
{
public:
    bool Initialize(HWND hWnd);
    void Input();
    void Update(float dt);
    void Render();
    void Run();
    void Shutdown();

private:
    HWND m_hWnd = nullptr;
    std::vector<GameObject*> m_gameObjects;
    bool m_hasStarted = false;
};

// --- 입력 처리 (WndProc) ---
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


// --- 리소스 재빌드 함수 ---
bool RebuildVideoResources(HWND hWnd)
{
    if (!g_pSwapChain || !g_pd3dDevice || !g_pImmediateContext)
        return false;

    g_pImmediateContext->OMSetRenderTargets(0, nullptr, nullptr);
    g_pImmediateContext->Flush();

    if (g_pRenderTargetView)
    {
        g_pRenderTargetView->Release();
        g_pRenderTargetView = nullptr;
    }

    HRESULT hr = g_pSwapChain->SetFullscreenState(g_Config.IsFullscreen ? TRUE : FALSE, nullptr);
    if (FAILED(hr)) return false;

    if (g_Config.IsFullscreen)
    {
        g_Config.Width = GetSystemMetrics(SM_CXSCREEN);
        g_Config.Height = GetSystemMetrics(SM_CYSCREEN);

        SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(hWnd, HWND_TOP, 0, 0, g_Config.Width, g_Config.Height, SWP_FRAMECHANGED);
    }
    else
    {
        g_Config.Width = WINDOWED_WIDTH;
        g_Config.Height = WINDOWED_HEIGHT;

        SetWindowLongPtr(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

        RECT rc = { 0, 0, WINDOWED_WIDTH, WINDOWED_HEIGHT };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        SetWindowPos(hWnd, HWND_TOP, 100, 100, rc.right - rc.left, rc.bottom - rc.top, SWP_FRAMECHANGED);
    }

    hr = g_pSwapChain->ResizeBuffers(0, g_Config.Width, g_Config.Height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return false;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return false;

    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)g_Config.Width;
    vp.Height = (FLOAT)g_Config.Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    g_pImmediateContext->RSSetViewports(1, &vp);

    return true;
}

bool GameLoop::Initialize(HWND hWnd)
{
    m_hWnd = hWnd;

    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);

    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (FLOAT)g_Config.Width;
    vp.Height = (FLOAT)g_Config.Height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pImmediateContext->RSSetViewports(1, &vp);

    // 위쪽 삼각형 (빨강) - 방향키
    std::vector<Vertex> triangleA =
    {
        {  0.0f,    0.42f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        { -0.3637f, -0.21f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        {  0.3637f, -0.21f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f }
    };

    // 아래쪽 삼각형 (파랑) - WASD
    std::vector<Vertex> triangleB =
    {
        {  0.0f,   -0.42f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        {  0.3637f, 0.21f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { -0.3637f, 0.21f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f }
    };

    GameObject* obj1 = new GameObject();
    obj1->Name = "Triangle_Arrow";
    obj1->X = -0.5f;
    obj1->Y = 0.0f;

    RendererComponent* renderer1 = new RendererComponent(triangleA);
    if (!renderer1->Initialize())
    {
        delete renderer1;
        delete obj1;
        return false;
    }

    obj1->AddComponent(renderer1);
    obj1->AddComponent(new ArrowKeyController());

    GameObject* obj2 = new GameObject();
    obj2->Name = "Triangle_WASD";
    obj2->X = 0.5f;
    obj2->Y = 0.0f;

    RendererComponent* renderer2 = new RendererComponent(triangleB);
    if (!renderer2->Initialize())
    {
        delete renderer2;
        delete obj1;
        delete obj2;
        return false;
    }

    obj2->AddComponent(renderer2);
    obj2->AddComponent(new WASDController());

    m_gameObjects.push_back(obj1);
    m_gameObjects.push_back(obj2);

    return true;
}


void GameLoop::Update(float dt) {
    for (GameObject* obj : m_gameObjects)
    {
        obj->Update(dt);
    }
}

void GameLoop::Render()
{
    float clearColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);

    for (GameObject* obj : m_gameObjects)
    {
        obj->Render();
    }

    g_pSwapChain->Present(g_Config.VSync, 0);
}

void GameLoop::Input()
{
    if (g_keys[VK_ESCAPE])
    {
        PostQuitMessage(0);
        return;
    }

    bool currentFKey = g_keys['F'];

    if (currentFKey && !g_prevFKey)
    {
        g_Config.IsFullscreen = !g_Config.IsFullscreen;

        if (!RebuildVideoResources(m_hWnd))
        {
            printf("Failed to rebuild video resources.\n");
            PostQuitMessage(0);
            return;
        }
    }

    g_prevFKey = currentFKey;

    for (GameObject* obj : m_gameObjects)
    {
        obj->Input();
    }
}


void GameLoop::Run()
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER previousTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previousTime);

    double fpsAccumTime = 0.0;
    int frameCount = 0;

    MSG msg = {};

    while (msg.message != WM_QUIT)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (msg.message == WM_QUIT)
            break;

        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);

        double deltaTime = (double)(currentTime.QuadPart - previousTime.QuadPart) / (double)frequency.QuadPart;
        previousTime = currentTime;

        if (!m_hasStarted)
        {
            for (GameObject* obj : m_gameObjects)
                obj->Start();

            m_hasStarted = true;
        }

        Input();
        Update((float)deltaTime);
        Render();

        fpsAccumTime += deltaTime;
        frameCount++;

        if (fpsAccumTime >= 1.0)
        {
            double fps = frameCount / fpsAccumTime;

            system("cls");
            printf("========================\n");
            printf(" DeltaTime : %.6f sec\n", deltaTime);
            printf(" FPS       : %.2f\n", fps);
            printf(" Width     : %d\n", g_Config.Width);
            printf(" Height    : %d\n", g_Config.Height);
            printf(" Fullscreen: %s\n", g_Config.IsFullscreen ? "ON" : "OFF");
            printf("========================\n");

            frameCount = 0;
            fpsAccumTime -= 1.0;
        }
    }
}

void GameLoop::Shutdown()
{
    for (GameObject* obj : m_gameObjects)
    {
        delete obj;
    }
    m_gameObjects.clear();

    if (g_pImmediateContext)
    {
        g_pImmediateContext->OMSetRenderTargets(0, nullptr, nullptr);
        g_pImmediateContext->Flush();
    }

    if (g_pSwapChain)
    {
        g_pSwapChain->SetFullscreenState(FALSE, nullptr);
    }

    if (g_pRenderTargetView) { g_pRenderTargetView->Release(); g_pRenderTargetView = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pImmediateContext) { g_pImmediateContext->Release(); g_pImmediateContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

    WNDCLASSEXW wcex = { sizeof(WNDCLASSEXW) };
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.lpszClassName = L"DX11Win32Class";
    RegisterClassExW(&wcex);

    RECT rc = { 0, 0, WINDOWED_WIDTH, WINDOWED_HEIGHT };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindowW(
        L"DX11Win32Class", L"DirectX 11 game engine",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) return -1;
    ShowWindow(hWnd, nCmdShow);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_Config.Width;
    sd.BufferDesc.Height = g_Config.Height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = !g_Config.IsFullscreen;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, nullptr, &g_pImmediateContext
    );
    if (FAILED(hr)) return -1;

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return -1;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return -1;

    GameLoop game;

    if (!game.Initialize(hWnd))
    {
        game.Shutdown();
        return -1;
    }

    game.Run();

    game.Shutdown();

    return 0;
}