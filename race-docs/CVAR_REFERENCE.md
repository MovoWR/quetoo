# Cvar Reference

All cvars below are verified against current source. "Owner" is the module
that registers the cvar. "Archived" means the client saves it across sessions
(`CVAR_ARCHIVE`). Race *settings* (`gset`/`mset` aliases) are a separate
system backed by some of these cvars — see
[SETTINGS_SYSTEM.md](SETTINGS_SYSTEM.md); this page lists the cvars
themselves.

## Server: physics

| Cvar | Owner | Default | Values / range | Purpose |
|---|---|---:|---|---|
| `g_race_physics` | GAME | `q2` | `quetoo-common-v1`, `q2-v1`, `quetoo-fix-v1`, `dp2-v1`, or aliases `q2`, `quake2`, `dp2` | Selects the movement physics preset. Latched — applies at the next map load; visible in the server browser. Unknown values refuse to start the server. |
| `g_q2_snap_mode` | GAME | `2` | `0` off, `1` nearest 1/8 unit, `2` truncate | Q2-family velocity snapping; part of the leaderboard ruleset identity. Latched. |

## Server: Race settings backing cvars

These back the settings catalog; change them with `gset`/`mset` so the values
persist and are validated.

| Cvar | Default | Range | Purpose |
|---|---:|---|---|
| `g_race_finish_cue_enabled` | `1` | 0/1 | Play the generic finish sound after a completed run. |
| `g_race_finish_cue_gain` | `100` | 1–100 | Finish cue volume. |
| `g_race_checkpoint_feedback` | `time` | `time` / `silent` | Show the checkpoint time, or stay silent. |
| `g_race_voting_time` | `30` | 0–300 s | How long a regular vote runs (0 disables voting). |
| `g_race_max_votes` | `3` | 0–100 | How many votes one player may start per map. |
| `g_race_vote_menu_duration` | `20` | 0–300 s | How long the end-of-map map vote runs. |
| `g_race_vote_menu_choices` | `3` | 0–8 | Number of map choices in the end-of-map vote. |
| `g_race_vote_allow_spectators` | `0` | 0/1 | Let spectators vote. |
| `g_race_weapons` | `1` | 0/1 | Whether Race players carry weapons at all (applies on restart). |
| `g_gravity` | `800` | 1–32767 | World gravity. |
| `g_gameplay` | `default` | 7 gameplay modes | Gameplay mode (applies on restart). |
| `sv_min_clients` | `0` | 0–max | Minimum client population. |
| `g_frag_limit` | `30` | ≥0 | Frag limit. |
| `g_time_limit` | `30` | ≥0, minutes | Map time limit. |
| `g_music` | `""` | track list | Music override; empty uses the map's own. |

## Server: other Race-relevant

| Cvar | Owner | Default | Purpose |
|---|---|---:|---|
| `sv_map_list` | GAME | `maps.lst` | The map rotation file that doubles as the map catalog and per-map override store. |
| `g_admin_password` | GAME | `""` | **Deprecated and ignored by Race** — use `radmin` accounts. |
| `g_hook` / `g_hook_pull_speed` / `g_hook_*` | GAME | stock | Grapple hook policy; Race allows the hook only in Practice, and the pull speed is mirrored to clients through a strictly parsed config string. |
| `g_self_knockback` | GAME | `1` | Scales self-inflicted knockback (rocket jumps); also the baseline for the tuning value `global.self_knockback`. |
| `g_player_projectile` | GAME | `1` | Scales player velocity into projectiles; baseline for `grenade.inherit_fraction`. |
| `g_ai_no_target` | GAME | `0` | Developer: bots stop targeting enemies. |
| `g_ai_node_dev` | GAME | `0` | Developer: AI node development mode (1 full, 2 live debug). Latched. |

Race's GAME module also registers the full stock movement
(`g_ground_speed`, `g_air_acceleration`, ...) and weapon balance
(`g_balance_*`) cvar sets from its `g_main.c` override. Two things to know:
the movement cvars only govern the `quetoo-common-v1` preset — the fixed
presets overwrite movement parameters every frame — and a subset of the
balance cvars (`g_balance_hyperblaster_*`, `g_balance_rocketlauncher_*`,
`g_balance_grenadelauncher_*`, `g_balance_handgrenade_refire`) form the
weapon-tuning baseline. They are not enumerated here; they retain their stock
meanings.

## Client: HUD

All archived.

| Cvar | Default | Purpose |
|---|---:|---|
| `cg_show_jumpers` | `1` | Show other players and their projectiles, trails, and sounds. `0` hides them all. |
| `cg_race_hud_edge` | `56` | Edge inset for the HUD corner clusters, in 1440p design pixels (0–320). |
| `cg_race_hud_timer_size` | `104` | Run timer size in design pixels. |
| `cg_race_hud_legibility` | `none` | Text treatment: `none`, `shadow`, `stroke`, or `plates`. |
| `cg_hb_climb_helper` | `1` | Draw the hyperblaster climb-distance helper (`CLIMB`/`CLOSER`/`TOO FAR`). |

