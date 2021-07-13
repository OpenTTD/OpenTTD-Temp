/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file 40bpp_optimized.cpp Implementation of the optimized 40 bpp blitter. */

#include "../stdafx.h"
#include "../zoom_func.h"
#include "../settings_type.h"
#include "../video/video_driver.hpp"
#include "40bpp_anim.hpp"
#include "common.hpp"

#include "../table/sprites.h"

#include "../safeguards.h"


/** Instantiation of the 40bpp with animation blitter factory. */
static FBlitter_40bppAnim iFBlitter_40bppAnim;

/** Cached black value. */
static const Colour _black_colour(0, 0, 0);

static int TranslateToAnim(const void* video)
{
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();

	int px = (const uint32 *)video - (const uint32 *)_screen.dst_ptr;
	int row = px / _screen.pitch;
	int column = px - row * _screen.pitch;

	return column + row * anim_pitch;
}

void Blitter_40bppAnim::SetPixel(void *video, int x, int y, uint8 colour)
{
	if (_screen_disable_anim) {
		Blitter_32bppOptimized::SetPixel(video, x, y, colour);
	} else {
		*((Colour *)video + x + y * _screen.pitch) = _black_colour;

		int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();
		VideoDriver::GetInstance()->GetAnimBuffer()[TranslateToAnim(video) + x + y * anim_pitch] = colour;
	}
}

void Blitter_40bppAnim::DrawRect(void *video, int width, int height, uint8 colour)
{
	if (_screen_disable_anim) {
		/* This means our output is not to the screen, so we can't be doing any animation stuff, so use our parent DrawRect() */
		Blitter_32bppOptimized::DrawRect(video, width, height, colour);
		return;
	}

	assert(VideoDriver::GetInstance()->GetAnimBuffer() != nullptr);
	uint8 *anim_line = TranslateToAnim(video) + VideoDriver::GetInstance()->GetAnimBuffer();
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();

	do {
		Colour *dst = (Colour *)video;
		uint8 *anim = anim_line;

		for (int i = width; i > 0; i--) {
			*dst = _black_colour;
			*anim = colour;
			dst++;
			anim++;
		}
		video = (uint32 *)video + _screen.pitch;
		anim_line += anim_pitch;
	} while (--height);
}

void Blitter_40bppAnim::DrawLine(void *video, int x, int y, int x2, int y2, int screen_width, int screen_height, uint8 colour, int width, int dash)
{
	if (_screen_disable_anim) {
		/* This means our output is not to the screen, so we can't be doing any animation stuff, so use our parent DrawRect() */
		Blitter_32bppOptimized::DrawLine(video, x, y, x2, y2, screen_width, screen_height, colour, width, dash);
		return;
	}

	assert(VideoDriver::GetInstance()->GetAnimBuffer() != nullptr);
	uint8 *anim = TranslateToAnim(video) + VideoDriver::GetInstance()->GetAnimBuffer();
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();

	this->DrawLineGeneric(x, y, x2, y2, screen_width, screen_height, width, dash, [=](int x, int y) {
		*((Colour *)video + x + y * _screen.pitch) = _black_colour;
		*(anim + x + y * anim_pitch) = colour;
	});
}

/**
 * Draws a sprite to a (screen) buffer. It is templated to allow faster operation.
 *
 * @tparam mode blitter mode
 * @param bp further blitting parameters
 * @param zoom zoom level at which we are drawing
 */
