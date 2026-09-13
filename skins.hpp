#pragma once
#include <map>
#include <unordered_map>
#include <string>
#include <vector>

namespace skin_changer
{
	inline std::vector< std::pair< std::string, int > > paint_kits{};

	extern void init_parser();
	extern int correct_sequence(const short& index, const int seq);
	extern void on_postdataupdate_start(int stage);
	extern void on_game_events(c_game_event* event);
	extern void on_frame_render_end(int stage);

	// returns human-readable labels for each entry in `player_models` in the same order
	extern std::vector<std::string> get_player_model_labels();
}