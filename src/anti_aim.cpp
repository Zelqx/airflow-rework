#include "globals.hpp"
#include "anti_aim.hpp"
#include "movement.hpp"
#include "game_movement.hpp"
#include "fake_lag.hpp"
#include "penetration.hpp"
#include "engine_prediction.hpp"
#include "animations.hpp"
#include "entlistener.hpp"
#include "esp.hpp"
#include "lagcomp.hpp"
#include "ragebot.hpp"
#include "rage_logger.hpp"
#include "exploits.hpp"
#include "cmd_shift.hpp"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
INLINE float approach(float current, float target, float speed)
{
	float delta = target - current;

	if (std::fabs(delta) <= speed)
		return target;

	return current + (delta > 0.f ? speed : -speed);
}
bool can_fake_duck()
{
	return g_cfg.binds[fd_b].toggled && (MOVEMENT->on_ground() && !(HACKS->cmd->buttons.has(IN_JUMP)));
}

// ============================================================================
// STATE DETECTION AND HELPERS
// ============================================================================

/// Detect the current player movement state with proper priority:
/// Defensive > In Air > Crouch > Running > Moving > Standing
aa_state_t c_anti_aim::detect_player_state()
{
	if (!HACKS || !HACKS->local)
		return AA_STATE_UNKNOWN;

	if (!MOVEMENT)
		return AA_STATE_UNKNOWN;

	// Check if on ground
	const bool on_ground = MOVEMENT->on_ground();
	const bool is_crouch = HACKS->local->flags().has(FL_DUCKING) && !is_fake_ducking();

	// FIXED: Track air time with counter (prevents false positives from single tick)
	if (!on_ground)
		air_ticks++;
	else
		air_ticks = 0;

	// Get velocity
	auto velocity = HACKS->local->velocity();
	float speed = velocity.length_2d();

	// Detect if using slowwalk
	bool is_slowwalk = g_cfg.binds[sw_b].toggled;

	// FIXED: Only consider "in air" if airborne for minimum ticks
	// This prevents AA from triggering on momentary airtime (uneven terrain, etc)
	if (air_ticks >= g_cfg.antihit.def_aa_air_minimum_ticks)
	{
		if (is_crouch)
			return AA_STATE_IN_AIR_CROUCH;
		return AA_STATE_IN_AIR;
	}

	if (is_crouch)
		return AA_STATE_CROUCH;

	// Check movement speed thresholds
	if (is_slowwalk)
		return AA_STATE_SLOW_WALK;
	else if (speed >= 180.0f)
		return AA_STATE_RUNNING;
	else if (speed > 5.0f)
		return AA_STATE_MOVING;
	else
		return AA_STATE_STANDING;
}
static active_aa_t build_defensive_aa_settings(const configs_t::anti_hit_t& cfg)
{
	active_aa_t out{};
	out.custom_yaw = cfg.custom_yaw;
	out.desync_enabled = true;
	out.pitch = cfg.pitch;
	out.custom_pitch = cfg.custom_pitch;
	out.random_pitch = cfg.random_pitch;
	out.random_pitch2 = cfg.random_pitch2;
	out.switch_pitch = cfg.switch_pitch;
	out.switch_pitch2 = cfg.switch_pitch2;
	out.way_pitch = cfg.way_pitch;
	out.way_pitch2 = cfg.way_pitch2;
	out.yaw = cfg.yaw;
	out.yaw_add = cfg.yaw_add;
	out.jitter_mode = cfg.jitter_mode;
	out.jitter_range = cfg.jitter_range;
	out.desync_left = cfg.desync_left;
	out.desync_right = cfg.desync_right;
	out.desync_enabled = cfg.desync;
	out.spin_speed = cfg.spin_speed;  // FIXED: moved before return
	out.spin_clamp = cfg.spin_clamp;   // FIXED: moved before return
	return out;
}

static active_aa_t build_air_aa_settings(const configs_t::anti_hit_t& cfg)
{
	active_aa_t out{};
	out.custom_yaw = cfg.custom_yaw;
	out.desync_enabled = true;
	out.pitch = cfg.air_pitch;
	out.custom_pitch = cfg.custom_pitch;
	out.random_pitch = cfg.random_pitch;
	out.random_pitch2 = cfg.random_pitch2;
	out.switch_pitch = cfg.switch_pitch;
	out.switch_pitch2 = cfg.switch_pitch2;
	out.way_pitch = cfg.way_pitch;
	out.way_pitch2 = cfg.way_pitch2;
	out.yaw = cfg.air_yaw;
	out.yaw_add = cfg.air_yaw_add;
	out.jitter_mode = cfg.air_jitter_mode;
	out.jitter_range = cfg.air_jitter_range;
	out.desync_left = cfg.air_desync_left;
	out.desync_right = cfg.air_desync_right;
	out.desync_enabled = cfg.desync;
	out.spin_speed = cfg.spin_speed;   // moved up
	out.spin_clamp = cfg.spin_clamp;   // moved up
	return out;
}

//static active_aa_t build_ground_aa_settings(const configs_t::anti_hit_t& cfg, aa_state_t current_state)
//{
//	active_aa_t out{};
//	out.desync_enabled = true;
//	out.pitch = cfg.pitch;
//	out.custom_pitch = cfg.custom_pitch;
//	out.random_pitch = cfg.random_pitch;
//	out.random_pitch2 = cfg.random_pitch2;
//	out.switch_pitch = cfg.switch_pitch;
//	out.switch_pitch2 = cfg.switch_pitch2;
//	out.way_pitch = cfg.way_pitch;
//	out.way_pitch2 = cfg.way_pitch2;
//	out.yaw = cfg.yaw;
//	out.custom_yaw = cfg.custom_yaw;
//	out.yaw_add = cfg.yaw_add;
//	out.jitter_mode = cfg.jitter_mode;
//	out.jitter_range = cfg.jitter_range;
//	out.desync_left = cfg.desync_left;
//	out.desync_right = cfg.desync_right;
//	out.desync_enabled = cfg.desync;
//	out.spin_speed = cfg.spin_speed;
//	out.spin_clamp = cfg.spin_clamp;
//}

static active_aa_t compute_active_aa_settings(const c_anti_aim* self, bool on_ground, const vec3_t& local_velocity, bool defensive_aa, aa_state_t current_state)
{
	active_aa_t out{};
	if (defensive_aa)
		out = build_defensive_aa_settings(g_cfg.antihit);
	else if (g_cfg.antihit.air_override && !on_ground)
		out = build_air_aa_settings(g_cfg.antihit);
	else
		out = build_defensive_aa_settings(g_cfg.antihit);  // Use main AA settings for ground

	// Clamp values (existing code)
	out.jitter_mode = std::clamp(out.jitter_mode, 0, 6);
	out.custom_yaw = std::clamp(out.custom_yaw, -180, 180);
	out.yaw_add = std::clamp(out.yaw_add, -180, 180);
	out.jitter_range = std::clamp(out.jitter_range, -180, 180);
	out.spin_speed = std::clamp(out.spin_speed, 1, 60);
	out.spin_clamp = g_cfg.antihit.spin_clamp;
	out.yaw = std::clamp(out.yaw, 0, 5);
	if (g_cfg.antihit.adaptive_jitter && out.jitter_range != 0) {
		float speed_factor = std::clamp(local_velocity.length_2d() / 260.f, 0.f, 1.f);
		float scale = static_cast<float>(std::clamp(g_cfg.antihit.adaptive_jitter_scale, 0, 100)) / 100.f;
		out.jitter_range = std::clamp(static_cast<int>(out.jitter_range * (1.f + speed_factor * scale)), -60, 60);
	}

	return out;
}

static unsigned int build_defensive_conditions(const configs_t::anti_hit_t& cfg)
{
	unsigned int conditions = cfg.def_aa_conditions & def_aa_cond_all;

	if (cfg.def_aa_dt_active)
	{
		switch (std::clamp(cfg.def_aa_dt_mode, 0, 4))
		{
		case 0: conditions |= def_aa_cond_on_shot; break;
		case 1: conditions |= def_aa_cond_peek; break;
		case 2: conditions |= def_aa_cond_in_air; break;
		case 3: conditions |= def_aa_cond_always; break;
		case 4: conditions |= def_aa_cond_in_crouch; break;
		default: break;
		}
	}

	return conditions;
}

static const configs_t::anti_hit_t::def_aa_phase_t& get_defensive_phase(const configs_t::anti_hit_t& cfg, int tick, int& phase_tick)
{
	const int phase_count = std::clamp(cfg.def_aa_phase_count, 1, configs_t::anti_hit_t::DEF_AA_MAX_PHASES);
	int total_ticks = 0;

	for (int i = 0; i < phase_count; ++i)
		total_ticks += std::max(cfg.def_aa_phases[i].duration, 1);

	int cycle_tick = total_ticks > 0 ? tick % total_ticks : 0;
	for (int i = 0; i < phase_count; ++i)
	{
		const int duration = std::max(cfg.def_aa_phases[i].duration, 1);
		if (cycle_tick < duration)
		{
			phase_tick = cycle_tick;
			return cfg.def_aa_phases[i];
		}
		cycle_tick -= duration;
	}

	phase_tick = 0;
	return cfg.def_aa_phases[0];
}

static float apply_defensive_phase_yaw(const configs_t::anti_hit_t::def_aa_phase_t& phase, float base_yaw, int tick, int phase_tick, int fake_side)
{
	const float amount = static_cast<float>(std::clamp(phase.yaw_angle, -180, 180));
	const float range = std::fabs(amount);
	const float factor = static_cast<float>(std::clamp(g_cfg.antihit.def_aa_random_factor, 1, 10));

	switch (std::clamp(phase.yaw_mode, 0, 9))
	{
	case 0: // Jitter (aggressive)
		return math::normalize_yaw(base_yaw + ((phase_tick & 1) ? -range * factor : range * factor));
	case 1: // Random
		math::random_seed(HACKS->global_vars->tickcount + HACKS->cmd->command_number + phase_tick);
		return math::normalize_yaw(base_yaw + math::random_float(-range * factor, range * factor));
	case 2: // Spin
	{
		// Correct degrees per tick: 360 * rpm / 60 / tickrate
		float degrees_per_tick = 360.f * static_cast<float>(phase.yaw_rpm) / 60.f / HACKS->tick_rate;
		float dir = phase.spin_clockwise ? 1.f : -1.f;
		if (phase.spin_inverter_mode != 0)
		{
			// Placeholder inverter – flip direction every 64 ticks (or hook real events)
			if ((HACKS->global_vars->tickcount / 64) % 2 == 0)
				dir = -dir;
		}
		return math::normalize_yaw(base_yaw + dir * static_cast<float>(tick) * degrees_per_tick);
	}
	case 3: // Backwards
		return math::normalize_yaw(base_yaw + 180.f);
	case 4: // Fake side (static)
		return math::normalize_yaw(base_yaw + amount * factor);
	case 5: // Side switch
		return math::normalize_yaw(base_yaw + static_cast<float>(fake_side) * amount * factor);
	case 6: // Alternating sides (switches between left and right each phase)
		return math::normalize_yaw(base_yaw + ((phase_tick / std::max(1, static_cast<int>(factor))) & 1 ? range : -range));
	case 7: // Lean with desync (biased towards fake side)
		return math::normalize_yaw(base_yaw + static_cast<float>(fake_side) * range * 0.75f);
	case 8: // Three-way
		switch (phase_tick % 3)
		{
		case 0: return math::normalize_yaw(base_yaw - range);
		case 1: return math::normalize_yaw(base_yaw);
		default: return math::normalize_yaw(base_yaw + range);
		}
	case 9: // Stepped
	{
		static constexpr float steps[] = { -1.f, -0.5f, 0.f, 0.5f, 1.f };
		return math::normalize_yaw(base_yaw + range * steps[phase_tick % 5]);
	}
	default:
		return math::normalize_yaw(base_yaw);
	}
}

