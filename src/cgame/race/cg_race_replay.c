/*
 * Copyright(c) 2006 Quetoo.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "cg_local.h"
#include "cg_input_viewer.h"
#include "cg_race_hud.h"
#include "cg_race_presentation.h"
#include "cg_race_replay.h"
#include "cg_race_training.h"
#include "cg_strafe_helper.h"

#include "race_replay_transport.h"
#include "race_wire.h"

#define CG_RACE_RACELINE_TRAIL_MSEC 3000u
#define CG_RACE_RACELINE_Z_OFFSET 2.f
#define CG_RACE_RACELINE_GLOW_SIZE 12.f
#define CG_RACE_RACELINE_CORE_SIZE 4.f
#define CG_RACE_RACELINE_HEAD_GLOW_SIZE 11.f
#define CG_RACE_RACELINE_HEAD_CORE_SIZE 4.5f

typedef struct {
  race_replay_projectile_event_t spawn;
  cl_entity_t entity;
  bool active;
} cg_race_replay_projectile_t;

static race_replay_client_cache_t cg_race_replay_cache;
static cg_race_replay_projectile_t
  cg_race_replay_projectiles[RACE_REPLAY_MAX_ACTIVE_PROJECTILES];
static r_model_t *cg_race_replay_rocket_model;
static s_sample_t *cg_race_replay_rocket_fly_sample;

static void Cg_RaceReplay_RocketSpawnPresentation(
    const vec3_t muzzle, const vec3_t direction,
    const vec3_t sound_origin, const cl_entity_t *sound_entity) {
  Cg_AddLight(&(cg_light_t) {
    .origin = muzzle,
    .radius = 200.f,
    .color = Vec3(.9f, .6f, .3f),
    .intensity = 4.f,
    .decay = 400
  });

  if (cgi.PointContents(sound_origin) & CONTENTS_MASK_LIQUID) {
    Cg_BubbleTrail(NULL, muzzle, Vec3_Fmaf(sound_origin, 40.f, direction), 2.f);
  } else {
    Cg_AddSprite(&(cg_sprite_t) {
      .atlas_image = cg_sprite_explosion_glow,
      .origin = Vec3_Fmaf(muzzle, 8.f, direction),
      .lifetime = 300,
      .size = 60.f,
      .color = Vec3(.9f, .6f, .3f),
    });

    for (int32_t i = 0; i < 3; i++) {
      Cg_AddSprite(&(cg_sprite_t) {
        .atlas_image = cg_sprite_flame,
        .origin = Vec3_Fmaf(muzzle, 4.f + i * 4.f, direction),
        .velocity = Vec3_Scale(direction, RandomRangef(40.f, 80.f)),
        .lifetime = RandomRangef(200.f, 400.f),
        .size = RandomRangef(10.f, 18.f),
        .size_velocity = -20.f,
        .rotation = RandomRadian(),
        .color = ColorHSV(RandomRangef(20.f, 50.f), 1.f, 1.f).vec3,
      });
    }

    Cg_AddSprite(&(cg_sprite_t) {
      .atlas_image = cg_sprite_smoke,
      .origin = muzzle,
      .velocity = Vec3_Scale(direction, RandomRangef(15.f, 30.f)),
      .lifetime = 800,
      .size = 4.f,
      .size_velocity = 36.f,
      .rotation = RandomRadian(),
      .rotation_velocity = RandomRangef(.2f, .8f),
      .color = Vec3(.5f, .5f, .5f),
      .lighting = 1.f,
    });
  }

  Cg_AddSample(cgi.stage, &(const s_play_sample_t) {
    .origin = sound_origin,
    .sample = cg_sample_rocketlauncher_fire,
    .entity = sound_entity,
    .pitch = RandomRangei(-3, 4)
  });
}

static void Cg_RaceReplay_HyperblasterSpawnPresentation(
    const vec3_t muzzle, const vec3_t direction,
    const vec3_t sound_origin, const cl_entity_t *sound_entity) {
  const vec3_t color = ColorHSV(204.f, .8f, 1.f).vec3;

  Cg_AddLight(&(cg_light_t) {
    .origin = muzzle,
    .radius = 120.f,
    .color = color,
    .intensity = 2.f,
    .decay = 300,
  });

  Cg_AddSprite(&(cg_sprite_t) {
    .atlas_image = cg_sprite_impact_spark_01_dot,
    .origin = Vec3_Fmaf(muzzle, 2.f, direction),
    .rotation = RandomRadian(),
    .size = 24.f,
    .size_velocity = -80.f,
    .lifetime = 100.f,
    .color = color,
  });

  for (int32_t i = 0; i < 2; i++) {
    Cg_AddSprite(&(cg_sprite_t) {
      .atlas_image = cg_sprite_impact_spark_01_dot,
      .origin = Vec3_Fmaf(muzzle, 2.f, direction),
      .velocity = Vec3_Scale(
        Vec3_Mix(Vec3_RandomDir(), direction, .7f),
        RandomRangef(60.f, 120.f)),
      .size = RandomRangef(2.f, 4.f),
      .size_velocity = -10.f,
      .lifetime = RandomRangef(80.f, 200.f),
      .color = color,
    });
  }

  Cg_AddSample(cgi.stage, &(const s_play_sample_t) {
    .origin = sound_origin,
    .sample = cg_sample_hyperblaster_fire,
    .entity = sound_entity,
    .pitch = RandomRangei(-5, 6)
  });
}

static void Cg_RaceReplay_RocketImpactPresentation(
    const vec3_t origin, const vec3_t normal) {
  if (cgi.PointContents(origin) & CONTENTS_MASK_LIQUID) {
    for (int32_t i = 0; i < 16; i++) {
      const vec3_t start = Vec3_Add(origin, Vec3_RandomRange(-16.f, 16.f));
      const vec3_t end = Vec3_Fmaf(
        start, RandomRangef(64.f, 192.f), Vec3_RandomDir());
      Cg_BubbleTrail(NULL, start, end, 1.f);
    }
  } else {
    for (int32_t i = 0; i < 150; i++) {
      const uint32_t lifetime = 3000 + Randomf() * 500;
      const float size = 2.f + Randomf() * 2.f;
      if (!Cg_AddSprite(&(cg_sprite_t) {
          .atlas_image = cg_sprite_particle2,
          .origin = Vec3_Add(origin, Vec3_RandomRange(-16.f, 16.f)),
          .velocity = Vec3_RandomRange(-400.f, 400.f),
          .acceleration.z = -SPRITE_GRAVITY * 2.f,
          .lifetime = lifetime,
          .size = size,
          .size_velocity = -size / MILLIS_TO_SECONDS(lifetime),
          .bounce = .4f,
          .color = ColorHSV(RandomRangef(10.f, 50.f), .9f, .8f).vec3,
          .lighting = .35f,
        })) {
        break;
      }
    }
  }

  Cg_AddSprite(&(cg_sprite_t) {
    .origin = origin,
    .animation = cg_sprite_explosion,
    .lifetime = Cg_AnimationLifetime(cg_sprite_explosion, 40),
    .size = 150.f,
    .size_velocity = 40.f,
    .rotation = RandomRadian(),
    .color = Vec3(.5f, .5f, .5f),
  });
  Cg_AddSprite(&(cg_sprite_t) {
    .origin = origin,
    .animation = cg_sprite_explosion_ring_02,
    .lifetime = Cg_AnimationLifetime(cg_sprite_explosion_ring_02, 20),
    .size = 100.f,
    .size_velocity = 700.f,
    .size_acceleration = -700.f,
    .rotation = RandomRadian(),
    .color = Vec3(.5f, .5f, .5f),
    .dir = normal
  });
  Cg_AddSprite(&(cg_sprite_t) {
    .origin = origin,
    .lifetime = 600,
    .size = 300.f,
    .rotation = RandomRadian(),
    .atlas_image = cg_sprite_explosion_glow,
    .color = Vec3(.4f, .4f, .4f),
    .lighting = .55f
  });
  Cg_AddDecal(&(r_decal_t) {
    .image = cg_decal_burn[Randomi() % lengthof(cg_decal_burn)],
    .origin = origin,
    .radius = RandomRangef(32.f, 64.f),
    .color = Color4f(0.f, 0.f, 0.f, .5f + Randomf() * .4f),
    .lifetime = 16000 + Randomf() * 8000,
    .rotation = RandomRadian()
  });
  Cg_AddLight(&(const cg_light_t) {
    .origin = origin,
    .radius = 360.f,
    .color = Vec3(.9f, .6f, .3f),
    .intensity = 6.f,
    .decay = 1600
  });
  Cg_AddSample(cgi.stage, &(const s_play_sample_t) {
    .sample = cg_sample_explosion,
    .origin = origin,
  });
}

static void Cg_RaceReplay_HyperblasterImpactPresentation(
    const vec3_t origin, const vec3_t normal) {
  const vec3_t color = ColorHSV(204.f, .6f, 1.f).vec3;

  for (uint32_t i = 0; i < 6; i++) {
    Cg_AddSprite(&(cg_sprite_t) {
      .origin = origin,
      .animation = cg_sprite_electro_01,
      .lifetime = Cg_AnimationLifetime(cg_sprite_electro_01, 20),
      .size = 50.f,
      .size_velocity = 400.f,
      .rotation = RandomRadian(),
      .dir = Vec3_RandomRange(-1.f, 1.f),
      .color = color,
      .lighting = .3f
    });
  }
  Cg_AddSprite(&(cg_sprite_t) {
    .origin = origin,
    .atlas_image = cg_sprite_flash,
    .lifetime = 150,
    .size = 100.f,
    .rotation = RandomRadian(),
    .color = color,
  });
  Cg_AddDecal(&(r_decal_t) {
    .image = cg_decal_burn[Randomi() % lengthof(cg_decal_burn)],
    .origin = origin,
    .radius = RandomRangef(12.f, 18.f),
    .color = Color3fv(ColorHSV(225.f, .7f, .8f).vec3),
    .lifetime = 3000 + Randomf() * 3000,
    .rotation = RandomRadian()
  });
  Cg_AddLight(&(cg_light_t) {
    .origin = Vec3_Add(origin, normal),
    .radius = 200.f,
    .color = Vec3(.4f, .7f, 1.f),
    .intensity = 3.f,
    .decay = 250,
  });
  Cg_AddSample(cgi.stage, &(const s_play_sample_t) {
    .sample = cg_sample_hyperblaster_hit,
    .origin = Vec3_Add(origin, normal),
  });
}

static void Cg_RaceReplay_ClearProjectiles(void) {
  memset(cg_race_replay_projectiles, 0,
         sizeof(cg_race_replay_projectiles));
}

void Cg_RaceReplay_Init(void) {
  Cg_RaceReplay_Clear();
  cgi.AddCmd("race", NULL, CMD_CGAME, NULL);
  cgi.AddCmd("replay", NULL, CMD_CGAME, NULL);
  cgi.AddCmd("raceline", NULL, CMD_CGAME, NULL);
  cgi.AddCmd("replay_control", NULL, CMD_CGAME, NULL);
  cgi.AddCmd("replay_cancel", NULL, CMD_CGAME, NULL);
}

void Cg_RaceReplay_Clear(void) {
  Race_ReplayClientCache_Clear(&cg_race_replay_cache);
  Cg_RaceReplay_ClearProjectiles();
}

void Cg_RaceReplay_LoadMedia(void) {
  cg_race_replay_rocket_model = cgi.LoadModel(
    "models/projectiles/rocket/tris");
  cg_race_replay_rocket_fly_sample = cgi.LoadSample(
    "projectiles/rocket/fly");
}

static bool Cg_RaceReplay_ReadPayload(uint8_t *payload, size_t *length) {
  const int32_t encoded_length = cgi.ReadByte();
  if (encoded_length < 1 ||
      encoded_length > (int32_t) RACE_REPLAY_TRANSPORT_MAX_PAYLOAD) {
    Cg_Warn("Invalid Race replay payload length\n");
    return false;
  }
  *length = (size_t) encoded_length;
  cgi.ReadData(payload, *length);
  return true;
}

bool Cg_RaceReplay_ParseMessage(const int32_t command) {
  if (command != SV_CMD_RACE_REPLAY_STATE &&
      command != SV_CMD_RACE_REPLAY_TELEMETRY &&
      command != SV_CMD_RACE_REPLAY_PROJECTILES &&
      command != SV_CMD_RACE_RACELINE) {
    return false;
  }
  uint8_t payload[RACE_REPLAY_TRANSPORT_MAX_PAYLOAD];
  size_t length;
  if (!Cg_RaceReplay_ReadPayload(payload, &length)) {
    return true;
  }
  race_replay_transport_result_t result;
  if (command == SV_CMD_RACE_REPLAY_STATE) {
    result = Race_ReplayClientCache_ApplyState(
      &cg_race_replay_cache, payload, length,
      (uint32_t) cgi.client->unclamped_time);
  } else if (command == SV_CMD_RACE_REPLAY_TELEMETRY) {
    result = Race_ReplayClientCache_ApplyTelemetry(
      &cg_race_replay_cache, payload, length);
    if (result == RACE_REPLAY_TRANSPORT_APPLIED) {
      Cg_RaceTraining_ReplayTelemetry(&cg_race_replay_cache.telemetry);
    }
  } else if (command == SV_CMD_RACE_RACELINE) {
    result = Race_ReplayClientCache_ApplyRaceline(
      &cg_race_replay_cache, payload, length);
  } else {
    race_replay_projectile_message_t message;
    if (!Race_ReplayProjectiles_Decode(
          payload, length, &message)) {
      result = RACE_REPLAY_TRANSPORT_MALFORMED;
    } else {
      const uint32_t previous_generation = cg_race_replay_cache.generation;
      result = Race_ReplayClientCache_ApplyProjectiles(
        &cg_race_replay_cache, payload, length);
      if (result == RACE_REPLAY_TRANSPORT_APPLIED) {
        if (message.op == RACE_REPLAY_PROJECTILE_MESSAGE_RESET ||
            (previous_generation &&
             previous_generation != message.generation)) {
          Cg_RaceReplay_ClearProjectiles();
        }
        for (size_t i = 0u; i < message.event_count; i++) {
          const race_replay_projectile_event_t *event = message.events + i;
          cg_race_replay_projectile_t *projectile = NULL;
          cg_race_replay_projectile_t *free_projectile = NULL;
          for (size_t j = 0u; j < lengthof(cg_race_replay_projectiles); j++) {
            cg_race_replay_projectile_t *candidate =
              cg_race_replay_projectiles + j;
            if (candidate->active && candidate->spawn.id == event->id) {
              projectile = candidate;
              break;
            }
            if (!candidate->active && !free_projectile) {
              free_projectile = candidate;
            }
          }

          if (event->operation == RACE_REPLAY_PROJECTILE_SPAWN) {
            projectile = free_projectile;
            if (!projectile) {
              Cg_RaceReplay_ClearProjectiles();
              result = RACE_REPLAY_TRANSPORT_MALFORMED;
              break;
            }
            memset(projectile, 0, sizeof(*projectile));
            projectile->spawn = *event;
            projectile->active = true;
            projectile->entity.current.number = -1;
            projectile->entity.current.client =
              cgi.client->frame.ps.client;
            projectile->entity.current.trail =
              event->kind == RACE_REPLAY_PROJECTILE_ROCKET
                ? TRAIL_ROCKET : TRAIL_HYPERBLASTER;
            projectile->entity.origin = event->origin;
            projectile->entity.previous_origin = event->origin;
            projectile->entity.current.origin = event->origin;
            projectile->entity.prev.origin = event->origin;
            projectile->entity.angles = Vec3_Euler(event->velocity);
            for (size_t j = 0u;
                 j < lengthof(projectile->entity.trail_origins); j++) {
              projectile->entity.trail_origins[j] = event->origin;
            }
            if (message.op == RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS) {
              const vec3_t direction = Vec3_Normalize(event->velocity);
              if (event->kind == RACE_REPLAY_PROJECTILE_ROCKET) {
                Cg_RaceReplay_RocketSpawnPresentation(
                  event->origin, direction, event->origin,
                  &projectile->entity);
              } else {
                Cg_RaceReplay_HyperblasterSpawnPresentation(
                  event->origin, direction, event->origin,
                  &projectile->entity);
              }
            }
          } else if (projectile) {
            projectile->active = false;
            if (message.op == RACE_REPLAY_PROJECTILE_MESSAGE_EVENTS &&
                event->operation == RACE_REPLAY_PROJECTILE_IMPACT) {
              if (event->kind == RACE_REPLAY_PROJECTILE_ROCKET) {
                Cg_RaceReplay_RocketImpactPresentation(
                  event->origin, event->normal);
              } else {
                Cg_RaceReplay_HyperblasterImpactPresentation(
                  event->origin, event->normal);
              }
            }
          }
        }
      }
    }
  }
  if (result == RACE_REPLAY_TRANSPORT_MALFORMED) {
    Cg_Warn(command == SV_CMD_RACE_REPLAY_STATE
      ? "Malformed Race replay state ignored\n"
      : command == SV_CMD_RACE_REPLAY_TELEMETRY
        ? "Malformed Race replay telemetry ignored\n"
        : command == SV_CMD_RACE_REPLAY_PROJECTILES
          ? "Malformed Race replay projectile message ignored\n"
          : "Malformed Race raceline message ignored\n");
  }
  return true;
}

bool Cg_ReplayActive(void) {
  return (cg_race_replay_cache.state.flags & RACE_REPLAY_STATE_ACTIVE) != 0;
}

bool Cg_ReplayTimeline(uint32_t *generation, uint32_t *playhead_ms,
                       uint32_t *frame_cursor, bool *paused) {
  if (!Cg_ReplayActive() || !cg_race_replay_cache.telemetry_valid) {
    return false;
  }
  if (generation) {
    *generation = cg_race_replay_cache.telemetry.generation;
  }
  if (playhead_ms) {
    *playhead_ms = cg_race_replay_cache.telemetry.playhead_ms;
  }
  if (frame_cursor) {
    *frame_cursor = cg_race_replay_cache.telemetry.frame_cursor;
  }
  if (paused) {
    *paused = (cg_race_replay_cache.state.flags &
               RACE_REPLAY_STATE_PAUSED) != 0;
  }
  return true;
}

bool Cg_RaceReplay_Telemetry(pm_state_t *pm,
                             race_strafe_sample_t *strafe_helper,
                             int16_t *input_flags) {
  if (!Cg_ReplayActive() || !cg_race_replay_cache.telemetry_valid) {
    return false;
  }
  const race_replay_telemetry_message_t *telemetry =
    &cg_race_replay_cache.telemetry;
  if (pm) {
    memset(pm, 0, sizeof(*pm));
    pm->type = telemetry->pm_type;
    pm->flags = telemetry->pm_flags;
    pm->origin = telemetry->origin;
    pm->velocity = telemetry->velocity;
  }
  if (strafe_helper) {
    *strafe_helper = telemetry->strafe_helper;
  }
  if (input_flags) {
    *input_flags = telemetry->input_flags;
  }
  return true;
}

static void Cg_RaceReplay_AddRacelineBeam(
    const vec3_t start, const vec3_t end, const float size,
    const vec3_t color) {
  cgi.AddBeam(cgi.view, &(const r_beam_t) {
    .start = start,
    .end = end,
    .color = color,
    .image = cg_beam_line,
    .size = size,
    .stretch = 1.f,
    .lighting = 0.f
  });
}

static void Cg_RaceReplay_AddRacelineHead(const vec3_t head) {
  cgi.AddSprite(cgi.view, &(const r_sprite_t) {
    .origin = head,
    .size = CG_RACE_RACELINE_HEAD_GLOW_SIZE,
    .media = (r_media_t *) cg_sprite_particle,
    .color = Vec3(0.8f, 0.12f, 0.01f),
    .lighting = 0.f
  });
  cgi.AddSprite(cgi.view, &(const r_sprite_t) {
    .origin = head,
    .size = CG_RACE_RACELINE_HEAD_CORE_SIZE,
    .media = (r_media_t *) cg_sprite_particle,
    .color = Vec3(1.f, 0.55f, 0.08f),
    .lighting = 0.f
  });
}

static void Cg_RaceReplay_DrawRaceline(void) {
  if (!cg_race_replay_cache.raceline_complete) {
    return;
  }
  const race_replay_state_message_t *state = &cg_race_replay_cache.state;
  const bool follows_playback =
    (state->flags & RACE_REPLAY_STATE_ACTIVE) &&
    state->replay_id == cg_race_replay_cache.raceline_replay_id;
  const player_state_t *ps = &cgi.client->frame.ps;
  uint32_t head_ms = Race_WireElapsed(
    ps->stats[STAT_RACE_ELAPSED_LOW],
    ps->stats[STAT_RACE_ELAPSED_HIGH]);
  if (follows_playback) {
    head_ms = Race_ReplayState_PresentationTime(
      state, cg_race_replay_cache.state_received_time,
      (uint32_t) cgi.client->unclamped_time);
  }
  vec3_t line[RACE_REPLAY_RACELINE_MAX_POINTS + 2u];
  const size_t count = Race_ReplayRaceline_BuildWindow(
    cg_race_replay_cache.raceline_points,
    cg_race_replay_cache.raceline_total_points,
    head_ms, CG_RACE_RACELINE_TRAIL_MSEC, false,
    line, lengthof(line));
  if (!count) {
    return;
  }
  for (size_t i = 0u; i < count; i++) {
    line[i].z += CG_RACE_RACELINE_Z_OFFSET;
  }
  if (count >= 2u) {
    for (size_t i = 1u; i < count; i++) {
      Cg_RaceReplay_AddRacelineBeam(
        line[i - 1u], line[i], CG_RACE_RACELINE_GLOW_SIZE,
        Vec3(.32f, .04f, .01f));
      Cg_RaceReplay_AddRacelineBeam(
        line[i - 1u], line[i], CG_RACE_RACELINE_CORE_SIZE,
        Vec3(1.f, .42f, .025f));
    }
  }
  Cg_RaceReplay_AddRacelineHead(line[count - 1u]);
}

static void Cg_RaceReplay_DrawProjectiles(void) {
  if (!Cg_ReplayActive()) {
    return;
  }

  const race_replay_state_message_t *state = &cg_race_replay_cache.state;
  const bool paused = (state->flags & RACE_REPLAY_STATE_PAUSED) != 0;
  const uint32_t playhead = Race_ReplayState_PresentationTime(
    state, cg_race_replay_cache.state_received_time,
    (uint32_t) cgi.client->unclamped_time);
  for (size_t i = 0u; i < lengthof(cg_race_replay_projectiles); i++) {
    cg_race_replay_projectile_t *projectile =
      cg_race_replay_projectiles + i;
    if (!projectile->active || playhead < projectile->spawn.time_ms) {
      continue;
    }

    const float seconds =
      (playhead - projectile->spawn.time_ms) * .001f;
    const vec3_t origin = Vec3_Fmaf(
      projectile->spawn.origin, seconds, projectile->spawn.velocity);
    projectile->entity.previous_origin = projectile->entity.origin;
    projectile->entity.prev.origin = projectile->entity.current.origin;
    projectile->entity.origin = origin;
    projectile->entity.current.origin = origin;

    if (!paused) {
      Cg_EntityTrail(&projectile->entity);
    } else if (projectile->spawn.kind ==
               RACE_REPLAY_PROJECTILE_HYPERBLASTER) {
      cgi.AddSprite(cgi.view, &(const r_sprite_t) {
        .media = (r_media_t *) cg_sprite_blob_01,
        .origin = origin,
        .size = 16.f,
        .color = Vec3(.5f, .85f, 1.f),
        .lighting = .1f
      });
      cgi.AddSprite(cgi.view, &(const r_sprite_t) {
        .media = (r_media_t *) cg_sprite_particle,
        .origin = origin,
        .size = 8.f,
        .color = Vec3(.8f, .95f, 1.f),
        .lighting = 0.f
      });
    }

    if (projectile->spawn.kind == RACE_REPLAY_PROJECTILE_ROCKET) {
      if (cg_race_replay_rocket_model) {
        cgi.AddEntity(cgi.view, &(const r_entity_t) {
          .id = projectile,
          .origin = origin,
          .angles = projectile->entity.angles,
          .scale = 1.f,
          .model = cg_race_replay_rocket_model,
          .color = Vec4(1.f, 1.f, 1.f, 1.f)
        });
      }
      if (!paused && cg_race_replay_rocket_fly_sample) {
        Cg_AddSample(cgi.stage, &(const s_play_sample_t) {
          .sample = cg_race_replay_rocket_fly_sample,
          .origin = origin,
          .velocity = projectile->spawn.velocity,
          .flags = S_PLAY_LOOP | S_PLAY_FRAME,
          .entity = &projectile->entity
        });
      }
    }
  }
}

void Cg_RaceReplay_PopulateScene(void) {
  Cg_RaceReplay_DrawProjectiles();
  Cg_RaceReplay_DrawRaceline();
}

static const char *Cg_RaceReplay_SourceLabel(
  const race_replay_state_message_t *state) {
  switch (state->source) {
    case RACE_REPLAY_SOURCE_PERSONAL_BEST:
      return "PB";
    case RACE_REPLAY_SOURCE_WORLD_RECORD:
      return "WR";
    default:
      return "ID";
  }
}

static const char *Cg_RaceReplay_SpeedLabel(const race_replay_speed_t speed) {
  static const char *labels[RACE_REPLAY_SPEED_TOTAL] = {
    "0.25x", "0.5x", "1x", "2x", "4x"
  };
  return speed < RACE_REPLAY_SPEED_TOTAL ? labels[speed] : "?";
}

static void Cg_RaceReplay_FormatTime(const uint32_t time_ms,
                                     char *output, const size_t size) {
  snprintf(output, size, "%u:%02u.%03u",
           time_ms / 60000u, time_ms / 1000u % 60u, time_ms % 1000u);
}

/**
 * @brief Resolves the key cap for `command`.
 * @remarks The UNBOUND fallback is rendered as the cap text, so an unbound
 * control still reads as a row rather than vanishing.
 */
