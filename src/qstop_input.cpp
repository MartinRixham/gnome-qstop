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

#include <cmath>
#include <limits>
#include <mutex>
#include <ranges>

#include <sys/select.h>
#include <unistd.h>

#include "qstop_config.hpp"
#include "qstop_draw.hpp"
#include "qstop_input.hpp"
#include "qstop_menu.hpp"
#include "qstop_shared.hpp"
#include "qstop_tools.hpp"

using std::max;
using std::min;

using namespace Tools;
using namespace std::literals;

namespace Input {

	//* Map for translating key codes to readable values
	const std::unordered_map<string, string> Key_escapes = {
		{"\033",	"escape"},
		{"\n",		"enter"},
		{"\r",		"enter"},
		{" ",		"space"},
		{"\x7f",	"backspace"},
		{"\x08",	"backspace"},
		{"[A", 		"up"},
		{"OA",		"up"},
		{"[B", 		"down"},
		{"OB",		"down"},
		{"[D", 		"left"},
		{"OD",		"left"},
		{"[C", 		"right"},
		{"OC",		"right"},
		{"[2~",		"insert"},
		{"[3~",		"delete"},
		{"[H",		"home"},
		{"[1~",		"home"},
		{"[F",		"end"},
		{"[4~",		"end"},
		{"[5~",		"page_up"},
		{"[6~",		"page_down"},
		{"\t",		"tab"},
		{"[Z",		"shift_tab"},
		{"OP",		"f1"},
		{"OQ",		"f2"},
		{"OR",		"f3"},
		{"OS",		"f4"},
		{"[15~",	"f5"},
		{"[17~",	"f6"},
		{"[18~",	"f7"},
		{"[19~",	"f8"},
		{"[20~",	"f9"},
		{"[21~",	"f10"},
		{"[23~",	"f11"},
		{"[24~",	"f12"}
	};

	sigset_t signal_mask;
	std::atomic<bool> polling (false);
	array<int, 2> mouse_pos;
	std::unordered_map<string, Mouse_loc> mouse_mappings;

	namespace {
		string input;
	}

	bool poll(const uint64_t timeout) {
		atomic_lock lck(polling);
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);
		struct timespec wait;
		struct timespec *waitptr = nullptr;

		if(timeout != std::numeric_limits<uint64_t>::max()) {
			wait.tv_sec = timeout / 1000;
			wait.tv_nsec = (timeout % 1000) * 1000000;
			waitptr = &wait;
		}

		if(pselect(STDIN_FILENO + 1, &fds, nullptr, nullptr, waitptr, &signal_mask) > 0) {
			input.clear();
			char buf[1024];
			ssize_t count = 0;
			while((count = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
				input.append(std::string_view(buf, count));
			}

			return true;
		}

		return false;
	}

	bool pending() {
		return not input.empty();
	}

