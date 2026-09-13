#pragma once
#include <string>

// Detailed ragebot diagnostics ("Ragebot debug log" option).
// Appends structured lines to Documents\Ryze\logs\ryze_<date>_<time>.log (the
// logs folder is created on first use; falls back to %TEMP%) and flushes every
// line so the file can be pasted while the game is running. All entry points
// are cheap no-ops when the option is off.
namespace rage_log
{
	// True when the "Ragebot debug log" option is enabled.
	bool enabled();

	// Absolute path of the active log file (set after the first write).
	const std::string& path();

	// Writes the one-time session header (date + log path).
	void session_header();

	// printf-style line, prefixed with tick/curtime. No-op when disabled.
	void line(const char* fmt, ...);
}