static void Cg_RaceReplay_ControlLabel(char *output, const size_t size,
                                       const char *command) {
  const SDL_Scancode code = cgi.KeyForBind(SDL_SCANCODE_UNKNOWN, command);

  q_strlcpy(output, code == SDL_SCANCODE_UNKNOWN
    ? "UNBOUND" : cgi.KeyName(code), size);
}

static bool Cg_RaceReplay_HudVisible(const player_state_t *ps) {
  const char *vote = cgi.ConfigString(CS_RACE_VOTE_INFO);
  const char *vote_menu = cgi.ConfigString(CS_RACE_VOTE_MENU);
  return ps && Cg_ReplayActive() && cgi.GetKeyDest() == KEY_GAME &&
         !ps->stats[STAT_SCORES] && (!vote || !*vote) &&
         (!vote_menu || !*vote_menu);
}

/**
 * @return The one accent that drives the tag, status dot, status label,
 * progress fill and playhead together.
 */
static color_t Cg_RaceReplay_Accent(const bool completed, const bool paused) {
  if (completed) {
    return Color4b(0xb8, 0xc2, 0xcc, 0xff);
  }
  if (paused) {
    return Cg_RaceHud_Gold();
  }
  return Cg_RaceHud_Cyan();
}

/**
 * @brief Draws the centered top stack: the replay tag and route, then the
 * cursor time. No records row -- the run final time lives with the timeline.
 */
