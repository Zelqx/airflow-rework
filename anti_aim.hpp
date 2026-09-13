#pragma once

// Anti-aim state enumeration for per-state settings
enum aa_state_t
{
	AA_STATE_STANDING = 0,
	AA_STATE_MOVING = 1,
	AA_STATE_RUNNING = 2,
	AA_STATE_SLOW_WALK = 3,
	AA_STATE_CROUCH = 4,
	AA_STATE_IN_AIR = 5,
	AA_STATE_IN_AIR_CROUCH = 6,
	AA_STATE_MAX = 7,
	AA_STATE_UNKNOWN = -1
};
struct aa_state_settings_t {
	bool enabled = true;   // NEW – per state toggle
	int pitch = 0;
	int custom_pitch = 0;
	int random_pitch = -89;
	int random_pitch2 = 89;
	int switch_pitch = -89;
	int switch_pitch2 = 89;
	int way_pitch = -89;
	int way_pitch2 = 45;
	int yaw = 0;           // will use 0-9 for the new unified list
	int yaw_add = 0;
	int custom_yaw = 0;   // NEW – for the "Custom" yaw mode
	int jitter_mode = 0;
	int jitter_range = 0;
	bool random_jitter = false;
	bool desync = true;
	int desync_mode = 0;
	int desync_left = 58;
	int desync_right = 58;
	bool random_desync = false;
};
struct head_position_t
{
	float angle;
	float fraction;
	vec3_t position;
	vec3_t end_position;
};

class c_adaptive_angle
{
public:
	float yaw{};
	float distance{};

public:
	INLINE c_adaptive_angle(float yaw, float penalty = 0.f) : yaw(math::normalize_yaw(yaw))
	{
		distance = 0.f;
		distance -= penalty;
	}
};
// In anti_aim.hpp - add this helper
struct aa_context_t {
	bool manual_override = false;
	bool edge_override = false;
	bool freestand_override = false;
	bool is_defensive = false;
};

struct active_aa_t {
	int pitch;
	int custom_pitch;
	int random_pitch;
	int random_pitch2;
	int switch_pitch;
	int switch_pitch2;
	int way_pitch;
	int way_pitch2;
	int yaw;
	int custom_yaw;
	int yaw_add;
	int jitter_mode;
	int jitter_range;
	int desync_left;
	int desync_right;
	bool desync_enabled;
	int spin_speed;
	bool spin_clamp;
};

class c_anti_aim
{
private:
	int air_ticks = 0;  // NEW: Track consecutive frames in air
private:
    void apply_pitch_angle(const active_aa_t& active);

    void apply_manual_yaw();

    void apply_normal_yaw(const active_aa_t& active);

    void apply_defensive_yaw(const active_aa_t& active);

    void update_fake_side();
private:
	int current_state = AA_STATE_UNKNOWN;
	int previous_state = AA_STATE_UNKNOWN;
	int state_change_tick = 0;

	float jitter_current = 0.f;
	float jitter_target = 0.f;

	float held_real_pitch = 89.f;
	float held_fake_pitch = -89.f;
	int pitch_cycle_step = 0;
	int pitch_send_count = 0;

	bool fake_ducking{};
	bool defensive_aa{};
	bool can_duck{};
	bool edging{};
	// Set by freestanding() when it found a clear cover side. When false the
	// caller falls back to the configured yaw mode instead of the raw view.
	bool freestanding_has_direction{};

	// Freestanding stabilisation (anti-flick): the picked side is held for
	// freestand_hold ticks, the target is sticky, and the applied yaw can be
	// rate-limited so a side flip sweeps behind the enemy.
	bool freestand_active{};
	float freestand_hold_offset{};
	float freestand_smooth_offset{};
	float freestand_sweep_sign{};
	int freestand_hold_tick{};
	int freestand_target_index = -1;
	float freestand_logged_yaw{};
	int freestand_logged_mode = -1;

	bool flip_side{};
	bool flip_jitter{};
	bool flip_move{};

	int fake_side{};
	int shot_cmd{};
	float fake_angle{};
	float start_yaw{};
	float best_yaw{};

	float real_yaw{};
	float real_pitch{};
	int landing_pitch_ticks = 0;
	bool was_on_ground = true;