static float apply_defensive_phase_pitch(const configs_t::anti_hit_t::def_aa_phase_t& phase, int phase_tick)
{
	const float amount = static_cast<float>(std::clamp(phase.pitch_angle, -89, 89));
	const float range = std::fabs(amount);
	const float amount2 = static_cast<float>(std::clamp(phase.pitch_angle2, -89, 89));
	const float range2 = std::fabs(amount2);
	const float factor = static_cast<float>(std::clamp(g_cfg.antihit.def_aa_random_factor, 1, 10));

	switch (std::clamp(phase.pitch_mode, 0, 8))
	{
	case 0: // Static
		return amount;
	case 1: // Random
		math::random_seed(HACKS->global_vars->tickcount + HACKS->cmd->command_number + phase_tick);
		return math::random_float(-range * factor, range * factor);
	case 2: // Up
		return -89.f;
	case 3: // Down
		return 89.f;
	case 4: // Switch
		return (phase_tick & 1) ? -range * factor : range2 * factor;
	case 5: // 3-way
		switch (phase_tick % 3)
		{
		case 0: return -range * factor;
		case 1: return 0.f;
		default: return range2 * factor;
		}
	case 6: // Jitter between the configured pitch values
		return (phase_tick & 1) ? amount2 : amount;
	case 7: // Smooth pitch sway
	{
		static constexpr float steps[] = { 0.f, 0.25f, 0.5f, 0.75f, 1.f, 0.75f, 0.5f, 0.25f };
		return amount + (amount2 - amount) * steps[phase_tick % 8];
	}
	case 8: // Inverted static
		return -amount;
	default:
		return -89.f;
	}
}
void c_anti_aim::apply_pitch_angle(const active_aa_t& active)
{
	// Check if manual yaw override is active - manual yaw shouldn't affect pitch
	bool manual_yaw_override = g_cfg.binds[left_b].toggled || g_cfg.binds[right_b].toggled || g_cfg.binds[back_b].toggled;

	if (defensive_aa && g_cfg.antihit.def_aa_enable && g_cfg.antihit.def_pitch && !manual_yaw_override)
		return;

	if (RAGEBOT && RAGEBOT->firing)
		return;

	update_peek_pitch_mode();
	bool applied_peek_pitch = peek_pitch_active && !defensive_aa;

	if (applied_peek_pitch)
	{
		HACKS->cmd->viewangles.x = peek_current_pitch;
	}
	else
	{
		// Default to 0 (neutral) before applying mode
		HACKS->cmd->viewangles.x = 0.f;

		switch (active.pitch)
		{
		case 0:
			// Disabled - stays at 0
			break;
		case 1:
			// Down
			HACKS->cmd->viewangles.x = 89.f;
			break;
		case 2:
			// Up
			HACKS->cmd->viewangles.x = -89.f;
			break;
		case 3:
			// Custom pitch
			HACKS->cmd->viewangles.x = std::clamp((float)active.custom_pitch, -89.f, 89.f);
			break;
		case 4:
		{
			// Random pitch
			math::random_seed(HACKS->global_vars->tickcount + HACKS->cmd->command_number);
			float min_pitch = std::min((float)active.random_pitch, (float)active.random_pitch2);
			float max_pitch = std::max((float)active.random_pitch, (float)active.random_pitch2);
			HACKS->cmd->viewangles.x = math::random_float(min_pitch, max_pitch);
			break;
		}
		case 5:
		{
			// Switch between two pitches
			if ((HACKS->global_vars->tickcount + HACKS->cmd->command_number) % 2 == 0)
				HACKS->cmd->viewangles.x = std::clamp((float)active.switch_pitch, -89.f, 89.f);
			else
				HACKS->cmd->viewangles.x = std::clamp((float)active.switch_pitch2, -89.f, 89.f);
			break;
		}
		case 6:
		{
			// 3-way pitch
			int idx = (HACKS->global_vars->tickcount + HACKS->cmd->command_number) % 3;
			if (idx == 0)
				HACKS->cmd->viewangles.x = std::clamp((float)active.way_pitch, -89.f, 89.f);
			else if (idx == 1)
				HACKS->cmd->viewangles.x = std::clamp((float)active.way_pitch2, -89.f, 89.f);
			else
				HACKS->cmd->viewangles.x = 89.f;
			break;
		}
		default:
			HACKS->cmd->viewangles.x = -89.f;
			break;
		}
	}

	// FIXED: Allow landing pitch to work in defensive mode
	// Landing pitch should apply regardless of defensive AA state
	if (g_cfg.antihit.landing_pitch && landing_pitch_ticks > 0)
	{
		// Only skip if peek pitch was applied
		if (!applied_peek_pitch)
			HACKS->cmd->viewangles.x = std::clamp((float)g_cfg.antihit.landing_pitch_value, -89.f, 89.f);
		--landing_pitch_ticks;
	}
}
void c_anti_aim::apply_manual_yaw()
{
	const int offset = g_cfg.antihit.manual_offset;

	if (g_cfg.binds[left_b].toggled)
		best_yaw += 90 + offset;
	if (g_cfg.binds[right_b].toggled)
		best_yaw -= 90 + offset;
	if (g_cfg.binds[back_b].toggled)
		best_yaw += 180 + offset;

	// Manual jitter (only while manual bind active)
	if (g_cfg.antihit.manual_jitter > 0)
	{
		math::random_seed(HACKS->global_vars->tickcount + HACKS->cmd->command_number);
		best_yaw += math::random_float(-(float)g_cfg.antihit.manual_jitter, (float)g_cfg.antihit.manual_jitter);
	}
}
void c_anti_aim::update_fake_side()
{
	// fake_side must be valid for EVERY yaw path (normal, freestanding, edge,
	// manual, defensive). Previously it was only assigned in apply_normal_yaw,
	// so freestanding/defensive ran with a stale or zero side -> no fake body
	// and the model appeared to face the wrong way.
	auto dsy_flipper = g_cfg.antihit.random_dsy ? random_dsy_flipper : flip_side;
	if (g_cfg.antihit.desync_mode)
		fake_side = dsy_flipper ? 1 : -1;
	else
		fake_side = g_cfg.binds[inv_b].toggled ? -1 : 1;
}

