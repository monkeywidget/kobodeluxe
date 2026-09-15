/*(GPLv2)
------------------------------------------------------------------------------
	kobo_sdl12.c - SDL 1.2 video API over SDL 2
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

/* We call the real SDL 2 functions in here, not our own wrappers. */
#define	_KOBO_SDL12_NO_REDEFINES_
#include "kobo_sdl12.h"
#include "kobolog.h"

/*
 * The whole of the shim's state. There is exactly one display, which is what
 * SDL 1.2 assumed and what this game wants.
 */
static SDL_Window	*kobo12_window = NULL;
static SDL_Renderer	*kobo12_renderer = NULL;
static SDL_Texture	*kobo12_texture = NULL;
static SDL_Surface	*kobo12_screen = NULL;
static int		kobo12_vsync = 0;


void kobo12_VideoQuit(void)
{
	if(kobo12_screen)
	{
		SDL_FreeSurface(kobo12_screen);
		kobo12_screen = NULL;
	}
	if(kobo12_texture)
	{
		SDL_DestroyTexture(kobo12_texture);
		kobo12_texture = NULL;
	}
	if(kobo12_renderer)
	{
		SDL_DestroyRenderer(kobo12_renderer);
		kobo12_renderer = NULL;
	}
	if(kobo12_window)
	{
		SDL_DestroyWindow(kobo12_window);
		kobo12_window = NULL;
	}
}


int kobo12_VSync(int use)
{
	int was = kobo12_vsync;
	if(use >= 0)
		kobo12_vsync = use;
	return was;
}


SDL_Window *kobo12_GetWindow(void)
{
	return kobo12_window;
}


const char *kobo12_RendererName(void)
{
	SDL_RendererInfo info;
	if(!kobo12_renderer)
		return "none";
	if(SDL_GetRendererInfo(kobo12_renderer, &info) < 0)
		return "unknown";
	return info.name;
}


SDL_Surface *kobo12_SetVideoMode(int w, int h, int bpp, Uint32 flags)
{
	Uint32 winflags = SDL_WINDOW_SHOWN;
	Uint32 rndflags = SDL_RENDERER_ACCELERATED;

	/* A second call is a mode change: start from nothing. */
	kobo12_VideoQuit();

	(void)bpp;	/* 1.2 compatibility; we always render 32-bit ARGB */

	if(flags & SDL_FULLSCREEN)
		winflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

	if(kobo12_vsync)
		rndflags |= SDL_RENDERER_PRESENTVSYNC;

	/*
	 * Nearest-neighbour scaling. This is SDL's default, but the game is
	 * 1996 pixel art being scaled to displays it was never drawn for, so
	 * it is worth stating rather than inheriting.
	 */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

	kobo12_window = SDL_CreateWindow("Kobo Deluxe",
			SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
			w, h, winflags);
	if(!kobo12_window)
	{
		log_printf(ELOG, "kobo_sdl12: could not create window: %s\n",
				SDL_GetError());
		return NULL;
	}

	kobo12_renderer = SDL_CreateRenderer(kobo12_window, -1, rndflags);
	if(!kobo12_renderer)
	{
		/*
		 * No accelerated renderer. Software still plays, so warn and
		 * continue rather than failing to start.
		 */
		log_printf(WLOG, "kobo_sdl12: no accelerated renderer (%s);"
				" falling back to software\n", SDL_GetError());
		kobo12_renderer = SDL_CreateRenderer(kobo12_window, -1,
				SDL_RENDERER_SOFTWARE);
	}
	if(!kobo12_renderer)
	{
		log_printf(ELOG, "kobo_sdl12: could not create renderer: %s\n",
				SDL_GetError());
		kobo12_VideoQuit();
		return NULL;
	}

	/*
	 * Scale the game's own geometry to whatever the display is, adding
	 * letterbox bars as needed. Kobo Deluxe inherits XKobo's 4:3 layout,
	 * so on a 16:9 TV this pillarboxes rather than distorting the
	 * playfield -- which keeps the play area the shape the game was
	 * designed and balanced for.
	 */
	SDL_RenderSetLogicalSize(kobo12_renderer, w, h);

	kobo12_texture = SDL_CreateTexture(kobo12_renderer,
			SDL_PIXELFORMAT_ARGB8888,
			SDL_TEXTUREACCESS_STREAMING, w, h);
	if(!kobo12_texture)
	{
		log_printf(ELOG, "kobo_sdl12: could not create texture: %s\n",
				SDL_GetError());
		kobo12_VideoQuit();
		return NULL;
	}

	kobo12_screen = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
			SDL_PIXELFORMAT_ARGB8888);
	if(!kobo12_screen)
	{
		log_printf(ELOG, "kobo_sdl12: could not create screen"
				" surface: %s\n", SDL_GetError());
		kobo12_VideoQuit();
		return NULL;
	}

	/*
	 * Report back the buffering the caller asked for. gfxengine.cpp tests
	 * this bit on the returned surface and warns that it could not get a
	 * double buffered display otherwise -- which would be misleading,
	 * because presentation really is double buffered: the game draws into
	 * this surface while SDL_RenderPresent() shows the previous upload.
	 * SDL 2's own surface flags live in the low nibble, well clear of this.
	 */
	if(flags & SDL_DOUBLEBUF)
		kobo12_screen->flags |= SDL_DOUBLEBUF;

	log_printf(VLOG, "kobo_sdl12: %dx%d, vsync %s, renderer \"%s\"\n",
			w, h, kobo12_vsync ? "on" : "off",
			kobo12_RendererName());

	return kobo12_screen;
}


