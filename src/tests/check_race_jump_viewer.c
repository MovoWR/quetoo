#include <check.h>

#include "shared/shared.h"
#include "game/common/bg_pmove.h"
#include "cgame/race/cg_jump_viewer_math.h"

static cg_jump_sample_t Sample(uint64_t time, vec3_t origin, bool grounded) {
  return (cg_jump_sample_t) {
    .origin = origin,
    .time = time,
    .source = CG_JUMP_SOURCE_PREDICTION,
    .identity = 1,
    .lifecycle = 1,
    .grounded = grounded,
    .valid = true,
    .launch = CG_JUMP_LAUNCH_NORMAL
  };
}

static void Update(cg_jump_tracker_t *tracker, cg_jump_sample_t sample) {
  Cg_JumpTracker_Update(tracker, &sample);
}

static void CompleteJump(cg_jump_tracker_t *tracker) {
  Update(tracker, Sample(0, Vec3(0.f, 0.f, 0.f), true));
  Update(tracker, Sample(10, Vec3(0.f, 0.f, 0.f), false));
  Update(tracker, Sample(20, Vec3(30.f, 40.f, 24.f), false));
  Update(tracker, Sample(30, Vec3(60.f, 80.f, 0.f), true));
  ck_assert(tracker->completed.valid);
  ck_assert_int_eq(tracker->completed.launch, CG_JUMP_LAUNCH_NORMAL);
  ck_assert_float_eq_tol(tracker->completed.length, 100.f, 0.001f);
  ck_assert_float_eq_tol(tracker->completed.peak, 24.f, 0.001f);
  ck_assert_float_eq_tol(tracker->completed.path, 110.9234f, 0.001f);
  ck_assert_uint_eq(tracker->completed.airtime, 20u);
  ck_assert(!tracker->active);
  ck_assert(!tracker->live.valid);
}

static void AssertCleared(const cg_jump_tracker_t *tracker, bool has_previous) {
  ck_assert(!tracker->active);
  ck_assert_int_eq(tracker->has_previous, has_previous);
  ck_assert(!tracker->live.valid);
  ck_assert_int_eq(tracker->live.launch, CG_JUMP_LAUNCH_AIRBORNE);
  ck_assert_float_eq_tol(tracker->live.length, 0.f, 0.001f);
  ck_assert_float_eq_tol(tracker->live.peak, 0.f, 0.001f);
  ck_assert_float_eq_tol(tracker->live.path, 0.f, 0.001f);
  ck_assert_uint_eq(tracker->live.airtime, 0u);
  ck_assert(!tracker->completed.valid);
  ck_assert_int_eq(tracker->completed.launch, CG_JUMP_LAUNCH_AIRBORNE);
  ck_assert_float_eq_tol(tracker->completed.length, 0.f, 0.001f);
  ck_assert_float_eq_tol(tracker->completed.peak, 0.f, 0.001f);
  ck_assert_float_eq_tol(tracker->completed.path, 0.f, 0.001f);
  ck_assert_uint_eq(tracker->completed.airtime, 0u);
}

START_TEST(check_team_transition_invalidation) {
  cg_jump_tracker_t tracker = {};
  CompleteJump(&tracker);
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), true);
  sample.lifecycle = 2;
  Update(&tracker, sample);
  AssertCleared(&tracker, true);
} END_TEST

START_TEST(check_spectator_transition_invalidation) {
  cg_jump_tracker_t tracker = {};
  CompleteJump(&tracker);
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), true);
  sample.lifecycle = 3;
  Update(&tracker, sample);
  AssertCleared(&tracker, true);
} END_TEST

START_TEST(check_respawn_transition_invalidation) {
  cg_jump_tracker_t tracker = {};
  CompleteJump(&tracker);
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), true);
  sample.lifecycle = 4;
  Update(&tracker, sample);
  AssertCleared(&tracker, true);
} END_TEST

