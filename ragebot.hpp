#pragma once
#include "lagcomp.hpp"
#include "resolver.hpp"

constexpr int MAX_SCANNED_POINTS = 512;
constexpr float MATRIX_HEAD_ROTATION = 0.70710678f;
constexpr int MAX_SEEDS = 128;

using multipoint_t = std::pair<vec3_t, bool>;
using multipoints_t = std::vector<multipoint_t>;
using rage_bullet_t = std::pair<vec3_t, std::uint64_t>;

struct resolver_info_t;
inline std::vector<std::tuple<float, float, float>> precomputed_seeds{};

struct restore_record_t;

struct aim_shot_record_t
{
	bool fire{};
	bool impact_fire{};

	int safety{};
	int damage{};
	int hitgroup{};
	int hitbox{};
	// Damage the scan predicted for this shot (used by the debug log; `damage`
	// is reserved as the "-1 pending" sentinel for miss processing).
	int predicted_damage{};

	float time{};
	float init_time{};
	float hitchance{};

	int index{};

	// Tick the resolver last resolved this target vs the tick the shot was
	// fired (diagnostic: separates snapshot lag from a wrong side).
	int resolver_tick{};
	int fire_tick{};

	vec3_t start{};
	vec3_t impact{};
	vec3_t point{};

	c_cs_player* pointer{};

	anim_record_t record{};
	resolver_info_t resolver{};
};

struct rage_point_t
{
	bool found = false;
	bool center = false;
	bool extrapolated = false;
	bool predicted_eye_pos = false;

	int hitbox = 0;
	int damage = -1;
	int safety = 0;

	float accuracy = 0.f;
	float priority = 0.f;

	vec3_t aim_point{};
	anim_record_t* record = nullptr;

	INLINE void reset()
	{
		found = false;
		center = false;
		extrapolated = false;
		predicted_eye_pos = false;

		hitbox = 0;
		damage = 0;
		safety = 0;

		accuracy = 0.f;
		priority = 0.f;

		aim_point.reset();
		record = nullptr;
	}
};

struct rage_player_t
{
	bool valid = false;
	bool start_scans = false;

	float distance{};
	float cached_dynamic_scale = 0.f;
	int failed_scan_ticks = 0;
	// Ticks the corrected-origin gate has been failing for this target (auto
	// delay bound). Reset when a shot is committed.
	int shot_hold_ticks = 0;
	c_cs_player* player{};

	restore_record_t restore{};

	anim_record_t* hitscan_record{};
	anim_record_t* best_record{};

	rage_point_t best_point{};

	std::vector<rage_point_t> points_to_scan{};

	INLINE rage_player_t() {}

	INLINE rage_player_t(c_cs_player* player) : player(player) {
		distance = HACKS->local->origin().dist_to(player->origin());
	}

	INLINE void reset_hitscan()
	{
		start_scans = false;
		hitscan_record = nullptr;
		best_record = nullptr;
		cached_dynamic_scale = 0.f;

		best_point = {};

		points_to_scan.clear();
		points_to_scan.reserve(MAX_SCANNED_POINTS);
	}

	INLINE void reset()
	{
		valid = false;
		start_scans = false;

		distance = 0.f;
		cached_dynamic_scale = 0.f;
		failed_scan_ticks = 0;
		shot_hold_ticks = 0;
		player = nullptr;

		restore.reset();

		hitscan_record = nullptr;
		best_record = nullptr;

		best_point.reset();

		points_to_scan.clear();
	}
};

struct knife_point_t
{
	int damage{};
	vec3_t point{};
	anim_record_t* record{};
};

class c_ragebot
{
private:
	bool should_shot = true;
	bool reset_rage_hitscan = false;
	int rage_player_iter = 0;

	int last_checked = 0;
	float last_spawn_time = 0.f;
	int tick_cocked = 0;
	int tick_strip = 0;

	std::vector<int> hitboxes{};
	std::string debug_reason{};

