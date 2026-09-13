#include "globals.hpp"
#include "resolver.hpp"
#include "animations.hpp"
#include "server_bones.hpp"
#include "ragebot.hpp"
#include "penetration.hpp"
#include "engine_prediction.hpp"
#include "rage_logger.hpp"
// OLD CODE (kept for revert):
// #include "neverlose.hpp"

namespace resolver
{
	static constexpr int RESOLVE_LOCK_TICKS = 16;

	void on_miss(int index, int failed_side)
	{
		if (index <= 0 || index >= 65)
			return;

		auto& info = resolver_info[index];

		// Brute force works with the precomputed side matrices, so drop any
		// custom (jitter-centre) yaw resolution.
		info.use_resolved_yaw = false;

		// OLD CODE (kept for revert):
		// const int failed = failed_side;
		// info.prev_failed_side = info.last_failed_side;
		// info.last_failed_side = failed;
		//
		// // Prefer the opposite side; if that was just tried too, fall back to zero
		// // so we cycle through all three sides instead of oscillating between two.
		// const int opposite = (failed == side_left) ? side_right :
		//	(failed == side_right) ? side_left : side_right;
		//
		// if (opposite != info.prev_failed_side)
		//	info.side = opposite;
		// else
		//	info.side = side_zero;
		//
		// Fatality-style blacklist brute (resolver.cpp:896-1099): blacklist the
		// candidate that missed and advance to the next untried candidate. When
		// every candidate has been tried, reset the list and start over.
		info.blacklist_side(failed_side);
		info.prev_failed_side = info.last_failed_side;
		info.last_failed_side = failed_side;

		if (info.is_blacklisted(side_left) && info.is_blacklisted(side_right) && info.is_blacklisted(side_zero))
			info.clear_blacklist();

		// OLD CODE (kept for revert):
		// info.side = info.pick_next_side();
		//
		// Furthest non-blacklisted candidate (maximum information gain) instead
		// of the next one in the fixed ring.
		info.side = info.pick_furthest_side(failed_side);

		info.resolved = true;
		info.mode = XOR("brute");
		info.anim_resolve_ticks = HACKS->global_vars->tickcount;
		info.last_miss_count = RAGEBOT->missed_shots[index];

		rage_log::line("[RES] idx=%d ON_MISS failed=%d new_side=%d misses=%d", index, failed_side, info.side, info.last_miss_count);
	}

	void on_hit(int index, int side)
	{
		if (index <= 0 || index >= 65)
			return;

		auto& info = resolver_info[index];

		// OLD CODE (kept for revert): a hit cleared the entire blacklist.
		// info.clear_blacklist();
		//
		// A real hit confirms THAT side only: clear its error counter and remove
		// it from the blacklist. Other sides keep their learning state.
		const int bi = info.blacklist_index(side);
		if (bi >= 0 && bi < 5)
			info.side_errors[bi] = 0;

		info.unblacklist_side(side);
		info.last_miss_count = 0;
		info.anim_resolve_ticks = HACKS->global_vars->tickcount;

		rage_log::line("[RES] idx=%d ON_HIT side=%d", index, side);
	}

	// Registered head->body hit (client aimed head, server scored body): learn
	// against the side that was used. Two strikes blacklists it until a real
	// head lands - this is the hit-side counterpart of the miss replay.
	void on_side_error(int index, int side)
	{
		if (index <= 0 || index >= 65)
			return;

		auto& info = resolver_info[index];
		const int bi = info.blacklist_index(side);
		if (bi < 0 || bi >= 5)
			return;

		if (++info.side_errors[bi] >= 2)
		{
			info.blacklist_side(side);

			if (info.side == side)
				info.side = info.pick_furthest_side(side);

			rage_log::line("[RES] idx=%d SIDE_ERROR side=%d -> blacklisted, new_side=%d", index, side, info.side);
		}
	}

