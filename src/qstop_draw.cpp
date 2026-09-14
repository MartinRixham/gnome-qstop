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

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>

#include "qstop_config.hpp"
#include "qstop_draw.hpp"
#include "qstop_input.hpp"
#include "qstop_menu.hpp"
#include "qstop_shared.hpp"
#include "qstop_theme.hpp"
#include "qstop_tools.hpp"

using std::clamp;
using std::max;
using std::min;
using std::round;
using std::views::iota;

using namespace Tools;

namespace Symbols {
	const string h_line				= "─";
	const string v_line				= "│";
	const string left_up			= "┌";
	const string right_up			= "┐";
	const string left_down			= "└";
	const string right_down			= "┘";
	const string round_left_up		= "╭";
	const string round_right_up		= "╮";
	const string round_left_down	= "╰";
	const string round_right_down	= "╯";
	const string title_left_down	= "┘";
	const string title_right_down	= "└";
	const string title_left			= "┐";
	const string title_right		= "┌";

	const string up = "↑";
	const string down = "↓";
	const string enter = "↵";
	const string meter = "■";
	const string radio_on = "●";
	const string radio_off = "○";

	const array<string, 10> superscript = { "⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹" };
	const array<string, 4> signal = { "▂", "▄", "▆", "█" };

	const string& expand() {
		static const string normal = "›", tty = ">";
		return (Config::getB("tty_mode") ? tty : normal);
	}
}

namespace Draw {

	vector<Focusable> focusables;
	string focused;

	namespace {
		//* Minimum terminal width needed to draw the boxes
		constexpr int min_width = 40;
		constexpr int toggle_min_width = 22;
		constexpr int toggle_height = 4;
		constexpr int slider_label_width = 11;

		int total_height = 0;

		void add_focus(const string& id, int x, int y, int width, int height) {
			focusables.push_back({id, x, y, width, height});
		}

		void map_mouse(const string& id, int x, int y, int width, int height) {
			if (Config::getB("disable_mouse")) return;
			Input::mouse_mappings[id] = {y, x, height, width};
		}

		//* Draw a key hint in the bottom border of a box, btop style, returns width used
		string hint(int x, int y, const string& key, const string& label, const string& line_color) {
			return Mv::to(y, x) + line_color + Symbols::title_left_down + Fx::b + Theme::c("hi_fg") + key
				+ Theme::c("title") + ' ' + label + Fx::ub + line_color + Symbols::title_right_down;
		}
	}

	string createBox(
			const int x, const int y, const int width, const int height, string line_color, bool fill, const std::string_view title,
			const std::string_view title2, const int num
	) {
		string out;

		if (line_color.empty())
			line_color = Theme::c("div_line");

		auto tty_mode = Config::getB("tty_mode");
		auto rounded = Config::getB("rounded_corners");
		const string numbering = (num == 0) ? "" : Theme::c("hi_fg") + (tty_mode ? std::to_string(num) : Symbols::superscript.at(clamp(num, 0, 9)));
		const auto& right_up = (tty_mode or not rounded ? Symbols::right_up : Symbols::round_right_up);
		const auto& left_up = (tty_mode or not rounded ? Symbols::left_up : Symbols::round_left_up);
		const auto& right_down = (tty_mode or not rounded ? Symbols::right_down : Symbols::round_right_down);
		const auto& left_down = (tty_mode or not rounded ? Symbols::left_down : Symbols::round_left_down);

		out = Fx::reset + line_color;

		//? Draw horizontal lines
		for (const int& hpos : {y, y + height - 1}) {
			out += Mv::to(hpos, x) + Symbols::h_line * (width - 1);
		}

		//? Draw vertical lines and fill if enabled
		for (const int& hpos : iota(y + 1, y + height - 1)) {
			out += Mv::to(hpos, x) + Symbols::v_line
				+  ((fill) ? string(width - 2, ' ') : Mv::r(width - 2))
				+  Symbols::v_line;
		}

		//? Draw corners
		out += 	Mv::to(y, x) + left_up
			+	Mv::to(y, x + width - 1) + right_up
			+	Mv::to(y + height - 1, x) + left_down
			+	Mv::to(y + height - 1, x + width - 1) + right_down;

		//? Draw titles if defined
		if (not title.empty()) {
			out += std::format(
				"{}{}{}{}{}{}{}{}{}", Mv::to(y, x + 2), Symbols::title_left, Fx::b, numbering, Theme::c("title"), title, Fx::ub,
				line_color, Symbols::title_right
			);
		}
		if (not title2.empty()) {
			out += std::format(
				"{}{}{}{}{}{}{}{}", Mv::to(y + height - 1, x + 2), Symbols::title_left_down, Fx::b, Theme::c("title"), title2, Fx::ub,
				line_color, Symbols::title_right_down
			);
		}

		return out + Fx::reset + Mv::to(y + 1, x + 1);
	}

