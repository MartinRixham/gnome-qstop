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
#include <functional>

#include "qstop_config.hpp"
#include "qstop_draw.hpp"
#include "qstop_menu.hpp"
#include "qstop_shared.hpp"
#include "qstop_theme.hpp"
#include "qstop_tools.hpp"

using std::clamp;
using std::max;
using std::min;

using namespace Tools;

namespace Menu {

	atomic<bool> active{};
	std::unordered_map<string, Input::Mouse_loc> mouse_mappings;

	namespace {
		int current_menu = -1;
		string list_id;
		int selected = 0, offset = 0, help_page = 0;

		//? Geometry of the last drawn menu box, clicks outside it close the menu
		int box_x = 0, box_y = 0, box_width = 0, box_height = 0;

		Network::wifi_network password_network;
		string password;
		bool show_password = false;

		string confirm_title;
		vector<string> confirm_message;
		std::function<void()> confirm_action;
		int confirm_selected = 0;

		struct item {
			string label;
			string detail{};
			int detail_width = 0;
			bool radio = false;
			bool marked = false;
			std::function<void()> action{};
			bool closes = false;
		};

		bool outside_box() {
			const auto& [col, line] = Input::mouse_pos;
			return col < box_x or col >= box_x + box_width or line < box_y or line >= box_y + box_height;
		}

		void set_box(int x, int y, int width, int height) {
			box_x = x;
			box_y = y;
			box_width = width;
			box_height = height;
		}

		//* Draw key hints left to right in the bottom border of the menu box
		string hints(const vector<std::pair<string, string>>& keys) {
			string out;
			int hx = box_x + 2;
			for (const auto& [key, label] : keys) {
				const int w = ulen(key) + label.size() + 3;
				if (hx + w >= box_x + box_width - 1) break;
				out += Mv::to(box_y + box_height - 1, hx) + Theme::c("hi_fg") + Symbols::title_left_down + Fx::b + key
					+ Theme::c("title") + ' ' + label + Fx::ub + Theme::c("hi_fg") + Symbols::title_right_down;
				hx += w + 1;
			}
			return out;
		}

		string signal_bars(int signal) {
			const int bars = (signal >= 80 ? 4 : signal >= 55 ? 3 : signal >= 30 ? 2 : 1);
			string out;
			for (int i = 0; i < 4; i++) {
				out += (i < bars ? Theme::c("main_fg") : Theme::c("meter_bg"))
					+ (Config::getB("tty_mode") ? "|" : Symbols::signal.at(i));
			}
			return out;
		}

		void open_password(const Network::wifi_network& network) {
			password_network = network;
			password.clear();
			show_password = false;
			current_menu = Password;
		}

		string list_title(const string& id) {
			if (id == "wifi") return "wi-fi";
			if (id == "bluetooth") return "bluetooth";
			if (id == "power_mode") return "power mode";
			if (id == "vpn") return "vpn";
			if (id == "volume") return "sound output";
			if (id == "mic") return "sound input";
			if (id == "power") return "power";
			return id;
		}

