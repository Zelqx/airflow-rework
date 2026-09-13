// clantag.cpp
#include "globals.hpp"
#include "clantag.hpp"

void c_clantag::run()
{
	if (!HACKS->local)
		return;

	auto predicted_curtime = TICKS_TO_TIME(HACKS->local->tickbase());
	auto clantag_time = (int)((HACKS->global_vars->curtime * 2.0f) + HACKS->ping); // Slightly slower for smooth effect

	if (g_cfg.misc.clantag)
	{
		reset_tag = false;

		if (clantag_time != last_change_time)
		{
			// Update clantag with smooth scrolling animation
			set_clan_tag(clantag_frames[current_frame].c_str(), clantag_frames[current_frame].c_str());

			// Move to the next frame
			current_frame = (current_frame + 1) % clantag_frames.size();

			last_change_time = clantag_time;
		}
	}
	else if (!reset_tag)
	{
		set_clan_tag("", "");
		reset_tag = true;
		last_change_time = clantag_time;
	}
}