static void Cg_RaceReplay_DrawTopStack(
    const race_replay_state_message_t *state, const char *cursor) {
  const int32_t center = cgi.context->w / 2;
  const int32_t gap = Cg_RaceHud_Scale(RACE_HUD_STACK_GAP);
  int32_t y = Cg_RaceHud_Edge();

  char tag[32];
  q_snprintf(tag, sizeof(tag), "REPLAY%s%s", RACE_HUD_SEPARATOR,
             Cg_RaceReplay_SourceLabel(state));

  const char *map_name = cgi.ConfigString(CS_MESSAGE);
  if (!map_name || !*map_name) {
    map_name = "unknown";
  }

  int32_t tag_height, route_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &tag_height);
  const int32_t tag_width = cgi.StringWidth(tag);
  Cg_RaceHud_BindFont(RACE_FONT_ROUTE, &route_height);
  const int32_t route_width = cgi.StringWidth(map_name);

  const int32_t mode_gap = Cg_RaceHud_Scale(RACE_HUD_MODE_GAP);
  const int32_t row_height = Maxi(tag_height, route_height);
  const int32_t row_x = center - (tag_width + mode_gap + route_width) / 2;
  const color_t accent = Cg_RaceReplay_Accent(
    state->flags & RACE_REPLAY_STATE_COMPLETED,
    state->flags & RACE_REPLAY_STATE_PAUSED);

  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawShadowedString(row_x, y + (row_height - tag_height) / 2,
                                tag, accent);
  Cg_RaceHud_BindFont(RACE_FONT_ROUTE, NULL);
  Cg_RaceHud_DrawShadowedString(row_x + tag_width + mode_gap,
                                y + (row_height - route_height) / 2,
                                map_name, Color4f(1.f, 1.f, 1.f, .66f));
  y += row_height + gap;

  Cg_RaceHud_BindFont(RACE_FONT_TIMER, NULL);
  Cg_RaceHud_DrawCentered(center, y, cursor,
                          Color4f(.957f, .973f, .984f, .94f));
}