SDL_Surface *kobo12_GetVideoSurface(void)
{
	return kobo12_screen;
}


int kobo12_Flip(SDL_Surface *screen)
{
	if(!kobo12_renderer || !kobo12_texture || !screen)
		return -1;

	if(SDL_UpdateTexture(kobo12_texture, NULL,
			screen->pixels, screen->pitch) < 0)
	{
		log_printf(ELOG, "kobo_sdl12: texture upload failed: %s\n",
				SDL_GetError());
		return -1;
	}

	/*
	 * Clear first: with a logical size the letterbox bars are outside the
	 * texture, and stale pixels would otherwise persist there.
	 */
	SDL_RenderClear(kobo12_renderer);
	SDL_RenderCopy(kobo12_renderer, kobo12_texture, NULL, NULL);
	SDL_RenderPresent(kobo12_renderer);
	return 0;
}


void kobo12_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects)
{
	/*
	 * The upload is one texture write regardless of how much changed, so
	 * the rectangle list buys nothing here and is ignored.
	 */
	(void)numrects;
	(void)rects;
	kobo12_Flip(screen);
}


void kobo12_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y,
		Uint32 w, Uint32 h)
{
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	kobo12_Flip(screen);
}


SDL_Surface *kobo12_DisplayFormat(SDL_Surface *surface)
{
	if(!surface)
		return NULL;
	if(!kobo12_screen)
		return SDL_ConvertSurfaceFormat(surface,
				SDL_PIXELFORMAT_ARGB8888, 0);
	return SDL_ConvertSurface(surface, kobo12_screen->format, 0);
}


SDL_Surface *kobo12_DisplayFormatAlpha(SDL_Surface *surface)
{
	if(!surface)
		return NULL;
	return SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
}


int kobo12_SetAlpha(SDL_Surface *surface, Uint32 flags, Uint8 alpha)
{
	if(!surface)
		return -1;

	if(flags & SDL_SRCALPHA)
	{
		if(SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND) < 0)
			return -1;
		return SDL_SetSurfaceAlphaMod(surface, alpha);
	}

	/*
	 * No SDL_SRCALPHA: SDL 1.2 then ignores both the per-surface alpha and
	 * any alpha channel, so the blend mode goes off entirely rather than
	 * just setting the modulation to opaque.
	 */
	if(SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE) < 0)
		return -1;
	return SDL_SetSurfaceAlphaMod(surface, SDL_ALPHA_OPAQUE);
}


int kobo12_KeysymChar(const SDL_Keysym *keysym)
{
	int shifted;

	if(!keysym)
		return 0;
	if((keysym->sym < SDLK_a) || (keysym->sym > SDLK_z))
		return 0;

	/* Caps Lock and Shift cancel rather than compound, as on any tty. */
	shifted = ((keysym->mod & KMOD_SHIFT) != 0) !=
			((keysym->mod & KMOD_CAPS) != 0);

	/* SDLK_a..SDLK_z are the ASCII lowercase letters by definition. */
	return shifted ? (keysym->sym - 'a' + 'A') : keysym->sym;
}


int kobo12_GrabInput(int mode)
{
	if(!kobo12_window)
		return SDL_GRAB_OFF;

	if(mode != SDL_GRAB_QUERY)
		SDL_SetWindowGrab(kobo12_window,
				mode == SDL_GRAB_ON ? SDL_TRUE : SDL_FALSE);

	/*
	 * 1.2 returned the resulting state, not the requested one, and
	 * states.cpp relies on that to avoid re-grabbing.
	 */
	return SDL_GetWindowGrab(kobo12_window) == SDL_TRUE ?
			SDL_GRAB_ON : SDL_GRAB_OFF;
}


void kobo12_SetCaption(const char *title, const char *icon)
{
	(void)icon;	/* 1.2's separate icon-title text has no SDL 2 analogue */
	if(kobo12_window && title)
		SDL_SetWindowTitle(kobo12_window, title);
}


int kobo12_SetColorKey(SDL_Surface *surface, Uint32 flags, Uint32 key)
{
	if(!surface)
		return -1;

	/*
	 * Only SDL_SRCCOLORKEY enables the key. Callers in filters.c pass
	 * SDL_RLEACCEL on its own to *disable* one, so a 'flags != 0' test
	 * would invert those sites and break sprite transparency in a way
	 * that is very hard to trace back to here.
	 */
	if(flags & SDL_SRCCOLORKEY)
		return SDL_SetColorKey(surface, SDL_TRUE, key);
	return SDL_SetColorKey(surface, SDL_FALSE, 0);
}
