#include "globals.hpp"
#include "engine_prediction.hpp"
#include "animations.hpp"
#include "lagcomp.hpp"
#include "movement.hpp"
#include "game_movement.hpp"
#include "entlistener.hpp"
#include "exploits.hpp"
#include "event_logs.hpp"
#include "chams.hpp"
#include "anti_aim.hpp"
#include "penetration.hpp"
#include "resolver.hpp"
// OLD CODE (kept for revert):
// #include "neverlose.hpp"
#include "ragebot.hpp"
#include "rage_logger.hpp"
#include "multipoint.hpp"

void draw_hitbox__(c_cs_player* player, matrix3x4_t* bones, int idx, int idx2, bool dur = false)
{
	auto studio_model = HACKS->model_info->get_studio_model(player->get_model());
	if (!studio_model)
		return;

	auto hitbox_set = studio_model->hitbox_set(0);
	if (!hitbox_set)
		return;

	for (int i = 0; i < hitbox_set->num_hitboxes; i++)
	{
		auto hitbox = hitbox_set->hitbox(i);
		if (!hitbox)
			continue;

		vec3_t vMin, vMax;
		math::vector_transform(hitbox->min, bones[hitbox->bone], vMin);
		math::vector_transform(hitbox->max, bones[hitbox->bone], vMax);

		if (hitbox->radius != -1.f)
			HACKS->debug_overlay->add_capsule_overlay(vMin, vMax, hitbox->radius, 255, 255 * idx, 255 * idx2, 150, dur ? HACKS->global_vars->interval_per_tick * 2 : 5.f, 0, 1);
	}
}

INLINE bool valid_hitgroup(int index)
{
	if ((index >= HITGROUP_HEAD && index <= HITGROUP_RIGHTLEG) || index == HITGROUP_GEAR)
		return true;

	return false;
}

static float get_miss_angle_degrees(const aim_shot_record_t& shot)
{
	auto ideal_dir = (shot.point - shot.start).normalized();
	auto impact_dir = (shot.impact - shot.start).normalized();
	auto dot = std::clamp(ideal_dir.dot(impact_dir), -1.0f, 1.0f);
	return RAD2DEG(std::acos(dot));
}

// NL record-bracketing parity.
// Research: NL-PATCHES rage_record_logic.h (`find_shot_record` /
// `choose_extended_tick_delta`) and fix_rage_records.cpp. The server rewinds
// the target to the record that brackets the interpolation point of the
// outgoing command. A stale or future record shifts the entire hitbox, which
// turns a head shot into a body shot. Weight each point by how well its
// record brackets the ideal point so newer/correct records win ties.
static float get_record_bracket_weight(anim_record_t* record)
{
	if (!record)
		return 0.f;

	const float max_window = HACKS->max_unlag > 0.f ? HACKS->max_unlag : 0.2f;
	// Same manual interpolation override as is_tick_valid so record scoring and
	// validity agree.
	const float bracket_interp = g_cfg.rage.interpolation_ticks > 0
		? TICKS_TO_TIME(g_cfg.rage.interpolation_ticks)
		: LAGCOMP->get_interp_time();
	// Symmetric bracket around the ideal; the one-sided server-target variant
	// caused constant extrapolation and was reverted.
	const float one_way = HACKS->incoming > 0.f ? HACKS->incoming : HACKS->ping * 0.5f;
	const float correct = std::clamp(bracket_interp + one_way, 0.f, max_window);
	const float ideal = HACKS->predicted_time - correct;
	const float delta = std::fabs(ideal - record->sim_time);

	const float window = g_cfg.rage.backtrack
		? std::clamp(static_cast<float>(g_cfg.rage.backtrack_ms) / 1000.f, 0.02f, std::max(0.02f, max_window))
		: 0.02f;

	return std::clamp(1.f - delta / window, 0.f, 1.f);
}

// `snapshot` lets callers resolve the pose that was actually fired instead of
// the live resolver state (used by miss processing; null = live).
static matrix3x4_t* get_resolver_matrix(c_cs_player* player, anim_record_t* record, const resolver_info_t* snapshot = nullptr)
{
	const auto& info = snapshot ? *snapshot : resolver_info[player->index()];
	if (!info.resolved)
		return record->matrix_orig.matrix;

	switch (info.side)
	{
	case side_left:
		return record->matrix_left.matrix;
	case side_right:
		return record->matrix_right.matrix;
	case side_left_extra:
		return record->matrix_left_extra.matrix;
	case side_right_extra:
		return record->matrix_right_extra.matrix;
	case side_zero:
		// A custom zero yaw (jitter centre) is baked into matrix_orig by
		// resolver::apply_side, not into the precomputed matrix_zero.
		return info.use_resolved_yaw ? record->matrix_orig.matrix : record->matrix_zero.matrix;
	default:
		return record->matrix_orig.matrix;
	}
}

static matrix3x4_t* get_aim_matrix(c_cs_player* player, anim_record_t* record, const resolver_info_t* snapshot = nullptr)
{
	if (!record || !player)
		return nullptr;

	if (record->extrapolated)
		return record->predicted_matrix;

	return get_resolver_matrix(player, record, snapshot);
}

#ifndef LEGACY
static int max_point_safety()
{
	return 3;
}
#endif
static resolver_direction convert_side_to_direction(int side)
{
	switch (side)
	{
	case side_left:  return resolver_min;
	case side_right: return resolver_max;
	case side_left_extra:  return resolver_min_min;
	case side_right_extra: return resolver_max_max;
	case side_zero:  return resolver_networked;
	default:         return resolver_networked;
	}
}

static float clamp_pitch(float pitch)
{
	// First normalize to -180 to 180
	pitch = math::normalize_yaw(pitch);
	// Then clamp to valid pitch range -90 to 90
	if (pitch > 90.f)
		pitch = 180.f - pitch;
	else if (pitch < -90.f)
		pitch = -180.f - pitch;
	return pitch;
}

static float clamp_yaw(float yaw)
{
	// Robust yaw normalization to -180 to 180
	yaw = fmodf(yaw, 360.f);
	if (yaw > 180.f)
		yaw -= 360.f;
	else if (yaw < -180.f)
		yaw += 360.f;
	return yaw;
}

static const char* get_resolver_direction_label(resolver_direction dir)
{
	switch (dir)
	{
	case resolver_networked: return "resolved";
	case resolver_min:       return "left";
	case resolver_max:       return "right";
	case resolver_min_min:   return "left x2";
	case resolver_max_max:   return "right x2";
	case resolver_min_extra: return "left x3";
	case resolver_max_extra: return "right x3";
	default:                 return "unknown";
	}
}

bool can_hit_hitbox(const vec3_t& start, const vec3_t& end, rage_player_t* rage, int hitbox, matrix3x4_t* matrix, anim_record_t* record)
{
	auto model = rage->player->get_model();
	if (!model)
		return false;

	auto studio_model = HACKS->model_info->get_studio_model(rage->player->get_model());
	if (!studio_model)
		return false;

	auto set = studio_model->hitbox_set(0);

	if (!set)
		return false;

	auto studio_box = set->hitbox(hitbox);
	if (!studio_box)
		return false;

	// Always trace and require the ray to reach THIS hitbox first. The old
	// capsule path returned true on pure segment-to-segment distance, so a
	// nearer chest/arm counted as a head hit - the "head aim, body register"
	// miss class. `clip_ray_to_entity` handles capsule hitboxes and
	// `studio_box->group` is the hitgroup the trace must report.
	c_game_trace trace{};
	HACKS->engine_trace->clip_ray_to_entity({ start, end }, MASK_SHOT_HULL | CONTENTS_HITBOX, rage->player, &trace);

	return trace.entity == rage->player && trace.hitgroup == studio_box->group;
}

#ifndef LEGACY
static int calc_point_safety(const vec3_t& start, const vec3_t& end, rage_player_t* rage, int hitbox, anim_record_t* record)
{
	// Non-cheating players (bots / no fake detected) don't desync, so every
	// point is effectively safe. Marking them full prevents force-safe and
	// prefer-safe from blanking the target entirely (Fatality does the same
	// with `point.safe = record->bot || ...`).
	if (rage && rage->player && rage->player->is_bot())
		return max_point_safety();

	int safety = 0;

	matrix3x4_t* standard_matrices[]{
		record->matrix_left.matrix,
		record->matrix_right.matrix,
		record->matrix_zero.matrix,
	};

	for (int i = 0; i < 3; ++i)
	{
		if (can_hit_hitbox(start, end, rage, hitbox, standard_matrices[i], record))
			++safety;
	}

	// NOTE: the roll matrices are pose-identical copies (the roll transform is
	// not actually applied), so they are NOT counted - that would be duplicate
	// votes inflating "5/5 safe" while only 3 distinct poses exist.

	return safety;
}
#endif

float get_point_accuracy(rage_player_t* rage, vec3_t& eye_pos, const rage_point_t& point, matrix3x4_t* matrix, anim_record_t* record)
{
	static auto weapon_accuracy_nospread = HACKS->convars.weapon_accuracy_nospread;
	if (weapon_accuracy_nospread && weapon_accuracy_nospread->get_bool())
		return 1.f;

	auto predicted_info = ENGINE_PREDICTION->get_networked_vars(HACKS->cmd->command_number);

	auto hits = 0;

	auto spread = predicted_info->spread + predicted_info->inaccuracy;
	auto angle = math::calc_angle(eye_pos, point.aim_point).normalized_angle();

	vec3_t forward, right, up;
	math::angle_vectors(angle, &forward, &right, &up);

	for (auto i = 1; i <= 6; ++i)
	{
		for (auto j = 0; j < 8; ++j)
		{
			auto current_spread = spread * ((float)i / 6.f);

			float value = (float)j / 8.0f * (M_PI * 2.f);

			auto direction_cos = std::cos(value);
			auto direction_sin = std::sin(value);

			auto spread_x = direction_sin * current_spread;
			auto spread_y = direction_cos * current_spread;

			vec3_t direction{};
			direction.x = forward.x + spread_x * right.x + spread_y * up.x;
			direction.y = forward.y + spread_x * right.y + spread_y * up.y;
			direction.z = forward.z + spread_x * right.z + spread_y * up.z;

			auto end = eye_pos + direction.normalized() * HACKS->weapon_info->range;

			if (can_hit_hitbox(eye_pos, end, rage, point.hitbox, matrix, record))
				hits++;
		}
	}

	return (float)hits / 48.f;
}

bool c_ragebot::can_fire(bool ignore_revolver)
{
	if (!HACKS->local || !HACKS->weapon)
		return false;

	if (HACKS->cmd->weapon_select != 0)
		return false;

	if (!HACKS->weapon_info)
		return false;

	if (HACKS->local->flags().has(FL_ATCONTROLS))
		return false;

	if (HACKS->local->wait_for_no_attack())
		return false;

	if (HACKS->local->is_defusing())
		return false;

	if (HACKS->weapon_info->weapon_type >= 1 && HACKS->weapon_info->weapon_type <= 6 && HACKS->weapon->clip1() < 1)
		return false;

	if (HACKS->local->player_state() > 0)
		return false;

	auto weapon_index = HACKS->weapon->item_definition_index();
	if ((weapon_index == WEAPON_GLOCK || weapon_index == WEAPON_FAMAS) && HACKS->weapon->burst_shots_remaining() > 0)
		return HACKS->predicted_time >= HACKS->weapon->next_burst_shot();

	// TO-DO: auto revolver detection
	if (weapon_index == WEAPON_REVOLVER && !ignore_revolver)
		return revolver_fire;

	float next_attack = HACKS->local->next_attack();
	float next_primary_attack = HACKS->weapon->next_primary_attack();

	return HACKS->predicted_time >= next_attack && HACKS->predicted_time >= next_primary_attack;
}

bool c_ragebot::is_shooting()
{
	if (!HACKS->weapon)
		return false;

	bool attack2 = HACKS->cmd->buttons.has(IN_ATTACK2);
	bool attack = HACKS->cmd->buttons.has(IN_ATTACK);

	short weapon_index = HACKS->weapon->item_definition_index();

	if (weapon_index == WEAPON_C4)
		return false;

	if ((weapon_index == WEAPON_GLOCK || weapon_index == WEAPON_FAMAS) && HACKS->weapon->burst_shots_remaining() > 0)
		return HACKS->predicted_time >= HACKS->weapon->next_burst_shot();

	if (HACKS->weapon->is_grenade())
		return !HACKS->weapon->pin_pulled() && HACKS->weapon->throw_time() > 0.f && HACKS->weapon->throw_time() < HACKS->predicted_time;

	auto can_fire_now = can_fire();
	if (weapon_index == WEAPON_REVOLVER)
		return attack && can_fire_now;

	if (HACKS->weapon->is_knife())
		return (attack || attack2) && can_fire_now;

	return attack && can_fire_now;
}

// These MUST match the bitmask written by the "Hitboxes" multi_combo
// (Head=bit0, Chest=bit1, Stomach=bit2, Pelvis=bit3, Arms=bit4, Legs=bit5)
// and the rage_hitbox_t enum. Using the raw HITBOX_* enum indices here
// desynchronised selection from the menu (e.g. "Chest" read as nothing).
namespace hitbox_flags
{
	constexpr int HEAD = 1 << 0;
	constexpr int CHEST = 1 << 1;
	constexpr int STOMACH = 1 << 2;
	constexpr int PELVIS = 1 << 3;
	constexpr int ARMS = 1 << 4;
	constexpr int LEGS = 1 << 5;
}

void c_ragebot::update_hitboxes()
{
	if (HACKS->weapon->is_taser())
	{
		hitboxes.emplace_back(HITBOX_STOMACH);
		hitboxes.emplace_back(HITBOX_PELVIS);
		return;
	}

	if (rage_config.hitboxes & hitbox_flags::HEAD)
		hitboxes.emplace_back(HITBOX_HEAD);

	if (rage_config.hitboxes & hitbox_flags::CHEST)
	{
		hitboxes.emplace_back(HITBOX_NECK);
		hitboxes.emplace_back(HITBOX_CHEST);
		hitboxes.emplace_back(HITBOX_UPPER_CHEST);
		hitboxes.emplace_back(HITBOX_THORAX);
	}

	if (rage_config.hitboxes & hitbox_flags::STOMACH)
		hitboxes.emplace_back(HITBOX_STOMACH);

	if (rage_config.hitboxes & hitbox_flags::PELVIS)
		hitboxes.emplace_back(HITBOX_PELVIS);

	if (rage_config.hitboxes & hitbox_flags::ARMS)
	{
		hitboxes.emplace_back(HITBOX_LEFT_UPPER_ARM);
		hitboxes.emplace_back(HITBOX_RIGHT_UPPER_ARM);
		hitboxes.emplace_back(HITBOX_LEFT_FOREARM);
		hitboxes.emplace_back(HITBOX_RIGHT_FOREARM);
	}

	if (rage_config.hitboxes & hitbox_flags::LEGS)
	{
		hitboxes.emplace_back(HITBOX_LEFT_THIGH);
		hitboxes.emplace_back(HITBOX_RIGHT_THIGH);
		hitboxes.emplace_back(HITBOX_LEFT_CALF);
		hitboxes.emplace_back(HITBOX_RIGHT_CALF);
		hitboxes.emplace_back(HITBOX_LEFT_FOOT);
		hitboxes.emplace_back(HITBOX_RIGHT_FOOT);
	}
}

