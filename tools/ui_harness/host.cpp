// Tiny Direct3D 11 host for looking at the Sherbet overlay without the game.
// Shows a still picture as the "game" and presents at vsync. ReShade loads as dxgi.dll next to this exe.
//   host.exe <width> <height> <picture or -> <window x>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
	if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int)
{
	int argc = 0;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	const int W = argc > 1 ? _wtoi(argv[1]) : 1920, H = argc > 2 ? _wtoi(argv[2]) : 1080;
	const wchar_t *picture = argc > 3 && argv[3][0] != L'-' ? argv[3] : nullptr;
	const int X = argc > 4 ? _wtoi(argv[4]) : -6000;

	WNDCLASSW wc{};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = inst;
	wc.lpszClassName = L"SherbetUiHost";
	RegisterClassW(&wc);
	HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"Sherbet UI host", WS_POPUP, X, 0, W, H, nullptr, nullptr, inst, nullptr);
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);

	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 1;
	sd.BufferDesc.Width = W;
	sd.BufferDesc.Height = H;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	IDXGISwapChain *swapchain = nullptr;
	ID3D11Device *device = nullptr;
	ID3D11DeviceContext *context = nullptr;
	if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &swapchain, &device, nullptr, &context)))
		return 1;

	ID3D11Texture2D *backbuffer = nullptr;
	swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backbuffer));
	ID3D11RenderTargetView *rtv = nullptr;
	device->CreateRenderTargetView(backbuffer, nullptr, &rtv);

	// The "game": a still picture stretched to the window (plain bilinear on the CPU, once).
	ID3D11Texture2D *game = nullptr;
	if (picture != nullptr)
	{
		FILE *file = nullptr;
		_wfopen_s(&file, picture, L"rb");
		int iw = 0, ih = 0, comp = 0;
		unsigned char *src = file != nullptr ? stbi_load_from_file(file, &iw, &ih, &comp, 4) : nullptr;
		if (file != nullptr) fclose(file);
		if (src != nullptr)
		{
			std::vector<unsigned char> dst(static_cast<size_t>(W) * H * 4);
			for (int y = 0; y < H; ++y)
			{
				const float fy = (y + 0.5f) * ih / H - 0.5f;
				const int y0 = fy < 0 ? 0 : static_cast<int>(fy), y1 = y0 + 1 < ih ? y0 + 1 : ih - 1;
				const float ty = fy < 0 ? 0 : fy - y0;
				for (int x = 0; x < W; ++x)
				{
					const float fx = (x + 0.5f) * iw / W - 0.5f;
					const int x0 = fx < 0 ? 0 : static_cast<int>(fx), x1 = x0 + 1 < iw ? x0 + 1 : iw - 1;
					const float tx = fx < 0 ? 0 : fx - x0;
					for (int c = 0; c < 4; ++c)
					{
						const float a = src[(y0 * iw + x0) * 4 + c] * (1 - tx) + src[(y0 * iw + x1) * 4 + c] * tx;
						const float b = src[(y1 * iw + x0) * 4 + c] * (1 - tx) + src[(y1 * iw + x1) * 4 + c] * tx;
						dst[(static_cast<size_t>(y) * W + x) * 4 + c] = static_cast<unsigned char>(a * (1 - ty) + b * ty + 0.5f);
					}
				}
			}
			stbi_image_free(src);
			D3D11_TEXTURE2D_DESC td{};
			td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_IMMUTABLE;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA init{ dst.data(), static_cast<UINT>(W * 4), 0 };
			device->CreateTexture2D(&td, &init, &game);
		}
	}

	const float clear[4] = { 0.20f, 0.24f, 0.30f, 1.0f };
	MSG msg{};
	while (msg.message != WM_QUIT)
	{
		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
			if (msg.message == WM_QUIT) break;
		}
		if (game != nullptr)
			context->CopyResource(backbuffer, game);
		else
			context->ClearRenderTargetView(rtv, clear);
		context->OMSetRenderTargets(1, &rtv, nullptr);
		swapchain->Present(1, 0);
	}
	return 0;
}