## Client: input viewer and jump viewer

All archived.

| Cvar | Default | Purpose |
|---|---:|---|
| `cg_input_viewer` | `1` | Draw the effective-input cluster for live, chase, and replay play. |
| `cg_jump_viewer` | `0` | Draw jump length / height / air-time telemetry. |
| `cg_jump_viewer_path` | `0` | Show accumulated 3D path distance instead of straight-line length. |
| `cg_jump_viewer_hold_time` | `3` | Seconds to hold a completed jump result (0–30). |

## Client: strafe helper

All archived; all prefixed `cg_race_strafe_helper_` (legacy `sh_*` values are
migrated once at first registration).

| Cvar (suffix) | Default | Purpose |
|---|---:|---|
| `draw` | `1` | Draw the strafe helper bar. |
| `center` | `1` | Center the bar on the current view angle. |
| `centermarker` | `1` | Draw the center marker. |
| `center_width` | `2` | Center marker width (0.1–5). |
| `height` | `15` | Bar height (0–128). |
| `scale` | `1.5` | Horizontal bar scale (0.25–8). |
| `y` | `100` | Vertical offset from screen center. |
| `alpha` | `0.5` | Opacity of every bar element (0–1). |
| `bar_style` | `gradient` | Bar fill style. |
| `optimal_width` | `2` | Optimal-angle marker width (0.1–5). |
| `optimal_outline` | `1` | Outline the optimal marker instead of filling it. |
| `color_accelerating` | `0 128 0 128` | Accelerating-zone color (`r g b a`). |
| `color_optimal` | `255 215 0 255` | Optimal-angle color. |
| `color_centermarker` | `255 255 255 255` | Center-marker color. |
| `ups` | `1` | Draw the speed readout (replaces the HUD's own crosshair speed while on). |
| `ups_scale` / `ups_y` / `ups_shadow` | `1` / `-5` / `1` | Speed readout size, offset, shadow. |
| `ups_hide_zero` | `1` | Hide the readout at zero speed. |
| `ups_color_mode` | `dynamic` | Color by gain/loss, or fixed. |
| `ups_color_gain` / `ups_color_loss` / `ups_color_neutral` | green / red / white | Dynamic readout colors. |
| `ups_format` | `plain` | Readout number format. |
| `ups_3d` | `0` | Include vertical velocity in the readout. |
| `max_speed` (+ `_scale`, `_y`, `_shadow`, `_color`) | `0` | Optional max-speed readout and its styling. |
| `velocity_angle` (+ `_scale`, `_y`, `_shadow`, `_color`, `_format`) | `0` | Optional velocity-angle readout and its styling. |

## Client: markers and practice

All archived.

| Cvar | Default | Purpose |
|---|---:|---|
| `cg_race_markers` | `0` | Draw course start / checkpoint / finish markers. |
| `cg_markers` | `1` | Show your private practice markers. |
| `cg_marker_type` | `0` | Type placed by `marker_add`: 0 point, 1 takeoff, 2 landing, 3 aim. |
| `cg_marker_color` | `00e5ff` | Marker color (`rrggbb`). |
| `cg_marker_alpha` | `0.85` | Marker opacity (0.1–1). |
| `cg_marker_size` | `16` | Marker size (4–64 units). |

## Client: UI and identity

| Cvar | Default | Archived | Purpose |
|---|---:|:--:|---|
| `cg_race_menu_grid` | `1` | yes | Draw the animated speed grid behind the menu. |
| `cg_race_menu_grid_speed` | `1` | yes | Grid scroll rate as a multiple of your carried speed; 0 holds it still. |
| `cg_join_server_hide_empty` | `0` | yes | Server browser: hide empty servers. |
| `cg_join_server_hide_bots` | `0` | yes | Server browser: hide bot-only population. |
| `radmin_password` | `""` | **no** | One-use admin password; consumed and force-cleared the moment a login challenge arrives. Never sent to the server. |
| `race_profile_credential_<hash>` | `""` | yes | Per-server ranked-profile credential; the suffix is a hash of the server address, so credentials never cross servers. Reset with `race_profile_reset`. |

The client's `g_race_physics` registration in the Create Server screen exists
only so the selector has a binding; the server-side definition above is
authoritative.

## Notable non-cvars

The replay system, publication, persistence, voting internals, and admin
internals register **no cvars** — their limits are compile-time constants,
and Race's own tunables live in the settings and weapon-tuning systems
rather than the cvar namespace.

Diagnostic commands (not cvars): `race_physics [status|families]` (server),
`cg_race_physics` (client), `jumpers`, `markers`.