// Debug visualisation.
//   Yellow  = hitbox centre (validated)
//   Green   = safe multipoint (validated)
//   Red     = unsafe multipoint (validated)
//   Cyan    = raw point produced by the current multipoint generator (pre-trace)
// plus an on-screen state block when "Hitscan debug" is enabled.
void c_ragebot::debug_draw_multipoints()
{
	const bool draw_dots = g_cfg.rage.multipoint_debug;
	const bool draw_text = g_cfg.rage.debug_hitscan;

	if ((!draw_dots && !draw_text) || !HACKS->debug_overlay || !HACKS->global_vars)
		return;

	const float duration = std::max(HACKS->global_vars->interval_per_tick * 2.f, 0.05f);

#if 0 // OLD CODE (kept for revert)
	if (draw_text)
	{
		float y = 220.f;

		auto line = [&](const std::string& text, int r, int g, int b)
			{
				HACKS->debug_overlay->add_screen_text_overlay(20.f, y, duration, r, g, b, 255, text.c_str());
				y += 14.f;
			};

		line(tfm::format(
			CXOR("[rage] %s | mp:%s | backtrack:%s"),
			debug_reason,
			g_cfg.rage.multipoint_advanced ? CXOR("advanced") : CXOR("legacy"),
			g_cfg.rage.backtrack ? CXOR("on") : CXOR("off")
		), 180, 220, 255);

		if (rage_config.hitchance > 0)
		{
			line(tfm::format(
				CXOR("[cfg] hc:%d%% mindmg:%d head:%d body:%d hitboxes:%u"),
				rage_config.hitchance,
				get_min_damage(HACKS->local),
				rage_config.scale_head,
				rage_config.scale_body,
				rage_config.hitboxes
			), 160, 200, 255);
		}

		LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
			{
				if (!player || !player->is_alive() || player->dormant())
					return;

				auto& rage = rage_players[player->index()];
				if (!rage.player || rage.player != player)
					return;

				auto& info = resolver_info[player->index()];

				if (rage.start_scans)
				{
					// OLD CODE (kept for revert):
					// line(tfm::format(
					//	CXOR("%s: side %s mode %s miss %d scans %d"),
					//	player->get_name(),
					//	get_resolver_direction_label(convert_side_to_direction(info.side)),
					//	info.resolved ? info.mode.c_str() : CXOR("unresolved"),
					//	missed_shots[player->index()],
					//	(int)rage.points_to_scan.size()
					// ), 160, 255, 160);
					//
					// Backtrack diagnostics: lc = valid/total records in the
					// player's window, shift = records flagged out-of-order.
					auto lc_anims = ANIMFIX->get_anims(player->index());
					line(tfm::format(
						CXOR("%s: side %s mode %s miss %d scans %d lc %d/%d shift %d"),
						player->get_name(),
						get_resolver_direction_label(convert_side_to_direction(info.side)),
						info.resolved ? info.mode.c_str() : CXOR("unresolved"),
						missed_shots[player->index()],
						(int)rage.points_to_scan.size(),
						lc_anims ? lc_anims->dbg_valid : 0,
						lc_anims ? lc_anims->dbg_records : 0,
						lc_anims ? lc_anims->dbg_shifting : 0
					), 160, 255, 160);
				}

				if (rage.best_point.found)
				{
					line(tfm::format(
						CXOR("  best %s dmg %d safe %d acc %.0f%% rec %s"),
						main_utils::hitbox_to_string(rage.best_point.hitbox),
						rage.best_point.damage,
						rage.best_point.safety,
						rage.best_point.accuracy * 100.f,
						rage.best_point.predicted_eye_pos ? CXOR("pred") : CXOR("hist")
					), 255, 230, 140);
				}
			});
	}

	if (!draw_dots)
		return;

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player || !player->is_alive() || player->dormant())
				return;

			auto& rage = rage_players[player->index()];
			if (!rage.player || rage.player != player)
				return;

			// Raw generator output (pre-trace) so you can see exactly where the
			// selected multipoint mode is placing points.
			if (draw_text && !hitboxes.empty())
			{
				auto record = rage.best_record ? rage.best_record : rage.hitscan_record;
				if (record)
				{
					auto matrix = get_aim_matrix(player, record);
					int drawn_hitboxes = 0;

					for (auto hitbox : hitboxes)
					{
						if (drawn_hitboxes++ >= 6)
							break;

						auto raw = get_points(player, hitbox, matrix, record);
						for (auto& p : raw)
						{
							if (p.first.valid())
								HACKS->debug_overlay->add_sphere_overlay(p.first, 0.8f, 6, 6, 0, 220, 255, 255, duration);
						}
					}
				}
			}

			for (auto& point : rage.points_to_scan)
			{
				if (!point.aim_point.valid())
					continue;

				int r = 255, g = 255, b = 0;
				if (!point.center)
				{
#ifndef LEGACY
					const bool safe = point.safety >= max_point_safety();
#else
					const bool safe = true;
#endif
					r = safe ? 0 : 255;
					g = safe ? 255 : 0;
					b = 0;
				}

				HACKS->debug_overlay->add_sphere_overlay(point.aim_point, point.center ? 2.5f : 1.5f, 8, 8, r, g, b, 255, duration);
			}
		});
#endif

	// -----------------------------------------------------------------------
	// Standalone debug draw. Independent of the ragebot's enabled/scan state:
	// works whenever either toggle is on and an enemy record exists.
	//   Hitscan debug  -> hitbox capsules (aim matrix green, resolver sides
	//                     blue/cyan/red) + the state text block.
	//   Multipoint dbg -> raw generator points (cyan) + validated scan points
	//                     (yellow centre / green safe / red unsafe).
	// -----------------------------------------------------------------------
	float text_y = 220.f;
	auto text_line = [&](const std::string& text, int r, int g, int b)
		{
			HACKS->debug_overlay->add_screen_text_overlay(20.f, text_y, duration, r, g, b, 255, text.c_str());
			text_y += 14.f;
		};

	if (draw_text)
	{
		text_line(tfm::format(
			CXOR("[rage] %s | mp:%s | backtrack:%s | log:%d"),
			debug_reason,
			g_cfg.rage.multipoint_advanced ? CXOR("advanced") : CXOR("legacy"),
			g_cfg.rage.backtrack ? CXOR("on") : CXOR("off"),
			rage_log::enabled() ? 1 : 0
		), 180, 220, 255);

		if (rage_config.hitchance > 0)
		{
			text_line(tfm::format(
				CXOR("[cfg] hc:%d%% mindmg:%d head:%d body:%d hitboxes:%u"),
				rage_config.hitchance,
				get_min_damage(HACKS->local),
				rage_config.scale_head,
				rage_config.scale_body,
				rage_config.hitboxes
			), 160, 200, 255);
		}
	}

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player || !player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			if (player == HACKS->local || player->is_teammate(false))
				return;

			auto& rage = rage_players[player->index()];
			auto lag_anims = ANIMFIX->get_anims(player->index());

			// Always use the live newest record: rage.best_record/hitscan_record
			// are frame-local pointers into the record deque and can dangle once
			// the deque shifts, which is why this draw runs before the scan.
			anim_record_t* record = nullptr;
			if (lag_anims && !lag_anims->records.empty())
				record = &lag_anims->records.front();

			if (!record)
				return;

			auto& info = resolver_info[player->index()];
			auto matrix = get_aim_matrix(player, record);

			if (draw_text)
			{
				// Hitboxes actually scanned (aim matrix) and every resolver
				// candidate so a wrong side is visible.
				draw_hitbox__(player, matrix, 1, 0, true);

#ifndef LEGACY
				draw_hitbox__(player, record->matrix_left.matrix, 0, 1, true);
				draw_hitbox__(player, record->matrix_right.matrix, 1, 1, true);
				draw_hitbox__(player, record->matrix_zero.matrix, 0, 0, true);
#endif

				text_line(tfm::format(
					CXOR("%s: side %s mode %s miss %d scans %d lc %d/%d shift %d"),
					player->get_name(),
					get_resolver_direction_label(convert_side_to_direction(info.side)),
					info.resolved ? info.mode.c_str() : CXOR("unresolved"),
					missed_shots[player->index()],
					(int)rage.points_to_scan.size(),
					lag_anims ? lag_anims->dbg_valid : 0,
					lag_anims ? lag_anims->dbg_records : 0,
					lag_anims ? lag_anims->dbg_shifting : 0
				), 160, 255, 160);

				if (rage.best_point.found)
				{
					text_line(tfm::format(
						CXOR("  best %s dmg %d safe %d acc %.0f%% rec %s"),
						main_utils::hitbox_to_string(rage.best_point.hitbox),
						rage.best_point.damage,
						rage.best_point.safety,
						rage.best_point.accuracy * 100.f,
						rage.best_point.predicted_eye_pos ? CXOR("pred") : CXOR("hist")
					), 255, 230, 140);
				}
			}

			if (draw_dots)
			{
				// Raw generator output for the configured hitboxes (cyan).
				int drawn_hitboxes = 0;
				for (auto hitbox : hitboxes)
				{
					if (drawn_hitboxes++ >= 8)
						break;

					auto raw = get_points(player, hitbox, matrix, record);
					for (auto& p : raw)
					{
						if (p.first.valid())
							HACKS->debug_overlay->add_sphere_overlay(p.first, 0.8f, 6, 6, 0, 220, 255, 255, duration);
					}
				}

				// Validated scan points.
				for (auto& point : rage.points_to_scan)
				{
					if (!point.aim_point.valid())
						continue;

					int r = 255, g = 255, b = 0;
					if (!point.center)
					{
#ifndef LEGACY
						const bool safe = point.safety >= max_point_safety();
#else
						const bool safe = true;
#endif
						r = safe ? 0 : 255;
						g = safe ? 255 : 0;
						b = 0;
					}

					HACKS->debug_overlay->add_sphere_overlay(point.aim_point, point.center ? 2.5f : 1.5f, 8, 8, r, g, b, 255, duration);
				}
			}
		});
}

// ---------------------------------------------------------------------------
// Multipoint generation
//
// Density (the Head/body multipoint slider, 0..100) controls BOTH the number of
// generated points and how far they sit from the hitbox centre:
//   0   -> centre point only (safest, lowest coverage)
//   100 -> maximum ring/corner set placed on the hitbox surface (best coverage,
//          most aggressive)
// Head hitboxes are driven exclusively by scale_head, every other hitbox by
// scale_body. -1 keeps the legacy "auto" behaviour derived from the inaccuracy
// cone at the target distance.
// ---------------------------------------------------------------------------
static constexpr int MAX_MULTIPOINTS_PER_HITBOX = 12;

static int multipoint_budget_from_density(int density)
{
	density = std::clamp(density, 0, 100);
	if (density <= 0)
		return 0;

	return std::clamp(2 + (density * 10) / 100, 2, MAX_MULTIPOINTS_PER_HITBOX);
}

static float multipoint_spread_from_density(int density)
{
	density = std::clamp(density, 0, 100);
	// Cap below 1.0 so surface points do not sit exactly on the hitbox edge
	// (avoids float-precision misses on corner/edge traces).
	return 0.45f + 0.53f * (static_cast<float>(density) / 100.f);
}

// Option C capsule handling: CS:GO stores capsule hitboxes as an AABB. Pick
// the dominant AABB axis as the capsule axis, use the two other half-extents
// as the ring radius (they approximate the capsule radius), and inset the
// segment by the ring radius so the generated points land on the capsule
// surface instead of outside it.
static void append_capsule_multipoints(const vec3_t& min, const vec3_t& max, float radius, int density, multipoints_t& out)
{
	const int budget = multipoint_budget_from_density(density);
	if (budget <= 0)
		return;

	const float spread = multipoint_spread_from_density(density);
	const vec3_t center = (min + max) * 0.5f;
	const vec3_t half = (max - min) * 0.5f;

	const float ax = std::fabsf(half.x);
	const float ay = std::fabsf(half.y);
	const float az = std::fabsf(half.z);

	float axis_len = ax;
	float perp_a = ay;
	float perp_b = az;
	vec3_t axis{ 1.f, 0.f, 0.f };

	if (ay >= ax && ay >= az)
	{
		axis = { 0.f, 1.f, 0.f };
		axis_len = ay;
		perp_a = ax;
		perp_b = az;
	}
	else if (az >= ax && az >= ay)
	{
		axis = { 0.f, 0.f, 1.f };
		axis_len = az;
		perp_a = ax;
		perp_b = ay;
	}

	// Ring radius: if the AABB wraps the capsule, the perpendicular half
	// extents are ~radius. If they are tiny (endpoint-style data), fall back to
	// the hitbox radius. Never exceed the hitbox radius.
	float ring_radius = std::min(perp_a, perp_b);
	if (ring_radius < 0.1f)
		ring_radius = radius;
	ring_radius = std::clamp(ring_radius, 0.f, radius > 0.f ? radius : ring_radius);

	// Inset the segment endpoints by the cap radius.
	const float segment_half = std::max(axis_len - ring_radius, 0.f);

	const vec3_t reference = std::fabsf(axis.z) < 0.99f ? vec3_t{ 0.f, 0.f, 1.f } : vec3_t{ 1.f, 0.f, 0.f };
	const vec3_t u = axis.cross(reference).normalized();
	const vec3_t v = axis.cross(u).normalized();

	// 1 ring for small budgets, 2-3 rings for larger ones.
	const int rings = budget <= 4 ? 1 : budget <= 8 ? 2 : 3;
	const int segments = (budget + rings - 1) / rings;

	int emitted = 0;
	for (int r = 0; r < rings && emitted < budget; ++r)
	{
		const float t = rings == 1 ? 0.5f : static_cast<float>(r) / static_cast<float>(rings - 1);
		const float along = (t - 0.5f) * segment_half * 2.f;

		for (int s = 0; s < segments && emitted < budget; ++s)
		{
			const float angle = (2.f * static_cast<float>(M_PI) * s) / static_cast<float>(segments);

			vec3_t point = center
				+ axis * along
				+ (u * std::cos(angle) + v * std::sin(angle)) * (ring_radius * spread);

			out.emplace_back(point, false);
			++emitted;
		}
	}
}

static void append_box_multipoints(const vec3_t& min, const vec3_t& max, int density, multipoints_t& out)
{
	const int budget = multipoint_budget_from_density(density);
	if (budget <= 0)
		return;

	const float spread = multipoint_spread_from_density(density);
	const vec3_t center = (min + max) * 0.5f;
	const vec3_t half = (max - min) * 0.5f;

	// Corners first (maximal coverage), then face centres.
	static const vec3_t directions[12]
	{
		{  1.f,  1.f,  1.f }, { -1.f,  1.f,  1.f }, {  1.f, -1.f,  1.f }, { -1.f, -1.f,  1.f },
		{  1.f,  1.f, -1.f }, { -1.f,  1.f, -1.f }, {  1.f, -1.f, -1.f }, { -1.f, -1.f, -1.f },
		{  1.f,  0.f,  0.f }, { -1.f,  0.f,  0.f }, {  0.f,  1.f,  0.f }, {  0.f, -1.f,  0.f },
	};

	for (int i = 0; i < budget; ++i)
	{
		const vec3_t& dir = directions[i];
		out.emplace_back(vec3_t
		{
			center.x + half.x * dir.x * spread,
			center.y + half.y * dir.y * spread,
			center.z + half.z * dir.z * spread
		}, false);
	}
}

// ---------------------------------------------------------------------------
// Fatality `extra_head_calc` parity.
// Research: Fatality.win-Source / internal_hvh / features / hitscan.cpp,
// lines 576-612. The bottom of the head hitbox overlaps the neck/chest
// hitgroup, so a point that is geometrically inside the head can be traced by
// the server as chest/neck -> this is the classic "aims at head, hits body"
// failure. Fatality walks the head from the top downwards and stops at the
// first sample that no longer traces as the HEAD hitgroup, then returns the
// midpoint between the top sample and the last valid one, guaranteeing the
// server itself classifies the aim point as head.
//
// This uses the same hitgroup-priority studio trace as penetration::simulate
// (which already discards a head hit that is behind chest/stomach), so a
// positive result means a real, unobstructed head hitgroup intersection.
// ---------------------------------------------------------------------------
static bool point_traces_as_hitgroup(c_cs_player* player, const vec3_t& eye, const vec3_t& point, matrix3x4_t* matrix, int hitgroup)
{
	if (!player || !matrix || !HACKS->weapon_info)
		return false;

	vec3_t dir = point - eye;
	if (dir.normalized_float() < 0.001f)
		return false;

	c_game_trace trace{};
	ray_t ray(eye, eye + dir * (HACKS->weapon_info->range + 32.f));

	return penetration::test_hitboxes(player, &trace, ray, matrix) && trace.hitgroup == hitgroup;
}

static vec3_t extra_head_calc(c_cs_player* player, const vec3_t& top, const vec3_t& bot, const vec3_t& eye, matrix3x4_t* matrix)
{
	vec3_t point = top;

	for (auto i = 0u; i < 6u; ++i)
	{
		point = top + (bot - top) * (static_cast<float>(i) / 6.f);

		if (!point_traces_as_hitgroup(player, eye, point, matrix, HITGROUP_HEAD))
			break;
	}

	return (top + point) * 0.5f;
}

