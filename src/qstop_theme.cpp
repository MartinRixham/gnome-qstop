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
#include <fstream>
#include <ranges>

#include <unistd.h>

#include "qstop_config.hpp"
#include "qstop_theme.hpp"
#include "qstop_tools.hpp"

using std::round;
using std::stoi;
using std::to_string;
using std::views::iota;

using namespace Tools;

namespace fs = std::filesystem;

namespace Theme {

	fs::path theme_dir;
	fs::path user_theme_dir;
	vector<string> themes;
	std::unordered_map<string, string> colors;
	std::unordered_map<string, array<int, 3>> rgbs;
	std::unordered_map<string, array<string, 101>> gradients;

	const std::unordered_map<string, string> Default_theme = {
		{ "main_bg", "#00" },
		{ "main_fg", "#cc" },
		{ "title", "#ee" },
		{ "hi_fg", "#b54040" },
		{ "selected_bg", "#6a2f2f" },
		{ "selected_fg", "#ee" },
		{ "inactive_fg", "#40" },
		{ "meter_bg", "#40" },
		{ "div_line", "#30" },
		{ "status_box", "#556d59" },
		{ "sliders_box", "#6c6c4b" },
		{ "toggles_box", "#5c588d" },
		{ "volume_start", "#77ca9b" },
		{ "volume_mid", "#cbc06c" },
		{ "volume_end", "#dc4c4c" },
		{ "mic_start", "#7d4180" },
		{ "mic_mid", "#b986bb" },
		{ "mic_end", "#dcafde" },
		{ "brightness_start", "#4e3f0e" },
		{ "brightness_mid", "#ffd77a" },
		{ "brightness_end", "#ffb814" },
		{ "kbd_start", "#163350" },
		{ "kbd_mid", "#74e6fc" },
		{ "kbd_end", "#26c5ff" },
		{ "battery_start", "#dc4c4c" },
		{ "battery_mid", "#cbc06c" },
		{ "battery_end", "#77ca9b" },
	};

	//* qstop color names that fall back to a btop color name when a btop theme file is loaded
	const std::unordered_map<string, string> btop_aliases = {
		{ "status_box", "cpu_box" },
		{ "sliders_box", "mem_box" },
		{ "toggles_box", "net_box" },
		{ "volume_start", "cpu_start" },
		{ "volume_mid", "cpu_mid" },
		{ "volume_end", "cpu_end" },
		{ "mic_start", "upload_start" },
		{ "mic_mid", "upload_mid" },
		{ "mic_end", "upload_end" },
		{ "brightness_start", "available_start" },
		{ "brightness_mid", "available_mid" },
		{ "brightness_end", "available_end" },
		{ "kbd_start", "cached_start" },
		{ "kbd_mid", "cached_mid" },
		{ "kbd_end", "cached_end" },
		{ "battery_start", "free_start" },
		{ "battery_mid", "free_mid" },
		{ "battery_end", "free_end" },
	};

	const std::unordered_map<string, string> TTY_theme = {
		{ "main_bg", "\x1b[0;40m" },
		{ "main_fg", "\x1b[37m" },
		{ "title", "\x1b[97m" },
		{ "hi_fg", "\x1b[91m" },
		{ "selected_bg", "\x1b[41m" },
		{ "selected_fg", "\x1b[97m" },
		{ "inactive_fg", "\x1b[90m" },
		{ "meter_bg", "\x1b[90m" },
		{ "div_line", "\x1b[90m" },
		{ "status_box", "\x1b[32m" },
		{ "sliders_box", "\x1b[33m" },
		{ "toggles_box", "\x1b[35m" },
		{ "volume_start", "\x1b[92m" },
		{ "volume_mid", "\x1b[93m" },
		{ "volume_end", "\x1b[91m" },
		{ "mic_start", "\x1b[35m" },
		{ "mic_mid", "" },
		{ "mic_end", "\x1b[95m" },
		{ "brightness_start", "\x1b[33m" },
		{ "brightness_mid", "" },
		{ "brightness_end", "\x1b[93m" },
		{ "kbd_start", "\x1b[36m" },
		{ "kbd_mid", "" },
		{ "kbd_end", "\x1b[96m" },
		{ "battery_start", "\x1b[91m" },
		{ "battery_mid", "\x1b[93m" },
		{ "battery_end", "\x1b[92m" },
	};

	namespace {
		//* Convert 24-bit colors to 256 colors
		int truecolor_to_256(const int& r, const int& g, const int& b) {
			//? Use upper 232-255 greyscale values if the downscaled red, green and blue are the same value
			if (const int red = round((double)r / 11); red == round((double)g / 11) and red == round((double)b / 11)) {
				return 232 + std::min(red, 23);
			}
			//? Else use 6x6x6 color cube to calculate approximate colors
			else {
				return round((double)r / 51) * 36 + round((double)g / 51) * 6 + round((double)b / 51) + 16;
			}
		}

