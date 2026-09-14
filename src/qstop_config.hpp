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

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using std::string;
using std::vector;

//* Functions and variables for reading and writing the qstop config file
namespace Config {

	extern std::filesystem::path conf_dir;
	extern std::filesystem::path conf_file;

	extern std::unordered_map<std::string_view, string> strings;
	extern std::unordered_map<std::string_view, bool> bools;
	extern std::unordered_map<std::string_view, int> ints;

	extern const vector<string> valid_boxes;
	extern vector<string> current_boxes;

	extern bool write_new;

	[[nodiscard]] std::optional<std::filesystem::path> get_config_dir() noexcept;

	//* Check if string only contains space separated valid names for boxes and set current_boxes
	bool set_boxes(const string& boxes);

	//* Toggle box and update config string shown_boxes
	bool toggle_box(const string& box);

	//* Return bool for config key <name>
	inline bool getB(const std::string_view name) { return bools.at(name); }

	//* Return integer for config key <name>
	inline const int& getI(const std::string_view name) { return ints.at(name); }

	//* Return string for config key <name>
	inline const string& getS(const std::string_view name) { return strings.at(name); }

	extern string validError;

	bool intValid(const std::string_view name, const string& value);
	bool stringValid(const std::string_view name, const string& value);

	//* Set config key <name> to bool <value>
	inline void set(const std::string_view name, bool value) { bools.at(name) = value; }

	//* Set config key <name> to int <value>
	inline void set(const std::string_view name, const int value) { ints.at(name) = value; }

	//* Set config key <name> to string <value>
	inline void set(const std::string_view name, const string& value) { strings.at(name) = value; }

	//* Flip config key bool <name>
	inline void flip(const std::string_view name) { bools.at(name) = not bools.at(name); }

	//* Load the config file from disk
	void load(const std::filesystem::path& conf_file, vector<string>& load_warnings);

	//* Write the config file to disk
	void write();

	//* Write current config to an in-memory buffer
	[[nodiscard]] auto current_config() -> std::string;
}
