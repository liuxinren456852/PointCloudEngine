#include "PointCloudEngine.h"

// Variables for window creation and global access
std::wstring executablePath;
std::wstring executableDirectory;
HRESULT hr;
HWND hwnd = NULL;
LPCTSTR WndClassName = L"PointCloudEngine";
double dt = 0;
Timer timer;
Scene scene;
Settings* settings;
Camera* camera;
Shader* textShader;
Shader* splatShader;
Shader* octreeCubeShader;
Shader* octreeSplatShader;
Shader* octreeClusterShader;
Shader* octreeComputeShader;
Shader* octreeComputeVSShader;

// DirectX11 interface objects
IDXGISwapChain* swapChain;		                // Change between front and back buffer
ID3D11Device* d3d11Device;		                // GPU
ID3D11DeviceContext* d3d11DevCon;		        // (multi-threaded) rendering
ID3D11RenderTargetView* renderTargetView;		// 2D texture (backbuffer) -> output merger
ID3D11DepthStencilView* depthStencilView;
ID3D11Texture2D* depthStencilBuffer;
ID3D11DepthStencilState* depthStencilState;     // Standard depth/stencil state for 3d rendering
ID3D11BlendState* blendState;                   // Blend state that is used for transparency
ID3D11RasterizerState* rasterizerState;		    // Encapsulates settings for the rasterizer stage of the pipeline

void ErrorMessageOnFail(HRESULT hr, std::wstring message, std::wstring file, int line)
{
	if (FAILED(hr))
	{
		// Display the error code and message as well as the line and file of the error
		_com_error error(hr);
		std::wstringstream headerStream, messageStream;
		std::wstring filename = file.substr(file.find_last_of(L"/\\") + 1);
		headerStream << L"Error 0x" << std::hex << hr;
		messageStream << message << L"\n\n" << error.ErrorMessage() << L" in " << filename << " at line " << line;
		std::wstring header = std::wstring(headerStream.str());
		message = std::wstring(messageStream.str());
		MessageBox(hwnd, message.c_str(), header.c_str(), MB_ICONERROR | MB_APPLMODAL);
	}
}

bool LoadPlyFile(std::vector<Vertex> &vertices, const std::wstring &plyfile)
{
    try
    {
        // Load ply file
        std::ifstream ss(plyfile, std::ios::binary);

        tinyply::PlyFile file;
        file.parse_header(ss);

        // Tinyply untyped byte buffers for properties
        std::shared_ptr<tinyply::PlyData> rawPositions, rawNormals, rawColors;

        // Hardcoded properties and elements
        rawPositions = file.request_properties_from_element("vertex", { "x", "y", "z" });
        rawNormals = file.request_properties_from_element("vertex", { "nx", "ny", "nz" });
        rawColors = file.request_properties_from_element("vertex", { "red", "green", "blue" });

        // Read the file
        file.read(ss);

        // Create vertices
        size_t count = rawPositions->count;
        size_t stridePositions = rawPositions->buffer.size_bytes() / count;
        size_t strideNormals = rawNormals->buffer.size_bytes() / count;
        size_t strideColors = rawColors->buffer.size_bytes() / count;

        // When this trows an std::bad_alloc exception, the memory requirement is large -> build with x64
        vertices = std::vector<Vertex>(count);

        // Fill each vertex with its data
        for (int i = 0; i < count; i++)
        {
            std::memcpy(&vertices[i].position, rawPositions->buffer.get() + i * stridePositions, stridePositions);
            std::memcpy(&vertices[i].normal, rawNormals->buffer.get() + i * strideNormals, strideNormals);
            std::memcpy(&vertices[i].color, rawColors->buffer.get() + i * strideColors, strideColors);

            // Make sure that the normals are normalized
            vertices[i].normal.Normalize();
        }
    }
    catch (const std::exception &e)
    {
        return false;
    }

    return true;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    // Save the executable directory path
    wchar_t buffer [MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH);
    executablePath = std::wstring(buffer);
    executableDirectory = executablePath.substr(0, executablePath.find_last_of(L"\\/"));

    // Load the settings
    settings = new Settings();

	if (!InitializeWindow(hInstance, nShowCmd, settings->resolutionX, settings->resolutionY, true))
	{
		ERROR_MESSAGE(NAMEOF(InitializeWindow) + L" failed!");
		return 0;
	}

	if (!InitializeDirect3d11App(hInstance))
	{
		ERROR_MESSAGE(NAMEOF(InitializeDirect3d11App) + L" failed!");
		return 0;
	}

	if (!InitializeScene())
	{
		ERROR_MESSAGE(NAMEOF(InitializeScene) + L" failed!");
		return 0;
	}

	Messageloop();
	ReleaseObjects();
	return 0;
}