		vector<item> build_items(const string& id) {
			vector<item> items;
			if (id == "wifi") {
				const auto& net = Network::current;
				if (not net.wifi_enabled) {
					items.push_back({.label = "Turn On Wi-Fi", .action = []{ Network::set_wifi(true); }});
				}
				else {
					for (const auto& network : net.networks) {
						const string security = (network.security.empty() or network.security == "--" ? "" : network.security);
						items.push_back({
							.label = network.ssid,
							.detail = Theme::c("inactive_fg") + (security.empty() ? "" : security + ' ') + signal_bars(network.signal),
							.detail_width = (int)(security.empty() ? 0 : security.size() + 1) + 4,
							.radio = true,
							.marked = network.active,
							.action = [network]{
								if (network.active) Network::disconnect_wifi();
								else if (network.known or network.security.empty() or network.security == "--") Network::connect(network);
								else open_password(network);
							},
						});
					}
					if (net.networks.empty()) items.push_back({.label = "No networks found"});
					items.push_back({.label = "Scan for Networks", .action = []{
						Global::notify("Scanning for networks...", 3000);
						Network::rescan();
					}});
					items.push_back({.label = "Turn Off Wi-Fi", .action = []{ Network::set_wifi(false); }});
				}
				items.push_back({.label = "All Networks...", .action = []{ Power::open_settings("wifi"); }, .closes = true});
			}
			else if (id == "bluetooth") {
				const auto& bt = Bluetooth::current;
				if (not bt.powered) {
					items.push_back({.label = "Turn On Bluetooth", .action = []{ Bluetooth::set_power(true); }});
				}
				else {
					for (const auto& device : bt.devices) {
						items.push_back({
							.label = device.name,
							.detail = Theme::c("inactive_fg") + (device.connected ? "Connected" : ""),
							.detail_width = (device.connected ? 9 : 0),
							.radio = true,
							.marked = device.connected,
							.action = [device]{ Bluetooth::set_connected(device, not device.connected); },
						});
					}
					if (bt.devices.empty()) items.push_back({.label = "No paired devices"});
					items.push_back({.label = "Turn Off Bluetooth", .action = []{ Bluetooth::set_power(false); }});
				}
				items.push_back({.label = "Bluetooth Settings...", .action = []{ Power::open_settings("bluetooth"); }, .closes = true});
			}
			else if (id == "power_mode") {
				for (const auto& profile : Power::current.profiles) {
					items.push_back({
						.label = Power::profile_name(profile),
						.radio = true,
						.marked = (profile == Power::current.profile),
						.action = [profile]{ Power::set_profile(profile); },
					});
				}
				items.push_back({.label = "Power Settings...", .action = []{ Power::open_settings("power"); }, .closes = true});
			}
			else if (id == "vpn") {
				for (const auto& vpn : Network::current.vpns) {
					items.push_back({
						.label = vpn.name,
						.radio = true,
						.marked = vpn.active,
						.action = [vpn]{ Network::set_vpn(vpn.name, not vpn.active); },
					});
				}
				items.push_back({.label = "VPN Settings...", .action = []{ Power::open_settings("network"); }, .closes = true});
			}
			else if (is_in(id, "volume", "mic")) {
				const bool output = (id == "volume");
				const auto& audio = Audio::current;
				for (const auto& device : (output ? audio.sinks : audio.sources)) {
					items.push_back({
						.label = device.description,
						.radio = true,
						.marked = (device.name == (output ? audio.sink : audio.source)),
						.action = [device, output]{
							if (output) Audio::set_sink(device.name);
							else Audio::set_source(device.name);
						},
					});
				}
				const bool muted = (output ? audio.muted : audio.mic_muted);
				items.push_back({.label = (muted ? "Unmute" : "Mute"), .action = [output, muted]{
					if (output) Audio::set_mute(not muted);
					else Audio::set_mic_mute(not muted);
				}});
				items.push_back({.label = "Sound Settings...", .action = []{ Power::open_settings("sound"); }, .closes = true});
			}
			else if (id == "power") {
				items.push_back({.label = "Suspend", .action = []{ Power::run_action(Power::Action::Suspend); }, .closes = true});
				items.push_back({.label = "Restart...", .action = []{
					confirm("restart", {"Restart the system now?"}, []{ Power::run_action(Power::Action::Restart); });
				}});
				items.push_back({.label = "Power Off...", .action = []{
					confirm("power off", {"Power off the system now?"}, []{ Power::run_action(Power::Action::PowerOff); });
				}});
				items.push_back({.label = "Log Out...", .action = []{
					confirm("log out", {"Log out of " + username() + "?", "Unsaved work may be lost."}, []{ Power::run_action(Power::Action::LogOut); });
				}});
			}
			return items;
		}

		vector<array<string, 2>> help_text() {
			const bool vim_keys = Config::getB("vim_keys");
			vector<array<string, 2>> text = {
				{"q, Ctrl-C", "Quit."},
				{(vim_keys ? "H, ?, F1" : "h, ?, F1"), "Show this help."},
				{"1, 2, 3", "Toggle the status, sliders and toggles boxes."},
				{(vim_keys ? "Arrows, hjkl" : "Arrows"), "Move focus between elements."},
				{"Tab, Shift-Tab", "Cycle focus."},
				{"Enter, Space", "Activate focused element, mute focused slider."},
				{"e", "Open the submenu of the focused element."},
				{"Left, Right", "Adjust focused slider."},
				{"+, -", "Adjust focused slider or output volume."},
				{"m, M", "Mute or unmute output / microphone."},
				{"w, W", "Toggle Wi-Fi / show Wi-Fi networks."},
				{"b, B", "Toggle Bluetooth / show Bluetooth devices."},
				{"i", "Toggle wired connection."},
				{"r", "Choose power mode."},
				{"n", "Toggle Night Light."},
				{"d", "Toggle Dark Style."},
				{"v", "Toggle VPN."},
				{"a", "Toggle Airplane Mode."},
				{"o", "Toggle Do Not Disturb."},
				{"s", "Take a screenshot."},
				{"t", "Open GNOME Settings."},
				{(vim_keys ? "L" : "l"), "Lock the screen."},
				{"p", "Suspend, restart, power off or log out."},
				{"Mouse click", "Activate element, click " + Symbols::expand() + " to open submenu."},
				{"Mouse scroll", "Adjust slider under cursor."},
				{"Esc", "Close menus."},
			};
			return text;
		}