/**
 * @brief Draws the timeline: the head row, the scrub track, and the meta row.
 */
static void Cg_RaceReplay_DrawTimeline(
    const race_replay_state_message_t *state, const uint32_t playhead,
    const char *cursor, const char *duration, const color_t accent,
    const char *status) {
  const int32_t width = Mini(Cg_RaceHud_Scale(1680.f),
                             cgi.context->w - Cg_RaceHud_Edge() * 2);
  const int32_t x = cgi.context->w / 2 - width / 2;
  const int32_t track_y = cgi.context->h - Cg_RaceHud_Edge() -
                          Cg_RaceHud_Scale(108.f);
  const int32_t track_height = Cg_RaceHud_Scale(4.f);

  // Head row: the status light and speed badge at the left end, the run
  // identity and its final time at the right.
  int32_t head_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &head_height);
  const int32_t head_y = track_y - Cg_RaceHud_Scale(15.f) - head_height;
  const int32_t dot = Cg_RaceHud_Scale(9.f);

  cgi.Draw2DFill(x, head_y + (head_height - dot) / 2, dot, dot, accent);
  int32_t head_x = x + dot + Cg_RaceHud_Scale(11.f);
  Cg_RaceHud_DrawShadowedString(head_x, head_y, status, accent);
  head_x += cgi.StringWidth(status) + Cg_RaceHud_Scale(18.f);

  const char *speed = Cg_RaceReplay_SpeedLabel(state->speed);
  Cg_RaceHud_BindFont(RACE_FONT_VALUE, NULL);
  Cg_RaceHud_DrawShadowedString(head_x, head_y, speed, color_white);

  char final_time[24];
  Cg_RaceReplay_FormatTime(state->duration_ms, final_time,
                           sizeof(final_time));

  int32_t label_height;
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, &label_height);
  const int32_t gap = Cg_RaceHud_Scale(8.f);
  const int32_t run_by = cgi.StringWidth("run by");
  const int32_t final_tag = cgi.StringWidth("final");
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);
  const int32_t name_width = cgi.StringWidth(state->display_name);
  const int32_t final_width = cgi.StringWidth(final_time);

  const int32_t identity_width = run_by + gap + name_width +
                                 Cg_RaceHud_Scale(24.f) + final_tag + gap +
                                 final_width;
  int32_t identity_x = x + width - identity_width;

  Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
  Cg_RaceHud_DrawShadowedString(identity_x, head_y, "run by",
                                Color4f(1.f, 1.f, 1.f, .46f));
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);
  Cg_RaceHud_DrawShadowedString(identity_x + run_by + gap, head_y,
                                state->display_name, color_white);
  identity_x += run_by + gap + name_width + Cg_RaceHud_Scale(24.f);
  Cg_RaceHud_BindFont(RACE_FONT_LABEL, NULL);
  Cg_RaceHud_DrawShadowedString(identity_x, head_y, "final",
                                Color4f(1.f, 1.f, 1.f, .46f));
  Cg_RaceHud_BindFont(RACE_FONT_RECORD, NULL);
  Cg_RaceHud_DrawShadowedString(identity_x + final_tag + gap, head_y,
                                final_time, Cg_RaceHud_Gold());

  // Track. On COMPLETE the progress and playhead pin to the far end.
  const bool completed = state->flags & RACE_REPLAY_STATE_COMPLETED;
  const float progress = completed
    ? 1.f
    : state->duration_ms
      ? Clampf((float) playhead / state->duration_ms, 0.f, 1.f)
      : 0.f;

  cgi.Draw2DFill(x, track_y, width, track_height,
                 Color4f(1.f, 1.f, 1.f, .15f));
  cgi.Draw2DFill(x, track_y, (int32_t) (width * progress), track_height,
                 accent);

  const int32_t playhead_width = Cg_RaceHud_Scale(5.f);
  const int32_t playhead_height = Cg_RaceHud_Scale(22.f);
  const int32_t playhead_x = x + (int32_t) (width * progress) -
                             playhead_width / 2;
  cgi.Draw2DFill(Mini(Maxi(playhead_x, x), x + width - playhead_width),
                 track_y - Cg_RaceHud_Scale(9.f), playhead_width,
                 playhead_height, accent);

  // Meta row.
  Cg_RaceHud_BindFont(RACE_FONT_VALUE, NULL);
  const int32_t meta_y = track_y + track_height + Cg_RaceHud_Scale(16.f);
  Cg_RaceHud_DrawShadowedString(x, meta_y, cursor,
                                Color4f(1.f, 1.f, 1.f, .88f));

  char frame[48];
  q_snprintf(frame, sizeof(frame), "frame %u",
             cg_race_replay_cache.telemetry_valid
               ? cg_race_replay_cache.telemetry.frame_cursor + 1u : 0u);

  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawCentered(x + width / 2, meta_y, frame,
                          Color4f(1.f, 1.f, 1.f, .5f));
  Cg_RaceHud_DrawRightAligned(x + width, meta_y, duration,
                              Color4f(1.f, 1.f, 1.f, .5f));
}