		//* Return array of red, green and blue in decimal from a "#rrggbb" or "#gg" hex string, -1 on invalid
		array<int, 3> hex_to_dec(string hexa) {
			if (hexa.size() > 1) {
				hexa.erase(0, 1);
				for (auto& c : hexa) {
					if (not isxdigit(c))
						return array{-1, -1, -1};
				}

				if (hexa.size() == 2) {
					int h_int = stoi(hexa, nullptr, 16);
					return array{h_int, h_int, h_int};
				}
				else if (hexa.size() == 6) {
					return array{
						stoi(hexa.substr(0, 2), nullptr, 16),
						stoi(hexa.substr(2, 2), nullptr, 16),
						stoi(hexa.substr(4, 2), nullptr, 16)
					};
				}
			}
			return {-1 ,-1 ,-1};
		}

		//* Generate colors and rgb decimal vectors for the theme
		void generateColors(const std::unordered_map<string, string>& source) {
			bool t_to_256 = Config::getB("lowcolor");
			colors.clear(); rgbs.clear();
			for (const auto& [name, color] : Default_theme) {
				if (name == "main_bg" and not Config::getB("theme_background")) {
					colors[name] = "\x1b[49m";
					rgbs[name] = {-1, -1, -1};
					continue;
				}
				const string depth = (name.ends_with("bg") and name != "meter_bg") ? "bg" : "fg";

				//? Look for the qstop name first, then the equivalent btop name
				string value;
				bool found = false;
				if (auto it = source.find(name); it != source.end()) {
					value = it->second;
					found = true;
				}
				else if (auto alias = btop_aliases.find(name); alias != btop_aliases.end()) {
					if (auto it2 = source.find(alias->second); it2 != source.end()) {
						value = it2->second;
						found = true;
					}
				}

				if (found) {
					if (name == "main_bg" and value.empty()) {
						colors[name] = "\x1b[49m";
						rgbs[name] = {-1, -1, -1};
						continue;
					}
					else if (value.empty() and (name.ends_with("_mid") or name.ends_with("_end"))) {
						colors[name] = "";
						rgbs[name] = {-1, -1, -1};
						continue;
					}
					else if (value.starts_with('#')) {
						colors[name] = hex_to_color(value, t_to_256, depth);
						rgbs[name] = hex_to_dec(value);
					}
					else if (not value.empty()) {
						auto t_rgb = ssplit(value);
						if (t_rgb.size() == 3) {
							try {
								colors[name] = dec_to_color(stoi(t_rgb[0]), stoi(t_rgb[1]), stoi(t_rgb[2]), t_to_256, depth);
								rgbs[name] = array{stoi(t_rgb[0]), stoi(t_rgb[1]), stoi(t_rgb[2])};
							}
							catch (const std::exception&) {}
						}
					}
				}
				if (not colors.contains(name) or colors.at(name).empty() or rgbs[name][0] < 0) {
					colors[name] = hex_to_color(color, t_to_256, depth);
					rgbs[name] = hex_to_dec(color);
				}
			}
		}

		//* Generate color gradients from two or three colors, 101 values indexed 0-100
		void generateGradients() {
			gradients.clear();
			bool t_to_256 = Config::getB("lowcolor");

			for (const auto& [name, start] : rgbs) {
				if (not name.ends_with("_start")) continue;
				const string color_name { rtrim(name, "_start") };
				const auto& mid = rgbs[color_name + "_mid"];
				const auto& end = rgbs[color_name + "_end"];
				auto& gradient = gradients[color_name];

				//? Only generate a gradient if an end color is defined
				if (end[0] < 0) {
					gradient.fill(colors.at(name));
					continue;
				}

				for (int i : iota(0, 101)) {
					array<int, 3> rgb{};
					for (int c : iota(0, 3)) {
						//? Split in two passes of start->mid and mid->end if mid is defined
						if (mid[0] >= 0)
							rgb[c] = (i <= 50 ? start[c] + i * (mid[c] - start[c]) / 50 : mid[c] + (i - 50) * (end[c] - mid[c]) / 50);
						else
							rgb[c] = start[c] + i * (end[c] - start[c]) / 100;
					}
					gradient[i] = dec_to_color(rgb[0], rgb[1], rgb[2], t_to_256);
				}
			}
		}

