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

#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "qstop_input.hpp"

using std::atomic;
using std::string;
using std::vector;

//* Overlay menus drawn on top of the boxes, all functions require Shared::mtx to be held
namespace Menu {

	extern atomic<bool> active;

	//* Mouse mappings used instead of Input::mouse_mappings while a menu is active
	extern std::unordered_map<string, Input::Mouse_loc> mouse_mappings;

	enum Menus {
		Help,
		List,
		Password,
		Confirm,
	};

	//* Open <menu>, <id> selects the content of List menus:
	//* "wifi", "bluetooth", "power_mode", "vpn", "volume", "mic" or "power"
	void show(int menu, const string& id = "");

	//* Close any active menu
	void close();

	//* Process input for the active menu
	void process(const string& key);

	//* Regenerate Global::overlay for the active menu
	void draw();

	//* Ask for confirmation before running <action>, runs directly if "confirm_power_actions" is disabled
	void confirm(const string& title, const vector<string>& message, std::function<void()> action);
}
