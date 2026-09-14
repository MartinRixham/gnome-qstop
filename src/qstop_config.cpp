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

#include <array>
#include <format>
#include <fstream>
#include <iterator>
#include <ranges>

#include <sys/stat.h>
#include <unistd.h>

#include "qstop_config.hpp"
#include "qstop_shared.hpp"
#include "qstop_tools.hpp"

using std::array;

namespace fs = std::filesystem;
namespace rng = std::ranges;

using namespace Tools;

const vector<string> Config::valid_boxes = { "status", "sliders", "toggles" };

namespace Config {

	fs::path conf_dir;
	fs::path conf_file;

	const vector<array<string, 2>> descriptions = {
		{"color_theme", 		"#* Name of a btop++/qstop formatted \".theme\" file, \"Default\" and \"TTY\" for builtin themes.\n"
								"#* Themes are searched for in \"../share/qstop/themes\" relative to binary, \"$HOME/.config/qstop/themes\"\n"
								"#* and in the btop theme directories, so any installed btop theme can be used."},

		{"theme_background", 	"#* If the theme set background should be shown, set to False if you want terminal background transparency."},

		{"truecolor", 			"#* Sets if 24-bit truecolor should be used, will convert 24-bit colors to 256 color (6x6x6 color cube) if false."},

		{"force_tty", 			"#* Set to true to force tty mode regardless if a real tty has been detected or not.\n"
								"#* Will force 16-color mode and TTY theme and swap out other non tty friendly symbols."},

		{"vim_keys",			"#* Set to True to enable \"h,j,k,l\" keys for directional control.\n"
								"#* Conflicting keys for h:\"help\" and l:\"lock\" is accessible while holding shift."},

		{"rounded_corners",		"#* Rounded corners on boxes, is ignored if TTY mode is ON."},

		{"disable_mouse", 		"#* Disable all mouse events."},

		{"shown_boxes",			"#* Manually set which boxes to show. Available values are \"status sliders toggles\", separate values with whitespace."},

		{"update_ms", 			"#* Update time in milliseconds for polling system state, recommended 2000 ms or above."},

		{"clock_format", 		"#* Draw a clock at top of screen, formatting according to strftime, empty string to disable.\n"
								"#* Special formatting: /host = hostname | /user = username | /uptime = system uptime"},

		{"volume_step",			"#* Percentage the volume and microphone sliders change per key press or scroll step."},

		{"brightness_step",		"#* Percentage the brightness sliders change per key press or scroll step."},

		{"allow_volume_above_100", "#* Allow raising the output volume above 100% (over-amplification), as in GNOME sound settings."},

		{"confirm_power_actions", "#* Ask for confirmation before restarting, powering off or logging out."},

		{"show_battery_time",	"#* Show estimated time until battery is charged or empty in the status box."},
	};

	std::unordered_map<std::string_view, string> strings = {
		{"color_theme", "Default"},
		{"shown_boxes", "status sliders toggles"},
		{"clock_format", "%X"},
	};

	std::unordered_map<std::string_view, bool> bools = {
		{"theme_background", true},
		{"truecolor", true},
		{"force_tty", false},
		{"vim_keys", false},
		{"rounded_corners", true},
		{"disable_mouse", false},
		{"allow_volume_above_100", false},
		{"confirm_power_actions", true},
		{"show_battery_time", true},
		{"lowcolor", false},
		{"tty_mode", false},
	};

	std::unordered_map<std::string_view, int> ints = {
		{"update_ms", 2000},
		{"volume_step", 5},
		{"brightness_step", 5},
	};

	vector<string> current_boxes;

	bool write_new;

	string validError;

	std::optional<fs::path> get_config_dir() noexcept {
		fs::path config_dir;
		{
			std::error_code error;
			if (const auto xdg_config_home = std::getenv("XDG_CONFIG_HOME"); xdg_config_home != nullptr) {
				if (fs::exists(xdg_config_home, error)) {
					config_dir = fs::path(xdg_config_home) / "qstop";
				}
			} else if (const auto home = std::getenv("HOME"); home != nullptr) {
				error.clear();
				if (fs::exists(home, error)) {
					config_dir = fs::path(home) / ".config" / "qstop";
				}
			}
		}

		if (not config_dir.empty()) {
			std::error_code error;
			if (fs::exists(config_dir, error)) {
				if (fs::is_directory(config_dir, error)) {
					struct stat st;
					if (stat(config_dir.c_str(), &st) == 0 and access(config_dir.c_str(), R_OK | W_OK) == 0) {
						return config_dir;
					}
				}
			} else {
				error.clear();
				if (fs::create_directories(config_dir, error)) {
					return config_dir;
				}
			}
		}
		return std::nullopt;
	}

