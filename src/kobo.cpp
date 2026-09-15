/*(GPL)
------------------------------------------------------------
   Kobo Deluxe - An enhanced SDL port of XKobo
------------------------------------------------------------
 * Copyright (C) §95, 1996 Akira Higuchi
 * Copyright (C) 2001-2003, 2005-2007, 2020 David Olofson
 * Copyright (C) 2005 Erik Auerswald
 * Copyright (C) 2026, monkeywidget (SDL 2 port)
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define	DBG(x)	x

#undef	DEBUG_OUT

// Use this to benchmark and create a new percentage table!
#undef	TIME_PROGRESS

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>

#ifdef DEBUG
#include "audio.h"
extern "C" {
/*For the oscilloscope*/
#include "a_globals.h"
/*For VU and voice info*/
#include "a_struct.h"
}
#endif

#include "kobolog.h"
#include "config.h"
#include "kobo.h"
#include "states.h"
#include "screen.h"
#include "manage.h"
#include "score.h"
#include "gamectl.h"
#include "random.h"
#include "options.h"
#include "myship.h"
#include "enemies.h"

#define	MAX_FPS_RESULTS	64

/*
 * Joystick support.
 *
 * The axis, hat and button mapping lives in prefs.cpp as ordinary
 * preferences (js_lr, js_ud, js_hat, js_deadzone, js_fire, js_fire2,
 * js_exit, js_menu, js_pause), not as constants here, because the indices
 * belong to the device and the platform rather than to the game. Every one
 * is also a "-<key> <value>" command line switch, so a mapping can be
 * corrected on a new device without rebuilding.
 */


/*----------------------------------------------------------
	Singletons
----------------------------------------------------------*/
KOBO_sound		sound;


/*----------------------------------------------------------
	Globals
----------------------------------------------------------*/
filemapper_t		*fmap = NULL;
prefs_t			*prefs = NULL;
kobo_gfxengine_t	*gengine = NULL;

screen_window_t		*wscreen = NULL;
dashboard_window_t	*wdash = NULL;
bargraph_t		*whealth = NULL;
bargraph_t		*wtemp = NULL;
bargraph_t		*wttemp = NULL;
radar_map_t		*wmap = NULL;
radar_window_t		*wradar = NULL;
window_t		*wmain = NULL;
display_t		*dhigh = NULL;
display_t		*dscore = NULL;
display_t		*dstage = NULL;
display_t		*dships = NULL;
RGN_region		*logo_region = NULL;

int mouse_x = 0;
int mouse_y = 0;
int mouse_left = 0;
int mouse_middle = 0;
int mouse_right = 0;

int exit_game = 0;


static int main_init()
{
	prefs = new prefs_t;
	fmap = new filemapper_t;
	gengine = new kobo_gfxengine_t;
	return 0;
}


static void main_cleanup()
{
	delete gengine;
	gengine = NULL;
	delete fmap;
	fmap = NULL;
	delete prefs;
	prefs = NULL;
}


/*----------------------------------------------------------
	Various functions
----------------------------------------------------------*/

static void setup_dirs(char *xpath)
{
	fmap->exepath(xpath);

#ifdef __ANDROID__
	/*
	 * Android: the data is packaged inside the APK, and the only way to
	 * read it is SDL_RWFromFile()'s asset manager fallback, which triggers
	 * for a RELATIVE path and nothing else.
	 *
	 * So the asset roots are registered directly rather than through DATA.
	 * Going via DATA would not work: paths are joined as "%s/%s", so an
	 * empty data root yields "/gfx" -- absolute, and the fallback never
	 * fires. "gfx" and "sfx" are the paths the packaging step creates
	 * inside assets/, and they resolve to exactly what the asset manager
	 * expects: "GFX>>foo.png" -> "gfx/foo.png".
	 *
	 * Neither host mechanism means anything here: KOBO_DATADIR is an
	 * absolute path baked in on the build machine, and EXE>> derives from
	 * argv[0], which on Android points at the native library directory
	 * rather than anywhere data lives.
	 */
	fmap->addpath("GFX", "gfx");
	fmap->addpath("SFX", "sfx");
#else

#ifdef __APPLE__
	/*
	 * Inside a .app bundle the data lives in Contents/Resources, alongside
	 * the Contents/MacOS that holds the executable. Registered first so a
	 * bundled copy is self-contained: it then needs neither the build
	 * machine's install prefix nor anything else on the host to exist,
	 * which is what makes the .app distributable at all.
	 *
	 * Harmless when running from a build tree, where this simply does not
	 * resolve and the paths below are used instead.
	 */
	fmap->addpath("DATA", "EXE>>../Resources");
#endif

#ifdef KOBO_DATADIR
	fmap->addpath("DATA", KOBO_DATADIR);
#endif
	fmap->addpath("DATA", "EXE>>");		// Next to the executable

	/*
	 * Graphics data
	 */
	/* Current dir; from within the build tree */
	/* Real data dir */
	fmap->addpath("GFX", "DATA>>gfx");

	/*
	 * Sound data
	 */
	/* Current dir; from within the build tree */
	/* Real data dir */
	fmap->addpath("SFX", "DATA>>sfx");

#endif	/* __ANDROID__ */

	/*
	 * Score files (user and global)
	 */
	/*
	 * Writable user data: scores, profiles and the config file.
	 *
	 * SDL_GetPrefPath() is the preferred location on every platform, and on
	 * Android it is the only one that can work: there is no meaningful
	 * $HOME there, so "HOME>>.kobodeluxe" cannot resolve at all, and
	 * app-private storage is the only place the process may write.
	 *
	 * It also removes the need to create the directory: SDL_GetPrefPath()
	 * creates it as a side effect and returns a path with a trailing
	 * separator. That is why this is registered first -- it means the
	 * Android build never depends on mkdir() succeeding, or on the app
	 * having any idea what the device calls the user's account.
	 *
	 * The caller owns the returned string and must SDL_free() it.
	 */
	{
		char *prefpath = SDL_GetPrefPath(KOBO_ORGANIZATION,
				KOBO_APPLICATION);
		if(prefpath)
		{
			log_printf(VLOG, "User data directory: %s\n", prefpath);
			fmap->addpath("SCORES", prefpath, 1);
			fmap->addpath("CONFIG", prefpath, 1);
			SDL_free(prefpath);
		}
		else
			log_printf(WLOG, "SDL_GetPrefPath() failed: %s\n",
					SDL_GetError());
	}

#if defined(KOBO_USERDIR) && !defined(__ANDROID__)
	/*
	 * The traditional ~/.kobodeluxe, kept as a lower priority fallback so
	 * that saves made by an older build are still found. Created here
	 * because nothing in the game ever did, which is why saving a profile
	 * or a score used to fail outright on a machine without one.
	 */
	fmap->mkdir(KOBO_USERDIR, 0755);
	fmap->addpath("SCORES", KOBO_USERDIR);
	fmap->addpath("CONFIG", KOBO_USERDIR);
#endif
	/* System local */
#ifdef KOBO_SYSCONFDIR
	fmap->addpath("CONFIG", KOBO_SYSCONFDIR);
#endif
}


static void add_dirs(prefs_t *p)
{
	char buf[300];
	if(p->dir[0])
	{
		char *upath = fmap->sys2unix(p->dir);
		snprintf(buf, 300, "%s/sfx", upath);
		fmap->addpath("SFX", buf, 1);
		snprintf(buf, 300, "%s/gfx", upath);
		fmap->addpath("GFX", buf, 1);
		snprintf(buf, 300, "%s/scores", upath);
		fmap->addpath("SCORES", buf, 1);
		snprintf(buf, 300, "%s", upath);
		fmap->addpath("CONFIG", buf, 0);
	}

	if(p->sfxdir[0])
		fmap->addpath("SFX", fmap->sys2unix(p->sfxdir), 1);

	if(p->gfxdir[0])
		fmap->addpath("GFX", fmap->sys2unix(p->gfxdir), 1);

	if(p->scoredir[0])
		fmap->addpath("SCORES", fmap->sys2unix(p->scoredir), 1);
}


#ifdef DEBUG
static void draw_osc(int mode)
{
	int mx = oscframes;
	if(mx > wmain->width())
		mx = wmain->width();
	int yo = wmain->height() - 40;
	wmain->foreground(wmain->map_rgb(0x000099));
	wmain->fillrect(0, yo, wmain->width(), 1);
	wmain->foreground(wmain->map_rgb(0x990000));
	wmain->fillrect(0, yo - 32, wmain->width(), 1);
	wmain->fillrect(0, yo + 31, wmain->width(), 1);

	switch(mode)
	{
	  case 0:
		wmain->foreground(wmain->map_rgb(0x009900));
		for(int s = 0; s < mx; ++s)
			wmain->point(s, ((oscbufl[s]+oscbufr[s]) >> 11) + yo);
		break;
	  case 1:
		wmain->foreground(wmain->map_rgb(0x009900));
		for(int s = 0; s < mx; ++s)
			wmain->point(s, (oscbufl[s] >> 10) + yo);
		wmain->foreground(wmain->map_rgb(0xcc0000));
		for(int s = 0; s < mx; ++s)
			wmain->point(s, (oscbufr[s] >> 10) + yo);
		break;
	  case 2:
		wmain->foreground(wmain->map_rgb(0x009900));
		for(int s = 0; s < mx; ++s)
			wmain->point(s>>1, (oscbufl[s] >> 10) + yo);
		wmain->foreground(wmain->map_rgb(0xcc0000));
		for(int s = 0; s < mx; ++s)
			wmain->point((s>>1) + (mx>>1), (oscbufr[s] >> 10) + yo);
		wmain->fillrect(mx/2, yo-34, 1, 68);
		break;
	}

	wmain->foreground(wmain->map_rgb(0xcc0000));
	wmain->fillrect(0, yo - 32, 6, limiter.attenuation >> 11);
}


static void draw_vu(void)
{
	int xo = (wmain->width() - 5 * AUDIO_MAX_VOICES) / 2;
	int yo = wmain->height() - 50;
	wmain->foreground(wmain->map_rgb(0x000000));
	for(int s = 4; s < 40; s += 4)
		wmain->fillrect(xo, yo+s, 5 * AUDIO_MAX_VOICES-1, 1);
	wmain->foreground(wmain->map_rgb(0x333333));
	wmain->fillrect(xo-1, yo, 5 * AUDIO_MAX_VOICES+1, 1);
	wmain->fillrect(xo-1, yo+40, 5 * AUDIO_MAX_VOICES+1, 4);
	for(int s = 0; s <= AUDIO_MAX_VOICES; ++s)
		wmain->fillrect(xo + s*5 - 1, yo+1, 1, 39);
	for(int s = 0; s < AUDIO_MAX_VOICES; ++s)
	{
		int vu, vu2, vumin;
		if(VS_STOPPED != voicetab[s].state)
		{
			wmain->foreground(wmain->map_rgb(0x009900));
			wmain->fillrect(xo + 1, yo + 41, 2, 2);

			vu = labs((voicetab[s].ic[VIC_LVOL].v>>1) +
					(voicetab[s].ic[VIC_RVOL].v>>1)) >>
					RAMP_BITS;
#ifdef AUDIO_USE_VU
			vu2 = voicetab[s].vu;
			voicetab[s].vu = 0;
#else
			vu2 = 0;
#endif
			vu2 = (vu2>>4) * (vu>>4) >> 8;
			vu2 >>= 11;
			if(vu2 > 40)
				vu2 = 40;
			vu >>= 11;
			if(vu > 40)
				vu = 40;
			vumin = vu < vu2 ? vu : vu2;
		}
		else
			vu = vu2 = vumin = 0;
		wmain->foreground(wmain->map_rgb(0x006600));
		wmain->fillrect(xo, yo + 40 - vu, 4, vu - vu2);
		wmain->foreground(wmain->map_rgb(0xffcc00));
		wmain->fillrect(xo, yo + 40 - vu2, 4, vu2);
		xo += 5;
	}
}
#endif