// Option A: the original per-hitbox multipoint generator. The slider scales how
// far the (always generated) points sit from the hitbox centre.
multipoints_t c_ragebot::get_points_legacy(c_cs_player* player, int hitbox, matrix3x4_t* matrix, anim_record_t* record)
{
	auto local_anims = ANIMFIX->get_local_anims();
	multipoints_t points;

	const model_t* model = player->get_model();
	if (!model)
		return points;

	studiohdr_t* hdr = HACKS->model_info->get_studio_model(model);
	if (!hdr)
		return points;

	mstudiohitboxset_t* set = hdr->hitbox_set(player->hitbox_set());
	if (!set)
		return points;

	mstudiobbox_t* bbox = set->hitbox(hitbox);
	if (!bbox)
		return points;

	if (bbox->radius <= 0.f)
	{
		matrix3x4_t rot_matrix = { };
		rot_matrix.angle_matrix(bbox->rotation);

		matrix3x4_t mat = { };
		math::contact_transforms(matrix[bbox->bone], rot_matrix, mat);

		auto origin = mat.get_origin();
		auto center = (bbox->min + bbox->max) * 0.5f;

		if (hitbox == HITBOX_LEFT_FOOT || hitbox == HITBOX_RIGHT_FOOT)
		{
			float d1 = (bbox->min.z - center.z) * 0.875f;
			if (hitbox == HITBOX_LEFT_FOOT)
				d1 *= -1.f;

			points.emplace_back(vec3_t{ center.x, center.y, center.z + d1 }, false);

			float d2 = (bbox->min.x - center.x) * 0.5f;
			float d3 = (bbox->max.x - center.x) * 0.5f;

			points.emplace_back(vec3_t{ center.x + d2, center.y, center.z }, false);
			points.emplace_back(vec3_t{ center.x + d3, center.y, center.z }, false);
		}
		else
			points.emplace_back(center, true);

		if (points.empty())
			return points;

		for (auto& p : points)
		{
			p.first = { p.first.dot(mat.mat[0]), p.first.dot(mat.mat[1]), p.first.dot(mat.mat[2]) };
			p.first += origin;
		}
	}
	else
	{
		vec3_t max = bbox->max;
		vec3_t min = bbox->min;
		auto center = (bbox->min + bbox->max) / 2.f;

		// get_dynamic_scale() measures the distance from the point to the eye,
		// so it must receive the WORLD-space center, not the bone-local one.
		vec3_t world_center{};
		math::vector_transform(center, matrix[bbox->bone], world_center);

		auto dynamic_scale = get_dynamic_scale(world_center, bbox->radius);

		auto head_scale = rage_config.scale_head != -1 ? rage_config.scale_head * 0.01f : dynamic_scale;
		auto body_scale = rage_config.scale_body != -1 ? rage_config.scale_body * 0.01f : dynamic_scale;

		auto body_slider = 0.35f * body_scale;
		auto r = bbox->radius * body_slider;

		switch (hitbox)
		{
		case HITBOX_HEAD:
		{
			points.emplace_back(center, true);

			float head_slider = 0.75f * head_scale;

			float r = bbox->radius * head_slider;
			points.emplace_back(vec3_t{ max.x + (MATRIX_HEAD_ROTATION * r), max.y + (-MATRIX_HEAD_ROTATION * r), max.z }, false);

			vec3_t right{ max.x, max.y, max.z + r };
			points.emplace_back(right, false);

			vec3_t left{ max.x, max.y, max.z - r };
			points.emplace_back(left, false);

			points.emplace_back(vec3_t{ max.x, max.y - r, max.z }, false);

			// neck / lower-head (catches fake-vs-real boundary + chest overlap)
			points.emplace_back(vec3_t{ center.x, center.y, min.z }, false);

			// side points at mid-head height
			points.emplace_back(vec3_t{ center.x, max.y - r, center.z }, false);
			points.emplace_back(vec3_t{ center.x, min.y + r, center.z }, false);

			auto state = player->animstate();
			if (state && state->velocity_length_xy <= 0.1f && player->eye_angles().x <= 75.f)
				points.emplace_back(vec3_t{ max.x - r, max.y, max.z }, false);
		}
		break;
		case HITBOX_UPPER_CHEST:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, max.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, min.z - r }, false);
		}
		break;
		case HITBOX_PELVIS:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, max.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, min.z - r }, false);
		}
		break;
		case HITBOX_CHEST:
		case HITBOX_THORAX:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, max.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, min.z - r }, false);
			points.emplace_back(vec3_t{ center.x, max.y - r, center.z }, false);
		}
		break;
		case HITBOX_STOMACH:
		{
			points.emplace_back(center, true);

			points.emplace_back(vec3_t{ center.x, center.y, min.z + r }, false);
			points.emplace_back(vec3_t{ center.x, center.y, max.z - r }, false);
			points.emplace_back(vec3_t{ center.x, max.y - r, center.z }, false);
		}
		break;
		case HITBOX_RIGHT_CALF:
		case HITBOX_LEFT_CALF:
		{
			points.emplace_back(center, true);
			points.emplace_back(vec3_t{ max.x - (bbox->radius / 2.f), max.y, max.z }, false);
		}
		break;
		case HITBOX_RIGHT_UPPER_ARM:
		case HITBOX_LEFT_UPPER_ARM:
		{
			points.emplace_back(center, true);
			points.emplace_back(vec3_t{ max.x + bbox->radius, center.y, center.z }, false);
		}
		break;
		default:
			points.emplace_back(center, true);
			break;
		}

		if (points.empty())
			return points;

		for (auto& p : points)
			math::vector_transform(p.first, matrix[bbox->bone], p.first);
	}

	return points;
}

// Projection-based multipoint (research-verified against Fatality's hitscan
// multipoint): sample the hitbox cross-section, project the samples onto the
// plane facing the shooter, and emit centre + edge points. The safe hull is
// the intersection of the projections across the resolver directions we have,
// so safe points stay hittable no matter which side resolves.
multipoints_t c_ragebot::get_points_advanced(c_cs_player* player, int hitbox, matrix3x4_t* matrix, anim_record_t* record)
{
	multipoints_t points;

	const model_t* model = player->get_model();
	if (!model)
		return points;

	studiohdr_t* hdr = HACKS->model_info->get_studio_model(model);
	if (!hdr)
		return points;

	mstudiohitboxset_t* set = hdr->hitbox_set(player->hitbox_set());
	if (!set)
		return points;

	mstudiobbox_t* bbox = set->hitbox(hitbox);
	if (!bbox)
		return points;

	auto local_anims = ANIMFIX->get_local_anims();
	if (!local_anims)
		return points;

	const vec3_t eye = local_anims->eye_pos;

	const bool is_head = (hitbox == HITBOX_HEAD);
	int density = is_head ? rage_config.scale_head : rage_config.scale_body;

	const vec3_t local_center = (bbox->min + bbox->max) * 0.5f;
	vec3_t world_center{};
	math::vector_transform(local_center, matrix[bbox->bone], world_center);

	if (density < 0)
	{
		const float auto_scale = get_cone_scale(world_center, bbox->radius > 0.f ? bbox->radius : 8.f);
		density = static_cast<int>(std::round(std::clamp(35.f + auto_scale * 45.f, 0.f, 100.f)));
	}

	density = std::clamp(density, 0, 100);

	// OLD CODE (kept for revert):
	// // The centre point is always scanned first: it is the safest point.
	// points.emplace_back(world_center, true);
	//
	// For non-head hitboxes the raw geometric centre is still correct and is
	// emitted first. For the head the centre is replaced by the
	// extra_head_calc-validated point, which is emitted after the safe hull
	// has been built (see below).
	if (!is_head)
		points.emplace_back(world_center, true);

	if (density <= 0)
	{
		// Density 0 has no hull, so fall back to the raw centre for the head.
		if (is_head)
			points.emplace_back(world_center, true);

		return points;
	}

	// ---- shooter plane basis ----
	vec3_t fw = world_center - eye;
	if (fw.normalized_float() < 0.001f)
		fw = vec3_t{ 1.f, 0.f, 0.f };

	vec3_t right = fw.cross(vec3_t{ 0.f, 0.f, 1.f });
	if (right.normalized_float() < 0.001f)
	{
		right = fw.cross(vec3_t{ 1.f, 0.f, 0.f });
		right.normalized_float();
	}

	const vec3_t up = right.cross(fw).normalized();

	auto to2d = [&](const vec3_t& p) -> multipoint_geom::point2_t
		{
			const vec3_t d = p - world_center;
			return { d.dot(right), d.dot(up) };
		};

	auto to_world = [&](const multipoint_geom::point2_t& p) -> vec3_t
		{
			return world_center + right * p.x + up * p.y;
		};

	auto build_hull = [&](matrix3x4_t* mtx) -> std::vector<multipoint_geom::point2_t>
		{
			std::vector<multipoint_geom::point2_t> pts;

			if (bbox->radius > 0.f)
			{
				vec3_t vmin{}, vmax{};
				math::vector_transform(bbox->min, mtx[bbox->bone], vmin);
				math::vector_transform(bbox->max, mtx[bbox->bone], vmax);

				auto diag = [&](const vec3_t& a, const vec3_t& b, float t) {
					return (a * (1.f - t) + b * t).normalized();
					};

				const vec3_t dirs[12] = {
					right, up, -right, -up,
					diag(right, up, 0.375f), diag(right, up, 0.625f),
					diag(right, -up, 0.375f), diag(right, -up, 0.625f),
					diag(-right, up, 0.375f), diag(-right, up, 0.625f),
					diag(-right, -up, 0.375f), diag(-right, -up, 0.625f),
				};

				pts.reserve(24);
				for (const auto& d : dirs)
				{
					pts.push_back(to2d(vmin + d * bbox->radius));
					pts.push_back(to2d(vmax + d * bbox->radius));
				}
			}
			else
			{
				for (int i = 0; i < 8; ++i)
				{
					const vec3_t corner{
						(i & 1) ? bbox->max.x : bbox->min.x,
						(i & 2) ? bbox->max.y : bbox->min.y,
						(i & 4) ? bbox->max.z : bbox->min.z
					};

					vec3_t w{};
					math::vector_transform(corner, mtx[bbox->bone], w);
					pts.push_back(to2d(w));
				}
			}

			multipoint_geom::convex_hull(pts);
			return pts;
		};

	const auto current_hull = build_hull(matrix);

	std::vector<multipoint_geom::point2_t> safe_hull = current_hull;
	if (record)
	{
		matrix3x4_t* directions[5] = {
			record->matrix_left.matrix,
			record->matrix_right.matrix,
			record->matrix_zero.matrix,
			nullptr,
			nullptr
		};

		if (g_cfg.antihit.distortion)
		{
			directions[3] = record->matrix_left.roll_matrix;
			directions[4] = record->matrix_right.roll_matrix;
		}

		for (auto* dir : directions)
		{
			if (!dir || safe_hull.size() < 3)
				continue;

			safe_hull = multipoint_geom::intersect(safe_hull, build_hull(dir));
		}
	}

	if (safe_hull.size() < 3)
		safe_hull = current_hull;

	// ---- adaptive point scale ----
	const float cone = get_cone_scale(world_center, bbox->radius > 0.f ? bbox->radius : 8.f);

	// OLD CODE (kept for revert):
	// float rs = 0.5f + 0.5f * (static_cast<float>(density) / 100.f);
	// rs -= cone * 0.1f;
	// if (HACKS->cl_lagcomp0)
	//	rs *= 0.8f;
	// rs = std::clamp(rs, 0.f, 0.975f);
	float rs = 0.5f + 0.5f * (static_cast<float>(density) / 100.f);
	rs -= cone * 0.1f;
	if (HACKS->cl_lagcomp0)
		rs *= 0.8f;

	// Neverlose crouch-hitscan parity.
	// Research: NL-PATCHES fix_crouch_hitscan.cpp hooks Neverlose's head/body
	// point generators and contracts the generated points for lag records
	// whose duck amount is not full, because the points are generated with a
	// duck state that does not match the record's actual hitbox. Apply the
	// same contraction here, using the record's own duck amount.
	if (record)
	{
		const float record_duck = std::clamp(record->duck_amt, 0.f, 1.f);
		const float live_duck = std::clamp(player->duck_amount(), 0.f, 1.f);

		// Partially crouched records: the head capsule sits between the
		// standing and crouched pose, contract surface points.
		if (record_duck > 0.01f && record_duck < 0.99f)
			rs *= 0.75f + 0.25f * record_duck;

		// Record duck disagrees with the live entity (duck transition):
		// animation interpolation is least reliable here, contract again.
		if (std::fabs(record_duck - live_duck) > 0.05f)
			rs *= 0.9f;
	}

	rs = std::clamp(rs, 0.f, 0.975f);

	auto add_world = [&](const vec3_t& w, bool center)
		{
			vec3_t candidate = w;
			if (!candidate.valid())
				return;

			for (auto& existing : points)
			{
				if (existing.first.dist_to(candidate) < 1.f)
					return;
			}

			points.emplace_back(candidate, center);
		};

	const auto safe_center = multipoint_geom::centroid(safe_hull);
	const auto safe_ext = multipoint_geom::extremes(safe_hull);

	// Fatality `extra_head_calc` parity: build the head aim point from the top
	// safe sample downwards and stop at the last sample that still traces as
	// HITGROUP_HEAD, then use the midpoint. This is the validated centre the
	// server will register as a head hit.
	// OLD CODE (kept for revert): the raw centre was emitted before the hull
	// was built and never validated.
	if (is_head)
	{
		const vec3_t raw_top = to_world(multipoint_geom::lerp(safe_center, safe_ext.top, rs));
		const vec3_t raw_bot = to_world(multipoint_geom::lerp(safe_center, safe_ext.bottom, rs));
		const vec3_t validated = extra_head_calc(player, raw_top, raw_bot, eye, matrix);

		// center=false so collect_damage_from_multipoints scans it: the raw
		// geometric centre is already emitted by get_hitbox_points, this point
		// is an additional, hitgroup-validated head candidate.
		points.insert(points.begin(), multipoint_t{ validated, false });

		// Crown coverage.
		// Research: Fatality hitscan.cpp:520-543 samples the full 24-point ring
		// at BOTH capsule endpoints (vmin and vmax), so its intersected hull
		// covers the top of the skull front and back; the old generator only
		// emitted the plane-top extreme, leaving the crown front/back unscanned.
		// Emit explicit candidates: the upper capsule endpoint plus offsets
		// along the shooter forward axis (front/back) and the plane right axis.
		if (rs >= 0.2f)
		{
			vec3_t hb_min{}, hb_max{};
			math::vector_transform(bbox->min, matrix[bbox->bone], hb_min);
			math::vector_transform(bbox->max, matrix[bbox->bone], hb_max);

			const vec3_t upper = hb_min.z > hb_max.z ? hb_min : hb_max;
			const float crown_r = (bbox->radius > 0.f ? bbox->radius : 5.f) * 0.6f * rs;

			vec3_t fw_flat{ fw.x, fw.y, 0.f };
			if (fw_flat.normalized_float() < 0.001f)
				fw_flat = right;

			add_world(upper, false);                      // crown apex
			add_world(upper + fw_flat * crown_r, false);  // back of the crown
			add_world(upper - fw_flat * crown_r, false);  // front of the crown
			add_world(upper + right * crown_r, false);    // crown side
			add_world(upper - right * crown_r, false);    // crown side
		}
	}

	// Safe region points (hittable across resolver directions).
	add_world(to_world(safe_center), false);
	if (rs >= 0.2f)
	{
		add_world(to_world(multipoint_geom::lerp(safe_center, safe_ext.left, rs)), false);
		add_world(to_world(multipoint_geom::lerp(safe_center, safe_ext.right, rs)), false);

		if (is_head)
			add_world(to_world(multipoint_geom::lerp(safe_center, safe_ext.top, rs)), false);
	}

	// Aggressive silhouette edges from the currently resolved direction.
	if (rs >= 0.2f && density >= 50)
	{
		const auto cur_center = multipoint_geom::centroid(current_hull);
		const auto cur_ext = multipoint_geom::extremes(current_hull);

		add_world(to_world(multipoint_geom::lerp(cur_center, cur_ext.left, rs)), false);
		add_world(to_world(multipoint_geom::lerp(cur_center, cur_ext.right, rs)), false);

		if (is_head)
			add_world(to_world(multipoint_geom::lerp(cur_center, cur_ext.top, rs)), false);
	}

	return points;
}

multipoints_t c_ragebot::get_points(c_cs_player* player, int hitbox, matrix3x4_t* matrix, anim_record_t* record)
{
	// OLD CODE (kept for revert): the "Advanced multipoint (BETA)" toggle was
	// removed; advanced multipoint is always used now.
	// if (g_cfg.rage.multipoint_advanced)
	//	return get_points_advanced(player, hitbox, matrix, record);
	//
	// return get_points_legacy(player, hitbox, matrix, record);
	return get_points_advanced(player, hitbox, matrix, record);
}

void c_ragebot::run_stop()
{
	if (!HACKS->weapon || HACKS->client_state->delta_tick == -1)
		return;

	if (!HACKS->weapon_info || !HACKS->weapon || !trigger_stop)
		return;

	auto force_acc = rage_config.quick_stop_options & force_accuracy;

	auto max_speed = HACKS->local->is_scoped() ? HACKS->weapon_info->max_speed_alt : HACKS->weapon_info->max_speed;
	max_speed *= 0.34f;

	auto velocity = HACKS->local->velocity();
	if (velocity.length_2d() <= 15.f)
		return;

	if (velocity.length_2d() <= max_speed)
	{
		// Already inside the accuracy threshold: this is the accurate state, so
		// allow the shot. OLD CODE (kept for revert) had this inverted - it set
		// should_shot = false while accurate and true while still braking, so the
		// bot fired exactly when it was not accurate.
		game_movement::modify_move(*HACKS->cmd, velocity, max_speed);

		if (force_acc)
			should_shot = true;

		return;
	}

	// Too fast to be accurate: brake now and do NOT fire until the predicted
	// velocity is under the threshold on a following command.
	game_movement::force_stop();

	if (force_acc)
		should_shot = false;
}

void c_ragebot::auto_pistol()
{
	if (!HACKS->weapon)
		return;

	auto index = HACKS->weapon->item_definition_index();
	if (index == WEAPON_C4
		|| index == WEAPON_HEALTHSHOT
		|| index == WEAPON_REVOLVER
		|| (index == WEAPON_GLOCK || index == WEAPON_FAMAS) && HACKS->weapon->burst_shots_remaining() > 0)
		return;

	if (HACKS->weapon->is_misc_weapon() && !HACKS->weapon->is_knife())
		return;

	auto next_attack = HACKS->local->next_attack();
	auto next_primary_attack = HACKS->weapon->next_primary_attack();
	auto next_secondary_attack = HACKS->weapon->next_secondary_attack();

	if (HACKS->predicted_time < next_attack || HACKS->predicted_time < next_primary_attack)
	{
		if (HACKS->cmd->buttons.has(IN_ATTACK))
			HACKS->cmd->buttons.remove(IN_ATTACK);
	}

	if (HACKS->predicted_time < next_secondary_attack)
	{
		if (HACKS->cmd->buttons.has(IN_ATTACK2))
			HACKS->cmd->buttons.remove(IN_ATTACK2);
	}
}

void c_ragebot::force_scope()
{
	if (!rage_config.auto_scope)
		return;

	bool able_to_zoom = HACKS->predicted_time >= HACKS->weapon->next_secondary_attack();
	if (able_to_zoom && HACKS->weapon->zoom_level() < 1 && HACKS->weapon->is_sniper() && !HACKS->cmd->buttons.has(IN_ATTACK2))
		HACKS->cmd->buttons.force(IN_ATTACK2);
}