	string get() {
		//? Take one event from the input buffer, escape sequences end at their final byte
		size_t end = input.find('\x1b', 1);
		if (input.starts_with("\x1b[")) {
			//? CSI sequences, including SGR mouse reports, end with a byte in the range 0x40-0x7e
			const auto final_byte = std::ranges::find_if(input.begin() + 2, input.end(), [](char c) { return c >= 0x40 and c <= 0x7e; });
			if (final_byte != input.end()) end = std::distance(input.begin(), final_byte) + 1;
		}
		else if (input.starts_with("\x1bO") and input.size() >= 3) {
			end = 3;
		}
		string key = input.substr(0, end);
		input.erase(0, (end == string::npos ? input.size() : min(end, input.size())));

		//? Split plain characters typed or pasted faster than a poll into separate keys
		if (not key.empty() and key.at(0) != '\x1b' and ulen(key) > 1) {
			size_t n = 1;
			while (n < key.size() and (static_cast<unsigned char>(key[n]) & 0xC0) == 0x80) n++;
			input.insert(0, key.substr(n));
			key.resize(n);
		}

		if (not key.empty()) {
			//? A lone escape followed by a plain key, e.g. Esc then q pressed quickly
			if (key.size() > 1 and key.at(0) == '\x1b' and not is_in(key.at(1), '[', 'O')) {
				input.insert(0, key.substr(1));
				key = "\x1b";
			}

			//? Remove escape code prefix if present
			if (key.length() > 1 and key.at(0) == Fx::e.at(0)) {
				key.erase(0, 1);
			}

			//? Detect if input is a mouse event
			if (key.starts_with("[<")) {
				std::string_view key_view = key;
				string mouse_event;
				if (key_view.starts_with("[<0;") and key_view.find('M') != std::string_view::npos) {
					mouse_event = "mouse_click";
					key_view.remove_prefix(4);
				}
				else if (key_view.starts_with("[<32;")) {
					mouse_event = "mouse_drag";
					key_view.remove_prefix(5);
				}
				else if (key_view.starts_with("[<64;")) {
					mouse_event = "mouse_scroll_up";
					key_view.remove_prefix(5);
				}
				else if (key_view.starts_with("[<65;")) {
					mouse_event = "mouse_scroll_down";
					key_view.remove_prefix(5);
				}

				if (mouse_event.empty() or Config::getB("disable_mouse"))
					return "";

				//? Get column and line position of mouse and check for any actions mapped to current position
				try {
					const auto delim = key_view.find(';');
					const auto end = key_view.find_first_of("Mm", delim);
					mouse_pos[0] = stoi((string)key_view.substr(0, delim));
					mouse_pos[1] = stoi((string)key_view.substr(delim + 1, end - delim - 1));
				}
				catch (const std::exception&) { return ""; }

				key = mouse_event;

				if (key == "mouse_click" or key == "mouse_drag") {
					const auto& [col, line] = mouse_pos;
					std::lock_guard lck(Shared::mtx);
					for (const auto& [mapped_key, pos] : (Menu::active ? Menu::mouse_mappings : mouse_mappings)) {
						if (col >= pos.col and col < pos.col + pos.width and line >= pos.line and line < pos.line + pos.height) {
							//? Dragging only applies to sliders
							if (key == "mouse_drag" and not mapped_key.starts_with("slider:")) break;
							key = mapped_key;
							break;
						}
					}
				}
			}
			else if (auto it = Key_escapes.find(key); it != Key_escapes.end())
				key = it->second;
			else if (key.size() == 1 and key.at(0) == 3)
				key = "ctrl_c";
			else if (ulen(key) > 1)
				key.clear();
		}
		return key;
	}

	void interrupt() {
		kill(getpid(), SIGUSR1);
	}

	namespace {
		//* Split a focus id "type:name" into its parts
		std::pair<string, string> split_id(const string& id) {
			const auto pos = id.find(':');
			if (pos == string::npos) return {id, ""};
			return {id.substr(0, pos), id.substr(pos + 1)};
		}

		int slider_value(const string& name) {
			if (name == "volume") return Audio::current.volume;
			if (name == "mic") return Audio::current.mic_volume;
			if (name == "brightness") return Display::current.screen.percent();
			if (name == "kbd") return Display::current.kbd.percent();
			return 0;
		}

		void set_slider(const string& name, int value) {
			if (name == "volume") Audio::set_volume(value);
			else if (name == "mic") Audio::set_mic_volume(value);
			else if (name == "brightness") Display::set_brightness(value);
			else if (name == "kbd") Display::set_kbd_brightness(value);
		}

		void adjust_slider(const string& name, int direction) {
			const bool audio = is_in(name, "volume", "mic");
			int step = Config::getI(audio ? "volume_step" : "brightness_step");
			//? Keyboard backlights often only have a few levels, step one level at a time
			if (name == "kbd" and Display::current.kbd.max > 0 and Display::current.kbd.max < 100 / step)
				step = (int)std::ceil(100.0 / Display::current.kbd.max);
			int value = slider_value(name);
			//? Snap to step multiples like GNOME does for keyboard volume changes
			value = (direction > 0 ? (value / step + 1) * step : ((value + step - 1) / step - 1) * step);
			set_slider(name, value);
		}

		void toggle_mute(const string& name) {
			if (name == "volume" and Audio::current.has_sink) Audio::set_mute(not Audio::current.muted);
			else if (name == "mic" and Audio::current.has_source) Audio::set_mic_mute(not Audio::current.mic_muted);
		}

		//* Open the submenu belonging to element <id> if it has one
		bool expand(const string& id) {
			auto [type, name] = split_id(id);
			if ((type == "toggle" and is_in(name, "wifi", "bluetooth", "power_mode", "vpn"))
				or (type == "slider" and is_in(name, "volume", "mic"))
				or (type == "button" and name == "power")) {
				Menu::show(Menu::List, name);
				return true;
			}
			return false;
		}

