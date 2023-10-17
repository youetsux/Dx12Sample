#include <tchar.h>
#include <wrl.h>		// Microsoft::WRL::ComPtr

#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <dxgi1_4.h>
#pragma comment(lib, "dxgi.lib")
#include <D3Dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <DirectXMath.h>

using namespace DirectX;
using namespace Microsoft::WRL;

#define WINDOW_CLASS	_T("DirectX12Test")
#define WINDOW_TITLE	WINDOW_CLASS
#define	WINDOW_STYLE	WS_OVERLAPPEDWINDOW
#define WINDOW_WIDTH	1280
#define WINDOW_HEIGHT	720

// �E�B���h�E�v���V�[�W��
LRESULT CALLBACK WindowProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);
// ������
BOOL Init(HWND hWnd);
// �`��
BOOL Draw();
// 
BOOL WaitForPreviousFrame();

const UINT	FrameCount = 2;

struct Vertex {
	XMFLOAT3	position;
	XMFLOAT4	color;
};

// �p�C�v���C���I�u�W�F�N�g
D3D12_VIEWPORT				g_viewport = { 0.0f, 0.0f, (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT, 0.0f, 1.0f };
D3D12_RECT				g_scissorRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
ComPtr<IDXGISwapChain3>			g_swapChain;
ComPtr<ID3D12Device>			g_device;
ComPtr<ID3D12Resource>			g_renderTargets[FrameCount];
ComPtr<ID3D12CommandAllocator>		g_commandAllocator;
ComPtr<ID3D12CommandQueue>		g_commandQueue;
ComPtr<ID3D12RootSignature>		g_rootSignature;
ComPtr<ID3D12DescriptorHeap>		g_rtvHeap;
ComPtr<ID3D12PipelineState>		g_pipelineState;
ComPtr<ID3D12GraphicsCommandList>	g_commandList;
UINT					g_rtvDescriptorSize = 0;

// ���\�[�X
ComPtr<ID3D12Resource>		g_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW	g_vertexBufferView;

// �����I�u�W�F�N�g
UINT			g_frameIndex = 0;
HANDLE			g_fenceEvent;
ComPtr<ID3D12Fence>	g_fence;
UINT64			g_fenceValue;

// �r���[�|�[�g�̃A�X�y�N�g��
float	g_aspectRatio = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;

// �A�_�v�^���
bool	g_useWarpDevice = false;

int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, TCHAR* lpszCmdLine, int nCmdShow)
{
	// �E�B���h�E���쐬
	WNDCLASSEX	wndclass = {};

	// �E�B���h�E�N���X��o�^
	wndclass.cbSize = sizeof(WNDCLASSEX);
	wndclass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wndclass.lpfnWndProc = WindowProc;
	wndclass.hInstance = hInstance;
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.lpszClassName = WINDOW_CLASS;
	RegisterClassEx(&wndclass);

	RECT	windowRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };

	AdjustWindowRect(&windowRect, WINDOW_STYLE, FALSE);

	// �E�B���h�E���쐬
	HWND	hWnd = CreateWindow(
		WINDOW_CLASS,
		WINDOW_TITLE,
		WINDOW_STYLE,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,
		NULL,
		hInstance,
		NULL);

	// DirectX��������
	if (!Init(hWnd)) {
		MessageBox(hWnd, _T("DirectX�̏����������s���܂���"), _T("Init"), MB_OK | MB_ICONEXCLAMATION);
		return 0;
	}

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);

	// ���b�Z�[�W���[�v
	MSG	msg;

	while (1) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	// �I�����̌㏈��
	WaitForPreviousFrame();
	CloseHandle(g_fenceEvent);

	return (int)msg.wParam;
}

// �E�B���h�E�v���V�[�W��
LRESULT CALLBACK WindowProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
	switch (nMsg) {
	case WM_PAINT:
		// �`��
		Draw();
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, nMsg, wParam, lParam);
	}

	return 0;
}
// ������
BOOL Init(HWND hWnd)
{
#if defined(_DEBUG)
	// DirectX12�̃f�o�b�O���C���[��L���ɂ���
	{
		ComPtr<ID3D12Debug>	debugController;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
			debugController->EnableDebugLayer();
		}
	}
