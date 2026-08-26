GAME_COMMON_DIR = $(top_srcdir)/src/game/common

vpath %.c $(srcdir):$(GAME_COMMON_DIR)

AM_CPPFLAGS = \
	-DARGON2_NO_THREADS \
	-DG_HOOK \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/deps/argon2/include \
	-I$(top_srcdir)/deps/argon2/src \
	-I$(top_srcdir)/deps/minizip \
	-I$(GAME_COMMON_DIR)

# Movement state crosses GAME, CGAME, AI, prediction, and replay boundaries.
# Keep intermediate rounding independent of the target's FMA policy.
AM_CFLAGS = \
	-fms-extensions \
	-ffp-contract=off \
	-Wno-microsoft-anon-tag \
	-Wno-error=type-limits \
	@SO_CFLAGS@ \
	@BASE_CFLAGS@ \
	@OBJECTIVELY_CFLAGS@

gamelibdir = \
	@PKGLIBDIR@/race

gamelib_LTLIBRARIES = \
	game.la

game_la_SOURCES = \
	$(top_srcdir)/deps/argon2/src/argon2.c \
	$(top_srcdir)/deps/argon2/src/blake2/blake2b.c \
	$(top_srcdir)/deps/argon2/src/core.c \
	$(top_srcdir)/deps/argon2/src/encoding.c \
	$(top_srcdir)/deps/argon2/src/ref.c \
	$(top_srcdir)/deps/minizip/miniz.c \
	bg_item.c \
	bg_pmove.c \
	g_ai_goal.c \
	g_ai_grid.c \
	g_ai_info.c \
	g_ai_item.c \
	g_ai_main.c \
	g_ai_node.c \
	g_ballistics.c \
	g_client.c \
	g_client_chase.c \
	g_client_stats.c \
	g_client_view.c \
	g_cmd.c \
	g_combat.c \
	g_effect.c \
	g_entity.c \
	g_entity_func.c \
	g_entity_info.c \
	g_entity_misc.c \
	g_entity_target.c \
	g_entity_trigger.c \
	g_hook.c \
	g_item.c \
	g_main.c \
	g_module.c \
	g_physics.c \
	g_rules.c \
	g_sound.c \
	g_util.c \
	g_weapon.c \
	race_clip.c \
	race_kick_broker.c \
	race_actions.c \
	race_admin.c \
	race_admin_admission.c \
	race_admin_allowlist.c \
	race_admin_actions.c \
	race_admin_auth.c \
	race_admin_password.c \
	race_admin_service.c \
	race_admin_store.c \
	race.c \
	race_cmds.c \
	race_connection_address.c \
	race_finish_report.c \
	race_hook.c \
	race_leaderboard.c \
	race_leaderboard_wire.c \
	race_logic.c \
	race_map_browser_service.c \
	race_map_browser_wire.c \
	race_map_catalog.c \
	race_map_properties.c \
	race_map_state.c \
	race_map_state_service.c \
	race_map_state_store.c \
	race_modes.c \
	race_persistence.c \
	race_physics.c \
	race_physics_service.c \
	race_pmove_policy.c \
	race_weapon_movement.c \
	race_weapon_tuning.c \
	race_weapon_tuning_service.c \
	race_weapon_tuning_wire.c \
	race_publication.c \
	race_projectile_observer.c \
	race_profile.c \
	race_profile_auth.c \
	race_profiles.c \
	race_replay_format.c \
	race_replay_playback.c \
	race_replay_playback_service.c \
	race_replay_record.c \
	race_replay_service.c \
	race_replay_store.c \
	race_replay_transport.c \
	race_settings.c \
	race_settings_service.c \
	race_settings_store.c \
	race_settings_wire.c \
	race_trigger.c \
	race_training_service.c \
	race_vote.c \
	race_vote_menu.c \
	race_vote_menu_service.c \
	race_vote_service.c

game_la_LDFLAGS = \
	-avoid-version \
	-module \
	-no-undefined \
	-shared

game_la_LIBADD = \
	$(top_builddir)/src/shared/libshared.la \
	@OBJECTIVELY_LIBS@