/*----------------------------------------------------------
	The main object
----------------------------------------------------------*/
class KOBO_main
{
  public:
#ifdef DEBUG
	static int		audio_vismode;
#endif
	static SDL_Joystick	*joystick;
	static SDL_GameController	*gamepad;
	static SDL_JoystickID	js_instance;

	static FILE		*logfile;

	static Uint32		esc_tick;
	static int		esc_count;
	static int		exit_game_fast;

	// Frame rate counter
	static int		fps_count;
	static int		fps_starttime;
	static int		fps_nextresult;
	static int		fps_lastresult;
	static float		*fps_results;
	static display_t	*dfps;

	// Frame rate limiter
	static float		max_fps_filter;
	static int		max_fps_begin;

	static int		xoffs;
	static int		yoffs;

	// Backup in case we screw up we can't get back up
	static prefs_t		safe_prefs;

	static int open();
	static void close();
	static int run();

	static int open_logging(prefs_t *p);
	static void close_logging();
	static void load_config(prefs_t *p);
	static void save_config(prefs_t *p);

	static void build_screen();
	static int init_display(prefs_t *p);
	static void close_display();

	static void show_progress(prefs_t *p);
	static void progress();
	static void doing(const char *msg);
	static int load_graphics(prefs_t *p);
	static int load_sounds(prefs_t *p, int render_all = 0);

	static int init_js(prefs_t *p);
	static void close_js();

	static int escape_hammering();
	static int quit_requested();
	static void brutal_quit();
	static void pause_game();

	static void print_fps_results();
};

#ifdef DEBUG
int		KOBO_main::audio_vismode = 0;
#endif

SDL_Joystick	*KOBO_main::joystick = NULL;
SDL_GameController	*KOBO_main::gamepad = NULL;
SDL_JoystickID	KOBO_main::js_instance = -1;

FILE		*KOBO_main::logfile = NULL;

Uint32		KOBO_main::esc_tick = 0;
int		KOBO_main::esc_count = 0;
int		KOBO_main::exit_game_fast = 0;

int		KOBO_main::fps_count = 0;
int		KOBO_main::fps_starttime = 0;
int		KOBO_main::fps_nextresult = 0;
int		KOBO_main::fps_lastresult = 0;
float		*KOBO_main::fps_results = NULL;
display_t	*KOBO_main::dfps = NULL;

float		KOBO_main::max_fps_filter = 0.0f;
int		KOBO_main::max_fps_begin = 0;

int		KOBO_main::xoffs = 0;
int		KOBO_main::yoffs = 0;

prefs_t		KOBO_main::safe_prefs;


static KOBO_main km;


void KOBO_main::print_fps_results()
{
	int i, r = fps_nextresult;
	if(fps_lastresult != MAX_FPS_RESULTS-1)
		r = 0;
	for(i = 0; i < fps_lastresult; ++i)
	{
		log_printf(ULOG, "%.1f fps\n", fps_results[r++]);
		if(r >= MAX_FPS_RESULTS)
			r = 0;
	}

	free(fps_results);
	fps_nextresult = 0;
	fps_lastresult = 0;
	fps_results = NULL;
}


int KOBO_main::escape_hammering()
{
	Uint32 nt = SDL_GetTicks();
	if(nt - esc_tick > 300)
		esc_count = 1;
	else
		++esc_count;
	esc_tick = nt;
	return esc_count >= 5;
}


int KOBO_main::quit_requested()
{
	SDL_Event e;
	while(SDL_PollEvent(&e))
	{
		switch(e.type)
		{
		  case SDL_QUIT:
			exit_game_fast = 1;
			break;
		  case SDL_WINDOWEVENT:
			// SDL 2 folded SDL_VIDEOEXPOSE into SDL_WINDOWEVENT.
			if(e.window.event == SDL_WINDOWEVENT_EXPOSED)
				gengine->invalidate();
			break;
		  case SDL_KEYUP:
			switch(e.key.keysym.sym)
			{
			  case SDLK_ESCAPE:
				if(escape_hammering())
					exit_game_fast = 1;
				break;
			  default:
				break;
			}
			break;
		}
	}
	if(exit_game_fast)
		return 1;
	return SDL_QuitRequested();
}


void KOBO_main::close_logging()
{
	/* Flush logs to disk, close log files etc. */
	log_close();

	if(logfile)
	{
		fclose(logfile);
		logfile = NULL;
	}
}


int KOBO_main::open_logging(prefs_t *p)
{
	close_logging();

	if(log_open() < 0)
		return -1;

	if(p && p->logfile)
		switch (p->logformat)
		{
		  case 2:
			logfile = fopen("log.html", "wb");
			break;
		  default:
			logfile = fopen("log.txt", "wb");
			break;
		}

	if(logfile)
	{
		log_set_target_stream(0, logfile);
		log_set_target_stream(1, logfile);
	}
	else
	{
		log_set_target_stream(0, stdout);
		log_set_target_stream(1, stderr);
	}

	log_set_target_stream(2, NULL);

	if(p)
		switch(p->logformat)
		{
		  default:
			log_set_target_flags(-1, LOG_TIMESTAMP);
			break;
		  case 1:
			log_set_target_flags(-1, LOG_ANSI | LOG_TIMESTAMP);
			break;
		  case 2:
			log_set_target_flags(-1, LOG_HTML | LOG_TIMESTAMP);
			break;
		}

	/* All levels output to stdout... */
	log_set_level_target(-1, 0);

	/* ...except these, that output to stderr. */
	log_set_level_target(ELOG, 1);
	log_set_level_target(CELOG, 1);

	/* Some fancy colors... */
	log_set_level_attr(ULOG, LOG_YELLOW);
	log_set_level_attr(WLOG, LOG_YELLOW | LOG_BRIGHT);
	log_set_level_attr(ELOG, LOG_RED | LOG_BRIGHT);
	log_set_level_attr(CELOG, LOG_RED | LOG_BRIGHT | LOG_BLINK);
	log_set_level_attr(DLOG, LOG_CYAN);
	log_set_level_attr(D2LOG, LOG_BLUE | LOG_BRIGHT);
	log_set_level_attr(D3LOG, LOG_BLUE);

	/* Disable levels as desired */
	if(p)
		switch(p->logverbosity)
		{
		  case 0:
			log_set_level_target(ELOG, 2);
		  case 1:
			log_set_level_target(WLOG, 2);
		  case 2:
			log_set_level_target(DLOG, 2);
		  case 3:
			log_set_level_target(D2LOG, 2);
		  case 4:
			log_set_level_target(D3LOG, 2);
		  case 5:
			break;
		}

	if(p && p->logfile && !logfile)
		log_printf(ELOG, "Couldn't open log file!\n");

	return 0;
}


void KOBO_main::build_screen()
{
	gengine->clear(0x000000);

	wdash->place(xoffs, yoffs, SCREEN_WIDTH, SCREEN_HEIGHT);

	whealth->place(xoffs + 4, yoffs + 92, 8, 128);
	whealth->bgimage(B_HEALTH_LID, 0);
	whealth->background(whealth->map_rgb(0x182838));
	whealth->redmax(0);

	wmain->place(xoffs + 8 + MARGIN, yoffs + MARGIN, WSIZE, WSIZE);
	gengine->output(wmain);

	dhigh->place(xoffs + 252, yoffs + 4, 64, 18);
	dhigh->font(B_NORMAL_FONT);
	dhigh->bgimage(B_HIGH_BACK, 0);
	dhigh->caption("HIGHSCORE");
	dhigh->text("000000000");

	dscore->place(dhigh->x(), dhigh->y2() + 4, 64, 18);
	dscore->font(B_NORMAL_FONT);
	dscore->bgimage(B_SCORE_BACK, 0);
	dscore->caption("SCORE");
	dscore->text("000000000");

	wmap->place(0, 0, MAP_SIZEX, MAP_SIZEY);
	wmap->offscreen();

	wradar->place(xoffs + 244,
			yoffs + (SCREEN_HEIGHT - MAP_SIZEY) / 2,
			MAP_SIZEX, MAP_SIZEY);
	wradar->bgimage(B_RADAR_BACK, 0);

	wtemp->place(xoffs + 244, yoffs + 188, 4, 32);
	wtemp->bgimage(B_TEMP_LID, 0);
	wtemp->background(wtemp->map_rgb(0x182838));
	wtemp->redmax(1);

	wttemp->place(xoffs + 248, yoffs + 188, 4, 32);
	wttemp->bgimage(B_TTEMP_LID, 0);
	wttemp->background(wttemp->map_rgb(0x182838));
	wttemp->redmax(1);

	dships->place(xoffs + 264, yoffs + 196, 38, 18);
	dships->font(B_NORMAL_FONT);
	dships->bgimage(B_SHIPS_BACK, 0);
	dships->caption("SHIPS");
	dships->text("000");

	dstage->place(dships->x(), dships->y2() + 4, 38, 18);
	dstage->font(B_NORMAL_FONT);
	dstage->bgimage(B_STAGE_BACK, 0);
	dstage->caption("STAGE");
	dstage->text("000");

	if(prefs->cmd_fps)
	{
		dfps = new display_t;
		dfps->init(gengine);
		dfps->place(0, -9, 48, 18);
		dfps->color(wdash->map_rgb(0, 0, 0));
		dfps->font(B_NORMAL_FONT);
		dfps->caption("FPS");
	}
}