void c_anti_aim::apply_normal_yaw(const active_aa_t& active)
{
	// Reset to real view yaw first
	best_yaw = start_yaw;

	static int tick = 0, delay = 0, delayjitter = 0;

	// --- At targets (separate toggle) ---
	// When active, it fully determines the base facing (face away from the target
	// by at_targets_offset). The directional yaw modes below must not override it,
	// otherwise "backwards" would add another 180 and face the target instead.
	const bool at_targets_active = at_targets();

	// --- Apply selected yaw mode on top of best_yaw ---
	if (!at_targets_active)
	{
		switch (active.yaw)
		{
		case 0: // Disabled
			break;
		case 1: // Backwards
			best_yaw += 180.f;
			break;
		case 2: // Side snap
			best_yaw = math::normalize_yaw(best_yaw + (flip_jitter ? static_cast<float>(active.custom_yaw) : -static_cast<float>(active.custom_yaw)));
			break;
		case 3: // Random
			math::random_seed(HACKS->global_vars->tickcount + HACKS->cmd->command_number);
			best_yaw = math::random_float(-180.f, 180.f);
			break;
		case 4: // Custom
			best_yaw = math::normalize_yaw(best_yaw + static_cast<float>(active.custom_yaw));
			break;
		case 5: // Spin
		{
			float speed = static_cast<float>(active.spin_speed);
			float base = best_yaw;
			if (active.spin_clamp)
			{
				float period = 180.f / std::max(speed, 1.f);
				int t = HACKS->global_vars->tickcount % (2 * static_cast<int>(period));
				float offset;
				if (t < period)
					offset = (t / period) * 180.f - 90.f;
				else
					offset = 180.f - ((t - period) / period) * 180.f - 90.f;
				best_yaw = math::normalize_yaw(base + offset);
			}
			else
				best_yaw = math::normalize_yaw(base + speed * static_cast<float>(tick));
			break;
		}
		default: break;
		}
	}

	// --- Velocity bias (now also applies when at targets is active) ---
	if (g_cfg.antihit.velocity_yaw_bias)
	{
		float speed = HACKS->local->velocity().length_2d();
		if (speed > 15.f)
		{
			float move_yaw = RAD2DEG(std::atan2f(HACKS->local->velocity().y, HACKS->local->velocity().x));
			float bias = (float)std::clamp(g_cfg.antihit.velocity_yaw_bias_amount, 0, 100) / 100.f;
			float speed_factor = std::clamp(speed / 260.f, 0.f, 1.f);
			float delta = std::clamp(math::normalize_yaw(move_yaw - best_yaw), -90.f, 90.f);
			best_yaw += delta * bias * speed_factor;
		}
	}

	// --- Jitter (unchanged) ---
	float jitter_target = 0.f;
	auto jitter_flipper = g_cfg.antihit.random_jitter ? random_flipper : flip_jitter;
	int active_jitter_mode = active.jitter_mode;
	int active_jitter_range = active.jitter_range;

	switch (active_jitter_mode)
	{
	case 1: jitter_target = jitter_flipper ? active_jitter_range : -active_jitter_range; break;
	case 2: jitter_target = jitter_flipper ? 0.f : active_jitter_range; break;
	case 3: jitter_target = math::random_float(-active_jitter_range, active_jitter_range); break;
	case 4: jitter_target = (tick == 0) ? -active_jitter_range : (tick == 2) ? active_jitter_range : 0.f; break;
	case 5: { static float vals[5] = { -1.f, -0.5f, 0.f, 0.5f, 1.f }; jitter_target = vals[delay] * active_jitter_range; break; }
	case 6: jitter_target = (delayjitter < 3) ? -active_jitter_range : active_jitter_range; break;
	default: break;
	}

	jitter_current = approach(jitter_current, jitter_target, 10.f);
	best_yaw += jitter_current;

	// --- Flick (unchanged) ---
	if (g_cfg.antihit.flick_enable && g_cfg.binds[flick_b].toggled)
	{
		const int interval = std::max(g_cfg.antihit.flick_interval, 1);
		const int trigger_tick = std::clamp(g_cfg.antihit.flick_duration, 0, interval - 1);
		const bool flick_now = HACKS->global_vars->tickcount % interval == trigger_tick;

		if (flick_now)
		{
			const float flick_yaw = (float)std::clamp(g_cfg.antihit.flick_yaw, -60, 60);
			best_yaw = math::normalize_yaw(best_yaw + flick_yaw);
		}
	}

	if (*HACKS->send_packet)
	{
		++tick; tick %= 3;
		++delay; delay %= 5;
		++delayjitter; delayjitter %= 6;
	}
}
void c_anti_aim::apply_defensive_yaw(const active_aa_t& active)
{
	best_yaw = start_yaw;

	static int defensive_tick = 0;
	static bool was_defensive = false;  // FIXED: Track previous state
	int phase_tick = 0;

	// FIXED: Reset tick counter when defensive state changes
	if (!was_defensive && defensive_aa)
	{
		defensive_tick = 0;  // Entering defensive mode
	}
	else if (was_defensive && !defensive_aa)
	{
		defensive_tick = 0;  // Exiting defensive mode
	}

	was_defensive = defensive_aa;  // Update state for next frame

	// Get the current defensive phase based on tick counter
	auto& phase = get_defensive_phase(g_cfg.antihit, defensive_tick, phase_tick);

	// Calculate phase progress for smooth transitions
	float phase_progress = (float)phase_tick / (float)phase.duration;
	phase_progress = std::clamp(phase_progress, 0.f, 1.f);

	// Apply yaw based on phase yaw_mode
	switch (phase.yaw_mode)
	{
	case 0:  // Jitter
	{
		if (flip_jitter)
			best_yaw += phase.yaw_angle;
		else
			best_yaw -= phase.yaw_angle;
		break;
	}
	case 1:  // Random
	{
		math::random_seed(HACKS->global_vars->tickcount + defensive_tick);
		best_yaw += math::random_float(-phase.yaw_angle, phase.yaw_angle);
		break;
	}
	case 2:  // Spin
	{
		float rpm = phase.yaw_rpm;
		float degrees_per_tick = (rpm / 60.f) / 64.f;  // 64 ticks per second
		if (phase.spin_clockwise)
			best_yaw += degrees_per_tick * phase_tick;
		else
			best_yaw -= degrees_per_tick * phase_tick;
		break;
	}
	case 3:  // Backwards
	{
		best_yaw += 180.f;
		break;
	}
	case 4:  // Fake side (side snap)
	{
		best_yaw += phase.yaw_angle * (fake_side == 1 ? 1 : -1);
		break;
	}
	case 5:  // Side switch (alternates sides)
	{
		if (flip_side)
			best_yaw += phase.yaw_angle;
		else
			best_yaw -= phase.yaw_angle;
		break;
	}
	default:
		break;
	}

	// Apply pitch based on phase pitch_mode
	float target_pitch = 0.f;
	switch (phase.pitch_mode)
	{
	case 0:  // Static
		target_pitch = phase.pitch_angle;
		break;
	case 1:  // Random
	{
		math::random_seed(HACKS->global_vars->tickcount + defensive_tick);
		target_pitch = math::random_float(phase.pitch_angle, phase.pitch_angle2);
		break;
	}
	case 2:  // Up
		target_pitch = -phase.pitch_angle;
		break;
	case 3:  // Down
		target_pitch = phase.pitch_angle;
		break;
	case 4:  // Switch
	{
		if ((HACKS->global_vars->tickcount + defensive_tick) % 2 == 0)
			target_pitch = phase.pitch_angle;
		else
			target_pitch = phase.pitch_angle2;
		break;
	}
	case 5:  // 3-way
	{
		int idx = (HACKS->global_vars->tickcount + defensive_tick) % 3;
		if (idx == 0)
			target_pitch = phase.pitch_angle;
		else if (idx == 1)
			target_pitch = phase.pitch_angle2;
		else
			target_pitch = 0.f;
		break;
	}
	default:
		target_pitch = 0.f;
		break;
	}

	if (g_cfg.antihit.def_pitch)
		HACKS->cmd->viewangles.x = std::clamp(target_pitch, -89.f, 89.f);

	// Increment tick counter
	defensive_tick++;

	// FIXED: Clamp counter to prevent overflow (resets every ~40 minutes)
	if (defensive_tick > 1000000)
		defensive_tick = 0;
}
void c_anti_aim::manual_yaw()
{
	const int offset = g_cfg.antihit.manual_offset;

	if (g_cfg.binds[left_b].toggled)
		best_yaw += 90 + offset;          // left key = turn left

	if (g_cfg.binds[right_b].toggled)
		best_yaw -= 90 + offset;          // right key = turn right

	if (g_cfg.binds[back_b].toggled)
		best_yaw += 180 + offset;         // back key = turn around
}
void c_anti_aim::fake_duck()
{
#ifndef LEGACY
	auto state = HACKS->local->animstate();
	if (!state)
		return;

#if 0 // OLD CODE (kept for revert)
	static bool start = true;
	if (HACKS->game_rules->is_valve_ds() || HACKS->game_rules->is_freeze_time())
	{
		fake_ducking = false;
		start = true;
		return;
	}

	if (HACKS->local->flags().has(FL_FROZEN) || !MOVEMENT->on_ground())
	{
		fake_ducking = false;
		start = true;
		return;
	}

	if (g_cfg.binds[fd_b].toggled)
	{
		HACKS->cmd->buttons.force(IN_BULLRUSH);

		if (start)
		{
			if (HACKS->client_state->choked_commands > 0)
			{
				fake_ducking = false;
				return;
			}
			else
				HACKS->cmd->buttons.remove(IN_DUCK);

			start = false;
			fake_ducking = false;
			return;
		}

		if (HACKS->client_state->choked_commands < 7)
			HACKS->cmd->buttons.remove(IN_DUCK);
		else
			HACKS->cmd->buttons.force(IN_DUCK);

		fake_ducking = true;
	}
	else
	{
		fake_ducking = false;

		start = true;
	}
#endif

	// Fatality `antiaim::fake_duck` parity (antiaim.cpp:557-613):
	//  1. duck first (hold IN_DUCK until the duck amount is >= 0.8),
	//  2. keep the choke high, but remove IN_DUCK on the command that will be
	//     sent, so the server keeps the standing state,
	//  3. force IN_BULLRUSH while unducking and force the send at the choke
	//     limit,
	//  4. on release keep sending until the unduck has gone out cleanly.
	static bool first_unduck = false;
	static bool first_duck = true;

	if (HACKS->game_rules->is_valve_ds() || HACKS->game_rules->is_freeze_time()
		|| HACKS->local->flags().has(FL_FROZEN) || !MOVEMENT->on_ground())
	{
		fake_ducking = false;
		first_duck = true;
		first_unduck = false;
		return;
	}

	// Do not fight the tickbase shifting.
	if (EXPLOITS->cl_move.trigger && EXPLOITS->cl_move.shifting)
		return;

	const int to_choke = std::clamp(HACKS->max_choke - 1, 0, 14);

	if (!g_cfg.binds[fd_b].toggled)
	{
		if (first_unduck && HACKS->cmd->buttons.has(IN_DUCK) && HACKS->local->duck_amount() != 1.f)
			*HACKS->send_packet = HACKS->client_state->choked_commands >= to_choke;
		else
			first_unduck = false;

		first_duck = true;
		fake_ducking = false;
		return;
	}

	HACKS->cmd->buttons.force(IN_DUCK);

	if (first_duck && HACKS->local->duck_amount() < 0.8f)
	{
		fake_ducking = true;
		return;
	}

	if (first_duck && HACKS->client_state->choked_commands)
	{
		*HACKS->send_packet = true;
		HACKS->cmd->buttons.remove(IN_DUCK);
		HACKS->cmd->buttons.force(IN_BULLRUSH);
		first_duck = false;
		fake_ducking = true;
		return;
	}

	const float duck_speed = std::max(HACKS->local->duck_speed(), 0.01f);
	const int needed_ticks = static_cast<int>(std::ceil(
		(1.f - HACKS->local->duck_amount()) / (HACKS->global_vars->interval_per_tick * duck_speed)));

	if (needed_ticks <= to_choke - HACKS->client_state->choked_commands + 1)
		HACKS->cmd->buttons.remove(IN_DUCK);

	*HACKS->send_packet = HACKS->client_state->choked_commands >= to_choke;

	if (!HACKS->cmd->buttons.has(IN_DUCK))
		HACKS->cmd->buttons.force(IN_BULLRUSH);

	first_duck = false;
	first_unduck = true;
	fake_ducking = true;
#endif
}

INLINE int get_ticks_to_stop()
{
	auto vel = HACKS->local->velocity();

	int ticks_to_stop = 0;
	while (true)
	{
		if (vel.length_2d() < 1.f)
			break;

		game_movement::friction(vel);

		ticks_to_stop++;
	}
	return ticks_to_stop;
}

void c_anti_aim::slow_walk()
{
	if (!HACKS || !HACKS->local || !HACKS->weapon_info || !MOVEMENT || !RAGEBOT)
		return;

	if (!HACKS->weapon_info || !MOVEMENT->on_ground() || RAGEBOT->trigger_stop)
		return;

	auto velocity = HACKS->local->velocity();

#ifdef LEGACY
	if (g_cfg.binds[sw_b].toggled)
	{
		HACKS->cmd->buttons.remove(IN_SPEED);

		int ticks = get_ticks_to_stop();
		if (ticks > (13 - HACKS->client_state->choked_commands) || !HACKS->client_state->choked_commands)
			game_movement::force_stop();
	}

#else
	auto max_speed = HACKS->local->is_scoped() ? HACKS->weapon_info->max_speed_alt : HACKS->weapon_info->max_speed;

	if (g_cfg.binds[sw_b].toggled)
	{
		// OLD CODE (kept for revert):
		// game_movement::modify_move(*HACKS->cmd, velocity, max_speed * 0.34f);
		//
		// Adjustable slow-walk speed: percent of max weapon speed.
		const float speed_scale = std::clamp(g_cfg.antihit.slow_walk_speed, 5, 100) * 0.01f;
		game_movement::modify_move(*HACKS->cmd, velocity, max_speed * speed_scale);
	}
	else
	{
		auto max_speed = HACKS->local->is_scoped() ? HACKS->weapon_info->max_speed_alt : HACKS->weapon_info->max_speed;

		float tickrate_rate = 5.f / HACKS->tick_rate;

		float rate = ((HACKS->cmd->command_number % HACKS->tick_rate) * tickrate_rate) + 95.f;
		float strength = std::clamp(rate, 95.f, 100.f);

		float new_max_speed = (strength / 100.0f) * max_speed;
		game_movement::modify_move(*HACKS->cmd, velocity, new_max_speed);
	}
#endif
}

void c_anti_aim::force_move()
{
	auto animstate = HACKS->local->animstate();
	if (!animstate)
		return;

	if (!g_cfg.antihit.desync)
		return;

	auto peek_info = MOVEMENT->get_peek_info();
	if (peek_info.peek_execute && peek_info.start_pos.dist_to(HACKS->local->origin()) > 1.f)
		return;

	float speed = HACKS->local->velocity().length_2d();
	if (speed > 10.f)
		return;

	//if (cmd_shift::shifting)
	//{
	//	g_ctx.cmd->forwardmove = 0.f;
	//	return;
	//}

	auto holding_w = HACKS->cmd->buttons.has(IN_FORWARD);
	auto holding_a = HACKS->cmd->buttons.has(IN_MOVELEFT);
	auto holding_s = HACKS->cmd->buttons.has(IN_BACK);
	auto holding_d = HACKS->cmd->buttons.has(IN_MOVERIGHT);

	auto moving = holding_w || holding_a || holding_s || holding_d;

	bool ready_to_move = !moving && MOVEMENT->on_ground();
	if (ready_to_move)
	{
		float modifier = (animstate->anim_duck_amount > 0.f || fake_ducking) ? 3.25f : 1.01f;
		if (g_cfg.antihit.distortion)
			modifier *= -2.f;

		HACKS->cmd->forwardmove = flip_move ? -modifier : modifier;
	}

	flip_move = !flip_move;
}

void c_anti_aim::extended_fake()
{
	if (!HACKS || !HACKS->local || !HACKS->cmd || !HACKS->weapon_info || !FAKE_LAG || !HACKS->send_packet)
		return;

	if (!g_cfg.antihit.distortion || HACKS->valve_ds)
		return;

	if (!*HACKS->send_packet)
		return;

	auto max_speed = HACKS->local->is_scoped() ? HACKS->weapon_info->max_speed_alt : HACKS->weapon_info->max_speed;
	max_speed *= 0.34f;

	auto speed = HACKS->local->velocity().length_2d();

	bool ensure_lean = (max_speed / 3.4f) >= speed || g_cfg.binds[sw_b].toggled || g_cfg.binds[ens_lean_b].toggled;

	auto holding_w = HACKS->cmd->buttons.has(IN_FORWARD);
	auto holding_a = HACKS->cmd->buttons.has(IN_MOVELEFT);
	auto holding_s = HACKS->cmd->buttons.has(IN_BACK);
	auto holding_d = HACKS->cmd->buttons.has(IN_MOVERIGHT);

	auto moving = holding_w || holding_a || holding_s || holding_d;

	auto choke = FAKE_LAG->get_choke_amount();
	float max_roll = ensure_lean ? 50.f : 47.f;
	if (g_cfg.antihit.distortion_pitch > 0 && choke >= 14)
		max_roll = 94.f;

	float roll_angle = max_roll * ((float)g_cfg.antihit.distortion_range / 100.f) * (-fake_side);

	if (g_cfg.binds[ens_lean_b].toggled || !moving)
		HACKS->cmd->viewangles.z = roll_angle;
}

