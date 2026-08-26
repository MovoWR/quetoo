if BUILD_TESTS

TESTS_CFLAGS = \
	-I$(top_srcdir)/src \
	@BASE_CFLAGS@ \
	@CHECK_CFLAGS@ \
	@OBJECTIVELY_CFLAGS@ \
	@SDL3_CFLAGS@
	
# Race movement and its synthetic collision fixtures are required to produce
# the same float stream on x86-64 and arm64. Do not allow target FMA policy to
# change intermediate rounding.
RACE_TEST_CFLAGS = \
	-ffp-contract=off \
	$(TESTS_CFLAGS)

TESTS_LIBS = \
	libtests.la \
	@BASE_LIBS@ \
	@CHECK_LIBS@ \
	@OBJECTIVELY_LIBS@ \
	@SDL3_LIBS@

if APPLE
RACE_TEST_GC_FLAGS = \
	-Wl,-dead_strip
else
RACE_TEST_GC_FLAGS = \
	-Wl,--gc-sections
endif

noinst_LTLIBRARIES = \
	libtests.la

noinst_HEADERS = \
	tests.h

libtests_la_SOURCES = \
	tests.c
libtests_la_CFLAGS = \
	$(TESTS_CFLAGS)
libtests_la_LDFLAGS = \
	-shared
libtests_la_LIBADD = \
	$(top_builddir)/src/common/libcommon.la

TESTS = \
	check_atlas \
	check_box \
	check_cm_entity \
	check_cm_manifest \
	check_cm_polylib \
	check_cm_test \
	check_cmd \
	check_color \
	check_cvar \
	check_editor_map \
	check_filesystem \
	check_http \
	check_master \
	check_mem \
	check_net_message \
	check_race \
	check_race_native \
	check_race_game_landing \
	check_race_weapon_movement \
	check_race_weapon_tuning \
	check_race_jump_viewer \
	check_race_pmove \
	check_r_media \
	check_shared \
	check_thread \
	check_vector

noinst_PROGRAMS = \
	$(TESTS) \
	race_portability_fixture

check_atlas_SOURCES = \
	check_atlas.c
check_atlas_CFLAGS = \
	$(TESTS_CFLAGS)
check_atlas_LDADD = \
	$(TESTS_LIBS)

check_box_SOURCES = \
	check_box.c
check_box_CFLAGS = \
	$(TESTS_CFLAGS)
check_box_LDADD = \
	$(TESTS_LIBS)

check_cmd_SOURCES = \
	check_cmd.c
check_cmd_CFLAGS = \
	$(TESTS_CFLAGS)
check_cmd_LDADD = \
	$(TESTS_LIBS)

check_cm_entity_SOURCES = \
	check_cm_entity.c
check_cm_entity_CFLAGS = \
	$(TESTS_CFLAGS)
check_cm_entity_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/collision/libcollision.la

check_cm_manifest_SOURCES = \
	check_cm_manifest.c
check_cm_manifest_CFLAGS = \
	$(TESTS_CFLAGS)
check_cm_manifest_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/collision/libcollision.la

check_editor_map_SOURCES = \
	check_editor_map.c
check_editor_map_CFLAGS = \
	$(TESTS_CFLAGS)
check_editor_map_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/collision/libcollision.la

check_cm_polylib_SOURCES = \
	check_cm_polylib.c
check_cm_polylib_CFLAGS = \
	$(TESTS_CFLAGS)
check_cm_polylib_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/collision/libcollision.la

check_cm_test_SOURCES = \
	check_cm_test.c
check_cm_test_CFLAGS = \
	$(TESTS_CFLAGS)
check_cm_test_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/collision/libcollision.la

check_color_SOURCES = \
	check_color.c
check_color_CFLAGS = \
	$(TESTS_CFLAGS)
check_color_LDADD = \
	$(TESTS_LIBS)

check_cvar_SOURCES = \
	check_cvar.c
check_cvar_CFLAGS = \
	$(TESTS_CFLAGS)
check_cvar_LDADD = \
	$(TESTS_LIBS)

check_filesystem_SOURCES = \
	check_filesystem.c
check_filesystem_CFLAGS = \
	$(TESTS_CFLAGS)