START_TEST(check_ramp_trick_classification_is_conservative) {
  ck_assert_int_eq(Cg_JumpViewer_ClassifyLaunch(PMF_JUMPED, -1.f),
                   CG_JUMP_LAUNCH_NORMAL);
  ck_assert_int_eq(Cg_JumpViewer_ClassifyLaunch(PMF_JUMPED | PMF_TIME_TRICK_START, -1.f),
                   CG_JUMP_LAUNCH_NORMAL);
  ck_assert_int_eq(Cg_JumpViewer_ClassifyLaunch(PMF_TIME_TRICK_START, 270.f),
                   CG_JUMP_LAUNCH_AIRBORNE);
} END_TEST

START_TEST(check_q2_jump_and_walkoff_classification) {
  ck_assert_int_eq(Cg_JumpViewer_ClassifyLaunch(PMF_JUMP_HELD, 270.f),
                   CG_JUMP_LAUNCH_NORMAL);
  ck_assert_int_eq(Cg_JumpViewer_ClassifyLaunch(PMF_JUMP_HELD, 0.f),
                   CG_JUMP_LAUNCH_AIRBORNE);
  ck_assert_int_eq(Cg_JumpViewer_ClassifyLaunch(PMF_JUMP_HELD, -1.f),
                   CG_JUMP_LAUNCH_AIRBORNE);
  ck_assert_int_eq(Cg_JumpViewer_ClassifyLaunch(0, 270.f),
                   CG_JUMP_LAUNCH_AIRBORNE);
} END_TEST

START_TEST(check_prediction_correction_does_not_complete_jump) {
  cg_jump_tracker_t tracker = {};
  cg_jump_sample_t ground = Sample(0, Vec3(0.f, 0.f, 0.f), true);
  ground.predicted = true;
  Update(&tracker, ground);
  cg_jump_sample_t air = Sample(10, Vec3(0.f, 0.f, 0.f), false);
  air.predicted = true;
  Update(&tracker, air);
  ck_assert(tracker.active);

  cg_jump_sample_t correction = Sample(10, Vec3(1.f, 0.f, 0.f), true);
  correction.predicted = true;
  Update(&tracker, correction);
  ck_assert(tracker.active);
  ck_assert(!tracker.completed.valid);
} END_TEST

START_TEST(check_time_crosses_uint32_boundary) {
  const uint64_t start = UINT32_MAX - 5ull;
  cg_jump_tracker_t tracker = {};
  Update(&tracker, Sample(start, Vec3(0.f, 0.f, 0.f), true));
  Update(&tracker, Sample(start + 10u, Vec3(0.f, 0.f, 0.f), false));
  Update(&tracker, Sample(start + 20u, Vec3(10.f, 0.f, 8.f), false));
  Update(&tracker, Sample(start + 30u, Vec3(20.f, 0.f, 0.f), true));
  ck_assert(tracker.completed.valid);
  ck_assert_uint_eq(tracker.completed.airtime, 20u);
} END_TEST

START_TEST(check_normal_jump) {
  cg_jump_tracker_t tracker = {};
  CompleteJump(&tracker);
} END_TEST

START_TEST(check_grounded_takeoff_origin_and_first_segment) {
  cg_jump_tracker_t tracker = {};

  Update(&tracker, Sample(0, Vec3(0.f, 0.f, 0.f), true));
  Update(&tracker, Sample(10, Vec3(0.f, 0.f, 4.f), false));
  Update(&tracker, Sample(20, Vec3(0.f, 0.f, 44.f), false));
  Update(&tracker, Sample(30, Vec3(0.f, 0.f, 0.f), true));

  ck_assert(tracker.completed.valid);
  ck_assert_float_eq_tol(tracker.takeoff_origin.z, 0.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.peak, 44.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.path, 88.f, 0.001f);
  ck_assert_uint_eq(tracker.completed.airtime, 20u);
} END_TEST