	// Replay-driven miss: `chosen_side` is the candidate that actually hit the
	// aimed point client-side (preferred head hitgroup). Blacklist the failed
	// side and move straight to a candidate known to hit, instead of stepping
	// through the fixed ring.
	void on_miss_choose(int index, int failed_side, int chosen_side)
	{
		if (index <= 0 || index >= 65)
			return;

		auto& info = resolver_info[index];

		info.use_resolved_yaw = false;
		info.blacklist_side(failed_side);
		info.prev_failed_side = info.last_failed_side;
		info.last_failed_side = failed_side;

		if (info.is_blacklisted(side_left) && info.is_blacklisted(side_right) && info.is_blacklisted(side_zero))
			info.clear_blacklist();

		info.side = info.is_blacklisted(chosen_side) ? info.pick_furthest_side(failed_side) : chosen_side;

		info.resolved = true;
		info.mode = XOR("brute");
		info.anim_resolve_ticks = HACKS->global_vars->tickcount;
		info.last_miss_count = RAGEBOT->missed_shots[index];

		rage_log::line("[RES] idx=%d ON_MISS_CHOOSE failed=%d chosen=%d new=%d misses=%d",
			index, failed_side, chosen_side, info.side, info.last_miss_count);
	}

	static void prepare_jitter(c_cs_player* player, resolver_info_t& resolver_info, anim_record_t* current)
	{
		auto& jitter = resolver_info.jitter;

		jitter.yaw_cache[jitter.yaw_cache_offset] = math::normalize_yaw(current->eye_angles.y);
		int old_offset = jitter.yaw_cache_offset;
		jitter.yaw_cache_offset = (jitter.yaw_cache_offset + 1) % YAW_CACHE_SIZE;

		++jitter.samples;

		// first sample has no valid previous angle to diff against, skip it
		if (jitter.samples < 2)
			return;

		int prev_idx = (old_offset - 1 + YAW_CACHE_SIZE) % YAW_CACHE_SIZE;
		float diff = std::fabsf(math::normalize_yaw(jitter.yaw_cache[old_offset] - jitter.yaw_cache[prev_idx]));

		if (diff < 5.0f)
		{
			jitter.static_ticks = std::min(jitter.static_ticks + 1, 3);
			jitter.jitter_ticks = std::max(jitter.jitter_ticks - 1, 0);
		}
		else if (diff >= 20.0f)
		{
			jitter.jitter_ticks = std::min(jitter.jitter_ticks + 1, 3);
			jitter.static_ticks = std::max(jitter.static_ticks - 1, 0);
		}

		jitter.is_jitter = jitter.jitter_ticks > jitter.static_ticks;

		// ---- spin (continuous rotation) ----
		// Consistent signed deltas across >= 3 samples. An alternating jitter
		// fails the sign-consistency test, so it never enters spin mode.
		const int tick_now = HACKS->global_vars ? HACKS->global_vars->tickcount : 0;
		const int dt = std::clamp(tick_now - jitter.last_sample_tick, 1, 16);
		jitter.last_sample_tick = tick_now;

		const float signed_delta = math::normalize_yaw(jitter.yaw_cache[old_offset] - jitter.yaw_cache[prev_idx]);
		const float rate = signed_delta / static_cast<float>(dt);
		constexpr float SPIN_MIN_DELTA = 25.f;

		const bool same_sign = jitter.spin_streak == 0
			|| (signed_delta > 0.f) == (jitter.last_delta > 0.f);

		if (std::fabsf(signed_delta) >= SPIN_MIN_DELTA && same_sign)
			jitter.spin_streak = std::min(jitter.spin_streak + 1, 8);
		else
			jitter.spin_streak = 0;

		jitter.last_delta = signed_delta;

		// The server body lags the eye during a flip: a positive eye jump leaves
		// the body on the negative edge. Track the edge from the latest large
		// delta and hold it between flips (Fatality yaw_resolve parity).
		if (std::fabsf(signed_delta) > 30.f)
			jitter.flip_side = signed_delta > 0.f ? side_left : side_right;

		jitter.is_spin = jitter.spin_streak >= 3;
		if (jitter.is_spin)
		{
			jitter.spin_rate = rate;
			jitter.spin_predicted = math::normalize_yaw(current->eye_angles.y
				+ std::clamp(rate * 2.f, -90.f, 90.f));
		}
	}