bool c_ragebot::should_stop(const rage_point_t& point)
{
	if (!rage_config.quick_stop)
		return false;

	if (HACKS->weapon->is_taser())
		return false;

	auto unpredicted_vars = ENGINE_PREDICTION->get_unpredicted_vars();
	if (!unpredicted_vars)
		return false;

	bool on_ground = MOVEMENT->on_ground();

	if (!on_ground && !(rage_config.quick_stop_options & in_air))
		return false;

	if (rage_config.quick_stop_options & between_shots)
		return !can_fire();

	if (rage_config.quick_stop_options & early)
		return true;

	return false;
}
void c_ragebot::update_predicted_eye_pos()
{
	auto unpredicted_vars = ENGINE_PREDICTION->get_initial_vars();
	if (!unpredicted_vars)
		return;

	auto anim = ANIMFIX->get_local_anims();
	if (!anim)
		return;

	// Fatality predictive lookahead: eye + unpredicted velocity * 3 ticks.
	// This is a trigger for the early-autostop scan, not a shooting origin -
	// the final bullet/hitchance/aim always use the real eye position.
	if (unpredicted_vars->velocity.length_2d() <= 20.f)
	{
		predicted_eye_pos = anim->eye_pos;
		return;
	}

	predicted_eye_pos = anim->eye_pos + unpredicted_vars->velocity * HACKS->global_vars->interval_per_tick * 3.f;
}
void c_ragebot::prepare_players_for_scan()
{
	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			auto& rage = rage_players[player->index()];

			if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
			{
				if (rage.valid)
					rage.reset();
				return;
			}

			if (rage.player != player)
			{
				rage.reset();
				rage.player = player;
				// Fall through instead of returning: reset_rage_players() clears
				// `player`, so returning here meant every following tick took this
				// branch, rage_player_iter stayed 0 and the ragebot never scanned
				// (no [SHOT]/[PRED] at all).
			}

			rage.distance = HACKS->local->origin().dist_to(player->origin());

			// NOTE (soundness): a "speed gate" is intentionally NOT applied as a
			// target skip here. Filtering slow targets would make the ragebot
			// ignore stationary enemies (campers holding angles), which is never
			// correct for target selection. Position-based staleness detection
			// belongs to record validation, not target filtering.

			// CACHE DYNAMIC SCALE HERE
			float speed = HACKS->local->velocity().length_2d();
			rage.cached_dynamic_scale = std::clamp(speed / 260.f, 0.f, 1.f);

			rage.valid = true;
			++rage_player_iter;

			// Blind brute-force: only for a stationary, non-bot target that
			// actually has fake desync, only after a long dry spell, and never
			// while the jitter resolver is already centering (that would fight
			// the better signal and churn the side).
			// OLD CODE (kept for revert): this rotated bots and fast-moving
			// players with 0 real misses and overrode the jitter center.
			auto brute_anims = ANIMFIX->get_anims(player->index());
			const bool has_fake = brute_anims && !brute_anims->records.empty() && brute_anims->records.front().choke >= 2;
			const bool stationary = player->velocity().length_2d() < 5.f;

			if (g_cfg.rage.resolver && !player->is_bot() && has_fake && stationary && rage.failed_scan_ticks >= 32
				&& !resolver_info[player->index()].jitter.is_jitter)
			{
				auto& info = resolver_info[player->index()];
				resolver::on_miss(player->index(), info.side);
				rage.failed_scan_ticks = 0;
			}
		});
}
std::vector<rage_point_t> get_hitbox_points(int damage, std::vector<int>& hitboxes, vec3_t& eye_pos, vec3_t& predicted_eye_pos, rage_player_t* rage, anim_record_t* record, bool predicted = false)
{
	if (hitboxes.empty())
		return{};

	if (g_cfg.rage.delay_lc)
	{
		if (record->break_lc)
			return{};
	}

	std::vector<rage_point_t> out{};
	out.reserve(hitboxes.size());

	auto matrix_to_aim = get_aim_matrix(rage->player, record);
	LAGCOMP->set_record(rage->player, record, matrix_to_aim);

	auto local_anims = ANIMFIX->get_local_anims();
	// OLD CODE (kept for revert): the passed eye was ignored and the scan used
	// the un-pitch-adjusted local eye, which did not match the shot origin.
	// auto& start_eye_pos = predicted ? predicted_eye_pos : local_anims->eye_pos;
	auto& start_eye_pos = predicted ? predicted_eye_pos : eye_pos;

	int wrong_damage_counter = 0;
	for (auto& hitbox : hitboxes)
	{
		auto aim_point = rage->player->get_hitbox_position(hitbox, matrix_to_aim);
		auto bullet = penetration::simulate(HACKS->local, rage->player, start_eye_pos, aim_point);

		// NOTE: do not reject the center for being below min damage here; its
		// multipoints can still clear the wall / do lethal damage. choose_best_point
		// applies the final min-damage filter.
		// OLD CODE (kept for revert):
		// if (bullet.traced_target == nullptr
		//	|| bullet.traced_target != rage->player
		//	|| HACKS->weapon->is_taser() && bullet.penetration_count < 4)
		// 	continue;
		//
		// Head-hitgroup validation (Fatality extra_head_calc parity) is NOT
		// applied here anymore: rejecting head points whose scan trace registers
		// neck/chest emptied points_to_scan and made head-only never fire (all
		// blue raw dots, no shot). The corrected-origin final gate is the
		// authority for head-vs-body.
		if (bullet.traced_target == nullptr
			|| bullet.traced_target != rage->player
			|| HACKS->weapon->is_taser() && bullet.penetration_count < 4)
			continue;

		rage_point_t point{};
		point.center = true;
		point.hitbox = hitbox;
		point.damage = bullet.damage;
		point.aim_point = aim_point;
		point.predicted_eye_pos = predicted;
		point.record = record;

#ifndef LEGACY
		point.safety = calc_point_safety(start_eye_pos, aim_point, rage, hitbox, record);
#endif

		out.emplace_back(point);
	}

	return out;
}

void player_move(c_cs_player* player, anim_record_t* record)
{
	static auto sv_gravity = HACKS->convars.sv_gravity;
	static auto sv_jump_impulse = HACKS->convars.sv_jump_impulse;

	auto src = record->prediction.origin;
	auto end = src + record->prediction.velocity * HACKS->global_vars->interval_per_tick;

	c_game_trace t;
	c_trace_filter filter;
	filter.skip = player;

	HACKS->engine_trace->trace_ray(ray_t(src, end, record->mins, record->maxs), MASK_PLAYERSOLID, &filter, &t);

	if (t.fraction != 1.f)
	{
		for (auto i = 0; i < 2; i++)
		{
			record->prediction.velocity -= t.plane.normal * record->prediction.velocity.dot(t.plane.normal);

			const auto dot = record->prediction.velocity.dot(t.plane.normal);
			if (dot < 0.f)
				record->prediction.velocity -= vec3_t{ dot * t.plane.normal.x, dot * t.plane.normal.y, dot * t.plane.normal.z };

			end = t.end + record->prediction.velocity * TICKS_TO_TIME(1.f - t.fraction);

			HACKS->engine_trace->trace_ray(ray_t(t.end, end, record->mins, record->maxs), MASK_PLAYERSOLID, &filter, &t);

			if (t.fraction == 1.f)
				break;
		}
	}

	src = end = record->prediction.origin = t.end;
	end.z -= 2.f;

	HACKS->engine_trace->trace_ray(ray_t(record->prediction.origin, end, record->mins, record->maxs), MASK_PLAYERSOLID, &filter, &t);

	record->prediction.flags.remove(FL_ONGROUND);

	if (t.fraction != 1.f && t.plane.normal.z > 0.7f)
		record->prediction.flags.force(FL_ONGROUND);
}

bool start_fakelag_fix(c_cs_player* player, anims_t* anims)
{
	if (anims->records.empty() || player->dormant())
		return false;

	// Always clear the front record's stale prediction first (even when the
	// feature is off), otherwise get_aim_matrix() keeps returning the frozen
	// predicted_matrix from the last tick extrapolation ran.
	anims->records.front().extrapolated = false;
	anims->records.front().extrapolate_ticks = 0;

	// Extrapolation master switch: off -> the ragebot only uses real/backtrack
	// records (the backtrack scan runs instead).
	if (!g_cfg.rage.extrapolation)
		return false;

	size_t size = 0;
	for (const auto& it : anims->records)
	{
		if (it.dormant)
			break;
		++size;
	}

	auto record = &anims->records.front();
	record->extrapolated = false;
	record->predict();

	// Fatality `run_extrapolation` parity.
	// PROOF OF THE OLD BUG (05:12-build log): with enemy chokes up to 16 the log
	// had 0 [PRED] lines and 0 extrapolated shots. Both old guards short-circuit
	// for the newest record at normal latency/choke:
	//   TIME_TO_TICKS(outgoing) <= lag - updatedelta     -> outgoing_ticks <= lag
	//   server_tick_estimation + 1 + lag >= arrival_tick -> lag + 1 >= outgoing_ticks
	// Replaced with Fatality's highest-simtime staleness check + bounded lagamt
	// math (lag_compensation.cpp:100-182).
	// OLD CODE (kept for revert):
	// const float expected_sim = TICKS_TO_TIME(server_tick) - HACKS->ping - LAGCOMP->get_interp_time();
	// const bool stale_record = record->sim_time < expected_sim - interval;
	// if (record->choke <= 0 || (!record->break_lc && !stale_record)) return false;
	// int simulation = TIME_TO_TICKS(record->sim_time);
	// if (std::abs(HACKS->arrival_tick - simulation) >= 128) return false;
	// int lag = record->choke;
	// int updatedelta = server_tick - record->server_tick_estimation;
	// if (TIME_TO_TICKS(HACKS->outgoing) <= lag - updatedelta ||
	//	record->server_tick_estimation + 1 + lag >= HACKS->arrival_tick) return false;
	// auto latency = std::clamp(HACKS->ping, 0.0f, 1.0f);
	// auto correct = std::clamp(latency + LAGCOMP->get_interp_time(), 0.0f, HACKS->convars.sv_maxunlag->get_float());
	// auto delta_time = correct - (TICKS_TO_TIME(HACKS->tickbase) - record->sim_time);
	// auto predicted_tick = (server_tick + TIME_TO_TICKS(latency) - record->server_tick_estimation) / record->choke;
	// if (predicted_tick > 0 && predicted_tick < 20 || EXPLOITS->cl_move.shifting) {
	//	auto max_backtrack_time = std::ceil(((delta_time - 0.2f) / HACKS->global_vars->interval_per_tick + 0.5f) / (float)record->choke);
	//	auto prediction_ticks = std::min(predicted_tick, TIME_TO_TICKS(max_backtrack_time));
	//	if (prediction_ticks > 0) {
	// expected_sim keeps the one-way term the firing build used; the server-source
	// lerp-only target marked every record stale and spammed the extrapolator.
	const float one_way = HACKS->incoming > 0.f ? HACKS->incoming : HACKS->ping * 0.5f;
	const float rtt = HACKS->ping > 0.f ? HACKS->ping : (HACKS->incoming + HACKS->outgoing);
	const float expected_sim = TICKS_TO_TIME(HACKS->client_state->clock_drift_mgr.server_tick) - one_way - LAGCOMP->get_interp_time();
	// An invalid front record cannot be backtracked either, so treat it as stale
	// too - otherwise "valid=0 + extrap=0" windows produce no points and the bot
	// never fires (log: [SCAN] ... valid=0 with [PRED]=0).
	const bool front_invalid = !record->valid_lc;
	const bool stale_record = front_invalid
		|| record->sim_time < anims->highest_sim_time
		|| record->sim_time < expected_sim - HACKS->global_vars->interval_per_tick;

	if (record->choke <= 0 || (!record->break_lc && !stale_record))
		return false;

	const int server_tick = HACKS->client_state->clock_drift_mgr.server_tick;
	const int lag = std::max(1, record->choke);
	const int max_usrcmd = HACKS->convars.sv_maxusrcmdprocessticks
		? std::clamp(HACKS->convars.sv_maxusrcmdprocessticks->get_int(), 1, 16)
		: 16;
	const int ticks_behind = std::clamp(server_tick - record->server_tick_estimation, 0, max_usrcmd);

	// Never extrapolate past what the server can still process.
	const int possible_future_tick = server_tick + 1 + TIME_TO_TICKS(rtt) + 8;
	const int max_future_ticks = possible_future_tick - TIME_TO_TICKS(record->sim_time + LAGCOMP->get_interp_time());
	if (max_future_ticks <= 0)
		return false;

	// Fatality parity: find the smallest whole-choke shift that reaches a
	// server-valid simtime, then never predict past the server position.
	int lagamt = 0;
	for (int i = 1; i <= max_future_ticks; ++i)
	{
		const int candidate = std::clamp((i + ticks_behind) / lag * lag, 0, 64);
		if (LAGCOMP->is_tick_valid(false, record->break_lc, record->sim_time + TICKS_TO_TIME(candidate)))
		{
			lagamt = candidate;
			break;
		}
	}

	const int server_position = std::clamp((TIME_TO_TICKS(one_way) + ticks_behind) / lag * lag, 0, 64);
	if (lagamt <= 0 || server_position < lagamt)
		lagamt = server_position;

	// Weapon-aware cap, kept to whole chokes so the predicted pose lands on a
	// record. The scout is a precision one-tap: auto keeps it to a single choke
	// unit so backtrack stays preferred; a manual menu value overrides.
	const bool is_scout = HACKS->weapon && HACKS->weapon->item_definition_index() == WEAPON_SSG08;
	const int manual_cap = std::clamp(g_cfg.rage.extrapolation_ticks, 0, 16);
	const int cap = manual_cap > 0 ? manual_cap : (is_scout ? lag : 64);
	int prediction_ticks = std::min(lagamt, std::max(lag, (cap / lag) * lag));
	prediction_ticks = (prediction_ticks / lag) * lag;

	// Whole-choke rounding can zero the prediction when ping < one choke unit
	// (log: choke=16, valid=0 for every record -> "no scannable points", no
	// shots at all). The stale record is rejected by lag comp, so move the
	// target to the server's current time with a plain per-tick prediction
	// instead of giving up. Bounded by the server horizon.
	if (prediction_ticks <= 0 && max_future_ticks > 0)
	{
		const int raw = TIME_TO_TICKS(HACKS->ping) + ticks_behind;
		prediction_ticks = std::clamp(raw > 0 ? raw : 1, 1, std::min(max_future_ticks, 16));
	}

	if (prediction_ticks > 0)
	{
			record->extrapolate_ticks = prediction_ticks;

			// OLD CODE (kept for revert):
			// for (int i = 0; i < prediction_ticks; ++i)
			// {
			//	for (int j = 0; j < record->choke; ++j)
			//	{
			//		if (record->prediction.flags.has(FL_ONGROUND))
			//		{
			//			if (!HACKS->convars.sv_enablebunnyhopping->get_int())
			//			{
			//				float max_speed = player->max_speed() * 1.1f;
			//				float speed = record->prediction.velocity.length();
			//				if (max_speed > 0.f && speed > max_speed)
			//					record->prediction.velocity *= (max_speed / speed);
			//			}
			//			record->prediction.velocity.z = HACKS->convars.sv_jump_impulse->get_float();
			//		}
			//		else
			//		{
			//			record->prediction.velocity.z -= HACKS->convars.sv_gravity->get_float() * HACKS->global_vars->interval_per_tick;
			//		}
			//
			//		player_move(player, record);
			//		record->prediction.time += HACKS->global_vars->interval_per_tick;
			//	}
			// }
			//
			// Prediction upgrade (research: Fatality resolver.cpp:47-233
			// `extrapolate_record`):
			//  - horizontal velocity change is predicted from the two newest
			//    records so accelerating / slow-walking / decelerating targets
			//    are not extrapolated at a stale speed;
			//  - the old code forced a jump impulse on EVERY grounded tick,
			//    which produced a bouncing prediction; a jump is now predicted
			//    once, when the newest record just landed and the previous one
			//    was airborne (bhop re-jump prediction).
			vec3_t predicted_accel{};
			if (anims->records.size() > 1)
			{
				const auto& p1 = anims->records[1];
				const float dt1 = std::max(record->sim_time - p1.sim_time, HACKS->global_vars->interval_per_tick);

				// Fatality 2023 resolver.cpp:94-97: predict the NEXT velocity
				// change as (newest change) - (previous change) so a braking or
				// accelerating target is not extrapolated at a stale rate.
				predicted_accel = (record->prediction.velocity - p1.velocity) / dt1;

				if (anims->records.size() > 2)
				{
					const auto& p2 = anims->records[2];
					const float dt2 = std::max(p1.sim_time - p2.sim_time, HACKS->global_vars->interval_per_tick);
					predicted_accel = ((record->prediction.velocity - p1.velocity) / dt1)
						- ((p1.velocity - p2.velocity) / dt2);
				}

				predicted_accel.x = std::clamp(predicted_accel.x, -1000.f, 1000.f);
				predicted_accel.y = std::clamp(predicted_accel.y, -1000.f, 1000.f);
				predicted_accel.z = 0.f;
			}

			bool predict_jump = false;
			if (anims->records.size() > 1
				&& record->prediction.flags.has(FL_ONGROUND)
				&& !anims->records[1].flags.has(FL_ONGROUND))
				predict_jump = true;

			// `prediction_ticks` is a total tick count (rounded to whole chokes,
			// Fatality lagamt semantics), so advance one movement tick per
			// iteration. The old nested choke loop multiplied it by choke again,
			// which over-extrapolated by a factor of `choke`.
			// OLD CODE (kept for revert):
			// for (int i = 0; i < prediction_ticks; ++i)
			//	for (int j = 0; j < record->choke; ++j)
			//	{
			//		if (predict_jump && i == 0 && j == 0)
			for (int t = 0; t < prediction_ticks; ++t)
			{
				record->prediction.velocity.x += predicted_accel.x * HACKS->global_vars->interval_per_tick;
				record->prediction.velocity.y += predicted_accel.y * HACKS->global_vars->interval_per_tick;

				if (record->prediction.flags.has(FL_ONGROUND))
				{
					if (!HACKS->convars.sv_enablebunnyhopping->get_int())
					{
						float max_speed = player->max_speed() * 1.1f;
						float speed = record->prediction.velocity.length_2d();
						if (max_speed > 0.f && speed > max_speed)
						{
							const float scale = max_speed / speed;
							record->prediction.velocity.x *= scale;
							record->prediction.velocity.y *= scale;
							predicted_accel.x *= scale;
							predicted_accel.y *= scale;
						}
					}

					if (predict_jump && t == 0)
					{
						record->prediction.velocity.z = HACKS->convars.sv_jump_impulse->get_float();
						record->prediction.flags.remove(FL_ONGROUND);
					}
				}
				else
				{
					record->prediction.velocity.z -= HACKS->convars.sv_gravity->get_float() * HACKS->global_vars->interval_per_tick;
				}

				player_move(player, record);
				record->prediction.time += HACKS->global_vars->interval_per_tick;
			}

			auto current_origin = record->prediction.origin;

			clamp_bones_info_t info{};
			info.collision_change_origin = record->collision_change_origin;
			info.collision_change_time = record->collision_change_time;
			info.origin = current_origin;
			info.collision_origin = current_origin;
			info.ground_entity = record->prediction.flags.has(FL_ONGROUND) ? 1 : -1;
			info.view_offset = record->view_offset;

			auto* src_matrix = get_resolver_matrix(player, record);
			math::change_bones_position(src_matrix, 128, record->origin, current_origin);
			math::memcpy_sse(record->predicted_matrix, src_matrix, sizeof(record->predicted_matrix));
			record->matrix_orig.bone_builder.clamp_bones_in_bbox(player, record->predicted_matrix, 0x7FF00, record->prediction.time, player->eye_angles(), info);

			math::change_bones_position(src_matrix, 128, current_origin, record->origin);
			record->extrapolated = true;

			if (rage_log::enabled())
				rage_log::line("[PRED] tgt=%s(%d) extrap ticks=%d choke=%d sim=%.3f->%.3f onground=%d vel=(%.0f %.0f %.0f) org=(%.0f %.0f %.0f)",
					player->get_name().c_str(), player->index(), prediction_ticks, record->choke,
					record->sim_time, record->prediction.time,
					record->prediction.flags.has(FL_ONGROUND) ? 1 : 0,
					record->prediction.velocity.x, record->prediction.velocity.y, record->prediction.velocity.z,
					record->prediction.origin.x, record->prediction.origin.y, record->prediction.origin.z);

			return true;
		}

	return false;
}