START_TEST(check_peak_is_invariant_to_first_airborne_sample_height) {
  cg_jump_tracker_t early = {};
  cg_jump_tracker_t late = {};

  Update(&early, Sample(0, Vec3(0.f, 0.f, 0.f), true));
  Update(&early, Sample(8, Vec3(0.f, 0.f, 2.f), false));
  Update(&early, Sample(100, Vec3(0.f, 0.f, 44.f), false));
  Update(&early, Sample(200, Vec3(0.f, 0.f, 0.f), true));

  Update(&late, Sample(0, Vec3(0.f, 0.f, 0.f), true));
  Update(&late, Sample(16, Vec3(0.f, 0.f, 4.f), false));
  Update(&late, Sample(100, Vec3(0.f, 0.f, 44.f), false));
  Update(&late, Sample(200, Vec3(0.f, 0.f, 0.f), true));

  ck_assert(early.completed.valid);
  ck_assert(late.completed.valid);
  ck_assert_float_eq_tol(early.completed.peak, 44.f, 0.001f);
  ck_assert_float_eq_tol(late.completed.peak, 44.f, 0.001f);
  ck_assert_float_eq_tol(early.completed.peak, late.completed.peak, 0.001f);
} END_TEST

START_TEST(check_landing_above_takeoff) {
  cg_jump_tracker_t tracker = {};
  Update(&tracker, Sample(0, Vec3(0.f, 0.f, 10.f), true));
  Update(&tracker, Sample(10, Vec3(0.f, 0.f, 10.f), false));
  Update(&tracker, Sample(20, Vec3(30.f, 40.f, 30.f), false));
  Update(&tracker, Sample(30, Vec3(60.f, 80.f, 20.f), true));
  ck_assert(tracker.completed.valid);
  ck_assert_int_eq(tracker.completed.launch, CG_JUMP_LAUNCH_NORMAL);
  ck_assert_float_eq_tol(tracker.completed.length, 100.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.peak, 20.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.path, 104.8416f, 0.001f);
  ck_assert_uint_eq(tracker.completed.airtime, 20u);
  ck_assert(!tracker.active);
  ck_assert(!tracker.live.valid);
} END_TEST

START_TEST(check_landing_below_takeoff) {
  cg_jump_tracker_t tracker = {};
  Update(&tracker, Sample(0, Vec3(0.f, 0.f, 10.f), true));
  Update(&tracker, Sample(10, Vec3(0.f, 0.f, 10.f), false));
  Update(&tracker, Sample(20, Vec3(30.f, 40.f, 30.f), false));
  Update(&tracker, Sample(30, Vec3(60.f, 80.f, 0.f), true));
  ck_assert(tracker.completed.valid);
  ck_assert_int_eq(tracker.completed.launch, CG_JUMP_LAUNCH_NORMAL);
  ck_assert_float_eq_tol(tracker.completed.length, 100.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.peak, 20.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.path, 112.1611f, 0.001f);
  ck_assert_uint_eq(tracker.completed.airtime, 20u);
  ck_assert(!tracker.active);
  ck_assert(!tracker.live.valid);
} END_TEST

START_TEST(check_walk_off_edge) {
  cg_jump_tracker_t tracker = {};
  Update(&tracker, Sample(0, Vec3(0.f, 0.f, 32.f), true));
  cg_jump_sample_t air = Sample(10, Vec3(10.f, 0.f, 30.f), false);
  air.launch = CG_JUMP_LAUNCH_AIRBORNE;
  Update(&tracker, air);
  Update(&tracker, Sample(30, Vec3(40.f, 0.f, 0.f), true));
  ck_assert(tracker.completed.valid);
  ck_assert_int_eq(tracker.completed.launch, CG_JUMP_LAUNCH_AIRBORNE);
  ck_assert_float_eq_tol(tracker.completed.length, 40.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.peak, 0.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.path, 52.6244f, 0.001f);
  ck_assert_uint_eq(tracker.completed.airtime, 20u);
  ck_assert(!tracker.active);
  ck_assert(!tracker.live.valid);
} END_TEST