bool InitializeWindow(HINSTANCE hInstance, int ShowWnd, int width, int height, bool windowed)
{
	/*
	int ShowWnd - How the window should be displayed.Some common commands are SW_SHOWMAXIMIZED, SW_SHOW, SW_SHOWMINIMIZED.
	int width - Width of the window in pixels
	int height - Height of the window in pixels
	bool windowed - False if the window is fullscreen and true if the window is windowed
	*/

	WNDCLASSEX wc;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;		            // Redraw when the window is moved or changed size
	wc.lpfnWndProc = WndProc;		                    // lpfnWndProc is a pointer to the function we want to process the windows messages
	wc.cbClsExtra = NULL;		                        // cbClsExtra is the number of extra bytes allocated after WNDCLASSEX.
	wc.cbWndExtra = NULL;		                        // cbWndExtra specifies the number of bytes allocated after the windows instance.
	wc.hInstance = hInstance;		                    // Handle to the current application, GetModuleHandle() function can be used to get the current window application by passing NUll to its 1 parameter
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);		    // Cursor icon
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);		// Colors the background
	wc.lpszMenuName = NULL;		                        // Name to the menu that is attached to our window. we don't have one so we put NULL
	wc.lpszClassName = WndClassName;		            // Name the class here
    wc.hIcon = wc.hIconSm = (HICON) LoadImage(NULL, L"Assets/Icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED);

	if (!RegisterClassEx(&wc))
	{
		ERROR_MESSAGE(NAMEOF(RegisterClassEx) + L" failed!");
		return 1;
	}

	// Create window with extended styles like WS_EX_ACCEPTFILES, WS_EX_APPWINDOW, WS_EX_CONTEXTHELP, WS_EX_TOOLWINDOW
	hwnd = CreateWindowEx(NULL, WndClassName, L"PointCloudEngine", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, hInstance, NULL);

	if (!hwnd)
	{
		ERROR_MESSAGE(NAMEOF(CreateWindowEx) + L" failed!");
		return 1;
	}

	ShowWindow(hwnd, ShowWnd);
	UpdateWindow(hwnd);
    Input::Initialize(hwnd);

	return true;
}

