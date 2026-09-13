#pragma once
#include "menu/ui_structs.h"
#include <array>

enum fov_t
{
	world = 0,
	arms,
	zoom
};

enum world_clr_t
{
	walls = 0,
	props,
	sky,
	fog,
	lights,
	world_clr_max,
};

enum weapon_group_t
{
	global = 0,
	auto_snipers,
	heavy_pistols,
	pistols,
	scout,
	awp,
	weapon_max
};

enum quick_stop_options_t
{
	early = 1,
	between_shots = 2,
	force_accuracy = 4,
	in_air = 8,
	force_duck = 16,
};

enum defensive_aa_condition_t
{
	def_aa_cond_tickbase_trigger = 1 << 0,
	def_aa_cond_tickbase_choke = 1 << 1,
	def_aa_cond_on_shot = 1 << 2,
	def_aa_cond_peek = 1 << 3,
	def_aa_cond_in_air = 1 << 4,
	def_aa_cond_in_crouch = 1 << 5,
	def_aa_cond_always = 1 << 6,
	def_aa_cond_all = def_aa_cond_tickbase_trigger | def_aa_cond_tickbase_choke | def_aa_cond_on_shot |
		def_aa_cond_peek | def_aa_cond_in_air | def_aa_cond_in_crouch | def_aa_cond_always,
};

enum removals_t
{
	scope = 1,
	vis_recoil = 2,
	post_process = 4,
	smoke = 8,
	flash = 16,
	fog_ = 32,
	shadow = 64,
	viewmodel_move = 128,
	landing_bob = 256,
};

enum rage_hitbox_t
{
	head = 1,
	chest = 2,
	stomach = 4,
	pelvis = 8,
	arms_ = 16,
	legs = 32
};

	enum binds_idx_t
{
	fd_b = 0,
	sw_b,
	dt_b,
	hs_b,
	ap_b,
	tp_b,
	inv_b,
	edge_b,
	left_b,
	right_b,
	back_b,
	override_dmg_b,
	ens_lean_b,
	spike_b,
	toggle_legit_b,
	force_roll_b,
	freestand_b,
	force_body_b,
	force_sp_b,
	ej_b,
	flick_b, // Flick bind
	binds_max
};

enum esp_type_t
{
	esp_enemy,
	esp_weapon,
	esp_max
};

#if _DEBUG || ALPHA || BETA
constexpr unsigned int esp_element_fake_duck = 1u << 10;
#else
constexpr unsigned int esp_element_fake_duck = 1u << 9;
#endif

enum chams_type_t
{
	c_vis,
	c_xqz,
	c_history,
	c_onshot,
	c_ragdolls,
	c_local,
	c_viewmodel,
	c_wpn,
	c_attachments,
	c_fake,
	c_gloves,
	c_max
};

enum weapon_cfg_type_t
{
	weapon_cfg_deagle = 0,
	weapon_cfg_duals,
	weapon_cfg_fiveseven,
	weapon_cfg_glock,
	weapon_cfg_ak47,
	weapon_cfg_aug,
	weapon_cfg_awp,
	weapon_cfg_famas,
	weapon_cfg_g3sg1,
	weapon_cfg_galil,
	weapon_cfg_m249,
	weapon_cfg_m4a1,
	weapon_cfg_m4a1s,
	weapon_cfg_mac10,
	weapon_cfg_p90,
	weapon_cfg_mp5sd,
	weapon_cfg_ump45,
	weapon_cfg_xm1014,
	weapon_cfg_bizon,
	weapon_cfg_mag7,
	weapon_cfg_negev,
	weapon_cfg_sawedoff,
	weapon_cfg_tec9,
	weapon_cfg_p2000,
	weapon_cfg_mp7,
	weapon_cfg_mp9,
	weapon_cfg_nova,
	weapon_cfg_p250,
	weapon_cfg_scar20,
	weapon_cfg_sg556,
	weapon_cfg_ssg08,
	weapon_cfg_usps,
	weapon_cfg_cz75,
	weapon_cfg_revolver,
	weapon_cfg_knife,
	weapon_cfg_max
};