		void draw_help() {
			const auto text = help_text();
			const int width = min(Term::width - 2, 72);
			const int height = min(Term::height - 2, (int)text.size() + 3);
			const int x = max(1, Term::width / 2 - width / 2 + 1);
			const int y = max(1, Term::height / 2 - height / 2 + 1);
			const int rows = max(1, height - 3);
			const int pages = (int)std::ceil((double)text.size() / rows);
			help_page = clamp(help_page, 0, max(0, pages - 1));
			set_box(x, y, width, height);

			auto& out = Global::overlay;
			out = Draw::createBox(x, y, width, height, Theme::c("hi_fg"), true, "help");
			if (pages > 1) {
				out += Mv::to(y + height - 1, x + 2) + Theme::c("hi_fg") + Symbols::title_left_down + Fx::b + Symbols::up + Theme::c("title") + " page "
					+ to_string(help_page + 1) + '/' + to_string(pages) + ' ' + Theme::c("hi_fg") + Symbols::down + Fx::ub + Symbols::title_right_down;
			}
			out += Mv::to(y + 1, x + 1) + Theme::c("title") + Fx::b + cjust("Key:", 20) + "Description:";
			for (int c = 0, i = rows * help_page; c < rows and i < (int)text.size(); c++, i++) {
				out += Mv::to(y + 2 + c, x + 1) + Theme::c("hi_fg") + Fx::b + cjust(text[i][0], 20, true)
					+ Theme::c("main_fg") + Fx::ub + uresize(text[i][1], max(0, width - 22), true);
			}
			out += Fx::reset;
		}

		void draw_list() {
			const auto items = build_items(list_id);
			const int count = items.size();
			selected = clamp(selected, 0, max(0, count - 1));

			int content = 28;
			for (const auto& it : items)
				content = max(content, 4 + (int)ulen(it.label, true) + (it.detail_width > 0 ? it.detail_width + 2 : 0));
			//? Wide enough for the select, activate and close hints in the bottom border
			const int width = min(Term::width - 2, max(44, content + 4));
			const int pad = (Term::height >= count + 6 ? 1 : 0);
			const int rows = max(1, min(count, Term::height - 2 - pad * 2));
			const int height = rows + 2 + pad * 2;
			const int x = max(1, Term::width / 2 - width / 2 + 1);
			const int y = max(1, Term::height / 2 - height / 2 + 1);
			set_box(x, y, width, height);

			if (selected < offset) offset = selected;
			if (selected >= offset + rows) offset = selected - rows + 1;
			offset = clamp(offset, 0, max(0, count - rows));

			auto& out = Global::overlay;
			out = Draw::createBox(x, y, width, height, Theme::c("hi_fg"), true, list_title(list_id));
			out += hints({{Symbols::up + Symbols::down, "select"}, {Symbols::enter, "activate"}, {"esc", "close"}});

			if (count > rows) {
				const string position = to_string(selected + 1) + '/' + to_string(count);
				out += Mv::to(y, x + width - (int)position.size() - 4) + Theme::c("hi_fg") + Symbols::title_left + Theme::c("title")
					+ position + Theme::c("hi_fg") + Symbols::title_right;
			}

			for (int r = 0; r < rows and offset + r < count; r++) {
				const int i = offset + r;
				const int row = y + 1 + pad + r;
				const auto& it = items.at(i);
				const bool is_selected = (i == selected);
				const string bg = (is_selected ? Theme::c("selected_bg") : "");
				const string fg = (is_selected ? Theme::c("selected_fg") : it.action ? Theme::c("main_fg") : Theme::c("inactive_fg"));
				const string marker = (it.radio ? (it.marked ? Symbols::radio_on : Symbols::radio_off) : " ");
				const int label_width = width - 2 - 4 - (it.detail_width > 0 ? it.detail_width + 1 : 0);

				out += Mv::to(row, x + 1) + Fx::reset + bg + ' ' + (it.marked and not is_selected ? Theme::c("hi_fg") : fg) + marker + ' '
					+ fg + (is_selected ? Fx::b : "") + ljust(it.label, max(0, label_width), true, true) + Fx::ub;
				if (it.detail_width > 0) out += ' ' + (is_selected ? Fx::uncolor(it.detail) : it.detail) + bg;
				out += ' ' + Fx::reset;

				mouse_mappings["item:" + to_string(i)] = {row, x + 1, 1, width - 2};
			}
			out += Fx::reset;
		}

