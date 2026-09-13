#pragma once
#include "animations.hpp"

struct restore_record_t
{
	vec3_t origin{};
	vec3_t abs_origin{};
	vec3_t eye_angles{};

	vec3_t mins{};
	vec3_t maxs{};

	matrix3x4_t bone_cache[128]{};

	INLINE void store(c_cs_player* player)
	{
		origin = player->origin();
		abs_origin = player->get_abs_origin();
		eye_angles = player->eye_angles();

		mins = player->bb_mins();
		maxs = player->bb_maxs();

		player->store_bone_cache(bone_cache);
	}

	INLINE void restore(c_cs_player* player)
	{
		player->origin() = origin;
		player->set_abs_origin(abs_origin);
		player->eye_angles() = eye_angles;

		player->bb_maxs() = maxs;
		player->bb_mins() = mins;

		player->invalidate_bone_cache();
		player->set_bone_cache(bone_cache);
	}

	INLINE void reset()
	{
		origin.reset();
		abs_origin.reset();

		mins.reset();
		maxs.reset();

		std::memset(bone_cache, 0, sizeof(bone_cache));
	}
};

class c_lag_comp
{
private:
	struct tick_validity_t {
		int last_valid_tick = -1;
		float last_valid_sim_time = 0.f;
		int tick_age = 0;
	};

	std::unordered_map<int, tick_validity_t> valid_ticks_per_player;

public:
	// Effective interpolation used by validity, bracket weighting and the fire
	// tickcount. 0 in the menu = engine lerp; a manual value overrides it.
	INLINE float get_interp_time() const
	{
		if (g_cfg.rage.interpolation_ticks > 0)
			return TICKS_TO_TIME(g_cfg.rage.interpolation_ticks);

		return HACKS->lerp_time;
	}

	INLINE bool is_tick_valid(bool shifting, bool break_lc, float sim_time, int player_index = -1)
	{
		// OLD CODE (kept for revert):
		// if (shifting || break_lc)
		// 	return false;
		//
		// Research: Fatality lag_compensation.cpp:279-317 (`valid_simtime`) does
		// not reject records for being out of order - the simtime window checks
		// below are the real authority. Only an actual lag-compensation break is
		// a hard reject here. `shifting` is kept for diagnostics only.
		(void)shifting;

		if (break_lc)
			return false;

		auto netchan = HACKS->engine->get_net_channel();
		if (!netchan)
			return false;

		if (HACKS->cl_lagcomp0)
			return true;

		if (!HACKS->client_state)
			return false;

		// Symmetric window around the ideal interpolation point. The one-sided
		// "server target" variant was tried and regressed hard (PRED 119k, shots
		// landing on extrapolated records) because our predicted_time is offset
		// from cmd->tick_count; keep the working formula.
		const int server_tick = HACKS->client_state->clock_drift_mgr.server_tick;
		const float rtt = HACKS->incoming > 0.f ? HACKS->incoming : HACKS->ping * 0.5f;
		const float max_unlag = HACKS->max_unlag > 0.f ? HACKS->max_unlag : 0.2f;
		const float correct = std::clamp(get_interp_time() + rtt, 0.f, max_unlag);

		const int possible_future_tick = server_tick + 1 + TIME_TO_TICKS(rtt) + 8;
		if (TIME_TO_TICKS(sim_time + get_interp_time()) > possible_future_tick)
			return false;

		const float dead_time = TICKS_TO_TIME(server_tick) + rtt - max_unlag;
		if (sim_time <= dead_time)
			return false;

		const float delta_time = std::fabs(correct - (HACKS->predicted_time - sim_time));

		const float backtrack_window = g_cfg.rage.backtrack
			? std::clamp(static_cast<float>(g_cfg.rage.backtrack_ms) / 1000.f, 0.f, max_unlag)
			: 0.f;
		const float tolerance = std::max(0.02f, backtrack_window);

		bool is_valid = delta_time < tolerance;

		if (is_valid && player_index >= 0)
		{
			valid_ticks_per_player[player_index].last_valid_tick = server_tick;
			valid_ticks_per_player[player_index].last_valid_sim_time = sim_time;
			valid_ticks_per_player[player_index].tick_age = 0;
		}
		else if (player_index >= 0)
		{
			valid_ticks_per_player[player_index].tick_age++;
		}

		return is_valid;
	}