		void activate(const string& id) {
			auto [type, name] = split_id(id);
			if (type == "toggle") {
				const auto& net = Network::current;
				const auto& desktop = Desktop::current;
				if (name == "wired") Network::set_wired(not net.wired_connected);
				else if (name == "wifi") Network::set_wifi(not net.wifi_enabled);
				else if (name == "bluetooth") Bluetooth::set_power(not Bluetooth::current.powered);
				else if (name == "power_mode") Menu::show(Menu::List, "power_mode");
				else if (name == "night_light") Desktop::set_night_light(not desktop.night_light);
				else if (name == "dark_style") Desktop::set_dark_style(not desktop.dark_style);
				else if (name == "airplane") Desktop::set_airplane(not desktop.airplane);
				else if (name == "dnd") Desktop::set_dnd(not desktop.dnd);
				else if (name == "vpn") {
					auto active = std::ranges::find_if(net.vpns, &Network::vpn_connection::active);
					if (active != net.vpns.end()) Network::set_vpn(active->name, false);
					else if (net.vpns.size() == 1) Network::set_vpn(net.vpns.front().name, true);
					else Menu::show(Menu::List, "vpn");
				}
			}
			else if (type == "button") {
				if (name == "screenshot") Power::screenshot();
				else if (name == "settings") Power::open_settings();
				else if (name == "lock") Power::lock_screen();
				else if (name == "power") Menu::show(Menu::List, "power");
			}
			else if (type == "slider") {
				toggle_mute(name);
			}
		}

		//* Move focus to the nearest element in direction <dir>
		void move_focus(const string& dir) {
			auto& items = Draw::focusables;
			if (items.empty()) return;
			auto current = std::ranges::find(items, Draw::focused, &Draw::Focusable::id);
			if (current == items.end()) {
				Draw::focused = items.front().id;
				return;
			}
			const int index = std::distance(items.begin(), current);
			const int count = items.size();

			if (is_in(dir, "tab", "shift_tab")) {
				Draw::focused = items.at((index + (dir == "tab" ? 1 : count - 1)) % count).id;
				return;
			}

			const auto& cur = *current;
			const double cx = cur.x + cur.width / 2.0;
			const double cy = cur.y + cur.height / 2.0;
			int best = -1;
			double best_score = std::numeric_limits<double>::max();

			for (int i = 0; i < count; i++) {
				if (i == index) continue;
				const auto& other = items.at(i);
				const double ox = other.x + other.width / 2.0;
				const double oy = other.y + other.height / 2.0;
				const bool same_row = other.y < cur.y + cur.height and other.y + other.height > cur.y;
				double primary, secondary;

				if (dir == "up") {
					if (other.y + other.height > cur.y) continue;
					primary = cur.y - (other.y + other.height);
					secondary = std::abs(ox - cx);
				}
				else if (dir == "down") {
					if (other.y < cur.y + cur.height) continue;
					primary = other.y - (cur.y + cur.height);
					secondary = std::abs(ox - cx);
				}
				else if (dir == "left") {
					if (not same_row or ox >= cx) continue;
					primary = cx - ox;
					secondary = std::abs(oy - cy);
				}
				else {
					if (not same_row or ox <= cx) continue;
					primary = ox - cx;
					secondary = std::abs(oy - cy);
				}

				const double score = primary * 10000 + secondary;
				if (score < best_score) {
					best_score = score;
					best = i;
				}
			}

			if (best >= 0)
				Draw::focused = items.at(best).id;
			else if (is_in(dir, "left", "right"))
				Draw::focused = items.at((index + (dir == "right" ? 1 : count - 1)) % count).id;
		}

		//* Return id of the focusable slider under the mouse, or empty string
		string slider_at_mouse() {
			const auto& [col, line] = mouse_pos;
			for (const auto& item : Draw::focusables) {
				if (item.id.starts_with("slider:") and line >= item.y and line < item.y + item.height
					and col >= item.x and col < item.x + item.width)
					return item.id;
			}
			return "";
		}

		//* Focus <id> and run <action> on it if the element is available
		void hotkey(const string& id, bool expand_instead = false) {
			if (std::ranges::find(Draw::focusables, id, &Draw::Focusable::id) == Draw::focusables.end()) return;
			Draw::focused = id;
			if (not expand_instead or not expand(id)) activate(id);
		}
	}

