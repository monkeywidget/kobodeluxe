/*(GPLv2)
------------------------------------------------------------------------------
	kobo_sdl12.h - SDL 1.2 video API over SDL 2
------------------------------------------------------------------------------
 * Copyright 2026 monkeywidget
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

/*
 * This replaces graphics/glSDL.{c,h}, which accelerated SDL 1.2 blitting with
 * immediate-mode desktop OpenGL (glBegin/glVertex2i/glEnd). Immediate mode
 * does not exist in OpenGL ES in any version, so glSDL cannot run on the
 * Android target at all and is not ported.
 *
 * glSDL presented the SDL 1.2 API to the rest of the game via a block of
 * #defines. That is the property worth keeping: the game renders into a
 * software surface and blits dirty rectangles to a "screen" surface, and it
 * never needs to know what carries those pixels to the display. So this file
 * presents the same 1.2-shaped API, and the ~140 files above it compile
 * unchanged.
 *
 * Underneath, the screen surface is plain system memory. SDL_Flip() uploads
 * it to one streaming texture and presents it through SDL_Render, which gets
 * a GLES2 backend on Android for free and Metal on macOS.
 *
 * Only the calls whose SDL 2 signature or semantics actually differ are
 * wrapped. SDL_BlitSurface(), SDL_FillRect(), SDL_LockSurface(),
 * SDL_ConvertSurface(), SDL_CreateRGBSurfaceFrom(), SDL_LoadBMP(),
 * SDL_SaveBMP() and IMG_Load() are unchanged in SDL 2 and are left alone, so
 * that reading this code tells you the whole of what the shim does.
 */

#ifndef	_KOBO_SDL12_H_
#define	_KOBO_SDL12_H_

#include "SDL.h"
#include "SDL_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------
	SDL 1.2 type names that SDL 2 renamed
----------------------------------------------------------*/
/*
 * SDL 1.2's SDLKey/SDLMod enums became SDL_Keycode/SDL_Keymod. Keeping the old
 * names available costs nothing and means the game-logic headers that carry
 * these in function signatures -- manage.h among them, which is one of the
 * files still under Akira Higuchi's copyright -- do not have to be edited at
 * all. The underlying type is Sint32 either way, so this is a spelling, not a
 * behavioural difference.
 */
typedef SDL_Keycode	SDLKey;
typedef SDL_Keymod	SDLMod;

/*----------------------------------------------------------
	SDL 1.2 key symbols that SDL 2 respelled
----------------------------------------------------------*/
/*
 * Renames only -- same physical keys, same values. Absorbing them here keeps
 * gamectl.cpp's keyboard tables, which are part of the original XKobo control
 * code, exactly as they were. Anything that actually changed *behaviour*
 * (key repeat, UNICODE translation, window focus events) is dealt with at the
 * call site instead, because hiding that kind of change behind a macro would
 * be misleading.
 */
#define	SDLK_KP0		SDLK_KP_0
#define	SDLK_KP1		SDLK_KP_1
#define	SDLK_KP2		SDLK_KP_2
#define	SDLK_KP3		SDLK_KP_3
#define	SDLK_KP4		SDLK_KP_4
#define	SDLK_KP5		SDLK_KP_5
#define	SDLK_KP6		SDLK_KP_6
#define	SDLK_KP7		SDLK_KP_7
#define	SDLK_KP8		SDLK_KP_8
#define	SDLK_KP9		SDLK_KP_9
#define	SDLK_PRINT		SDLK_PRINTSCREEN

/* Same physical key -- Command on a Mac, Windows key on a PC. */
#define	KMOD_META		KMOD_GUI

/*----------------------------------------------------------
	SDL 1.2 flags that SDL 2 dropped
----------------------------------------------------------*/
/*
 * SDL 2 still defines SDL_SWSURFACE (0) and SDL_RLEACCEL, so neither is
 * redefined here. Note that SDL 2's SDL_RLEACCEL is 0x00000002 and shares the
 * low nibble with the live surface flags SDL_PREALLOC, SDL_DONTFREE and
 * SDL_SIMD_ALIGNED -- so the two values below deliberately sit well clear of
 * it, at the values SDL 1.2 itself used.
 *
 * These are only ever passed *to* the shim. Do not test them against a live
 * surface->flags: SDL 2 surfaces do not carry colourkey or per-surface alpha
 * state in that field. Use SDL_GetColorKey() and SDL_GetSurfaceBlendMode().
 */
#ifndef	SDL_SRCCOLORKEY
#define	SDL_SRCCOLORKEY		0x00001000
#endif
#ifndef	SDL_SRCALPHA
#define	SDL_SRCALPHA		0x00010000
#endif

/* Video flags. Only the ones this tree actually passes are defined. */
#ifndef	SDL_HWSURFACE
#define	SDL_HWSURFACE		0x00000001
#endif
#ifndef	SDL_DOUBLEBUF
#define	SDL_DOUBLEBUF		0x40000000
#endif
#ifndef	SDL_FULLSCREEN
#define	SDL_FULLSCREEN		0x80000000
#endif

/*----------------------------------------------------------
	The wrapped calls
----------------------------------------------------------*/

