# System Index

The fastest answer to "where does this feature live". Paths are relative to
the repository root; `.c` implies the matching `.h`.

| System | Responsibility | Side | Main files |
|---|---|---|---|
| Module lifecycle (GAME) | Init/frame/per-client contract, hook installation | GAME | `src/game/race/g_module.c`, `race.c`, `race_module_compat.h` |
| Module lifecycle (CGAME) | Init/parse/scene/UI contract | CGAME | `src/cgame/race/cg_module.c`, `cg_module_compat.h` |
| Wire manifest | Protocol minor, messages, config strings, stats, struct extensions | shared | `src/game/race/g_types.h`, `race_wire.h` |
| Run logic | Run state machine, checkpoints, splits, stages, timing | GAME | `src/game/race/race_logic.c`, `race.c` |
| Modes | Race / Practice / Spectator policy, spawns, noclip | GAME | `src/game/race/race_modes.c` |
| Finish & publication | Finish report wire record; replay+record two-phase commit | GAME | `src/game/race/race_finish_report.c`, `race_publication.c` |
| Course entities | Start/checkpoint/split/stage/finish triggers, barriers | GAME | `src/game/race/race_trigger.c`, `g_entity.c` |
| Conditional barriers | Shared gate/one-way collision kernel | shared | `src/game/race/race_clip.c`; CGAME `src/cgame/race/cg_race_barriers.c` |
| Physics identity | Families, presets, selectors, wire codec | shared | `src/game/race/race_physics.c`, `race_pmove_policy.c` |
| Physics runtime | Selection, parameter enforcement, rankability | GAME | `src/game/race/race_physics_service.c` |
| Movement | The shared mover (compiled into both modules) | shared | `src/game/race/bg_pmove.c` |
| Prediction | Client movement prediction + sync gate | CGAME | `src/cgame/race/cg_predict.c`, `cg_race_physics.c` |
| Weapon–movement | Fire-path scoping, projectile stamping, hyper climb | GAME | `src/game/race/race_weapon_movement.c`, `g_ballistics.c` |
| Weapon tuning | 22-value live tuning catalog, service, wire sync | GAME + CGAME | `src/game/race/race_weapon_tuning*.c`, `src/cgame/race/cg_race_weapon_tuning.c` |
| Damage policy | No PvP damage; self-knockback preserved | GAME | `src/game/race/race_logic.c` (`Race_DamagePolicy`), `g_combat.c` |
| Profiles | UUID identity, enrollment, challenge/response auth | GAME + CGAME | `src/game/race/race_profile*.c`, `race_profiles.c`; `src/cgame/race/cg_race_profiles.c` |
| Map catalog | Parsed map-list metadata | GAME | `src/game/race/race_map_catalog.c` |
| Map properties | Per-map setting overrides in the map list | GAME | `src/game/race/race_map_properties.c` |
| Map state | Per-(map,ruleset) leaderboard file + status machine | GAME | `src/game/race/race_map_state*.c` |
| Leaderboards | Record identity, evaluation, top-15 wire | GAME | `src/game/race/race_leaderboard.c`, `race_leaderboard_wire.c` |
| Map browser | Paged catalog + records to the client | GAME + CGAME | `src/game/race/race_map_browser_*.c`; `src/cgame/race/cg_race_map_browser.c` |
| Persistence layer | Safe candidate/promote file I/O | GAME | `src/game/race/race_persistence.c` |
| Replay recording | 40 Hz samples + projectile events | GAME | `src/game/race/race_replay_service.c`, `race_replay_record.c`, `race_projectile_observer.c` |
| Replay format | QRPL encode/parse/validate, content-derived IDs | shared | `src/game/race/race_replay_format.c` |
| Replay storage | `.ghost` files, idempotent commit | GAME | `src/game/race/race_replay_store.c` |
| Replay transport | State/telemetry/projectile/raceline messages | shared | `src/game/race/race_replay_transport.c` |
| Replay playback | Viewer re-drive, controls, sessions | GAME | `src/game/race/race_replay_playback_service.c`, `race_replay_playback.c` |
| Replay presentation | Replay HUD, projectiles, raceline rendering | CGAME | `src/cgame/race/cg_race_replay.c` |
| Settings | Catalog, scopes, persistence, wire status | GAME + CGAME | `src/game/race/race_settings*.c`; `src/cgame/race/cg_race_settings.c` |
| Voting | Map/kick/physics votes, admission, resolution | GAME | `src/game/race/race_vote.c`, `race_vote_service.c`, `race_vote_admission.c` |
| Vote menu | End-of-map map-choice ballot | GAME | `src/game/race/race_vote_menu*.c` |
| Vote presentation | Vote HUD + menu screens | CGAME | `src/cgame/race/cg_race_vote.c`, `ui/voting/`, `ui/main/ActiveVoteViewController.c` |
| Administration | Accounts, roles, sessions, auth, allowlist, audit | GAME | `src/game/race/race_admin*.c` |
| Admin client | Proof computation, menu command validation | CGAME | `src/cgame/race/cg_race_admin_auth.c`, `cg_race_admin_command.c` |
| Kick broker | Identity-safe kick commit | GAME | `src/game/race/race_kick_broker.c`, `race_actions.c` |
| Commands | The single client-command dispatcher | GAME | `src/game/race/race_cmds.c` |
| HUD | Timer, splits, checkpoints, speed, climb helper | CGAME | `src/cgame/race/cg_race_hud.c`, `cg_race_presentation.c` |
| Scoreboard / roster | Score model, leaderboard, roster bands | CGAME | `src/cgame/race/cg_score.c`, `cg_score_model.c` |
| Finish banner | Finish report classification and display | CGAME | `src/cgame/race/cg_race_finish_report.c` |
| Training aggregator | Wires viewers into prediction/replay telemetry | CGAME | `src/cgame/race/cg_race_training.c` |
| Input viewer | Effective-input display | CGAME | `src/cgame/race/cg_input_viewer.c` |
| Jump viewer | Jump length/height/air-time telemetry | CGAME | `src/cgame/race/cg_jump_viewer.c` |
| Strafe helper | Acceleration-zone bar + readouts | CGAME | `src/cgame/race/cg_strafe_helper.c` |
| Course markers | BSP-derived start/checkpoint/finish markers | CGAME | `src/cgame/race/cg_race_markers.c` |
| Practice markers | Private per-map player markers | CGAME | `src/cgame/race/cg_race_practice_markers.c`, `cg_race_client_file.c` |
| Double jump | Client input macro | CGAME | `src/cgame/race/cg_race_double_jump.c` |
| Entity filtering | Jumper hiding, barrier draw filtering | CGAME | `src/cgame/race/cg_entity.c` |
| Native UI shell | ESC menu chrome, routes, responsive layout | CGAME | `src/cgame/race/ui/main/` |
| UI screens | Home, Play, Controls, Settings, Maps, Voting, Credits, Admin, WeaponLab | CGAME | `src/cgame/race/ui/<route>/` |
| Hook policy | Grapple allowed in Practice; pull-speed config string | GAME + CGAME | `src/game/race/race_hook.c`, `g_hook.c` |
| Training hook (pmove) | Pre-acceleration strafe sampling seam | shared | `src/game/race/race_training.h`, `race_training_service.c` |
| AI | Bot movement through the same pmove; barrier-safe lookahead | GAME | `src/game/race/g_ai_main.c`, `g_ai_node.c` |
| Verification | Override/UI/project audits | tools | `src/tools/verify_race_overrides.py`, `verify_race_ui.py`, `verify_projects.py` |
| Tests | Focused Race test binaries | tests | `src/tests/check_race*.c` |