	// Clear-cut resolution: a record with <= 1 choked command has NO fake body
	// (the last command was sent), so the networked eye yaw is the real body
	// yaw and brute forcing is unnecessary. This is the only sound fast-path
	// signal; everything else falls through to the jitter/LBY/layer/brute
	// logic below.
	static bool resolve_clear_cases(c_cs_player* player, anim_record_t* current, resolver_info_t& info)
	{
		if (!player || !current)
			return false;

		const int idx = player->index();
		if (idx <= 0 || idx >= 65)
			return false;

		// Only take the fast path when there are no misses yet - once shots
		// start missing, the full brute-force logic owns the decision.
		const int misses = RAGEBOT->missed_shots[idx];
		if (misses != 0)
			return false;

		// OLD CODE (kept for revert): choke < 2 alone was treated as "no fake".
		// It is not: a standstill LBY/desync body can hold +/-58 while sending
		// every tick, so this forced the network eye yaw and missed the head by
		// 1-2 hitboxes. Never override an active jitter detection, and require
		// positive no-fake evidence (moving, or the body aligned with the eye).
		if (info.jitter.is_jitter || info.jitter.is_spin)
			return false;

		// Only a body that is aligned with the eye is provably "no fake". A
		// moving target can still carry up to +/-max_rot of desync (the engine
		// clamps the body even while moving), so "moving" alone is NOT evidence:
		// the log showed mode=networked with lbyD=62.6 producing server misses.
		const float lby_delta = std::fabs(math::normalize_yaw(current->lby - current->eye_angles.y));
		const bool lby_aligned = lby_delta <= 5.f;

		if (current->choke < 2 && lby_aligned)
		{
			info.side = side_zero;
			info.resolved = true;
			info.mode = XOR("networked");
			info.resolved_eye_yaw = current->eye_angles.y;
			info.anim_resolve_ticks = HACKS->global_vars->tickcount;
			info.last_miss_count = 0;
			return true;
		}

		return false;
	}

	// Persisted CalcFootYaw (csgo_playeranimstate leak:2327-2377). The server
	// body yaw is stateful, so step it once per record:
	//   1) clamp the previous body to m_flEyeYaw +/- flTempYawMax
	//   2) while moving, ApproachAngle(eye); while still, ApproachAngle(LBY)
	// This replaces guessing a jitter body from an edge/mean heuristic.
	static void update_foot_tracker(c_cs_player* player, anim_record_t* current)
	{
		if (!player || !current)
			return;

		const int index = player->index();
		if (index <= 0 || index >= 65)
			return;

		auto& info = resolver_info[index];

		const float eye = math::normalize_yaw(current->eye_angles.y);
		const float lby = math::normalize_yaw(current->lby);

		float dt = HACKS->global_vars->interval_per_tick;
		if (info.foot_track_valid && current->sim_time > info.foot_track_sim)
			dt = std::clamp(current->sim_time - info.foot_track_sim,
				HACKS->global_vars->interval_per_tick, 0.25f);

		// flAimMatrixWidthRange (leak:2332-2337).
		const float speed = current->velocity.length_2d();
		const float speed_walk = speed / (CS_PLAYER_SPEED_RUN * CS_PLAYER_SPEED_WALK_MODIFIER);
		const float speed_duck = speed / (CS_PLAYER_SPEED_RUN * CS_PLAYER_SPEED_DUCK_MODIFIER);
		const float walk_run = std::clamp(speed / CS_PLAYER_SPEED_RUN, 0.f, 1.f);
		const float duck = std::clamp(current->duck_amt, 0.f, 1.f);

		float width = math::lerp(std::clamp(speed_walk, 0.f, 1.f), 1.f, math::lerp(walk_run, 0.8f, 0.5f));
		if (duck > 0.f)
			width = math::lerp(duck * std::clamp(speed_duck, 0.f, 1.f), width, 0.5f);

		const float yaw_max = 58.f * width;
		const float yaw_min = -58.f * width;

		// First sample: the LBY is the best available body estimate. (The record's
		// abs_angles is the client render/eye yaw, not the server foot yaw -
		// seeding from it kept the body on the eye and produced chest hits.)
		float foot = info.foot_track_valid ? info.foot_track_yaw : lby;

		// Step 1: clamp against the eye.
		const float eye_delta = math::angle_diff(eye, foot); // eye - foot
		if (eye_delta > yaw_max)
			foot = eye - std::fabs(yaw_max);
		else if (eye_delta < yaw_min)
			foot = eye + std::fabs(yaw_min);

		foot = math::normalize_yaw(foot);

		// Step 2: approach the eye while moving, else the LBY.
		if (current->flags.has(FL_ONGROUND))
		{
			if (speed > 0.1f)
				foot = math::approach_angle(eye, foot, dt * (30.f + 20.f * walk_run));
			else
				foot = math::approach_angle(lby, foot, dt * 100.f);
		}

		info.foot_track_yaw = math::normalize_yaw(foot);
		info.foot_track_sim = current->sim_time;
		info.foot_track_valid = true;
	}