	INLINE float get_lerp_time()
	{
		auto getf = [](c_convar* cv, float fallback) -> float
		{
			return cv ? cv->get_float() : fallback;
		};

		float ratio = getf(HACKS->convars.cl_interp_ratio, 1.f);
		float rate = getf(HACKS->convars.cl_updaterate, 64.f);
		const float interp = getf(HACKS->convars.cl_interp, 0.f);

		// Mirror Source's CBasePlayer::ClientSettingsChanged, which computes the
		// server's m_fLerpTime from the clamped client convars and rounds to ms.
		// Without this, cmd->tickcount and the validity window disagree with the
		// pose the server rewinds to - the dominant "all sides hit client-side,
		// server missed" miss class in the logs.
		if (HACKS->convars.sv_client_min_interp_ratio && HACKS->convars.sv_client_max_interp_ratio)
		{
			const float mn = HACKS->convars.sv_client_min_interp_ratio->get_float();
			const float mx = HACKS->convars.sv_client_max_interp_ratio->get_float();
			if (mx > 0.f)
				ratio = std::clamp(ratio, mn, mx);
		}

		if (HACKS->convars.sv_minupdaterate && HACKS->convars.sv_maxupdaterate)
		{
			const float mn = HACKS->convars.sv_minupdaterate->get_float();
			const float mx = HACKS->convars.sv_maxupdaterate->get_float();
			if (mx > 0.f)
				rate = std::clamp(rate, mn, mx);
		}

		float lerp = std::max(interp, ratio / std::max(rate, 1.f));
		lerp = std::round(lerp * 1000.f) / 1000.f;
		return lerp;
	}

	INLINE void set_record(c_cs_player* player, anim_record_t* record, matrix3x4_t* matrix)
	{
		if (!player || !record)
			return;

		auto anim = ANIMFIX->get_anims(player->index());
		if (!anim)
			return;

		// Extrapolated records were built at record->prediction.origin; the
		// collideable/OBB must follow the predicted bones, not the stale origin.
		const vec3_t origin = record->extrapolated ? record->prediction.origin : record->origin;

		player->origin() = origin;
		player->set_abs_origin(origin);

		player->bb_maxs() = record->maxs;
		player->bb_mins() = record->mins;

		player->invalidate_bone_cache();
		player->set_bone_cache(matrix);
	}

	INLINE bool validate_bone_positions(c_cs_player* player, matrix3x4_t* matrices)
	{
		if (!player || !matrices)
			return false;

		vec3_t head_pos = matrices[8].get_origin();
		vec3_t player_origin = player->get_abs_origin();

		float vertical_diff = std::fabs(head_pos.z - player_origin.z);
		// Head bone (8) sits ~64 above the origin standing and ~48-52 while
		// ducked; the old 50 floor rejected valid crouched records.
		if (vertical_diff < 40.f || vertical_diff > 100.f)
			return false;

		vec3_t player_mins = player->bb_mins();
		vec3_t player_maxs = player->bb_maxs();
		vec3_t head_delta = head_pos - player_origin;

		if (head_delta.x < player_mins.x || head_delta.x > player_maxs.x ||
			head_delta.y < player_mins.y || head_delta.y > player_maxs.y)
			return false;

		return true;
	}

	// Declarations only (implementations in .cpp)
	void build_roll_matrix(c_cs_player* player, matrix_t* side, int side_index, float& fresh_tick, vec3_t& fresh_angles, clamp_bones_info_t& clamp_info);
	void clamp_matrix(c_cs_player* player, matrix_t* side, float& fresh_tick, vec3_t& fresh_angles, clamp_bones_info_t& clamp_info);
	void update_tick_validation();
};

#ifdef _DEBUG
inline auto LAGCOMP = std::make_unique<c_lag_comp>();
#else
CREATE_DUMMY_PTR(c_lag_comp);
DECLARE_XORED_PTR(c_lag_comp, GET_XOR_KEYUI32);

#define LAGCOMP XORED_PTR(c_lag_comp)
#endif