static std::vector<rage_point_t> collect_damage_from_multipoints(int damage, const vec3_t& predicted_eye_pos, const vec3_t& scan_eye, rage_player_t* rage, const rage_point_t& points, anim_record_t* record, matrix3x4_t* matrix_to_aim, bool predicted);

void pre_cache_centers(int damage, std::vector<int>& hitboxes, vec3_t& predicted_eye_pos, rage_player_t* rage)
{
	rage->reset_hitscan();
	auto anim = ANIMFIX->get_local_anims();

	auto lagcomp = ANIMFIX->get_anims(rage->player->index());
	if (!lagcomp || lagcomp->records.empty())
		return;

	rage->points_to_scan.clear();
	rage->points_to_scan.reserve(MAX_SCANNED_POINTS);

	// Per-target scan hitboxes. With "Baim on jitter" the body hitboxes are
	// appended as a fallback for heavily jittering targets or targets that keep
	// scoring body hits while we aim head: the head side is a coin flip against
	// jitter, the body is not.
	std::vector<int> scan_hitboxes = hitboxes;
	const int scan_idx = rage->player->index();
	if (g_cfg.rage.baim_on_jitter
		&& (resolver_info[scan_idx].jitter.is_jitter || RAGEBOT->m_head_body[scan_idx] >= 2))
	{
		const int fallback[] = { HITBOX_UPPER_CHEST, HITBOX_CHEST, HITBOX_STOMACH, HITBOX_PELVIS };
		for (const int hb : fallback)
		{
			if (std::find(scan_hitboxes.begin(), scan_hitboxes.end(), hb) == scan_hitboxes.end())
				scan_hitboxes.emplace_back(hb);
		}
	}

	auto scan_record = [&](anim_record_t* record, bool predicted)
		{
			if (rage->points_to_scan.size() >= MAX_SCANNED_POINTS)
				return;

			if (g_cfg.rage.delay_lc && record->break_lc)
				return;

			// OLD CODE (kept for revert): the global MAX_SCANNED_POINTS cap
			// alone let the newest records consume the whole budget, so older
			// backtrack records were never scanned. Cap each record so every
			// valid record can contribute candidates.
			constexpr size_t PER_RECORD_POINT_CAP = 96;
			const size_t record_start = rage->points_to_scan.size();
			const auto record_budget_left = [&]() {
				return rage->points_to_scan.size() - record_start < PER_RECORD_POINT_CAP;
				};

			auto matrix_to_aim = get_aim_matrix(rage->player, record);

			auto center_points = get_hitbox_points(damage, scan_hitboxes, RAGEBOT->scan_eye_pos, predicted_eye_pos, rage, record, predicted);

			// OLD CODE (kept for revert):
			// for (auto& cp : center_points)
			//	rage->points_to_scan.emplace_back(cp);
			for (auto& cp : center_points)
			{
				if (rage->points_to_scan.size() >= MAX_SCANNED_POINTS || !record_budget_left())
					break;

				rage->points_to_scan.emplace_back(cp);
			}

			for (auto& cp : center_points)
			{
				if (rage->points_to_scan.size() >= MAX_SCANNED_POINTS || !record_budget_left())
					break;

				auto extra = collect_damage_from_multipoints(damage, predicted_eye_pos, RAGEBOT->scan_eye_pos, rage, cp, record, matrix_to_aim, predicted);
				for (auto& p : extra)
				{
					if (rage->points_to_scan.size() >= MAX_SCANNED_POINTS || !record_budget_left())
						break;
					rage->points_to_scan.emplace_back(std::move(p));
				}
			}
		};

	rage->restore.store(rage->player);

	// Extrapolation is an extra candidate, not a replacement for the backtrack
	// scan. Real (rewound) records are always preferred; the predicted front is
	// appended only when it exists. A precision one-tap (scout) must see the
	// real server pose before it is allowed to shoot at a predicted one.
	const bool extrapolated = start_fakelag_fix(rage->player, lagcomp);

	if (HACKS->cl_lagcomp0)
	{
		// Server lag compensation disabled: old records would be rewound to the
		// present pose, so only the current/predicted front is meaningful.
		scan_record(&lagcomp->records.front(), extrapolated);
	}
	else
	{
		// Full backtrack scan across every valid record in the window.
		// Backtrack disabled -> only the newest valid record is considered.
		constexpr int MAX_BACKTRACK_RECORDS = 14;
		int scanned = 0;
		size_t index = 0;

		for (auto& rec : lagcomp->records)
		{
			if (scanned >= MAX_BACKTRACK_RECORDS || rage->points_to_scan.size() >= MAX_SCANNED_POINTS)
				break;

			// The front record is scanned as the extrapolated candidate below.
			if (extrapolated && index++ == 0)
				continue;

			if (EXPLOITS && EXPLOITS->break_lc_tick > 0)
			{
				int delta = std::abs(HACKS->cmd->command_number - EXPLOITS->break_lc_tick);
				rec.break_lc = (delta >= 0 && delta <= 10);
			}

			if (!rec.valid_lc)
				continue;

			++scanned;
			scan_record(&rec, false);

			if (!g_cfg.rage.backtrack)
				break;
		}

		if (extrapolated)
			scan_record(&lagcomp->records.front(), true);
	}

	rage->restore.restore(rage->player);

	if (!rage->points_to_scan.empty())
	{
		rage->start_scans = true;
		rage->hitscan_record = &lagcomp->records.front();
	}
	else if (rage_log::enabled())
	{
		// Throttled per target: why did the scan produce nothing?
		static int last_scan_log[65]{};
		const int tick = HACKS->global_vars ? HACKS->global_vars->tickcount : 0;
		const int idx = rage->player ? rage->player->index() : 0;
		if (idx > 0 && idx < 65 && tick - last_scan_log[idx] > 32)
		{
			last_scan_log[idx] = tick;
			rage_log::line("[SCAN] tgt=%s(%d) no points records=%d valid=%d shift=%d hitboxes=%d extrap=%d new_sim=%.3f srv_tick=%d pred=%.3f interp=%.3f gap=%.3f",
				rage->player ? rage->player->get_name().c_str() : "-", idx,
				(int)lagcomp->records.size(), lagcomp->dbg_valid, lagcomp->dbg_shifting,
				(int)hitboxes.size(), extrapolated ? 1 : 0,
				lagcomp->records.empty() ? 0.f : lagcomp->records.front().sim_time,
				HACKS->client_state ? HACKS->client_state->clock_drift_mgr.server_tick : 0,
				HACKS->predicted_time, LAGCOMP->get_interp_time(),
				lagcomp->records.empty() ? 0.f : (HACKS->predicted_time - lagcomp->records.front().sim_time));
		}
	}
}

void get_result(bool& out, const vec3_t& start, const vec3_t& end, rage_player_t* rage, int hitbox, matrix3x4_t* matrix, anim_record_t* record)
{
	out = can_hit_hitbox(start, end, rage, hitbox, matrix, record);
}

bool hitchance(vec3_t eye_pos, rage_player_t& rage, const rage_point_t& point, anim_record_t* record, const float& chance, matrix3x4_t* matrix, float* hitchance_out = nullptr)
{
	static auto weapon_accuracy_nospread = HACKS->convars.weapon_accuracy_nospread;
	if (weapon_accuracy_nospread && weapon_accuracy_nospread->get_bool())
	{
		if (hitchance_out)
			*hitchance_out = 1.f;
		return true;
	}

#ifdef LEGACY
	if (EXPLOITS->enabled() && EXPLOITS->dt_bullet == 1)
		return true;
#endif

	auto current = 0;
	auto networked_vars = ENGINE_PREDICTION->get_networked_vars(HACKS->cmd->command_number);

	auto matrix_to_aim = get_aim_matrix(rage.player, record);
	auto current_bones = matrix ? matrix : matrix_to_aim;
	auto anim = ANIMFIX->get_local_anims();

	const float total_inaccuracy = networked_vars->spread + networked_vars->inaccuracy;

	// OLD CODE (kept for revert): the ideal-inaccuracy shortcut returned 100%
	// whenever the current cone was at the stance minimum, which at long range
	// bypassed min-HC and caused "random" misses. Always sample (Fatality does).

	rage.restore.store(rage.player);
	LAGCOMP->set_record(rage.player, record, current_bones);

	auto start = eye_pos;
	auto aim_angle = math::calc_angle(start, point.aim_point);

	vec3_t forward, right, up;
	math::angle_vectors(aim_angle, &forward, &right, &up);

	vec3_t total_spread, spread_angle, end;
	float inaccuracy, spread_x, spread_y;
	std::tuple<float, float, float>* seed{};

	for (auto i = 0; i < MAX_SEEDS; i++)
	{
		seed = &precomputed_seeds[i];

		inaccuracy = std::get<0>(*seed) * total_inaccuracy;
		spread_x = std::get<2>(*seed) * inaccuracy;
		spread_y = std::get<1>(*seed) * inaccuracy;
		total_spread = (forward + right * spread_x + up * spread_y).normalized();

		math::vector_angles(total_spread, spread_angle);

		math::angle_vectors(spread_angle, end);
		end = start + end.normalized() * HACKS->weapon_info->range;

		// Count any hitbox the spread ray reaches on the target, not only the
		// exact requested hitgroup. The old can_hit_hitbox() required
		// trace.hitgroup == studio_box->group, which understated hitchance and
		// blocked otherwise hittable points (Fatality runs a full trace over all
		// hitboxes for the spread test). The min-damage gate still filters
		// lethality separately.
		c_game_trace hc_trace{};
		HACKS->engine_trace->clip_ray_to_entity({ start, end }, MASK_SHOT_HULL | CONTENTS_HITBOX, rage.player, &hc_trace);
		if (hc_trace.entity == rage.player)
			current++;

		if (hitchance_out)
			*hitchance_out = (float)current / (float)MAX_SEEDS;
	}

	rage.restore.restore(rage.player);

	return ((float)current / (float)MAX_SEEDS) >= chance;
}

// If the resolved side cannot reach the selected point any more (or cannot meet
// hitchance on it), scan the remaining resolver sides with the same hitbox.
// This is what keeps the ragebot firing on desync targets: the resolver gets a
// real chance to be right immediately instead of only learning from misses.
bool c_ragebot::try_alternate_sides(rage_player_t& rage, rage_point_t& point, anim_record_t* record, const vec3_t& eye_pos, int damage, float chance, float& out_chance)
{
	if (!record || !rage.player || !HACKS->weapon || !HACKS->weapon_info)
		return false;

	const int index = rage.player->index();
	auto& info = resolver_info[index];

	const int original_side = info.side;
	const bool original_resolved = info.resolved;

	// OLD CODE (kept for revert):
	// static const int sides[] = { side_left, side_right, side_zero };
	//
	// Extended candidate set (Fatality min_min/max_max parity).
	static const int sides[] = { side_left, side_right, side_zero, side_left_extra, side_right_extra };

	for (const int side : sides)
	{
		if (info.resolved && side == original_side)
			continue;

		info.side = side;
		info.resolved = true;

		auto matrix = get_aim_matrix(rage.player, record);

		rage.restore.store(rage.player);
		LAGCOMP->set_record(rage.player, record, matrix);

		// OLD CODE (kept for revert):
		// auto aim_point = rage.player->get_hitbox_position(point.hitbox, matrix);
		//
		// Re-trace the point we actually scanned/aimed at (world-space, shared
		// across resolver directions) instead of snapping the impact back to the
		// raw hitbox centre. Matches Fatality `fix_shot`, which re-traces the
		// real shot point after correcting the origin.
		const vec3_t aim_point = point.aim_point;
		auto bullet = penetration::simulate(HACKS->local, rage.player, eye_pos, aim_point);

		rage.restore.restore(rage.player);

		// OLD CODE (kept for revert):
		// if (bullet.traced_target != rage.player || bullet.damage < damage)
		// 	continue;
		//
		// Head-hitgroup validation (see get_hitbox_points).
		if (bullet.traced_target != rage.player || bullet.damage < damage
			|| point.hitbox == HITBOX_HEAD && bullet.hitgroup != HITGROUP_HEAD)
			continue;

		if (HACKS->weapon->is_taser() && bullet.penetration_count < 4)
			continue;

		rage_point_t candidate = point;
		candidate.aim_point = aim_point;
		candidate.damage = bullet.damage;
		candidate.record = record;
		candidate.found = true;

#ifndef LEGACY
		candidate.safety = calc_point_safety(eye_pos, aim_point, &rage, point.hitbox, record);
#endif

		if (hitchance(eye_pos, rage, candidate, record, chance, matrix, &out_chance))
		{
			candidate.accuracy = out_chance;
			point = candidate;

			info.side = side;
			info.resolved = true;
			info.mode = XOR("scan");
			info.anim_resolve_ticks = HACKS->global_vars->tickcount;
			info.last_miss_count = missed_shots[index];
			return true;
		}
	}

	info.side = original_side;
	info.resolved = original_resolved;
	return false;
}

static std::vector<rage_point_t> collect_damage_from_multipoints(int damage, const vec3_t& predicted_eye_pos, const vec3_t& scan_eye, rage_player_t* rage, const rage_point_t& points, anim_record_t* record, matrix3x4_t* matrix_to_aim, bool predicted)
{
	std::vector<rage_point_t> out;

	auto multipoints = RAGEBOT->get_points(rage->player, points.hitbox, matrix_to_aim, record);
	if (multipoints.empty())
		return out;

	auto local_anims = ANIMFIX->get_local_anims();

	// OLD CODE (kept for revert):
	// const auto& start_eye_pos = predicted ? predicted_eye_pos : local_anims->eye_pos;
	const auto& start_eye_pos = predicted ? predicted_eye_pos : scan_eye;

	for (auto& multipoint : multipoints)
	{
		if (multipoint.second)
			continue;

		auto bullet = penetration::simulate(HACKS->local, rage->player, start_eye_pos, multipoint.first);
		// OLD CODE (kept for revert):
		// if (bullet.damage < damage
		//	|| bullet.traced_target == nullptr || bullet.traced_target != rage->player
		//	|| HACKS->weapon->is_taser() && bullet.penetration_count < 4)
		// 	continue;
		//
		// Head-hitgroup validation removed here too (see get_hitbox_points):
		// the corrected-origin final gate decides head vs body, so the scan pool
		// stays populated instead of going empty in head-only.
		if (bullet.damage < damage
			|| bullet.traced_target == nullptr || bullet.traced_target != rage->player
			|| HACKS->weapon->is_taser() && bullet.penetration_count < 4)
			continue;

		rage_point_t point{};
		point.center = false;
		point.hitbox = points.hitbox;
		point.damage = bullet.damage;
		point.aim_point = multipoint.first;
		point.predicted_eye_pos = points.predicted_eye_pos;
		point.record = record;

#ifndef LEGACY
		point.safety = calc_point_safety(start_eye_pos, multipoint.first, rage, points.hitbox, record);
#endif
		out.emplace_back(point);
	}

	return out;
}

