#pragma once
#include "animations.hpp"   // now contains enums
#include "penetration.hpp"
#include <deque>
#include <vector>

// ------------------------------------------------------------------
// Shot_t – defined here
// ------------------------------------------------------------------
struct shot_t {
    int enemy_index = -1;
    int tick = 0;
    int cmdnum = 0;
    vec3_t shotpos{};
    vec3_t original_shotpos{};
    vec3_t hitpos{};
    vec3_t dir{};
    anim_record_t record{};
    resolver_direction state = resolver_networked;
    bool hurt = false;
    bool hit = false;
    bool hit_originally = false;
    bool hit_extrapolation = false;
    int hitgroup = -1;
    int damage = 0;
    int safety = 0;
    int hitbox = -1;
    weapon_info_t weapon_data{};
    std::vector<vec3_t> hitposes{};
    std::vector<vec3_t> penetration_points{};
    struct record_shot_info_t {
        bool extrapolated = false;
        int backtrack_ticks = 0;
        float hitchance = 0.f;
    } shot_info{};
};

// ------------------------------------------------------------------
// Per‑player resolver state
// ------------------------------------------------------------------
constexpr int CACHE_SIZE = 2;
constexpr int YAW_CACHE_SIZE = 8;
constexpr auto MAX_TICKS = 5;

enum detected_cheat_id : int
{
    detected_cheat_unknown,
    detected_cheat_gamesense,
    detected_cheat_neverlose,
    detected_cheat_onetap,
    detected_cheat_fatality,
    detected_cheat_nixware,
    detected_cheat_pandora,
    detected_cheat_plague,
    detected_cheat_evolve,
    detected_cheat_rifk,
    detected_cheat_airflow
};

struct resolver_info_t
{
    bool initialized = false;   // set to false in reset()
    bool resolved{};
    bool m_was_first_bruteforce{};
    bool m_was_second_bruteforce{};
    int side{};   // simulate_side_t: left_extra / left / zero / right / right_extra
    detected_cheat_id detected_cheat = detected_cheat_unknown;
    float resolved_eye_yaw = 0.f;   // store record's eye yaw at resolve time
    // When true, the "zero" side resolves to resolved_eye_yaw (used for the
    // jitter resolver, whose correct body yaw is the jitter centre rather than
    // either extreme).
    bool use_resolved_yaw{};
    int brute_index{};
    int legit_ticks{};
    int fake_ticks{};
    int anim_resolve_ticks{};
    int last_miss_count{};
    int last_failed_side = side_zero;
    int prev_failed_side = side_zero;

    // Persisted CalcFootYaw state (csgo_playeranimstate leak): the server body
    // yaw is stateful (clamp against the eye, then ApproachAngle toward the eye
    // while moving / the LBY while still). Track it per player across records
    // instead of guessing per record.
    float foot_track_yaw{};
    float foot_track_sim{};
    bool foot_track_valid{};

    // Fatality-style candidate blacklist (research: Fatality resolver.cpp
    // :896-1099 `get_brute_angle`): candidates that did not reproduce the
    // recorded shot are skipped until the list resets.
    bool side_blacklist[5]{};

    INLINE int blacklist_index(int s) const
    {
        int idx = s + 2;
        if (idx < 0) idx = 0;
        if (idx > 4) idx = 4;
        return idx;
    }

    INLINE void blacklist_side(int s) { side_blacklist[blacklist_index(s)] = true; }
    INLINE void clear_blacklist() { for (auto& b : side_blacklist) b = false; }
    INLINE bool is_blacklisted(int s) const { return side_blacklist[blacklist_index(s)]; }

    INLINE int pick_next_side() const
    {
        // Only the two true clamp edges and zero are reachable: side *_extra
        // (eye +/- 2*max) is clamped back to the same edge by CalcFootYaw, so
        // treating it as a separate candidate stalls the brute blacklist.
        static const int order[3] = { side_left, side_right, side_zero };

        for (int i = 0; i < 3; ++i)
        {
            if (!is_blacklisted(order[i]))
                return order[i];
        }

        return side_zero;
    }

    // Furthest candidate from `from` -> maximum information gain. Avoids stepping
    // to an adjacent, geometrically similar angle (Fatality advances to the
    // furthest non-blacklisted direction, resolver.cpp:966-1025).
    INLINE int pick_furthest_side(int from) const
    {
        static const int order[3] = { side_left, side_right, side_zero };

        int best = side_zero;
        int best_dist = -1;

        for (const int s : order)
        {
            if (is_blacklisted(s))
                continue;

            const int dist = s > from ? s - from : from - s;
            if (dist > best_dist)
            {
                best_dist = dist;
                best = s;
            }
        }

        return best;
    }

    INLINE void unblacklist_side(int s) { side_blacklist[blacklist_index(s)] = false; }

