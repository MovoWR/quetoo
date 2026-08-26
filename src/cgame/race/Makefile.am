CGAME_COMMON_DIR = $(top_srcdir)/src/cgame/common

GAME_DIR = $(top_srcdir)/src/game/race
GAME_COMMON_DIR = $(top_srcdir)/src/game/common

vpath %.c $(srcdir):$(CGAME_COMMON_DIR):$(GAME_DIR):$(GAME_COMMON_DIR)

AM_CPPFLAGS = \
	-DARGON2_NO_THREADS \
	-DG_HOOK \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/cgame \
	-I$(top_srcdir)/deps/argon2/include \
	-I$(top_srcdir)/deps/argon2/src \
	-I$(CGAME_COMMON_DIR) \
	-I$(CGAME_COMMON_DIR)/ui \
	-I$(srcdir)/ui/admin \
	-I$(srcdir)/ui/controls \
	-I$(CGAME_COMMON_DIR)/ui/controls \
	-I$(srcdir)/ui/credits \
	-I$(CGAME_COMMON_DIR)/ui/credits \
	-I$(CGAME_COMMON_DIR)/ui/editor \
	-I$(srcdir)/ui/home \
	-I$(CGAME_COMMON_DIR)/ui/home \
	-I$(srcdir)/ui/main \
	-I$(CGAME_COMMON_DIR)/ui/main \
	-I$(srcdir)/ui/maps \
	-I$(srcdir)/ui/play \
	-I$(CGAME_COMMON_DIR)/ui/play \
	-I$(srcdir)/ui/settings \
	-I$(CGAME_COMMON_DIR)/ui/settings \
	-I$(srcdir)/ui/voting \
	-I$(CGAME_COMMON_DIR)/ui/teams \
	-I$(GAME_DIR) \
	-I$(GAME_COMMON_DIR)

# Movement state crosses GAME, CGAME, AI, prediction, and replay boundaries.
# Keep intermediate rounding independent of the target's FMA policy.
AM_CFLAGS = \
	-ffp-contract=off \
	-Wno-error=type-limits \
	@SO_CFLAGS@ \
	@BASE_CFLAGS@ \
	@OBJECTIVELY_CFLAGS@ \
	@OBJECTIVELYMVC_CFLAGS@ \
	@OPENAL_CFLAGS@ \
	@SDL3_CFLAGS@ \
	@SNDFILE_CFLAGS@

CGUI_DIR = $(CGAME_COMMON_DIR)/ui

cguidir = \
	@PKGLIBDIR@/race/ui

cgui_DATA = \
	$(srcdir)/ui/DialogViewController.css \
	$(srcdir)/ui/hud_input_attack.png \
	$(srcdir)/ui/hud_input_crouch.png \
	$(srcdir)/ui/hud_input_down.png \
	$(srcdir)/ui/hud_input_jump.png \
	$(srcdir)/ui/hud_input_left.png \
	$(srcdir)/ui/hud_input_right.png \
	$(srcdir)/ui/hud_input_up.png
cguiadmindir = \
	@PKGLIBDIR@/race/ui/admin

cguiadmin_DATA = \
	$(srcdir)/ui/admin/AdminViewController.css \
	$(srcdir)/ui/admin/AdminViewController.json

cguifontsdir = \
	@PKGLIBDIR@/race/ui/fonts

cguifonts_DATA = \
	$(srcdir)/ui/fonts/Coda-Regular.ttf

cguicontrolsdir = \
	@PKGLIBDIR@/race/ui/controls

cguicontrols_DATA = \
	$(srcdir)/ui/controls/ControlsViewController.json \
	$(srcdir)/ui/controls/MovementCombatViewController.css \
	$(srcdir)/ui/controls/MovementCombatViewController.json \
	$(srcdir)/ui/controls/ResponseServiceViewController.css \
	$(srcdir)/ui/controls/ResponseServiceViewController.json

cguicreditsdir = \
	@PKGLIBDIR@/race/ui/credits

cguicredits_DATA = \
	$(srcdir)/ui/credits/CreditsViewController.css \
	$(srcdir)/ui/credits/CreditsViewController.json

cguihomedir = \
	@PKGLIBDIR@/race/ui/home

cguihome_DATA = \
	$(srcdir)/ui/home/HomeViewController.css \
	$(srcdir)/ui/home/HomeViewController.json

cguimaindir = \
	@PKGLIBDIR@/race/ui/main

cguimain_DATA = \
	$(srcdir)/ui/main/LoadingViewController.css \
	$(srcdir)/ui/main/MainView.css \
	$(srcdir)/ui/main/MainView.json \
	$(srcdir)/ui/main/QuickSettingsViewController.css \
	$(srcdir)/ui/main/QuickSettingsViewController.json \
	$(srcdir)/ui/main/ActiveVoteViewController.css \
	$(srcdir)/ui/main/ActiveVoteViewController.json \
	$(srcdir)/ui/main/UpdateViewController.css

cguiplaydir = \
	@PKGLIBDIR@/race/ui/play

cguiplay_DATA = \
	$(srcdir)/ui/play/CreateServerViewController.css \
	$(srcdir)/ui/play/CreateServerViewController.json \
	$(srcdir)/ui/play/JoinServerViewController.css \
	$(srcdir)/ui/play/JoinServerViewController.json \
	$(srcdir)/ui/play/PlayerSetupViewController.css \
	$(srcdir)/ui/play/PlayerSetupViewController.json

cguisettingsdir = \
	@PKGLIBDIR@/race/ui/settings

