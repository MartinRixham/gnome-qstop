/* Copyright 2026 Martin Rixham

   Portions adapted from btop++, Copyright 2021 Aristocratos (jakob@qvantnet.com),
   originally licensed under the Apache License, Version 2.0.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.

indent = tab
tab-size = 4
*/

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using std::array;
using std::atomic;
using std::string;
using std::string_view;
using std::to_string;
using std::vector;

//? ------------------------------------------------- NAMESPACES ------------------------------------------------------

//* Collection of escape codes for text style and formatting
namespace Fx {
	extern const string e;	//* Escape sequence start
	extern const string b;	//* Bold on/off
	extern const string ub;	//* Bold off
	extern const string d;	//* Dark on
	extern const string ud;	//* Dark off
	extern const string i;	//* Italic on
	extern const string ui;	//* Italic off
	extern const string ul;	//* Underline on
	extern const string uul;//* Underline off
	extern const string bl;	//* Blink on
	extern const string ubl;//* Blink off
	extern const string s;	//* Strike/crossed-out on
	extern const string us;	//* Strike/crossed-out off

	//* Reset foreground/background color and text effects
	extern const string reset_base;

	//* Reset text effects and restore theme foregrund and background color
	extern string reset;

	//* Regex for matching only color and style escape sequences
	extern const std::regex color_regex;

	//* Return a string with all colors and text styling removed
	inline string uncolor(const string& s) { return std::regex_replace(s, color_regex, ""); }
}

//* Collection of escape codes and functions for cursor manipulation
namespace Mv {
	//* Move cursor to <line>, <column>
	inline string to(int line, int col) { return Fx::e + to_string(line) + ';' + to_string(col) + 'f'; }

	//* Move cursor right <x> columns
	inline string r(int x) { return Fx::e + to_string(x) + 'C'; }

	//* Move cursor left <x> columns
	inline string l(int x) { return Fx::e + to_string(x) + 'D'; }

	//* Move cursor up x lines
	inline string u(int x) { return Fx::e + to_string(x) + 'A'; }

	//* Move cursor down x lines
	inline string d(int x) { return Fx::e + to_string(x) + 'B'; }

	//* Save cursor position
	const string save = Fx::e + "s";

	//* Restore saved cursor position
	const string restore = Fx::e + "u";
}

//* Collection of escape codes and functions for terminal manipulation
namespace Term {
	extern atomic<bool> initialized;
	extern atomic<int> width;
	extern atomic<int> height;
	extern string fg, bg;

	extern const string hide_cursor;
	extern const string show_cursor;
	extern const string alt_screen;
	extern const string normal_screen;
	extern const string clear;
	extern const string clear_end;
	extern const string mouse_on;
	extern const string mouse_off;
	extern const string sync_start; //? Start of terminal synchronized output
	extern const string sync_end; //? End of terminal synchronized output

	//* Returns true if terminal has been resized and updates width and height
	bool refresh(bool only_check=false);

	//* Check for a valid tty, save terminal options and set new options
	bool init();

	//* Restore terminal options
	void restore();
}

//? --------------------------------------------------- FUNCTIONS -----------------------------------------------------

namespace Tools {
	constexpr auto SSmax = std::numeric_limits<std::streamsize>::max();

	//* Return number of UTF8 characters in a string (wide=true for column size needed on terminal)
	size_t ulen(const std::string_view str, bool wide = false);

	//* Resize a string consisting of UTF8 characters (only reduces size)
	string uresize(const string& str, const size_t len, bool wide = false);

	//* Replace <from> in <str> with <to> and return new string
	string s_replace(const string& str, const string& from, const string& to);

	//* Capitalize <str>
	inline string capitalize(string str) {
		if (not str.empty()) str.at(0) = toupper(str.at(0));
		return str;
	}

	//* Check if vector <vec> contains value <find_val>
	template <typename T, typename T2>
	inline bool v_contains(const vector<T>& vec, const T2& find_val) {
		return std::ranges::find(vec, find_val) != vec.end();
	}

	//* Compare <first> with all following values
	template<typename First, typename ... T>
	inline bool is_in(const First& first, const T& ... t) {
		return ((first == t) or ...);
	}

	//* Return current time since epoch in milliseconds
	inline uint64_t time_ms() {
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	//* Check if a string is a valid bool value
	inline bool isbool(const std::string_view str) {
		return is_in(str, "true", "false", "True", "False");
	}

	//* Convert string to bool, returning any value not equal to "true" or "True" as false
	inline bool stobool(const std::string_view str) {
		return is_in(str, "true", "True");
	}

	//* Check if a string is a valid integer value (only positive)
	constexpr bool isint(const std::string_view str) {
		return not str.empty() and std::ranges::all_of(str, ::isdigit);
	}

	//* Left-trim <t_str> from <str> and return new string
	string_view ltrim(string_view str, string_view t_str = " ");

	//* Right-trim <t_str> from <str> and return new string
	string_view rtrim(string_view str, string_view t_str = " ");

	//* Left/right-trim <t_str> from <str> and return new string
	inline string_view trim(string_view str, string_view t_str = " ") {
		return ltrim(rtrim(str, t_str), t_str);
	}

	//* Split <string> at all occurrences of <delim> and return as vector of strings
	constexpr auto ssplit(std::string_view str, char delim = ' ') {
		return str | std::views::split(delim) | std::views::filter([](auto&& range) { return !std::ranges::empty(range); }) |
			   std::ranges::to<std::vector<std::string>>();
	}

	//* Put current thread to sleep for <ms> milliseconds
	inline void sleep_ms(const size_t& ms) {
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
	}

	//* Left justify string <str> if <x> is greater than <str> length, limit return size to <x> by default
	string ljust(string str, const size_t x, bool utf = false, bool wide = false, bool limit = true);

	//* Right justify string <str> if <x> is greater than <str> length, limit return size to <x> by default
	string rjust(string str, const size_t x, bool utf = false, bool wide = false, bool limit = true);

	//* Center justify string <str> if <x> is greater than <str> length, limit return size to <x> by default
	string cjust(string str, const size_t x, bool utf = false, bool wide = false, bool limit = true);

	//* Convert seconds to format "<hours>:<minutes>" and return string
	string sec_to_hm(int64_t seconds);

	//* Add std::string operator * : Repeat string <str> <n> number of times
	std::string operator*(const string& str, int64_t n);

	//* Return current time in <strf> format
	string strf_time(const string& strf);

	string hostname();
	string username();

	//* Sets atomic<bool> to true on construct, sets to false on destruct
	class atomic_lock {
		atomic<bool>& atom;
	public:
		explicit atomic_lock(atomic<bool>& atom);
		~atomic_lock() noexcept;
		atomic_lock(const atomic_lock& other) = delete;
		atomic_lock& operator=(const atomic_lock& other) = delete;
	};

	//* Read a complete file and return as a string
	string readfile(const std::filesystem::path& path, const string& fallback = "");

	struct ExecResult {
		int status = -1;
		bool timed_out = false;
		string output;
	};

	//* Run <args> without a shell, returning exit status and stdout (stderr merged if <merge_stderr>)
	//* Child is killed if it runs longer than <timeout_ms>, output is collected with LC_ALL=C.UTF-8
	ExecResult exec(const vector<string>& args, uint64_t timeout_ms = 2000, bool merge_stderr = false);

	//* Launch <args> detached in a new session, not waiting for it to finish
	bool spawn_detached(const vector<string>& args);

	//* Check if <name> is an executable found in PATH
	bool command_exists(const string& name);
}
