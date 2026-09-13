#include "globals.hpp"
#include "bullet_tracers.hpp"
#include "animations.hpp"

#include <playsoundapi.h>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <windows.h>
#include <mmsystem.h>
#include <string>
#include <algorithm>
#pragma comment(lib, "Winmm.lib")

// Global sound cache to avoid reprocessing the same sound data repeatedly
std::unordered_map<std::string, std::pair<std::vector<std::uint8_t>, float>> sound_cache{};

// Helper function to check file extension
INLINE bool ends_with(const std::string& str, const std::string& suffix)
{
    if (str.size() < suffix.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

INLINE void draw_beam(const vec3_t& start, const vec3_t& end, c_color clr)
{
    const auto name = XOR("sprites/purplelaser1.vmt");

    beam_info_t beam_info;
    beam_info.type = BEAM_NORMAL;
    beam_info.model_name = name.c_str();
    beam_info.model_index = -1;
    beam_info.start = start;
    beam_info.end = end;
    beam_info.life = 3.f;
    beam_info.fade_lenght = .1f;
    beam_info.halo_scale = 0.f;
    beam_info.amplitude = 1.f;
    beam_info.segments = 2;
    beam_info.renderable = true;
    beam_info.brightness = clr.a();
    beam_info.red = clr.r();
    beam_info.green = clr.g();
    beam_info.blue = clr.b();
    beam_info.speed = 1.f;
    beam_info.start_frame = 0;
    beam_info.frame_rate = 0.f;
    beam_info.width = 3;
    beam_info.end_width = 3;
    beam_info.flags = BEAM_ONLY_NO_IS_ONCE | BEAM_FADE_IN | BEAM_NOTILE;

    beam_t* final_beam = HACKS->view_render_beams->create_beam_points(beam_info);

    if (final_beam)
        HACKS->view_render_beams->draw_beam(final_beam);
}

INLINE bool read_file_to_memory(const std::string& fname, std::vector<std::uint8_t>& buffer)
{
    std::ifstream file(fname, std::ios::binary);
    if (!file)
    {
        printf("Error: Unable to open file: %s\n", fname.c_str());
        return false;
    }

    // Move to the end of the file to get the size
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Resize buffer and read file content
    buffer.resize(file_size);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);

    if (!file)
    {
        printf("Error: Failed to read file: %s\n", fname.c_str());
        buffer.clear();
        return false;
    }

    file.close();
    return true;
}

INLINE void modify_volume_wav_sound(std::vector<std::uint8_t>& buffer, float volume)
{
    if (buffer.size() < 8)
        return;

    // Find the "data" chunk in WAV file
    size_t data_offset = 0;
    for (size_t i = 0; i < buffer.size() - 4; i++)
    {
        if (buffer[i] == 'd' && buffer[i + 1] == 'a' && buffer[i + 2] == 't' && buffer[i + 3] == 'a')
        {
            data_offset = i;
            break;
        }
    }

    if (data_offset == 0 || data_offset + 8 >= buffer.size())
        return;

    // Get the size of the data chunk
    DWORD sample_bytes = *(DWORD*)(buffer.data() + data_offset + 4);
    DWORD samples = sample_bytes / 2; // Assuming 16-bit audio

    // Make sure we don't go out of bounds
    if (data_offset + 8 + sample_bytes > buffer.size())
        return;

    SHORT* sample = (SHORT*)(buffer.data() + data_offset + 8);
    for (DWORD i = 0; i < samples; i++)
    {
        float in_sample = sample[i] / 32768.0f;
        float scaled = in_sample * volume;

        // Proper clamping to prevent distortion
        if (scaled > 1.0f) scaled = 1.0f;
        if (scaled < -1.0f) scaled = -1.0f;

        int final_sample = static_cast<int>(scaled * 32767.0f);
        sample[i] = static_cast<SHORT>(final_sample);
    }
}

// Function to play WAV sound from memory
INLINE void play_wav_from_memory(const std::vector<std::uint8_t>& buffer, float volume)
{
    if (buffer.empty())
        return;

    // For WAV files, we can use PlaySound directly
    PlaySoundA(reinterpret_cast<const char*>(buffer.data()), NULL, SND_ASYNC | SND_MEMORY);
}

// Function to play an MP3 file using MCI commands
INLINE void play_mp3_from_file(const std::string& fname, float volume)
{
    int mciVolume = static_cast<int>(volume * 1000);
    std::string alias = "mp3player";

    // Close any previous instance with the same alias
    mciSendStringA("close mp3player", NULL, 0, NULL);

    std::string openCmd = "open \"" + fname + "\" type mpegvideo alias " + alias;
    if (mciSendStringA(openCmd.c_str(), NULL, 0, NULL) != 0)
    {
        printf("Error: Failed to open MP3 file: %s\n", fname.c_str());
        return;
    }

    std::string setVolumeCmd = "setaudio " + alias + " volume to " + std::to_string(mciVolume);
    mciSendStringA(setVolumeCmd.c_str(), NULL, 0, NULL);

    std::string playCmd = "play " + alias + " from 0";
    mciSendStringA(playCmd.c_str(), NULL, 0, NULL);
}

// Function to play OGG file using MCI (treat as MP3 for simplicity)
INLINE void play_ogg_from_file(const std::string& fname, float volume)
{
    // MCI might not support OGG natively, so we'll use the same approach as MP3
    play_mp3_from_file(fname, volume);
}

// Unified sound playing function
INLINE void play_sound_file(const std::string& file_path, float volume)
{
    std::string extension = file_path.substr(file_path.find_last_of("."));
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    if (extension == ".wav")
    {
        // Check cache first
        auto cache_key = file_path + "_" + std::to_string(volume);
        auto it = sound_cache.find(cache_key);

        if (it != sound_cache.end())
        {
            // Use cached version
            play_wav_from_memory(it->second.first, volume);
        }
        else
        {
            // Load and process WAV file
            std::vector<std::uint8_t> buffer;
            if (read_file_to_memory(file_path, buffer))
            {
                // Create volume-adjusted version and cache it
                std::vector<std::uint8_t> volume_adjusted = buffer;
                modify_volume_wav_sound(volume_adjusted, volume);

                sound_cache[cache_key] = { volume_adjusted, volume };
                play_wav_from_memory(volume_adjusted, volume);
            }
        }
    }
    else if (extension == ".mp3")
    {
        play_mp3_from_file(file_path, volume);
    }
    else if (extension == ".ogg")
    {
        play_ogg_from_file(file_path, volume);
    }
    else
    {
        printf("Error: Unsupported audio format: %s\n", file_path.c_str());
    }
}

// Function to play sound from memory (for fallback sounds)
INLINE void play_sound_from_memory(uint8_t* bytes, size_t size, float volume)
{
    // For built-in sounds, just play them directly
    PlaySoundA((char*)bytes, NULL, SND_ASYNC | SND_MEMORY);
}

void c_bullet_tracers::on_player_hurt(c_game_event* event)
{
    if (std::strcmp(event->get_name(), CXOR("player_hurt")) || !HACKS->local)
        return;

    auto attacker = HACKS->engine->get_player_for_user_id(event->get_int(CXOR("attacker")));
    if (HACKS->local->index() != attacker)
        return;

    auto user_id = HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid")));
    auto player = (c_cs_player*)HACKS->entity_list->get_client_entity(user_id);

    if (!player || player->is_teammate())
        return;

    float volume = 0.2f * (g_cfg.misc.sound_volume * 0.01f);

    if (g_cfg.misc.sound != 0)
    {
        auto play_custom_sound = [&]()
            {
                if (g_cfg.misc.sound == 1 || !g_cfg.misc.sound_name[0]) // Check if first character is null (empty string)
                    return false;
                return true;
			};

        if (play_custom_sound())
        {
            // Try different extensions
            std::vector<std::string> extensions = { ".wav", ".mp3", ".ogg" };
            bool sound_played = false;

            for (const auto& ext : extensions)
            {
                std::string sound_path = config::sounds_folder + "\\" + g_cfg.misc.sound_name + ext;
                if (main_utils::file_exist(sound_path.c_str()))
                {
                    printf("Playing custom sound: %s\n", sound_path.c_str());
                    play_sound_file(sound_path, volume);
                    sound_played = true;
                    break;
                }
            }

            if (!sound_played)
            {
                printf("Error: Custom sound file not found with any supported extension\n");
                // Fallback to default sound
                printf("Playing fallback sound.\n");
                play_sound_from_memory((uint8_t*)gamesense_sound, sizeof(gamesense_sound), volume);
            }
        }
        else
        {
            // Play default gamesense sound
            printf("Playing fallback sound.\n");
            play_sound_from_memory((uint8_t*)gamesense_sound, sizeof(gamesense_sound), volume);
        }
    }

    if (!g_cfg.misc.hitmarker && !g_cfg.misc.damage)
    {
        if (!hitmarkers.empty())
            hitmarkers.clear();

        return;
    }

    hitmarker_t best_impact{};
    auto origin = player->get_abs_origin();

    auto best_impact_distance = -1.f;
    auto time = HACKS->system_time();

    for (int i = 0; i < impacts.size(); i++)
    {
        auto& iter = impacts[i];

        if (time > iter.impact_time + 3.f)
        {
            impacts.erase(impacts.begin() + i);
            continue;
        }

        float distance = iter.pos.dist_to(origin);
        if (distance < best_impact_distance || best_impact_distance == -1)
        {
            best_impact_distance = distance;
            best_impact = iter;
        }
    }

    if (best_impact_distance == -1)
        return;

    auto& hit = hitmarkers.emplace_back();
    hit.dmg = event->get_int(CXOR("dmg_health"));
    hit.time = time;
    hit.dmg_time = time;
    hit.alpha = 1.f;
    hit.pos = best_impact.pos;
    hit.hp = player->health();
}

void c_bullet_tracers::on_bullet_impact(c_game_event* event)
{
    if (std::strcmp(event->get_name(), CXOR("bullet_impact")) || !HACKS->local)
        return;

    auto user_id = HACKS->engine->get_player_for_user_id(event->get_int(CXOR("userid")));
    auto player = (c_cs_player*)HACKS->entity_list->get_client_entity(user_id);
    if (!player)
        return;

    auto x = event->get_float(CXOR("x"));
    auto y = event->get_float(CXOR("y"));
    auto z = event->get_float(CXOR("z"));

    if (player == HACKS->local)
    {
        if (g_cfg.misc.impacts)
        {
            auto clr = g_cfg.misc.server_clr.base();
            const auto& size = g_cfg.misc.impact_size * 0.1f;
            HACKS->debug_overlay->add_box_overlay({ x, y, z }, { -size, -size, -size },
                { size, size, size },
                { 0, 0, 0 },
                clr.r(), clr.g(), clr.b(), clr.a(), 4.f);
        }

        auto& impact = impacts.emplace_back();
        impact.pos = { x, y, z };
        impact.impact_time = HACKS->system_time();
    }

    bool is_local = player == HACKS->local;
    bool is_enemy = !is_local && !player->is_teammate();

    if (g_cfg.misc.tracers & 1 && is_enemy
        || g_cfg.misc.tracers & 2 && !is_enemy && !is_local
        || g_cfg.misc.tracers & 4 && is_local)
        bullets[player->index()].emplace_back(bullet_tracer_t{ player->origin() + player->view_offset(), {x, y, z} });
}

void c_bullet_tracers::on_game_events(c_game_event* event)
{
    on_player_hurt(event);
    on_bullet_impact(event);
}

void c_bullet_tracers::render_tracers()
{
    if (!HACKS->local || !HACKS->in_game)
        return;

#ifdef LEGACY
    auto& impact_list = *(c_utl_vector<client_verify_t>*)((std::uintptr_t)HACKS->local + 0xBA84);
#else
    auto& impact_list = *(c_utl_vector<client_verify_t>*)((std::uintptr_t)HACKS->local + 0x11C50);
#endif

    if (g_cfg.misc.impacts)
    {
        auto clr = g_cfg.misc.client_clr.base();

        const auto& size = g_cfg.misc.impact_size * 0.1f;
        for (auto i = impact_list.count(); i > last_impact_size; i--)
        {
            HACKS->debug_overlay->add_box_overlay(impact_list[i - 1].pos,
                { -size, -size, -size },
                { size, size, size },
                { 0, 0, 0 },
                clr.r(), clr.g(), clr.b(), clr.a(), 4);
        }

        impact_list.remove_all();
    }

    if (impact_list.count() != last_impact_size)
        last_impact_size = impact_list.count();

    if (g_cfg.misc.tracers)
    {
        for (auto i = 0; i < 65; i++)
        {
            auto& a = bullets[i];
            if (a.size() > 0)
            {
                auto player = (c_cs_player*)HACKS->entity_list->get_client_entity(i);
                if (player)
                {
                    bool is_local = player == HACKS->local;
                    bool is_enemy = !is_local && !player->is_teammate();

                    c_float_color trace_clr = c_float_color{ 255, 255, 255 };
                    if (is_local)
                        trace_clr = g_cfg.misc.trace_clr[2];
                    else if (!is_enemy && !is_local)
                        trace_clr = g_cfg.misc.trace_clr[1];
                    else
                        trace_clr = g_cfg.misc.trace_clr[0];

                    auto base = trace_clr.base();
                    switch (g_cfg.misc.tracer_type)
                    {
                    case 0:
                        draw_beam(a.back().eye_position, a.back().impact_position, base);
                        break;
                    case 1:
                        /*HACKS->debug_overlay->add_line_overlay(a.back().eye_position, a.back().impact_position, base.r(), base.g(), base.b(), false, 3.f); */
                        break;
                    }
                }

                a.clear();
            }
        }
    }
}

void c_bullet_tracers::render_hitmarkers()
{
    if (!HACKS->local || !HACKS->in_game)
        return;

    if (hitmarkers.empty())
        return;

    auto draw_list = RENDER->get_draw_list();
    RESTORE(draw_list->Flags);

    draw_list->Flags &= ~ImDrawListFlags_AntiAliasedLines;

    float screen_alpha = 0.f;
    for (int i = 0; i < hitmarkers.size(); ++i)
    {
        auto& hit = hitmarkers[i];
        if (hit.time == 0.f || hit.dmg <= 0)
        {
            screen_alpha = 0.f;
            continue;
        }

        float diff = std::clamp(HACKS->system_time() - hit.time, 0.f, 1.f);
        if (diff >= 1.f)
        {
            hit.alpha = std::lerp(hit.alpha, 0.f, RENDER->get_animation_speed() * 1.5f);
            if (hit.alpha <= 0.f)
            {
                screen_alpha = 0.f;
                hitmarkers.erase(hitmarkers.begin() + i);
                continue;
            }
        }

        if (hit.alpha > 0.f)
        {
            screen_alpha = hit.alpha;

            vec2_t position{};
            if (RENDER->world_to_screen(hit.pos, position))
            {
                if (g_cfg.misc.hitmarker & 1)
                {
                    auto clr = g_cfg.misc.hitmarker_clr.base();

                    RENDER->line(position.x - 2, position.y - 2, position.x - 8, position.y - 8, clr.new_alpha(255.f * hit.alpha), 1.f);
                    RENDER->line(position.x + 2, position.y + 2, position.x + 8, position.y + 8, clr.new_alpha(255.f * hit.alpha), 1.f);
                    RENDER->line(position.x - 2, position.y + 2, position.x - 8, position.y + 8, clr.new_alpha(255.f * hit.alpha), 1.f);
                    RENDER->line(position.x + 2, position.y - 2, position.x + 8, position.y - 8, clr.new_alpha(255.f * hit.alpha), 1.f);
                }

                if (g_cfg.misc.damage)
                {
                    auto clr = g_cfg.misc.damage_clr.base();
                    RENDER->text(position.x, position.y - 30.f,
                        clr.new_alpha(255.f * hit.alpha),
                        FONT_CENTERED_X | FONT_DROPSHADOW | FONT_LIGHT_BACK,
                        &RENDER->fonts.dmg,
                        tfm::format(CXOR("%d"), hit.dmg));
                }
            }
        }
    }

    if (screen_alpha && (g_cfg.misc.hitmarker & 2))
    {
        auto clr = g_cfg.misc.hitmarker_clr.base();

        auto position = vec2_t{ RENDER->screen.x / 2.f, RENDER->screen.y / 2.f };

        RENDER->line(position.x - 3, position.y - 3, position.x - 9, position.y - 9, clr.new_alpha(255.f * screen_alpha), 1.f);
        RENDER->line(position.x + 3, position.y + 3, position.x + 9, position.y - 9, clr.new_alpha(255.f * screen_alpha), 1.f);
        RENDER->line(position.x - 3, position.y + 3, position.x - 9, position.y + 9, clr.new_alpha(255.f * screen_alpha), 1.f);
        RENDER->line(position.x + 3, position.y - 3, position.x + 9, position.y + 9, clr.new_alpha(255.f * screen_alpha), 1.f);
    }
}