		//* Set colors and generate gradients for the TTY theme
		void generateTTYColors() {
			rgbs.clear();
			gradients.clear();
			colors = TTY_theme;
			if (not Config::getB("theme_background"))
				colors["main_bg"] = "\x1b[49m";

			for (const auto& c : colors) {
				if (not c.first.ends_with("_start")) continue;
				const string base_name { rtrim(c.first, "_start") };
				string section = "_start";
				int split = colors.at(base_name + "_mid").empty() ? 50 : 33;
				for (int i : iota(0, 101)) {
					gradients[base_name][i] = colors.at(base_name + section);
					if (i == split) {
						section = (split == 33) ? "_mid" : "_end";
						split *= 2;
					}
				}
			}
		}

		//* Load a .theme file from disk
		auto loadFile(const string& filename) {
			std::unordered_map<string, string> theme_out;
			const fs::path filepath = filename;
			std::error_code ec;
			if (not fs::exists(filepath, ec))
				return Default_theme;

			std::ifstream themefile(filepath);
			if (themefile.good()) {
				while (not themefile.bad()) {
					if (themefile.peek() == '#') {
						themefile.ignore(SSmax, '\n');
						continue;
					}
					themefile.ignore(SSmax, '[');
					if (themefile.eof()) break;
					string name, value;
					getline(themefile, name, ']');
					themefile.ignore(SSmax, '=');
					themefile >> std::ws;
					if (themefile.eof()) break;
					if (themefile.peek() == '"') {
						themefile.ignore(1);
						getline(themefile, value, '"');
						themefile.ignore(SSmax, '\n');
					}
					else getline(themefile, value, '\n');

					theme_out[name] = value;
				}
				return theme_out;
			}
			return Default_theme;
		}
	}

	string hex_to_color(string hexa, bool t_to_256, const string& depth) {
		if (hexa.size() > 1) {
			hexa.erase(0, 1);
			for (auto& c : hexa) {
				if (not isxdigit(c))
					return "";
			}
			string pre = Fx::e + (depth == "fg" ? "38" : "48") + ";" + (t_to_256 ? "5;" : "2;");

			if (hexa.size() == 2) {
				int h_int = stoi(hexa, nullptr, 16);
				if (t_to_256) {
					return pre + to_string(truecolor_to_256(h_int, h_int, h_int)) + "m";
				} else {
					string h_str = to_string(h_int);
					return pre + h_str + ";" + h_str + ";" + h_str + "m";
				}
			}
			else if (hexa.size() == 6) {
				int r = stoi(hexa.substr(0, 2), nullptr, 16);
				int g = stoi(hexa.substr(2, 2), nullptr, 16);
				int b = stoi(hexa.substr(4, 2), nullptr, 16);
				return dec_to_color(r, g, b, t_to_256, depth);
			}
		}
		return "";
	}

	string dec_to_color(int r, int g, int b, bool t_to_256, const string& depth) {
		string pre = Fx::e + (depth == "fg" ? "38" : "48") + ";" + (t_to_256 ? "5;" : "2;");
		r = std::clamp(r, 0, 255);
		g = std::clamp(g, 0, 255);
		b = std::clamp(b, 0, 255);
		if (t_to_256) return pre + to_string(truecolor_to_256(r, g, b)) + "m";
		else return pre + to_string(r) + ";" + to_string(g) + ";" + to_string(b) + "m";
	}

	void updateThemes() {
		themes.clear();
		themes.push_back("Default");
		themes.push_back("TTY");

		vector<fs::path> search_dirs = {theme_dir, user_theme_dir};

		//? Also accept themes from a btop installation
		if (const auto home = std::getenv("HOME"); home != nullptr)
			search_dirs.push_back(fs::path(home) / ".config" / "btop" / "themes");
		search_dirs.push_back("/usr/local/share/btop/themes");
		search_dirs.push_back("/usr/share/btop/themes");

		for (const auto& path : search_dirs) {
			std::error_code ec;
			if (path.empty() or not fs::is_directory(path, ec)) continue;
			for (auto& file : fs::directory_iterator(path, ec)) {
				if (file.path().extension() == ".theme" and access(file.path().c_str(), R_OK) != -1) {
					themes.push_back(file.path().string());
				}
			}
		}
	}

	void setTheme() {
		const auto& theme = Config::getS("color_theme");
		fs::path theme_path;
		for (const fs::path p : themes) {
			if (p == theme or p.filename() == theme or p.stem() == theme) {
				theme_path = p;
				break;
			}
		}
		if (theme == "TTY" or Config::getB("tty_mode"))
			generateTTYColors();
		else {
			generateColors((theme == "Default" or theme_path.empty() ? Default_theme : loadFile(theme_path)));
			generateGradients();
		}
		Term::fg = colors.at("main_fg");
		Term::bg = colors.at("main_bg");
		Fx::reset = Fx::reset_base + Term::fg + Term::bg;
	}

}