void c_anti_aim::automatic_edge()
{
	edging = false;

	//if (!MOVEMENT || !HACKS || !HACKS->local || !HACKS->engine_trace)
		//return;

	if (g_cfg.binds[freestand_b].toggled || !g_cfg.binds[edge_b].toggled || !MOVEMENT->on_ground() || g_cfg.binds[left_b].toggled || g_cfg.binds[right_b].toggled || g_cfg.binds[back_b].toggled)
		return;

	float best_dist = FLT_MAX;
	float best_edge_yaw = FLT_MAX;
	vec3_t best_pos = {};

	vec3_t start = HACKS->local->get_abs_origin() + vec3_t(0.f, 0.f, HACKS->local->view_offset().z / 2.f);

	for (float step = 0.f; step <= 2.f * M_PI; step += DEG2RAD(18.f))
	{
		float x = 40.f * std::cos(step);
		float y = 40.f * std::sin(step);

		vec3_t end = vec3_t(start.x + x, start.y + y, start.z);

		c_trace_filter filter;
		filter.skip = HACKS->local;

		c_game_trace trace = {};
		HACKS->engine_trace->trace_ray(ray_t(start, end), CONTENTS_SOLID | CONTENTS_GRATE, &filter, &trace);

		if (trace.entity && (trace.entity->is_player() || trace.entity->is_weapon()))
			continue;

		if (trace.fraction < 1.f)
		{
			float dist = start.dist_to(trace.end);
			if (best_dist > dist)
			{
				best_edge_yaw = RAD2DEG(step);
				best_pos = trace.end;
				best_dist = dist;
			}
		}
	}

	if (best_edge_yaw == FLT_MAX)
		return;

	if (g_cfg.antihit.yaw > 0)
		best_edge_yaw -= 180.f;

	edging = true;
	best_yaw = math::normalize_yaw(best_edge_yaw);
}
// Freestanding, Fatality model.
// Research: Fatality.win-Source internal_hvh/features/freestanding.cpp:179-327
// and resolver.cpp:321-451. Two-stage enemy-side comparison at 12/25 units
// around both the local player and the enemy. When neither side is clearly
// better (standoff) or the intended side is still shootable, no direction is
// reported and the caller falls back to the configured yaw mode instead of
// leaving the raw view.
void c_anti_aim::freestanding_legacy()
{
	freestanding_has_direction = false;

#if 0 // OLD CODE (kept for revert)
	if (!g_cfg.binds[freestand_b].toggled || !MOVEMENT->on_ground() || g_cfg.binds[left_b].toggled || g_cfg.binds[right_b].toggled || g_cfg.binds[back_b].toggled)
		return;

	auto local_anims = ANIMFIX->get_local_anims();
	if (!local_anims || !HACKS->local)
		return;

	// Enemy with the lowest FOV from the real (pre-AA) view, within 18 degrees.
	c_cs_player* target = nullptr;
	vec3_t target_pos{};
	float best_fov = 18.f;

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player || !player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			vec3_t pos = player->get_abs_origin();
			pos.z += 64.f;

			const auto angle = math::calc_angle(local_anims->eye_pos, pos);
			const float fov = std::fabsf(math::normalize_yaw(angle.y - start_yaw));
			if (fov < best_fov)
			{
				best_fov = fov;
				target = player;
				target_pos = pos;
			}
		});

	if (!target)
		return;

	const float target_yaw = math::calc_angle(local_anims->eye_pos, target_pos).y;
	const vec3_t enemy_eye = target->get_eye_position();

	const vec3_t base = HACKS->local->get_abs_origin() + vec3_t(0.f, 0.f, HACKS->local->view_offset().z * 0.5f);

	auto side_point = [&](float yaw, float dist) -> vec3_t
		{
			const float rad = DEG2RAD(yaw);
			return vec3_t{ base.x + std::cos(rad) * dist, base.y + std::sin(rad) * dist, base.z };
		};

	// Whether the enemy can land a bullet on that side of the body (walls and
	// penetration included, since simulate runs a real trace).
	auto exposed = [&](float yaw) -> bool
		{
			auto bullet = penetration::simulate(target, HACKS->local, enemy_eye, side_point(yaw, 8.f), false, true);
			return bullet.damage > 0 && bullet.traced_target == HACKS->local;
		};

	const bool left_exposed = exposed(target_yaw - 90.f);
	const bool right_exposed = exposed(target_yaw + 90.f);

	if (!left_exposed && right_exposed)
		best_yaw = math::normalize_yaw(target_yaw - 90.f);   // left covered -> face left
	else if (!right_exposed && left_exposed)
		best_yaw = math::normalize_yaw(target_yaw + 90.f);   // right covered -> face right
	else
		best_yaw = math::normalize_yaw(target_yaw + 180.f);  // no usable cover -> face away
}
#endif

	if (!g_cfg.binds[freestand_b].toggled || !MOVEMENT->on_ground() || g_cfg.binds[left_b].toggled || g_cfg.binds[right_b].toggled || g_cfg.binds[back_b].toggled)
		return;

	auto local_anims = ANIMFIX->get_local_anims();
	if (!local_anims || !HACKS->local)
		return;

	// Fatality collects the nearest enemy regardless of the view FOV. The old
	// 18 degree gate made freestanding silently do nothing (and the raw view
	// stay as the yaw) whenever the enemy was not near the crosshair.
	c_cs_player* target = nullptr;
	float best_fov = FLT_MAX;

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player || !player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			vec3_t pos = player->get_abs_origin();
			pos.z += 64.f;

			const auto angle = math::calc_angle(local_anims->eye_pos, pos);
			const float fov = std::fabsf(math::normalize_yaw(angle.y - start_yaw));
			if (fov < best_fov)
			{
				best_fov = fov;
				target = player;
			}
		});

	if (!target)
		return;

	auto get_rotated_pos = [](const vec3_t& start, float rotation, float distance) -> vec3_t
		{
			const float rad = DEG2RAD(rotation);
			return vec3_t{ start.x + std::cos(rad) * distance, start.y + std::sin(rad) * distance, start.z };
		};

	const vec3_t eye_pos = local_anims->eye_pos;

	vec3_t target_position = target->get_abs_origin();
	target_position.z += 66.f;

	const float target_angle = math::calc_angle(eye_pos, target_position).y;

	const vec3_t local_pos_left = get_rotated_pos(eye_pos, math::normalize_yaw(target_angle - 90.f), 25.f);
	const vec3_t local_pos_right = get_rotated_pos(eye_pos, math::normalize_yaw(target_angle + 90.f), 25.f);
	const vec3_t local_half_pos_left = get_rotated_pos(eye_pos, math::normalize_yaw(target_angle - 90.f), 12.f);
	const vec3_t local_half_pos_right = get_rotated_pos(eye_pos, math::normalize_yaw(target_angle + 90.f), 12.f);
	const vec3_t enemy_pos_left = get_rotated_pos(target_position, math::normalize_yaw(target_angle - 90.f), 25.f);
	const vec3_t enemy_pos_right = get_rotated_pos(target_position, math::normalize_yaw(target_angle + 90.f), 25.f);

	// Can the enemy damage the local player at `point` when firing from `from`?
	// (by value: vec3_t::valid() is non-const)
	auto can_hit_local = [&](vec3_t from, vec3_t point) -> bool
		{
			if (!from.valid() || !point.valid())
				return false;

			auto bullet = penetration::simulate(target, HACKS->local, from, point, false, true);
			return bullet.damage > 0 && bullet.traced_target == HACKS->local;
		};

	// Fatality compare result:
	//   1 = right side exposed -> hide by facing left
	//   2 = left side exposed  -> hide by facing right
	//   3 = standoff (both sides exposed)
	//   0 = no data (neither side exposed)
	auto compare = [&](const vec3_t& from_left, const vec3_t& from_right, const vec3_t& left, const vec3_t& right) -> int
		{
			const bool left_hit = can_hit_local(from_left, left);
			const bool right_hit = can_hit_local(from_right, right);

			if (!left_hit && right_hit)
				return 1;

			if (!right_hit && left_hit)
				return 2;

			if (left_hit && right_hit)
				return 3;

			return 0;
		};

	int result = compare(target_position, target_position, local_pos_left, local_pos_right);
	if (result == 0)
		result = compare(enemy_pos_left, enemy_pos_right, local_pos_left, local_pos_right);

	// Peek-direction override: while strafing, aim the real body to the OPPOSITE
	// side of the swing relative to the enemy near our FOV (swing right -> body
	// left, swing left -> body right). This makes the model actually lean away
	// from the peek instead of only turning to the covered side.
	const auto local_velocity = HACKS->local->velocity();
	if (local_velocity.length_2d() > 5.f)
	{
		const float rad = DEG2RAD(target_angle);
		const float right_x = std::sin(rad);
		const float right_y = -std::cos(rad);
		const float v_dot_right = local_velocity.x * right_x + local_velocity.y * right_y;

		if (std::fabsf(v_dot_right) > 1.f)
		{
			// +1 = moving to the right of the enemy direction -> face left (+90).
			const float peek_direction = v_dot_right > 0.f ? 90.f : -90.f;
			best_yaw = math::normalize_yaw(target_angle + peek_direction);
			freestanding_has_direction = true;

			if (rage_log::enabled())
				rage_log::line("[FS] peek tgt=%s(%d) vdot=%.0f yaw=%.1f",
					target->get_name().c_str(), target->index(), v_dot_right, best_yaw);
			return;
		}
	}

	// Standoff / no data: still produce a direction (face away) so holding the
	// key always does something. The old code silently fell back to the normal
	// yaw mode here, which is why freestanding "didn't work" half the time.
	if (result == 0 || result == 3)
	{
		best_yaw = math::normalize_yaw(target_angle + 180.f);
		freestanding_has_direction = true;
		return;
	}

	const float real_direction = result == 1 ? -90.f : 90.f;

	// OLD CODE (kept for revert): the stage-two sanity checks bailed out to the
	// normal yaw mode, which made freestanding unreliable. The key now always
	// produces a direction.
	// const vec3_t hide_point = real_direction < 0.f ? local_half_pos_left : local_half_pos_right;
	// if (can_hit_local(target_position, hide_point))
	//	return;
	// if (can_hit_local(real_direction < 0.f ? enemy_pos_right : enemy_pos_left, hide_point))
	//	return;

	best_yaw = math::normalize_yaw(target_angle + real_direction);
	freestanding_has_direction = true;
}
// ---------------------------------------------------------------------------
// Freestanding v2 - customizable methods with anti-flick stabilisation.
//
// The old implementation snapped the real yaw straight to whichever side won
// the last cover test. A single flipped comparison (or a strafe reversal)
// rotated the body 180 degrees in one tick - the flick that got people killed.
// The new code:
//   * offers seven selectable methods (Fatality, Wall scan, Face away,
//     Velocity peek, Hybrid, Jitter sides, Custom),
//   * keeps the chosen side for `freestand_hold` ticks (hysteresis),
//   * sticks to the same enemy while it is alive and roughly in view,
//   * optionally rate-limits the applied yaw so a side flip sweeps behind the
//     enemy instead of dragging the real head across the front,
//   * exposes offset / jitter / sample distance / FOV for tuning.
// ---------------------------------------------------------------------------
c_cs_player* c_anti_aim::freestanding_find_target(float& target_angle, const vec3_t& eye_pos)
{
	c_cs_player* target = nullptr;

	auto is_candidate = [&](c_cs_player* player, float fov_margin) -> bool
	{
		if (!player || !player->is_alive() || player->dormant() || player->has_gun_game_immunity())
			return false;

		vec3_t pos = player->get_abs_origin();
		pos.z += 64.f;

		const float fov = std::fabsf(math::normalize_yaw(math::calc_angle(eye_pos, pos).y - start_yaw));
		return fov <= static_cast<float>(g_cfg.antihit.freestand_fov) + fov_margin;
	};

	// Stick to the current target while it is valid so two enemies swapping
	// the "closest" slot cannot rock the real yaw back and forth.
	if (freestand_target_index >= 0)
	{
		LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
			{
				if (target || !player || player->index() != freestand_target_index)
					return;

				if (is_candidate(player, 15.f))
					target = player;
			});
	}

	if (!target)
	{
		freestand_target_index = -1;

		float best_fov = FLT_MAX;
		LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
			{
				if (!is_candidate(player, 0.f))
					return;

				vec3_t pos = player->get_abs_origin();
				pos.z += 64.f;

				const float fov = std::fabsf(math::normalize_yaw(math::calc_angle(eye_pos, pos).y - start_yaw));
				if (fov < best_fov)
				{
					best_fov = fov;
					target = player;
				}
			});

		if (target)
			freestand_target_index = target->index();
	}

	if (!target)
	{
		target_angle = start_yaw + 180.f;
		return nullptr;
	}

	vec3_t target_position = target->get_abs_origin();
	target_position.z += 66.f;
	target_angle = math::calc_angle(eye_pos, target_position).y;
	return target;
}

