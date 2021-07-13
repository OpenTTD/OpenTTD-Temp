/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file d3d11.cpp D3D11 video driver support. */

#include "../stdafx.h"

#include "d3d11.h"

#include "../core/geometry_func.hpp"
#include "../core/mem_func.hpp"
#include "../core/math_func.hpp"
#include "../core/mem_func.hpp"
#include "../gfx_func.h"
#include "../debug.h"
#include "../blitter/factory.hpp"
#include "../zoom_func.h"
#include "../table/hlsl_shader.h"

#include <d3dcompiler.h>

D3D11Backend* D3D11Backend::instance = nullptr;

struct HlslConstantBuffer {
	FLOAT sprite[4];
	FLOAT screen[2];
	FLOAT zoom;
	BOOL rgb;
	BOOL crash;
	FLOAT pad[3];
};

static HMODULE d3d11_module;
static HMODULE d3dcompiler_module;

static PFN_D3D11_CREATE_DEVICE _D3D11CreateDevice;
static pD3DCompile _D3DCompile;

static char* CompileShader(const char *source, const char *func, const char *profile, ComPtr<ID3DBlob> &code)
{
	ComPtr<ID3DBlob> error;

	HRESULT result = _D3DCompile(source, strlen(source),
		nullptr, nullptr, nullptr,
		func, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &error);

	if (FAILED(result)) {
		if (error) {
			return (char*)error->GetBufferPointer();
		}

		return "Shader compile error";
	}

	return nullptr;
}

static const char* CreateVertexShader(ID3D11Device *device, const char* source, ComPtr<ID3D11VertexShader> &shader)
{
	ComPtr<ID3DBlob> code;

	const char *msg = CompileShader(source, "vs_main", "vs_4_0", code);
	if (msg != nullptr)
		return msg;

	HRESULT result = device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);

	if (FAILED(result)) {
		return "Failed to create vertex shader";
	}

	return nullptr;
}

static const char* CreatePixelShader(ID3D11Device* device, const char* source, ComPtr<ID3D11PixelShader> &shader)
{
	ComPtr<ID3DBlob> code;

	const char* msg = CompileShader(source, "ps_main", "ps_4_0", code);
	if (msg != nullptr)
		return msg;

	HRESULT result = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);

	if (FAILED(result)) {
		return "Failed to create pixel shader";
	}

	return nullptr;
}

/**
 * Create and initialize the singleton back-end class.
 * @return nullptr on success, error message otherwise.
 */
const char* D3D11Backend::Create()
{
	if (D3D11Backend::instance != nullptr) D3D11Backend::Destroy();

	for (int ver = D3D_COMPILER_VERSION; ver >= 40; ver--) {
		std::string dllname = "d3dcompiler_" + std::to_string(ver) + ".dll";
		d3dcompiler_module = LoadLibraryA(dllname.c_str());
		if (d3dcompiler_module != nullptr) break;
	}

	if (d3dcompiler_module == nullptr) return "Failed to load d3dcompiler library";

	_D3DCompile = (pD3DCompile)GetProcAddress(d3dcompiler_module, "D3DCompile");
	if (_D3DCompile == nullptr) return "Failed to import D3DCompile function";

	d3d11_module = LoadLibraryA("d3d11.dll");
	if (d3d11_module == nullptr) return "Failed to load d3d11 library";

	_D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(d3d11_module, "D3D11CreateDevice");
	if (_D3D11CreateDevice == nullptr) return "Failed to import D3D11CreateDevice function";

	D3D11Backend::instance = new D3D11Backend();
	const char *err = D3D11Backend::instance->Init();
	if (err != nullptr) {
		delete D3D11Backend::instance;
		D3D11Backend::instance = nullptr;
	}
	return err;
}

/**
 * Free resources and destroy singleton back-end class.
 */
void D3D11Backend::Destroy()
{
	delete D3D11Backend::instance;
	D3D11Backend::instance = nullptr;

	if (d3d11_module != nullptr) FreeLibrary(d3d11_module);
	if (d3dcompiler_module != nullptr) FreeLibrary(d3dcompiler_module);

	d3d11_module = nullptr;
	d3dcompiler_module = nullptr;

	_D3D11CreateDevice = nullptr;
	_D3DCompile = nullptr;
}

/**
 * Check for the needed D3D11 functionality and allocate all resources.
 * @return Error string or nullptr if successful.
 */