	string hotkey(const string& label, char key, const string& fg, bool on_selected) {
		//? Highlight the first occurrence regardless of case
		const auto pos = label.find_first_of(string{(char)tolower(key), (char)toupper(key)});
		if (pos == string::npos) return fg + label;
		const string hi = (on_selected ? Fx::ul + fg : Theme::c("hi_fg"));
		const string unhi = (on_selected ? Fx::uul + fg : fg);
		return fg + label.substr(0, pos) + hi + label.substr(pos, 1) + unhi + label.substr(pos + 1);
	}

	//* Meter class ------------------------------------------------------------------------------------------------------------>
	Meter::Meter() : width(0), invert(false) {}

	Meter::Meter(const int width, string color_gradient, bool invert)
		: width(width), color_gradient(std::move(color_gradient)), invert(invert) {}

	string Meter::operator()(int value) {
		if (width < 1) return "";
		value = clamp(value, 0, 100);
		if (not cache.at(value).empty()) return cache.at(value);
		auto& out = cache.at(value);
		for (const int& i : iota(1, width + 1)) {
			int y = round((double)i * 100.0 / width);
			if (value >= y)
				out += Theme::g(color_gradient).at(invert ? 100 - y : y) + Symbols::meter;
			else {
				out += Theme::c("meter_bg") + Symbols::meter * (width + 1 - i);
				break;
			}
		}
		out += Fx::reset;
		return out;
	}

	//* Status box ------------------------------------------------------------------------------------------------------------->
	namespace Status {
		int x, y, width, height;
		bool shown;

		namespace {
			struct button {
				string id, label;
				char key;
			};

			const vector<button> buttons = {
				{"screenshot", "Screenshot", 's'},
				{"settings", "Settings", 't'},
				{"lock", "Lock", 'l'},
				{"power", "Power", 'p'},
			};

			string clock_text() {
				string clock = Config::getS("clock_format");
				if (clock.empty()) return "";
				clock = s_replace(clock, "/host", hostname());
				clock = s_replace(clock, "/user", username());
				if (clock.contains("/uptime")) {
					int64_t uptime = 0;
					try { uptime = std::stoll(readfile("/proc/uptime", "0")); } catch (const std::exception&) {}
					clock = s_replace(clock, "/uptime", sec_to_hm(uptime));
				}
				return strf_time(clock);
			}
		}

		string draw() {
			const string line_color = Theme::c("status_box");
			string out = createBox(x, y, width, height, line_color, true, "status", "", 1);

			//? Clock centered in the top border
			if (const string clock = clock_text(); not clock.empty() and (int)ulen(clock) + 20 < width) {
				const int len = ulen(clock);
				out += Mv::to(y, x + width / 2 - len / 2 - 1) + line_color + Symbols::title_left + Fx::b + Theme::c("title") + clock
					+ Fx::ub + line_color + Symbols::title_right;
			}

			//? Transient notifications in the bottom border
			if (not Global::notification.empty() and time_ms() < Global::notification_expires) {
				out += Mv::to(y + height - 1, x + 2) + line_color + Symbols::title_left_down + Fx::b + Theme::c("title")
					+ uresize(Global::notification, max(0, width - 6), true) + Fx::ub + line_color + Symbols::title_right_down;
			}

			//? Buttons right aligned on the content line
			int buttons_width = 0;
			for (const auto& btn : buttons)
				buttons_width += btn.label.size() + 2 + (btn.id == "power" ? 2 : 0) + 1;
			const bool compact = (buttons_width + 12 > width);
			//? Only show hotkey letters when the full labels don't fit
			if (compact) buttons_width = buttons.size() * 4;
			int bx = x + width - 1 - buttons_width;
			const int info_end = bx - 1;

			for (const auto& btn : buttons) {
				const bool is_focused = (focused == "button:" + btn.id);
				string label = (compact ? string(1, (char)toupper(btn.key)) : btn.label);
				if (btn.id == "power" and not compact) label += ' ' + Symbols::expand();
				const int w = ulen(label) + 2;
				const string fg = (is_focused ? Theme::c("selected_fg") : Theme::c("main_fg"));
				out += Mv::to(y + 1, bx) + Fx::reset + (is_focused ? Theme::c("selected_bg") + Fx::b : "")
					+ ' ' + hotkey(label, btn.key, fg, is_focused) + ' ' + Fx::reset;
				add_focus("button:" + btn.id, bx, y + 1, w, 1);
				map_mouse("button:" + btn.id, bx, y + 1, w, 1);
				bx += w + 1;
			}

			//? Battery or user@host on the left, dropping details that don't fit
			const int avail = info_end - (x + 2);
			const auto& battery = Power::current.battery;
			if (battery.present) {
				const string pct = rjust(to_string(battery.percent) + '%', 4);
				string details = battery.status;
				if (Config::getB("show_battery_time") and battery.seconds > 0) {
					details += ' ' + sec_to_hm(battery.seconds) + (battery.status == "Charging" ? " until full" : " left");
				}
				const int meter_width = clamp(avail - 8 - 5 - (int)details.size() - 1, 0, 10);
				string line = Theme::c("title") + Fx::b + "Battery" + Fx::ub;
				int used = 7;
				if (meter_width >= 4) {
					line += ' ' + Meter(meter_width, "battery")(battery.percent);
					used += 1 + meter_width;
				}
				line += Theme::c("main_fg") + ' ' + pct;
				used += 5;
				if (used + 1 + (int)details.size() <= avail) line += ' ' + Theme::c("inactive_fg") + details;
				else if (used + 1 + (int)battery.status.size() <= avail) line += ' ' + Theme::c("inactive_fg") + battery.status;
				if (used <= avail) out += Mv::to(y + 1, x + 2) + line;
			}
			else {
				const string host = username() + '@' + hostname();
				if (avail > 3) out += Mv::to(y + 1, x + 2) + Theme::c("title") + Fx::b + uresize(host, avail) + Fx::ub;
			}

			return out + Fx::reset;
		}
	}