	void process(const std::string_view key_view) {
		if (key_view.empty()) return;
		const string key{key_view};
		std::unique_lock lck(Shared::mtx);

		if (Menu::active) {
			Menu::process(key);
			lck.unlock();
			Runner::run();
			return;
		}

		const bool vim_keys = Config::getB("vim_keys");
		const string help_key = (vim_keys ? "H" : "h");
		const string lock_key = (vim_keys ? "L" : "l");
		const auto [focus_type, focus_name] = split_id(Draw::focused);
		bool redraw = true;
		bool force_redraw = false;

		//? Translate vim keys to directions
		string nav_key = key;
		if (vim_keys) {
			if (key == "h") nav_key = "left";
			else if (key == "j") nav_key = "down";
			else if (key == "k") nav_key = "up";
			else if (key == "l") nav_key = "right";
		}

		if (is_in(key, "q", "ctrl_c")) {
			Global::should_quit = true;
			redraw = false;
		}
		else if (is_in(key, "f1", "?", help_key)) {
			Menu::show(Menu::Help);
		}
		else if (key.size() == 1 and key.at(0) >= '1' and key.at(0) <= '3') {
			Config::toggle_box(Config::valid_boxes.at(key.at(0) - '1'));
			force_redraw = true;
		}
		else if (focus_type == "slider" and is_in(nav_key, "left", "right")) {
			adjust_slider(focus_name, (nav_key == "right" ? 1 : -1));
		}
		else if (is_in(nav_key, "up", "down", "left", "right", "tab", "shift_tab")) {
			move_focus(nav_key);
		}
		else if (is_in(key, "enter", "space")) {
			activate(Draw::focused);
		}
		else if (key == "e") {
			redraw = expand(Draw::focused);
		}
		else if (is_in(key, "+", "=", "-", "_")) {
			if (focus_type == "slider") adjust_slider(focus_name, (is_in(key, "+", "=") ? 1 : -1));
			else if (Audio::current.has_sink) adjust_slider("volume", (is_in(key, "+", "=") ? 1 : -1));
		}
		else if (key == "m") toggle_mute("volume");
		else if (key == "M") toggle_mute("mic");
		else if (key == "i") hotkey("toggle:wired");
		else if (key == "w") hotkey("toggle:wifi");
		else if (key == "W") hotkey("toggle:wifi", true);
		else if (key == "b") hotkey("toggle:bluetooth");
		else if (key == "B") hotkey("toggle:bluetooth", true);
		else if (key == "r") hotkey("toggle:power_mode");
		else if (key == "n") hotkey("toggle:night_light");
		else if (key == "d") hotkey("toggle:dark_style");
		else if (key == "v") hotkey("toggle:vpn");
		else if (key == "a") hotkey("toggle:airplane");
		else if (key == "o") hotkey("toggle:dnd");
		else if (key == "s") hotkey("button:screenshot");
		else if (key == "t") hotkey("button:settings");
		else if (key == lock_key) hotkey("button:lock");
		else if (key == "p") hotkey("button:power");

		//? Mouse actions
		else if (is_in(key, "mouse_scroll_up", "mouse_scroll_down")) {
			const string slider = slider_at_mouse();
			if (slider.empty()) redraw = false;
			else {
				Draw::focused = slider;
				adjust_slider(split_id(slider).second, (key == "mouse_scroll_up" ? 1 : -1));
			}
		}
		else if (key.starts_with("slider:")) {
			Draw::focused = key;
			if (auto it = mouse_mappings.find(key); it != mouse_mappings.end()) {
				const auto& loc = it->second;
				const auto name = split_id(key).second;
				const double fraction = (loc.width > 1 ? (double)(mouse_pos[0] - loc.col) / (loc.width - 1) : 1.0);
				set_slider(name, (int)std::round(std::clamp(fraction, 0.0, 1.0) * Draw::Sliders::max_value(name)));
			}
		}
		else if (key.starts_with("mute:")) {
			Draw::focused = "slider:" + split_id(key).second;
			toggle_mute(split_id(key).second);
		}
		else if (key.starts_with("expand:")) {
			const auto name = split_id(key).second;
			const string id = (std::ranges::find(Draw::focusables, "slider:" + name, &Draw::Focusable::id) != Draw::focusables.end()
				? "slider:" + name : "toggle:" + name);
			Draw::focused = id;
			expand(id);
		}
		else if (key.starts_with("toggle:") or key.starts_with("button:")) {
			Draw::focused = key;
			activate(key);
		}
		else redraw = false;

		lck.unlock();
		if (redraw) Runner::run(false, force_redraw);
	}

}