	// Peek pitch
	bool peek_pitch_active{};
	float peek_target_pitch{};
	float peek_current_pitch{};
	float last_peek_time{};
	int peek_pitch_update_counter{};
	int peek_pitch_step{};
	int peek_jitter_step{};
	bool peek_jitter_flip{};
	float peek_jitter_target{};
	std::vector<int> hitbox_list = {
		HITBOX_HEAD, HITBOX_CHEST, HITBOX_STOMACH, HITBOX_PELVIS,
		HITBOX_LEFT_FOOT, HITBOX_RIGHT_FOOT,
	};
	bool random_dsy_flipper = false;
	bool random_flipper = false;
	void fake_duck();
	void slow_walk();
	void force_move();
	void extended_fake();
	void manual_yaw();
	void automatic_edge();
	void freestanding();
	void freestanding_legacy();
	c_cs_player* freestanding_find_target(float& target_angle, const vec3_t& eye_pos);
	float freestanding_fatality(c_cs_player* target, float target_angle, const vec3_t& eye_pos);
	float freestanding_wall_yaw(const vec3_t& eye_pos);
	float freestanding_wall_dir(const vec3_t& eye_pos, bool& found);
	float freestanding_velocity(c_cs_player* target, float target_angle);
	float freestanding_hybrid(c_cs_player* target, float target_angle, const vec3_t& eye_pos);
	float freestanding_cover_yaw(c_cs_player* target, const vec3_t& eye_pos, float prefer_center);
	bool freestanding_peek_cover(float target_angle, const vec3_t& eye_pos, float& cover_yaw);
	float freestanding_edge(c_cs_player* target, float target_angle, const vec3_t& eye_pos);
	float freestanding_face_away(c_cs_player* target, float target_angle, const vec3_t& eye_pos);
	float freestanding_peek_edge(c_cs_player* target, float target_angle, const vec3_t& eye_pos);
	float freestanding_edge_jitter(c_cs_player* target, float target_angle, const vec3_t& eye_pos);
	void freestanding_apply(float desired_yaw, float target_angle);
	bool at_targets();
	// Peek pitch calculation
	float calculate_peek_pitch(c_cs_player* closest_enemy);
	void update_peek_pitch_mode();

	// State detection and helpers
	aa_state_t detect_player_state();
	// void update_state();
public:
	INLINE void reset()
	{
		fake_ducking = false;
		defensive_aa = false;
		can_duck = false;
		edging = false;
		air_ticks = 0;
		freestanding_has_direction = false;
		freestand_active = false;
		freestand_hold_offset = 0.f;
		freestand_smooth_offset = 0.f;
		freestand_sweep_sign = 0.f;
		freestand_hold_tick = 0;
		freestand_target_index = -1;
		freestand_logged_yaw = 0.f;
		freestand_logged_mode = -1;
		flip_side = false;
		flip_jitter = false;
		flip_move = false;

		fake_side = 0;
		shot_cmd = 0;

		fake_angle = 0.f;
		start_yaw = 0.f;
		best_yaw = 0.f;
		real_yaw = 0.f;	
		real_pitch = 0.f;
		jitter_current = 0.f;
		held_real_pitch = 89.f;
		held_fake_pitch = -89.f;
		pitch_cycle_step = 0;
		pitch_send_count = 0;

		peek_pitch_active = false;
		peek_target_pitch = 0.f;
		peek_current_pitch = -89.f;
		last_peek_time = 0.f;
		peek_pitch_update_counter = 0;
		peek_pitch_step = 0;
		peek_jitter_step = 0;
		peek_jitter_flip = false;
		landing_pitch_ticks = 0;
		was_on_ground = true;
		current_state = AA_STATE_UNKNOWN;
		previous_state = AA_STATE_UNKNOWN;
	}
	void fake();
	c_cs_player* get_closest_player(bool skip = false, bool local_distance = false);
	bool is_peeking();
	bool is_fake_ducking();
	aa_state_t get_current_state() const { return (aa_state_t)current_state; }

	void run_movement();
	void run();
	void cleanup();
};

#ifdef _DEBUG
inline auto ANTI_AIM = std::make_unique<c_anti_aim>();
#else
CREATE_DUMMY_PTR(c_anti_aim);
DECLARE_XORED_PTR(c_anti_aim, GET_XOR_KEYUI32);

#define ANTI_AIM XORED_PTR(c_anti_aim)
#endif