bool InitializeDirect3d11App(HINSTANCE hInstance)
{    
	DXGI_MODE_DESC bufferDesc;		                                        // Describe the backbuffer
	ZeroMemory(&bufferDesc, sizeof(DXGI_MODE_DESC));		                // Clear everything for safety
	bufferDesc.Width = settings->resolutionX;		                        // Resolution X
	bufferDesc.Height = settings->resolutionY;		                        // Resolution Y
	bufferDesc.RefreshRate.Numerator = 144;		                            // Hertz
	bufferDesc.RefreshRate.Denominator = 1;
	bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;		                    // Describes the display format. 32bit unsigned int for 8bit Color RGBA
	bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;		// Describes the order in which the rasterizer renders - not used since we use double buffering
	bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;		                // Descibes how window scaling is handled

	DXGI_SWAP_CHAIN_DESC swapChainDesc;		                                // Describe the spaw chain
	ZeroMemory(&swapChainDesc, sizeof(DXGI_SWAP_CHAIN_DESC));
	swapChainDesc.BufferDesc = bufferDesc;
	swapChainDesc.SampleDesc.Count = settings->msaaCount;		            // Multisampling -> Smooth choppiness in edges and lines
    swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;		    // Describing the access the cpu has to the surface of the back buffer
	swapChainDesc.BufferCount = 1;		                                    // 1 for double buffering, 2 for triple buffering and so on
	swapChainDesc.OutputWindow = hwnd;
	swapChainDesc.Windowed = settings->windowed;		                    // Fullscreen might freeze the programm -> set windowed before exit
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;		            // Let display driver decide what to do when swapping buffers

    // Add flags for the creation of the device for debugging
    UINT flags = 0;

#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	// Create device and swap chain
	hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, NULL, NULL, D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &d3d11Device, NULL, &d3d11DevCon);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(D3D11CreateDeviceAndSwapChain) + L" failed!");

	ID3D11Texture2D* backBuffer;
	hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);		// Create backbuffer for the render target view

	// Create render target view, will be sended to the output merger stage of the pipeline
	hr = d3d11Device->CreateRenderTargetView(backBuffer, NULL, &renderTargetView);		// NULL -> view accesses all subresources in mipmap level 0
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateRenderTargetView) + L" failed!");
    SAFE_RELEASE(backBuffer);

	// Depth/Stencil buffer description (needed for 3D Scenes + mirrors and such)
	D3D11_TEXTURE2D_DESC depthStencilBufferDesc;
	depthStencilBufferDesc.Width = settings->resolutionX;
	depthStencilBufferDesc.Height = settings->resolutionY;
	depthStencilBufferDesc.MipLevels = 1;
	depthStencilBufferDesc.ArraySize = 1;
	depthStencilBufferDesc.Format = DXGI_FORMAT_D32_FLOAT;
	depthStencilBufferDesc.SampleDesc.Count = settings->msaaCount;
	depthStencilBufferDesc.SampleDesc.Quality = 0;
	depthStencilBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	depthStencilBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthStencilBufferDesc.CPUAccessFlags = 0;
	depthStencilBufferDesc.MiscFlags = 0;

	// Create the depth/stencil view
	hr = d3d11Device->CreateTexture2D(&depthStencilBufferDesc, NULL, &depthStencilBuffer);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateTexture2D) + L" failed!");

    // Depth / Stencil description
    D3D11_DEPTH_STENCIL_DESC depthStencilDesc;

    // Depth test parameters
    depthStencilDesc.DepthEnable = true;
    depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

    // Stencil test parameters
    depthStencilDesc.StencilEnable = true;
    depthStencilDesc.StencilReadMask = 0xFF;
    depthStencilDesc.StencilWriteMask = 0xFF;

    // Stencil operations if pixel is front-facing
    depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
    depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    // Stencil operations if pixel is back-facing
    depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
    depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

    // Create depth stencil state
    hr = d3d11Device->CreateDepthStencilState(&depthStencilDesc, &depthStencilState);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateDepthStencilState) + L" failed!");
    d3d11DevCon->OMSetDepthStencilState(depthStencilState, 0);

    // Create blend state for transparency
    D3D11_BLEND_DESC blendStateDesc;
    ZeroMemory(&blendStateDesc, sizeof(blendStateDesc));
    blendStateDesc.RenderTarget[0].BlendEnable = true;
    blendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    hr = d3d11Device->CreateBlendState(&blendStateDesc, &blendState);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateBlendState) + L" failed!");
    float blendFactor[] = {0, 0, 0, 0};
    UINT sampleMask = 0xffffffff;
    d3d11DevCon->OMSetBlendState(blendState, blendFactor, sampleMask);

    // Create Depth / Stencil View
    D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
    ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
    depthStencilViewDesc.Format = depthStencilBufferDesc.Format;
    depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    depthStencilViewDesc.Texture2DMS.UnusedField_NothingToDefine = 0;

	hr = d3d11Device->CreateDepthStencilView(depthStencilBuffer, &depthStencilViewDesc, &depthStencilView);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateDepthStencilView) + L" failed!");

	// Describing the render state
	D3D11_RASTERIZER_DESC rasterizerDesc;
	ZeroMemory(&rasterizerDesc, sizeof(D3D11_RASTERIZER_DESC));
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthBias = D3D11_DEFAULT_DEPTH_BIAS;
    rasterizerDesc.DepthBiasClamp = D3D11_DEFAULT_DEPTH_BIAS_CLAMP;
    rasterizerDesc.SlopeScaledDepthBias = D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rasterizerDesc.DepthClipEnable = TRUE;
    rasterizerDesc.ScissorEnable = FALSE;
    rasterizerDesc.MultisampleEnable = TRUE;
    rasterizerDesc.AntialiasedLineEnable = TRUE;

	hr = d3d11Device->CreateRasterizerState(&rasterizerDesc, &rasterizerState);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(d3d11Device->CreateRasterizerState) + L" failed!");

	// Bind the rasterizer render state
	d3d11DevCon->RSSetState(rasterizerState);

	return true;
}