float c_anti_aim::freestanding_fatality(c_cs_player* target, float target_angle, const vec3_t& eye_pos)
{
	if (!target)
		return target_angle + 180.f;

	auto get_rotated_pos = [](const vec3_t& start, float rotation, float distance) -> vec3_t
	{
		const float rad = DEG2RAD(rotation);
		return vec3_t{ start.x + std::cos(rad) * distance, start.y + std::sin(rad) * distance, start.z };
	};

	vec3_t target_position = target->get_abs_origin();
	target_position.z += 66.f;

	const float dist = static_cast<float>(std::clamp(g_cfg.antihit.freestand_dist, 8, 60));

	const vec3_t local_pos_left = get_rotated_pos(eye_pos, math::normalize_yaw(target_angle - 90.f), dist);
	const vec3_t local_pos_right = get_rotated_pos(eye_pos, math::normalize_yaw(target_angle + 90.f), dist);
	const vec3_t enemy_pos_left = get_rotated_pos(target_position, math::normalize_yaw(target_angle - 90.f), dist);
	const vec3_t enemy_pos_right = get_rotated_pos(target_position, math::normalize_yaw(target_angle + 90.f), dist);

	auto can_hit_local = [&](vec3_t from, vec3_t point) -> bool
	{
		if (!from.valid() || !point.valid())
			return false;

		auto bullet = penetration::simulate(target, HACKS->local, from, point, false, true);
		return bullet.damage > 0 && bullet.traced_target == HACKS->local;
	};

	auto compare = [&](const vec3_t& from_left, const vec3_t& from_right, const vec3_t& left, const vec3_t& right) -> int
	{
		const bool left_hit = can_hit_local(from_left, left);
		const bool right_hit = can_hit_local(from_right, right);

		if (!left_hit && right_hit)
			return 1;   // right side exposed -> face left

		if (!right_hit && left_hit)
			return 2;   // left side exposed -> face right

		if (left_hit && right_hit)
			return 3;   // standoff

		return 0;
	};

	int result = compare(target_position, target_position, local_pos_left, local_pos_right);
	if (result == 0)
		result = compare(enemy_pos_left, enemy_pos_right, local_pos_left, local_pos_right);

	if (result == 1)
		return target_angle - 90.f;

	if (result == 2)
		return target_angle + 90.f;

	return target_angle + 180.f; // standoff / no data
}

// Direction toward the nearest wall (the covered side), or start_yaw+180 when
// nothing is close. Fatality orients the body toward the cover, not away from
// it - facing away mirrored the freestand to the wrong side.
float c_anti_aim::freestanding_wall_dir(const vec3_t& eye_pos, bool& found)
{
	const float reach = static_cast<float>(std::clamp(g_cfg.antihit.freestand_dist, 8, 60)) * 1.5f + 20.f;

	const vec3_t start = eye_pos;

	float best_dist = FLT_MAX;
	float best_yaw = start_yaw + 180.f;

	for (float step = 0.f; step < 360.f; step += 15.f)
	{
		const float rad = DEG2RAD(step);
		const vec3_t end = vec3_t(start.x + std::cos(rad) * reach, start.y + std::sin(rad) * reach, start.z);

		c_trace_filter filter;
		filter.skip = HACKS->local;

		c_game_trace trace = {};
		HACKS->engine_trace->trace_ray(ray_t(start, end), CONTENTS_SOLID | CONTENTS_GRATE, &filter, &trace);

		if (trace.entity && (trace.entity->is_player() || trace.entity->is_weapon()))
			continue;

		if (trace.fraction < 1.f)
		{
			const float hit_dist = start.dist_to(trace.end);
			if (hit_dist < best_dist)
			{
				best_dist = hit_dist;
				best_yaw = step;
			}
		}
	}

	found = best_dist != FLT_MAX;
	return best_yaw; // yaw pointing toward the nearest cover
}

float c_anti_aim::freestanding_wall_yaw(const vec3_t& eye_pos)
{
	bool found = false;
	const float dir = freestanding_wall_dir(eye_pos, found);

	if (!found)
		return start_yaw + 180.f;

	// Face away from the nearest wall so the real hitboxes sit behind it.
	return math::normalize_yaw(dir + 180.f);
}

// Body-hiding cover scan. Returns the yaw nearest `prefer_center` where the
// enemy cannot see our body silhouette across the whole desync range. Only
// rays are traced (no damage sims), so it is cheap enough to run every tick.
// This is what makes a freestand actually use an edge instead of only turning
// away from the enemy: the shoulders are re-sampled per candidate yaw and the
// candidate whose body is blocked by the wall wins.
float c_anti_aim::freestanding_cover_yaw(c_cs_player* target, const vec3_t& eye_pos, float prefer_center)
{
	if (!target || !HACKS->local || !HACKS->engine_trace)
		return prefer_center;

	auto state = HACKS->local->animstate();
	const float max_rot = state ? std::clamp(state->get_max_rotation(), 0.f, 60.f) : 58.f;

	const vec3_t origin = HACKS->local->get_abs_origin();
	const vec3_t enemy_eye = target->get_eye_position();

	// Head sits on the centre line, so its exposure does not depend on the body
	// yaw - trace it once. Shoulders move with the body, so each body yaw is
	// sampled separately.
	const vec3_t head_point{ origin.x, origin.y, origin.z + 64.f };

	c_trace_filter_skip_two_entities head_filter(target, HACKS->local);
	c_game_trace head_trace{};
	HACKS->engine_trace->trace_ray(ray_t(enemy_eye, head_point), MASK_SHOT_HULL | CONTENTS_HITBOX,
		(i_trace_filter*)&head_filter, &head_trace);
	const int head_exposed = head_trace.fraction >= 0.97f ? 2 : 0;

	auto shoulders_exposed = [&](float body_yaw) -> int
	{
		const float rad = DEG2RAD(body_yaw);
		const float right_x = std::sin(rad);
		const float right_y = -std::cos(rad);

		const vec3_t points[2] =
		{
			vec3_t{ origin.x + right_x * 13.f, origin.y + right_y * 13.f, origin.z + 42.f },
			vec3_t{ origin.x - right_x * 13.f, origin.y - right_y * 13.f, origin.z + 42.f },
		};

		int hits = 0;
		for (const auto& point : points)
		{
			c_trace_filter_skip_two_entities filter(target, HACKS->local);
			c_game_trace trace{};
			HACKS->engine_trace->trace_ray(ray_t(enemy_eye, point), MASK_SHOT_HULL | CONTENTS_HITBOX,
				(i_trace_filter*)&filter, &trace);

			if (trace.fraction >= 0.97f)
				++hits;
		}

		return hits;
	};

	float best_yaw = prefer_center;
	int best_exposed = 9999;
	float best_deviation = FLT_MAX;

	for (int i = -6; i <= 6; ++i)
	{
		const float candidate = math::normalize_yaw(prefer_center + i * 15.f);

		// The server body is the real yaw; the visible body is desynced to
		// either clamp edge (the side flips on send). Cover all three so
		// whichever body is shot at sits behind the wall.
		const int exposed = head_exposed
			+ shoulders_exposed(candidate)
			+ shoulders_exposed(math::normalize_yaw(candidate - max_rot))
			+ shoulders_exposed(math::normalize_yaw(candidate + max_rot));

		const float deviation = std::fabs(static_cast<float>(i));

		if (exposed < best_exposed || (exposed == best_exposed && deviation < best_deviation))
		{
			best_exposed = exposed;
			best_deviation = deviation;
			best_yaw = candidate;
		}
	}

	return best_yaw;
}

// Auto-peek cover override. While peeking, the player's safe spot (the peek
// start) is known, so the direction to it is a stable cover bias. The wall
// trace and velocity both reverse on the return leg, which flipped the
// freestand and exposed the head while sliding back to origin.
bool c_anti_aim::freestanding_peek_cover(float target_angle, const vec3_t& eye_pos, float& cover_yaw)
{
	auto& peek_info = MOVEMENT->get_peek_info();
	if (!peek_info.valid() || !peek_info.start_pos.valid())
		return false;

	const vec3_t origin = HACKS->local->get_abs_origin();
	if (peek_info.start_pos.dist_to(origin) <= 10.f)
		return false;

	cover_yaw = math::calc_angle(eye_pos, peek_info.start_pos).y;

	// Nothing to hide behind when the cover sits straight down the enemy line.
	return std::fabs(math::normalize_yaw(cover_yaw - target_angle)) > 20.f;
}

float c_anti_aim::freestanding_edge(c_cs_player* target, float target_angle, const vec3_t& eye_pos)
{
	if (!target)
		return freestanding_wall_yaw(eye_pos);

	// Auto-peek: the safe spot (peek start) is known, so hide behind it instead
	// of the wall trace. The trace flips sides on the return leg, which turned
	// the freestand the wrong way and poked the head into the open.
	float peek_cover = 0.f;
	if (freestanding_peek_cover(target_angle, eye_pos, peek_cover))
		return freestanding_cover_yaw(target, eye_pos, peek_cover);

	// Fatality parity: face the covered side (the wall) and let the cover scan
	// settle the exact yaw. Biasing toward "away" here mirrored the body to the
	// open side.
	bool found = false;
	const float wall_dir = freestanding_wall_dir(eye_pos, found);
	const float prefer = found ? wall_dir : target_angle + 180.f;

	return freestanding_cover_yaw(target, eye_pos, prefer);
}

float c_anti_aim::freestanding_face_away(c_cs_player* target, float target_angle, const vec3_t& eye_pos)
{
	if (!target)
		return target_angle + 180.f;

	// Auto-peek: keep hiding behind the peek start while sliding back.
	float peek_cover = 0.f;
	if (freestanding_peek_cover(target_angle, eye_pos, peek_cover))
		return freestanding_cover_yaw(target, eye_pos, peek_cover);

	float prefer = target_angle + 180.f;

	// When cover sits on a side of the enemy line, hug that edge instead of
	// only facing away. Directly behind (rel ~180) stays a pure face-away.
	bool found = false;
	const float wall_dir = freestanding_wall_dir(eye_pos, found);
	if (found)
	{
		const float rel = std::fabs(math::normalize_yaw(wall_dir - target_angle));
		if (rel > 45.f && rel < 135.f)
			prefer = wall_dir;
	}

	// At-targets + freestanding held together: keep the at-targets base facing
	// and hide the body around it, instead of the freestand bind silently
	// disabling at-targets.
	if ((g_cfg.antihit.at_targets_enabled || g_cfg.antihit.at_targets) && at_targets())
		prefer = best_yaw;

	return freestanding_cover_yaw(target, eye_pos, prefer);
}