/*
 * Open a window, renderer and streaming texture, and return the software
 * surface the game renders into. 'bpp' is accepted for source compatibility
 * and ignored; the surface is always 32-bit ARGB8888, which is what the
 * texture upload wants and what every modern display is anyway.
 *
 * SDL_FULLSCREEN maps to SDL_WINDOW_FULLSCREEN_DESKTOP (borderless at the
 * desktop resolution) rather than a real mode switch, which is correct for
 * both a Mac and a TV. The renderer is given a logical size of w x h, so the
 * game's own geometry is scaled and letterboxed to whatever the display is
 * without the game knowing.
 *
 * Returns NULL on failure, like SDL 1.2.
 */
SDL_Surface *kobo12_SetVideoMode(int w, int h, int bpp, Uint32 flags);

/* The surface returned by the last successful kobo12_SetVideoMode(). */
SDL_Surface *kobo12_GetVideoSurface(void);

/* Upload the screen surface and present it. Returns 0. */
int kobo12_Flip(SDL_Surface *screen);

/*
 * SDL 1.2 callers used these to present only what changed. The upload is a
 * single texture write either way, so both simply present. The rectangles are
 * accepted and ignored.
 */
void kobo12_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);
void kobo12_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y,
		Uint32 w, Uint32 h);

/* Convert to the screen's pixel format / to ARGB8888. */
SDL_Surface *kobo12_DisplayFormat(SDL_Surface *surface);
SDL_Surface *kobo12_DisplayFormatAlpha(SDL_Surface *surface);

/*
 * SDL 1.2 per-surface alpha. SDL_SRCALPHA in 'flags' enables blending;
 * its absence disables it. 'alpha' is the surface-wide modulation.
 */
int kobo12_SetAlpha(SDL_Surface *surface, Uint32 flags, Uint8 alpha);

/*
 * SDL 1.2 colour key. The key is enabled only when SDL_SRCCOLORKEY is present
 * in 'flags' -- callers in this tree pass SDL_RLEACCEL alone to *disable* a
 * key (see filters.c), so testing 'flags != 0' would invert those sites and
 * silently corrupt sprite transparency.
 */
int kobo12_SetColorKey(SDL_Surface *surface, Uint32 flags, Uint32 key);

/*
 * SDL 1.2 input grab. SDL 2 made this window-scoped and split the query out
 * into its own call, but the behaviour is otherwise identical, so the old
 * three-way entry point is emulated exactly rather than rewritten at each of
 * its four call sites in states.cpp.
 */
#define	SDL_GRAB_QUERY		(-1)
#define	SDL_GRAB_OFF		0
#define	SDL_GRAB_ON		1
int kobo12_GrabInput(int mode);

/* SDL 1.2 window caption. Straight mapping to SDL_SetWindowTitle(). */
void kobo12_SetCaption(const char *title, const char *icon);

/*
 * The character a keypress stands for, replacing SDL 1.2's
 * SDL_EnableUNICODE() and keysym.unicode, which SDL 2 removed in favour of
 * separate SDL_TEXTINPUT events.
 *
 * Deliberately limited to ASCII letters. The only thing this game ever does
 * with the value is enter a player name, and that field accepts nothing but
 * a-z and A-Z (states.cpp, new_player_t::change) -- so routing the game
 * through SDL_TEXTINPUT would reorder events in the input path to buy
 * characters it discards. Returns 0 for anything else, exactly as an
 * untranslated 1.2 keysym did.
 */
int kobo12_KeysymChar(const SDL_Keysym *keysym);

/*
 * Request vertical retrace synchronisation. Must be called before
 * kobo12_SetVideoMode() to take effect, as SDL 2 fixes vsync when the
 * renderer is created. Returns the previous setting.
 */
int kobo12_VSync(int use);

/* Tear down window, renderer, texture and screen surface. Idempotent. */
void kobo12_VideoQuit(void);

/*
 * Name of the backend SDL actually chose, or "none" before the video mode is
 * set. Worth logging at startup: SDL 2 picks Metal on macOS and GLES2 on
 * Android, so a rendering bug seen on the TV box may well not reproduce on
 * the development Mac, and this line is what says so.
 */
const char *kobo12_RendererName(void);

/*
 * The display window, or NULL before the video mode is set. SDL 1.2's global
 * calls -- input grab, window caption -- became window-scoped in SDL 2, so
 * their call sites need a handle to pass.
 */
SDL_Window *kobo12_GetWindow(void);

/*----------------------------------------------------------
	Present the 1.2 API to the rest of the game
----------------------------------------------------------*/
#ifndef	_KOBO_SDL12_NO_REDEFINES_
#define	SDL_SetVideoMode		kobo12_SetVideoMode
#define	SDL_GetVideoSurface		kobo12_GetVideoSurface
#define	SDL_Flip			kobo12_Flip
#define	SDL_UpdateRects			kobo12_UpdateRects
#define	SDL_UpdateRect			kobo12_UpdateRect
#define	SDL_DisplayFormat		kobo12_DisplayFormat
#define	SDL_DisplayFormatAlpha		kobo12_DisplayFormatAlpha
#define	SDL_SetAlpha			kobo12_SetAlpha
#undef	SDL_SetColorKey
#define	SDL_SetColorKey			kobo12_SetColorKey
#define	SDL_WM_GrabInput		kobo12_GrabInput
#define	SDL_WM_SetCaption		kobo12_SetCaption
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _KOBO_SDL12_H_ */