// Main part of the program
int Messageloop()
{
	MSG msg;		// Structure of the windows message - holds the message`s information
	ZeroMemory(&msg, sizeof(MSG));		// Clear memory

	// While there is a message
	while (true)
	{
		// See if there is a windows message
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);		// Translating like the keyboard's virtual keys to characters
			DispatchMessage(&msg);		// Sends the message to our windows procedure, WndProc
		}
		else
		{
			// Run game code
			UpdateScene();
			DrawScene();
		}
	}

	return msg.wParam;
}

// Check messages for events
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Input::ProcessMessage(msg, wParam, lParam);

	switch (msg)
	{
	    case WM_DESTROY:
        {
		    PostQuitMessage(0);
		    return 0;
        }
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);		//Takes care of all other messages
}

bool InitializeScene()
{
    // Create and initialize the camera
    camera = new Camera();

    // Compile the shared shaders
    textShader = Shader::Create(L"Shader/Text.hlsl", true, true, true, false, Shader::textLayout, 3);
    splatShader = Shader::Create(L"Shader/Splat.hlsl", true, true, true, false, Shader::splatLayout, 3);
    octreeCubeShader = Shader::Create(L"Shader/OctreeCube.hlsl", true, true, true, false, Shader::octreeLayout, 15);
    octreeSplatShader = Shader::Create(L"Shader/OctreeSplat.hlsl", true, true, true, false, Shader::octreeLayout, 15);
    octreeClusterShader = Shader::Create(L"Shader/OctreeCluster.hlsl", true, true, true, false, Shader::octreeLayout, 15);
    octreeComputeShader = Shader::Create(L"Shader/OctreeCompute.hlsl", false, false, false, true, NULL, 0);
    octreeComputeVSShader = Shader::Create(L"Shader/OctreeComputeVS.hlsl", true, false, false, false, NULL, 0);

    // Load fonts
    TextRenderer::CreateSpriteFont(L"Consolas", L"Assets/Consolas.spritefont");
    TextRenderer::CreateSpriteFont(L"Times New Roman", L"Assets/Times New Roman.spritefont");

	scene.Initialize();
    timer.ResetElapsedTime();

	return true;
}

void UpdateScene()
{
    Input::Update();

    timer.Tick([&]()
    {
        dt = timer.GetElapsedSeconds();
    });

    scene.Update(timer);
}

void DrawScene()
{
    // Bind the render target view to the output merger stage of the pipeline, also bind depth/stencil view as well
    d3d11DevCon->OMSetRenderTargets(1, &renderTargetView, depthStencilView);	// 1 since there is only 1 view

	// Clear out backbuffer to the updated color
	float backgroundColor[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
	d3d11DevCon->ClearRenderTargetView(renderTargetView, backgroundColor);
	
	// Refresh the depth/stencil view
	d3d11DevCon->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    // Calculates view and projection matrices and sets the viewport
    camera->PrepareDraw();

    // Draw scene
    scene.Draw();

	// Present backbuffer to the screen
	hr = swapChain->Present(1, 0);
	ERROR_MESSAGE_ON_FAIL(hr, NAMEOF(swapChain->Present) + L" failed!");
}

void ReleaseObjects()
{
    // Delete settings (also saves them to the hard drive)
    SafeDelete(settings);
    SafeDelete(camera);

    // Release and delete shaders
    Shader::ReleaseAllShaders();
    TextRenderer::ReleaseAllSpriteFonts();

    // Release scene objects
    scene.Release();

    // Release the COM (Component Object Model) objects
    SAFE_RELEASE(swapChain);
    SAFE_RELEASE(d3d11Device);
    SAFE_RELEASE(d3d11DevCon);
    SAFE_RELEASE(renderTargetView);
    SAFE_RELEASE(depthStencilView);
    SAFE_RELEASE(depthStencilState);
    SAFE_RELEASE(depthStencilBuffer);
    SAFE_RELEASE(rasterizerState);
}