START_TEST(check_multi_bounce_continuity) {
  cg_jump_tracker_t tracker = {};
  Update(&tracker, Sample(0, Vec3(0.f, 0.f, 0.f), true));
  Update(&tracker, Sample(10, Vec3(0.f, 0.f, 0.f), false));
  Update(&tracker, Sample(20, Vec3(20.f, 0.f, 10.f), false));
  Update(&tracker, Sample(30, Vec3(40.f, 0.f, 0.f), false));
  Update(&tracker, Sample(40, Vec3(60.f, 0.f, 15.f), false));
  Update(&tracker, Sample(50, Vec3(80.f, 0.f, 0.f), true));
  ck_assert(tracker.completed.valid);
  ck_assert_int_eq(tracker.completed.launch, CG_JUMP_LAUNCH_NORMAL);
  ck_assert_float_eq_tol(tracker.completed.length, 80.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.peak, 15.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.path, 94.7214f, 0.001f);
  ck_assert_uint_eq(tracker.completed.airtime, 40u);
  ck_assert(!tracker.active);
  ck_assert(!tracker.live.valid);
} END_TEST

START_TEST(check_direction_change_path) {
  cg_jump_tracker_t tracker = {};
  Update(&tracker, Sample(0, Vec3(0.f, 0.f, 0.f), true));
  Update(&tracker, Sample(10, Vec3(0.f, 0.f, 0.f), false));
  Update(&tracker, Sample(20, Vec3(100.f, 0.f, 12.f), false));
  Update(&tracker, Sample(30, Vec3(0.f, 0.f, 0.f), true));
  ck_assert(tracker.completed.valid);
  ck_assert_int_eq(tracker.completed.launch, CG_JUMP_LAUNCH_NORMAL);
  ck_assert_float_eq_tol(tracker.completed.length, 0.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.peak, 12.f, 0.001f);
  ck_assert_float_eq_tol(tracker.completed.path, 201.4349f, 0.001f);
  ck_assert_uint_eq(tracker.completed.airtime, 20u);
  ck_assert_float_gt(tracker.completed.path, tracker.completed.length);
  ck_assert(!tracker.active);
  ck_assert(!tracker.live.valid);
} END_TEST

static void InvalidateAndAssert(cg_jump_sample_t sample, bool has_previous) {
  cg_jump_tracker_t tracker = {};
  CompleteJump(&tracker);
  Update(&tracker, sample);
  AssertCleared(&tracker, has_previous);
}

START_TEST(check_teleport_invalidation) {
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), true);
  sample.teleport = true;
  InvalidateAndAssert(sample, false);
} END_TEST

START_TEST(check_dead_invalidation) {
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), true);
  sample.valid = false;
  InvalidateAndAssert(sample, false);
} END_TEST

START_TEST(check_noclip_invalidation) {
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), false);
  sample.valid = false;
  InvalidateAndAssert(sample, false);
} END_TEST

START_TEST(check_source_change_invalidation) {
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), true);
  sample.source = CG_JUMP_SOURCE_REPLAY;
  InvalidateAndAssert(sample, true);
} END_TEST

START_TEST(check_identity_change_invalidation) {
  cg_jump_sample_t sample = Sample(40, Vec3(60.f, 80.f, 0.f), true);
  sample.identity = 2;
  InvalidateAndAssert(sample, true);
} END_TEST

START_TEST(check_repeated_timestamp_invalidation) {
  InvalidateAndAssert(Sample(30, Vec3(60.f, 80.f, 0.f), true), true);
} END_TEST

START_TEST(check_large_discontinuity) {
  InvalidateAndAssert(Sample(40, Vec3(573.f, 80.f, 0.f), true), true);
} END_TEST

START_TEST(check_source_selection) {
  ck_assert_int_eq(Cg_JumpViewer_SelectSource(true, false, 0), CG_JUMP_SOURCE_PREDICTION);
  ck_assert_int_eq(Cg_JumpViewer_SelectSource(false, true, 0), CG_JUMP_SOURCE_REPLAY);
  ck_assert_int_eq(Cg_JumpViewer_SelectSource(false, false, 7), CG_JUMP_SOURCE_CHASE);
  ck_assert_int_eq(Cg_JumpViewer_SelectSource(false, false, 0), CG_JUMP_SOURCE_FRAME);
} END_TEST