#endif

	// DirectX12���T�|�[�g���闘�p�\�ȃn�[�h�E�F�A�A�_�v�^���擾
	ComPtr<IDXGIFactory4>	factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return FALSE;

	if (g_useWarpDevice) {
		ComPtr<IDXGIAdapter>	warpAdapter;
		if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)))) return FALSE;
		if (FAILED(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)))) return FALSE;
	}
	else {
		ComPtr<IDXGIAdapter1>	hardwareAdapter;
		ComPtr<IDXGIAdapter1>	adapter;
		hardwareAdapter = nullptr;

		for (UINT i = 0; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(i, &adapter); i++) {
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1(&desc);
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
			// �A�_�v�^��DirectX12�ɑΉ����Ă��邩�m�F
			if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) break;
		}

		hardwareAdapter = adapter.Detach();

		if (FAILED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&g_device)))) return FALSE;
	}
	// �R�}���h�L���[���쐬
	D3D12_COMMAND_QUEUE_DESC	queueDesc = {};
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

	if (FAILED(g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue)))) return FALSE;

	// �X���b�v�`�F�C�����쐬
	DXGI_SWAP_CHAIN_DESC1	swapChainDesc = {};
	swapChainDesc.BufferCount = FrameCount;
	swapChainDesc.Width = WINDOW_WIDTH;
	swapChainDesc.Height = WINDOW_HEIGHT;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1>	swapChain;
	if (FAILED(factory->CreateSwapChainForHwnd(g_commandQueue.Get(), hWnd, &swapChainDesc, nullptr, nullptr, &swapChain))) return FALSE;

	// �t���X�N���[���̃T�|�[�g�Ȃ�
	if (FAILED(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER))) return FALSE;

	if (FAILED(swapChain.As(&g_swapChain))) return FALSE;
	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
	// �L�q�q�q�[�v���쐬
	{
		// �����_�[�^�[�Q�b�g�r���[�p�̋L�q�q�q�[�v���쐬
		D3D12_DESCRIPTOR_HEAP_DESC	rtvHeapDesc = {};
		rtvHeapDesc.NumDescriptors = FrameCount;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		if (FAILED(g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)))) return FALSE;

		g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	}

	// �t���[�����\�[�X���쐬
	{
		D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
		rtvHandle.ptr = g_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr;

		// �t���[���o�b�t�@�ƃo�b�N�o�b�t�@�̂̃����_�[�^�[�Q�b�g�r���[���쐬
		for (UINT i = 0; i < FrameCount; i++) {
			if (FAILED(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])))) return FALSE;
			g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr, rtvHandle);
			rtvHandle.ptr += g_rtvDescriptorSize;
		}
	}

	// �R�}���h�A���P�[�^�[���쐬
	if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator)))) return FALSE;

	// ��̃��[�g�V�O�l�`�����쐬
	{
		D3D12_ROOT_SIGNATURE_DESC	rootSignatureDesc;
		rootSignatureDesc.NumParameters = 0;
		rootSignatureDesc.pParameters = nullptr;
		rootSignatureDesc.NumStaticSamplers = 0;
		rootSignatureDesc.pStaticSamplers = nullptr;
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ComPtr<ID3DBlob>	signature;
		ComPtr<ID3DBlob>	error;
		if (FAILED(D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) return FALSE;
		if (FAILED(g_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&g_rootSignature)))) return FALSE;
	}
	// �V�F�[�_�[���R���p�C��
	{
		ComPtr<ID3DBlob>	vertexShader;
		ComPtr<ID3DBlob>	pixelShader;

#if defined(_DEBUG)
		// �O���t�B�b�N�f�o�b�O�c�[���ɂ��V�F�[�_�[�̃f�o�b�O��L���ɂ���
		UINT	compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT	compileFlags = 0;
#endif

		if (FAILED(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr))) return FALSE;
		if (FAILED(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr))) return FALSE;

		// ���_���̓��C�A�E�g���`
		D3D12_INPUT_ELEMENT_DESC	inputElementDescs[] = {
			{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
		};

		// �O���t�B�b�N�X�p�C�v���C���̏�ԃI�u�W�F�N�g���쐬
		D3D12_GRAPHICS_PIPELINE_STATE_DESC	psoDesc = {};
		psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature = g_rootSignature.Get();
		{
			D3D12_SHADER_BYTECODE	shaderBytecode;
			shaderBytecode.pShaderBytecode = vertexShader->GetBufferPointer();
			shaderBytecode.BytecodeLength = vertexShader->GetBufferSize();
			psoDesc.VS = shaderBytecode;
		}
		{
			D3D12_SHADER_BYTECODE	shaderBytecode;
			shaderBytecode.pShaderBytecode = pixelShader->GetBufferPointer();
			shaderBytecode.BytecodeLength = pixelShader->GetBufferSize();
			psoDesc.PS = shaderBytecode;
		}
		{
			D3D12_RASTERIZER_DESC	rasterizerDesc = {};
			rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
			rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
			rasterizerDesc.FrontCounterClockwise = FALSE;
			rasterizerDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
			rasterizerDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
			rasterizerDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
			rasterizerDesc.DepthClipEnable = TRUE;
			rasterizerDesc.MultisampleEnable = FALSE;
			rasterizerDesc.AntialiasedLineEnable = FALSE;
			rasterizerDesc.ForcedSampleCount = 0;
			rasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
			psoDesc.RasterizerState = rasterizerDesc;
		}
		{
			D3D12_BLEND_DESC	blendDesc = {};
			blendDesc.AlphaToCoverageEnable = FALSE;
			blendDesc.IndependentBlendEnable = FALSE;
			for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
				blendDesc.RenderTarget[i].BlendEnable = FALSE;
				blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
				blendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
				blendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
				blendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
				blendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
				blendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
				blendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
				blendDesc.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
				blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			}
			psoDesc.BlendState = blendDesc;
		}
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.DepthStencilState.StencilEnable = FALSE;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.SampleDesc.Count = 1;
		if (FAILED(g_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_pipelineState)))) return FALSE;
	}

	// �R�}���h���X�g���쐬
	if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator.Get(), g_pipelineState.Get(), IID_PPV_ARGS(&g_commandList)))) return FALSE;
	if (FAILED(g_commandList->Close())) return FALSE;
	// ���_�o�b�t�@���쐬
	{
		// �O�p�`�̃W�I���g�����`
		Vertex	triangleVertices[] = {
			{{  0.0f,  0.25f * g_aspectRatio, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
			{{ 0.25f, -0.25f * g_aspectRatio, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
			{{-0.25f, -0.25f * g_aspectRatio, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}}
		};

		const UINT	vertexBufferSize = sizeof(triangleVertices);

		{
			D3D12_HEAP_PROPERTIES	heapProperties = {};
			heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
			heapProperties.CreationNodeMask = 1;
			heapProperties.VisibleNodeMask = 1;

			D3D12_RESOURCE_DESC	resourceDesc = {};
			resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			resourceDesc.Alignment = 0;
			resourceDesc.Width = vertexBufferSize;
			resourceDesc.Height = 1;
			resourceDesc.DepthOrArraySize = 1;
			resourceDesc.MipLevels = 1;
			resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
			resourceDesc.SampleDesc.Count = 1;
			resourceDesc.SampleDesc.Quality = 0;
			resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

			if (FAILED(g_device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_vertexBuffer)))) return FALSE;
		}

		// ���_�o�b�t�@�ɒ��_�f�[�^���R�s�[
		UINT8* pVertexDataBegin;
		D3D12_RANGE	readRange = { 0, 0 };		// CPU����o�b�t�@��ǂݍ��܂Ȃ��ݒ�
		if (FAILED(g_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)))) return FALSE;
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		g_vertexBuffer->Unmap(0, nullptr);

		// ���_�o�b�t�@�r���[��������
		g_vertexBufferView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
		g_vertexBufferView.StrideInBytes = sizeof(Vertex);
		g_vertexBufferView.SizeInBytes = vertexBufferSize;
	}

	// �����I�u�W�F�N�g���쐬���ă��\�[�X��GPU�ɃA�b�v���[�h�����܂őҋ@
	{
		if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return FALSE;
		g_fenceValue = 1;

		// �t���[�������Ɏg�p����C�x���g�n���h�����쐬
		g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (g_fenceEvent == nullptr) {
			if (FAILED(HRESULT_FROM_WIN32(GetLastError()))) return FALSE;
		}

		if (!WaitForPreviousFrame()) return FALSE;
	}

	return TRUE;
}
// �`��
BOOL Draw()
{
	if (FAILED(g_commandAllocator->Reset())) return FALSE;
	if (FAILED(g_commandList->Reset(g_commandAllocator.Get(), g_pipelineState.Get()))) return FALSE;

	g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
	g_commandList->RSSetViewports(1, &g_viewport);
	g_commandList->RSSetScissorRects(1, &g_scissorRect);

	// �o�b�N�o�b�t�@�������_�����O�^�[�Q�b�g�Ƃ��Ďg�p
	{
		D3D12_RESOURCE_BARRIER	resourceBarrier = {};
		resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resourceBarrier.Transition.pResource = g_renderTargets[g_frameIndex].Get();
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_commandList->ResourceBarrier(1, &resourceBarrier);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE	rtvHandle = {};
	rtvHandle.ptr = g_rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr + g_frameIndex * g_rtvDescriptorSize;
	g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

	// �o�b�N�o�b�t�@�ɕ`��
	const float	clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
	g_commandList->DrawInstanced(3, 1, 0, 0);

	// �o�b�N�o�b�t�@��\��
	{
		D3D12_RESOURCE_BARRIER	resourceBarrier = {};
		resourceBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		resourceBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		resourceBarrier.Transition.pResource = g_renderTargets[g_frameIndex].Get();
		resourceBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		resourceBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		resourceBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		g_commandList->ResourceBarrier(1, &resourceBarrier);
	}

	if (FAILED(g_commandList->Close())) return FALSE;


	// �R�}���h���X�g�����s
	ID3D12CommandList* ppCommandLists[] = { g_commandList.Get() };
	g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// �t���[�����ŏI�o��
	if (FAILED(g_swapChain->Present(1, 0))) return FALSE;

	return WaitForPreviousFrame();
}
BOOL WaitForPreviousFrame()
{
	const UINT64	fence = g_fenceValue;
	if (FAILED(g_commandQueue->Signal(g_fence.Get(), fence))) return FALSE;
	g_fenceValue++;

	// �O�̃t���[�����I������܂őҋ@
	if (g_fence->GetCompletedValue() < fence) {
		if (FAILED(g_fence->SetEventOnCompletion(fence, g_fenceEvent))) return FALSE;
		WaitForSingleObject(g_fenceEvent, INFINITE);
	}

	g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

	return TRUE;
}