# UI and HUD

## Purpose

Race ships a complete native menu (ObjectivelyMVC, JSON-driven views) and a
purpose-built HUD with a set of movement-training tools. This document maps
what each component *is* in the architecture — not how to click through it.

## Ownership

All CGAME. The UI issues console commands and renders server state; it grants
no authority of its own.

## The native UI

### How it is put together

Common CGAME still creates and pushes the main view controller — but the Race
module supplies its own `MainViewController` / `MainView` /
`LoadingViewController` implementations, selected ahead of common's by the
build (include-path order and project files). Every screen follows one
pattern: a JSON view resource (structure + identifiers), a CSS stylesheet
(theme), and a C controller that resolves named outlets and binds behavior.
Structural integrity is enforced by `src/tools/verify_race_ui.py`, which
checks that every declared outlet exists in its JSON, that resources resolve,
that required lifecycle markers are present in the sources, and that retired
constructs do not reappear.

### The shell

`MainViewController` + `MainView` form a fixed chrome frame (top bar with the
QUETOO // RACE lockup and route strip, a window header with map/session
metrics, a bottom bar with server info and commit status) around a
`NavigationViewController` that hosts one route at a time. The route strip
is exactly: **Home, Play, Controls, Settings, Maps, Credits, Admin** — Admin
appears only when the player's stat-published admin capabilities are
non-zero. Routes that edit cvars register an Apply/Revert delegate with the
shell footer. `MainView::render` is the live-refresh seam: while the menu is
open it pumps `Cg_Module_UpdateUi` each frame so admin visibility, vote
state, and chrome text stay current.

Responsive layout is code-driven (the stylesheet dialect has no `clamp()` or
viewport units): `ColumnsView` reflows authored columns into as many vertical
tracks as fit, a width-collapse pass re-fluidizes fixed desktop widths, page
content is wrapped in scroll views with a reserved scrollbar rail, and all
window resize/scale events trigger a relayout that restores focus.

### The screens

| Screen | Role |
|---|---|
| **Home** | Disconnected panel, or the connected dashboard: session summary, mode buttons (Race/Practice/Spectator), world record and top records, live roster. |
| **Play** | Join server (browser with filters), Create server (including the physics preset selector), and Runner (player setup) tabs. |
| **Controls** | Key binding roster (movement/combat pages), crosshair preview, restore-defaults, Apply/Revert. |
| **Settings** | Client settings pages: Display, Lighting, Effects, Audio, Mouse, Hud, Strafe (with a live strafe-helper preview), Network. |
| **Maps** | The map browser: all/personal scopes, filter, pager, per-map detail with top times and "Watch world record". |
| **Voting** | Live vote block, proposal builders (physics/map/kick), next-map choices and nomination. |
| **Credits** | Race, Quetoo, and license credits. |
| **Admin** | Six tabs — Server, Rules, Players, Session, Advanced, Weapons — capability-gated rows issuing `gset`/`mset`/`race admin ...` commands, with a response block. |
| **WeaponLab** | Hosted inside Admin › Weapons: renders the 22 tunable weapon values from the synchronized cache, edits as a sparse local draft, applies as one batch (see [SETTINGS_SYSTEM.md](SETTINGS_SYSTEM.md)). |
| **Quick Settings** | The tier-1 ESC drawer: Resume, Restart run, Watch best, Spectate, Call vote, Full menu, Disconnect, Quit — each with the player's real keybind shown. |
| **Active vote strip** | A global overlay in the menu showing the live vote with Yes/No buttons. |
| **Loading** | The Race loading screen (map shot, lockup, progress rail) — ABI-compatible with the stock loader that allocates it. |

Supporting views: the animated `SpeedGridView` menu backdrop (driven by the
speed you were carrying), `LoadingGridView`, `RaceSlider`, `RosterSelect`,
`RaceBindTextView`. Assets: the Coda font, six background plates, the
console background, and the HUD input glyphs.

## The HUD

`cg_race_hud.c` draws everything in 1440p design pixels scaled to the actual
screen, with a named seven-step type scale. The elements:

- **Top stack** — mode tag, route line (map title + physics preset short
  name), the run timer (color-coded: green only when finished), and the
  WR / PB pair.
- **Checkpoint ribbon and pips** — per-checkpoint captured splits and a
  progress bar with `N / M checkpoints`.
- **Analytical split popup** — when the server sends a split event: cumulative
  and segment time with PB/WR deltas.
- **Speed readout** — smoothed horizontal speed under the crosshair
  (stands down when the strafe helper owns the readout).
- **Hyperblaster climb helper** — classifies the aimed surface distance
  against the climb range as `CLIMB` / `CLOSER` / `TOO FAR`.
- **Map clock**, weapon-tuning warning line, vote displays, the finish
  report banner, and the replay HUD.

Legibility treatments (`none`/`shadow`/`stroke`/`plates`), edge insets, and
timer size are cvar-controlled.

## Training tools

All client-side, fed by prediction and (during playback) replay telemetry:

| Tool | What it does |
|---|---|
| **Input viewer** | Bottom-left cluster showing effective inputs (attack, duck, jump, movement chevrons) for the local, chased, or replayed player. |
| **Jump viewer** | Tracks each jump's length, peak height, and air time; holds the last result briefly. |
| **Strafe helper** | The acceleration-zone bar with optimal-angle and center markers, plus optional speed / max-speed / velocity-angle readouts — heavily configurable (30+ cvars). |
| **Course markers** | BSP-derived 3D markers over start/checkpoints/finish (`cg_race_markers`). |
| **Practice markers** | Up to 128 private, per-map, player-placed markers (point/takeoff/landing/aim), persisted client-side; `marker_add`, `marker_remove`, `markers_clear` (two-step), `markers_save`, `markers_reload`. |
| **Double jump** | The `+double_jump` input macro (see [MOVEMENT_AND_PHYSICS.md](MOVEMENT_AND_PHYSICS.md)). |
| **Jumper hiding** | `cg_show_jumpers` / the `jumpers` command hide other players and their projectiles and sounds entirely. |

Each tool separates its decision math into a standalone header
(`cg_*_math.h`) so the logic is unit-testable without the engine.

The replay HUD and raceline rendering are covered in
[REPLAY_SYSTEM.md](REPLAY_SYSTEM.md); every tool's cvars are tabulated in
[CVAR_REFERENCE.md](CVAR_REFERENCE.md).

## Main source

```text
src/cgame/race/ui/main/        shell, quick settings, active vote, loading
src/cgame/race/ui/home|play|controls|settings|maps|voting|credits|admin/
src/cgame/race/cg_race_hud.c   src/cgame/race/cg_strafe_helper.c
src/cgame/race/cg_input_viewer.c   src/cgame/race/cg_jump_viewer.c
src/cgame/race/cg_race_markers.c   src/cgame/race/cg_race_practice_markers.c
src/cgame/race/cg_race_training.c  src/tools/verify_race_ui.py
```