template <BlitterMode mode>
inline void Blitter_40bppAnim::Draw(const Blitter::BlitterParams *bp, ZoomLevel zoom)
{
	const SpriteData *src = (const SpriteData *)bp->sprite;

	/* src_px : each line begins with uint32 n = 'number of bytes in this line',
	 *          then n times is the Colour struct for this line */
	const Colour *src_px = (const Colour *)(src->data + src->offset[zoom][0]);
	/* src_n  : each line begins with uint32 n = 'number of bytes in this line',
	 *          then interleaved stream of 'm' and 'n' channels. 'm' is remap,
	 *          'n' is number of bytes with the same alpha channel class */
	const uint16 *src_n  = (const uint16 *)(src->data + src->offset[zoom][1]);

	/* skip upper lines in src_px and src_n */
	for (uint i = bp->skip_top; i != 0; i--) {
		src_px = (const Colour *)((const byte *)src_px + *(const uint32 *)src_px);
		src_n = (const uint16 *)((const byte *)src_n + *(const uint32 *)src_n);
	}

	/* skip lines in dst */
	Colour *dst = (Colour *)bp->dst + bp->top * bp->pitch + bp->left;
	assert(VideoDriver::GetInstance()->GetAnimBuffer() != nullptr);
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();
	uint8 *anim = VideoDriver::GetInstance()->GetAnimBuffer() + TranslateToAnim(bp->dst) + bp->top * anim_pitch + bp->left;

	/* store so we don't have to access it via bp everytime (compiler assumes pointer aliasing) */
	const byte *remap = bp->remap;

	for (int y = 0; y < bp->height; y++) {
		/* next dst line begins here */
		Colour *dst_ln = dst + bp->pitch;
		uint8 *anim_ln = anim + anim_pitch;

		/* next src line begins here */
		const Colour *src_px_ln = (const Colour *)((const byte *)src_px + *(const uint32 *)src_px);
		src_px++;

		/* next src_n line begins here */
		const uint16 *src_n_ln = (const uint16 *)((const byte *)src_n + *(const uint32 *)src_n);
		src_n += 2;

		/* we will end this line when we reach this point */
		Colour *dst_end = dst + bp->skip_left;

		/* number of pixels with the same alpha channel class */
		uint n;

		while (dst < dst_end) {
			n = *src_n++;

			if (src_px->a == 0) {
				dst += n;
				src_px++;
				src_n++;

				if (dst > dst_end) anim += dst - dst_end;
			} else {
				if (dst + n > dst_end) {
					uint d = dst_end - dst;
					src_px += d;
					src_n += d;

					dst = dst_end - bp->skip_left;
					dst_end = dst + bp->width;

					n = std::min<uint>(n - d, (uint)bp->width);
					goto draw;
				}
				dst += n;
				src_px += n;
				src_n += n;
			}
		}

		dst -= bp->skip_left;
		dst_end -= bp->skip_left;

		dst_end += bp->width;

		while (dst < dst_end) {
			n = std::min<uint>(*src_n++, (uint)(dst_end - dst));

			if (src_px->a == 0) {
				anim += n;
				dst += n;
				src_px++;
				src_n++;
				continue;
			}

			draw:;

			switch (mode) {
				case BM_COLOUR_REMAP:
				case BM_CRASH_REMAP:
					if (src_px->a == 255) {
						do {
							uint8 m = GB(*src_n, 0, 8);
							/* In case the m-channel is zero, only apply the crash remap by darkening the RGB colour. */
							if (m == 0) {
								*dst = mode == BM_CRASH_REMAP ? this->MakeDark(*src_px) : *src_px;
								*anim = 0;
							} else {
								uint r = remap[m];
								if (r != 0) {
									*dst = src_px->data;
									*anim = r;
								}
							}
							anim++;
							dst++;
							src_px++;
							src_n++;
						} while (--n != 0);
					} else {
						do {
							uint8 m = GB(*src_n, 0, 8);
							Colour b = this->RealizeBlendedColour(*anim, *dst);
							if (m == 0) {
								Colour c = mode == BM_CRASH_REMAP ? this->MakeDark(*src_px) : *src_px;
								*dst = this->ComposeColourRGBANoCheck(c.r, c.g, c.b, src_px->a, b);
								*anim = 0;
							} else {
								uint r = remap[m];
								if (r != 0) {
									*dst = this->ComposeColourPANoCheck(this->LookupColourInPalette(r), src_px->a, b);
									*anim = 0; // Animation colours don't work with alpha-blending.
								}
							}
							anim++;
							dst++;
							src_px++;
							src_n++;
						} while (--n != 0);
					}
					break;

				case BM_BLACK_REMAP:
					do {
						*anim++ = 0;
						*dst++ = _black_colour;
						src_px++;
						src_n++;
					} while (--n != 0);
					break;

				case BM_TRANSPARENT:
					/* TODO -- We make an assumption here that the remap in fact is transparency, not some colour.
					 *  This is never a problem with the code we produce, but newgrfs can make it fail... or at least:
					 *  we produce a result the newgrf maker didn't expect ;) */

					/* Make the current colour a bit more black, so it looks like this image is transparent */
					src_n += n;
					if (src_px->a == 255) {
						src_px += n;
						do {
							/* If the anim buffer contains a color value, the image composition will
							 * only look at the RGB brightness value. As such, we can simply darken the
							 * RGB value to darken the anim color. */
							Colour b = *anim != 0 ? Colour(this->GetColourBrightness(*dst), 0, 0) : *dst;
							*dst = this->MakeTransparent(b, 3, 4);
							anim++;
							dst++;
						} while (--n != 0);
					} else {
						do {
							Colour b = this->RealizeBlendedColour(*anim, *dst);
							*dst = this->MakeTransparent(b, (256 * 4 - src_px->a), 256 * 4);
							*anim = 0; // Animation colours don't work with alpha-blending.
							anim++;
							dst++;
							src_px++;
						} while (--n != 0);
					}
					break;

				default:
					if (src_px->a == 255) {
						do {
							*anim++ = GB(*src_n, 0, 8);
							*dst++ = src_px->data;
							src_px++;
							src_n++;
						} while (--n != 0);
						break;
					} else {
						do {
							uint8 m = GB(*src_n, 0, 8);
							Colour b = this->RealizeBlendedColour(*anim, *dst);

							if (m == 0) {
								*dst = this->ComposeColourRGBANoCheck(src_px->r, src_px->g, src_px->b, src_px->a, b);
								*anim = 0;
							} else {
								*dst = this->ComposeColourPANoCheck(this->LookupColourInPalette(m), src_px->a, b);
								*anim = m;
							}

							anim++;
							dst++;
							src_px++;
							src_n++;
						} while (--n != 0);
					}
			}
		}

		dst = dst_ln;
		anim = anim_ln;
		src_px = src_px_ln;
		src_n  = src_n_ln;
	}
}

