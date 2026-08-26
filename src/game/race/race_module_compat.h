/*
 * Copyright(c) 2006 Quetoo.
 *
 * Race-local hooks that current common GAME does not expose. Matching Race
 * override implementations are selected only by game-race.vcxproj.
 */

#pragma once

typedef struct {
  vec3_t origin;
  vec3_t angles;
  int32_t clip_mask;
  bool kill_box;
} g_client_spawn_t;

typedef bool (*g_hook_client_allowed_t)(const g_client_t *cl);
typedef void (*ClientKill)(g_client_t *cl);

extern ClientKill G_ClientKill;

box3_t Pm_PlayerBounds(bool ducked);

void G_Hook_SetClientAllowed(g_hook_client_allowed_t allowed);
void G_Module_Frame(void);
void G_Module_FinalizeClientFrames(void);
bool G_Module_IntermissionReady(void);
bool G_Module_IntermissionClientCommand(g_client_t *cl, const char *cmd);
void G_Module_ClientActivity(g_client_t *cl);
bool G_Module_SpawnEntity(g_entity_t *ent);
bool G_Module_ClientCommand(g_client_t *cl, const char *cmd);
void G_Module_ClientBegin(g_client_t *cl);
void G_Module_ClientDisconnect(g_client_t *cl);
bool G_Module_ClientModeChange(g_client_t *cl, bool spectator);
bool G_Module_ClientNoClip(g_client_t *cl);
void G_Module_ClientUserInfoChanged(g_client_t *cl);
void G_Module_ClientSpawn(g_client_t *cl, g_client_spawn_t *spawn);
bool G_Module_ClientInput(g_client_t *cl, const pm_cmd_t *cmd);
bool G_Module_ClientGameplay(const g_client_t *cl);
bool G_Module_ShouldClipMovementEntity(g_entity_t *mover,
                                       const g_entity_t *candidate,
                                       vec3_t start, vec3_t end,
                                       box3_t bounds);
cm_trace_t G_Module_TraceMovement(g_entity_t *mover, vec3_t start,
                                 vec3_t end, box3_t bounds,
                                 int32_t contents);
void G_Module_ClientThink(g_client_t *cl, const pm_cmd_t *cmd);
void G_Module_ClientStats(g_client_t *cl);
void G_Module_ClientScore(const g_client_t *cl, g_score_t *score);
void G_Module_ClientStatusInfo(const g_client_t *cl, char *info, size_t len);
bool G_Module_WeaponsEnabled(void);
bool G_Module_UnlimitedAmmo(void);
void G_Module_ClientInventory(g_client_t *cl);
bool G_Module_FilterDamage(g_entity_t *target, g_entity_t *attacker,
                           int32_t *damage, int32_t *knockback);