		void draw_password() {
			const int width = min(Term::width - 2, 54);
			const int height = 8;
			const int x = max(1, Term::width / 2 - width / 2 + 1);
			const int y = max(1, Term::height / 2 - height / 2 + 1);
			set_box(x, y, width, height);

			auto& out = Global::overlay;
			out = Draw::createBox(x, y, width, height, Theme::c("hi_fg"), true, "wi-fi password");
			out += hints({{Symbols::enter, "connect"}, {"tab", (show_password ? "hide" : "show")}, {"esc", "back"}});
			out += Mv::to(y + 2, x + 2) + Theme::c("title") + Fx::b + uresize("Connect to " + password_network.ssid, width - 4, true) + Fx::ub;
			out += Mv::to(y + 3, x + 2) + Theme::c("inactive_fg") + uresize("Security: " + password_network.security, width - 4, true);

			const string label = "Password: ";
			const int field_width = max(1, width - 4 - (int)label.size() - 1);
			string text = (show_password ? password : string("*") * (int64_t)ulen(password));
			//? Show the end of the text if it doesn't fit, removing whole UTF8 characters from the front
			while ((int)ulen(text, true) > field_width) {
				size_t n = 1;
				while (n < text.size() and (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) n++;
				text.erase(0, n);
			}
			out += Mv::to(y + 5, x + 2) + Theme::c("main_fg") + label + Fx::ul + Theme::c("title") + text + Fx::uul
				+ Theme::c("hi_fg") + Fx::bl + '_' + Fx::ubl + Fx::reset;
		}

		void draw_confirm() {
			int content = 30;
			for (const auto& line : confirm_message) content = max(content, (int)ulen(line, true));
			const int width = min(Term::width - 2, content + 6);
			const int height = confirm_message.size() + 7;
			const int x = max(1, Term::width / 2 - width / 2 + 1);
			const int y = max(1, Term::height / 2 - height / 2 + 1);
			set_box(x, y, width, height);

			const bool rounded = Config::getB("rounded_corners") and not Config::getB("tty_mode");
			const auto& right_up = (rounded ? Symbols::round_right_up : Symbols::right_up);
			const auto& left_up = (rounded ? Symbols::round_left_up : Symbols::left_up);
			const auto& right_down = (rounded ? Symbols::round_right_down : Symbols::right_down);
			const auto& left_down = (rounded ? Symbols::round_left_down : Symbols::left_down);
			const string button_left = left_up + Symbols::h_line * 6 + Mv::l(7) + Mv::d(2) + left_down + Symbols::h_line * 6 + Mv::l(7) + Mv::u(1) + Symbols::v_line;
			const string button_right = Symbols::v_line + Mv::l(7) + Mv::u(1) + Symbols::h_line * 6 + right_up + Mv::l(7) + Mv::d(2) + Symbols::h_line * 6 + right_down + Mv::u(2);

			auto& out = Global::overlay;
			out = Draw::createBox(x, y, width, height, Theme::c("hi_fg"), true, confirm_title);
			for (int i = 0; const auto& line : confirm_message) {
				out += Mv::to(y + 2 + i++, x + max(1, width / 2 - (int)ulen(line, true) / 2)) + Theme::c("main_fg") + line;
			}

			const int pos = max(1, width / 2 - 14);
			const int by = y + height - 4;
			const auto first_color = (confirm_selected == 0 ? Theme::c("hi_fg") : Theme::c("div_line"));
			const auto second_color = (confirm_selected == 1 ? Theme::c("hi_fg") : Theme::c("div_line"));
			out += Mv::to(by, x + pos) + Fx::b + first_color + button_left + (confirm_selected == 0 ? Theme::c("title") : Theme::c("main_fg") + Fx::ub)
				+ "    Yes    " + first_color + button_right;
			out += Mv::r(2) + Fx::b + second_color + button_left + (confirm_selected == 1 ? Theme::c("title") : Theme::c("main_fg") + Fx::ub)
				+ "    No    " + second_color + button_right;
			mouse_mappings["button:yes"] = {by, x + pos, 3, 13};
			mouse_mappings["button:no"] = {by, x + pos + 15, 3, 12};
			out += Fx::reset;
		}

		void process_list(const string& key) {
			const auto items = build_items(list_id);
			const int count = items.size();

			const auto activate = [&](int index) {
				if (index < 0 or index >= count or not items.at(index).action) return;
				const int menu_before = current_menu;
				items.at(index).action();
				if (items.at(index).closes and current_menu == menu_before) close();
			};

			if (is_in(key, "escape", "backspace", "q")) close();
			else if (count == 0) return;
			else if (is_in(key, "up", "k", "mouse_scroll_up")) selected = (selected - 1 + count) % count;
			else if (is_in(key, "down", "j", "mouse_scroll_down", "tab")) selected = (selected + 1) % count;
			else if (key == "shift_tab") selected = (selected - 1 + count) % count;
			else if (is_in(key, "home", "g")) selected = 0;
			else if (is_in(key, "end", "G")) selected = count - 1;
			else if (key == "page_up") selected = max(0, selected - 10);
			else if (key == "page_down") selected = min(count - 1, selected + 10);
			else if (is_in(key, "enter", "space", "right")) activate(selected);
			else if (key.starts_with("item:")) {
				try { selected = std::stoi(key.substr(5)); } catch (const std::exception&) { return; }
				activate(selected);
			}
			else if (key == "mouse_click" and outside_box()) close();
		}

		void process_password(const string& key) {
			if (is_in(key, "escape") or (key == "mouse_click" and outside_box())) {
				password.clear();
				current_menu = List;
			}
			else if (key == "enter") {
				//? WPA passphrases are at least 8 characters
				if (password.size() < 8 and not password_network.security.contains("WEP")) {
					Global::notify("Password must be at least 8 characters", 3000);
					return;
				}
				Network::connect(password_network, password);
				password.clear();
				current_menu = List;
			}
			else if (key == "tab") show_password = not show_password;
			else if (key == "backspace") {
				while (not password.empty() and (static_cast<unsigned char>(password.back()) & 0xC0) == 0x80) password.pop_back();
				if (not password.empty()) password.pop_back();
			}
			else if (key == "space") password += ' ';
			else if (ulen(key) == 1 and not key.starts_with("mouse_") and static_cast<unsigned char>(key.at(0)) >= 0x20) password += key;
		}

		void process_confirm(const string& key) {
			if (is_in(key, "escape", "backspace", "q", "n", "N", "button:no") or (key == "mouse_click" and outside_box())) close();
			else if (is_in(key, "y", "Y", "button:yes") or (is_in(key, "enter", "space") and confirm_selected == 0)) {
				auto action = std::move(confirm_action);
				close();
				if (action) action();
			}
			else if (is_in(key, "enter", "space")) close();
			else if (is_in(key, "left", "right", "tab", "shift_tab", "h", "l")) confirm_selected = 1 - confirm_selected;
		}
	}