int KOBO_main::init_display(prefs_t *p)
{
	int dw, dh;		// Display size
	int gw, gh;		// Game "window" size
	gengine->title("Kobo Deluxe " KOBO_VERSION_STRING, "kobodl");
	gengine->driver((gfx_drivers_t)p->videodriver);

	dw = p->width;
	dh = p->height;
	if(p->fullscreen)
	{
		// This game assumes 1:1 pixel aspect ratio, or 4:3
		// width:height ratio, so we need to adjust accordingly.
		// Note:
		//	This code assumes 1:1 pixels for all resolutions!
		//	This does not hold true for 1280x1024 or a CRT
		//	for example, but that's an incorrect (although
		//	all too common) setup anyway. (1280x1024 is for
		//	5:4 TFT displays only!)
		if(dw * 3 >= dh * 4)
		{
			// 4:3 or widescreen; Height defines size
			gw = dh * 4 / 3;
			gh = dh;
		}
		else
		{
			// "tallscreen" (probably 5:4 TFT or rotated display)
			// Width defines size
			gw = dw;
			gh = dw * 3 / 4;
		}
	}
	else
	{
		gw = dw;
		gh = dh;
	}

	// Scaling has 16ths granularity, so tiles scale properly!
	gengine->scale((int)((gw * 16 + 8) / SCREEN_WIDTH) / 16.f,
			(int)((gh * 16 + 8) / SCREEN_HEIGHT) / 16.f);

	// Read back and recalculate, in case the engine has some ideas...
	gw = (int)(SCREEN_WIDTH * gengine->xscale() + 0.5f);
	gh = (int)(SCREEN_HEIGHT * gengine->yscale() + 0.5f);

	if(!p->fullscreen)
	{
		//Add thin black border around the game "screen" in windowed mode.
		dw = gw + 8;
		dh = gh + 8;
	}

	xoffs = (int)((dw - gw) / 2 / gengine->xscale());
	yoffs = (int)((dh - gh) / 2 / gengine->yscale());
	gengine->size(dw, dh);

	gengine->mode(0, p->fullscreen);
	gengine->doublebuffer(p->doublebuf);
	gengine->pages(p->pages);
	gengine->vsync(p->vsync);
	gengine->shadow(p->shadow);
	gengine->cursor(0);

	gengine->period(game.speed);
	sound.period(game.speed);
	gengine->timefilter(p->timefilter * 0.01f);
	gengine->interpolation(p->filter);

	gengine->scroll_ratio(LAYER_OVERLAY, 0.0, 0.0);
	gengine->scroll_ratio(LAYER_BULLETS, 1.0, 1.0);
	gengine->scroll_ratio(LAYER_FX, 1.0, 1.0);
	gengine->scroll_ratio(LAYER_PLAYER, 1.0, 1.0);
	gengine->scroll_ratio(LAYER_ENEMIES, 1.0, 1.0);
	gengine->scroll_ratio(LAYER_BASES, 1.0, 1.0);
	gengine->wrap(MAP_SIZEX * CHIP_SIZEX, MAP_SIZEY * CHIP_SIZEY);

	if(gengine->open(ENEMY_MAX) < 0)
		return -1;

	gengine->clear();

	wscreen = new screen_window_t;
	wscreen->init(gengine);
	wscreen->place(0, 0,
			(int)(gengine->width() / gengine->xscale() + 0.5f),
			(int)(gengine->height() / gengine->yscale() + 0.5f));
	wscreen->border((int)(yoffs * gengine->yscale() + 0.5f),
			(int)(xoffs * gengine->xscale() + 0.5f),
			dw - gw - (int)(xoffs * gengine->xscale() + 0.5f),
			dh - gh - (int)(yoffs * gengine->yscale() + 0.5f));

	wdash = new dashboard_window_t;
	wdash->init(gengine);
	whealth = new bargraph_t;
	whealth->init(gengine);
	wmain = new window_t;
	wmain->init(gengine);
	dhigh = new display_t;
	dhigh->init(gengine);
	dscore = new display_t;
	dscore->init(gengine);
	wmap = new radar_map_t;
	wmap->init(gengine);
	wradar = new radar_window_t;
	wradar->init(gengine);
	wtemp = new bargraph_t;
	wtemp->init(gengine);
	wttemp = new bargraph_t;
	wttemp->init(gengine);
	dships = new display_t;
	dships->init(gengine);
	dstage = new display_t;
	dstage->init(gengine);

	build_screen();

	wdash->mode(DASHBOARD_BLACK);

	return 0;
}


void KOBO_main::close_display()
{
	delete dfps;
	dfps = NULL;
	delete dstage;
	dstage = NULL;
	delete dships;
	dships = NULL;
	delete wttemp;
	wttemp = NULL;
	delete wtemp;
	wtemp = NULL;
	delete wradar;
	wradar = NULL;
	delete wmap;
	wmap = NULL;
	delete dscore;
	dscore = NULL;
	delete dhigh;
	dhigh = NULL;
	delete wmain;
	wmain = NULL;
	delete whealth;
	whealth = NULL;
	delete wdash;
	wdash = NULL;
	delete wscreen;
	wscreen = NULL;
}


void KOBO_main::show_progress(prefs_t *p)
{
	wdash->mode(DASHBOARD_LOADING);
	wdash->doing("");
}


void KOBO_main::progress()
{
	wdash->progress();
}


void KOBO_main::doing(const char *msg)
{
	wdash->doing(msg);
}


#ifndef TIME_PROGRESS
static float progtab_graphics[] = {
	0.000000,
	1.167315,
	4.085603,
	5.836576,
	7.490273,
	9.046693,
	10.992218,
	12.937743,
	13.813230,
	15.564202,
	17.217899,
	18.774319,
	20.428015,
	22.081713,
	23.540855,
	25.291828,
	26.848249,
	28.404669,
	29.961090,
	32.003891,
	33.560310,
	35.311283,
	36.770428,
	38.521400,
	40.369648,
	41.828793,
	43.190662,
	44.649807,
	46.595329,
	48.151752,
	49.610893,
	51.361866,
	53.501945,
	58.852139,
	60.116730,
	61.089493,
	63.521400,
	66.050583,
	68.287941,
	69.844360,
	72.665367,
	74.124512,
	76.653694,
	77.334633,
	79.182877,
	80.642021,
	82.295723,
	83.852142,
	85.505836,
	87.159531,
	88.813232,
	90.369652,
	92.023346,
	93.677040,
	95.233459,
	96.984436,
	98.540855,
	100.0,
	0
};

static float progtab_sounds[] = {
	0.000000,
	58.709679,
	59.516129,
	98.790321,
	98.790321,
	100.0,
	0
};

static float progtab_all[] = {
	0.000000,
	1.556684,
	2.639594,
	3.282572,
	4.060914,
	4.737733,
	5.380711,
	8.697124,
	8.764806,
	10.016920,
	10.761421,
	11.472081,
	12.216582,
	12.893401,
	13.604061,
	14.416244,
	15.093062,
	15.262267,
	16.074450,
	17.428087,
	18.680202,
	19.932318,
	20.879864,
	21.759729,
	23.011845,
	24.365482,
	24.534687,
	24.839256,
	25.651438,
	26.429781,
	26.700508,
	27.241962,
	29.678511,
	38.477158,
	40.203045,
	40.372250,
	40.913704,
	42.571911,
	45.211506,
	45.922165,
	46.565144,
	46.903553,
	48.866329,
	48.967850,
	49.306259,
	49.847717,
	50.389172,
	50.964466,
	51.641285,
	52.115059,
	52.690357,
	53.231812,
	53.773266,
	54.382404,
	54.991539,
	55.499153,
	56.006767,
	57.495770,
	82.673431,
	82.978004,
	99.763115,
	99.796951,
	100.0,
	0
};
#endif


typedef enum
{
	KOBO_CLAMP =		0x0001,	// Clamp to frame edge pixels
	KOBO_CLAMP_OPAQUE =	0x0002,	// Clamp to black instead of transparent
	KOBO_NOALPHA =		0x0004,	// Disable alpha channel
	KOBO_NODITHER =		0x0008,	// Disable dithering
	KOBO_NEAREST =		0x0010,	// Force NEAREST scale mode
	KOBO_CENTER =		0x0020,	// Center hotspot in frames
	KOBO_NOBRIGHT =		0x0040,	// Disable brightness/contrast filter
	KOBO_FONT =		0x0080,	// Load as "SFont" rather than tiles!
	KOBO_MESSAGE =		0x1000	// Not a file! 'path' is a message.
} KOBO_GfxDescFlags;

typedef struct KOBO_GfxDesc
{
	const char	*path;		// Path + name (for filemapper)
	int		bank;		// Sprite bank ID
	int		w, h;		// Frame size (original image pixels)
	float		scale;		// Scale of source data rel. 320x240
	int		flags;		// KOBO_GfxDescFlags
} KOBO_GfxDesc;