struct chams_t
{
	bool enable{};
	bool interp{};

	int material = 0;
	int glow_fill = 100;
	int shot_duration = 5;

	c_float_color main_color = (255, 255, 255, 255);
	c_float_color glow_color = (255, 255, 255, 255);

	char chams_sprite[128]{};

	INLINE chams_t(bool enable, bool interp, int mat, int fill, int shot, c_float_color main, c_float_color glow)
	: enable(enable), interp(interp), material(mat), glow_fill(fill), shot_duration(shot), main_color(main), glow_color(glow) {
		std::strcpy(chams_sprite, CXOR("models\\weapons\\customization\\paints\\custom\\money"));
	}

	INLINE chams_t() {
		std::strcpy(chams_sprite, CXOR("models\\weapons\\customization\\paints\\custom\\money"));
	}
};

struct key_binds_t;

struct rage_weapon_t
{
	bool enable{};
	bool quick_stop{};
	bool auto_scope{};
	bool strict_mode{};
	int hitchance{};
	int mindamage = 1;
	int damage_override = 1;
	int scale_head = -1;
	int scale_body = -1;
	int group_type{};
	int hitbox_type{};

	bool prefer_body;
	bool prefer_safe;

	unsigned int hitboxes{};
	unsigned int hitchance_skips{ 1 | 2 };
	unsigned int quick_stop_options{};

	std::array< unsigned int, 6 > hitbox_cond{};
};

struct legit_weapon_t
{
	bool enable{};
	bool backtrack{};
	bool quick_stop{};
	bool fov_cicle{};
	int fov{};
	int smooth{};
	int aim_delay{};
	unsigned int hitboxes{};

	c_float_color circle_color = c_float_color(255, 255, 255, 255);
};

struct skin_weapon_t
{
	bool enable{};
	int skin{};
	int knife_model{};
	int old_model = -1;
	int old_skin = -1;

	char skins_search[256]{};
};

struct configs_t
{
	struct ragebot_t
	{
		bool enable{};
		// Defensive mode: 0=Disabled, 1=On Peek, 2=Always in Air, 3=Always On
		int defensive_mode{};
		bool auto_fire{};
		bool resolver{};
		bool delay_lc{};
		bool multipoint_debug{};
		// Advanced multipoint is the only generator now (toggle removed).
		bool multipoint_advanced = true;
		bool debug_hitscan{};
		// Detailed shot/miss diagnostics written to
		// Documents\Ryze\ryze_rage_debug.log (falls back to %TEMP%).
		bool debug_log{};
		bool backtrack = true;
		// Maximum historical rewind for backtrack, in milliseconds. Clamped to
		// sv_maxunlag at runtime; 0 disables historical records.
		int backtrack_ms = 200;

		// Forward extrapolation of stale records. OFF only works when the enemy
		// is not choking: a record older than sv_maxunlag is rejected by the
		// backtrack validity test, so against a heavy choker there would be no
		// points and the ragebot would not fire at all. Default ON and dynamic
		// (ping/choke driven); the tick value is only an optional hard cap.
		bool extrapolation = true;
		int extrapolation_ticks = 0;    // 0 = auto (ping/choke), 1..16 = manual cap
		// Manual interpolation override (ticks) used for lag compensation.
		// 0 = auto (cl_interp / cl_interp_ratio).
		int interpolation_ticks = 0;    // 0..16
		// Fallback to body hitboxes when the target's resolver detects heavy
		// jitter (the head side is a coin flip against jitter).
		bool baim_on_jitter = true;
		// Bounded hold: when the corrected-origin gate keeps failing, wait at
		// most auto_delay_max ms for a validating moment, then commit.
		bool auto_delay{};
		int auto_delay_max = 80;
#if ALPHA || _DEBUG || BETA
		bool jitterfix{};
		int jitterfix_method{};
		//bool delay_lc{};
		bool round_lagcomp{};
		bool delay_on_breaklc{};
#endif

		int spike_amt{};
		int roll_amt = 50;
		int roll_amt_pitch = 0;