float c_anti_aim::freestanding_peek_edge(c_cs_player* target, float target_angle, const vec3_t& eye_pos)
{
	if (!target)
		return freestanding_wall_yaw(eye_pos);

	// Auto-peek: use the known safe spot so the return leg does not flip the
	// body to the open side.
	float peek_cover = 0.f;
	if (freestanding_peek_cover(target_angle, eye_pos, peek_cover))
		return freestanding_cover_yaw(target, eye_pos, peek_cover);

	const auto local_velocity = HACKS->local->velocity();
	if (local_velocity.length_2d() <= 5.f)
		return freestanding_face_away(target, target_angle, eye_pos);

	const float rad = DEG2RAD(target_angle);
	const float right_x = std::sin(rad);
	const float right_y = -std::cos(rad);
	const float v_dot_right = local_velocity.x * right_x + local_velocity.y * right_y;

	if (std::fabsf(v_dot_right) <= 1.f)
		return freestanding_face_away(target, target_angle, eye_pos);

	// Swing right -> body left, then snap to the covered yaw near that side so
	// the peek actually uses cover instead of only turning away.
	const float prefer = target_angle + (v_dot_right > 0.f ? 90.f : -90.f);
	return freestanding_cover_yaw(target, eye_pos, prefer);
}

float c_anti_aim::freestanding_edge_jitter(c_cs_player* target, float target_angle, const vec3_t& eye_pos)
{
	if (!target)
		return freestanding_wall_yaw(eye_pos);

	const float left = freestanding_cover_yaw(target, eye_pos, target_angle - 90.f);
	const float right = freestanding_cover_yaw(target, eye_pos, target_angle + 90.f);

	const int tick = HACKS->global_vars ? HACKS->global_vars->tickcount : 0;
	const int interval = std::max(1, g_cfg.antihit.freestand_hold);

	return ((tick / interval) % 2) != 0 ? right : left;
}

float c_anti_aim::freestanding_velocity(c_cs_player* target, float target_angle)
{
	const auto local_velocity = HACKS->local->velocity();
	if (local_velocity.length_2d() <= 5.f)
		return target_angle + 180.f;

	const float rad = DEG2RAD(target_angle);
	const float right_x = std::sin(rad);
	const float right_y = -std::cos(rad);
	const float v_dot_right = local_velocity.x * right_x + local_velocity.y * right_y;

	if (std::fabsf(v_dot_right) <= 1.f)
		return target_angle + 180.f;

	// Moving to the enemy's right -> put the real body on the left.
	return target_angle + (v_dot_right > 0.f ? 90.f : -90.f);
}

float c_anti_aim::freestanding_hybrid(c_cs_player* target, float target_angle, const vec3_t& eye_pos)
{
	if (!target)
		return freestanding_wall_yaw(eye_pos);

	const float fatality = freestanding_fatality(target, target_angle, eye_pos);
	const float relative = std::fabs(math::normalize_yaw(fatality - target_angle));

	// Fatality found no usable cover (standoff / face-away) -> fall back to the
	// body-hiding cover scan instead of a blind wall direction.
	if (relative > 135.f)
		return freestanding_face_away(target, target_angle, eye_pos);

	// While strafing let the peek direction win, otherwise trust Fatality.
	const auto local_velocity = HACKS->local->velocity();
	if (local_velocity.length_2d() > 5.f)
		return freestanding_velocity(target, target_angle);

	return fatality;
}

void c_anti_aim::freestanding_apply(float desired_yaw, float target_angle)
{
	const int tick = HACKS->global_vars ? HACKS->global_vars->tickcount : 0;

	// Store the direction relative to the enemy so the held side survives
	// small target motion and only an actual side change counts as a switch.
	const float desired_offset = math::normalize_yaw(desired_yaw - target_angle);

	if (!freestand_active)
	{
		freestand_hold_offset = desired_offset;
		freestand_hold_tick = tick;
		freestand_smooth_offset = desired_offset;
		freestand_sweep_sign = 0.f;
	}
	else if (g_cfg.antihit.freestand_avoid_flick)
	{
		const float diff = std::fabs(math::normalize_yaw(desired_offset - freestand_hold_offset));
		const int hold = std::max(1, g_cfg.antihit.freestand_hold);

		if (diff > 45.f && tick - freestand_hold_tick >= hold)
		{
			freestand_hold_offset = desired_offset;
			freestand_hold_tick = tick;
		}
	}
	else
	{
		freestand_hold_offset = desired_offset;
		freestand_hold_tick = tick;
	}

	// Rate-limit the held offset (not the absolute yaw) so a side flip always
	// travels through the back - the offset opposite the enemy - instead of
	// dragging the real head across the front.
	if (g_cfg.antihit.freestand_smooth > 0)
	{
		const float step = static_cast<float>(g_cfg.antihit.freestand_smooth);
		float delta = math::normalize_yaw(freestand_hold_offset - freestand_smooth_offset);

		if (std::fabs(delta) > 90.f)
		{
			// Flip between sides: sweep the long way around (through 180). The
			// direction is latched once - re-deriving it from the wrapping
			// offset made the sweep oscillate forever on non-90 offsets.
			if (freestand_sweep_sign == 0.f)
				freestand_sweep_sign = (freestand_smooth_offset != 0.f)
					? (freestand_smooth_offset > 0.f ? -1.f : 1.f)
					: (delta > 0.f ? 1.f : -1.f);

			delta = freestand_sweep_sign * (360.f - std::fabs(delta));
		}
		else
		{
			freestand_sweep_sign = 0.f;
		}

		freestand_smooth_offset = math::normalize_yaw(freestand_smooth_offset + std::clamp(delta, -step, step));
	}
	else
	{
		freestand_smooth_offset = freestand_hold_offset;
		freestand_sweep_sign = 0.f;
	}

	float yaw = math::normalize_yaw(target_angle + freestand_smooth_offset);

	if (g_cfg.antihit.freestand_jitter > 0)
	{
		const double phase = static_cast<double>(tick) * 0.35;
		yaw = math::normalize_yaw(yaw + static_cast<float>(std::sin(phase)) * static_cast<float>(g_cfg.antihit.freestand_jitter));
	}

	best_yaw = math::normalize_yaw(yaw + static_cast<float>(g_cfg.antihit.freestand_offset));
	freestanding_has_direction = true;
	freestand_active = true;
}

void c_anti_aim::freestanding()
{
	freestanding_has_direction = false;

	if (!g_cfg.binds[freestand_b].toggled || !MOVEMENT->on_ground() || g_cfg.binds[left_b].toggled || g_cfg.binds[right_b].toggled || g_cfg.binds[back_b].toggled)
	{
		freestand_active = false;
		freestand_target_index = -1;
		return;
	}

	auto local_anims = ANIMFIX->get_local_anims();
	if (!local_anims || !HACKS->local)
	{
		freestand_active = false;
		return;
	}

	const vec3_t eye_pos = local_anims->eye_pos;

	float target_angle = start_yaw + 180.f;
	const int prev_target = freestand_target_index;
	c_cs_player* target = freestanding_find_target(target_angle, eye_pos);

	// No enemy: do not fabricate a yaw (the old math collapsed to the raw view
	// yaw). Clear the flag so run() falls back to the configured normal AA.
	if (!target)
	{
		freestand_active = false;
		freestanding_has_direction = false;
		return;
	}

	// New target: restart hold/smooth so the previous target's side cannot be
	// carried over (instant >90 flick on target switch).
	if (freestand_target_index != prev_target)
		freestand_active = false;

	float desired = target_angle + 180.f;

	switch (g_cfg.antihit.freestand_mode)
	{
	case 1: // Edge
		desired = freestanding_edge(target, target_angle, eye_pos);
		break;

	case 2: // Face away
		desired = freestanding_face_away(target, target_angle, eye_pos);
		break;

	case 3: // Peek edge
		desired = freestanding_peek_edge(target, target_angle, eye_pos);
		break;

	case 4: // Hybrid
		desired = freestanding_hybrid(target, target_angle, eye_pos);
		break;

	case 5: // Edge jitter
		desired = freestanding_edge_jitter(target, target_angle, eye_pos);
		break;

	case 6: // Custom (absolute, relative to the current view)
		desired = start_yaw + static_cast<float>(g_cfg.antihit.freestand_custom);
		break;

	case 0: // Fatality
	default:
		desired = freestanding_fatality(target, target_angle, eye_pos);
		break;
	}

	freestanding_apply(desired, target_angle);

	if (rage_log::enabled())
	{
		const float logged_delta = std::fabs(math::normalize_yaw(best_yaw - freestand_logged_yaw));
		if (logged_delta > 20.f || freestand_logged_mode != g_cfg.antihit.freestand_mode)
		{
			freestand_logged_yaw = best_yaw;
			freestand_logged_mode = g_cfg.antihit.freestand_mode;

			rage_log::line("[FS] mode=%d tgt=%s(%d) yaw=%.1f target=%.1f rel=%.1f vel=%.0f",
				g_cfg.antihit.freestand_mode, target->get_name().c_str(), target->index(),
				best_yaw, target_angle, math::normalize_yaw(best_yaw - target_angle),
				HACKS->local->velocity().length_2d());
		}
	}
}