/**
 * @brief Draws one key cap and its label, returning the width consumed.
 */
static int32_t Cg_RaceReplay_DrawHint(const int32_t x, const int32_t y,
                                      const char *command,
                                      const char *label) {
  char cap[48];
  Cg_RaceReplay_ControlLabel(cap, sizeof(cap), command);

  int32_t text_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &text_height);
  const int32_t pad_x = Cg_RaceHud_Scale(9.f);
  const int32_t pad_y = Cg_RaceHud_Scale(5.f);
  const int32_t cap_width = cgi.StringWidth(cap) + pad_x * 2;
  const int32_t cap_height = text_height + pad_y * 2;

  cgi.Draw2DFill(x, y, cap_width, cap_height, Color4f(1.f, 1.f, 1.f, .1f));
  Cg_RaceHud_DrawShadowedString(x + pad_x, y + pad_y, cap,
                                Color4f(1.f, 1.f, 1.f, .88f));

  const int32_t label_x = x + cap_width + Cg_RaceHud_Scale(9.f);
  Cg_RaceHud_DrawShadowedString(label_x, y + pad_y, label,
                                Color4f(1.f, 1.f, 1.f, .5f));

  return cap_width + Cg_RaceHud_Scale(9.f) + cgi.StringWidth(label);
}