		int group_type{};

		std::array< rage_weapon_t, 6 > weapon{};
	} rage;

	struct legitbot_t
	{
		bool enable{};
		bool smoke_check{};
		bool flash_check{};
		bool autowall{};
		int min_damage{};
		int group_type{};

		struct
		{
			bool enable{};
			int amount{};
		} rcs;

		std::array< legit_weapon_t, weapon_cfg_max > legit_weapon{};
	} legit;


	struct anti_hit_t
	{
		anti_hit_t() {
			def_aa_enable = true;
			def_aa_phase_count = 4;
			custom_yaw = 0;
			is_default_config = true;  // NEW: Track if this is a fresh config
			def_aa_phases[0] = { 4, 0, 45, 1, 89 };   // Jitter, random pitch
			def_aa_phases[1] = { 4, 1, 90, 0,  0 };   // Random yaw, static 0
			def_aa_phases[2] = { 4, 2, 90, 4, 89 };   // Spin, switch pitch
			def_aa_phases[3] = { 4, 3,  0, 5, 89 };   // Back, 3-way pitch
		}

		// ========== CONFIG STATE ==========
		bool is_default_config = true;  // NEW: Tracks if this is default/fresh config (for reset button)

		// ========== MASTER TOGGLES ==========
		bool enable{};
		bool antiam{};

		int yaw_rpm = 100;
		// Slow walk speed as a percentage of max weapon speed (34 = old default).
		int slow_walk_speed = 34;
		// ========== DEFENSIVE AA - MASTER ==========
		bool def_aa_enable = true;              // master defensive AA toggle
		bool def_aa_dt_active = false;          // enable defensive AA while DT is active
		int def_aa_dt_mode = 0;                 // 0=On Shot, 1=Peek, 2=In Air, 3=Always, 4=In Crouch
		unsigned int def_aa_conditions = def_aa_cond_tickbase_trigger;
		int def_aa_mode = 0;                    // 0=Trigger, 1=Always On
		bool def_pitch = true;                  // Override pitch while defensive AA is active
		bool def_yaw = true;                    // Override yaw while defensive AA is active
		int def_aa_random_factor = 3;           // 1-10, scales jitter/random aggression
		int ticks_defensive{};
		int def_aa_air_minimum_ticks = 2;       // NEW: Minimum ticks in air before def AA triggers (prevents false positives)

		// ========== DEFENSIVE AA - PHASES ==========
		constexpr static int DEF_AA_MAX_PHASES = 4;
		struct def_aa_phase_t
		{
			int duration = 4;           // ticks this phase lasts
			int yaw_mode = 0;           // 0=Jitter, 1=Random, 2=Spin, 3=Back, 4=At Targets, 5=Fake Side, 6=Alternating, 7=Lean
			int yaw_angle = 45;         // jitter range, spin speed, fake side offset, etc.
			int pitch_mode = 0;         // 0=Static, 1=Random, 2=Up, 3=Down, 4=Switch, 5=3-way
			int pitch_angle = 89;       // target pitch value / pitch range
			int pitch_angle2 = 89;
			int spin_rpm = 100;
			bool spin_clockwise = true;
			int spin_inverter_mode = 0; // 0=none, 1=on land, 2=on recharge, 3=on shot
			int yaw_rpm = 100;             // Used for spin mode
		};
		int def_aa_phase_count = 1;             // 1–4 active phases
		def_aa_phase_t def_aa_phases[DEF_AA_MAX_PHASES] = {};
		int def_aa_yaw_preset = 0;              // 0=Rotate, 1=Left, 2=Right, 3=Center, 4=Random
		int def_aa_rotate_ticks = 2;
		int def_aa_yaw_left = -60;
		int def_aa_yaw_right = 60;
		int def_aa_yaw_back = 0;
		bool def_aa_random_yaw = false;
		int def_aa_random_yaw_min = -60;
		int def_aa_random_yaw_max = 60;
		int def_aa_pitch = -89;
		bool def_aa_random_pitch = false;
		int def_aa_random_pitch_min = -89;
		int def_aa_random_pitch_max = 89;