void c_ragebot::scan_players()
{
	int threads_count = 0;

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			auto rage = &rage_players[player->index()];
			if (!rage || !rage->player || rage->player != player)
				return;

			++threads_count;

			auto dmg = get_min_damage(rage->player);
			THREAD_POOL->add_task(pre_cache_centers, dmg, std::ref(hitboxes), std::ref(predicted_eye_pos), rage);
		});

	if (threads_count < 1)
		return;

	THREAD_POOL->wait_all();
}

void c_ragebot::choose_best_point()
{
	auto prefer_baim_on_dt = EXPLOITS->enabled() && EXPLOITS->get_exploit_mode() == EXPLOITS_DT
		&& (HACKS->weapon->is_auto_sniper() || HACKS->weapon->is_heavy_pistols());

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			auto rage = &rage_players[player->index()];
			if (!rage || !rage->player || rage->player != player)
				return;

			auto damage = get_min_damage(rage->player);
			auto local_anims = ANIMFIX->get_local_anims();
			auto eye_pos = local_anims->eye_pos;
			auto health = player->health();

			// Down-pitch enemies (|pitch| > 80) tilt the head so desync resolution becomes
			// unreliable; prefer body hitboxes where desync barely matters.
			const bool down_pitch = std::fabsf(player->eye_angles().x) > 80.f;

			// Repeated head->body server mismatches only: the head is provably
			// not registering for this target, so body is the reliable option.
			// OLD CODE (kept for revert): also fired on jitter alone, which turned
			// clear head shots into stomach picks (log: client hb=stomach x4).
			// const bool jitter_baim = g_cfg.rage.baim_on_jitter
			//	&& (resolver_info[player->index()].jitter.is_jitter || m_head_body[player->index()] >= 2);
			const bool jitter_baim = g_cfg.rage.baim_on_jitter && m_head_body[player->index()] >= 2;

			auto get_best_aim_point = [&]() -> rage_point_t
				{
					rage_point_t best{};
					rage_point_t best_bracketing{};
					std::sort(rage->points_to_scan.begin(), rage->points_to_scan.end(), [](const rage_point_t& a, const rage_point_t& b) {
						return a.damage > b.damage;
						});

					float best_score = -1.f;
					float best_bracketing_score = -1.f;
					int evaluated = 0;
					constexpr int MAX_ACCURACY_EVAL = 16;

					// A body point may only pre-empt when no valid head point
					// exists. The old code returned a body point on
					// down_pitch/prefer_baim_on_dt/prefer_body/damage>=health even
					// when a head point was available, which turned clear head
					// shots into stomach picks (log: client hb=stomach x6). Fatality
					// has no lethal-body pre-emption (hitscan.cpp:44-45, 245-327).
					bool head_available = false;
					for (const auto& p : rage->points_to_scan)
					{
						if (p.hitbox == HITBOX_HEAD && p.damage >= damage)
						{
							head_available = true;
							break;
						}
					}

					for (auto& point : rage->points_to_scan)
					{
						auto is_body = point.hitbox == HITBOX_PELVIS || point.hitbox == HITBOX_STOMACH;

						if (point.damage < damage)
							continue;

						if (g_cfg.binds[force_body_b].toggled && !is_body)
							continue;

						// Down-pitch enemies prefer body hitboxes (see the is_body
						// early-return below), but the head is never hard-skipped:
						// a head-only config must still be able to shoot them.
						(void)down_pitch;

#ifndef LEGACY
						if (g_cfg.binds[force_sp_b].toggled && point.safety < max_point_safety())
							continue;
						if (point.safety >= max_point_safety() && rage_config.prefer_safe)
						{
							point.found = true;
							return point;
						}
#endif

						if (is_body && !head_available && (point.damage >= health || prefer_baim_on_dt || rage_config.prefer_body || down_pitch || jitter_baim))
						{
							point.found = true;
							return point;
						}

						// Accuracy-weighted fallback: prefer high damage AND high hitchance.
						// Uses the record's matrix directly (no player state mutation).
						if (evaluated < MAX_ACCURACY_EVAL && point.record)
						{
							auto& accuracy_eye = point.predicted_eye_pos ? predicted_eye_pos : eye_pos;
							point.accuracy = get_point_accuracy(rage, accuracy_eye, point, get_aim_matrix(player, point.record), point.record);
							++evaluated;
						}

						// OLD CODE (kept for revert):
						// const float score = static_cast<float>(point.damage) * (0.6f + 0.4f * point.accuracy);
						//
						// Prefer points whose record brackets the server's
						// interpolation point (NL record-bracketing parity).
						const float bracket_weight = get_record_bracket_weight(point.record);
						const float score = static_cast<float>(point.damage) * (0.6f + 0.4f * point.accuracy) * (0.85f + 0.15f * bracket_weight);
						if (score > best_score)
						{
							best_score = score;
							best = point;
							best.found = true;
						}

						// Hard record priority: a record that brackets the server's
						// interpolation point must beat an old backtrack record even
						// if the old one has more damage (the logs showed wins with
						// bracket as low as 0.05 that the server never rewound to).
						//
						// OLD CODE (kept for revert - crash on 15:02 log: this was
						// gated on !g_cfg.rage.backtrack, which let the scan pick
						// bt=16 bracket=0.00 records and crashed during fire):
						// if (!g_cfg.rage.backtrack && bracket_weight >= 0.6f && score > best_bracketing_score)
						if (bracket_weight >= 0.6f && score > best_bracketing_score)
						{
							best_bracketing_score = score;
							best_bracketing = point;
							best_bracketing.found = true;
						}
					}

					if (best_bracketing.found)
						return best_bracketing;

					// OLD CODE (kept for revert): an unconditional
					// "return the highest-bracket record" fallback made the scan
					// force the newest record, where body points win and the head%
					// collapsed (log: 22:35 = 90% head, 23:32 = 21%). Keep the
					// most-damaging fallback; only a >= 0.6 bracket overrides it.
					return best;
				};

			auto best_point = get_best_aim_point();
			if (best_point.found)
			{
				rage->best_point = best_point;
				rage->best_record = best_point.record ? best_point.record : rage->hitscan_record;
				rage->best_point.found = true;
				rage->failed_scan_ticks = 0;
			}
			else
			{
				++rage->failed_scan_ticks;

				if (rage_log::enabled() && rage->failed_scan_ticks % 16 == 1)
					rage_log::line("[SCAN] tgt=%s(%d) no best point scanned=%d failed=%d",
						player->get_name().c_str(), player->index(),
						(int)rage->points_to_scan.size(), rage->failed_scan_ticks);
			}
		});
}

void c_ragebot::auto_revolver()
{
	if (!HACKS->local || !HACKS->weapon || !HACKS->weapon_info)
		return;

	auto next_secondary_attack = HACKS->weapon->next_secondary_attack();

	if (!g_cfg.rage.enable || EXPLOITS->recharge.start && !EXPLOITS->recharge.finish || HACKS->weapon->item_definition_index() != WEAPON_REVOLVER || HACKS->weapon->clip1() <= 0)
	{
		last_checked = 0;
		tick_cocked = 0;
		tick_strip = 0;
		next_secondary_attack = 0.f;

		revolver_fire = false;
		return;
	}

	auto time = TICKS_TO_TIME(HACKS->tickbase - EXPLOITS->tickbase_offset());
	const auto max_ticks = TIME_TO_TICKS(.25f) - 1;
	const auto tick_base = TIME_TO_TICKS(time);

	if (HACKS->local->next_attack() > time)
		return;

	if (HACKS->local->spawn_time() != last_spawn_time)
	{
		tick_cocked = tick_base;
		tick_strip = tick_base - max_ticks - 1;
		last_spawn_time = HACKS->local->spawn_time();
	}

	if (HACKS->weapon->next_primary_attack() > time)
	{
		HACKS->cmd->buttons.remove(IN_ATTACK);
		revolver_fire = false;
		return;
	}

	if (last_checked == tick_base)
		return;

	last_checked = tick_base;
	revolver_fire = false;

	if (tick_base - tick_strip > 2 && tick_base - tick_strip < 14)
		revolver_fire = true;

	if (HACKS->cmd->buttons.has(IN_ATTACK) && revolver_fire)
		return;

	HACKS->cmd->buttons.force(IN_ATTACK);

	if (next_secondary_attack >= time)
		HACKS->cmd->buttons.force(IN_ATTACK2);

	if (tick_base - tick_cocked > max_ticks * 2 + 1)
	{
		tick_cocked = tick_base;
		tick_strip = tick_base - max_ticks - 1;
	}

	const auto cock_limit = tick_base - tick_cocked >= max_ticks;
	const auto after_strip = tick_base - tick_strip <= max_ticks;

	if (cock_limit || after_strip)
	{
		tick_cocked = tick_base;
		HACKS->cmd->buttons.remove(IN_ATTACK);

		if (cock_limit)
			tick_strip = tick_base;
	}
}

bool c_ragebot::knife_is_behind(c_cs_player* player, anim_record_t* record)
{
	auto origin = record ? record->origin : player->get_abs_origin();
	auto abs_angles = record ? record->abs_angles : player->get_abs_angles();

	auto anim = ANIMFIX->get_local_anims();

	vec3_t delta{ origin - anim->eye_pos };
	delta.z = 0.f;
	delta = delta.normalized();

	vec3_t target;
	math::angle_vectors(abs_angles, target);
	target.z = 0.f;

	return delta.dot(target) > 0.475f;
}

bool c_ragebot::knife_trace(vec3_t dir, bool stab, c_game_trace* trace)
{
	float range = stab ? 32.f : 48.f;

	auto anim = ANIMFIX->get_local_anims();

	vec3_t start = anim->eye_pos;
	vec3_t end = start + (dir * range);

	c_trace_filter filter{};
	filter.skip = HACKS->local;
	HACKS->engine_trace->trace_ray(ray_t(start, end), MASK_SOLID, &filter, trace);

	if (trace->fraction >= 1.f)
	{
		HACKS->engine_trace->trace_ray(ray_t(start, end, { -16.f, -16.f, -18.f }, { 16.f, 16.f, 18.f }), MASK_SOLID, &filter, trace);
		return trace->fraction < 1.f;
	}

	return true;
}

bool c_ragebot::can_knife(c_cs_player* player, anim_record_t* record, vec3_t angle, bool& stab)
{
	vec3_t forward{};
	math::angle_vectors(angle, forward);

	c_game_trace trace{};
	knife_trace(forward, false, &trace);

	if (!trace.entity || trace.entity != player)
		return false;

	bool armor = player->armor_value() > 0;
	bool first = HACKS->weapon->next_primary_attack() + 0.4f < HACKS->predicted_time;
	bool back = knife_is_behind(player, record);

	int stab_dmg = knife_dmg.stab[armor][back];
	int slash_dmg = knife_dmg.swing[first][armor][back];
	int swing_dmg = knife_dmg.swing[false][armor][back];

	int health = player->health();
	if (health <= slash_dmg)
		stab = false;
	else if (health <= stab_dmg)
		stab = true;
	else if (health > (slash_dmg + swing_dmg + stab_dmg))
		stab = true;
	else
		stab = false;

	if (stab && !knife_trace(forward, true, &trace))
		return false;

	return true;
}

void c_ragebot::knife_bot()
{
	if (!g_cfg.rage.enable)
		return;

	if (HACKS->predicted_time < HACKS->weapon->next_primary_attack() || HACKS->predicted_time < HACKS->weapon->next_secondary_attack())
		return;

	bool supress_doubletap_choke = true;
	if (EXPLOITS->enabled() && EXPLOITS->get_exploit_mode() == EXPLOITS_DT)
		supress_doubletap_choke = EXPLOITS->defensive.tickbase_choke > 2;

	bool best_stab{};
	knife_point_t best{};

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			auto anims = ANIMFIX->get_anims(player->index());
			if (!anims || anims->records.empty())
				return;

			auto first_find = std::find_if(anims->records.begin(), anims->records.end(), [&](anim_record_t& record) {
				return record.valid_lc;
				});

			anim_record_t* first = nullptr;
			if (first_find != anims->records.end())
				first = &*first_find;

			restore_record_t backup{};
			backup.store(player);

			if (!first)
			{
				backup.restore(player);
				return;
			}

			{
				{
					LAGCOMP->set_record(player, first, first->matrix_orig.matrix);

					for (auto& a : knife_ang)
					{
						if (!can_knife(player, first, a, best_stab))
							continue;

						best.point = a;
						best.record = first;
						break;
					}
				}

				{
					auto last_find = std::find_if(anims->records.rbegin(), anims->records.rend(), [&](anim_record_t& record) {
						return record.valid_lc;
						});

					anim_record_t* last = nullptr;
					if (last_find != anims->records.rend())
						last = &*last_find;

					if (!last || last == first)
					{
						backup.restore(player);
						return;
					}

					LAGCOMP->set_record(player, last, last->matrix_orig.matrix);

					for (auto& a : knife_ang)
					{
						if (!can_knife(player, last, a, best_stab))
							continue;

						best.point = a;
						best.record = last;
						break;
					}
				}
			}
			backup.restore(player);

			if (best.record)
			{
				backup.restore(player);
				return;
			}
		});

	if (supress_doubletap_choke && best.record)
	{
		HACKS->cmd->viewangles = best.point.normalized_angle();

		if (best.record && !HACKS->cl_lagcomp0)
			HACKS->cmd->tickcount = TIME_TO_TICKS(best.record->sim_time + LAGCOMP->get_interp_time());

		HACKS->cmd->buttons.force(best_stab ? IN_ATTACK2 : IN_ATTACK);
	}
}

