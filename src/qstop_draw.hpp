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
#include <string>
#include <string_view>
#include <vector>

using std::array;
using std::string;
using std::vector;

namespace Symbols {
	extern const string h_line;
	extern const string v_line;
	extern const string left_up;
	extern const string right_up;
	extern const string left_down;
	extern const string right_down;
	extern const string round_left_up;
	extern const string round_right_up;
	extern const string round_left_down;
	extern const string round_right_down;
	extern const string title_left_down;
	extern const string title_right_down;
	extern const string title_left;
	extern const string title_right;

	extern const string up;
	extern const string down;
	extern const string enter;
	extern const string meter;
	extern const string radio_on;
	extern const string radio_off;
	extern const array<string, 10> superscript;
	extern const array<string, 4> signal;

	//* Symbol shown on items that open a submenu
	const string& expand();
}

namespace Draw {

	//* An element that can receive keyboard focus, in screen coordinates
	struct Focusable {
		string id;
		int x, y, width, height;
	};

	//* Focusable elements from the last draw, in tab order (Shared::mtx)
	extern vector<Focusable> focusables;

	//* Id of the focused element, e.g. "toggle:wifi" (Shared::mtx)
	extern string focused;

	//* Create a box and return as a string
	string createBox(
			const int x, const int y, const int width, const int height, string line_color = "", bool fill = false,
			const std::string_view title = "", const std::string_view title2 = "", const int num = 0
	);

	//* Return <label> with the first occurrence of <key> highlighted, underlined instead if drawn <on_selected> background
	string hotkey(const string& label, char key, const string& fg, bool on_selected = false);

	//* Class holding a percentage meter
	class Meter {
		int width;
		string color_gradient;
		bool invert;
		array<string, 101> cache;
	public:
		Meter();
		Meter(const int width, string color_gradient, bool invert = false);

		//* Return a string representation of the meter with given value
		string operator()(int value);
	};

	//* Calculate sizes and positions of shown boxes
	void calcSizes();

	//* Draw all boxes and any active menu overlay, caller must hold Shared::mtx
	string draw_all(bool force_redraw);

	namespace Status {
		extern int x, y, width, height;
		extern bool shown;
	}

	namespace Sliders {
		struct slider {
			string id, label, gradient;
			int value;
			bool muted, muteable, expandable;
		};

		extern int x, y, width, height;
		extern bool shown;

		//* Highest value the slider <id> accepts
		int max_value(const string& id);

		vector<slider> items();
	}

	namespace Toggles {
		struct toggle {
			string id, title, subtitle;
			char key;
			bool active, expandable;
		};

		extern int x, y, width, height, cols;
		extern bool shown;

		vector<toggle> items();
	}
}