/**
 * @brief Draws the control hint row, centered along the bottom edge.
 */
static void Cg_RaceReplay_DrawHints(const bool paused) {
  const struct {
    const char *command;
    const char *label;
  } hints[] = {
    { "replay_control pause", paused ? "Resume" : "Pause" },
    { "replay_control restart", "Restart" },
    { "replay_control back", "Back" },
    { "replay_control forward", "Forward" },
    { "replay_cancel", "Exit replay" }
  };

  int32_t text_height;
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &text_height);
  const int32_t pad_x = Cg_RaceHud_Scale(9.f);
  const int32_t pad_y = Cg_RaceHud_Scale(5.f);
  const int32_t hint_gap = Cg_RaceHud_Scale(32.f);

  int32_t total = 0;
  for (size_t i = 0; i < lengthof(hints); i++) {
    char cap[48];
    Cg_RaceReplay_ControlLabel(cap, sizeof(cap), hints[i].command);
    total += cgi.StringWidth(cap) + pad_x * 2 + Cg_RaceHud_Scale(9.f) +
             cgi.StringWidth(hints[i].label);
    if (i + 1u < lengthof(hints)) {
      total += hint_gap;
    }
  }

  const int32_t y = cgi.context->h - Cg_RaceHud_Edge() - text_height -
                    pad_y * 2;
  int32_t x = cgi.context->w / 2 - total / 2;

  for (size_t i = 0; i < lengthof(hints); i++) {
    x += Cg_RaceReplay_DrawHint(x, y, hints[i].command, hints[i].label) +
         hint_gap;
  }
}