	bool intValid(const std::string_view name, const string& value) {
		int i_value;
		try {
			i_value = stoi(value);
		}
		catch (const std::invalid_argument&) {
			validError = "Invalid numerical value!";
			return false;
		}
		catch (const std::out_of_range&) {
			validError = "Value out of range!";
			return false;
		}

		if (name == "update_ms" and (i_value < 500 or i_value > 60000))
			validError = "Config value update_ms set too low or too high (<500 or >60000).";
		else if (is_in(name, "volume_step", "brightness_step") and (i_value < 1 or i_value > 25))
			validError = std::format("Config value {} must be between 1 and 25.", name);
		else
			return true;

		return false;
	}

	bool stringValid(const std::string_view name, const string& value) {
		if (name == "shown_boxes" and not value.empty() and not set_boxes(value))
			validError = "Invalid box name(s) in shown_boxes!";
		else
			return true;

		return false;
	}

	bool set_boxes(const string& boxes) {
		auto new_boxes = ssplit(boxes);
		for (auto& box : new_boxes) {
			if (not v_contains(valid_boxes, box)) return false;
		}
		current_boxes = std::move(new_boxes);
		return true;
	}

	bool toggle_box(const string& box) {
		auto old_boxes = current_boxes;
		auto box_pos = rng::find(current_boxes, box);
		if (box_pos == current_boxes.end())
			current_boxes.push_back(box);
		else
			current_boxes.erase(box_pos);

		//? Keep boxes in the canonical top to bottom order
		vector<string> ordered;
		for (const auto& valid : valid_boxes) {
			if (v_contains(current_boxes, valid)) ordered.push_back(valid);
		}
		current_boxes = std::move(ordered);

		string new_boxes;
		for (const auto& b : current_boxes) new_boxes += b + ' ';
		if (not new_boxes.empty()) new_boxes.pop_back();

		strings.at("shown_boxes") = new_boxes;
		write_new = true;
		return true;
	}

	void load(const fs::path& conf_file, vector<string>& load_warnings) {
		std::error_code error;
		if (conf_file.empty())
			return;
		else if (not fs::exists(conf_file, error)) {
			write_new = true;
			return;
		}
		if (error) {
			return;
		}

		std::ifstream cread(conf_file);
		if (cread.good()) {
			vector<string> valid_names;
			valid_names.reserve(descriptions.size());
			for (const auto &n : descriptions)
				valid_names.push_back(n[0]);
			if (string v_string; cread.peek() != '#' or (getline(cread, v_string, '\n') and not v_string.contains(Global::Version)))
				write_new = true;
			while (not cread.eof()) {
				cread >> std::ws;
				if (cread.peek() == '#') {
					cread.ignore(SSmax, '\n');
					continue;
				}
				string name, value;
				getline(cread, name, '=');
				if (name.ends_with(' ')) name = trim(name);
				if (not v_contains(valid_names, name)) {
					cread.ignore(SSmax, '\n');
					continue;
				}
				cread >> std::ws;

				if (bools.contains(name)) {
					cread >> value;
					if (not isbool(value))
						load_warnings.push_back("Got an invalid bool value for config name: " + name);
					else
						bools.at(name) = stobool(value);
				}
				else if (ints.contains(name)) {
					cread >> value;
					if (not isint(value))
						load_warnings.push_back("Got an invalid integer value for config name: " + name);
					else if (not intValid(name, value)) {
						load_warnings.push_back(validError);
					}
					else
						ints.at(name) = stoi(value);
				}
				else if (strings.contains(name)) {
					if (cread.peek() == '"') {
						cread.ignore(1);
						getline(cread, value, '"');
					}
					else cread >> value;

					if (not stringValid(name, value))
						load_warnings.push_back(validError);
					else
						strings.at(name) = value;
				}

				cread.ignore(SSmax, '\n');
			}
		}
	}

	void write() {
		if (conf_file.empty() or not write_new) return;
		std::ofstream cwrite(conf_file, std::ios::trunc);
		if (cwrite.good()) {
			cwrite << current_config();
		}
	}

	auto current_config() -> std::string {
		auto buffer = std::string {};
		std::format_to(std::back_inserter(buffer), "#? Config file for qstop v.{}\n", Global::Version);

		for (const auto& [name, description] : descriptions) {
			std::format_to(std::back_inserter(buffer), "\n");
			if (not description.empty()) {
				std::format_to(std::back_inserter(buffer), "{}\n", description);
			}

			std::format_to(std::back_inserter(buffer), "{} = ", name);
			if (strings.contains(name)) {
				std::format_to(std::back_inserter(buffer), R"("{}")", strings.at(name));
			} else if (ints.contains(name)) {
				std::format_to(std::back_inserter(buffer), "{}", ints.at(name));
			} else if (bools.contains(name)) {
				std::format_to(std::back_inserter(buffer), "{}", bools.at(name) ? "true" : "false");
			}
			std::format_to(std::back_inserter(buffer), "\n");
		}
		return buffer;
	}
}