const char* D3D11Backend::Init()
{
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_10_0;
	UINT device_flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;
	//device_flags |= D3D11_CREATE_DEVICE_DEBUG; /* Uncomment to create device with debug layer. */

	HRESULT result = _D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		device_flags, &feature_level, 1, D3D11_SDK_VERSION,
		&this->device, nullptr, &this->device_ctx);

	if (FAILED(result))
		return "Failed to create D3D11 device";

	const char* err;

	err = CreateVertexShader(this->device.Get(), _vertex_shader_sprite_hlsl, this->vertex_shader);
	if (err != nullptr) return err;

	err = CreatePixelShader(this->device.Get(), _frag_shader_direct_hlsl, this->direct_shader);
	if (err != nullptr) return err;

	err = CreatePixelShader(this->device.Get(), _frag_shader_palette_hlsl, this->palette_shader);
	if (err != nullptr) return err;

	err = CreatePixelShader(this->device.Get(), _frag_shader_rgb_mask_blend_hlsl, this->rgb_mask_blend_shader);
	if (err != nullptr) return err;

	err = CreatePixelShader(this->device.Get(), _frag_shader_sprite_blend_hlsl, this->sprite_blend_shader);
	if (err != nullptr) return err;

	D3D11_BUFFER_DESC cbuffer_desc;
	cbuffer_desc.ByteWidth = sizeof(HlslConstantBuffer);
	cbuffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
	cbuffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbuffer_desc.CPUAccessFlags = 0;
	cbuffer_desc.MiscFlags = 0;
	cbuffer_desc.StructureByteStride = 0;

	HlslConstantBuffer constant;
	constant.screen[0] = 1.0f;
	constant.screen[1] = 1.0f;
	constant.sprite[0] = 0.0f;
	constant.sprite[1] = 0.0f;
	constant.sprite[2] = 1.0f;
	constant.sprite[3] = 1.0f;
	constant.zoom = 0.0f;
	constant.rgb = TRUE;
	constant.crash = FALSE;

	D3D11_SUBRESOURCE_DATA data;
	data.pSysMem = &constant;
	data.SysMemPitch = sizeof(constant);
	data.SysMemSlicePitch = 0;

	result = device->CreateBuffer(&cbuffer_desc, &data, &this->constant_buffer);
	if (FAILED(result)) return "Failed to constant buffer";

	D3D11_TEXTURE1D_DESC desc;
	desc.Width = 256;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.MiscFlags = 0;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	result = device->CreateTexture1D(&desc, nullptr, &this->pal_texture);
	if (FAILED(result)) return "Failed to create texture";

	result = device->CreateShaderResourceView(this->pal_texture.Get(), nullptr, &this->pal_texture_srv);
	if (FAILED(result)) return "Failed to resource view";

	D3D11_SAMPLER_DESC sampler_desc;
	memset(&sampler_desc, 0, sizeof(sampler_desc));
	sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;

	result = device->CreateSamplerState(&sampler_desc, &texture_sampler);
	if (FAILED(result)) return "Failed to create sampler state";

	return nullptr;
}

void D3D11Backend::UpdatePalette(const Colour* pal, uint first, uint length)
{
	assert(first + length <= 256);

	D3D11_BOX box;
	box.front = 0;
	box.back = 1;
	box.left = first;
	box.right = first + length;
	box.top = 0;
	box.bottom = 1;

	device_ctx->UpdateSubresource(pal_texture.Get(), 0, &box, pal + first, 256, 0);
}

const char* D3D11Backend::Resize(int w, int h)
{
	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();

	D3D11_TEXTURE2D_DESC desc;
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.MiscFlags = 0;

	if (bpp == 8) {
		desc.Format = DXGI_FORMAT_R8_UNORM;
	}
	else {
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	}

	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.CPUAccessFlags = 0;

	HRESULT result = device->CreateTexture2D(&desc, nullptr, &this->vid_texture);
	if (FAILED(result)) return "Failed to create texture";

	desc.BindFlags = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

	result = device->CreateTexture2D(&desc, nullptr, &this->vid_texture_staging);
	if (FAILED(result)) return "Failed to create texture";

	desc.Format = DXGI_FORMAT_R8_UNORM;

	/* Does this blitter need a separate animation buffer? */
	if (BlitterFactory::GetCurrentBlitter()->NeedsAnimationBuffer()) {
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;

		result = device->CreateTexture2D(&desc, nullptr, &this->anim_texture);
		if (FAILED(result)) return "Failed to create texture";

		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;

		result = device->CreateTexture2D(&desc, nullptr, &this->anim_texture_staging);
		if (FAILED(result)) return "Failed to create texture";
	}
	else {
		/* Allocate dummy texture that always reads as 0 == no remap. */
		desc.Width = 1;
		desc.Height = 1;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.CPUAccessFlags = 0;

		byte dummy = 0;
		D3D11_SUBRESOURCE_DATA data;
		data.pSysMem = &dummy;
		data.SysMemPitch = 1;
		data.SysMemSlicePitch = 0;

		result = device->CreateTexture2D(&desc, &data, &this->anim_texture);
		if (FAILED(result)) return "Failed to create texture";

		this->anim_texture_staging.Reset();
	}

	result = device->CreateShaderResourceView(this->vid_texture.Get(), nullptr, &this->vid_texture_srv);
	if (FAILED(result)) return "Failed to create resource view";

	result = device->CreateShaderResourceView(this->anim_texture.Get(), nullptr, &this->anim_texture_srv);
	if (FAILED(result)) return "Failed to create resource view";

	/* Set new viewport. */
	_screen.height = h;
	_screen.width = w;
	_screen.dst_ptr = nullptr;

	return nullptr;
}