    // Per-side head->body server errors. Two body hits from the same side while
    // aiming head blacklist that side until a real head lands (learning from
    // registered hits, the counterpart of the miss replay).
    int side_errors[5]{};

    INLINE void add_legit_ticks()
    {
        if (legit_ticks < MAX_TICKS)
            ++legit_ticks;
        if (fake_ticks > 0)
            --fake_ticks;
    }

    INLINE void add_fake_ticks()
    {
        if (fake_ticks < MAX_TICKS)
            ++fake_ticks;
        if (legit_ticks > 0)
            --legit_ticks;
    }

    INLINE bool is_legit()
    {
        return legit_ticks >= MAX_TICKS && fake_ticks == 0;
    }
    std::string mode{};
    c_animation_layers initial_layers[13]{};
    // ---- jitter ----
    struct jitter_info_t {
        bool is_jitter{};
        float yaw_cache[YAW_CACHE_SIZE]{};
        int yaw_cache_offset{};
        int jitter_ticks{};
        int static_ticks{};
        int samples{};

        // Continuous rotation (spin): 3+ consecutive same-sign deltas with a
        // consistent magnitude. The predicted eye is used as the resolved base.
        bool is_spin{};
        float spin_rate{};       // signed degrees per tick
        float spin_predicted{};  // predicted current eye yaw
        float last_delta{};
        int spin_streak{};
        int last_sample_tick{};

        // Server body edge inferred from the last large eye flip: the body lags
        // the eye, so a positive eye jump leaves the body on the negative edge.
        // Held between flips so a jittering target does not oscillate per tick.
        int flip_side{ side_right };

        __forceinline void reset() {
            is_jitter = false;
            yaw_cache_offset = 0;
            jitter_ticks = 0;
            static_ticks = 0;
            samples = 0;
            std::memset(yaw_cache, 0, sizeof(yaw_cache));

            is_spin = false;
            spin_rate = 0.f;
            spin_predicted = 0.f;
            last_delta = 0.f;
            spin_streak = 0;
            last_sample_tick = 0;
            flip_side = side_right;
        }
    } jitter;

    // ---- freestanding ----
    struct freestanding_t {
        bool updated{};
        int side{};
        float update_time{};
        inline void reset() { updated = false; side = 0; update_time = 0.f; }
    } freestanding{};
#ifdef LEGACY
    int lby_breaker{};
    int lby_update{};

    struct move_t
    {
        float time{};
        float lby{};

        inline void reset()
        {
            time = 0.f;
            lby = 0.f;
        }
    } move{};

    struct lby_flicks_t
    {
        bool lby_breaker_failed = false;

        float last_lby_value = 0.0f;
        float next_lby_update = 0.0f;

        int logged_lby_delta_score = 0;
        float logged_lby_delta = 0.0f;

        c_animation_layers old_layers[13]{};

        inline void reset()
        {
            lby_breaker_failed = false;
            last_lby_value = 0.f;
            next_lby_update = 0.f;

            logged_lby_delta_score = 0;
            logged_lby_delta = 0.f;

            for (auto& i : old_layers)
                i = {};
        }
    } lby{};
#endif
    anim_record_t record{};

    // ---- reset ----
    inline void reset() {
        initialized = false;
        resolved = false;
        side = side_zero;
        resolved_eye_yaw = 0.f;
        use_resolved_yaw = false;
        brute_index = 0;
        //detected_cheat = detected_cheat_unknown;
        legit_ticks = 0;
        fake_ticks = 0;
        anim_resolve_ticks = 0;
        last_miss_count = 0;
        last_failed_side = side_zero;
        prev_failed_side = side_zero;
        foot_track_yaw = 0.f;
        foot_track_sim = 0.f;
        foot_track_valid = false;
        clear_blacklist();
        for (auto& e : side_errors)
            e = 0;
        m_was_first_bruteforce = false;
        m_was_second_bruteforce = false;

        mode = "";
        freestanding.reset();
        jitter.reset();
#ifdef LEGACY
        lby_breaker = 0;
        lby_update = 0;
        lby.reset();
        move.reset();
        record.reset();
#endif

        for (auto& i : initial_layers)
            i = {};
    }
};

inline resolver_info_t resolver_info[65]{};

// ------------------------------------------------------------------
// Resolver namespace
// ------------------------------------------------------------------
namespace resolver
{

    INLINE void reset() {
        for (auto& i : resolver_info) i.reset();
    }

    extern void prepare_side(c_cs_player* player, anim_record_t* current, anim_record_t* last);
    extern void apply_side(c_cs_player* player, anim_record_t* current, int choke);
    extern void on_miss(int index, int failed_side);
    // Replay-driven choice: pick the candidate that actually hits the aimed point
    // (used when the played side failed client-side).
    extern void on_miss_choose(int index, int failed_side, int chosen_side);
    // Registered head->body hit: learn against that side.
    extern void on_side_error(int index, int side);
    extern void on_hit(int index, int side);
}