	vec3_t predicted_eye_pos{};
	rage_player_t rage_players[65]{};

	std::vector<aim_shot_record_t> shots{};

	struct table_t
	{
		std::uint8_t swing[2][2][2]{};
		std::uint8_t stab[2][2]{};
	};

	const table_t knife_dmg{ { { { 25, 90 }, { 21, 76 } }, { { 40, 90 }, { 34, 76 } } }, { { 65, 180 }, { 55, 153 } } };
	std::array<vec3_t, 12 > knife_ang
	{
		vec3_t{ 0.f, 0.f, 0.f },
		vec3_t{ 0.f, -90.f, 0.f },
		vec3_t{ 0.f, 90.f, 0.f },
		vec3_t{ 0.f, 180.f, 0.f },
		vec3_t{ -80.f, 0.f, 0.f },
		vec3_t{ -80.f, -90.f, 0.f },
		vec3_t{ -80.f, 90.f, 0.f },
		vec3_t{ -80.f, 180.f, 0.f },
		vec3_t{ 80.f, 0.f, 0.f },
		vec3_t{ 80.f, -90.f, 0.f },
		vec3_t{ 80.f, 90.f, 0.f },
		vec3_t{ 80.f, 180.f, 0.f }
	};

	void update_hitboxes();
	void debug_draw_multipoints();
	multipoints_t get_points_legacy(c_cs_player* player, int hitbox, matrix3x4_t* matrix, anim_record_t* record);
	multipoints_t get_points_advanced(c_cs_player* player, int hitbox, matrix3x4_t* matrix, anim_record_t* record);
	INLINE float get_dynamic_scale_cached(rage_player_t* rage)
	{
		if (rage && rage->cached_dynamic_scale > 0.f)
			return rage->cached_dynamic_scale;

		auto velocity = HACKS->local->velocity();
		float speed = velocity.length_2d();
		return std::clamp(speed / 260.f, 0.f, 1.f);
	}
	INLINE void reset_rage_players()
	{
		for (auto& i : rage_players)
			i.reset();

		reset_rage_hitscan = false;
		rage_player_iter = 0;
	}

	void force_scope();
	bool should_stop(const rage_point_t& point);
	bool try_alternate_sides(rage_player_t& rage, rage_point_t& point, anim_record_t* record, const vec3_t& eye_pos, int damage, float chance, float& out_chance);
	void update_predicted_eye_pos();
	void prepare_players_for_scan();
	void scan_players();
	void choose_best_point();
	void add_shot_record(c_cs_player* player, const rage_point_t& best, anim_record_t* record, vec3_t eye_pos);

	void weapon_fire(c_game_event* event);
	void bullet_impact(c_game_event* event);
	void player_hurt(c_game_event* event);
	void round_start(c_game_event* event);

public:
	bool trigger_stop = false;
	bool firing{};
	bool working{};
	bool revolver_fire{};
	int missed_shots[65]{};
	int m_missed_anim_side[65]{};
	int m_missed_prev_side[65]{};
	// Head->body server mismatches per player (client aimed head, server scored
	// body). Drives the baim fallback; reset by a real head hit or a round.
	int m_head_body[65]{};
	rage_player_t best_rage_player{};
	rage_weapon_t rage_config{};

	// Pitch-adjusted eye computed once per frame on the main thread and read by
	// the scan workers, so scan and final-shot validation use the same origin.
	vec3_t scan_eye_pos{};

	// Original auto-scale used by the legacy multipoint generator.
	INLINE float get_dynamic_scale(const vec3_t& point, const float& hitbox_radius)
	{
		auto anim = ANIMFIX->get_local_anims();
		auto predicted_info = ENGINE_PREDICTION->get_networked_vars(HACKS->cmd->command_number);

		auto spread = predicted_info->spread + predicted_info->inaccuracy;
		auto distance = point.dist_to(anim->eye_pos);

		auto new_dist = distance / std::sin(DEG2RAD(90.f - RAD2DEG(spread)));
		auto scale = (hitbox_radius - new_dist * spread) + 0.1f;

		return std::clamp(scale, 0.f, 0.95f);
	}