	//* Sliders box ------------------------------------------------------------------------------------------------------------>
	namespace Sliders {
		int x, y, width, height;
		bool shown;

		int max_value(const string& id) {
			return (id == "volume" and Config::getB("allow_volume_above_100") ? 150 : 100);
		}

		vector<slider> items() {
			vector<slider> list;
			const auto& audio = Audio::current;
			if (audio.has_sink)
				list.push_back({"volume", "Volume", "volume", audio.volume, audio.muted, true, audio.sinks.size() > 1});
			if (audio.has_source)
				list.push_back({"mic", "Microphone", "mic", audio.mic_volume, audio.mic_muted, true, audio.sources.size() > 1});
			if (Display::current.screen.present)
				list.push_back({"brightness", "Brightness", "brightness", Display::current.screen.percent(), false, false, false});
			if (Display::current.kbd.present)
				list.push_back({"kbd", "Keyboard", "kbd", Display::current.kbd.percent(), false, false, false});
			return list;
		}

		string draw() {
			const auto list = items();
			const string line_color = Theme::c("sliders_box");
			string out = createBox(x, y, width, height, line_color, true, "sliders", "", 2);

			if (list.empty()) {
				out += Mv::to(y + 1, x + 2) + Theme::c("inactive_fg") + uresize("No audio or backlight devices found", width - 4);
				return out + Fx::reset;
			}

			const bool any_expand = std::ranges::any_of(list, &slider::expandable);
			const int meter_width = width - 2 - (slider_label_width + 9 + (any_expand ? 2 : 0));
			const int meter_x = x + 3 + slider_label_width;

			for (int i = 0; const auto& s : list) {
				const int row = y + 1 + i++;
				const bool is_focused = (focused == "slider:" + s.id);
				const string bg = (is_focused ? Theme::c("selected_bg") : "");
				const string fg = (is_focused ? Theme::c("selected_fg") : Theme::c("main_fg"));
				const int max_val = max_value(s.id);

				out += Mv::to(row, x + 1) + Fx::reset + bg + ' ' + (is_focused ? Fx::b : "") + fg + ljust(s.label, slider_label_width) + Fx::ub + ' ';
				if (s.muted)
					out += Theme::c("inactive_fg") + Symbols::meter * meter_width;
				else
					out += Meter(meter_width, s.gradient)((int)round((double)s.value * 100 / max_val)) + bg;
				out += bg + ' ' + (s.muted ? Theme::c("inactive_fg") + rjust("muted", 5) : fg + rjust(to_string(s.value) + '%', 5));
				if (any_expand)
					out += string(" ") + (s.expandable ? (is_focused ? fg : Theme::c("hi_fg")) + Symbols::expand() : " ");
				out += ' ' + Fx::reset;

				add_focus("slider:" + s.id, x + 1, row, width - 2, 1);
				if (s.muteable) map_mouse("mute:" + s.id, x + 1, row, slider_label_width + 2, 1);
				map_mouse("slider:" + s.id, meter_x, row, max(1, meter_width), 1);
				if (s.expandable) map_mouse("expand:" + s.id, x + width - 4, row, 3, 1);
			}

			return out + Fx::reset;
		}
	}