START_TEST(check_result_selection) {
  cg_jump_tracker_t tracker = {};
  bool live = false;

  tracker.completed.valid = true;
  tracker.completed_time = 1000u;
  ck_assert_ptr_eq(Cg_JumpViewer_SelectResult(&tracker, 3999u, 3000u, &live), &tracker.completed);
  ck_assert(!live);
  ck_assert_ptr_eq(Cg_JumpViewer_SelectResult(&tracker, 4000u, 3000u, &live), &tracker.completed);
  ck_assert_ptr_null(Cg_JumpViewer_SelectResult(&tracker, 4001u, 3000u, &live));

  tracker.active = true;
  tracker.live.valid = true;
  ck_assert_ptr_eq(Cg_JumpViewer_SelectResult(&tracker, 9000u, 0u, &live), &tracker.live);
  ck_assert(live);
} END_TEST

START_TEST(check_result_formatting) {
  const cg_jump_result_t result = {
    .valid = true,
    .launch = CG_JUMP_LAUNCH_RAMP,
    .length = 384.24f,
    .peak = 52.66f,
    .path = 421.84f,
    .airtime = 630u
  };
  cg_jump_viewer_text_t text;

  Cg_JumpViewer_FormatResult(&result, true, true, &text);
  ck_assert_str_eq(text.header, "RAMP JUMP - LIVE");
  ck_assert_str_eq(text.length, "Length  384.2");
  ck_assert_str_eq(text.peak, "Peak     52.7");
  ck_assert_str_eq(text.air, "Air 0.63s | Path 421.8");

  Cg_JumpViewer_FormatResult(&result, false, false, &text);
  ck_assert_str_eq(text.header, "JUMP RESULT");
  ck_assert_str_eq(text.air, "Air 0.63s");
} END_TEST

START_TEST(check_visibility_policy) {
  ck_assert_int_eq(Cg_JumpViewer_Visibility(false, true, false, false, false, false, false),
                   CG_JUMP_VIEWER_HIDE);
  ck_assert_int_eq(Cg_JumpViewer_Visibility(true, false, false, false, false, false, false),
                   CG_JUMP_VIEWER_HIDE_AND_CLEAR);
  ck_assert_int_eq(Cg_JumpViewer_Visibility(true, true, true, false, false, false, false),
                   CG_JUMP_VIEWER_HIDE);
  ck_assert_int_eq(Cg_JumpViewer_Visibility(true, true, false, true, false, false, false),
                   CG_JUMP_VIEWER_HIDE);
  ck_assert_int_eq(Cg_JumpViewer_Visibility(true, true, false, false, true, false, false),
                   CG_JUMP_VIEWER_HIDE);
  ck_assert_int_eq(Cg_JumpViewer_Visibility(true, true, false, false, false, true, false),
                   CG_JUMP_VIEWER_HIDE);
  ck_assert_int_eq(Cg_JumpViewer_Visibility(true, true, false, false, false, false, true),
                   CG_JUMP_VIEWER_HIDE);
  ck_assert_int_eq(Cg_JumpViewer_Visibility(true, true, false, false, false, false, false),
                   CG_JUMP_VIEWER_DRAW);
} END_TEST

START_TEST(check_disabled_update_clear_policy) {
  ck_assert(Cg_JumpViewer_ClearOnUpdate(false, false));
  ck_assert(Cg_JumpViewer_ClearOnUpdate(true, false));
  ck_assert(!Cg_JumpViewer_ClearOnUpdate(true, true));
} END_TEST

START_TEST(check_replay_sample_clock_pause_speed_and_seek) {
  cg_jump_replay_sample_state_t state = {};
  ck_assert(Cg_JumpViewer_AcceptReplaySample(&state, 7u, 100u, 10u, false));
  ck_assert_uint_eq(Cg_JumpViewer_ReplayTelemetryTime(&state), 100u);
  ck_assert(!Cg_JumpViewer_AcceptReplaySample(&state, 7u, 100u, 20u, true));
  ck_assert(!Cg_JumpViewer_AcceptReplaySample(&state, 7u, 100u, 20u, true));
  ck_assert(Cg_JumpViewer_AcceptReplaySample(&state, 7u, 125u, 20u, false));
  ck_assert(!Cg_JumpViewer_AcceptReplaySample(&state, 7u, 112u, 21u, true));
  ck_assert_uint_eq(Cg_JumpViewer_ReplayTelemetryTime(&state), 125u);
  ck_assert(Cg_JumpViewer_AcceptReplaySample(&state, 7u, 130u, 21u, false));
  memset(&state, 0, sizeof(state));
  ck_assert(Cg_JumpViewer_AcceptReplaySample(&state, 7u, 50u, 4u, true));
  ck_assert(Cg_JumpViewer_AcceptReplaySample(&state, 8u, 0u, 0u, false));
  ck_assert(!Cg_JumpViewer_AcceptReplaySample(NULL, 8u, 0u, 0u, false));
  ck_assert(!Cg_JumpViewer_AcceptReplaySample(&state, 0u, 0u, 0u, false));
  ck_assert_uint_eq(Cg_JumpViewer_ReplayTelemetryTime(NULL), 0u);
} END_TEST