		// ========== PITCH MODES (Global & Per-State) ==========
		int pitch = 0;                          // 0=Off, 1=Down, 2=Up, 3=Custom, 4=Random, 5=Switch, 6=3-way, 7=Flick, 8=Dynamic
		int custom_pitch = 0;                   // Used when pitch mode = 3 (Custom)
		int random_pitch = -89;                 // Min pitch for random mode
		int random_pitch2 = 89;                 // Max pitch for random mode
		int switch_pitch = -89;                 // Pitch 1 for switch/flick/dynamic
		int switch_pitch2 = 89;                 // Pitch 2 for switch/flick/dynamic
		int way_pitch = -89;                    // Pitch 1 for 3-way mode
		int way_pitch2 = 45;                    // Pitch 2 for 3-way mode
		bool pitch_desync = true;               // Real pitch on choke, fake pitch on send
		int pitch_fake = -89;                   // Sent/fake pitch when pitch_desync is on
		int pitch_flick_speed = 1;              // 0=Fast, 1=Medium, 2=Slow, 3=Random
		int pitch_dynamic_type = 0;             // 0=Smooth, 1=Step, 2=Jitter

		// ========== YAW MODES (Global & Per-State) ==========
		int yaw{};                              // 0=Off, 1=Backwards, 2=Side Snap, 3=Random, 4=Custom, 5=Spin, 6=3-way, 7=Sideways, 8=Fake side
		int custom_yaw{};                       // Used when yaw mode = 4 (Custom)
		int yaw_add{};                          // Offset for yaw modes
		int manual_offset = 0;                  // Applied to manual left/right/back
		int manual_jitter = 0;                  // Jitter applied only while manual bind active (0-20 degrees)
		bool manual_swap_left_right = false;    // Swap left/right directions

		// ========== FREESTANDING ==========
		// Method used to pick the real (server) yaw while the freestanding bind
		// is held. 0=Fatality, 1=Wall scan, 2=Face away, 3=Velocity peek,
		// 4=Hybrid, 5=Jitter sides, 6=Custom.
		int freestand_mode = 0;
		int freestand_offset = 0;            // Extra yaw offset on the picked side (-90..90)
		int freestand_hold = 8;              // Ticks a direction is held before it may switch
		int freestand_smooth = 45;           // Max real-yaw degrees per tick (0=instant)
		int freestand_jitter = 0;            // +/- degrees oscillated around the picked yaw
		int freestand_dist = 25;             // Sample distance for side/cover points (8..60)
		int freestand_fov = 180;             // Enemy selection FOV (10..180)
		int freestand_custom = 0;            // Real yaw for Custom mode, relative to view (-180..180)
		bool freestand_avoid_flick = true;   // Hold the side + rate-limit turns

		// ========== SPIN YAW SETTINGS ==========
		int spin_speed = 10;                    // Degrees per tick
		bool spin_clamp = true;                 // true = limit to ±90°, false = continuous 360

		// ========== AT TARGETS ==========
		bool at_targets_enabled = false;
		int at_targets_offset = 0;              // Offset from target angle (-180..180)
		bool at_targets_center = false;         // If true, ignore target and set yaw to 0
		bool at_targets = false;                // Legacy flag (kept for compatibility)

		// ========== JITTER MODES (Global & Per-State) ==========
		int jitter_mode{};                      // 0=Off, 1=Binary, 2=Half, 3=Random, 4=Stepped, 5=Progressive, 6=Delayed
		int jitter_range{};                     // Jitter amount in degrees (-60..60)
		bool random_jitter{};                   // Randomize jitter flipper
		bool adaptive_jitter = false;           // Scale jitter based on velocity
		int adaptive_jitter_scale = 45;         // Scale factor (0-100%)

		// ========== DESYNC (Global & Per-State) ==========
		bool desync{};                          // Master desync toggle
		int desync_mode{};                      // 0=Default, 1=Jitter
		int desync_left = 58;                   // Left desync amount
		int desync_right = 58;                  // Right desync amount
		bool random_dsy{};                      // Randomize desync flipper
		bool random_amount{};                   // Randomize desync amounts (0-58)