bool c_anti_aim::at_targets()
{
	// NEW: Check if at_targets is explicitly enabled
	if (!g_cfg.antihit.at_targets_enabled && !g_cfg.antihit.at_targets)
		return false;

	// Don't apply at_targets if any manual override is active.
	// OLD CODE (kept for revert): the freestanding bind was also excluded here,
	// which meant at-targets could never be the fallback while holding both.
	// if (g_cfg.binds[edge_b].toggled ||
	//	g_cfg.binds[left_b].toggled ||
	//	g_cfg.binds[right_b].toggled ||
	//	g_cfg.binds[back_b].toggled ||
	//	g_cfg.binds[freestand_b].toggled)
	//	return false;
	if (g_cfg.binds[edge_b].toggled ||
		g_cfg.binds[left_b].toggled ||
		g_cfg.binds[right_b].toggled ||
		g_cfg.binds[back_b].toggled)
		return false;

	auto local_anims = ANIMFIX->get_local_anims();
	if (!local_anims)
		return false;

	// Pick the enemy closest to the REAL (pre-AA) view yaw. Using
	// engine->get_view_angles() here is wrong because it already contains the
	// previous tick's anti-aim, which made "at targets" lock onto an enemy
	// behind the player (i.e. face the wrong way).
	c_cs_player* target = nullptr;
	float best_fov = FLT_MAX;

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player || !player->is_alive() || player->dormant() || player->has_gun_game_immunity())
				return;

			auto angle = math::calc_angle(local_anims->eye_pos, player->get_eye_position());
			const float fov = std::fabsf(math::normalize_yaw(angle.y - start_yaw));
			if (fov < best_fov)
			{
				best_fov = fov;
				target = player;
			}
		});

	if (!target)
	{
		// No enemy found — don't apply at_targets
		return false;
	}

	// Calculate angle to target independently.
	// Fatality convention: at-targets faces 180 degrees AWAY from the target
	// (target_angle + 180). at_targets_offset is applied on top of that so a
	// value of 0 means "face directly away".
	float target_angle = math::calc_angle(local_anims->eye_pos, target->get_eye_position()).y;

	// at_targets_center pins the view exactly on the target, ignoring offset
	if (g_cfg.antihit.at_targets_center)
	{
		best_yaw = math::normalize_yaw(target_angle);
		return true;
	}

	best_yaw = math::normalize_yaw(target_angle + 180.f + static_cast<float>(g_cfg.antihit.at_targets_offset));

	return true;
}
void c_anti_aim::fake()
{
	if (!HACKS || !HACKS->local || !ANIMFIX || !FAKE_LAG || !EXPLOITS || !HACKS->global_vars)
		return;

	auto state = HACKS->local->animstate();
	if (!state)
		return;

	// OLD CODE (kept for revert):
	// if (!g_cfg.antihit.desync || cmd_shift::shifting || EXPLOITS->cl_move.trigger && EXPLOITS->cl_move.shifting)
	// 	return;
	//
	// Same precedence as the old line (`&&` binds tighter than `||`), written
	// with explicit parentheses now for readability.
	if (!g_cfg.antihit.desync || cmd_shift::shifting || (EXPLOITS->cl_move.trigger && EXPLOITS->cl_move.shifting))
		return;

	auto vars = ANIMFIX->get_local_anims();
	if (!vars)
		return;

	auto local_velocity = HACKS->local->velocity();
	aa_state_t current_state = this->detect_player_state();

	active_aa_t active = compute_active_aa_settings(this, MOVEMENT->on_ground(), local_velocity, defensive_aa, current_state);

	// 2021-2023 (post-anim-update) desync.
	//
	// The fake body yaw is no longer produced by pushing the *view* yaw on the
	// sent packet. Instead:
	//   * the desired fake body offset is handed to update_local, which forces
	//     state->abs_yaw / BODY_YAW directly (direct feet/body control), and
	//   * only the CHOKED commands carry the fake eye yaw. The sent command
	//     keeps the real yaw. Source delivers backup (choked) commands to the
	//     server, so its foot_yaw is dragged onto the fake side while the last
	//     (real) command is what a shot uses.
	vars->desync_valid = false;

	// Requested desync for the active side (UI range 0..58).
	float angle = 0.f;
	if (g_cfg.antihit.random_amount)
	{
		math::random_seed(HACKS->global_vars->tickcount);
		const int max_desync = fake_side == 1 ? active.desync_left : active.desync_right;
		// Never roll 0 - a 0 roll would drop the fake body for that tick and
		// make it snap back (visible flick).
		angle = static_cast<float>(math::random_int(std::max(1, max_desync / 2), std::max(1, max_desync)));
	}
	else
	{
		angle = static_cast<float>(fake_side == 1 ? active.desync_left : active.desync_right);
	}

	angle = std::clamp(angle, 0.f, 58.f);
	if (angle <= 0.f)
		return;

	const float desync_range = angle * std::max(vars->aim_matrix_width_range, 0.f);
	if (desync_range <= 0.f)
		return;

	vars->desync_valid = true;
	vars->desync_sign = static_cast<float>(fake_side);
	vars->desync_scale = std::clamp(angle / 58.f, 0.f, 1.f);

	if (rage_log::enabled())
		rage_log::line("[DESYNC] cmd=%d choked=%d side=%d angle=%.1f range=%.1f width=%.2f valid=1",
			HACKS->cmd ? HACKS->cmd->command_number : 0, !*HACKS->send_packet,
			fake_side, angle, desync_range, vars->aim_matrix_width_range);

	// Clamp-forcing offset. The animstate *clamps* the body to +/- max_rot, it
	// does not approach it, so a plain 1x offset never pins the server body.
	// Pushing the choked eye to base + range + max_rot makes the clamp force the
	// server body exactly onto the requested fake side (body = eye - max_rot =
	// base + range) in one tick. The sent command keeps the real yaw and the
	// baked fake body uses the true 1x clamp.
	if (!*HACKS->send_packet)
	{
		const float max_rot = state->aim_yaw_max * std::max(vars->aim_matrix_width_range, 0.f);
		best_yaw += (desync_range + max_rot) * static_cast<float>(fake_side);
	}
}
c_cs_player* c_anti_aim::get_closest_player(bool skip, bool local_distance)
{
	c_cs_player* best = nullptr;
	float best_dist = FLT_MAX;

	auto center = vec2_t(RENDER->screen.x * 0.5f, RENDER->screen.y * 0.5f);

	vec3_t view_angles{};
	HACKS->engine->get_view_angles(view_angles);

	auto local_anim = ANIMFIX->get_local_anims();

	vec3_t local_eye_pos = local_anim->eye_pos;

	LISTENER_ENTITY->for_each_player([&](c_cs_player* player)
		{
			if (!player->is_alive() || player->has_gun_game_immunity())
				return;

			auto esp = ESP->get_esp_player(player->index());
			if (skip)
			{
				if (!esp->valid)
					return;
			}

			auto valid_dormant = player->dormant() && (std::abs(esp->dormant.time - HACKS->global_vars->curtime) < 5.f);

			auto base_origin = valid_dormant && esp->dormant.origin.valid() ? esp->dormant.origin : player->get_abs_origin();
			base_origin += vec3_t(0.f, 0.f, player->view_offset().z / 2.f);

			vec2_t origin = {};
			RENDER->world_to_screen(base_origin, origin);

			auto angle = math::calc_angle(local_eye_pos, base_origin);

			float dist = local_distance ? math::get_fov(view_angles, angle) : center.dist_to(origin);
			if (dist < best_dist)
			{
				best = player;
				best_dist = dist;
			}
		});

	return best;
}

vec3_t get_predicted_pos()
{
	if (!ANIMFIX || !ENGINE_PREDICTION || !HACKS || !HACKS->local || !HACKS->weapon_info)
		return {};

	auto updated_vars = ANIMFIX->get_local_anims();
	auto unpred_vars = ENGINE_PREDICTION->get_unpredicted_vars();

	if (!updated_vars || !unpred_vars)
		return {};

	const int max_ticks = 17;
	const auto& velocity = unpred_vars->velocity;

	auto max_speed = HACKS->local->is_scoped() ? HACKS->weapon_info->max_speed_alt : HACKS->weapon_info->max_speed;
	max_speed = std::max(max_speed, 0.001f);

	// Mirror the ragebot's prediction model: simulate the ticks it takes to
	// decelerate to a stop (capped by the lag-comp window), not "max - stop".
	float speed = std::max< float >(velocity.length_2d(), 1.f);
	int max_stop_ticks = std::max< int >(((speed / max_speed) * 5.f) - 1, 0);

	const int max_tick_limit = TIME_TO_TICKS(HACKS->convars.sv_maxunlag->get_float());
	int max_predict_ticks = std::clamp(max_stop_ticks, 0, std::min(max_ticks, max_tick_limit));
	if (max_predict_ticks == 0)
		return {};

	// Tick-by-tick prediction with friction deceleration so the predicted eye position
	// doesn't overshoot a stopping player.
	const bool on_ground = HACKS->local->flags().has(FL_ONGROUND);

	vec3_t pred_velocity = velocity;
	vec3_t displacement{};

	for (int i = 0; i < max_predict_ticks; ++i)
	{
		if (on_ground)
			game_movement::friction(pred_velocity);
		else
			pred_velocity.z -= HACKS->convars.sv_gravity->get_float() * HACKS->global_vars->interval_per_tick;

		displacement += pred_velocity * HACKS->global_vars->interval_per_tick;

		if (pred_velocity.length_2d() < 1.f)
			break;
	}

	return displacement;
}

bool c_anti_aim::is_peeking()
{
	if (!ANIMFIX || !HACKS || !HACKS->local || !HACKS->weapon || !HACKS->weapon_info
		|| !HACKS->engine_trace || !HACKS->in_game || !HACKS->client_state)
		return false;

	auto updated_vars = ANIMFIX->get_local_anims();

	if (!updated_vars || !updated_vars->foot_yaw || HACKS->client_state->delta_tick == -1)
		return false;

	if (!RAGEBOT)
		return false;

	if (!RAGEBOT->can_fire() || RAGEBOT->is_shooting())
		return false;

	auto& local_cache = HACKS->local->bone_cache();
	if (!local_cache.base() || !local_cache.count())
		return false;

	auto player = get_closest_player(false, true);
	if (!player)
		return false;

	auto predicted_velocity = get_predicted_pos();
	auto predicted_eye_pos = updated_vars->eye_pos + predicted_velocity;

	if (!predicted_eye_pos.valid())
		return false;

	bool can_peek = false;

	if (!ESP)
		return false;

	auto esp = ESP->get_esp_player(player->index());
	if (!esp || !esp->valid)
		return false;

	auto valid_dormant = player->dormant() && (std::abs(esp->dormant.time - HACKS->global_vars->curtime) <= 5.f);
	auto origin = valid_dormant && esp->dormant.origin.valid() ? esp->dormant.origin : player->get_abs_origin();

	static matrix3x4_t predicted_matrix[128]{};
	std::memcpy(predicted_matrix, local_cache.base(), sizeof(predicted_matrix));

	if (player->dormant())
	{
		vec3_t poses[3]{ origin, origin + player->view_offset(), origin + vec3_t(0.f, 0.f, player->view_offset().z / 2.f) };

		for (int i = 0; i < 3; ++i)
		{
			c_trace_filter filter{};
			filter.skip = HACKS->local;

			c_game_trace out{};
			HACKS->engine_trace->trace_ray(ray_t(predicted_eye_pos, poses[i]), MASK_SHOT | CONTENTS_HITBOX, &filter, &out);

			if (out.fraction >= 0.97f)
			{
				can_peek = true;
				break;
			}
		}

		if (can_peek)
			return true;
	}
	else
	{
		auto anims = ANIMFIX->get_anims(player->index());
		if (!anims)
			return false;

		auto weapon = (c_base_combat_weapon*)(HACKS->entity_list->get_client_entity_handle(player->active_weapon()));
		if (!weapon)
			return false;

		auto weapon_info = HACKS->weapon_system->get_weapon_data(weapon->item_definition_index());
		if (!weapon_info)
			return false;

		// detect if you can get dmg by enemy
		auto predicted_origin = HACKS->local->origin() + predicted_velocity;
		math::change_bones_position(predicted_matrix, 128, HACKS->local->origin(), predicted_origin);
		{
			auto head_pos = HACKS->local->get_hitbox_position(0, predicted_matrix);

			auto old_abs_origin = HACKS->local->get_abs_origin();
			static matrix3x4_t old_cache[128]{};
			HACKS->local->store_bone_cache(old_cache);
			{
				HACKS->local->set_abs_origin(predicted_origin);
				HACKS->local->set_bone_cache(predicted_matrix);

				HACKS->local->set_abs_origin(predicted_eye_pos);
				auto eyepos_awall = penetration::simulate(player, HACKS->local, player->get_eye_position(), predicted_eye_pos, false, true);
				//	HACKS->debug_overlay->add_text_overlay(predicted_eye_pos, 0.1f, "%d", eyepos_awall);

				for (auto& i : hitbox_list)
				{
					auto hitbox_position = HACKS->local->get_hitbox_position(i, local_cache.base());
					HACKS->local->set_abs_origin(hitbox_position);
					auto awall = penetration::simulate(player, HACKS->local, player->get_eye_position(), hitbox_position, false, true);

					//	HACKS->debug_overlay->add_text_overlay(hitbox_position, 0.1f, "! %d", awall.damage);
					if (eyepos_awall.damage >= 1 || awall.damage >= 1)
					{
						can_peek = true;
						break;
					}
				}
			}
			HACKS->local->set_abs_origin(old_abs_origin);
			HACKS->local->set_bone_cache(old_cache);
		}
		math::change_bones_position(predicted_matrix, 128, predicted_origin, HACKS->local->origin());

		if (!anims->records.empty())
		{
			auto first_find = std::find_if(anims->records.begin(), anims->records.end(), [&](anim_record_t& record) {
				return record.valid_lc;
				});

			anim_record_t* first = nullptr;
			if (first_find != anims->records.end())
				first = &*first_find;

			restore_record_t restore{};
			for (const auto& i : hitbox_list)
			{
				if (i < 0 || i >= 128)  // Bounds check for hitbox index
					continue;

				auto hitbox_pos = player->get_hitbox_position(i, first ? first->matrix_orig.matrix : nullptr);

				if (first)
				{
					if (!LAGCOMP)
						continue;

					restore.store(player);
					LAGCOMP->set_record(player, first, first->matrix_orig.matrix);
				}

				auto awall = penetration::simulate(HACKS->local, player, updated_vars->eye_pos, hitbox_pos, false, true);
				bool can_hit_point = awall.damage >= 1;

				if (first)
					restore.restore(player);

				if (can_hit_point)
				{
					can_peek = true;
					break;
				}
			}
		}

		if (can_peek)
			return true;
	}

	return can_peek;
}

bool c_anti_aim::is_fake_ducking()
{
#ifndef LEGACY
	return fake_ducking;
#else
	return false;
#endif
}