	void show(int menu, const string& id) {
		if (menu == List and (current_menu != List or id != list_id)) {
			selected = 0;
			offset = 0;
			//? GNOME rescans for networks when the Wi-Fi menu is opened
			if (id == "wifi" and Network::current.wifi_enabled) Network::rescan();
		}
		if (menu == Help) help_page = 0;
		current_menu = menu;
		if (menu == List) list_id = id;
		active = true;
		Runner::run(menu == List);
	}

	void close() {
		active = false;
		current_menu = -1;
		Global::overlay.clear();
		mouse_mappings.clear();
		password.clear();
		confirm_action = nullptr;
	}

	void confirm(const string& title, const vector<string>& message, std::function<void()> action) {
		if (not Config::getB("confirm_power_actions")) {
			close();
			if (action) action();
			return;
		}
		confirm_title = title;
		confirm_message = message;
		confirm_action = std::move(action);
		confirm_selected = 0;
		current_menu = Confirm;
		active = true;
	}

	void process(const string& key) {
		if (key.empty()) return;
		switch (current_menu) {
			case Help:
				if (is_in(key, "down", "j", "page_down", "tab", "mouse_scroll_down")) help_page++;
				else if (is_in(key, "up", "k", "page_up", "shift_tab", "mouse_scroll_up")) help_page = max(0, help_page - 1);
				else close();
				break;
			case List:
				process_list(key);
				break;
			case Password:
				process_password(key);
				break;
			case Confirm:
				process_confirm(key);
				break;
			default:
				close();
		}
	}

	void draw() {
		mouse_mappings.clear();
		switch (current_menu) {
			case Help: draw_help(); break;
			case List: draw_list(); break;
			case Password: draw_password(); break;
			case Confirm: draw_confirm(); break;
			default: Global::overlay.clear();
		}
	}
}