/**
 * @brief Draws the replay speed readout in the live HUD slot.
 * @remarks Flat cyan: the accel color-coding of §5 is deliberately not
 * applied, because a viewer is not correcting their strafing and the flicker
 * would be noise.
 */
static void Cg_RaceReplay_DrawSpeed(void) {

  // The strafe helper keeps drawing through a replay -- it is fed the replay
  // telemetry -- so the one-readout rule of §6 applies here exactly as it does
  // to the live HUD.
  if (Cg_StrafeHelper_OwnsSpeedReadout()) {
    return;
  }

  pm_state_t pm;
  if (!Cg_RaceReplay_Telemetry(&pm, NULL, NULL)) {
    return;
  }

  char value[16];
  q_snprintf(value, sizeof(value), "%d",
             Cg_Race_HorizontalSpeed(pm.velocity));

  int32_t value_height, unit_height;
  Cg_RaceHud_BindFont(RACE_FONT_SPEED, &value_height);
  const int32_t value_width = cgi.StringWidth(value);
  Cg_RaceHud_BindFont(RACE_FONT_BODY, &unit_height);
  const int32_t unit_width = cgi.StringWidth("ups");

  const int32_t gap = Cg_RaceHud_Scale(10.f);
  const int32_t y = cgi.context->h / 2 + Cg_RaceHud_Scale(80.f);
  const int32_t x = cgi.context->w / 2 - (value_width + gap + unit_width) / 2;

  Cg_RaceHud_BindFont(RACE_FONT_SPEED, NULL);
  Cg_RaceHud_DrawShadowedString(x, y, value, Cg_RaceHud_Cyan());
  Cg_RaceHud_BindFont(RACE_FONT_BODY, NULL);
  Cg_RaceHud_DrawShadowedString(x + value_width + gap,
                                y + (value_height - unit_height), "ups",
                                Color4f(1.f, 1.f, 1.f, .5f));
}