void c_ragebot::run()
{
	if (rage_log::enabled())
	{
		rage_log::session_header();

		// Log the previous tick's decision when it changes (fail -> fail or
		// fail -> fire), so a resolver/fire dry spell is visible without having
		// to touch every early return below.
		static std::string last_reason;
		if (!debug_reason.empty() && debug_reason != last_reason)
		{
			rage_log::line("[FAIL] %s", debug_reason.c_str());
			last_reason = debug_reason;
		}
	}

	if (!HACKS->weapon || !HACKS->weapon_info || HACKS->client_state->delta_tick == -1)
	{
		debug_reason = XOR("no weapon / delta -1");
		return;
	}

	auto_pistol();

	if (EXPLOITS->cl_move.trigger && EXPLOITS->cl_move.shifting)
	{
		debug_reason = XOR("cl_move shifting");
		return;
	}

	hitboxes.clear();
	hitboxes.reserve(HITBOX_MAX);

	rage_config = main_utils::get_weapon_config();
	update_hitboxes();

	// Standalone debug draw: runs before any rage enable/scan early-return so
	// the debug toggles work regardless of ragebot state.
	debug_draw_multipoints();

	trigger_stop = false;
	should_shot = true;
	reset_rage_hitscan = false;
	firing = false;
	working = false;
	rage_player_iter = 0;
	predicted_eye_pos.reset();
	best_rage_player.reset();

	if (!g_cfg.rage.enable || HACKS->weapon->is_misc_weapon() && !HACKS->weapon->is_taser() && !HACKS->weapon->is_knife())
	{
		debug_reason = XOR("rage disabled / misc weapon");
		reset_rage_players();
		return;
	}

	if (HACKS->weapon->is_knife())
	{
		debug_reason = XOR("knife handled");
		knife_bot();
		return;
	}

	update_predicted_eye_pos();

	// Origin alignment (fixes "all blue points, head-only never fires"): the
	// scan must trace from the same pitch-adjusted eye the shot will use, or a
	// point can register head at scan and chest at fire. Computed once on the
	// main thread; scan workers only read it.
	{
		auto local_anims = ANIMFIX->get_local_anims();
		if (local_anims)
			scan_eye_pos = ANIMFIX->get_eye_position(HACKS->cmd->viewangles.x);
	}

	prepare_players_for_scan();

	if (rage_player_iter < 1)
	{
		debug_reason = XOR("no valid players");
		reset_rage_players();
		return;
	}

	reset_rage_hitscan = true;

	scan_players();
	choose_best_point();
	// OLD CODE (kept for revert): the debug draw now happens earlier so it is
	// not gated behind the ragebot scan results.
	// debug_draw_multipoints();

	float lowest_distance = FLT_MAX;

	firing = false;
	working = false;
	best_rage_player.reset();

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			auto rage = &rage_players[player->index()];
			if (!rage || !rage->player || rage->player != player)
				return;

			if (!rage->start_scans)
				return;

			if (!rage->best_point.found)
				return;

			if (lowest_distance > rage->distance)
			{
				lowest_distance = rage->distance;
				best_rage_player = *rage;
			}
		});

	auto& best_point = best_rage_player.best_point;
	if (best_rage_player.player && best_point.found && best_rage_player.start_scans)
	{
		working = true;

		auto local_anims = ANIMFIX->get_local_anims();

		// also, with predictive scans we automatically achieved early auto stop without useless conditions & heavy code (@opai)
		// and auto scope too...

		auto damage = get_min_damage(best_rage_player.player);
		float out_chance = 0.f;
		auto max_hitchance = rage_config.hitchance * 0.01f;

		bool already_stooped = false;
		if (best_point.predicted_eye_pos && best_point.damage >= damage && (rage_config.quick_stop_options & early))
		{
			force_scope();

			if (rage_config.quick_stop && should_stop(best_point))
			{
				already_stooped = true;
				trigger_stop = true;
			}
		}

		auto aim_angle = math::calc_angle(local_anims->eye_pos, best_point.aim_point).normalized_angle();
		auto ideal_start = ANIMFIX->get_eye_position(aim_angle.x);

		auto best_record = best_rage_player.best_record;

		// when we have pre-scanned points we won't shoot at it's position from pred eye pos
		// because it's wrong
		// wait untill you will actually see the point and can shoot to it 
		{
			best_rage_player.restore.store(best_rage_player.player);

			auto matrix_to_aim = get_aim_matrix(best_rage_player.player, best_record);
			LAGCOMP->set_record(best_rage_player.player, best_record, matrix_to_aim);

			auto final_bullet = penetration::simulate(HACKS->local, best_rage_player.player, ideal_start, best_point.aim_point);
			best_rage_player.restore.restore(best_rage_player.player);

			// OLD CODE (kept for revert):
			// if (final_bullet.damage < damage || HACKS->weapon->is_taser() && final_bullet.penetration_count < 4)
			//
			// Fatality `fix_shot` parity (research: Fatality.win-Source
			// internal_hvh/features/aimbot.cpp lines 814-872). Fatality
			// recomputes the shot origin after prediction, re-traces, and
			// aborts the shot if the corrected trace no longer hits. Here the
			// scan validated a head point from the scan eye, but the real shot
			// starts at `ideal_start`, where the same point can register as
			// neck/chest. Re-verify hitgroup + target, then try to recover.
			const bool wrong_target = final_bullet.traced_target != best_rage_player.player;
			const bool head_mismatch = best_point.hitbox == HITBOX_HEAD && final_bullet.hitgroup != HITGROUP_HEAD;

			auto bullet_valid = [&](const bullet_t& b) -> bool
				{
					return b.damage >= damage
						&& b.traced_target == best_rage_player.player
						&& !(HACKS->weapon->is_taser() && b.penetration_count < 4)
						&& !(best_point.hitbox == HITBOX_HEAD && b.hitgroup != HITGROUP_HEAD);
				};

			bool gate_ok = bullet_valid(final_bullet);

			// Predictive fire (Fatality predictive autostop parity, aimbot.cpp
			// :313-321 / :389-394): while moving (>20 u/s) accept the point
			// validated from eye + unpred_vel*interval*3 instead of holding fire
			// for the corrected real origin. This is what removes the peek
			// delay - the point is world-space, the server uses the real origin.
			if (!gate_ok
				&& HACKS->local->velocity().length_2d() > 20.f
				&& predicted_eye_pos.valid()
				&& predicted_eye_pos.dist_to(local_anims->eye_pos) > 0.1f)
			{
				best_rage_player.restore.store(best_rage_player.player);
				LAGCOMP->set_record(best_rage_player.player, best_record, matrix_to_aim);
				auto predictive_bullet = penetration::simulate(HACKS->local, best_rage_player.player, predicted_eye_pos, best_point.aim_point);
				best_rage_player.restore.restore(best_rage_player.player);

				if (bullet_valid(predictive_bullet))
				{
					gate_ok = true;

					if (rage_log::enabled())
						rage_log::line("[FINAL] predictive tgt=%s(%d) dmg=%d hg=%d vel=%.0f",
							best_rage_player.player->get_name().c_str(), best_rage_player.player->index(),
							predictive_bullet.damage, predictive_bullet.hitgroup,
							HACKS->local->velocity().length_2d());
				}
			}

			if (gate_ok)
			{
				rage_players[best_rage_player.player->index()].shot_hold_ticks = 0;
			}
			else
			{
				// OLD CODE (kept for revert):
				// if (final_bullet.damage < damage
				//	|| HACKS->weapon->is_taser() && final_bullet.penetration_count < 4
				//	|| wrong_target || head_mismatch)
				if (rage_log::enabled())
					rage_log::line("[FINAL] reject tgt=%s(%d) dmg=%d hg=%d hitbox=%s reason=%s",
						best_rage_player.player->get_name().c_str(), best_rage_player.player->index(),
						final_bullet.damage, final_bullet.hitgroup,
						main_utils::hitbox_to_string(best_point.hitbox).c_str(),
						wrong_target ? "target" : head_mismatch ? "hitgroup" : "damage");

				// Bounded auto delay: wait at most auto_delay_max ms for a
				// validating origin, then commit instead of holding fire for
				// hundreds of ticks (the old behaviour that got you killed).
				++rage_players[best_rage_player.player->index()].shot_hold_ticks;
				const int max_hold = g_cfg.rage.auto_delay
					? std::max(1, TIME_TO_TICKS(std::clamp(g_cfg.rage.auto_delay_max, 20, 500) * 0.001f))
					: 0;

				if (max_hold > 0 && rage_players[best_rage_player.player->index()].shot_hold_ticks > max_hold)
				{
					if (rage_log::enabled())
						rage_log::line("[FINAL] force tgt=%s(%d) after %d ticks (auto delay)",
							best_rage_player.player->get_name().c_str(), best_rage_player.player->index(),
							rage_players[best_rage_player.player->index()].shot_hold_ticks);

					rage_players[best_rage_player.player->index()].shot_hold_ticks = 0;
					// Committed: fall through and fire with the best point.
				}
				// Wrong resolver side can make the point unreachable. Try the
				// other sides before dropping the shot so single-shot weapons
				// (scout) still fire and the resolver keeps getting feedback.
				else if (!try_alternate_sides(best_rage_player, best_point, best_record, ideal_start, damage, max_hitchance, out_chance))
				{
					// Corrected-origin recovery (purpose: head-only must FIRE).
					// The scan validated this point from the scan eye; the real
					// origin can flip the server hitgroup (head -> neck/chest).
					// Instead of aborting, re-test the already-scanned candidates
					// of the same hitbox from the real origin and adopt the first
					// one that still validates. This is what stops head-only from
					// silently refusing every shot on a boundary point.
					bool recovered = false;
					best_rage_player.restore.store(best_rage_player.player);

					for (auto& candidate : best_rage_player.points_to_scan)
					{
						if (candidate.hitbox != best_point.hitbox || !candidate.aim_point.valid() || !candidate.record)
							continue;

						auto candidate_matrix = get_aim_matrix(best_rage_player.player, candidate.record);
						LAGCOMP->set_record(best_rage_player.player, candidate.record, candidate_matrix);

						auto probe = penetration::simulate(HACKS->local, best_rage_player.player, ideal_start, candidate.aim_point);
						const bool hitgroup_ok = candidate.hitbox != HITBOX_HEAD || probe.hitgroup == HITGROUP_HEAD;

						if (probe.traced_target == best_rage_player.player && probe.damage >= damage && hitgroup_ok)
						{
							best_point = candidate;
							best_point.found = true;
							best_point.damage = probe.damage;
							best_point.record = candidate.record;
							best_record = candidate.record;

							best_rage_player.best_point = best_point;
							best_rage_player.best_record = best_record;
							recovered = true;
							break;
						}
					}

					best_rage_player.restore.restore(best_rage_player.player);

					if (!recovered)
					{
						if (rage_log::enabled())
							rage_log::line("[FINAL] abort tgt=%s(%d) no candidate validates from corrected origin (hb=%s)",
								best_rage_player.player->get_name().c_str(), best_rage_player.player->index(),
								main_utils::hitbox_to_string(best_point.hitbox).c_str());

						// OLD CODE (kept for revert):
						// debug_reason = XOR("final bullet dmg < min (all sides)");
						debug_reason = XOR("final bullet invalid (all sides + candidates)");
						return;
					}

					rage_players[best_rage_player.player->index()].shot_hold_ticks = 0;

					if (rage_log::enabled())
						rage_log::line("[FINAL] recovered tgt=%s(%d) hb=%s dmg=%d hg=%d bt=%d",
							best_rage_player.player->get_name().c_str(), best_rage_player.player->index(),
							main_utils::hitbox_to_string(best_point.hitbox).c_str(), best_point.damage,
							head_mismatch ? HITGROUP_HEAD : final_bullet.hitgroup,
							std::abs(TIME_TO_TICKS(best_rage_player.player->sim_time() - best_record->sim_time)));
				}
				else
				{
					// Alternate side succeeded: it committed the point/record.
					rage_players[best_rage_player.player->index()].shot_hold_ticks = 0;
				}
			}
		}

		if (!should_shot)
		{
			debug_reason = XOR("should_shot false");
			return;
		}

		if (!already_stooped || !(rage_config.quick_stop_options & early))
		{
			force_scope();

			// Never fire a sniper on the same tick the scope is requested: the
			// prediction/vars still see the unscoped weapon, so the first shot
			// would be an unscoped miss. Scope this tick, fire next tick.
			if (rage_config.auto_scope && HACKS->weapon && HACKS->weapon->is_sniper()
				&& HACKS->weapon->zoom_level() < 1)
			{
				debug_reason = XOR("scoping");
				return;
			}

			if (rage_config.quick_stop && should_stop(best_point))
				trigger_stop = true;
		}

		bool supress_doubletap_choke = true;
		if (EXPLOITS->enabled() && EXPLOITS->get_exploit_mode() == EXPLOITS_DT)
			supress_doubletap_choke = EXPLOITS->defensive.tickbase_choke > 2;

		if (!supress_doubletap_choke)
		{
			debug_reason = XOR("doubletap choke suppress");
			return;
		}

		if (!can_fire())
		{
			debug_reason = XOR("can_fire false");
			return;
		}

		if (!hitchance(ideal_start, best_rage_player, best_point, best_record, max_hitchance, nullptr, &out_chance))
		{
			debug_reason = tfm::format(CXOR("hitchance %.1f%% < %.1f%%"), out_chance * 100.f, max_hitchance * 100.f);
			return;
		}

		if (g_cfg.rage.auto_fire)
			HACKS->cmd->buttons.force(IN_ATTACK);

		if (HACKS->cmd->buttons.has(IN_ATTACK))
		{
			firing = true;
			debug_reason = tfm::format(CXOR("firing hc %.1f%%"), out_chance * 100.f);

			// Use the predicted time for extrapolated records: the aim point and
			// matrix were built at prediction.time, so rewinding to sim_time would
			// make the server lag-compensate to a pose we never aimed at (P0).
			const float record_time = best_record->extrapolated ? best_record->prediction.time : best_record->sim_time;

			HACKS->cmd->tickcount = TIME_TO_TICKS(record_time + LAGCOMP->get_interp_time());
			auto backtrack_ticks = std::abs(TIME_TO_TICKS(best_rage_player.player->sim_time() - record_time));

			if (g_cfg.visuals.chams[c_onshot].enable)
				CHAMS->add_shot_record(best_rage_player.player, get_aim_matrix(best_rage_player.player, best_record));

			// FIXED: Calculate the proper aim angles without AA interference
			// These angles will be set BEFORE anti-aim runs, so protect them
			vec3_t calculated_aim = math::calc_angle(ideal_start, best_point.aim_point).normalized_angle();
			calculated_aim -= HACKS->local->aim_punch_angle() * (HACKS->convars.weapon_recoil_scale->get_float());
			calculated_aim = calculated_aim.normalized_angle();

			// Set the command with correct aim angles
			HACKS->cmd->viewangles = calculated_aim;

			add_shot_record(best_rage_player.player, best_point, best_record, ideal_start);

			if (g_cfg.visuals.eventlog.logs & 4)
			{
				static auto log_str = ("Fire to %s, %s, %d%%, %dhp, %dt, %dsp%s");

				auto& resolver = resolver_info[best_rage_player.player->index()];
				auto resolver_suffix = resolver.resolved
					? tfm::format(
						", %s P %.1f, Y %.1f",
						get_resolver_direction_label(convert_side_to_direction(resolver.side)),
						clamp_pitch(best_record->eye_angles.x),
						clamp_yaw(best_record->eye_angles.y)
					)
					: std::string{};
				//const auto cheat_tag = get_detected_cheat_tag(resolver.detected_cheat);
				//if (cheat_tag[0])
					//resolver_suffix += tfm::format(", cheat %s", cheat_tag);

				std::string log = tfm::format(
					log_str,
					best_rage_player.player->get_name(),
					main_utils::hitbox_to_string(best_point.hitbox),
					(int)(out_chance * 100.f),
					best_point.damage,
					best_record->extrapolated ? -best_record->extrapolate_ticks : backtrack_ticks,
					best_point.safety,
					resolver_suffix
				);

				EVENT_LOGS->push_message(log, {}, true);
			}

#ifdef LEGACY
			if (!ANTI_AIM->is_fake_ducking())
			{
				if (g_cfg.binds[hs_b].toggled || g_cfg.binds[dt_b].toggled)
					*HACKS->send_packet = true;
				else
				{
					if (!HACKS->client_state->choked_commands)
						*HACKS->send_packet = false;
				}
			}
#else
			if ((g_cfg.binds[hs_b].toggled || !ANTI_AIM->is_fake_ducking()) && !*HACKS->send_packet)
				*HACKS->send_packet = true;
#endif
		}
	}

	if (!working)
		debug_reason = XOR("no scannable points for any player");

	best_rage_player.reset();
}

void c_ragebot::add_shot_record(c_cs_player* player, const rage_point_t& best, anim_record_t* record, vec3_t eye_pos)
{
	auto anims = ANIMFIX->get_local_anims();

	auto& new_shot = shots.emplace_back();
	new_shot.time = HACKS->predicted_time;
	new_shot.init_time = 0.f;
	new_shot.impact_fire = false;
	new_shot.fire = false;
	new_shot.damage = -1;
	new_shot.predicted_damage = best.damage;
	new_shot.safety = best.safety;
	new_shot.start = eye_pos;
	new_shot.hitgroup = -1;
	new_shot.hitchance = best.accuracy;
	new_shot.hitbox = best.hitbox;
	new_shot.pointer = player;
	new_shot.record = *record;
	new_shot.index = player->index();
	new_shot.resolver = resolver_info[new_shot.index];
	new_shot.resolver_tick = resolver_info[new_shot.index].anim_resolve_ticks;
	new_shot.fire_tick = HACKS->global_vars->tickcount;
	new_shot.point = best.aim_point;

	if (rage_log::enabled())
	{
		const auto side_label = get_resolver_direction_label(convert_side_to_direction(new_shot.resolver.side));
		const int backtrack_ticks = std::abs(TIME_TO_TICKS(player->sim_time() - record->sim_time));
		const float bracket = get_record_bracket_weight(record);
		const auto aim_angle = math::calc_angle(eye_pos, best.aim_point);
		const auto punch = HACKS->local->aim_punch_angle();

		char bl[6]{};
		bl[0] = new_shot.resolver.is_blacklisted(side_left) ? 'L' : '-';
		bl[1] = new_shot.resolver.is_blacklisted(side_right) ? 'R' : '-';
		bl[2] = new_shot.resolver.is_blacklisted(side_zero) ? 'Z' : '-';
		bl[3] = new_shot.resolver.is_blacklisted(side_left_extra) ? 'l' : '-';
		bl[4] = new_shot.resolver.is_blacklisted(side_right_extra) ? 'r' : '-';

		rage_log::line("[SHOT] tick=%d cmd=%d tgt=%s(%d) hp=%d hb=%s dmg=%d mindmg=%d safe=%d acc=%.1f%% hc_cfg=%d mp=%d pred_eye=%d",
			HACKS->global_vars->tickcount, HACKS->cmd->command_number, player->get_name().c_str(), new_shot.index, player->health(),
			main_utils::hitbox_to_string(best.hitbox).c_str(), best.damage, get_min_damage(player), best.safety,
			best.accuracy * 100.f, rage_config.hitchance, g_cfg.rage.multipoint_advanced ? 1 : 0, best.predicted_eye_pos ? 1 : 0);

		rage_log::line("[SHOT] aim=(%.1f %.1f %.1f) eye=(%.1f %.1f %.1f) ang pitch=%.1f yaw=%.1f punch=(%.2f %.2f %.2f)",
			best.aim_point.x, best.aim_point.y, best.aim_point.z, eye_pos.x, eye_pos.y, eye_pos.z,
			aim_angle.x, aim_angle.y, punch.x, punch.y, punch.z);

		rage_log::line("[SHOT] record sim=%.3f choke=%d extra=%d amt=%d bt=%d bracket=%.2f shift=%d break=%d duck=%.2f flags=0x%X vel=(%.0f %.0f %.0f) org=(%.0f %.0f %.0f)",
			record->sim_time, record->choke, record->extrapolated ? 1 : 0, record->extrapolate_ticks, backtrack_ticks, bracket,
			record->shifting ? 1 : 0, record->break_lc ? 1 : 0, record->duck_amt, (unsigned int)record->flags.bits,
			record->velocity.x, record->velocity.y, record->velocity.z,
			record->origin.x, record->origin.y, record->origin.z);

		rage_log::line("[SHOT] resolver side=%s mode=%s resolved=%d use_yaw=%d jitter=%d baim=%d misses=%d failed=%d/%d bl=[%s]",
			side_label, new_shot.resolver.mode.c_str(), new_shot.resolver.resolved ? 1 : 0, new_shot.resolver.use_resolved_yaw ? 1 : 0,
			new_shot.resolver.jitter.is_jitter ? 1 : 0, g_cfg.rage.baim_on_jitter ? 1 : 0,
			missed_shots[new_shot.index], new_shot.resolver.last_failed_side, new_shot.resolver.prev_failed_side, bl);

		rage_log::line("[SHOT] exploits mode=%d offset=%d hs_pad=%d dt_bullet=%d shifting=%d send=%d tickbase=%d",
			EXPLOITS->get_exploit_mode(), EXPLOITS->tickbase_offset(), EXPLOITS->hide_shot_commands, EXPLOITS->dt_bullet,
			EXPLOITS->is_shifting() ? 1 : 0, *HACKS->send_packet ? 1 : 0, HACKS->tickbase);
	}
}

