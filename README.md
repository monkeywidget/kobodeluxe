# Kobo Deluxe — macOS port

A fork of [Kobo Deluxe](https://github.com/olofson/kobodeluxe) that builds and
runs on modern macOS (Apple Silicon), with gamepad support.

The upstream tree is an SDL **1.2** application whose acceleration path is
immediate-mode desktop OpenGL. Neither survives on a current Mac: SDL 1.2 is
long unmaintained, and the `cmake_minimum_required(VERSION 2.8)` in the
upstream build is a hard error on CMake 4. This fork moves it to SDL 2.

## Lineage

```
1995-1996   XKobo         Akira Higuchi     X11
2001-2011   Kobo Deluxe   David Olofson     SDL 1.2 + glSDL
2026        this fork     monkeywidget      SDL 2, macOS
```

Kobo Deluxe is Higuchi's XKobo with X11 replaced by SDL and the original game
logic kept — including a "Classic" skill level that plays as the 1996 original
did. That fidelity is the reason this tree is worth porting, and preserving it
is the constraint the whole port was built around.

## The game logic is untouched, and you can check

24 files still carry Akira Higuchi's copyright: the level and stage data, the
enemy AI, the PRNG, the ship, the map. **19 of those 24 are byte-identical to
upstream.** The other five changed only where SDL 2 forced it, and each has a
recorded reason.

```
$ tools/port/check-fidelity.sh
fidelity: 19/24 game-logic files byte-identical to upstream, 5 changed with justification
```

The five, and why:

| File | Change |
|---|---|
| `manage.h`, `random.h`, `gamectl.h` | one `#include` line each — `glSDL.h` is gone |
| `gamectl.cpp` | `SDL_EnableKeyRepeat()` no longer exists in SDL 2 |
| `kobo.cpp` | SDL 2 event model, line-buffered logging, user-directory creation, gamepad support |

Nothing in that list alters gameplay, balance, or level content. The baseline
hashes are in `tools/port/fidelity-baseline.txt` and the justifications in
`tools/port/fidelity-allowed.txt`, so the claim is checkable rather than
asserted.

## What changed

**Renderer.** `glSDL` (2,328 lines of immediate-mode OpenGL) is deleted and
replaced by `src/graphics/kobo_sdl12.{c,h}`, which presents the same SDL 1.2
surface API over SDL 2 — a software surface uploaded to one streaming texture
through `SDL_Render`. The ~140 files above it compile unchanged.

**Gamepad support.** Upstream had two hardcoded buttons, no D-pad handling, and
an axis handler that fell through into the mouse handler on every sample. This
fork uses `SDL_GameController`, so button identities come from SDL's own
mapping database and need no configuration: A/X fire, B backs out, Back and
Start open the pause menu, D-pad and left stick both move. The stick uses a
radial deadzone with hysteresis, decoded once per frame.

**Upstream bugs fixed along the way**, all of which affected the shipped game:

- `make install` shipped **no sound data at all** — the install rules matched
  `*.a2s` (Kobo Redux's format) against Kobo Deluxe's 60 `*.agw` files.
- `make install` failed outright on any non-Windows host, asking for an icon
  file under a name that does not exist in the tree.
- `filemapper_t::mkdir()` was an unimplemented stub, so the user directory was
  never created and **no player profile or high score could be saved** on a
  machine that did not already have one.

## Building

Requires macOS on Apple Silicon, CMake 3.16+, SDL 2 and SDL2_image.

```sh
brew install sdl2 sdl2_image
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --build build -j8
cmake --install build
```

To produce a self-contained `.app` that runs on a Mac without Homebrew:

```sh
tools/port/bundle-macos.sh /your/prefix
```

That copies SDL 2 and SDL2_image into the bundle, rewrites the install names
and RPATHs to be relative to the executable, stages the game data into
`Contents/Resources`, and re-signs. It verifies its own work and refuses to
report success if anything still points outside the bundle.

## Running the release build

The released `.app` is **ad-hoc signed, not notarized**. macOS will refuse to
open it on first launch. Right-click the app and choose **Open**, then confirm
— after that it launches normally.

`tools/port/smoke-test.sh <prefix>` asserts on the game's own log that it
started, opened a display, loaded its sound data and reached the title screen.

## What is not covered

- **Apple Silicon only.** No Intel build is produced or tested.
- **Not notarized.** See above.

## Licence

GPL v2 — see [COPYING](COPYING). Copyright is held by, among others, Akira
Higuchi (1995-1996), David Olofson (2001-2011), Jeremy Sheeley, Erik
Auerswald, Simon Peter and Florian Schulze. Their notices are intact in the
files they wrote; this fork's changes are noted alongside them, not in place
of them.

Upstream's own documentation is preserved: [README](README) (the original),
[README.xkobo](README.xkobo) (Higuchi's), and the rest of the `README.*` files.