/**
 * Draws a sprite to a (screen) buffer. Calls adequate templated function.
 *
 * @param bp further blitting parameters
 * @param mode blitter mode
 * @param zoom zoom level at which we are drawing
 */
void Blitter_40bppAnim::Draw(Blitter::BlitterParams *bp, BlitterMode mode, ZoomLevel zoom)
{
	assert(_screen.dst_ptr != nullptr);

	if (_screen_disable_anim || VideoDriver::GetInstance()->GetAnimBuffer() == nullptr) {
		/* This means our output is not to the screen, so we can't be doing any animation stuff, so use our parent Draw() */
		Blitter_32bppOptimized::Draw<true>(bp, mode, zoom);
		return;
	}

	switch (mode) {
		default: NOT_REACHED();
		case BM_NORMAL:       Draw<BM_NORMAL>      (bp, zoom); return;
		case BM_COLOUR_REMAP: Draw<BM_COLOUR_REMAP>(bp, zoom); return;
		case BM_TRANSPARENT:  Draw<BM_TRANSPARENT> (bp, zoom); return;
		case BM_CRASH_REMAP:  Draw<BM_CRASH_REMAP> (bp, zoom); return;
		case BM_BLACK_REMAP:  Draw<BM_BLACK_REMAP> (bp, zoom); return;
	}
}

void Blitter_40bppAnim::DrawColourMappingRect(void *dst, int width, int height, PaletteID pal)
{
	if (_screen_disable_anim) {
		/* This means our output is not to the screen, so we can't be doing any animation stuff, so use our parent DrawColourMappingRect() */
		Blitter_32bppOptimized::DrawColourMappingRect(dst, width, height, pal);
		return;
	}

	Colour *udst = (Colour *)dst;
	uint8* anim = VideoDriver::GetInstance()->GetAnimBuffer() + TranslateToAnim(dst);
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();

	if (pal == PALETTE_TO_TRANSPARENT) {
		/* If the anim buffer contains a color value, the image composition will
		 * only look at the RGB brightness value. As such, we can simply darken the
		 * RGB value to darken the anim color. */
		do {
			for (int i = 0; i != width; i++) {
				Colour b = *anim != 0 ? Colour(this->GetColourBrightness(*udst), 0, 0) : *udst;
				*udst = MakeTransparent(b, 154);
				udst++;
				anim++;
			}
			udst = udst - width + _screen.pitch;
			anim = anim - width + anim_pitch;
		} while (--height);
	} else if (pal == PALETTE_NEWSPAPER) {
		const uint8 *remap = GetNonSprite(pal, ST_RECOLOUR) + 1;
		do {
			for (int i = 0; i != width; i++) {
				if (*anim == 0) *udst = MakeGrey(*udst);
				*anim = remap[*anim];
				udst++;
				anim++;
			}
			udst = udst - width + _screen.pitch;
			anim = anim - width + anim_pitch;
		} while (--height);
	} else {
		const uint8 *remap = GetNonSprite(pal, ST_RECOLOUR) + 1;
		do {
			for (int i = 0; i != width; i++) {
				*anim = remap[*anim];
				anim++;
			}
			anim = anim - width + anim_pitch;
		} while (--height);
	}
}

Sprite *Blitter_40bppAnim::Encode(const SpriteLoader::Sprite *sprite, AllocatorProc *allocator)
{
	return this->EncodeInternal<false>(sprite, allocator);
}