cguisettings_DATA = \
	$(srcdir)/ui/settings/SettingsViewController.css \
	$(srcdir)/ui/settings/SettingsViewController.json

cguimapsdir = \
	@PKGLIBDIR@/race/ui/maps

cguimaps_DATA = \
	$(srcdir)/ui/maps/MapBrowserViewController.css \
	$(srcdir)/ui/maps/MapBrowserViewController.json

cguivotingdir = \
	@PKGLIBDIR@/race/ui/voting

cguivoting_DATA = \
	$(srcdir)/ui/voting/VotingViewController.css \
	$(srcdir)/ui/voting/VotingViewController.json

cgamelibdir = \
	@PKGLIBDIR@/race

cgamelib_LTLIBRARIES = \
	cgame.la

cgame_la_SOURCES = \
	$(top_srcdir)/deps/argon2/src/argon2.c \
	$(top_srcdir)/deps/argon2/src/blake2/blake2b.c \
	$(top_srcdir)/deps/argon2/src/core.c \
	$(top_srcdir)/deps/argon2/src/encoding.c \
	$(top_srcdir)/deps/argon2/src/ref.c \
	bg_item.c \
	bg_pmove.c \
	race_settings.c \
	race_settings_wire.c \
	cg_client.c \
	cg_discord.c \
	cg_editor.c \
	cg_effect.c \
	cg_entity.c \
	cg_entity_effect.c \
	cg_entity_event.c \
	cg_entity_misc.c \
	cg_entity_trail.c \
	cg_flare.c \
	cg_hud.c \
	cg_hud_draw.c \
	cg_input.c \
	cg_input_viewer.c \
	cg_inventory.c \
	cg_light.c \
	cg_main.c \
	cg_media.c \
	cg_module.c \
	cg_race_admin_auth.c \
	cg_race_admin_command.c \
	cg_race_barriers.c \
	cg_race_double_jump.c \
	cg_muzzle_flash.c \
	cg_predict.c \
	cg_jump_viewer.c \
	cg_race_map_browser.c \
	cg_race_finish_report.c \
	cg_race_hud.c \
	cg_race_client_file.c \
	cg_race_message.c \
	cg_race_markers.c \
	cg_race_practice_markers.c \
	cg_race_physics.c \
	cg_race_presentation.c \
	cg_race_profiles.c \
	cg_race_replay.c \
	cg_race_settings.c \
	cg_race_training.c \
	cg_race_vote.c \
	cg_race_weapon_tuning.c \
	cg_score.c \
	cg_score_model.c \
	cg_strafe_helper.c \
	cg_sound.c \
	cg_sprite.c \
	cg_team_mode.c \
	cg_temp_entity.c \
	cg_ui.c \
	cg_view.c \
	cg_weapon.c \
	race_clip.c \
	race_admin_auth.c \
	race_finish_report.c \
	race_hook.c \
	race_profile.c \
	race_profile_auth.c \
	race_physics.c \
	race_pmove_policy.c \
	race_leaderboard_wire.c \
	race_map_browser_wire.c \
	race_replay_transport.c \
	race_weapon_tuning_wire.c \
	ui/BindTextView.c \
	ui/CvarCheckbox.c \
	ui/CvarSelect.c \
	ui/CvarSlider.c \
	ui/CvarTextView.c \
	ui/DialogViewController.c \
	ui/admin/AdminViewController.c \
	ui/controls/ControlsViewController.c \
	ui/controls/CrosshairView.c \
	ui/controls/MovementCombatViewController.c \
	ui/controls/RaceBindTextView.c \
	ui/controls/ResponseServiceViewController.c \
	ui/credits/CreditsViewController.c \
	ui/editor/EditorViewController.c \
	ui/editor/EntityView.c \
	ui/editor/EntityViewController.c \
	ui/editor/MaterialViewController.c \
	ui/editor/MeshViewController.c \
	ui/home/HomeViewController.c \
	ui/home/LeaderboardViewController.c \
	ui/home/StatsViewController.c \
	ui/main/LoadingViewController.c \
	ui/main/ActiveVoteViewController.c \
	ui/main/ColumnsView.c \
	ui/main/RaceSlider.c \
	ui/main/MainView.c \
	ui/main/MainViewController.c \
	ui/main/QuickSettingsViewController.c \
	ui/main/UpdateViewController.c \
	ui/maps/MapBrowserViewController.c \
	ui/play/CreateServerViewController.c \
	ui/play/JoinServerViewController.c \
	ui/play/MapListCollectionItemView.c \
	ui/play/MapListCollectionView.c \
	ui/play/PlayViewController.c \
	ui/play/PlayerModelView.c \
	ui/play/PlayerSetupViewController.c \
	ui/settings/SettingsViewController.c \
	ui/settings/StrafeHelperPreview.c \
	ui/voting/RosterSelect.c \
	ui/voting/VotingViewController.c \
	ui/teams/TeamPlayerView.c \
	ui/teams/TeamView.c \
	ui/teams/TeamsViewController.c

cgame_la_LDFLAGS = \
	-avoid-version \
	-module \
	-no-undefined \
	-shared

cgame_la_LIBADD = \
	$(top_builddir)/src/shared/libshared.la \
	$(top_builddir)/deps/discord-rpc/src/libdiscord-rpc.la \
	@OBJECTIVELY_LIBS@ \
	@OBJECTIVELYMVC_LIBS@

if APPLE
cgame_la_LIBADD += -lc++
cgame_la_LDFLAGS += -framework AppKit
else
cgame_la_LIBADD += -lstdc++
endif