	// Option C auto-scale: usable fraction of the hitbox radius after the
	// inaccuracy cone at the target distance.
	INLINE float get_cone_scale(const vec3_t& point, const float& hitbox_radius)
	{
		auto anim = ANIMFIX->get_local_anims();
		if (!anim || hitbox_radius <= 0.f)
			return 0.f;

		auto predicted_info = ENGINE_PREDICTION->get_networked_vars(HACKS->cmd->command_number);

		const float inaccuracy = predicted_info->spread + predicted_info->inaccuracy;
		const float distance = point.dist_to(anim->eye_pos);
		const float cone_radius = distance * std::tan(std::max(inaccuracy, 0.f));

		return std::clamp((hitbox_radius - cone_radius) / hitbox_radius, 0.f, 0.95f);
	}

	INLINE float get_head_scale(c_cs_player* player)
	{
		if (rage_config.scale_head != -1)
			return rage_config.scale_head * 0.01f;

		return 0.f;
	}

	INLINE float get_body_scale(c_cs_player* player)
	{
		if (rage_config.scale_body != -1)
			return rage_config.scale_body * 0.01f;

		return 0.f;
	}

	INLINE int get_min_damage(c_cs_player* player)
	{
		auto mindamage = g_cfg.binds[override_dmg_b].toggled ?
			rage_config.damage_override : rage_config.mindamage;

		auto health = player->health();
		if (mindamage > 99)
			return health + (mindamage - 100);

		if (mindamage > player->health())
			return health;

		return mindamage;
	}

	INLINE void reset()
	{
		trigger_stop = false;
		should_shot = false;
		reset_rage_hitscan = false;
		firing = false;
		working = false;
		revolver_fire = false;
		rage_player_iter = 0;
		last_checked = 0;
		last_spawn_time = 0.f;
		tick_cocked = 0;
		tick_strip = 0;

		for (auto& i : rage_players)
			i.reset();

		for (auto& i : missed_shots)
			i = 0;

		for (auto& i : m_missed_anim_side)
			i = 0;

		for (auto& i : m_missed_prev_side)
			i = 0;

		for (auto& i : m_head_body)
			i = 0;

		rage_config = {};
		best_rage_player.reset();
		predicted_eye_pos.reset();
		hitboxes.clear();
		shots.clear();
	}

	INLINE void build_seeds()
	{
		for (auto i = 0; i < MAX_SEEDS; i++)
		{
			math::random_seed(i + 1);

			const auto pi_seed = math::random_float(0.f, M_PI * 2);
			precomputed_seeds.emplace_back(math::random_float(0.f, 1.f), std::sin(pi_seed), std::cos(pi_seed));
		}
	}

	multipoints_t get_points(c_cs_player* player, int hitbox, matrix3x4_t* matrix, anim_record_t* record = nullptr);
	bool can_fire(bool ignore_revolver = false);
	bool is_shooting();
	void run_stop();
	void auto_pistol();
	void auto_revolver();
	bool knife_is_behind(c_cs_player* player, anim_record_t* record);
	bool knife_trace(vec3_t dir, bool stab, c_game_trace* trace);
	bool can_knife(c_cs_player* player, anim_record_t* record, vec3_t angle, bool& stab);
	void knife_bot();
	void on_game_events(c_game_event* event);
	void proceed_misses();
	void run();
};

#ifdef _DEBUG
inline auto RAGEBOT = std::make_unique<c_ragebot>();
#else
CREATE_DUMMY_PTR(c_ragebot);
DECLARE_XORED_PTR(c_ragebot, GET_XOR_KEYUI32);

#define RAGEBOT XORED_PTR(c_ragebot)
#endif