START_TEST(check_replay_paused_adjacent_frame_is_sampled) {
  cg_jump_replay_sample_state_t state = {};
  ck_assert(Cg_JumpViewer_AcceptReplaySample(&state, 9u, 80u, 2u, true));
  memset(&state, 0, sizeof(state));
  ck_assert(Cg_JumpViewer_AcceptReplaySample(&state, 9u, 80u, 3u, true));
  ck_assert(!Cg_JumpViewer_AcceptReplaySample(&state, 9u, 80u, 8u, true));
} END_TEST

START_TEST(check_panel_left_margin) {
  ck_assert_int_eq(Cg_JumpViewer_PanelX(640), 24);
  ck_assert_int_eq(Cg_JumpViewer_PanelX(1600), 24);
  ck_assert_int_eq(Cg_JumpViewer_PanelX(3840), 24);
} END_TEST

int32_t main(void) {
  TCase *tcase = tcase_create("jump_tracker");
  tcase_add_test(tcase, check_normal_jump);
  tcase_add_test(tcase, check_grounded_takeoff_origin_and_first_segment);
  tcase_add_test(tcase, check_peak_is_invariant_to_first_airborne_sample_height);
  tcase_add_test(tcase, check_team_transition_invalidation);
  tcase_add_test(tcase, check_spectator_transition_invalidation);
  tcase_add_test(tcase, check_respawn_transition_invalidation);
  tcase_add_test(tcase, check_ramp_trick_classification_is_conservative);
  tcase_add_test(tcase, check_q2_jump_and_walkoff_classification);
  tcase_add_test(tcase, check_prediction_correction_does_not_complete_jump);
  tcase_add_test(tcase, check_time_crosses_uint32_boundary);
  tcase_add_test(tcase, check_landing_above_takeoff);
  tcase_add_test(tcase, check_landing_below_takeoff);
  tcase_add_test(tcase, check_walk_off_edge);
  tcase_add_test(tcase, check_multi_bounce_continuity);
  tcase_add_test(tcase, check_direction_change_path);
  tcase_add_test(tcase, check_teleport_invalidation);
  tcase_add_test(tcase, check_dead_invalidation);
  tcase_add_test(tcase, check_noclip_invalidation);
  tcase_add_test(tcase, check_source_change_invalidation);
  tcase_add_test(tcase, check_identity_change_invalidation);
  tcase_add_test(tcase, check_repeated_timestamp_invalidation);
  tcase_add_test(tcase, check_large_discontinuity);
  tcase_add_test(tcase, check_source_selection);
  tcase_add_test(tcase, check_result_selection);
  tcase_add_test(tcase, check_result_formatting);
  tcase_add_test(tcase, check_visibility_policy);
  tcase_add_test(tcase, check_disabled_update_clear_policy);
  tcase_add_test(tcase, check_replay_sample_clock_pause_speed_and_seek);
  tcase_add_test(tcase, check_replay_paused_adjacent_frame_is_sampled);
  tcase_add_test(tcase, check_panel_left_margin);
  Suite *suite = suite_create("check_race_jump_viewer");
  suite_add_tcase(suite, tcase);
  SRunner *runner = srunner_create(suite);
  srunner_run_all(runner, CK_VERBOSE);
  const int32_t failed = srunner_ntests_failed(runner);
  srunner_free(runner);
  return failed;
}
