/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

 /** @file d3d11.h D3D11 video driver support. */

#ifndef VIDEO_D3D11_H
#define VIDEO_D3D11_H

#include "../core/alloc_type.hpp"
#include "../core/geometry_type.hpp"
#include "../gfx_type.h"
#include "../spriteloader/spriteloader.hpp"
#include "../misc/lrucache.hpp"

// Vista required for WRL header
#undef NTDDI_VERSION
#undef _WIN32_WINNT

#define NTDDI_VERSION    NTDDI_VISTA
#define _WIN32_WINNT     _WIN32_WINNT_VISTA

#include <wrl/client.h>
#include <d3d11.h>

using Microsoft::WRL::ComPtr;

class D3D11Backend {
private:
	static D3D11Backend* instance;

	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> device_ctx;

	ComPtr<ID3D11VertexShader> vertex_shader;
	ComPtr<ID3D11PixelShader> direct_shader;
	ComPtr<ID3D11PixelShader> palette_shader;
	ComPtr<ID3D11PixelShader> rgb_mask_blend_shader;
	ComPtr<ID3D11PixelShader> sprite_blend_shader;

	ComPtr<ID3D11Buffer> constant_buffer;

	ComPtr<ID3D11Texture2D> vid_texture;
	ComPtr<ID3D11Texture2D> vid_texture_staging;
	ComPtr<ID3D11ShaderResourceView> vid_texture_srv;

	ComPtr<ID3D11Texture2D> anim_texture;
	ComPtr<ID3D11Texture2D> anim_texture_staging;
	ComPtr<ID3D11ShaderResourceView> anim_texture_srv;

	ComPtr<ID3D11Texture1D> pal_texture;
	ComPtr<ID3D11ShaderResourceView> pal_texture_srv;

	ComPtr<ID3D11SamplerState> texture_sampler;

public:
	/** Get singleton instance of this class. */
	static inline D3D11Backend* Get()
	{
		return D3D11Backend::instance;
	}
	static const char* Create();
	static void Destroy();

	const char* Init();

	void UpdatePalette(const Colour* pal, uint first, uint length);
	const char* Resize(int w, int h);
	ComPtr<ID3D11Device> GetDevice();
	void Paint(ComPtr<ID3D11RenderTargetView> rendertarget);

	void* GetVideoBuffer();
	std::pair<uint8*, int> GetAnimBuffer();
	void ReleaseVideoBuffer(const Rect& update_rect);
	void ReleaseAnimBuffer(const Rect& update_rect);
};

#endif