	// Fatality resolver::wall_detect parity (resolver.cpp:322-452). Probe which
	// side of the enemy is exposed with AWP-style rays around both players and
	// pick the candidate whose head sits on that side. This is the strongest
	// geometric disambiguator when the enemy is at cover.
	static bool do_wall_detect(c_cs_player* player, anim_record_t* current, resolver_info_t& info)
	{
		if (!player || !current || !HACKS->local || !HACKS->weapon_info)
			return false;

		// Never re-resolve on a shot record (Fatality yaw_resolve guard).
		if (current->shooting)
			return false;

		const vec3_t eye_pos = HACKS->local->get_eye_position();
		vec3_t target_position = current->origin;
		target_position.z += 66.f;
		const float target_angle = math::calc_angle(eye_pos, target_position).y;

		auto rotated = [](const vec3_t& start, float yaw, float distance) -> vec3_t
		{
			const float rad = DEG2RAD(yaw);
			return vec3_t{ start.x + std::cos(rad) * distance, start.y + std::sin(rad) * distance, start.z };
		};

		const vec3_t local_left = rotated(eye_pos, math::normalize_yaw(target_angle - 90.f), 25.f);
		const vec3_t local_right = rotated(eye_pos, math::normalize_yaw(target_angle + 90.f), 25.f);
		const vec3_t enemy_left = rotated(target_position, math::normalize_yaw(target_angle - 90.f), 25.f);
		const vec3_t enemy_right = rotated(target_position, math::normalize_yaw(target_angle + 90.f), 25.f);

		auto can_hit_local = [&](vec3_t from, vec3_t point) -> bool
		{
			if (!from.valid() || !point.valid())
				return false;

			auto bullet = penetration::simulate(player, HACKS->local, from, point, false, true);
			return bullet.damage > 0 && bullet.traced_target == HACKS->local;
		};

		auto compare = [&](const vec3_t& from_left, const vec3_t& from_right, const vec3_t& left, const vec3_t& right) -> int
		{
			const bool left_hit = can_hit_local(from_left, left);
			const bool right_hit = can_hit_local(from_right, right);
			if (!left_hit && right_hit) return 1;
			if (!right_hit && left_hit) return 2;
			return 0;
		};

		int goal = compare(local_left, local_right, enemy_left, enemy_right);
		if (goal == 0)
			goal = compare(local_left, local_right, enemy_right, enemy_left);
		if (goal == 0)
			return false;

		// The "exposed" side must still be shootable from our eye (Fatality's
		// stage-two sanity check); otherwise the detection is bogus.
		if (can_hit_local(eye_pos, goal == 1 ? enemy_left : enemy_right))
			return false;

		const float exposed_angle = math::normalize_yaw(target_angle + (goal == 1 ? -90.f : 90.f));

		// Pick the candidate matrix whose head is closest to the exposed side.
		const int head_bone = 8;
		const int sides[3] = { side_left, side_zero, side_right };
		int best_side = side_zero;
		float best_diff = FLT_MAX;

		for (const int s : sides)
		{
			matrix3x4_t* matrix = nullptr;
			switch (s)
			{
			case side_left:  matrix = current->matrix_left.matrix; break;
			case side_right: matrix = current->matrix_right.matrix; break;
			default:         matrix = current->matrix_zero.matrix; break;
			}

			const vec3_t head = matrix[head_bone].get_origin();
			const float diff = std::fabs(math::normalize_yaw(math::calc_angle(current->origin, head).y - exposed_angle));
			if (diff < best_diff)
			{
				best_diff = diff;
				best_side = s;
			}
		}

		info.side = best_side;
		info.use_resolved_yaw = false;
		info.resolved = true;
		info.resolved_eye_yaw = current->eye_angles.y;
		info.mode = XOR("wall");
		info.anim_resolve_ticks = HACKS->global_vars->tickcount;
		info.last_miss_count = RAGEBOT->missed_shots[player->index()];
		return true;
	}