static KOBO_GfxDesc gfxdesc[] = {
	// Loading screen
	{ "Loading loading screen graphics", 0, 0,0, 0.0f, KOBO_MESSAGE },
	{ "GFX>>loading3.png", B_LOADING,	0, 0,	2.0f,
			KOBO_CLAMP | KOBO_NOALPHA },
	{ "GFX>>icefont2.png", B_NORMAL_FONT,	0, 0,	2.0f,	KOBO_FONT },

	// In-game
	{ "Loading in-game graphics", 0, 0,0, 0.0f, KOBO_MESSAGE },
	{ "GFX>>tiles-green.png", B_TILES1,	32, 32,	2.0f,	KOBO_CLAMP },
	{ "GFX>>tiles-metal.png", B_TILES2,	32, 32,	2.0f,	KOBO_CLAMP },
	{ "GFX>>tiles-blood.png", B_TILES3,	32, 32,	2.0f,	KOBO_CLAMP },
	{ "GFX>>tiles-double.png", B_TILES4,	32, 32,	2.0f,	KOBO_CLAMP },
	{ "GFX>>tiles-chrome.png", B_TILES5,	32, 32,	2.0f,	KOBO_CLAMP },
	{ "GFX>>flatstars1.png", B_OLDSTARS,	32, 32,	2.0f,	KOBO_CLAMP },
	{ "GFX>>crosshair.png", B_CROSSHAIR,	32, 32,	2.0f,	KOBO_CENTER },
	{ "GFX>>player.png", B_PLAYER,		40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>bmr-green.png", B_BMR_GREEN,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>bmr-purple.png", B_BMR_PURPLE,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>bmr-pink.png", B_BMR_PINK,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>fighter.png", B_FIGHTER,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>missile.png", B_MISSILE1,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>missile2.png", B_MISSILE2,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>missile3.png", B_MISSILE3,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>bolt.png", B_BOLT,		16, 16,	2.0f,	KOBO_CENTER },
	{ "GFX>>boltexpl.png", B_BOLTEXPL,	32, 32,	2.0f,	KOBO_CENTER },
	{ "GFX>>explo1e.png", B_EXPLO1,		48, 48,	2.0f,	KOBO_CENTER },
	{ "GFX>>explo3e.png", B_EXPLO3,		64, 64,	2.0f,	KOBO_CENTER },
	{ "GFX>>explo4e.png", B_EXPLO4,		64, 64,	2.0f,	KOBO_CENTER },
	{ "GFX>>explo5e.png", B_EXPLO5,		64, 64,	2.0f,	KOBO_CENTER },
	{ "GFX>>rock1c.png", B_ROCK1,		32, 32,	2.0f,	KOBO_CENTER },
	{ "GFX>>rock2.png", B_ROCK2,		32, 32,	2.0f,	KOBO_CENTER },
	{ "GFX>>shinyrock.png", B_ROCK3,	32, 32,	2.0f,	KOBO_CENTER },
	{ "GFX>>rockexpl.png", B_ROCKEXPL,	64, 64,	2.0f,	KOBO_CENTER },
	{ "GFX>>bullet5b.png", B_BULLETS,	16, 16,	2.0f,	KOBO_CENTER },
	{ "GFX>>bulletexpl2.png", B_BULLETEXPL,	32, 32,	2.0f,	KOBO_CENTER },
	{ "GFX>>ring.png", B_RING,		32, 32,	2.0f,	KOBO_CENTER },
	{ "GFX>>ringexpl2b.png", B_RINGEXPL,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>bomb.png", B_BOMB,		24, 24,	2.0f,	KOBO_CENTER },
	{ "GFX>>bombdeto.png", B_BOMBDETO,	40, 40,	2.0f,	KOBO_CENTER },
	{ "GFX>>bigship.png", B_BIGSHIP,	72, 72,	2.0f,	KOBO_CENTER },

	// Framework
	{ "Loading framework graphics", 0, 0,0, 0.0f, KOBO_MESSAGE },
	{ "GFX>>screen2.png", B_SCREEN,	0, 0,	2.0f,	KOBO_CLAMP_OPAQUE },

	// Logo
	{ "Loading logo", 0, 0,0, 0.0f, KOBO_MESSAGE },
	{ "GFX>>logo-outline.png", B_LOGO,	0, 0,	2.0f,	0 },
	{ "GFX>>deluxe.png", B_LOGODELUXE,	0, 0,	2.0f,	0 },
	{ "GFX>>logomask3.png", B_LOGOMASK,	0, 0,	4.0f,
			KOBO_NEAREST | KOBO_NODITHER },

	// Fonts
	{ "Loading fonts", 0, 0,0, 0.0f, KOBO_MESSAGE },
	{ "GFX>>goldfont.png", B_MEDIUM_FONT,	0, 0,	2.0f,	KOBO_FONT },
	{ "GFX>>bigfont3.png", B_BIG_FONT,	0, 0,	2.0f,	KOBO_FONT },
	{ "GFX>>counterfont.png", B_COUNTER_FONT, 0, 0,	2.0f,	KOBO_FONT },

	// Special FX
	{ "Loading special FX graphics", 0, 0,0, 0.0f, KOBO_MESSAGE },
	{ "GFX>>noise.png", B_NOISE,		NOISE_SIZEX, 1,	1.0f,
			KOBO_CLAMP },
	{ "GFX>>hitnoise.png", B_HITNOISE,	NOISE_SIZEX, 1,	1.0f,
			KOBO_CLAMP | KOBO_NEAREST },
	{ "GFX>>focusfx.png", B_FOCUSFX,	0, 0,	1.0f, KOBO_CLAMP },
	{ "GFX>>brushes.png", B_BRUSHES,	16, 16,	1.0f,
			KOBO_CLAMP | KOBO_NEAREST | KOBO_NOALPHA },

	{ NULL, 0,	0, 0,	0.0f,	0 }	// Terminator
};


int KOBO_main::load_graphics(prefs_t *p)
{
	KOBO_GfxDesc *gd;
	gengine->reset_filters();
	show_progress(p);
	for(gd = gfxdesc; gd->path; ++gd)
	{
		if(gd->flags & KOBO_MESSAGE)
		{
			doing(gd->path);
			continue;
		}

		// Set scale (filter) mode and clamping
		int clamping;
		gfx_scalemodes_t sm;
		if(gd->flags & KOBO_CLAMP)
		{
			gengine->clampcolor(0, 0, 0, 0);
			clamping = 1;
		}
		else if(gd->flags & KOBO_CLAMP_OPAQUE)
		{
			gengine->clampcolor(0, 0, 0, 255);
			clamping = 1;
		}
		else
			clamping = 0;
		if(gd->flags & KOBO_NEAREST)
			sm = GFX_SCALE_NEAREST;
		else
			sm = (gfx_scalemodes_t) p->scalemode;
		gengine->scalemode(sm, clamping);

		// Disable brightness filter?
		if(gd->flags & KOBO_NOBRIGHT)
			gengine->brightness(1.0f, 1.0f);
		else
			gengine->brightness(0.01f * p->brightness,
					0.01f * p->contrast);

		// Dithering
		if(gd->flags & KOBO_NODITHER || !p->use_dither)
			gengine->dither(-1);
		else
			gengine->dither(p->dither_type, p->broken_rgba8);

		// Alpha channels
		if(gd->flags & KOBO_NOALPHA || !p->alpha)
			gengine->noalpha(NOALPHA_THRESHOLD);
		else
			gengine->noalpha(0);

		// Source image scale
		gengine->source_scale(gd->scale, gd->scale);

		// Load!
		const char *fn = fmap->get(gd->path);
		if(!fn)
		{
			log_printf(ELOG, "Couldn't get path to \"%s\"!\n",
					gd->path);
			return -1;
		}
		int res;
		if(gd->flags & KOBO_FONT)
			res = gengine->loadfont(gd->bank, fn);
		else if(!gd->w || !gd->h)
			res = gengine->loadimage(gd->bank, fn);
		else
			res = gengine->loadtiles(gd->bank, gd->w, gd->h, fn);
		if(res < 0)
		{
			log_printf(ELOG, "Couldn't load \"%s\"!\n", fn);
			return -1;
		}

		// Hotspot
		if(gd->flags & KOBO_CENTER)
			gengine->set_hotspot(gd->bank, -1,
					(int)(gd->w / gd->scale / 2),
					(int)(gd->h / gd->scale / 2));

		// Update progress bar
		progress();
	}

	// Chop up the dashboard graphics as needed
	SDL_Rect r;
	r.w = r.h = 16;
	r.x = (8 + MARGIN);
	r.y = MARGIN;
	if(gengine->copyrect(B_FRAME_TL, B_SCREEN, 0, &r) < 0)
		return -6;
	progress();
	r.x += WSIZE - 16;
	if(gengine->copyrect(B_FRAME_TR, B_SCREEN, 0, &r) < 0)
		return -7;
	progress();
	r.x = (8 + MARGIN);
	r.y = MARGIN + WSIZE - 16;
	if(gengine->copyrect(B_FRAME_BL, B_SCREEN, 0, &r) < 0)
		return -8;
	progress();
	r.x += WSIZE - 16;
	if(gengine->copyrect(B_FRAME_BR, B_SCREEN, 0, &r) < 0)
		return -9;
	progress();
	r.x = 252;
	r.y = 4;
	r.w = 64;
	r.h = 18;
	if(gengine->copyrect(B_HIGH_BACK, B_SCREEN, 0, &r) < 0)
		return -26;
	progress();
	r.y += 18 + 4;
	if(gengine->copyrect(B_SCORE_BACK, B_SCREEN, 0, &r) < 0)
		return -27;
	progress();
	r.x = 244;
	r.y = (SCREEN_HEIGHT - MAP_SIZEY) / 2;
	r.w = MAP_SIZEX;
	r.h = MAP_SIZEY;
	if(gengine->copyrect(B_RADAR_BACK, B_SCREEN, 0, &r) < 0)
		return -28;
	progress();
	r.x = 264;
	r.y = 196;
	r.w = 38;
	r.h = 18;
	if(gengine->copyrect(B_SHIPS_BACK, B_SCREEN, 0, &r) < 0)
		return -29;
	progress();
	r.y += 18 + 4;
	if(gengine->copyrect(B_STAGE_BACK, B_SCREEN, 0, &r) < 0)
		return -30;
	progress();
	r.x = 4;
	r.y = 92;
	r.w = 8;
	r.h = 128;
	if(gengine->copyrect(B_HEALTH_LID, B_SCREEN, 0, &r) < 0)
		return -31;
	progress();
	r.x = 244;
	r.y = 188;
	r.w = 4;
	r.h = 32;
	if(gengine->copyrect(B_TEMP_LID, B_SCREEN, 0, &r) < 0)
		return -32;
	progress();
	r.x += 4;
	if(gengine->copyrect(B_TTEMP_LID, B_SCREEN, 0, &r) < 0)
		return -33;
	progress();

	// Prepare the logo region
	s_sprite_t *s = gengine->get_sprite(B_LOGOMASK, 0);
	if(!s || !s->surface)
		return -10;
	RGN_FreeRegion(logo_region);
	logo_region = RGN_ScanMask(s->surface,
			SDL_MapRGB(s->surface->format, 255, 255, 255));
	if(!logo_region)
	{
		log_printf(ELOG, "Could not create logo region!\n");
		return -92;
	}
	progress();

	return 0;
}


static int progress_cb(const char *msg)
{
	if(msg)
		km.doing(msg);
	km.progress();
	if(km.quit_requested())
		return -999;
	return 0;
}


int KOBO_main::load_sounds(prefs_t *p, int render_all)
{
	if(!p->use_sound)
		return 0;
	show_progress(p);
	return sound.load(progress_cb, render_all);
}


int KOBO_main::init_js(prefs_t *p)
{
	/* Activate Joystick sub-sys if we are using it */
	if(p->use_joystick)
	{
#ifdef __MACOSX__
		/*
		 * Disable SDL's MFI (GameController framework) driver.
		 *
		 * On recent macOS it claims generic USB HID gamepads and then
		 * fails to expose them, so SDL_NumJoysticks() returns 0 with a
		 * perfectly healthy pad attached and no error reported
		 * anywhere. Verified with a Logitech F310 in DirectInput mode:
		 * MFI on -> "No joysticks found!"; MFI off -> "Logitech Dual
		 * Action", 4 axes, 12 buttons, 1 hat.
		 *
		 * Must be set before the joystick subsystem starts. Android has
		 * no MFI driver, so this is host-side only.
		 */
		SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
#endif
		/*
		 * SDL_INIT_GAMECONTROLLER, which implies SDL_INIT_JOYSTICK.
		 *
		 * The game controller layer is what makes this port usable on
		 * the TV box. There, the app is launched from the remote and
		 * there is no shell, no arguments and no reachable config file,
		 * so a wrong button mapping cannot be corrected by hand -- it
		 * has to be right unaided. SDL_GameController provides exactly
		 * that: stable A/B/X/Y/Back/Start/D-pad identities, which on
		 * Android SDL derives automatically from the capability bits it
		 * packs into each pad's GUID
		 * (SDL_CreateMappingForAndroidController()).
		 *
		 * The raw joystick path below is kept as a fallback for any pad
		 * SDL has no mapping for, where the js_* preferences still
		 * apply.
		 */
		if(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
		{
			log_printf(ELOG, "Error setting up joystick!\n");
			return -1;
		}
		p->number_of_joysticks = SDL_NumJoysticks();
		if(p->number_of_joysticks > 0)
		{
			SDL_JoystickEventState(SDL_ENABLE);
			if(p->joystick_no >= p->number_of_joysticks)
				p->joystick_no = 0;
			log_printf(VLOG, "Joysticks found: %d\n",
					p->number_of_joysticks);
			for(int i = 0; i < p->number_of_joysticks; ++i)
				log_printf(VLOG, "  %d: \"%s\"\n", i,
						SDL_JoystickNameForIndex(i));

			joystick = SDL_JoystickOpen(p->joystick_no);
			if(!joystick)
			{
				log_printf(ELOG, "Could not open joystick %d:"
						" %s\n", p->joystick_no,
						SDL_GetError());
				SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
				return -2;
			}

			if(SDL_IsGameController(p->joystick_no))
			{
				gamepad = SDL_GameControllerOpen(
						p->joystick_no);
				if(gamepad)
				{
					/*
					 * Say so positively. On the TV box this
					 * line is the difference between "SDL
					 * knows this pad" and "we are guessing
					 * button indices", and the absence of a
					 * warning is not evidence of the former.
					 */
					char *map = SDL_GameControllerMapping(
							gamepad);
					log_printf(VLOG, "Game controller:"
							" \"%s\"\n",
							SDL_GameControllerName(
							gamepad));
					log_printf(VLOG, "     Mapping: %s\n",
							map ? map : "(none)");
					if(map)
						SDL_free(map);
				}
				if(!gamepad)
					log_printf(WLOG, "Could not open as a"
							" game controller"
							" (%s); falling back to"
							" raw joystick.\n",
							SDL_GetError());
			}
			else
				log_printf(WLOG, "SDL has no game controller"
						" mapping for this device;"
						" falling back to the js_*"
						" button preferences.\n");

			/*
			 * Report what the device actually is. Button and axis
			 * indices are whatever the device claims -- they differ
			 * between the same pad's X and D modes, and Android
			 * derives them from its own keycode table -- so this
			 * block is the only thing that says what any mapping
			 * has to be written against.
			 */
			log_printf(VLOG, "Using joystick %d:\n",
					p->joystick_no);
			log_printf(VLOG, "         Name: %s\n",
					SDL_JoystickName(joystick));
			js_instance = SDL_JoystickInstanceID(joystick);
			log_printf(VLOG, "  Instance ID: %d\n", js_instance);
			log_printf(VLOG, "         Axes: %d\n",
					SDL_JoystickNumAxes(joystick));
			log_printf(VLOG, "      Buttons: %d\n",
					SDL_JoystickNumButtons(joystick));
			log_printf(VLOG, "         Hats: %d\n",
					SDL_JoystickNumHats(joystick));

			/*
			 * Warn about a mapping that cannot work.
			 *
			 * The button handler tests these in a fixed order and
			 * the first match wins, so binding two actions to one
			 * button silently makes the later one unreachable. That
			 * matters most for the exit button: it is the only way
			 * out of every options screen, and on a device with no
			 * keyboard, losing it means force-quitting the app.
			 */
			{
				static const struct
				{
					const char	*name;
					int		*btn;
				} bindings[] = {
					{ "fire",	&prefs->js_fire },
					{ "fire2",	&prefs->js_fire2 },
					{ "exit",	&prefs->js_exit },
					{ "menu",	&prefs->js_menu },
					{ "pause",	&prefs->js_pause },
					{ "dpad_up",	&prefs->js_dpad_up },
					{ "dpad_down",	&prefs->js_dpad_down },
					{ "dpad_left",	&prefs->js_dpad_left },
					{ "dpad_right",	&prefs->js_dpad_right }
				};
				int n = sizeof(bindings) / sizeof(bindings[0]);
				int nbuttons = SDL_JoystickNumButtons(joystick);
				for(int i = 0; i < n; ++i)
				{
					/* Negative means "not bound here". */
					if(*bindings[i].btn < 0)
						continue;
					if(*bindings[i].btn >= nbuttons)
						log_printf(WLOG, "Joystick"
							" binding \"%s\" is"
							" button %d, but this"
							" device only has"
							" %d!\n",
							bindings[i].name,
							*bindings[i].btn,
							nbuttons);
					for(int j = i + 1; j < n; ++j)
						if((*bindings[j].btn >= 0) &&
								(*bindings[i].btn ==
								*bindings[j].btn))
							log_printf(WLOG,
							"Joystick bindings"
							" \"%s\" and \"%s\""
							" are both button"
							" %d; \"%s\" will"
							" never fire!\n",
							bindings[i].name,
							bindings[j].name,
							*bindings[i].btn,
							bindings[j].name);
				}
			}
		}
		else
		{
			log_printf(ELOG, "No joysticks found!\n");
			joystick = NULL;
			return -3;
		}

	}
	return 0;
}


void KOBO_main::close_js()
{
	if(!SDL_WasInit(SDL_INIT_JOYSTICK))
		return;

	if(!joystick)
		return;

	// SDL 2 removed SDL_JoystickOpened(); the non-NULL check above already
	// tells us we have an open device, and it is the right one, which
	// SDL_JoystickOpened(0) did not actually establish.
	if(gamepad)
	{
		SDL_GameControllerClose(gamepad);
		gamepad = NULL;
	}
	SDL_JoystickClose(joystick);
	joystick = NULL;

	SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}


void KOBO_main::load_config(prefs_t *p)
{
	FILE *f = fmap->fopen("CONFIG>>" KOBO_CONFIGFILE, "r");
	if(f)
	{
		p->read(f);
		fclose(f);
	}
}


void KOBO_main::save_config(prefs_t *p)
{
	FILE *f;
#if defined(HAVE_GETEGID) && defined(HAVE_SETGID)
	gid_t oldgid = getegid();
	if(setgid(getgid()) != 0)
	{
		log_printf(ELOG, "Cannot save config! (%s)\n",
				strerror(errno));
		return;
	}
#endif
	f = fmap->fopen("CONFIG>>" KOBO_CONFIGFILE, "w");
	if(f)
	{
		p->write(f);
		fclose(f);
	}
#if defined(HAVE_GETEGID) && defined(HAVE_SETGID)
	if(setgid(oldgid) != 0)
	{
		log_printf(ELOG, "Cannot restore GID! (%s)\n",
				strerror(errno));
		return;
	}
#endif
}


void KOBO_main::brutal_quit()
{
	if(exit_game_fast)
	{
		log_printf(ULOG, "Second try quitting; using brutal method!\n");
		atexit(SDL_Quit);
		close_logging();
		exit(1);
	}

	exit_game_fast = 1;
	if(gengine)
		gengine->stop();
}


void KOBO_main::pause_game()
{
	if(gsm.current() != &st_pause_game)
		gsm.press(BTN_PAUSE);
}


void kobo_render_highlight(ct_widget_t *wg)
{
	screen.set_highlight(wg->y() + wg->height() / 2 - wmain->y(),
			wg->height());
}


int KOBO_main::open()
{
	if(init_display(prefs) < 0)
		return -1;

#ifdef TIME_PROGRESS
	wdash->progress_init(NULL);
#else
	wdash->progress_init(progtab_all);
#endif

	if(load_graphics(prefs) < 0)
		return -2;

#ifdef TIME_PROGRESS
	// Use these two lines to time graphics and sounds separately!
	wdash->progress_done();
	wdash->progress_init(NULL);
#endif

	sound.open();

	if(load_sounds(prefs) < 0)
		return -3;

	wdash->progress_done();

	wdash->nibble();

	ct_engine.render_highlight = kobo_render_highlight;
	wdash->mode(DASHBOARD_GAME);
	wradar->mode(RM_NOISE);
	pubrand.init();
	init_js(prefs);
	gamecontrol.init(prefs->always_fire);
	manage.init();

	gsm.push(&st_intro_title);

	return 0;
}


void KOBO_main::close()
{
	close_js();
	RGN_FreeRegion(logo_region);
	logo_region = NULL;
	close_display();
	if(gengine)
	{
		delete gengine;
		gengine = NULL;
	}
	sound.close();
	SDL_Quit();
}


int KOBO_main::run()
{
	int retry_status = 0;
	int dont_retry = 0;
	while(1)
	{
		if(!retry_status)
		{
			safe_prefs = *prefs;
			gengine->run();

			if(exit_game_fast)
				break;

			if(exit_game)
			{
				if(wdash)
					wdash->nibble();
				break;
			}
			dont_retry = 0;
		}

		retry_status = 0;
		if(global_status & OS_RESTART_AUDIO)
		{
			if(prefs->use_sound)
			{
				log_printf(ULOG, "--- Restarting audio...\n");
				sound.stop();
#ifdef TIME_PROGRESS
				wdash->progress_init(NULL);
#else
				wdash->progress_init(progtab_sounds);
#endif
				if(load_sounds(prefs) < 0)
					return 5;
				wdash->progress_done();
				sound.open();
				wdash->nibble();
				log_printf(ULOG, "--- Audio restarted.\n");
				wdash->mode(DASHBOARD_GAME);
				wradar->mode(screen.radar_mode);
				manage.set_bars();
			}
			else
			{
				log_printf(ULOG, "--- Stopping audio...\n");
				sound.close();
				log_printf(ULOG, "--- Audio stopped.\n");
			}
		}
		if((global_status & OS_RELOAD_AUDIO_CACHE) && prefs->cached_sounds)
		{
			log_printf(ULOG, "--- Rendering sounds to disk...\n");
#ifdef TIME_PROGRESS
			wdash->progress_init(NULL);
#else
			wdash->progress_init(progtab_sounds);
#endif
			if(load_sounds(prefs) < 0)
			{
				log_printf(ELOG, "--- Could not render sounds to disk!\n");
				st_error.message("Could not render to disk!",
						"Please, check your installation.");
				gsm.push(&st_error);
			}
			wdash->progress_done();
			wdash->mode(DASHBOARD_GAME);
			wradar->mode(screen.radar_mode);
			manage.set_bars();
		}
		if(global_status & OS_RESTART_VIDEO)
		{
			log_printf(ULOG, "--- Restarting video...\n");
			wdash->nibble();
			gengine->hide();
			close_display();
			gengine->unload();
			if(init_display(prefs) < 0)
			{
				log_printf(ELOG, "--- Video init failed!\n");
				st_error.message("Video initialization failed!",
						"Try different settings.");
				gsm.push(&st_error);
				if(dont_retry)
					return 6;
				*prefs = safe_prefs;
				dont_retry = 1;
				retry_status |= OS_RESTART_VIDEO;
			}
			else
			{
				gamecontrol.init(prefs->always_fire);
				log_printf(ULOG, "--- Video restarted.\n");
			}
		}
		if(global_status & (OS_RELOAD_GRAPHICS |
					OS_RESTART_VIDEO))
		{
			if(!(global_status & OS_RESTART_VIDEO))
				wdash->nibble();
			gengine->unload();
			RGN_FreeRegion(logo_region);
			logo_region = NULL;
			if(!retry_status)
			{
				log_printf(ULOG, "--- Reloading graphics...\n");
#ifdef TIME_PROGRESS
				wdash->progress_init(NULL);
#else
				wdash->progress_init(progtab_graphics);
#endif
				if(load_graphics(prefs) < 0)
					return 7;
				wdash->progress_done();
				wdash->nibble();
				wdash->mode(DASHBOARD_GAME);
				wradar->mode(screen.radar_mode);
				manage.set_bars();
				log_printf(ULOG, "--- Graphics reloaded.\n");
			}
		}
		if(global_status & OS_RESTART_ENGINE)
			log_printf(ELOG, "OS_RESTART_ENGINE not implemented!\n");
		if(global_status & OS_RESTART_INPUT)
		{
			close_js();
			init_js(prefs);
			gamecontrol.init(prefs->always_fire);
		}
		if(global_status & OS_RESTART_LOGGER)
			open_logging(prefs);

		global_status = retry_status;
		km.pause_game();
		manage.reenter();
	}
	return 0;
}


/*----------------------------------------------------------
	Kobo Graphics Engine
----------------------------------------------------------*/

kobo_gfxengine_t::kobo_gfxengine_t()
{
}


/*----------------------------------------------------------------------------
	Joystick direction decoding
----------------------------------------------------------------------------*/
/*
 * Upstream decoded the two stick axes independently, each against a hardcoded
 * +/-3200 threshold, and had no D-pad support at all. Two things were wrong
 * with that beyond the missing hat:
 *
 *   - A per-axis threshold gives a square deadzone, so the stick engages
 *     sooner on the diagonals than on the cardinals, and a stick resting just
 *     inside the corner of the square reads as a held diagonal.
 *   - Decoding each axis event on its own lets a single physical push emit two
 *     different directions, because SDL reports X and Y as separate events.
 *
 * Both are fixed by keeping the last known value of each axis and deriving one
 * direction from the pair, which is what js_dir_from_axes() does: a *radial*
 * deadzone, then a quantisation to the same eight directions the numpad
 * produces.
 */


/*
 * Hysteresis. Without it, a stick held near a boundary flips repeatedly
 * between two neighbouring directions: measured over one play session, 32.5%
 * of all direction changes were an immediate reversal (A -> B -> A), which is
 * what makes analog control of an 8-way game feel unsteady.
 *
 * KOBO_JS_SECTOR_HYST is in units of 45 degree sectors, so 0.15 means the
 * stick has to travel about 7 degrees past the boundary before the direction
 * gives way. KOBO_JS_DEADZONE_HYST does the same for the centre, so that a
 * stick hovering at the edge of the deadzone does not flicker between "held"
 * and "released".
 */
#define	KOBO_JS_SECTOR_HYST	0.15
#define	KOBO_JS_DEADZONE_HYST	3

static int js_axis_x = 0;		/* last known raw axis values */
static int js_axis_y = 0;
static int js_stick_dir = -1;		/* BTN_* currently held, or -1 */
static int js_hat_dir = -1;

/* The eight directions, indexed by sector, starting at right and going CCW. */
static const int js_sector_btn[8] = {
	BTN_RIGHT, BTN_UR, BTN_UP, BTN_UL,
	BTN_LEFT, BTN_DL, BTN_DOWN, BTN_DR
};


static int js_dir_from_axes(int x, int y, int current)
{
	double mag = sqrt((double)x * x + (double)y * y);

	/*
	 * A lower threshold to let go than to engage, so that a stick resting
	 * right at the edge of the deadzone stays decided one way or the other.
	 */
	double limit = 32767.0 * (current < 0 ? prefs->js_deadzone :
			prefs->js_deadzone - KOBO_JS_DEADZONE_HYST) / 100.0;
	if(mag < limit)
		return -1;

	/*
	 * Screen Y grows downwards, so negate it to get a conventional angle.
	 * Working in units of 45 degrees makes a sector index a simple round,
	 * and gives each direction an equal share -- unlike a per-axis
	 * threshold, which hands the diagonals a larger slice.
	 */
	double a = atan2(-(double)y, (double)x) / (M_PI / 4.0);
	if(a < 0.0)
		a += 8.0;

	/* Hold the current direction until the stick clears the boundary. */
	if(current >= 0)
		for(int i = 0; i < 8; ++i)
			if(js_sector_btn[i] == current)
			{
				double d = a - i;
				while(d > 4.0)
					d -= 8.0;
				while(d < -4.0)
					d += 8.0;
				if(fabs(d) < 0.5 + KOBO_JS_SECTOR_HYST)
					return current;
				break;
			}

	return js_sector_btn[(int)floor(a + 0.5) & 7];
}


/*
 * D-pad delivered as four separate buttons rather than a hat.
 *
 * This is how Android presents it: Android_AddJoystick() folds any hat the
 * device reports into D-pad buttons and then sets the hat count to zero
 * (SDL_sysjoystick.c), so SDL_JOYHATMOTION never arrives there at all and the
 * hat path below would leave the D-pad completely dead.
 *
 * The four buttons are independent, so their held state is accumulated into a
 * mask and decoded as a unit -- otherwise a diagonal, which is two separate
 * button events, could never produce a diagonal direction.
 */
#define	JS_DPAD_UP	1
#define	JS_DPAD_DOWN	2
#define	JS_DPAD_LEFT	4
#define	JS_DPAD_RIGHT	8

static int js_dpad_mask = 0;

/*
 * Latched input state, decoded once per frame by js_decode_frame().
 *
 * Events must not decode a direction as they arrive. One physical movement
 * can span several events -- a hat that SDL has split into four D-pad
 * buttons sends a release and a press for UR -> UP, and the two stick axes
 * are always reported separately -- so decoding per event emits intermediate
 * directions the player never selected. Measured over one session, 34.9% of
 * D-pad transitions were an immediate A -> B -> A reversal, some of them in
 * the same millisecond.
 *
 * This matters most on Android, where SDL reports no hats at all and the
 * D-pad arrives ONLY as those four buttons.
 */
static Uint8 js_hat_value = SDL_HAT_CENTERED;


/* The mask bit for a button index, or 0 if it is not a D-pad button. */
static int js_dpad_bit(int button)
{
	if(button < 0)
		return 0;	/* -1 means "no such binding" */
	if(button == prefs->js_dpad_up)
		return JS_DPAD_UP;
	if(button == prefs->js_dpad_down)
		return JS_DPAD_DOWN;
	if(button == prefs->js_dpad_left)
		return JS_DPAD_LEFT;
	if(button == prefs->js_dpad_right)
		return JS_DPAD_RIGHT;
	return 0;
}


static int js_dir_from_dpad(int mask)
{
	/* Opposite directions cancel, as they do on a physical hat. */
	if((mask & (JS_DPAD_UP | JS_DPAD_DOWN)) == (JS_DPAD_UP | JS_DPAD_DOWN))
		mask &= ~(JS_DPAD_UP | JS_DPAD_DOWN);
	if((mask & (JS_DPAD_LEFT | JS_DPAD_RIGHT)) ==
			(JS_DPAD_LEFT | JS_DPAD_RIGHT))
		mask &= ~(JS_DPAD_LEFT | JS_DPAD_RIGHT);

	switch(mask)
	{
	  case JS_DPAD_UP:			return BTN_UP;
	  case JS_DPAD_DOWN:			return BTN_DOWN;
	  case JS_DPAD_LEFT:			return BTN_LEFT;
	  case JS_DPAD_RIGHT:			return BTN_RIGHT;
	  case JS_DPAD_UP | JS_DPAD_LEFT:	return BTN_UL;
	  case JS_DPAD_UP | JS_DPAD_RIGHT:	return BTN_UR;
	  case JS_DPAD_DOWN | JS_DPAD_LEFT:	return BTN_DL;
	  case JS_DPAD_DOWN | JS_DPAD_RIGHT:	return BTN_DR;
	  default:				return -1;	/* centred */
	}
}


/*
 * Game controller mappings.
 *
 * Unlike the js_* preferences, these need no per-device or per-platform
 * tuning: SDL_CONTROLLER_BUTTON_A is the A button on any pad SDL can map, so
 * there is nothing to guess and nothing for a player to correct -- which is
 * the whole point on a device that is launched from a TV remote.
 */
static int gc_dpad_bit(int button)
{
	switch(button)
	{
	  case SDL_CONTROLLER_BUTTON_DPAD_UP:		return JS_DPAD_UP;
	  case SDL_CONTROLLER_BUTTON_DPAD_DOWN:		return JS_DPAD_DOWN;
	  case SDL_CONTROLLER_BUTTON_DPAD_LEFT:		return JS_DPAD_LEFT;
	  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:	return JS_DPAD_RIGHT;
	  default:					return 0;
	}
}


static int gc_action(int button)
{
	switch(button)
	{
	  /* Both face buttons fire; neither may open the pause menu. */
	  case SDL_CONTROLLER_BUTTON_A:
	  case SDL_CONTROLLER_BUTTON_X:		return BTN_FIRE;

	  /* B is the conventional cancel, and the way out of every screen. */
	  case SDL_CONTROLLER_BUTTON_B:		return BTN_EXIT;

	  case SDL_CONTROLLER_BUTTON_BACK:
	  case SDL_CONTROLLER_BUTTON_START:	return BTN_PAUSE;

	  default:				return -1;
	}
}


static int js_dir_from_hat(Uint8 hat)
{
	switch(hat)
	{
	  case SDL_HAT_UP:		return BTN_UP;
	  case SDL_HAT_DOWN:		return BTN_DOWN;
	  case SDL_HAT_LEFT:		return BTN_LEFT;
	  case SDL_HAT_RIGHT:		return BTN_RIGHT;
	  case SDL_HAT_LEFTUP:		return BTN_UL;
	  case SDL_HAT_RIGHTUP:		return BTN_UR;
	  case SDL_HAT_LEFTDOWN:	return BTN_DL;
	  case SDL_HAT_RIGHTDOWN:	return BTN_DR;
	  default:			return -1;	/* centred */
	}
}


/*
 * Emit press/release edges for a change of direction. Releasing the old
 * direction before pressing the new one matters: without it a held direction
 * is left stuck down when the stick crosses from one sector to another.
 */
static const char *js_dir_name(int dir)
{
	switch(dir)
	{
	  case BTN_UP:		return "UP";
	  case BTN_DOWN:	return "DOWN";
	  case BTN_LEFT:	return "LEFT";
	  case BTN_RIGHT:	return "RIGHT";
	  case BTN_UL:		return "UL";
	  case BTN_UR:		return "UR";
	  case BTN_DL:		return "DL";
	  case BTN_DR:		return "DR";
	  default:		return "centre";
	}
}


/* Defined below, next to the direction helpers it belongs with. */
static void js_set_dir(const char *source, int *current, int newdir);


/*
 * Decode the latched input state into at most one direction change per source,
 * per frame.
 *
 * Everything above only records what the hardware reported; this is the single
 * place a direction is derived and press/release edges are emitted. That is
 * what stops one physical movement -- which can arrive as several events --
 * from producing intermediate directions nobody asked for.
 */
static void js_decode_frame(void)
{
	/*
	 * A real hat reports an absolute position, so it wins outright when one
	 * is present. Where SDL gives no hat (Android), js_hat_value stays
	 * centred and the four D-pad buttons are the only source.
	 */
	int dpad = js_hat_value != SDL_HAT_CENTERED ?
			js_dir_from_hat(js_hat_value) :
			js_dir_from_dpad(js_dpad_mask);

	js_set_dir("D-pad", &js_hat_dir, dpad);
	js_set_dir("stick", &js_stick_dir,
			js_dir_from_axes(js_axis_x, js_axis_y, js_stick_dir));
}


/*
 * Auto-repeat for a held direction, in menus and forms.
 *
 * A keyboard gets this free from the OS, but a joystick direction is
 * synthesized from a single physical transition, so without this every step of
 * a long list or a numeric value costs one discrete press. That is merely
 * tedious with a keyboard to fall back on, and close to unusable on a TV box
 * where the pad is the only input device.
 *
 * Repeats go to the game state machine only, never to gamecontrol_t: in-game
 * movement is driven by the held direction that is already latched there, and
 * re-pressing it would fight that. This also makes the repeat harmless during
 * play, because st_game_t::press() does not act on directions at all -- so
 * there is no need to detect "are we in a menu", which would otherwise have to
 * be kept in step with every state in the game.
 */
#define	KOBO_JS_UI_REPEAT_DELAY		375	/* ms before the first repeat */
#define	KOBO_JS_UI_REPEAT_INTERVAL	80	/* ms between repeats */

static int	js_repeat_dir = -1;
static Uint32	js_repeat_due = 0;


static void js_ui_repeat(void)
{
	/* The D-pad wins if both are pushed; it is the more deliberate input. */
	int dir = js_hat_dir >= 0 ? js_hat_dir : js_stick_dir;
	Uint32 now = SDL_GetTicks();

	if(dir < 0)
	{
		js_repeat_dir = -1;
		return;
	}

	if(dir != js_repeat_dir)
	{
		/*
		 * Newly held. The press edge has already been delivered by
		 * js_set_dir(), so only arm the timer here. Re-derived from the
		 * live direction every frame rather than tracked by events, so
		 * that anything which clears the direction without an event --
		 * a state change, a lost device -- cannot leave a repeat
		 * running against a direction nobody is holding.
		 */
		js_repeat_dir = dir;
		js_repeat_due = now + KOBO_JS_UI_REPEAT_DELAY;
		return;
	}

	if(now < js_repeat_due)
		return;

	js_repeat_due = now + KOBO_JS_UI_REPEAT_INTERVAL;
	gsm.press(dir);
	log_printf(DLOG, "Joystick UI repeat: %s\n", js_dir_name(dir));
}


static void js_set_dir(const char *source, int *current, int newdir)
{
	if(*current == newdir)
		return;

	if(*current >= 0)
	{
		gamecontrol.release(*current);
		gsm.release(*current);
	}
	if(newdir >= 0)
	{
		gamecontrol.press(newdir);
		gsm.press(newdir);
	}
	log_printf(DLOG, "Joystick %s: %s -> %s\n", source,
			js_dir_name(*current), js_dir_name(newdir));
	*current = newdir;
}


void kobo_gfxengine_t::frame()
{
	sound.frame();

	if(!gsm.current())
	{
		log_printf(CELOG, "INTERNAL ERROR: No gamestate!\n");
		exit_game = 1;
		stop();
		return;
	}
	if(exit_game || manage.aborted())
	{
		stop();
		return;
	}

	/*
	 * Process input
	 */
	SDL_Event ev;
	while(SDL_PollEvent(&ev))
	{
		int k, ms;
		switch (ev.type)
		{
		  case SDL_KEYDOWN:
			switch(ev.key.keysym.sym)
			{
#ifdef PROFILE_AUDIO
			  case SDLK_F10:
				++km.audio_vismode;
				break;
			  case SDLK_F12:
				audio_print_info();
				break;
			  case SDLK_r:
			  {
				audio_channel_stop(-1, -1);
				int startt = SDL_GetTicks();
				audio_wave_load(0, "sfx.agw", 0);
				log_printf(VLOG, "(Loading + processing time: %d ms)\n",
						SDL_GetTicks() - startt);
				break;
			  }
#endif
			  case SDLK_DELETE:
				if(prefs->cmd_debug)
				{
					manage.ships = 1;
					myship.hit(1000);
				}
				break;
			  case SDLK_RETURN:
				ms = SDL_GetModState();
				if(ms & (KMOD_CTRL | KMOD_SHIFT | KMOD_META))
					break;
				if(!(ms & KMOD_ALT))
					break;
				km.pause_game();
				prefs->fullscreen = !prefs->fullscreen;
				prefs->changed = 1;
				global_status |= OS_RELOAD_GRAPHICS |
						OS_RESTART_VIDEO;
				stop();
				return;
			  case SDLK_PRINT:
			  case SDLK_SYSREQ:
// FIXME: Doesn't this trigger when entering names and stuff...?
			  case SDLK_s:
				gengine->screenshot();
				break;
			  default:
				break;
			}
			k = gamecontrol.map(ev.key.keysym.sym);
			gamecontrol.press(k);
			gsm.press(k, kobo12_KeysymChar(&ev.key.keysym));
			break;
		  case SDL_KEYUP:
			if((ev.key.keysym.sym == SDLK_ESCAPE) && km.escape_hammering())
			{
				km.pause_game();
				prefs->fullscreen = 0;
				prefs->videodriver = (int)GFX_DRIVER_SDL2D;
				prefs->width = 640;
				prefs->height = 480;
				prefs->aspect = 1000;
				prefs->depth = 0;
				prefs->doublebuf = 0;
				prefs->pages = -1;
				prefs->shadow = 1;
				prefs->scalemode = (int)GFX_SCALE_NEAREST;
				prefs->brightness = 100;
				prefs->contrast = 100;
				global_status |= OS_RELOAD_GRAPHICS |
						OS_RESTART_VIDEO;
				stop();
				st_error.message("Safe video settings applied!",
					"Enter Options menu to save.");
				gsm.push(&st_error);
				return;
			}
			k = gamecontrol.map(ev.key.keysym.sym);
			if(k == SDLK_PAUSE)
			{
				gamecontrol.press(BTN_PAUSE);
				gsm.press(BTN_PAUSE);
			}
			else
			{
				gamecontrol.release(k);
				gsm.release(k);
			}
			break;
		  case SDL_WINDOWEVENT:
			// SDL 2 folded SDL_VIDEOEXPOSE and SDL_ACTIVEEVENT into
			// this one event with a sub-type.
			switch(ev.window.event)
			{
			  case SDL_WINDOWEVENT_EXPOSED:
				gengine->invalidate();
				break;
			  // Any type of focus loss should activate pause mode!
			  //
			  // Note this is a deliberate behaviour change. The 1.2
			  // code tested "!gain && state != SDL_APPINPUTFOCUS",
			  // which paused on losing *mouse* focus but NOT on
			  // losing keyboard focus -- the opposite of what its
			  // own comment says, and almost certainly not what was
			  // meant. SDL 2 has no equivalent of that bitmask, and
			  // reproducing the quirk would mean a game that keeps
			  // running while you alt-tab away from it. Pausing on
			  // real focus loss is what the comment asks for and
			  // what a player expects on both a Mac and a TV box.
			  case SDL_WINDOWEVENT_FOCUS_LOST:
			  case SDL_WINDOWEVENT_MINIMIZED:
			  case SDL_WINDOWEVENT_HIDDEN:
				km.pause_game();
				break;
			  default:
				break;
			}
			break;
		  case SDL_QUIT:
			/*gsm.press(BTN_CLOSE);*/
			km.brutal_quit();
			break;
		  case SDL_CONTROLLERBUTTONDOWN:
		  case SDL_CONTROLLERBUTTONUP:
		  {
			int down = (ev.type == SDL_CONTROLLERBUTTONDOWN);
			int bit = gc_dpad_bit(ev.cbutton.button);
			int act;
			if(bit)
			{
				if(down)
					js_dpad_mask |= bit;
				else
					js_dpad_mask &= ~bit;
				break;		/* decoded once per frame */
			}
			act = gc_action(ev.cbutton.button);
			if(act >= 0)
			{
				if(down)
				{
					gamecontrol.press(act);
					gsm.press(act);
				}
				else
				{
					gamecontrol.release(act);
					gsm.release(act);
				}
			}
			log_printf(DLOG, "Controller \"%s\" %s\n",
					SDL_GameControllerGetStringForButton(
					(SDL_GameControllerButton)
					ev.cbutton.button),
					down ? "down" : "up");
			break;
		  }

		  case SDL_CONTROLLERAXISMOTION:
			if(ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
				js_axis_x = ev.caxis.value;
			else if(ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
				js_axis_y = ev.caxis.value;
			else
				break;		/* right stick, triggers */
			break;			/* decoded once per frame */

		  case SDL_JOYDEVICEADDED:
			/*
			 * NOTE: 'which' is a DEVICE INDEX in this event, but an
			 * INSTANCE ID in SDL_JOYDEVICEREMOVED below. Same field,
			 * different namespace, and they only coincide for the
			 * first device of a fresh process.
			 *
			 * SDL also queues one of these for every device already
			 * present at startup, so the check for an open device is
			 * what stops the pad being opened twice.
			 */
			if(!km.joystick)
			{
				log_printf(VLOG, "Joystick added (device index"
						" %d)\n", ev.jdevice.which);
				prefs->joystick_no = ev.jdevice.which;
				KOBO_main::init_js(prefs);
			}
			break;

		  case SDL_JOYDEVICEREMOVED:
			if(km.joystick &&
					(ev.jdevice.which == km.js_instance))
			{
				log_printf(WLOG, "Joystick removed (instance"
						" %d) - this is the device in"
						" use!\n", ev.jdevice.which);

				/*
				 * Drop any held direction BEFORE closing, or
				 * the ship keeps flying with nothing left to
				 * steer it. SDL zeroes axis values just before
				 * sending this event, so the held state may
				 * never be observed any other way.
				 */
				js_set_dir("stick", &js_stick_dir, -1);
				js_set_dir("D-pad", &js_hat_dir, -1);
				js_axis_x = js_axis_y = 0;
				js_dpad_mask = 0;
				js_hat_value = SDL_HAT_CENTERED;

				/*
				 * Close the device but NOT the subsystem --
				 * quitting it would stop the "added" events
				 * that make replugging work.
				 */
				if(km.gamepad)
				{
					SDL_GameControllerClose(km.gamepad);
					km.gamepad = NULL;
				}
				SDL_JoystickClose(km.joystick);
				km.joystick = NULL;
				km.js_instance = -1;

				/*
				 * One USB port on the target, and the pad is
				 * in it. Losing it mid-game would otherwise
				 * leave the player under fire with no input.
				 */
				if(!manage.game_stopped())
					km.pause_game();
			}
			break;

		  case SDL_JOYBUTTONDOWN:
			/*
			 * SDL sends both controller and raw joystick
			 * events for the same device, so ignore these
			 * whenever the controller layer is handling it.
			 */
			if(km.gamepad)
				break;
			/*
			 * D-pad first, and consumed here: on Android these ARE
			 * the D-pad, and must not fall through to the action
			 * bindings below.
			 */
			if(js_dpad_bit(ev.jbutton.button))
			{
				js_dpad_mask |= js_dpad_bit(ev.jbutton.button);
				log_printf(DLOG, "Joystick button %d down"
						" (D-pad)\n", ev.jbutton.button);
				break;
			}
			if(ev.jbutton.button == prefs->js_fire)
			{
				gamecontrol.press(BTN_FIRE);
				gsm.press(BTN_FIRE);
			}
			else if(ev.jbutton.button == prefs->js_fire2)
			{
				/*
				 * A second fire button, NOT BTN_START.
				 *
				 * st_game_t::press() treats BTN_START exactly
				 * like BTN_PAUSE, so while this button was
				 * mapped to BTN_START it opened the pause menu
				 * in the middle of play. BTN_FIRE is what a
				 * face button should do in a shooter, and menus
				 * accept it to activate the selected entry
				 * (st_menu_base_t::press()), so it stays useful
				 * outside the game too.
				 */
				gamecontrol.press(BTN_FIRE);
				gsm.press(BTN_FIRE);
			}
			else if((ev.jbutton.button == prefs->js_pause) ||
					(ev.jbutton.button == prefs->js_menu))
			{
				/* Start and Back both open the pause menu. */
				gamecontrol.press(BTN_PAUSE);
				gsm.press(BTN_PAUSE);
			}
			else if(ev.jbutton.button == prefs->js_exit)
			{
				gamecontrol.press(BTN_EXIT);
				gsm.press(BTN_EXIT);
			}
			log_printf(DLOG, "Joystick button %d down\n",
					ev.jbutton.button);
			break;
		  case SDL_JOYBUTTONUP:
			/*
			 * SDL sends both controller and raw joystick
			 * events for the same device, so ignore these
			 * whenever the controller layer is handling it.
			 */
			if(km.gamepad)
				break;
			if(js_dpad_bit(ev.jbutton.button))
			{
				js_dpad_mask &= ~js_dpad_bit(ev.jbutton.button);
				break;		/* decoded once per frame */
			}
			if(ev.jbutton.button == prefs->js_fire)
			{
				gamecontrol.release(BTN_FIRE);
				gsm.release(BTN_FIRE);
			}
			else if(ev.jbutton.button == prefs->js_fire2)
			{
				/*
				 * Upstream never released this button at all,
				 * so gamecontrol_t's state bitmask kept the bit
				 * set for the rest of the session.
				 */
				gamecontrol.release(BTN_FIRE);
				gsm.release(BTN_FIRE);
			}
			else if((ev.jbutton.button == prefs->js_pause) ||
					(ev.jbutton.button == prefs->js_menu))
			{
				gamecontrol.release(BTN_PAUSE);
				gsm.release(BTN_PAUSE);
			}
			else if(ev.jbutton.button == prefs->js_exit)
			{
				gamecontrol.release(BTN_EXIT);
				gsm.release(BTN_EXIT);
			}
			break;
		  case SDL_JOYAXISMOTION:
			/*
			 * SDL sends both controller and raw joystick
			 * events for the same device, so ignore these
			 * whenever the controller layer is handling it.
			 */
			if(km.gamepad)
				break;
			/*
			 * Remember this axis and re-derive the direction from
			 * both of them. Deciding from one axis at a time is what
			 * let a single diagonal push emit two directions.
			 */
			if(ev.jaxis.axis == prefs->js_lr)
				js_axis_x = ev.jaxis.value;
			else if(ev.jaxis.axis == prefs->js_ud)
				js_axis_y = ev.jaxis.value;
			else
				break;		/* an axis we do not use */
			break;			/* decoded once per frame */	/* upstream fell through into SDL_MOUSEMOTION
				 * here, which then read ev.motion.x/y out of
				 * what is really an SDL_JoyAxisEvent -- feeding
				 * garbage coordinates to the mouse handler on
				 * every single stick sample. */
		  case SDL_JOYHATMOTION:
			/*
			 * SDL sends both controller and raw joystick
			 * events for the same device, so ignore these
			 * whenever the controller layer is handling it.
			 */
			if(km.gamepad)
				break;
			/*
			 * D-pad. Upstream had no hat handling whatsoever, so the
			 * D-pad did nothing at all. All eight positions are
			 * decoded, exactly as the numpad keys are.
			 */
			if(ev.jhat.hat != prefs->js_hat)
				break;		/* a hat we do not use */
			js_hat_value = ev.jhat.value;
			break;			/* decoded once per frame */
		  case SDL_MOUSEMOTION:
			mouse_x = (int)(ev.motion.x / gengine->xscale()) - km.xoffs;
			mouse_y = (int)(ev.motion.y / gengine->yscale()) - km.yoffs;
			if(prefs->use_mouse)
				gamecontrol.mouse_position(
						mouse_x - 8 - MARGIN - WSIZE/2,
						mouse_y - MARGIN - WSIZE/2);
			break;
		  case SDL_MOUSEBUTTONDOWN:
			mouse_x = (int)(ev.motion.x / gengine->xscale()) - km.xoffs;
			mouse_y = (int)(ev.motion.y / gengine->yscale()) - km.yoffs;
			gsm.press(BTN_FIRE);
			if(prefs->use_mouse)
			{
				gamecontrol.mouse_position(
						mouse_x - 8 - MARGIN - WSIZE/2,
						mouse_y - MARGIN - WSIZE/2);
				switch(ev.button.button)
				{
				  case SDL_BUTTON_LEFT:
					mouse_left = 1;
					break;
				  case SDL_BUTTON_MIDDLE:
					mouse_middle = 1;
					break;
				  case SDL_BUTTON_RIGHT:
					mouse_right = 1;
					break;
				}
				gamecontrol.press(BTN_FIRE);
			}
			break;
		  case SDL_MOUSEBUTTONUP:
			mouse_x = (int)(ev.motion.x / gengine->xscale()) - km.xoffs;
			mouse_y = (int)(ev.motion.y / gengine->yscale()) - km.yoffs;
			if(prefs->use_mouse)
			{
				gamecontrol.mouse_position(
						mouse_x - 8 - MARGIN - WSIZE/2,
						mouse_y - MARGIN - WSIZE/2);
				switch(ev.button.button)
				{
				  case SDL_BUTTON_LEFT:
					mouse_left = 0;
					break;
				  case SDL_BUTTON_MIDDLE:
					mouse_middle = 0;
					break;
				  case SDL_BUTTON_RIGHT:
					mouse_right = 0;
					break;
				}
			}
			if(!mouse_left && !mouse_middle && !mouse_right)
			{
				if(prefs->use_mouse)
					gamecontrol.release(BTN_FIRE);
				gsm.release(BTN_FIRE);
			}
			break;
		}
	}
	gamecontrol.process();

	/* One direction change per source per frame, from the latched state. */
	js_decode_frame();

	/* Auto-repeat a held joystick direction into the UI. */
	js_ui_repeat();

	/*
	 * Run the current gamestate for one frame
	 */
	gsm.frame();

	if(prefs->cmd_autoshot && !manage.game_stopped())
	{
		static int c = 0;
		++c;
		if(c >= 3)
		{
			gengine->screenshot();
			c = 0;
		}
	}
}


void kobo_gfxengine_t::pre_render()
{
	sound.run();
	gsm.pre_render();
}


void kobo_gfxengine_t::post_render()
{
	gsm.post_render();

	if(!prefs->cmd_noframe)
	{
		wmain->sprite(0, 0, B_FRAME_TL, 0, 0);
		wmain->sprite(WSIZE - 16, 0, B_FRAME_TR, 0, 0);
		wmain->sprite(0, WSIZE - 16, B_FRAME_BL, 0, 0);
		wmain->sprite(WSIZE - 16, WSIZE - 16, B_FRAME_BR, 0, 0);
	}

#ifdef DEBUG
	if(prefs->cmd_debug)
	{
		char buf[20];
		snprintf(buf, sizeof(buf), "Obj: %d",
				gengine->objects_in_use());
		wmain->font(B_NORMAL_FONT);
		wmain->string(160, 5, buf);
	}

	switch (km.audio_vismode % 5)
	{
	  case 0:
		break;
	  case 1:
		draw_osc(0);
		break;
	  case 2:
		draw_vu();
		break;
	  case 3:
		draw_osc(1);
		break;
	  case 4:
		draw_osc(2);
		break;
	}
#endif

	// Frame rate counter
	int nt = (int)SDL_GetTicks();
	int tt = nt - km.fps_starttime;
	if((tt > 1000) && km.fps_count)
	{
		float f = km.fps_count * 1000.0 / tt;
		::screen.fps(f);
		km.fps_count = 0;
		km.fps_starttime = nt;
		if(prefs->cmd_fps)
		{
			char buf[20];
			snprintf(buf, sizeof(buf), "%.1f", f);
			km.dfps->text(buf);
			if(!km.fps_results)
				km.fps_results = (float *)
						calloc(MAX_FPS_RESULTS,
						sizeof(float));
			if(km.fps_results)
			{
				km.fps_results[km.fps_nextresult++] = f;
				if(km.fps_nextresult >= MAX_FPS_RESULTS)
					km.fps_nextresult = 0;
				if(km.fps_nextresult > km.fps_lastresult)
					km.fps_lastresult = km.fps_nextresult;
			}
		}
	}
	++km.fps_count;

	// Frame rate limiter
	if(prefs->max_fps)
	{
		if(prefs->max_fps_strict)
		{
			static double nextframe = -1000000.0f;
			while(1)
			{
				double t = (double)SDL_GetTicks();
				if(fabs(nextframe - t) > 1000.0f)
					nextframe = t;
				double d = nextframe - t;
				if(d > 10.0f)
					SDL_Delay(10);
				else if(d > 1.0f)
					SDL_Delay(1);
				else
					break;
			}
			nextframe += 1000.0f / prefs->max_fps;
		}
		else
		{
			int rtime = nt - km.max_fps_begin;
			km.max_fps_begin = nt;
			km.max_fps_filter += (float)(rtime - km.max_fps_filter) * 0.3;
			int delay = (int)(1000.0 / prefs->max_fps -
					km.max_fps_filter + 0.5);
			if((delay > 0) && (delay < 1100))
				SDL_Delay(delay);
			km.max_fps_begin = (int)SDL_GetTicks();
		}
	}
}


/*----------------------------------------------------------
	main() and related stuff
----------------------------------------------------------*/

static void put_usage()
{
	printf("\nKobo Deluxe %s\n", KOBO_VERSION_STRING);
	printf("Usage: kobodl [<options>]\n");
	printf("Recognized options:\n");
	int s = -1;
	while(1)
	{
		const char *fs;
		s = prefs->find_next(s);
		if(s < 0)
			break;
		switch(prefs->type(s))
		{
		  case CFG_BOOL:
			fs = "	-[no]%-16.16s[%s]\t%s%s\n";
			break;
		  case CFG_INT:
		  case CFG_FLOAT:
			fs = "	-%-20.20s[%s]\t%s%s\n";
			break;
		  case CFG_STRING:
			fs = "	-%-20.20s[\"%s\"]\t%s%s\n";
			break;
		  default:
			continue;
		}
		printf(fs, prefs->name(s), prefs->get_default_s(s),
				prefs->do_save(s) ? "" : "(Not saved!) ",
				prefs->description(s));
	}
}


static void put_options_man()
{
	int s = -1;
	while(1)
	{
		const char *fs;
		s = prefs->find_next(s);
		if(s < 0)
			break;
		switch(prefs->type(s))
		{
		  case CFG_BOOL:
			fs = ".TP\n.B \\-[no]%s\n%s%s. Default: %s.\n";
			break;
		  case CFG_INT:
		  case CFG_FLOAT:
			fs = ".TP\n.B \\-%s\n%s%s. Default: %s.\n";
			break;
		  case CFG_STRING:
			fs = ".TP\n.B \\-%s\n%s%s. Default: \"%s\"\n";
			break;
		  default:
			continue;
		}
		printf(fs, prefs->name(s),
				prefs->do_save(s) ? "" : "(Not saved!) ",
				prefs->description(s),
				prefs->get_default_s(s));
	}
}


extern "C" void emergency_close(void)
{
	km.close();
}


extern "C" RETSIGTYPE breakhandler(int dummy)
{
	/* For platforms that drop the handlers on the first signal... */
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);
	km.brutal_quit();
#if (RETSIGTYPE != void)
	return 0;
#endif
}


int main(int argc, char *argv[])
{
	int cmd_exit = 0;

	/*
	 * Line buffering, before anything can be logged.
	 *
	 * Block buffering loses exactly the startup diagnostics you need when
	 * the game dies during init, and it interleaves badly with unbuffered
	 * stderr when both go to one pipe. It matters more on the Android
	 * target, where stdout goes to logcat.
	 */
	setvbuf(stdout, NULL, _IOLBF, 0);

#ifdef __ANDROID__
	/*
	 * Deliver the Back button to the game instead of letting Android act
	 * on it.
	 *
	 * On a TV box the app is launched from the remote, and the remote's
	 * Back key is an ordinary Android Back: untrapped, it finishes the
	 * activity, so pressing it anywhere in the game quits outright rather
	 * than leaving the current screen. Trapped, it arrives as SDLK_AC_BACK
	 * and gamecontrol_t::map() turns it into BTN_EXIT, which is what backs
	 * out of a screen everywhere else.
	 */
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif

	atexit(emergency_close);
	signal(SIGTERM, breakhandler);
	signal(SIGINT, breakhandler);

	SDL_Init(0);

	if(main_init())
	{
		fprintf(stderr, "INTERNAL ERROR\n");
		return 1;
	};

	km.open_logging(NULL);

	setup_dirs(argv[0]);
	--argc;
	++argv;

	if((argc < 1) || (strcmp("-override", argv[0]) != 0))
		km.load_config(prefs);

	if((prefs->parse(argc, argv) < 0) || prefs->cmd_help)
	{
		put_usage();
		main_cleanup();
		return 1;
	}

	if(prefs->cmd_options_man)
	{
		put_options_man();
		main_cleanup();
		return 1;
	}

	if(prefs->cmd_noparachute)
	{
		SDL_Quit();
		SDL_Init(SDL_INIT_NOPARACHUTE);
	}

	km.open_logging(prefs);

	for(int a = 0; a < argc; ++a)
		log_printf(DLOG, "argv[%d] = \"%s\"\n", a, argv[a]);

	int k = -1;
	while((k = prefs->find_next(k)) >= 0)
	{
		log_printf(D3LOG, "key %d: \"%s\"\ttype=%d\t\"%s\"\n",
				k,
				prefs->name(k),
				prefs->type(k),
				prefs->description(k)
			);
	}

	add_dirs(prefs);

	if(prefs->cmd_hiscores)
	{
		scorefile.gather_high_scores();
		scorefile.print_high_scores();
		cmd_exit = 1;
	}

	if(prefs->cmd_showcfg)
	{
		printf("Configuration:\n");
		printf("----------------------------------------\n");
		prefs->write(stdout);
		printf("\nPaths:\n");
		printf("----------------------------------------\n");
		fmap->print(stdout, "*");
		printf("----------------------------------------\n");
		cmd_exit = 1;
	}

	if(cmd_exit)
	{
		km.close_logging();
		main_cleanup();
		return 0;
	}

	if(km.open() < 0)
	{
		km.close_logging();
		main_cleanup();
		return 1;
	}

	km.run();

	km.close();

	/* 
	 * Seems like we got all the way here without crashing,
	 * so let's save the current configuration! :-)
	 */
	if(prefs->changed)
	{
		km.save_config(prefs);
		prefs->changed = 0;
	}

	if(prefs->cmd_fps)
		km.print_fps_results();

	km.close_logging();
	main_cleanup();
	return 0;
}