void Cg_RaceReplay_DrawHud(const player_state_t *ps) {
  if (!Cg_RaceReplay_HudVisible(ps)) {
    return;
  }

  const race_replay_state_message_t *state = &cg_race_replay_cache.state;
  const uint32_t playhead = Race_ReplayState_PresentationTime(
    state, cg_race_replay_cache.state_received_time,
    (uint32_t) cgi.client->unclamped_time);
  const bool completed = state->flags & RACE_REPLAY_STATE_COMPLETED;
  const bool paused = state->flags & RACE_REPLAY_STATE_PAUSED;
  const char *status = completed ? "COMPLETE" : paused ? "PAUSED" : "PLAYING";
  const color_t accent = Cg_RaceReplay_Accent(completed, paused);

  char cursor[24], duration[24];
  Cg_RaceReplay_FormatTime(playhead, cursor, sizeof(cursor));
  Cg_RaceReplay_FormatTime(state->duration_ms, duration, sizeof(duration));

  Cg_RaceReplay_DrawTopStack(state, cursor);
  Cg_RaceReplay_DrawSpeed();
  Cg_RaceReplay_DrawTimeline(state, playhead, cursor, duration, accent,
                             status);
  Cg_RaceReplay_DrawHints(paused);

  int16_t flags;
  const int32_t x = Cg_RaceHud_Edge();
  const int32_t bottom = cgi.context->h - Cg_RaceHud_Edge();
  if (Cg_RaceReplay_Telemetry(NULL, NULL, &flags) &&
      Race_InputFlagsValid(flags)) {
    Cg_InputViewer_DrawCluster(x, bottom, flags);
  } else {
    Cg_InputViewer_DrawUnavailable(x, bottom);
  }

  cgi.BindFont(NULL, NULL, NULL);
}