		// ========== EXTENDED FAKE / DISTORTION ==========
		bool distortion{};                      // Master extended fake toggle
		int distortion_range = 100;             // Roll angle amount (0-100%)
		int distortion_pitch{};                 // Height/pitch of distortion (0-50)

		// ========== FAKELAG ==========
		bool fakelag{};                         // Master fakelag toggle
		int fakelag_limit = 14;                 // Max choke amount in ticks
		unsigned int fakelag_conditions{};      // Bitflags: Standing, Moving, Air
		bool fluctiate_in_air{};                // Fluctuate fakelag while airborne
		bool jitter_move{};                     // Apply jitter to movement

		// ========== PEEK PITCH ==========
		bool peek_pitch_enable = false;
		int peek_pitch_phase = 2;               // 0=On Peek, 1=On Return, 2=Both
		int peek_pitch_mode = 0;                // 0=Up, 1=Down, 2=Zero, 3=Custom, 4=Meowmode
		int peek_custom_pitch = 0;              // Custom pitch value (-89..89)
		int peek_jitter_mode = 0;               // 0=Off, 1=Binary, 2=Progressive, 3=Random, 4=Stepped
		int peek_jitter_amount = 0;             // Jitter amount for peek pitch

		// ========== FLICK / DESYNC BAIT ==========
		bool flick_enable = false;
		int flick_interval = 4;                 // Idle ticks between flicks (1-64)
		int flick_duration = 1;                 // How many ticks the flick lasts (0-63)
		int flick_yaw = 45;                     // Yaw offset during flick (-60..60 degrees)

		// ========== AIR OVERRIDE ==========
		bool air_override = false;              // Use different AA settings while airborne
		int air_pitch = 0;                      // Pitch mode for air
		int air_yaw = 0;                        // Yaw mode for air
		int air_yaw_add = 0;                    // Yaw offset for air
		int air_jitter_mode = 0;                // Jitter mode for air
		int air_jitter_range = 0;               // Jitter range for air
		int air_desync_left = 58;               // Desync left for air
		int air_desync_right = 58;              // Desync right for air

		// ========== VELOCITY / MOVEMENT BASED AA ==========
		bool velocity_yaw_bias = false;         // Bias yaw towards movement direction
		int velocity_yaw_bias_amount = 35;      // Bias strength (0-100%)

		// ========== LANDING PITCH ==========
		bool landing_pitch = false;             // Apply special pitch when landing
		int landing_pitch_ticks = 4;            // Duration to apply landing pitch (1-16 ticks)
		int landing_pitch_value = -89;          // Pitch value during landing (-89..89)

	} antihit;

	struct visuals_t
	{
		chams_t chams[c_max]{
		  chams_t{ false, false, 0, 100, 5, c_float_color(150, 200, 60), c_float_color(80, 78, 138, 150) },        // C_VIS
		  chams_t{ false, false, 0, 100, 5, c_float_color(60, 120, 180), c_float_color(80, 78, 138, 150) },         // C_XQZ
		  chams_t{ false, false, 3, 100, 5, c_float_color(169, 166, 255, 80), c_float_color(80, 78, 138, 80) },     // C_HISTORY
		  chams_t{ false, false, 4, 100, 5, c_float_color(0, 0, 0, 0), c_float_color(102, 92, 138, 100) },          // C_ONSHOT
		  chams_t{ false, false, 0, 100, 5, c_float_color(108, 126, 184), c_float_color(80, 78, 138, 150) },        // C_RAGDOLLS
		  chams_t{ false, false, 3, 100, 5, c_float_color(148, 168, 255, 100), c_float_color(100, 100, 100, 150) }, // C_LOCAL
		  chams_t{ false, false, 3, 100, 5, c_float_color(169, 125, 255, 100), c_float_color(168, 169, 255, 150) }, // C_VIEWMODEL
		  chams_t{ false, false, 3, 100, 5, c_float_color(218, 125, 255, 100), c_float_color(100, 100, 100, 150) }, // C_WPN
		  chams_t{ false, false, 3, 100, 5, c_float_color(218, 125, 255, 100), c_float_color(100, 100, 100, 150) }, // C_ATTACHMENTS
		  chams_t{ false, false, 2, 100, 5, c_float_color(80, 160, 255, 160), c_float_color(0, 0, 0, 0) },          // C_FAKE (flat desync body)
		};