	//* Toggles box ------------------------------------------------------------------------------------------------------------>
	namespace Toggles {
		int x, y, width, height, cols;
		bool shown;

		vector<toggle> items() {
			vector<toggle> list;
			const auto& net = Network::current;
			const auto& bt = Bluetooth::current;
			const auto& power = Power::current;
			const auto& desktop = Desktop::current;

			if (net.has_wired)
				list.push_back({"wired", "Wired", (net.wired_connected ? net.wired_connection : "Disconnected"), 'i', net.wired_connected, false});
			if (net.has_wifi) {
				string subtitle = (not net.wifi_enabled ? "Off"
					: not net.wifi_connection.empty() ? net.wifi_connection
					: net.wifi_state.starts_with("connecting") ? "Connecting..." : "Disconnected");
				list.push_back({"wifi", "Wi-Fi", subtitle, 'w', net.wifi_enabled, true});
			}
			if (bt.available) {
				const int connected = bt.connected_count();
				string subtitle = "On";
				if (not bt.powered) subtitle = "Off";
				else if (connected > 1) subtitle = to_string(connected) + " Devices";
				else if (connected == 1) subtitle = std::ranges::find_if(bt.devices, &Bluetooth::bt_device::connected)->name;
				list.push_back({"bluetooth", "Bluetooth", subtitle, 'b', bt.powered, true});
			}
			if (power.has_profiles)
				list.push_back({"power_mode", "Power Mode", Power::profile_name(power.profile), 'r', power.profile != "balanced", true});
			if (desktop.available) {
				list.push_back({"night_light", "Night Light", (desktop.night_light ? "On" : "Off"), 'n', desktop.night_light, false});
				list.push_back({"dark_style", "Dark Style", (desktop.dark_style ? "On" : "Off"), 'd', desktop.dark_style, false});
			}
			if (not net.vpns.empty()) {
				auto active = std::ranges::find_if(net.vpns, &Network::vpn_connection::active);
				list.push_back({"vpn", "VPN", (active != net.vpns.end() ? active->name : "Off"), 'v', active != net.vpns.end(), true});
			}
			if (desktop.has_rfkill)
				list.push_back({"airplane", "Airplane Mode", (desktop.airplane ? "On" : "Off"), 'a', desktop.airplane, false});
			if (desktop.available)
				list.push_back({"dnd", "Do Not Disturb", (desktop.dnd ? "On" : "Off"), 'o', desktop.dnd, false});
			return list;
		}

		string draw() {
			const auto list = items();
			const string line_color = Theme::c("toggles_box");
			string out = createBox(x, y, width, height, line_color, true, "toggles", "", 3);

			//? Key hints in bottom border
			const bool vim_keys = Config::getB("vim_keys");
			int hx = x + 2;
			for (const auto& [key, label] : vector<std::pair<string, string>>{
					{Symbols::enter, "toggle"}, {"e", "expand"}, {(vim_keys ? "H" : "h"), "help"}, {"q", "quit"}}) {
				const int w = ulen(key) + label.size() + 3;
				if (hx + w >= x + width - 1) break;
				out += hint(hx, y + height - 1, key, label, line_color);
				hx += w + 1;
			}

			if (list.empty()) {
				out += Mv::to(y + 1, x + 2) + Theme::c("inactive_fg") + uresize("No quick settings available", width - 4);
				return out + Fx::reset;
			}

			const int base_width = (width - 2) / cols;
			for (int i = 0; const auto& t : list) {
				const int cx = x + 1 + (i % cols) * base_width;
				const int cy = y + 1 + (i / cols) * toggle_height;
				//? Last column absorbs the remainder so the grid fills the box
				const int cell_width = base_width + ((i % cols) == cols - 1 ? (width - 2) % cols : 0);
				i++;
				const bool is_focused = (focused == "toggle:" + t.id);
				const string border = (is_focused ? Theme::c("hi_fg") : t.active ? Theme::c("main_fg") : Theme::c("div_line"));
				const string bg = (t.active ? Theme::c("selected_bg") : "");
				const string fg = (t.active ? Theme::c("selected_fg") : Theme::c("main_fg"));
				const int text_width = cell_width - 4 - (t.expandable ? 2 : 0);

				out += createBox(cx, cy, cell_width, toggle_height, border, true);
				out += Mv::to(cy + 1, cx + 1) + bg + ' ' + Fx::b + hotkey(ljust(t.title, text_width, true, true), t.key, fg, t.active) + Fx::ub
					+ (t.expandable ? ' ' + (t.active ? fg : Theme::c("hi_fg")) + Symbols::expand() : "") + bg + ' ';
				out += Mv::to(cy + 2, cx + 1) + bg + ' ' + (t.active ? fg : Theme::c("inactive_fg"))
					+ ljust(t.subtitle, text_width + (t.expandable ? 2 : 0), true, true) + ' ' + Fx::reset;

				add_focus("toggle:" + t.id, cx, cy, cell_width, toggle_height);
				map_mouse("toggle:" + t.id, cx, cy, cell_width - (t.expandable ? 4 : 0), toggle_height);
				if (t.expandable) map_mouse("expand:" + t.id, cx + cell_width - 4, cy, 4, toggle_height);
			}

			return out + Fx::reset;
		}
	}