check_filesystem_LDADD = \
	$(TESTS_LIBS)

check_http_SOURCES = \
	check_http.c
check_http_CFLAGS = \
	$(TESTS_CFLAGS) \
	@OBJECTIVELY_CFLAGS@
check_http_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/net/libnet.la

check_master_SOURCES = \
	check_master.c
check_master_CFLAGS = \
	$(TESTS_CFLAGS)
check_master_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/net/libnet.la

check_mem_SOURCES = \
	check_mem.c
check_mem_CFLAGS = \
	$(TESTS_CFLAGS)
check_mem_LDADD = \
	$(TESTS_LIBS)

check_net_message_SOURCES = \
	check_net_message.c
check_net_message_CFLAGS = \
	-I$(top_srcdir)/src \
	$(TESTS_CFLAGS)
check_net_message_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/net/libnet.la

check_race_SOURCES = \
	check_race.c \
	check_race_map_browser.c \
	$(top_srcdir)/deps/argon2/src/argon2.c \
	$(top_srcdir)/deps/argon2/src/blake2/blake2b.c \
	$(top_srcdir)/deps/argon2/src/core.c \
	$(top_srcdir)/deps/argon2/src/encoding.c \
	$(top_srcdir)/deps/argon2/src/ref.c \
	$(top_srcdir)/src/cgame/race/cg_race_message.c \
	$(top_srcdir)/src/cgame/race/cg_race_presentation.c \
	$(top_srcdir)/src/game/race/race_admin.c \
	$(top_srcdir)/src/game/race/race_admin_admission.c \
	$(top_srcdir)/src/game/race/race_admin_allowlist.c \
	$(top_srcdir)/src/game/race/race_admin_auth.c \
	$(top_srcdir)/src/game/race/race_admin_password.c \
	$(top_srcdir)/src/game/race/race_admin_store.c \
	$(top_srcdir)/src/game/race/race_connection_address.c \
	$(top_srcdir)/src/game/race/race_leaderboard.c \
	$(top_srcdir)/src/game/race/race_leaderboard_wire.c \
	$(top_srcdir)/src/game/race/race_finish_report.c \
	$(top_srcdir)/src/game/race/race_logic.c \
	$(top_srcdir)/src/game/race/race_map_browser_wire.c \
	$(top_srcdir)/src/game/race/race_map_catalog.c \
	$(top_srcdir)/src/game/race/race_map_properties.c \
	$(top_srcdir)/src/game/race/race_map_state.c \
	$(top_srcdir)/src/game/race/race_map_state_store.c \
	$(top_srcdir)/src/game/race/race_persistence.c \
	$(top_srcdir)/src/game/race/race_physics.c \
	$(top_srcdir)/src/game/race/race_publication.c \
	$(top_srcdir)/src/game/race/race_profile.c \
	$(top_srcdir)/src/game/race/race_profile_auth.c \
	$(top_srcdir)/src/game/race/race_replay_format.c \
	$(top_srcdir)/src/game/race/race_replay_playback.c \
	$(top_srcdir)/src/game/race/race_replay_record.c \
	$(top_srcdir)/src/game/race/race_replay_store.c \
	$(top_srcdir)/src/game/race/race_replay_transport.c \
	$(top_srcdir)/src/game/race/race_settings.c \
	$(top_srcdir)/src/game/race/race_settings_store.c \
	$(top_srcdir)/src/game/race/race_vote.c \
	$(top_srcdir)/src/game/race/race_vote_admission.c \
	$(top_srcdir)/src/game/race/race_vote_menu.c
check_race_CFLAGS = \
	-DARGON2_NO_THREADS \
	-fms-extensions \
	-Wno-error=type-limits \
	-I$(top_srcdir)/deps/argon2/include \
	-I$(top_srcdir)/deps/argon2/src \
	-I$(top_srcdir)/deps/minizip \
	-I$(top_srcdir)/src/cgame/race \
	-I$(top_srcdir)/src/cgame/common \
	-I$(top_srcdir)/src/game/race \
	-I$(top_srcdir)/src/game/common \
	$(RACE_TEST_CFLAGS)
check_race_LDADD = \
	$(top_builddir)/deps/minizip/libminizip.la \
	$(TESTS_LIBS)

