/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file hlsl_shader.h HLSL shader programs. */

#define _hlsl_cbuffer \
R"STR(
	cbuffer UniformConstantBuffer : register(b0)
	{
		float4 sprite;
		float2 screen;
		float zoom;
		bool rgb;
		bool crash;
	};
)STR"

/** HLSL vertex shader that positions a sprite on screen. */
static const char _vertex_shader_sprite_hlsl[] =
_hlsl_cbuffer
R"STR(
	struct vs_out
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD;
	};

	static const float4 vert_array[] = {
		//       x     y    u    v
		float4( 1.0, -1.0, 1.0, 1.0),
		float4(-1.0, -1.0, 0.0, 1.0),
		float4( 1.0,  1.0, 1.0, 0.0),
		float4(-1.0,  1.0, 0.0, 0.0)
	};

	vs_out vs_main(uint id: SV_VertexID)
	{
		float2 size = sprite.zw / screen.xy;
		float2 offset = ((2.0 * sprite.xy + sprite.zw) / screen.xy - 1.0) * float2(1.0, -1.0);

		vs_out output;
		output.texcoord = vert_array[id].zw;
		output.position = float4(vert_array[id].xy * size + offset, 0.0, 1.0);

		return output;
	};
)STR";

/** HLSL pixel shader that reads the fragment colour from a 32bpp texture. */
static const char _frag_shader_direct_hlsl[] =
R"STR(
	uniform Texture2D colour_tex : register(t0);
	uniform SamplerState texture_sampler : register(s0);

	struct vs_out
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD;
	};

	float4 ps_main(vs_out input) : SV_TARGET
	{
		return colour_tex.Sample(texture_sampler, input.texcoord);
	};
)STR";

/** HLSL pixel shader that reads the fragment colour from a 32bpp texture. */
static const char _frag_shader_palette_hlsl[] =
R"STR(
	uniform Texture2D colour_tex : register(t0);
	uniform Texture1D palette : register(t1);
	uniform SamplerState texture_sampler : register(s0);

	struct vs_out
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD;
	};

	float4 ps_main(vs_out input) : SV_TARGET
	{
		float idx = colour_tex.Sample(texture_sampler, input.texcoord).x;
		return palette.Sample(texture_sampler, idx);
	};
)STR";

/** Pixel shader function for remap brightness modulation. */
#define _frag_shader_remap_func_hlsl \
R"STR(
	float max3(float3 v)
	{
		return max(max(v.x, v.y), v.z);
	}

	float3 adj_brightness(float3 colour, float3 brightness)
	{
		float3 adj = colour * (brightness > 0.0 ? brightness / 0.5 : 1.0);
		float3 ob_vec = clamp(adj - 1.0, 0.0, 1.0);
		float ob = (ob_vec.r + ob_vec.g + ob_vec.b) / 2.0;
		return clamp(adj + ob * (1.0 - adj), 0.0, 1.0);
	}
)STR"

/** HLSL pixel shader that performs a palette lookup to read the colour from an 8bpp texture. */
static const char _frag_shader_rgb_mask_blend_hlsl[] =
_hlsl_cbuffer
_frag_shader_remap_func_hlsl
R"STR(
	uniform Texture2D colour_tex : register(t0);
	uniform Texture1D palette : register(t1);
	uniform Texture2D remap_tex : register(t2);
	uniform SamplerState texture_sampler : register(s0);

	struct vs_out
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD;
	};

	float4 ps_main(vs_out input) : SV_TARGET
	{
		float idx = remap_tex.SampleLevel(texture_sampler, input.texcoord, zoom).r;
		float4 remap_col = palette.Sample(texture_sampler, idx);
		float4 rgb_col = colour_tex.SampleLevel(texture_sampler, input.texcoord, zoom);

		float4 output;
		output.a = rgb ? rgb_col.a : remap_col.a;
		output.rgb = idx > 0.0 ? adj_brightness(remap_col.rgb, max3(rgb_col.rgb)) : rgb_col.rgb;

		return output;
	};
)STR";

/** HLSL pixel shader that performs a palette lookup to read the colour from a sprite texture. */
static const char _frag_shader_sprite_blend_hlsl[] =
_hlsl_cbuffer
_frag_shader_remap_func_hlsl
R"STR(
	uniform Texture2D colour_tex : register(t0);
	uniform Texture1D palette : register(t1);
	uniform Texture2D remap_tex : register(t2);
	uniform Texture1D pal : register(t3);
	uniform SamplerState texture_sampler : register(s0);

	struct vs_out
	{
		float4 position : SV_POSITION;
		float2 texcoord : TEXCOORD;
	};

	float4 ps_main(vs_out input) : SV_TARGET
	{
		float idx = remap_tex.SampleLevel(texture_sampler, input.texcoord, zoom).r;
		float r = pal.Sample(texture_sampler, idx).r;
		float4 remap_col = palette.Sample(texture_sampler, r);
		float4 rgb_col = colour_tex.SampleLevel(texture_sampler, input.texcoord, zoom);

		if (crash && idx == 0.0)
			rgb_col.rgb = float2(dot(rgb_col.rgb, float3(0.199325561523, 0.391342163085, 0.076004028320)), 0.0).rrr;

		float4 output;
		output.a = rgb && (r > 0.0 || idx == 0.0) ? rgb_col.a : remap_col.a;
		output.rgb = idx > 0.0 ? adj_brightness(remap_col.rgb, max3(rgb_col.rgb)) : rgb_col.rgb;

		return output;
	};
)STR";