ComPtr<ID3D11Device> D3D11Backend::GetDevice()
{
	return this->device;
}

void D3D11Backend::Paint(ComPtr<ID3D11RenderTargetView> rendertarget)
{
	FLOAT background_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	device_ctx->ClearRenderTargetView(rendertarget.Get(), background_color);
	device_ctx->OMSetRenderTargets(1, rendertarget.GetAddressOf(), nullptr);

	device_ctx->VSSetShader(vertex_shader.Get(), nullptr, 0);
	device_ctx->VSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());
	device_ctx->PSSetConstantBuffers(0, 1, constant_buffer.GetAddressOf());

	ID3D11ShaderResourceView* resources[4] = { nullptr };
	resources[0] = this->vid_texture_srv.Get();
	resources[1] = this->pal_texture_srv.Get();
	if (BlitterFactory::GetCurrentBlitter()->NeedsAnimationBuffer()) {
		resources[2] = this->anim_texture_srv.Get();
		device_ctx->PSSetShader(this->rgb_mask_blend_shader.Get(), nullptr, 0);
	}
	else {
		if (BlitterFactory::GetCurrentBlitter()->GetScreenDepth() == 8) {
			device_ctx->PSSetShader(this->palette_shader.Get(), nullptr, 0);
		}
		else {
			device_ctx->PSSetShader(this->direct_shader.Get(), nullptr, 0);
		}
	}

	device_ctx->PSSetSamplers(0, 1, texture_sampler.GetAddressOf());
	device_ctx->PSSetShaderResources(0, 4, resources);

	D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (float)_screen.width, (float)_screen.height, 0.0f, 1.0f };
	device_ctx->RSSetViewports(1, &viewport);

	device_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	device_ctx->Draw(4, 0);

	device_ctx->ClearState();
}

void* D3D11Backend::GetVideoBuffer()
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	device_ctx->Map(vid_texture_staging.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped);

	int bpp = BlitterFactory::GetCurrentBlitter()->GetScreenDepth();
	_screen.pitch = mapped.RowPitch / bpp * 8;

	return mapped.pData;
}

std::pair<uint8*, int> D3D11Backend::GetAnimBuffer()
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	device_ctx->Map(anim_texture_staging.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped);

	return std::make_pair((uint8_t*)mapped.pData, mapped.RowPitch);
}

void D3D11Backend::ReleaseVideoBuffer(const Rect& update_rect)
{
	device_ctx->Unmap(vid_texture_staging.Get(), 0);

	if (!IsEmptyRect(update_rect)) {
		D3D11_BOX box;
		box.front = 0;
		box.back = 1;
		box.left = update_rect.left;
		box.right = update_rect.right;
		box.top = update_rect.top;
		box.bottom = update_rect.bottom;

		device_ctx->CopySubresourceRegion(vid_texture.Get(), 0, update_rect.left, update_rect.top, 0, vid_texture_staging.Get(), 0, &box);
	}
}

void D3D11Backend::ReleaseAnimBuffer(const Rect& update_rect)
{
	device_ctx->Unmap(anim_texture_staging.Get(), 0);

	if (!IsEmptyRect(update_rect)) {
		D3D11_BOX box;
		box.front = 0;
		box.back = 1;
		box.left = update_rect.left;
		box.right = update_rect.right;
		box.top = update_rect.top;
		box.bottom = update_rect.bottom;

		device_ctx->CopySubresourceRegion(anim_texture.Get(), 0, update_rect.left, update_rect.top, 0, anim_texture_staging.Get(), 0, &box);
	}
}