check_race_native_SOURCES = \
	check_race_native.c \
	check_race_cgame_module.c \
	check_race_persistence_native.c \
	check_race_ui_native.c \
	$(top_srcdir)/src/cgame/race/cg_module.c \
	$(top_srcdir)/src/cgame/race/cg_race_admin_command.c \
	$(top_srcdir)/src/cgame/race/cg_race_client_file.c \
	$(top_srcdir)/src/cgame/race/cg_race_dashboard_layout.c \
	$(top_srcdir)/src/cgame/race/cg_race_double_jump.c \
	$(top_srcdir)/src/cgame/race/cg_score_model.c \
	$(top_srcdir)/src/cgame/race/cg_team_mode.c \
	$(top_srcdir)/src/cgame/race/ui/main/QuickSettingsHostView.c \
	$(top_srcdir)/src/game/race/race_clip.c \
	$(top_srcdir)/src/game/race/race_hook.c \
	$(top_srcdir)/src/game/race/race_kick_broker.c \
	$(top_srcdir)/src/game/race/race_persistence.c \
	$(top_srcdir)/src/game/race/race_projectile_observer.c
check_race_native_CFLAGS = \
	-fms-extensions \
	-ffunction-sections \
	-fdata-sections \
	@OBJECTIVELYMVC_CFLAGS@ \
	-I$(top_srcdir)/src/cgame/race \
	-I$(top_srcdir)/src/cgame/common \
	-I$(top_srcdir)/src/game/race \
	-I$(top_srcdir)/src/game/common \
	$(RACE_TEST_CFLAGS)
check_race_native_LDFLAGS = \
	$(RACE_TEST_GC_FLAGS)
check_race_native_LDADD = \
	@BASE_LIBS@ \
	@OBJECTIVELY_LIBS@ \
	@OBJECTIVELYMVC_LIBS@

race_portability_fixture_SOURCES = \
	race_portability_fixture.c \
	$(top_srcdir)/src/game/race/race_finish_report.c \
	$(top_srcdir)/src/game/race/race_leaderboard.c \
	$(top_srcdir)/src/game/race/race_map_state.c \
	$(top_srcdir)/src/game/race/race_physics.c \
	$(top_srcdir)/src/game/race/race_profile.c \
	$(top_srcdir)/src/game/race/race_replay_format.c \
	$(top_srcdir)/src/game/race/race_replay_transport.c \
	$(top_srcdir)/src/game/race/race_settings.c
race_portability_fixture_CFLAGS = \
	-fms-extensions \
	-I$(top_srcdir)/deps/minizip \
	-I$(top_srcdir)/src/game/race \
	-I$(top_srcdir)/src/game/common \
	$(RACE_TEST_CFLAGS)
race_portability_fixture_LDADD = \
	$(top_builddir)/deps/minizip/libminizip.la \
	$(TESTS_LIBS)

check_race_game_landing_SOURCES = \
	check_race_game_landing.c \
	$(top_srcdir)/src/game/common/g_ai_grid.c \
	$(top_srcdir)/src/game/race/g_ai_node.c \
	$(top_srcdir)/src/game/race/bg_pmove.c \
	$(top_srcdir)/src/game/race/race_physics.c \
	$(top_srcdir)/src/game/race/race_pmove_policy.c \
	$(top_srcdir)/src/shared/qstring.c \
	$(top_srcdir)/src/shared/swap.c
check_race_game_landing_CFLAGS = \
	-DRACE_PHYSICS_TEST \
	-fms-extensions \
	-ffunction-sections \
	-fdata-sections \
	-I$(top_srcdir)/src/game/race \
	-I$(top_srcdir)/src/game/common \
	$(RACE_TEST_CFLAGS)
check_race_game_landing_LDFLAGS = \
	$(RACE_TEST_GC_FLAGS)
check_race_game_landing_LDADD = \
	@CHECK_LIBS@ \
	@OBJECTIVELY_LIBS@

check_race_weapon_movement_SOURCES = \
	check_race_weapon_movement.c \
	$(top_srcdir)/src/game/race/race_physics.c \
	$(top_srcdir)/src/game/race/race_weapon_movement.c \
	$(top_srcdir)/src/shared/qstring.c