void Blitter_40bppAnim::CopyFromBuffer(void *video, const void *src, int width, int height)
{
	assert(!_screen_disable_anim);
	assert(video >= _screen.dst_ptr && video <= (uint32 *)_screen.dst_ptr + _screen.width + _screen.height * _screen.pitch);
	uint32 *dst = (uint32 *)video;
	const uint32 *usrc = (const uint32 *)src;

	uint8 *anim_buf = VideoDriver::GetInstance()->GetAnimBuffer();
	if (anim_buf == nullptr) return;
	uint8 *anim_line = TranslateToAnim(video) + anim_buf;
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();

	for (; height > 0; height--) {
		memcpy(dst, usrc, width * sizeof(uint32));
		usrc += width;
		dst += _screen.pitch;
		/* Copy back the anim-buffer */
		memcpy(anim_line, usrc, width * sizeof(uint8));
		usrc = (const uint32 *)((const uint8 *)usrc + width);
		anim_line += anim_pitch;
	}
}

void Blitter_40bppAnim::CopyToBuffer(const void *video, void *dst, int width, int height)
{
	assert(!_screen_disable_anim);
	assert(video >= _screen.dst_ptr && video <= (uint32 *)_screen.dst_ptr + _screen.width + _screen.height * _screen.pitch);
	uint32 *udst = (uint32 *)dst;
	const uint32 *src = (const uint32 *)video;

	uint8 *anim_buf = VideoDriver::GetInstance()->GetAnimBuffer();
	if (anim_buf == nullptr) return;
	const uint8 *anim_line = TranslateToAnim(video) + anim_buf;
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();

	for (; height > 0; height--) {
		memcpy(udst, src, width * sizeof(uint32));
		src += _screen.pitch;
		udst += width;
		/* Copy the anim-buffer */
		memcpy(udst, anim_line, width * sizeof(uint8));
		udst = (uint32 *)((uint8 *)udst + width);
		anim_line += anim_pitch;
	}
}

void Blitter_40bppAnim::CopyImageToBuffer(const void *video, void *dst, int width, int height, int dst_pitch)
{
	uint8 *anim_buf = VideoDriver::GetInstance()->GetAnimBuffer();
	if (anim_buf == nullptr) {
		Blitter_32bppOptimized::CopyImageToBuffer(video, dst, width, height, dst_pitch);
		return;
	}

	uint32 *udst = (uint32 *)dst;
	const uint32 *src = (const uint32 *)video;
	const uint8 *anim_line = TranslateToAnim(video) + anim_buf;
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();

	for (; height > 0; height--) {
		for (int x = 0; x < width; x++) {
			udst[x] = this->RealizeBlendedColour(anim_line[x], src[x]).data;
		}
		src += _screen.pitch;
		anim_line += anim_pitch;
		udst += dst_pitch;
	}
}

void Blitter_40bppAnim::ScrollBuffer(void *video, int &left, int &top, int &width, int &height, int scroll_x, int scroll_y)
{
	assert(!_screen_disable_anim);
	assert(video >= _screen.dst_ptr && video <= (uint32 *)_screen.dst_ptr + _screen.width + _screen.height * _screen.pitch);
	uint8 *anim_buf = VideoDriver::GetInstance()->GetAnimBuffer();
	int anim_pitch = VideoDriver::GetInstance()->GetAnimBufferPitch();
	uint8 *dst, *src;

	/* We need to scroll the anim-buffer too */
	if (scroll_y > 0) {
		dst = anim_buf + left + (top + height - 1) * anim_pitch;
		src = dst - scroll_y * anim_pitch;

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
		} else {
			src -= scroll_x;
		}

		uint tw = width + (scroll_x >= 0 ? -scroll_x : scroll_x);
		uint th = height - scroll_y;
		for (; th > 0; th--) {
			memcpy(dst, src, tw * sizeof(uint8));
			src -= anim_pitch;
			dst -= anim_pitch;
		}
	} else {
		/* Calculate pointers */
		dst = anim_buf + left + top * anim_pitch;
		src = dst - scroll_y * anim_pitch;

		/* Adjust left & width */
		if (scroll_x >= 0) {
			dst += scroll_x;
		} else {
			src -= scroll_x;
		}

		/* the y-displacement may be 0 therefore we have to use memmove,
		 * because source and destination may overlap */
		uint tw = width + (scroll_x >= 0 ? -scroll_x : scroll_x);
		uint th = height + scroll_y;
		for (; th > 0; th--) {
			memmove(dst, src, tw * sizeof(uint8));
			src += anim_pitch;
			dst += anim_pitch;
		}
	}

	Blitter_32bppBase::ScrollBuffer(video, left, top, width, height, scroll_x, scroll_y);
}

int Blitter_40bppAnim::BufferSize(int width, int height)
{
	return width * height * (sizeof(uint32) + sizeof(uint8));
}

Blitter::PaletteAnimation Blitter_40bppAnim::UsePaletteAnimation()
{
	return Blitter::PALETTE_ANIMATION_VIDEO_BACKEND;
}

bool Blitter_40bppAnim::NeedsAnimationBuffer()
{
	return true;
}