		struct event_logs_t
		{
			bool enable{};
			bool filter_console{};
			unsigned int logs{};

			
			bool kill_say_enable{};   // whether to say an insult on kill
			int kill_say_style{};     // 0..2 style selector
		} eventlog;

		struct esp_t
		{
			bool enable{};
			bool nade_offscreen{};
			unsigned int elements{};

			int offscreen_size{ 15 };
			int offscreen_dist{ 250 };

			struct esp_colors_t
			{
				c_float_color box = c_float_color(255, 255, 255, 255);
				c_float_color name = c_float_color(255, 255, 255, 255);
				c_float_color health = c_float_color(177, 255, 155);
				c_float_color weapon = c_float_color(255, 255, 255, 255);
				c_float_color ammo_bar = c_float_color(80, 140, 200);
				c_float_color offscreen_arrow = c_float_color(139, 166, 226);
				c_float_color offscreen_arrow_outline = c_float_color(0, 0, 0);
				c_float_color glow = c_float_color(180, 60, 120, 130);
				c_float_color skeleton = c_float_color(255, 255, 170, 130);

				c_float_color smoke_range = c_float_color(94, 112, 251, 255);
				c_float_color molotov_range = c_float_color(251, 94, 204, 255);
			} colors;
		};

		std::array< esp_t, esp_max > esp{};

		bool grenade_predict{};

		c_float_color predict_clr = c_float_color(156, 150, 255, 255);

		bool grenade_warning{};
		bool grenade_warning_line{};

		c_float_color warning_clr = c_float_color(156, 200, 255, 255);

		bool local_glow{};
		c_float_color local_glow_color = c_float_color{ 255, 158, 158, 130 };
		bool sound_esp = false;
		c_float_color sound_esp_clr = c_float_color{ 255, 158, 158, 130 };
		bool show_all_history{};
	} visuals;

	struct misc_t
	{
		bool menu = false;

		bool auto_jump{};
		bool auto_strafe{};

		bool fast_stop{};
		bool slide_walk{};

		int fovs[3] = { 0, 0, 100 };
		int viewmodel_pos[3] = {};

		bool viewmodel_scope{};
		bool penetration_crosshair{};
		bool fix_sensitivity{};
		bool skip_second_zoom{};

		int aspect_ratio{};
		int strafe_smooth = 70;

		int thirdperson_dist = 150;
		bool thirdperson_dead{};

		bool blend_scope{};
		int scope_amt = 50;
		int attachments_amt = 100;

		bool impacts{};

		c_float_color server_clr = c_float_color(0, 0, 255, 127);
		c_float_color client_clr = c_float_color(255, 0, 0, 127);

		int impact_size = 10;

		unsigned int tracers{};
		int tracer_type{};

		c_float_color trace_clr[3] = { c_float_color(150, 130, 255), c_float_color(150, 130, 255), c_float_color(150, 130, 255) };

		bool custom_bloom{};
		int bloom_scale{};
		int exposure_min{};
		int exposure_max{};

		bool custom_fog{};
		int fog_start{};
		int fog_end = 9300;
		int fog_density = 70;

		char skybox_name[128]{};
		int skybox = 0;
		int prop_alpha = 100;

		unsigned int world_material_options{};
		unsigned int world_modulation{};

		struct
		{
			int x = 0, y = -140, z = 0;
		} sunset_angle;

		bool pen_xhair{};
		bool retrack_peek{};

		bool retrack_peek_halo{};
		c_float_color retrack_peek_halo_clr = c_float_color(255, 255, 255, 180);