check_race_weapon_movement_CFLAGS = \
	-fms-extensions \
	-ffunction-sections \
	-fdata-sections \
	-I$(top_srcdir)/src/game/race \
	-I$(top_srcdir)/src/game/common \
	$(RACE_TEST_CFLAGS)
check_race_weapon_movement_LDFLAGS = \
	$(RACE_TEST_GC_FLAGS)
check_race_weapon_movement_LDADD = \
	@CHECK_LIBS@

check_race_weapon_tuning_SOURCES = \
	check_race_weapon_tuning.c \
	check_race_weapon_tuning_service.c \
	$(top_srcdir)/src/cgame/race/cg_race_weapon_tuning.c \
	$(top_srcdir)/src/game/race/race_weapon_tuning.c \
	$(top_srcdir)/src/game/race/race_weapon_tuning_service.c \
	$(top_srcdir)/src/game/race/race_weapon_tuning_wire.c \
	$(top_srcdir)/src/shared/qstring.c
check_race_weapon_tuning_CFLAGS = \
	-fms-extensions \
	-ffunction-sections \
	-fdata-sections \
	-I$(top_srcdir)/src/game/race \
	-I$(top_srcdir)/src/game/common \
	-I$(top_srcdir)/src/cgame/race \
	-I$(top_srcdir)/src/cgame/common \
	-I$(top_srcdir)/src/cgame \
	-I$(top_srcdir)/src \
	$(RACE_TEST_CFLAGS)
check_race_weapon_tuning_LDFLAGS = \
	$(RACE_TEST_GC_FLAGS)
check_race_weapon_tuning_LDADD = \
	@BASE_LIBS@

check_race_jump_viewer_SOURCES = \
	check_race_jump_viewer.c
check_race_jump_viewer_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/game/common \
	$(RACE_TEST_CFLAGS)
check_race_jump_viewer_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/shared/libshared.la

check_race_pmove_SOURCES = \
	check_race_cgame_bounds.c \
	check_race_physics_service.c \
	check_race_pmove.c \
	check_race_pmove_cgame.c \
	check_race_pmove_common.c \
	$(top_srcdir)/src/cgame/race/cg_entity.c \
	$(top_srcdir)/src/game/race/bg_pmove.c \
	$(top_srcdir)/src/game/race/race_physics.c \
	$(top_srcdir)/src/game/race/race_physics_service.c \
	$(top_srcdir)/src/game/race/race_pmove_policy.c \
	$(top_srcdir)/src/shared/qstring.c
check_race_pmove_CFLAGS = \
	-DRACE_PHYSICS_TEST \
	-fms-extensions \
	-ffunction-sections \
	-fdata-sections \
	-I$(top_srcdir)/src/cgame/common \
	-I$(top_srcdir)/src/game/race \
	-I$(top_srcdir)/src/game/common \
	$(RACE_TEST_CFLAGS)
check_race_pmove_LDFLAGS = \
	$(RACE_TEST_GC_FLAGS)
check_race_pmove_LDADD = \
	@CHECK_LIBS@

check_r_media_SOURCES = \
	check_r_media.c
check_r_media_CFLAGS = \
	-I$(top_srcdir)/src/client/renderer \
	$(TESTS_CFLAGS)
check_r_media_LDADD = \
	$(TESTS_LIBS) \
	$(top_builddir)/src/collision/libcollision.la \
	$(top_builddir)/src/client/renderer/librenderer.la

check_shared_SOURCES = \
	check_shared.c
check_shared_CFLAGS = \
	-I$(top_srcdir)/src \
	$(TESTS_CFLAGS)
check_shared_LDADD = \
	$(TESTS_LIBS)

check_thread_SOURCES = \
	check_thread.c
check_thread_CFLAGS = \
	-I$(top_srcdir)/src \
	$(TESTS_CFLAGS)
check_thread_LDADD = \
	$(TESTS_LIBS)

check_vector_SOURCES = \
	check_vector.c
check_vector_CFLAGS = \
	$(TESTS_CFLAGS)
check_vector_LDADD = \
	$(TESTS_LIBS)

endif