void c_anti_aim::run_movement()
{
	if (!HACKS || !HACKS->local || !HACKS->game_rules || !MOVEMENT)
		return;

	if (HACKS->local->move_type() == MOVETYPE_LADDER || HACKS->local->move_type() == MOVETYPE_NOCLIP)
		return;

	if (HACKS->game_rules->is_freeze_time() || HACKS->local->flags().has(FL_FROZEN) || HACKS->local->has_gun_game_immunity())
		return;

#ifndef LEGACY
	force_move();
#endif

	fake_duck();
	slow_walk();
}
void c_anti_aim::run()
{
	// On shot/early-out ticks fake() never runs: clear the direct-desync
	// request FIRST (before the weapon check) so a stale request cannot force
	// the fake body on a tick where fake() is skipped.
	if (ANIMFIX)
	{
		if (auto vars = ANIMFIX->get_local_anims())
			vars->desync_valid = false;
	}

	if (!HACKS->weapon || !HACKS->weapon_info)
		return;

	// 1) Grenade/Shooting override – skip all AA
	if (HACKS->weapon && HACKS->weapon->is_grenade())
	{
		if (HACKS->cmd->buttons.has(IN_ATTACK) || HACKS->cmd->buttons.has(IN_ATTACK2))
			return;
	}

	if (RAGEBOT && RAGEBOT->is_shooting())
		return;

	if (HACKS->local->move_type() == MOVETYPE_LADDER || HACKS->local->move_type() == MOVETYPE_NOCLIP)
		return;

	// 2) Update defensive AA state
	auto update_tickbase_state = [&]()
		{
#ifndef LEGACY
			static int old_tickbase = 0;
			defensive_aa = false;

			if (!g_cfg.antihit.def_aa_enable)
			{
				old_tickbase = 0;
				return;
			}

			const unsigned int conditions = build_defensive_conditions(g_cfg.antihit);
			const bool can_use_dt_state = EXPLOITS && EXPLOITS->enabled() &&
				(EXPLOITS->get_exploit_mode() == EXPLOITS_DT) &&
				!(EXPLOITS->cl_move.trigger && EXPLOITS->cl_move.shifting) &&
				!cmd_shift::shifting;

			auto tickbase_diff = HACKS->local->tickbase() - old_tickbase;

			if (can_use_dt_state)
			{
				if (conditions & def_aa_cond_tickbase_trigger)
					defensive_aa = defensive_aa || tickbase_diff < 0 || tickbase_diff > 1;

				if ((conditions & def_aa_cond_tickbase_choke) && HACKS->client_state)
					defensive_aa = defensive_aa || (EXPLOITS->defensive.tickbase_choke != 100
						&& EXPLOITS->defensive.tickbase_choke > g_cfg.antihit.ticks_defensive
						&& HACKS->client_state->choked_commands);
			}

			if (conditions & def_aa_cond_on_shot)
				defensive_aa = defensive_aa || RAGEBOT->is_shooting();
			if (conditions & def_aa_cond_peek)
				defensive_aa = defensive_aa || is_peeking();
			if (conditions & def_aa_cond_in_air)
				defensive_aa = defensive_aa || (air_ticks >= g_cfg.antihit.def_aa_air_minimum_ticks);
			if (conditions & def_aa_cond_in_crouch)
				defensive_aa = defensive_aa || (HACKS->local->flags().has(FL_DUCKING) && !is_fake_ducking());
			if (conditions & def_aa_cond_always)
				defensive_aa = true;

			old_tickbase = HACKS->local->tickbase();
#endif
		};
	update_tickbase_state();

	// 3) Basic checks
	if (!HACKS->weapon || !HACKS->weapon_info)
		return;

	if (HACKS->local->move_type() == MOVETYPE_LADDER || HACKS->local->move_type() == MOVETYPE_NOCLIP)
		return;

	if (!g_cfg.antihit.enable)
		return;

	if (HACKS->game_rules->is_freeze_time() || HACKS->local->flags().has(FL_FROZEN))
		return;

	auto state = HACKS->local->animstate();
	if (!state)
		return;

	// 4) Shot tracking
	if (RAGEBOT && RAGEBOT->is_shooting())
		shot_cmd = HACKS->cmd->command_number;

	if (HACKS->cmd->buttons.has(IN_USE))
		return;

	const bool dt_on_shot_override = g_cfg.antihit.def_aa_enable &&
		(build_defensive_conditions(g_cfg.antihit) & def_aa_cond_on_shot);
	if (shot_cmd == HACKS->cmd->command_number && !dt_on_shot_override)
		return;

	// 5) Get movement state
	const bool on_ground = MOVEMENT->on_ground();
	auto local_velocity = HACKS->local->velocity();
	aa_state_t current_state = this->detect_player_state();

	// 6) Landing pitch
	if (g_cfg.antihit.landing_pitch && !was_on_ground && on_ground)
		landing_pitch_ticks = std::clamp(g_cfg.antihit.landing_pitch_ticks, 1, 16);
	was_on_ground = on_ground;

	// 7) Get active AA settings
	active_aa_t active = compute_active_aa_settings(this, on_ground, local_velocity, defensive_aa, current_state);

	apply_pitch_angle(active);

	start_yaw = HACKS->cmd->viewangles.y;
	best_yaw = HACKS->cmd->viewangles.y;

#ifdef LEGACY
	apply_legacy_aa(active);
#else
	aa_context_t ctx{};
	ctx.manual_override = g_cfg.binds[left_b].toggled || g_cfg.binds[right_b].toggled || g_cfg.binds[back_b].toggled;
	ctx.edge_override = g_cfg.binds[edge_b].toggled;
	ctx.freestand_override = g_cfg.binds[freestand_b].toggled;
	ctx.is_defensive = defensive_aa && !ctx.manual_override && !ctx.edge_override && !ctx.freestand_override;

	// Resolve the desync side once, independent of which yaw path runs below.
	update_fake_side();

	// OLD CODE (kept for revert): the flag was only cleared inside
	// freestanding(), so releasing the bind left it stuck true and fake() kept
	// skipping the sent desync forever. Clear it every tick before the yaw path.
	freestanding_has_direction = false;

	// Leaving freestanding must drop the smoothed side too, or re-entering
	// resumes from a stale hold/smooth offset.
	if (!ctx.freestand_override)
	{
		freestand_active = false;
		freestand_target_index = -1;
		freestand_sweep_sign = 0.f;
	}

	if (ctx.manual_override)
		apply_manual_yaw();
	else if (ctx.freestand_override)
	{
		// OLD CODE (kept for revert):
		// freestanding();
		//
		// If freestanding has no clear cover direction (standoff / no target /
		// too risky) it must not leave best_yaw at the raw view - that is what
		// made the player "face forward" while the key was held. Fall back to
		// the configured yaw mode, matching Fatality where real_direction == 0
		// simply means "no freestand override" and normal anti-aim continues.
		freestanding();

		if (!freestanding_has_direction)
			apply_normal_yaw(active);
	}
	else if (ctx.edge_override)
	{
		automatic_edge();

		// No edge found -> keep anti-aim instead of emitting the raw view yaw.
		if (!edging)
			apply_normal_yaw(active);
	}
	else if (ctx.is_defensive && g_cfg.antihit.def_yaw)
		apply_defensive_yaw(active);
	else
		apply_normal_yaw(active);

	if (!(g_cfg.binds[left_b].toggled || g_cfg.binds[right_b].toggled || g_cfg.binds[back_b].toggled))
		best_yaw += active.yaw_add;

	fake();
	extended_fake();

	if (*HACKS->send_packet)
	{
		math::random_seed(HACKS->global_vars->tickcount);
		auto make_rand = [](bool& flip, int& timer, int min, int max) {
			if (std::abs(HACKS->global_vars->tickcount - timer) > math::random_int(min, max)) {
				timer = HACKS->global_vars->tickcount;
				flip = !flip;
			}
		};

		static int dsy_duration = 0, duration = 0;

		make_rand(random_dsy_flipper, dsy_duration, 1, 4);
		make_rand(random_flipper, duration, 2, 5);

		flip_side = !flip_side;
		flip_jitter = !flip_jitter;
	}
#endif

	HACKS->cmd->viewangles.y = math::normalize_yaw(best_yaw);
	HACKS->cmd->viewangles.x = std::clamp(HACKS->cmd->viewangles.x, -89.f, 89.f);
}
// Calculate base peek pitch based on selected mode
float c_anti_aim::calculate_peek_pitch(c_cs_player* /*closest_enemy*/)
{
	if (!HACKS || !HACKS->local || !HACKS->local->is_alive())
		return 0.f;

	switch (g_cfg.antihit.peek_pitch_mode)
	{
	case 0: // Up
		return -89.f;
	case 1: // Down
		return 89.f;
	case 2: // Zero
		return 0.f;
	case 3: // Custom
		return std::clamp(static_cast<float>(g_cfg.antihit.peek_custom_pitch), -89.f, 89.f);
	case 4: // Meowmode: five-step pitch state
	{
		static constexpr float meow_steps[] = { -89.f, -45.f, 0.f, 45.f, 89.f };
		return meow_steps[peek_pitch_step % IM_ARRAYSIZE(meow_steps)];
	}
	default:
		return -89.f;
	}
}
void c_anti_aim::update_peek_pitch_mode()
{
	auto peek_info = MOVEMENT->get_peek_info();
	bool valid_peek = g_cfg.antihit.peek_pitch_enable && peek_info.valid() && MOVEMENT->on_ground();
	bool on_return = valid_peek && peek_info.peek_execute;
	bool on_peek = valid_peek && !peek_info.peek_execute && peek_info.start_pos.dist_to(HACKS->local->get_abs_origin()) > 10.f;

	bool should_use_peek_pitch = false;
	switch (g_cfg.antihit.peek_pitch_phase)
	{
	case 0:
		should_use_peek_pitch = on_peek;
		break;
	case 1:
		should_use_peek_pitch = on_return;
		break;
	default:
		should_use_peek_pitch = on_peek || on_return;
		break;
	}

	// FIXED: Reset peek pitch when defensive AA activates
	if (defensive_aa && peek_pitch_active)
	{
		peek_pitch_active = false;
		peek_target_pitch = 0.f;
		peek_current_pitch = 0.f;
		peek_pitch_update_counter = 0;
		peek_pitch_step = 0;
		peek_jitter_step = 0;
		peek_jitter_flip = false;
		return;  // Early return, don't process peek pitch while in defensive mode
	}

	if (should_use_peek_pitch && !peek_pitch_active)
	{
		peek_pitch_active = true;
		peek_pitch_update_counter = 0;
		peek_pitch_step = 0;
		peek_jitter_step = 0;
		peek_jitter_flip = false;
		last_peek_time = HACKS->global_vars->curtime;
	}
	else if (!should_use_peek_pitch && peek_pitch_active)
	{
		peek_pitch_active = false;
		peek_target_pitch = 0.f;
		peek_current_pitch = 0.f;
		peek_pitch_update_counter = 0;
		peek_pitch_step = 0;
		peek_jitter_step = 0;
		peek_jitter_flip = false;
	}
}
void c_anti_aim::cleanup()
{
	HACKS->cmd->viewangles = HACKS->cmd->viewangles.normalized_angle();
	HACKS->cmd->sidemove = std::clamp(HACKS->cmd->sidemove, -450.f, 450.f);
	HACKS->cmd->forwardmove = std::clamp(HACKS->cmd->forwardmove, -450.f, 450.f);
	HACKS->cmd->upmove = std::clamp(HACKS->cmd->upmove, -320.f, 320.f);

#ifdef LEGACY
	auto local_anim = ANIMFIX->get_local_anims();
	if (HACKS->client_state->choked_commands < 1) {
		local_anim->sent_eye_pos = HACKS->cmd->viewangles;
		return;
	}

	if (!*HACKS->send_packet)
		local_anim->sent_eye_pos = HACKS->cmd->viewangles;
#endif
}