	void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* previous)
	{
		const int index = player->index();
		if (index <= 0 || index >= 65)
			return;

		auto& info = resolver_info[index];
		if (!HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive() || player->is_bot() || !g_cfg.rage.resolver)
		{
			if (info.resolved)
				info.reset();

			return;
		}

		auto state = player->animstate();
		if (!state)
		{
			if (info.resolved)
				info.reset();

			return;
		}

		auto hdr = player->get_studio_hdr();
		if (!hdr)
		{
			if (info.resolved)
				info.reset();

			return;
		}

		// Debug log: report resolver mode/side transitions, not every record.
		const std::string res_prev_mode = info.mode;
		const int res_prev_side = info.side;

		auto log_res_change = [&](const char* reason)
			{
				if (rage_log::enabled() && (res_prev_mode != info.mode || res_prev_side != info.side))
					rage_log::line("[RES] idx=%d %s mode=%s side=%d choke=%d misses=%d vel=%.0f lby=%.1f eye=%.1f lbyD=%.1f jit=%d jt=%d st=%d spin=%d rate=%.1f",
						index, reason, info.mode.c_str(), info.side, current->choke,
						RAGEBOT->missed_shots[index], current->velocity.length_2d(),
						current->lby, current->eye_angles.y,
						math::normalize_yaw(current->lby - current->eye_angles.y),
						info.jitter.is_jitter ? 1 : 0, info.jitter.jitter_ticks, info.jitter.static_ticks,
						info.jitter.is_spin ? 1 : 0, info.jitter.spin_rate);
			};

		// Advance the persisted body-yaw tracker for every record, before any
		// early return below, so it never desyncs from the record stream.
		update_foot_tracker(player, current);

		if (current->choke < 2)
			info.add_legit_ticks();
		else
			info.add_fake_ticks();

		prepare_jitter(player, info, current);

		// A spinning/jittering target is never "legit" even if it sends every
		// tick: choke < 2 does NOT mean the body yaw equals the eye yaw.
		if (info.is_legit() && !info.jitter.is_jitter && !info.jitter.is_spin)
		{
			info.resolved = false;
			info.mode = XOR("no fake");
			info.side = side_zero;
			info.resolved_eye_yaw = current->eye_angles.y;
			info.jitter.reset();
			return;
		}

		const int misses = RAGEBOT->missed_shots[player->index()];

		// Reset per-frame custom-yaw flag; detection paths below re-enable it.
		info.use_resolved_yaw = false;

		// ---- spin: continuous rotation resolves to the predicted eye ----
		// The body is pinned to a clamp edge while the eye rotates, so the best
		// estimate of the current body base is the predicted eye. Both clamp
		// edges remain available to the scan as candidates.
		if (info.jitter.is_spin)
		{
			// While the eye keeps rotating the body is pinned to the clamp edge
			// in the direction of rotation (positive eye delta -> body at
			// eye - max). Resolve to that edge instead of the predicted eye: the
			// eye itself is never the body for a spinner (log: spread/spin
			// misses).
			info.side = info.jitter.spin_rate > 0.f ? side_left : side_right;
			info.use_resolved_yaw = false;
			info.resolved_eye_yaw = current->eye_angles.y;
			info.resolved = true;
			info.mode = XOR("spin");
			info.anim_resolve_ticks = HACKS->global_vars->tickcount;
			info.last_miss_count = RAGEBOT->missed_shots[player->index()];
			log_res_change("spin");
			return;
		}

		// Fatality wall_detect before the stateful tracker: a clearly exposed
		// side is more reliable than a body estimate for a covered enemy.
		if (do_wall_detect(player, current, info))
		{
			log_res_change("wall");
			return;
		}

		// Leak-exact body: the persisted CalcFootYaw tracker models the server's
		// foot yaw statefully, so it is the authority whenever it is valid and we
		// are not brute-forcing. Placed AFTER spin so a continuously-rotating
		// body (which a single stateful value models poorly) uses the edge logic.
		if (info.foot_track_valid && misses == 0)
		{
			info.side = side_zero;
			info.use_resolved_yaw = true;
			info.resolved_eye_yaw = info.foot_track_yaw;
			info.resolved = true;
			info.mode = XOR("tracked");
			info.anim_resolve_ticks = HACKS->global_vars->tickcount;
			info.last_miss_count = misses;
			log_res_change("foot-track");
			return;
		}

		// OLD CODE (kept for revert):
		// // ---- Neverlose fast-path: clear moving/LBY cases resolve here ----
		// if (neverlose::fast_resolve(player, current, info))
		//	return;
		//
		// Clear-cut cases (<= 1 choked command) resolve here.
		if (resolve_clear_cases(player, current, info))
		{
			log_res_change("fast");
			return;
		}

		// Fatality parity (resolver.cpp:266): never re-resolve on a shot record.
		// When the enemy fires, the body is committed to the shot; changing the
		// side here only adds churn. The record keeps the side learned so far.
		if (current->shooting)
		{
			info.last_miss_count = misses;
			info.anim_resolve_ticks = HACKS->global_vars->tickcount;
			log_res_change("shot-freeze");
			return;
		}
		const int ticks_since_resolve = HACKS->global_vars->tickcount - info.anim_resolve_ticks;
		// Never lock a jittering target to the old side - the jitter body must
		// be re-resolved to the circular mean every record (Fatality parity).
		// Also never hold an eye-yaw (static/networked) resolve once the LBY has
		// moved off the eye: the body is now at the LBY edge and the stale eye
		// resolve is the choking-static miss (log: rtick lag=7 mode=static
		// lbyD=173.7). Non-zero brute sides keep the lock.
		const bool lby_still_aligned = std::fabsf(math::normalize_yaw(current->lby - current->eye_angles.y)) <= 5.f;
		const bool lock_active = info.resolved
			&& !info.jitter.is_jitter
			&& info.anim_resolve_ticks > 0
			&& misses == info.last_miss_count
			&& ticks_since_resolve >= 0
			&& ticks_since_resolve < RESOLVE_LOCK_TICKS
			&& (info.use_resolved_yaw || info.side != side_zero || lby_still_aligned);

		if (lock_active)
		{
			// A custom zero yaw (lby / lby-body) must keep tracking the LBY
			// target; only eye-derived resolutions track the current eye. The old
			// unconditional eye overwrite silently replaced the LBY yaw.
			if (info.use_resolved_yaw)
				info.resolved_eye_yaw = current->lby;
			else
				info.resolved_eye_yaw = current->eye_angles.y;

			return;
		}

		auto& jitter = info.jitter;
		// OLD CODE (kept for revert):
		// if (jitter.is_jitter && misses == 0)
		//
		// Fatality keeps resolving jitter to the centre even after misses; the
		// brute left/right fallback on a jittering target is what produced the
		// chest-hit / server-pose miss streaks in the logs.
		if (jitter.is_jitter)
		{
			// The LBY is the server's authoritative body yaw. When it is aligned
			// with the eye there is no fake body; when it is far (massive fake /
			// extended yaw boost - the log showed lbyD=-173 on a jumping enemy)
			// the body is at the LBY, not at the mean of the eye samples. Only a
			// genuine mid-range jitter uses the circular mean.
			const float jitter_lby_delta = math::normalize_yaw(current->lby - current->eye_angles.y);
			const float jitter_lby_mag = std::fabsf(jitter_lby_delta);

			if (jitter_lby_mag <= 5.f || jitter_lby_mag >= 90.f)
			{
				const bool aligned = jitter_lby_mag <= 5.f;

				if (aligned)
				{
					// Body yaw == LBY, and the LBY == the eye here, so the enemy
					// is NOT desynced. Forcing a clamp edge offset moved the aim
					// onto the chest while the head was clear (log: lbyD=0.0,
					// mode=jit-aligned side=right -> server hg=chest). Resolve to
					// the eye.
					info.side = side_zero;
					info.use_resolved_yaw = true;
					info.resolved_eye_yaw = current->eye_angles.y;
				}
				else
				{
					// CS:GO source (CalcFootYaw): m_flFootYaw is clamped to
					// m_flEyeYaw +/- flTempYawMax, so a far LBY does NOT put the
					// body at the LBY - it pins it to the clamp edge on the LBY's
					// side. Resolve to that edge explicitly instead of relying on
					// the downstream animstate clamp (which used the wrong eye).
					info.side = jitter_lby_delta > 0.f ? side_right : side_left;
					info.use_resolved_yaw = false;
					info.resolved_eye_yaw = current->eye_angles.y;
				}

				info.resolved = true;
				info.mode = aligned ? XOR("jit-aligned") : XOR("jit-lby");
				info.anim_resolve_ticks = HACKS->global_vars->tickcount;
				info.last_miss_count = misses;
				log_res_change("jitter-lby");
				return;
			}

			// The real body of a jittering enemy settles in the middle of the
			// jitter range, not at either extreme. Resolve to the circular
			// mean of the most recent eye yaws (ring buffer order).
			const int sample_count = std::min(jitter.samples, YAW_CACHE_SIZE);

			float sum_sin = 0.f, sum_cos = 0.f;
			for (int i = 0; i < sample_count; ++i)
			{
				const int idx = (jitter.yaw_cache_offset - 1 - i + YAW_CACHE_SIZE * 2) % YAW_CACHE_SIZE;
				const float sample = math::normalize_yaw(jitter.yaw_cache[idx]);
				sum_sin += std::sin(DEG2RAD(sample));
				sum_cos += std::cos(DEG2RAD(sample));
			}

			const float center = math::normalize_yaw(RAD2DEG(std::atan2f(sum_sin, sum_cos)));

			info.side = side_zero;
			info.use_resolved_yaw = true;
			info.resolved_eye_yaw = center;
			info.resolved = true;
			info.mode = XOR("jitter");
			info.anim_resolve_ticks = HACKS->global_vars->tickcount;
			info.last_miss_count = misses;
			log_res_change("jitter");
			return;
		}

		// LBY-based detection (stationary desync): the body approaches the LBY
		// (CS:GO source CalcFootYaw: ApproachAngle(m_flLowerBodyYawTarget, ...)
		// while still), so resolve to the LBY itself, not the full clamp edge.
		// Only trusted while we are not already brute-forcing from misses.
		if (misses == 0 && current->velocity.length_2d() < 5.f)
		{
			const float lby_delta = math::normalize_yaw(current->lby - current->eye_angles.y);
			const float lby_mag = std::fabsf(lby_delta);
			if (lby_mag > 5.f && lby_mag < 60.f)
			{
				info.side = side_zero;
				info.use_resolved_yaw = true;
				info.resolved = true;
				info.mode = XOR("lby");
				info.resolved_eye_yaw = current->lby;
				info.anim_resolve_ticks = HACKS->global_vars->tickcount;
				info.last_miss_count = misses;
				log_res_change("lby");
				return;
			}
		}

		if (previous && current->choke <= 8)
		{
			// ANIMATION_LAYER_MOVEMENT_MOVE (=6 in the CS:GO animstate enum) is
			// the locomotion layer; its cycle/playback depends on the real body
			// yaw, so the resolver side whose simulated layer best matches the
			// networked record is the correct one to use.
			auto& cur_move = current->layers[ANIMATION_LAYER_MOVEMENT_MOVE];

			if (cur_move.weight > 0.f
				&& static_cast<int>(cur_move.weight * 1000.f) == static_cast<int>(previous->layers[ANIMATION_LAYER_MOVEMENT_MOVE].weight * 1000.f))
			{
				const auto zero_delta = std::fabs(cur_move.cycle - current->matrix_zero.layers[ANIMATION_LAYER_MOVEMENT_MOVE].cycle);
				const auto left_delta = std::fabs(cur_move.cycle - current->matrix_left.layers[ANIMATION_LAYER_MOVEMENT_MOVE].cycle);
				const auto right_delta = std::fabs(cur_move.cycle - current->matrix_right.layers[ANIMATION_LAYER_MOVEMENT_MOVE].cycle);

				const auto zero_i = static_cast<int>(zero_delta * 1000.f);
				const auto left_i = static_cast<int>(left_delta * 1000.f);
				const auto right_i = static_cast<int>(right_delta * 1000.f);
				const auto min_i = std::min(zero_i, std::min(left_i, right_i));
				const int matches = (zero_i == min_i) + (left_i == min_i) + (right_i == min_i);

				if (matches == 1)
				{
					int side = side_zero;
					std::string mode = XOR("zero");
					if (left_i == min_i)
					{
						side = side_left;
						mode = XOR("left");
					}
					else if (right_i == min_i)
					{
						side = side_right;
						mode = XOR("right");
					}

					info.side = side;
					info.resolved = true;
					info.mode = std::move(mode);
					info.resolved_eye_yaw = current->eye_angles.y;
					info.anim_resolve_ticks = HACKS->global_vars->tickcount;
					info.last_miss_count = misses;
					log_res_change("layer");
					return;
				}
			}
		}

		if (misses > 0)
		{
			// OLD CODE (kept for revert):
			// info.mode = XOR("brute");
			info.mode = XOR("brute");

			// If the locked candidate has already been blacklisted, advance to
			// the furthest untried one (Fatality get_brute_angle parity).
			if (info.is_blacklisted(info.side))
				info.side = info.pick_furthest_side(info.side);
		}
		else
		{
			// Re-evaluate the body on every fallthrough, not only on the first
			// resolve: the old `!info.resolved` gate meant a wrong first guess
			// stuck forever and `lby-body` never ran (log: mode=lby-body=0).
			const float fallback_lby_delta = math::normalize_yaw(current->lby - current->eye_angles.y);

			if (std::fabsf(fallback_lby_delta) > 5.f)
			{
				info.side = side_zero;
				info.use_resolved_yaw = true;
				info.resolved_eye_yaw = current->lby;
				info.mode = XOR("lby-body");
			}
			else
			{
				info.side = side_zero;
				info.use_resolved_yaw = false;
				info.mode = XOR("static");
			}
		}

		info.resolved = true;
		// Jitter/spin/lby-body supply their own body base; only overwrite with
		// the eye yaw when the detector derived the body from the record.
		if (!info.use_resolved_yaw)
			info.resolved_eye_yaw = current->eye_angles.y;
		info.anim_resolve_ticks = HACKS->global_vars->tickcount;
		info.last_miss_count = misses;
		log_res_change("fallthrough");
	}

	void apply_side(c_cs_player* player, anim_record_t* current, int choke)
	{
		const int index = player->index();
		if (index <= 0 || index >= 65)
			return;

		auto& info = resolver_info[index];
		if (!g_cfg.rage.resolver || !HACKS->weapon_info || !HACKS->local || !HACKS->local->is_alive()
			|| !info.resolved || player->is_teammate(false))
			return;

		auto state = player->animstate();
		if (!state)
			return;

		// Jitter resolution supplies an explicit body yaw (the centre of the
		// jitter range); every other mode derives it from the record's eye yaw.
		const float eye_yaw = info.use_resolved_yaw
			? info.resolved_eye_yaw
			: (current ? current->eye_angles.y : info.resolved_eye_yaw);

		if (!info.use_resolved_yaw)
			info.resolved_eye_yaw = eye_yaw;

		// Sides are the engine's clamp edges: CalcFootYaw pins m_flFootYaw to
		// m_flEyeYaw +/- flTempYawMax. Drive abs_yaw well past the limit so the
		// animstate clamps it onto the exact edge for the record's own speed,
		// rather than relying on a possibly-stale get_max_rotation().
		if (info.side == side_zero)
			state->abs_yaw = math::normalize_yaw(eye_yaw);
		else
			state->abs_yaw = math::normalize_yaw(eye_yaw + (info.side > side_zero ? 180.f : -180.f));
	}
}