	void calcSizes() {
		const int width = Term::width;
		const auto& boxes = Config::current_boxes;
		Status::shown = v_contains(boxes, "status");
		Sliders::shown = v_contains(boxes, "sliders");
		Toggles::shown = v_contains(boxes, "toggles");

		int cy = 1;
		if (Status::shown) {
			Status::x = 1;
			Status::y = cy;
			Status::width = width;
			Status::height = 3;
			cy += Status::height;
		}
		if (Sliders::shown) {
			Sliders::x = 1;
			Sliders::y = cy;
			Sliders::width = width;
			Sliders::height = max(1, (int)Sliders::items().size()) + 2;
			cy += Sliders::height;
		}
		if (Toggles::shown) {
			const int count = max(1, (int)Toggles::items().size());
			Toggles::x = 1;
			Toggles::y = cy;
			Toggles::width = width;
			Toggles::cols = clamp((width - 2) / toggle_min_width, 1, min(4, count));
			Toggles::height = (int)std::ceil((double)count / Toggles::cols) * toggle_height + 2;
			cy += Toggles::height;
		}
		total_height = cy - 1;
	}

	string draw_all(bool force_redraw) {
		string out = Term::sync_start + (force_redraw ? Term::clear : "");
		calcSizes();

		if (Term::width < min_width or total_height > Term::height) {
			Input::mouse_mappings.clear();
			focusables.clear();
			const int cy = max(1, Term::height / 2 - 2);
			out += Fx::reset + Term::clear;
			const vector<string> lines = {
				Theme::c("title") + Fx::b + "Terminal size too small:" + Fx::ub,
				Theme::c("main_fg") + std::format("Width = {} Height = {}", Term::width.load(), Term::height.load()),
				"",
				Theme::c("title") + Fx::b + "Needed for current config:" + Fx::ub,
				Theme::c("main_fg") + std::format("Width = {} Height = {}", min_width, total_height),
			};
			for (int i = 0; const auto& line : lines) {
				const int len = ulen(Fx::uncolor(line));
				out += Mv::to(cy + i++, max(1, Term::width / 2 - len / 2)) + line;
			}
			return out + Fx::reset + Term::sync_end;
		}

		//? Draw boxes, redrawing once if the focused element disappeared since the last draw
		string boxes;
		for (int pass = 0; pass < 2; pass++) {
			Input::mouse_mappings.clear();
			focusables.clear();
			boxes.clear();
			if (Status::shown) boxes += Status::draw();
			if (Sliders::shown) boxes += Sliders::draw();
			if (Toggles::shown) boxes += Toggles::draw();
			if (focusables.empty() or std::ranges::find(focusables, focused, &Focusable::id) != focusables.end()) break;
			focused = focusables.front().id;
		}

		if (Config::current_boxes.empty()) {
			const string msg = "No boxes shown, press 1-3 to toggle boxes";
			boxes += Mv::to(Term::height / 2, max(1, Term::width / 2 - (int)msg.size() / 2)) + Theme::c("inactive_fg") + msg;
		}

		out += boxes;
		if (total_height < Term::height) out += Mv::to(total_height + 1, 1) + Fx::reset + Term::clear_end;

		if (Menu::active) {
			Menu::draw();
			out += Global::overlay;
		}

		return out + Fx::reset + Term::sync_end;
	}
}