		c_float_color texture_reflect_clr = { 255, 255, 255, 255 };
		c_float_color autopeek_clr_back = c_float_color(143, 205, 253, 60);
		c_float_color autopeek_clr = c_float_color(139, 135, 253, 60);

		c_float_color world_clr[world_clr_max]
		{
			c_float_color(43, 41, 46),
			c_float_color(255, 255, 255),
			c_float_color(255, 255, 255),
			c_float_color(255, 255, 255),
			c_float_color(255, 255, 255),
		};

		unsigned int hitmarker = 0;
		c_float_color hitmarker_clr = c_float_color(255, 255, 255);

		bool damage{};
		c_float_color damage_clr = c_float_color(150, 113, 220);

		int sound{};
		int sound_volume = 50;
		char sound_name[128]{};

		bool unlock_inventory{};
		bool snip_crosshair{};
		bool preverse_killfeed{};
		bool remove_ads{};
		bool force_radar{};

		int ragdoll_gravity{};

		bool clantag{};
		bool bypass_sv_pure{};
		bool unlock_hidden_cvars{};
		bool force_crash{};

		unsigned int removals{};

		struct buy_bot_t
		{
			bool enable{};
			int main_weapon{};
			int second_weapon{};
			unsigned int other_items{};
		} buybot;

		unsigned int menu_indicators = 1 | 2 | 4;

		ImVec2 keybind_position{};
		ImVec2 bomb_position{};
		ImVec2 watermark_position{};
		ImVec2 spectators_position{};
		ImVec2 aa_state_position{};

		c_float_color ui_color{ 168, 168, 255 };
	} misc;

	struct skinchanger_t
	{
		int model_t{};
		int model_ct{};
		int group_type{};

		int model_glove{};
		int glove_skin{};

		int masks{};

		char custom_model_ct[128] = "";
		char custom_model_t[128] = "";

		std::array< skin_weapon_t, weapon_cfg_max > skin_weapon{};
	} skins;

	std::array< key_binds_t, binds_max > binds = {
		  key_binds_t{ -1, 1, false, CXOR("Fake duck") },
#ifdef LEGACY
		  key_binds_t{ -1, 1, false, CXOR("Fake walk") },
#else
		  key_binds_t{ -1, 1, false, CXOR("Slow walk") },
#endif
		  key_binds_t{ -1, 2, false, CXOR("Double tap") },
		  key_binds_t{ -1, 2, false, CXOR("Hide shots") },
		  key_binds_t{ -1, 2, false, CXOR("Auto peek") },
		  key_binds_t{ -1, 2, false, CXOR("Thirdperson") },
		  key_binds_t{ -1, 2, false, CXOR("Inverter") },
		  key_binds_t{ -1, 2, false, CXOR("Edge yaw") },
		  key_binds_t{ -1, 2, false, CXOR("Manual left") },
		  key_binds_t{ -1, 2, false, CXOR("Manual right") },
		  key_binds_t{ -1, 2, false, CXOR("Manual back") },
		  key_binds_t{ -1, 2, false, CXOR("Override dmg") },
		  key_binds_t{ -1, 1, false, CXOR("Force extend") },
		  key_binds_t{ -1, 1, false, CXOR("Ping spike") },
		  key_binds_t{ -1, 1, false, CXOR("Aim (legit)") },
		  key_binds_t{ -1, 1, false, CXOR("Roll override") },
		  key_binds_t{ -1, 1, false, CXOR("Freestanding") },
		  key_binds_t{ -1, 1, false, CXOR("Force body") },
		  key_binds_t{ -1, 1, false, CXOR("Force safe") },
		  key_binds_t{ -1, 1, false, CXOR("Edge jump") },
		  key_binds_t{ -1, -1, false, CXOR("Flick") } // Flick bind (Disabled by default)
	};

	INLINE void reset_init()
	{
		for (auto& i : binds)
			i.reset2();
	}
};
constexpr unsigned int REMOVAL_RAGDOLL = 1 << 1; // Use next available bit
extern configs_t g_cfg;