void c_ragebot::weapon_fire(c_game_event* event)
{
	if (shots.empty())
		return;

	if (HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid"))) != HACKS->engine->get_local_player())
		return;

	auto& shot = shots.front();
	if (!shot.fire)
		shot.fire = true;
}

void c_ragebot::bullet_impact(c_game_event* event)
{
	if (shots.empty())
		return;

	auto& shot = shots.front();

	if (HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid"))) != HACKS->engine->get_local_player())
		return;

	const auto vec_impact = vec3_t{ event->get_float(CXOR("x")), event->get_float(CXOR("y")), event->get_float(CXOR("z")) };

	bool check = false;
	if (shot.impact_fire)
	{
		if (shot.start.dist_to(vec_impact) > shot.start.dist_to(shot.impact))
			check = true;
	}
	else
		check = true;

	if (!check)
		return;

	shot.impact_fire = true;
	shot.init_time = HACKS->predicted_time;
	shot.impact = vec_impact;

	if (rage_log::enabled())
	{
		const float dist_aim = shot.point.dist_to(vec_impact);
		const float travel = shot.start.dist_to(vec_impact);
		const float miss_angle = get_miss_angle_degrees(shot);
		const auto to_aim = (shot.point - shot.start).normalized();
		const auto to_imp = (vec_impact - shot.start).normalized();

		rage_log::line("[IMPACT] tick=%d tgt_idx=%d impact=(%.1f %.1f %.1f) aim=(%.1f %.1f %.1f) dist=%.1f travel=%.1f angle=%.3fdeg dot=%.4f",
			HACKS->global_vars->tickcount, shot.index, vec_impact.x, vec_impact.y, vec_impact.z,
			shot.point.x, shot.point.y, shot.point.z, dist_aim, travel, miss_angle, to_aim.dot(to_imp));
	}
}
void c_ragebot::player_hurt(c_game_event* event)
{
	if (HACKS->engine->get_player_for_user_id(event->get_int(CXOR("attacker"))) != HACKS->engine->get_local_player())
		return;

	if (!shots.empty())
	{
		auto& shot = shots.front();
		const int server_hg = event->get_int(CXOR("hitgroup"));
		const int server_dmg = event->get_int(CXOR("dmg_health"));

		// Head->body mismatch counter. Client aimed a head point, server scored
		// a body hitgroup -> stop trusting the head against this player until a
		// real head lands. This drives the baim fallback in choose_best_point
		// and the body-hitbox scan append in pre_cache_centers.
		if (shot.index > 0 && shot.index < 65)
		{
			if (shot.hitbox == HITBOX_HEAD && server_hg != HITGROUP_HEAD)
				m_head_body[shot.index] = std::min(m_head_body[shot.index] + 1, 10);
			else if (server_hg == HITGROUP_HEAD)
				m_head_body[shot.index] = 0;
		}

		if (rage_log::enabled())
		{
			const auto side_label = get_resolver_direction_label(convert_side_to_direction(shot.resolver.side));

			rage_log::line("[HURT] tick=%d tgt_idx=%d server hg=%s dmg=%d | client hb=%s pred_dmg=%d safe=%d acc=%.1f%% headbody=%d resolver=%s mode=%s side=%s",
				HACKS->global_vars->tickcount, shot.index,
				main_utils::hitgroup_to_string(server_hg).c_str(), server_dmg,
				main_utils::hitbox_to_string(shot.hitbox).c_str(), shot.predicted_damage, shot.safety,
				shot.hitchance * 100.f, m_head_body[shot.index],
				shot.resolver.resolved ? "res" : "unres",
				shot.resolver.mode.c_str(), side_label);
		}

		// Reset miss counter on confirmed hit. A head point that registered a
		// body hitgroup is learned against the side that was used; a real head
		// confirms the side.
		if (shot.index > 0 && shot.index < 65)
		{
			// OLD CODE (kept for revert):
			// resolver::on_hit(shot.index);
			if (shot.hitbox == HITBOX_HEAD && server_hg != HITGROUP_HEAD)
				resolver::on_side_error(shot.index, shot.resolver.side);
			else
				resolver::on_hit(shot.index, shot.resolver.side);

			missed_shots[shot.index] = 0;
		}
		shots.erase(shots.begin());
	}
}
void c_ragebot::round_start(c_game_event* event)
{
	for (auto& i : missed_shots)
		i = 0;

	for (auto& i : m_missed_anim_side)
		i = 0;

	for (auto& i : m_missed_prev_side)
		i = 0;

	for (auto& i : m_head_body)
		i = 0;

	shots.clear();
}

void c_ragebot::on_game_events(c_game_event* event)
{
	auto name = CONST_HASH(event->get_name());

	switch (name)
	{
	case HASH("weapon_fire"):
		weapon_fire(event);
		break;
	case HASH("bullet_impact"):
		bullet_impact(event);
		break;
	case HASH("player_hurt"):
		player_hurt(event);
		break;
	case HASH("round_start"):
		round_start(event);
		break;
	}
}

void c_ragebot::proceed_misses()
{
	if (shots.empty())
		return;

	auto& shot = shots.front();
	if (std::abs(HACKS->predicted_time - shot.time) > 1.f)
	{
		shots.erase(shots.begin());
		return;
	}

	if (shot.init_time != -1.f && shot.index && shot.damage == -1 && shot.fire && shot.impact_fire)
	{
		auto new_player = (c_cs_player*)HACKS->entity_list->get_client_entity(shot.index);
		if (new_player && new_player->is_player() && shot.pointer == new_player)
		{
			const auto studio_model = HACKS->model_info->get_studio_model(new_player->get_model());

			if (studio_model)
			{
				// Renamed to avoid shadowing the global resolver_info array
				auto& resolver_info = shot.resolver;
				const auto end = shot.impact;
				const auto miss_angle = get_miss_angle_degrees(shot);
				const auto miss_suffix = tfm::format(CXOR(" (%.2fdeg off)"), miss_angle);

				auto matrix_to_aim = get_aim_matrix(new_player, &shot.record, &shot.resolver);

				rage_player_t rage_player{};
				rage_player.player = new_player;
				rage_player.restore.store(new_player);

				LAGCOMP->set_record(new_player, &shot.record, matrix_to_aim);

				// Replay every resolver candidate against the actual aimed point
				// (Fatality get_brute_angle parity). Runs on every miss, not just
				// when logging:
				//  - used_side_hits: if the played side still hits client-side the
				//    miss was server-side (resolve/pose/origin correction) and the
				//    resolver must not churn.
				//  - best_alt_side: the candidate that DOES hit (preferring the
				//    right hitgroup) so a true resolver miss moves straight to it.
				// OLD CODE (kept for revert): it flipped whenever shot.safety > 0,
				// then stepped through the fixed ring.
				const int side_order[5] = { side_left, side_right, side_zero, side_left_extra, side_right_extra };
				bool side_hits[5]{};

				int used_index = 2;
				for (int si = 0; si < 5; ++si)
				{
					if (side_order[si] == resolver_info.side)
						used_index = si;
				}

				int best_alt_side = -999;
				int best_alt_score = -1;

				for (int si = 0; si < 5; ++si)
				{
					matrix3x4_t* mtx = nullptr;
					switch (side_order[si])
					{
					case side_left: mtx = shot.record.matrix_left.matrix; break;
					case side_right: mtx = shot.record.matrix_right.matrix; break;
					case side_zero: mtx = shot.record.matrix_zero.matrix; break;
					case side_left_extra: mtx = shot.record.matrix_left_extra.matrix; break;
					case side_right_extra: mtx = shot.record.matrix_right_extra.matrix; break;
					default: mtx = shot.record.matrix_orig.matrix; break;
					}

					restore_record_t backup{};
					backup.store(new_player);
					LAGCOMP->set_record(new_player, &shot.record, mtx);

					const auto probe = penetration::simulate(HACKS->local, new_player, shot.start, shot.point);
					const bool geom = can_hit_hitbox(shot.start, end, &rage_player, shot.hitbox, mtx, &shot.record);

					backup.restore(new_player);

					side_hits[si] = geom && probe.traced_target == new_player && probe.damage > 0;

					if (side_order[si] == resolver_info.side || !side_hits[si])
						continue;

					const bool hitgroup_ok = shot.hitbox != HITBOX_HEAD || probe.hitgroup == HITGROUP_HEAD;
					const int score = (hitgroup_ok ? 1000 : 0) + probe.damage;

					if (score > best_alt_score)
					{
						best_alt_score = score;
						best_alt_side = side_order[si];
					}
				}

				const bool used_side_hits = side_hits[used_index];

				const char* classification = "unknown";

				if (!can_hit_hitbox(shot.start, end, &rage_player, shot.hitbox, matrix_to_aim, &shot.record))
				{
					float dist = shot.start.dist_to(shot.impact);
					float dist2 = shot.start.dist_to(shot.point);

					if (dist2 > dist)
					{
						classification = "wall";
						EVENT_LOGS->push_message(tfm::format(CXOR("Miss due wall%s"), miss_suffix));
					}
					else
					{
						classification = "spread";
						EVENT_LOGS->push_message(tfm::format(CXOR("Miss due spread%s"), miss_suffix));
					}
				}
				else
				{
					if (new_player->is_alive())
					{
						if (shot.record.extrapolated)
						{
							classification = "extrapolation";
							EVENT_LOGS->push_message(tfm::format(CXOR("Miss due extrapolation%s"), miss_suffix));
						}
						else if (shot.safety > 0)
						{
							// OLD CODE (kept for revert):
							// if (resolver_info.resolved) { ... } else { ... }
							// flipped on safety>0 alone, even when the used side
							// had hit client-side.
							//
							// Fatality parity: used-side-hit = server-side miss,
							// do not flip the resolver.
							if (used_side_hits)
							{
								classification = "server";
								EVENT_LOGS->push_message(tfm::format(CXOR("Miss due server/pose%s"), miss_suffix));
							}
							else if (resolver_info.resolved)
							{
								missed_shots[shot.index]++;
								m_missed_prev_side[shot.index] = m_missed_anim_side[shot.index];
								m_missed_anim_side[shot.index] = resolver_info.side;
								// Move to a candidate that actually hits the aimed
								// point when the replay found one; otherwise fall
								// back to the furthest-side brute.
								// OLD CODE (kept for revert):
								// resolver::on_miss(shot.index, resolver_info.side);
								if (best_alt_side != -999)
									resolver::on_miss_choose(shot.index, resolver_info.side, best_alt_side);
								else
									resolver::on_miss(shot.index, resolver_info.side);
								classification = "resolver";
								EVENT_LOGS->push_message(tfm::format(CXOR("Miss due resolver%s"), miss_suffix));
							}
							else
							{
								missed_shots[shot.index]++;
								// OLD CODE (kept for revert):
								// resolver::on_miss(shot.index, resolver_info.side);
								if (best_alt_side != -999)
									resolver::on_miss_choose(shot.index, resolver_info.side, best_alt_side);
								else
									resolver::on_miss(shot.index, resolver_info.side);
								classification = "shit-resolver";
								EVENT_LOGS->push_message(tfm::format(CXOR("Miss due shit resolver%s"), miss_suffix));
							}
						}
						else
						{
							classification = "spread";
							EVENT_LOGS->push_message(tfm::format(CXOR("Miss due spread%s"), miss_suffix));
						}
					}
					else
					{
						classification = "death";
						EVENT_LOGS->push_message(tfm::format(CXOR("Miss due death%s"), miss_suffix));
					}
				}

				// Detailed rage log: replay every resolver candidate against the
				// shot record and against the live newest record. This is the
				// data needed to tell a wrong side from a wrong backtrack record.
				if (rage_log::enabled())
				{
					const int side_list[5] = { side_left, side_right, side_zero, side_left_extra, side_right_extra };

					auto replay_side = [&](int side, anim_record_t* rec) -> std::string
						{
							matrix3x4_t* mtx = nullptr;
							switch (side)
							{
							case side_left: mtx = rec->matrix_left.matrix; break;
							case side_right: mtx = rec->matrix_right.matrix; break;
							case side_zero: mtx = rec->matrix_zero.matrix; break;
							case side_left_extra: mtx = rec->matrix_left_extra.matrix; break;
							case side_right_extra: mtx = rec->matrix_right_extra.matrix; break;
							default: mtx = rec->matrix_orig.matrix; break;
							}

							// Local backup so the outer restore snapshot is not
							// overwritten by these diagnostic state changes.
							restore_record_t backup{};
							backup.store(new_player);
							LAGCOMP->set_record(new_player, rec, mtx);

							auto bullet = penetration::simulate(HACKS->local, new_player, shot.start, shot.point);
							const bool geom = can_hit_hitbox(shot.start, end, &rage_player, shot.hitbox, mtx, rec);

							backup.restore(new_player);

							return tfm::format(CXOR("%s:%s d%d h%d"),
								get_resolver_direction_label(convert_side_to_direction(side)),
								geom ? "HIT" : "no", bullet.damage, bullet.hitgroup);
						};

					std::string sides;
					for (const int side : side_list)
					{
						sides += replay_side(side, &shot.record);
						sides += ' ';
					}

					rage_log::line("[MISS] tick=%d tgt=%s(%d) class=%s angle=%.2fdeg hb=%s pt=(%.0f %.0f %.0f) imp=(%.0f %.0f %.0f)",
						HACKS->global_vars->tickcount, new_player->get_name().c_str(), shot.index, classification, miss_angle,
						main_utils::hitbox_to_string(shot.hitbox).c_str(),
						shot.point.x, shot.point.y, shot.point.z, shot.impact.x, shot.impact.y, shot.impact.z);

					rage_log::line("[MISS] record sim=%.3f choke=%d extra=%d bt=%d bracket=%.2f shift=%d break=%d duck=%.2f vel=(%.0f %.0f %.0f)",
						shot.record.sim_time, shot.record.choke, shot.record.extrapolated ? 1 : 0,
						std::abs(TIME_TO_TICKS(new_player->sim_time() - shot.record.sim_time)),
						get_record_bracket_weight(&shot.record), shot.record.shifting ? 1 : 0, shot.record.break_lc ? 1 : 0,
						shot.record.duck_amt, shot.record.velocity.x, shot.record.velocity.y, shot.record.velocity.z);

					rage_log::line("[MISS] client side=%s mode=%s side=%d misses=%d | replay[%s]",
						get_resolver_direction_label(convert_side_to_direction(resolver_info.side)),
						resolver_info.mode.c_str(), resolver_info.side, missed_shots[shot.index], sides.c_str());

					auto lag_anims = ANIMFIX->get_anims(shot.index);
					if (lag_anims && !lag_anims->records.empty())
					{
						auto& newest = lag_anims->records.front();
						const auto newest_replay = replay_side(resolver_info.side, &newest);
						rage_log::line("[MISS] newest sim=%.3f delta_ticks=%d replay[%s]",
							newest.sim_time, std::abs(TIME_TO_TICKS(newest.sim_time - shot.record.sim_time)), newest_replay.c_str());
					}

					// Resolver snapshot at fire time: exactly what the resolver
					// decided, so a miss can be attributed to a mode/side/state.
					rage_log::line("[MISS] rsv side=%d mode=%s resolved=%d use_yaw=%d rsv_eye=%.1f rtick=%d lag=%d err=%d/%d/%d/%d/%d bl=%d%d%d%d%d",
						resolver_info.side, resolver_info.mode.c_str(), resolver_info.resolved ? 1 : 0,
						resolver_info.use_resolved_yaw ? 1 : 0, resolver_info.resolved_eye_yaw,
						shot.resolver_tick, shot.fire_tick - shot.resolver_tick,
						resolver_info.side_errors[0], resolver_info.side_errors[1], resolver_info.side_errors[2],
						resolver_info.side_errors[3], resolver_info.side_errors[4],
						resolver_info.side_blacklist[0] ? 1 : 0, resolver_info.side_blacklist[1] ? 1 : 0,
						resolver_info.side_blacklist[2] ? 1 : 0, resolver_info.side_blacklist[3] ? 1 : 0,
						resolver_info.side_blacklist[4] ? 1 : 0);

					// Jitter/spin detector internals: what the enemy anti-aim was
					// doing when the shot was taken.
					rage_log::line("[MISS] jit=%d jt=%d st=%d n=%d d=%.1f spin=%d rate=%.1f streak=%d pred=%.1f",
						resolver_info.jitter.is_jitter ? 1 : 0, resolver_info.jitter.jitter_ticks,
						resolver_info.jitter.static_ticks, resolver_info.jitter.samples,
						resolver_info.jitter.last_delta, resolver_info.jitter.is_spin ? 1 : 0,
						resolver_info.jitter.spin_rate, resolver_info.jitter.spin_streak,
						resolver_info.jitter.spin_predicted);

					// Enemy body state at the fired record.
					rage_log::line("[MISS] enemy lby=%.1f eye=%.1f lbyD=%.1f vel=%.0f choke=%d duck=%.2f ground=%d",
						shot.record.lby, shot.record.eye_angles.y,
						math::normalize_yaw(shot.record.lby - shot.record.eye_angles.y),
						shot.record.velocity.length_2d(), shot.record.choke, shot.record.duck_amt,
						shot.record.on_ground ? 1 : 0);
				}

				rage_player.restore.restore(new_player);
			}
		}

		shots.erase(shots.begin());
	}
}