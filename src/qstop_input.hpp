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

#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

using std::array;
using std::atomic;
using std::string;

/* The input functions rely on the following termios parameters being set:
	Non-canonical mode (c_lflags & ~(ICANON))
	VMIN and VTIME (c_cc) set to 0
	These will automatically be set when running Term::init() from qstop_tools.cpp
*/

//* Functions and variables for handling keyboard and mouse input
namespace Input {

	struct Mouse_loc {
		int line, col, height, width;
	};

	//? line, col, height, width (Shared::mtx)
	extern std::unordered_map<string, Mouse_loc> mouse_mappings;

	//* Signal mask used during polling read
	extern sigset_t signal_mask;

	extern atomic<bool> polling;

	//* Mouse column and line position
	extern array<int, 2> mouse_pos;

	//* Poll keyboard & mouse input for <timeout> ms and return input availability as a bool
	bool poll(const uint64_t timeout=0);

	//* Get the next key or mouse action from input
	string get();

	//* Returns true if more input events are buffered after get()
	bool pending();

	//* Interrupt poll/wait
	void interrupt();

	//* Process actions for input <key>
	void process(const std::string_view key);

}
