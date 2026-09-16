/* Copyright 2026 Martin Rixham

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
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <unordered_map>
#include <unordered_set>

#include "../qstop_config.hpp"
#include "../qstop_shared.hpp"
#include "../qstop_tools.hpp"

using std::clamp;
using std::max;
using std::min;

using namespace Tools;

namespace fs = std::filesystem;

namespace {
	//* Split a line of "nmcli -t" terse output at unescaped ':' and unescape "\:" and "\\"
	vector<string> terse_split(const string& line) {
		vector<string> fields(1);
		for (size_t i = 0; i < line.size(); i++) {
			if (line[i] == '\\' and i + 1 < line.size()) {
				fields.back() += line[++i];
			}
			else if (line[i] == ':') {
				fields.emplace_back();
			}
			else fields.back() += line[i];
		}
		return fields;
	}

	//* Return the first integer directly preceding a '%' in <str>, or -1
	int parse_percent(const string& str) {
		const auto pos = str.find('%');
		if (pos == string::npos or pos == 0) return -1;
		size_t start = pos;
		while (start > 0 and isdigit(str[start - 1])) --start;
		if (start == pos) return -1;
		try { return std::stoi(str.substr(start, pos - start)); }
		catch (const std::exception&) { return -1; }
	}

	string trimmed(const string& str) {
		return string(trim(trim(str, "\n"), " "));
	}

	int readint(const fs::path& path, int fallback = 0) {
		try { return std::stoi(readfile(path, to_string(fallback))); }
		catch (const std::exception&) { return fallback; }
	}

	int64_t readint64(const fs::path& path, int64_t fallback = -1) {
		try { return std::stoll(readfile(path, to_string(fallback))); }
		catch (const std::exception&) { return fallback; }
	}
}

namespace Audio {
	audio_info current;

	namespace {
		//* Parse "pactl list sinks|sources" into devices, filling volume and mute for the device named <default_name>
		vector<device> parse_devices(const string& output, const string& default_name, bool skip_monitors, int& volume, bool& muted, bool& found) {
			vector<device> devices;
			bool is_default = false;
			for (const auto& raw_line : ssplit(output, '\n')) {
				const string line = string(ltrim(ltrim(raw_line, "\t"), " "));
				if (line.starts_with("Sink #") or line.starts_with("Source #")) {
					is_default = false;
				}
				else if (line.starts_with("Name: ")) {
					const string name = line.substr(6);
					is_default = (name == default_name);
					if (is_default) found = true;
					if (skip_monitors and name.ends_with(".monitor")) continue;
					devices.push_back({name, name});
				}
				else if (line.starts_with("Description: ") and not devices.empty()) {
					devices.back().description = line.substr(13);
				}
				else if (is_default and line.starts_with("Mute: ")) {
					muted = line.contains("yes");
				}
				else if (is_default and line.starts_with("Volume: ")) {
					volume = max(0, parse_percent(line));
				}
			}
			return devices;
		}

		//* Parse a wpctl volume such as "0.72" or "0.72 MUTED" into <volume> percent and <muted>
		bool parse_wpctl_volume(string_view str, int& volume, bool& muted) {
			str = trim(str, " ");
			const auto space = str.find(' ');
			try { volume = max(0, (int)std::lround(std::stod(string(str.substr(0, space))) * 100)); }
			catch (const std::exception&) { return false; }
			muted = (space != string_view::npos and str.substr(space).contains("MUTED"));
			return true;
		}

		//* Parse the Audio section of "wpctl status", where devices are listed as "│  *   73. Description [vol: 0.72 MUTED]"
		//* with node ids as names, as wpctl addresses nodes by id. The default device of each kind is marked with '*'.
		void parse_wpctl_status(const string& output, audio_info& info) {
			string section, subsection;
			bool sink_volume = false, source_volume = false;
			for (const auto& line : ssplit(output, '\n')) {
				if (not line.starts_with(' ')) {
					section = trimmed(line);
					subsection.clear();
					continue;
				}
				if (section != "Audio") continue;
				//? Subsection headers follow tree branches: " ├─ Sinks:"
				if (const auto branch = line.find("─ "); branch != string::npos) {
					subsection = trimmed(line.substr(branch + string("─ ").size()));
					continue;
				}
				const bool sinks = (subsection == "Sinks:");
				if (not sinks and subsection != "Sources:") continue;

				const auto digit = line.find_first_of("0123456789");
				const auto dot = line.find(". ", digit);
				if (digit == string::npos or dot == string::npos or not isint(string_view(line).substr(digit, dot - digit))) continue;
				const bool is_default = line.substr(0, digit).contains('*');

				string description = trimmed(line.substr(dot + 2));
				int volume = 0;
				bool muted = false, has_volume = false;
				if (const auto vol = description.rfind(" [vol: "); vol != string::npos and description.ends_with(']')) {
					has_volume = parse_wpctl_volume(string_view(description).substr(vol + 7, description.size() - vol - 8), volume, muted);
					description = trimmed(description.substr(0, vol));
				}

				device dev{line.substr(digit, dot - digit), description};
				if (is_default) {
					if (sinks) {
						info.has_sink = true;
						info.sink = dev.name;
						info.volume = volume;
						info.muted = muted;
						sink_volume = has_volume;
					}
					else {
						info.has_source = true;
						info.source = dev.name;
						info.mic_volume = volume;
						info.mic_muted = muted;
						source_volume = has_volume;
					}
				}
				(sinks ? info.sinks : info.sources).push_back(std::move(dev));
			}

			//? Nodes without volume in the listing are asked directly
			if (info.has_sink and not sink_volume)
				parse_wpctl_volume(ltrim(trimmed(exec({"wpctl", "get-volume", info.sink}).output), "Volume:"), info.volume, info.muted);
			if (info.has_source and not source_volume)
				parse_wpctl_volume(ltrim(trimmed(exec({"wpctl", "get-volume", info.source}).output), "Volume:"), info.mic_volume, info.mic_muted);
		}

		bool collect_wpctl(audio_info& info) {
			if (not command_exists("wpctl")) return false;
			auto status = exec({"wpctl", "status"});
			if (status.status != 0 or not status.output.contains("\nAudio")) return false;
			info.available = true;
			info.wpctl = true;
			parse_wpctl_status(status.output, info);
			return true;
		}

		void collect_pactl(audio_info& info) {
			if (not command_exists("pactl")) return;
			auto default_sink = exec({"pactl", "get-default-sink"});
			if (default_sink.status != 0) return;
			info.available = true;
			info.sink = trimmed(default_sink.output);
			info.source = trimmed(exec({"pactl", "get-default-source"}).output);

			auto sinks = exec({"pactl", "list", "sinks"});
			info.sinks = parse_devices(sinks.output, info.sink, false, info.volume, info.muted, info.has_sink);

			auto sources = exec({"pactl", "list", "sources"});
			info.sources = parse_devices(sources.output, info.source, true, info.mic_volume, info.mic_muted, info.has_source);
			if (info.source.ends_with(".monitor")) info.has_source = false;
		}

		//* Command setting the volume of the default sink or source to <percent>
		vector<string> volume_cmd(bool sink, int percent) {
			if (current.wpctl) return {"wpctl", "set-volume", (sink ? "@DEFAULT_AUDIO_SINK@" : "@DEFAULT_AUDIO_SOURCE@"), to_string(percent) + '%'};
			return {"pactl", (sink ? "set-sink-volume" : "set-source-volume"), (sink ? "@DEFAULT_SINK@" : "@DEFAULT_SOURCE@"), to_string(percent) + '%'};
		}

		//* Command muting or unmuting the default sink or source
		vector<string> mute_cmd(bool sink, bool mute) {
			if (current.wpctl) return {"wpctl", "set-mute", (sink ? "@DEFAULT_AUDIO_SINK@" : "@DEFAULT_AUDIO_SOURCE@"), (mute ? "1" : "0")};
			return {"pactl", (sink ? "set-sink-mute" : "set-source-mute"), (sink ? "@DEFAULT_SINK@" : "@DEFAULT_SOURCE@"), (mute ? "1" : "0")};
		}

		//* Command making device <name> the default sink or source
		vector<string> default_cmd(bool sink, const string& name) {
			if (current.wpctl) return {"wpctl", "set-default", name};
			return {"pactl", (sink ? "set-default-sink" : "set-default-source"), name};
		}
	}

	void collect(audio_info& info) {
		if (not collect_wpctl(info)) collect_pactl(info);
	}

	void set_volume(int percent) {
		const int max_volume = (Config::getB("allow_volume_above_100") ? 150 : 100);
		percent = clamp(percent, 0, max_volume);
		vector<vector<string>> cmds = {volume_cmd(true, percent)};
		//? Moving the slider unmutes, as in GNOME
		if (current.muted and percent > 0) cmds.push_back(mute_cmd(true, false));
		current.volume = percent;
		current.muted = current.muted and percent == 0;
		Runner::queue("volume", std::move(cmds), 3000, false, "Failed to set volume");
	}

	void set_mute(bool mute) {
		current.muted = mute;
		Runner::queue("volume_mute", {mute_cmd(true, mute)}, 3000, false, "Failed to mute output");
	}

	void set_mic_volume(int percent) {
		percent = clamp(percent, 0, 100);
		vector<vector<string>> cmds = {volume_cmd(false, percent)};
		if (current.mic_muted and percent > 0) cmds.push_back(mute_cmd(false, false));
		current.mic_volume = percent;
		current.mic_muted = current.mic_muted and percent == 0;
		Runner::queue("mic", std::move(cmds), 3000, false, "Failed to set microphone volume");
	}

	void set_mic_mute(bool mute) {
		current.mic_muted = mute;
		Runner::queue("mic_mute", {mute_cmd(false, mute)}, 3000, false, "Failed to mute microphone");
	}

	void set_sink(const string& name) {
		current.sink = name;
		Runner::queue("sink", {default_cmd(true, name)}, 3000, false, "Failed to change output device");
	}

	void set_source(const string& name) {
		current.source = name;
		Runner::queue("source", {default_cmd(false, name)}, 3000, false, "Failed to change input device");
	}
}

namespace Display {
	display_info current;

	namespace {
		//* Queue a brightness change through logind, falling back to brightnessctl
		void queue_brightness(const string& id, const backlight& light, int raw) {
			Runner::queue(id, {
				{"busctl", "call", "org.freedesktop.login1", "/org/freedesktop/login1/session/auto", "org.freedesktop.login1.Session",
				 "SetBrightness", "ssu", light.subsystem, light.name, to_string(raw)},
				{"brightnessctl", "--class=" + light.subsystem, "--device=" + light.name, "set", to_string(raw)},
			}, 3000, true, "Failed to set brightness");
		}
	}

	void collect(display_info& info) {
		std::error_code ec;

		//? Prefer firmware and platform interfaces over raw, as gnome-settings-daemon does
		const fs::path backlight_dir = "/sys/class/backlight";
		int best_rank = -1;
		if (fs::is_directory(backlight_dir, ec)) {
			for (const auto& entry : fs::directory_iterator(backlight_dir, ec)) {
				const string type = trimmed(readfile(entry.path() / "type"));
				const int rank = (type == "firmware" ? 3 : type == "platform" ? 2 : type == "raw" ? 1 : 0);
				if (rank <= best_rank) continue;
				best_rank = rank;
				info.screen.present = true;
				info.screen.subsystem = "backlight";
				info.screen.name = entry.path().filename();
				info.screen.value = readint(entry.path() / "brightness");
				info.screen.max = readint(entry.path() / "max_brightness");
			}
		}

		const fs::path leds_dir = "/sys/class/leds";
		if (fs::is_directory(leds_dir, ec)) {
			for (const auto& entry : fs::directory_iterator(leds_dir, ec)) {
				if (not entry.path().filename().string().contains("kbd_backlight")) continue;
				info.kbd.present = true;
				info.kbd.subsystem = "leds";
				info.kbd.name = entry.path().filename();
				info.kbd.value = readint(entry.path() / "brightness");
				info.kbd.max = readint(entry.path() / "max_brightness");
				break;
			}
		}
	}

	void set_brightness(int percent) {
		if (not current.screen.present or current.screen.max <= 0) return;
		percent = clamp(percent, 0, 100);
		//? Never turn the screen fully off from a slider
		const int raw = max(1, (int)std::lround((double)percent * current.screen.max / 100));
		current.screen.value = raw;
		queue_brightness("brightness", current.screen, raw);
	}

	void set_kbd_brightness(int percent) {
		if (not current.kbd.present or current.kbd.max <= 0) return;
		percent = clamp(percent, 0, 100);
		const int raw = (int)std::lround((double)percent * current.kbd.max / 100);
		current.kbd.value = raw;
		queue_brightness("kbd_brightness", current.kbd, raw);
	}
}

namespace Network {
	net_info current;

	void collect(net_info& info, bool networks) {
		if (not command_exists("nmcli")) return;
		auto devices = exec({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"});
		if (devices.status != 0) return;
		info.available = true;

		for (const auto& line : ssplit(devices.output, '\n')) {
			const auto f = terse_split(line);
			if (f.size() < 4) continue;
			if (f[1] == "wifi" and not info.has_wifi) {
				info.has_wifi = true;
				info.wifi_device = f[0];
				info.wifi_state = f[2];
				if (f[2].starts_with("connect")) info.wifi_connection = f[3];
			}
			//? Devices without a cable are unavailable, GNOME hides them and unmanaged devices
			else if (f[1] == "ethernet" and not info.has_wired and not is_in(f[2], "unavailable", "unmanaged")) {
				info.has_wired = true;
				info.wired_device = f[0];
				info.wired_connected = (f[2] == "connected");
				if (f[2].starts_with("connect")) info.wired_connection = f[3];
			}
		}

		info.wifi_enabled = (trimmed(exec({"nmcli", "-t", "-f", "WIFI", "radio"}).output) == "enabled");

		std::unordered_set<string> known;
		auto connections = exec({"nmcli", "-t", "-f", "NAME,TYPE,ACTIVE", "connection", "show"});
		for (const auto& line : ssplit(connections.output, '\n')) {
			const auto f = terse_split(line);
			if (f.size() < 3) continue;
			if (f[1] == "802-11-wireless") known.insert(f[0]);
			else if (f[1] == "vpn" or f[1] == "wireguard") info.vpns.push_back({f[0], f[2] == "yes"});
		}

		if (networks and info.has_wifi and info.wifi_enabled) {
			auto list = exec({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "no"}, 5000);
			std::unordered_map<string, size_t> index;
			for (const auto& line : ssplit(list.output, '\n')) {
				const auto f = terse_split(line);
				if (f.size() < 4 or f[1].empty()) continue;
				wifi_network network{f[1], f[3], 0, trimmed(f[0]) == "*", known.contains(f[1])};
				try { network.signal = std::stoi(f[2]); } catch (const std::exception&) {}

				//? Show each SSID once, keeping the strongest or active access point
				if (auto it = index.find(network.ssid); it != index.end()) {
					auto& existing = info.networks.at(it->second);
					existing.active = existing.active or network.active;
					existing.signal = max(existing.signal, network.signal);
					continue;
				}
				index[network.ssid] = info.networks.size();
				info.networks.push_back(std::move(network));
			}
			std::ranges::stable_sort(info.networks, [](const auto& a, const auto& b) {
				if (a.active != b.active) return a.active;
				if (a.known != b.known) return a.known;
				return a.signal > b.signal;
			});
		}
	}

	void set_wifi(bool enabled) {
		current.wifi_enabled = enabled;
		if (not enabled) {
			current.wifi_connection.clear();
			current.networks.clear();
		}
		Runner::queue("wifi_radio", {{"nmcli", "radio", "wifi", (enabled ? "on" : "off")}}, 10000, false, "Failed to toggle Wi-Fi");
	}

	void set_wired(bool connected) {
		if (not current.has_wired) return;
		current.wired_connected = connected;
		Runner::queue("wired", {{"nmcli", "device", (connected ? "connect" : "disconnect"), current.wired_device}},
			30000, false, (connected ? "Failed to connect wired network" : "Failed to disconnect wired network"));
	}

	void connect(const wifi_network& network, const string& password) {
		vector<string> cmd;
		if (network.known and password.empty())
			cmd = {"nmcli", "connection", "up", "id", network.ssid};
		else {
			cmd = {"nmcli", "device", "wifi", "connect", network.ssid};
			if (not password.empty()) {
				cmd.push_back("password");
				cmd.push_back(password);
			}
			if (not current.wifi_device.empty()) {
				cmd.push_back("ifname");
				cmd.push_back(current.wifi_device);
			}
		}
		current.wifi_state = "connecting";
		Global::notify("Connecting to " + network.ssid + "...", 3000);
		Runner::queue("wifi_connect", {cmd}, 60000, false, "Failed to connect to " + network.ssid);
	}

	void disconnect_wifi() {
		if (current.wifi_device.empty()) return;
		current.wifi_connection.clear();
		for (auto& network : current.networks) network.active = false;
		Runner::queue("wifi_connect", {{"nmcli", "device", "disconnect", current.wifi_device}}, 15000, false, "Failed to disconnect Wi-Fi");
	}

	void rescan() {
		Runner::queue("wifi_rescan", {{"nmcli", "device", "wifi", "rescan"}}, 15000, false);
	}

	void set_vpn(const string& name, bool active) {
		for (auto& vpn : current.vpns) {
			if (vpn.name == name) vpn.active = active;
		}
		Runner::queue("vpn_" + name, {{"nmcli", "connection", (active ? "up" : "down"), "id", name}}, 60000, false,
			(active ? "Failed to connect " : "Failed to disconnect ") + name);
	}
}

namespace Bluetooth {
	bt_info current;

	int bt_info::connected_count() const {
		return std::ranges::count_if(devices, [](const auto& d) { return d.connected; });
	}

	namespace {
		atomic<uint64_t> next_retry{0};

		//* Parse "Device <address> <name>" lines from bluetoothctl output
		vector<bt_device> parse_devices(const string& output) {
			vector<bt_device> devices;
			for (const auto& raw_line : ssplit(Fx::uncolor(output), '\n')) {
				const auto pos = raw_line.find("Device ");
				if (pos == string::npos) continue;
				const string line = raw_line.substr(pos + 7);
				const auto space = line.find(' ');
				if (space == string::npos) continue;
				devices.push_back({line.substr(0, space), trimmed(line.substr(space + 1)), false});
			}
			return devices;
		}
	}

	void collect(bt_info& info) {
		if (time_ms() < next_retry or not command_exists("bluetoothctl")) return;
		auto show = exec({"bluetoothctl", "show"}, 1500);
		if (show.status != 0 or not show.output.contains("Controller")) {
			//? bluetoothctl blocks waiting for bluetoothd when it isn't running, so back off
			next_retry = time_ms() + 30000;
			return;
		}
		info.available = true;
		info.powered = show.output.contains("Powered: yes");

		info.devices = parse_devices(exec({"bluetoothctl", "devices", "Paired"}, 1500).output);
		if (info.devices.empty()) info.devices = parse_devices(exec({"bluetoothctl", "devices"}, 1500).output);

		if (info.powered) {
			for (const auto& connected : parse_devices(exec({"bluetoothctl", "devices", "Connected"}, 1500).output)) {
				auto it = std::ranges::find(info.devices, connected.address, &bt_device::address);
				if (it != info.devices.end()) it->connected = true;
				else info.devices.push_back({connected.address, connected.name, true});
			}
		}
		std::ranges::stable_sort(info.devices, [](const auto& a, const auto& b) { return a.connected and not b.connected; });
	}

	void set_power(bool on) {
		current.powered = on;
		if (not on) {
			for (auto& device : current.devices) device.connected = false;
		}
		vector<vector<string>> cmds;
		if (on) cmds.push_back({"rfkill", "unblock", "bluetooth"});
		cmds.push_back({"bluetoothctl", "power", (on ? "on" : "off")});
		Runner::queue("bt_power", std::move(cmds), 10000, false, "Failed to toggle Bluetooth");
	}

	void set_connected(const bt_device& device, bool connect) {
		if (connect) Global::notify("Connecting to " + device.name + "...", 3000);
		Runner::queue("bt_" + device.address, {{"bluetoothctl", (connect ? "connect" : "disconnect"), device.address}}, 30000, false,
			(connect ? "Failed to connect " : "Failed to disconnect ") + device.name);
	}
}

namespace Power {
	power_info current;

	namespace {
		struct ppd_service {
			string name, path, iface;
		};

		const array<ppd_service, 2> ppd_services = {{
			{"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles", "org.freedesktop.UPower.PowerProfiles"},
			{"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles", "net.hadess.PowerProfiles"},
		}};

		atomic<size_t> ppd_index{0};

		void collect_battery(battery_info& battery) {
			std::error_code ec;
			const fs::path supply_dir = "/sys/class/power_supply";
			if (not fs::is_directory(supply_dir, ec)) return;
			for (const auto& entry : fs::directory_iterator(supply_dir, ec)) {
				const auto& path = entry.path();
				if (trimmed(readfile(path / "type")) != "Battery") continue;
				//? Skip batteries of peripherals like mice and headsets
				if (trimmed(readfile(path / "scope")) == "Device") continue;
				if (readint(path / "present", 1) == 0) continue;

				battery.present = true;
				battery.percent = clamp(readint(path / "capacity"), 0, 100);
				battery.status = trimmed(readfile(path / "status", "Unknown"));

				int64_t now = readint64(path / "energy_now");
				int64_t full = readint64(path / "energy_full");
				int64_t rate = readint64(path / "power_now");
				if (now < 0 or rate < 0) {
					now = readint64(path / "charge_now");
					full = readint64(path / "charge_full");
					rate = readint64(path / "current_now");
				}
				if (rate > 0 and now >= 0) {
					if (battery.status == "Discharging") battery.seconds = now * 3600 / rate;
					else if (battery.status == "Charging" and full > now) battery.seconds = (full - now) * 3600 / rate;
					//? A near zero rate when idle or full gives meaningless estimates, hide them as GNOME does
					if (battery.seconds > 99 * 3600) battery.seconds = -1;
				}
				break;
			}
		}

		//* Return all "..." quoted values from busctl output following <prefix>
		vector<string> busctl_strings(const string& output, const string& prefix) {
			vector<string> values;
			const std::regex pattern(prefix + "\"([^\"]*)\"");
			for (auto it = std::sregex_iterator(output.begin(), output.end(), pattern); it != std::sregex_iterator(); ++it)
				values.push_back((*it)[1]);
			return values;
		}
	}

	void collect(power_info& info) {
		collect_battery(info.battery);

		if (not command_exists("busctl")) return;
		for (size_t attempt = 0; attempt < ppd_services.size(); attempt++) {
			const size_t index = (ppd_index + attempt) % ppd_services.size();
			const auto& service = ppd_services[index];
			auto active = exec({"busctl", "--system", "get-property", service.name, service.path, service.iface, "ActiveProfile"}, 1500);
			if (active.status != 0) continue;
			ppd_index = index;
			auto active_values = busctl_strings(active.output, "s ");
			if (active_values.empty()) continue;
			info.has_profiles = true;
			info.profile = active_values.front();
			info.profiles = busctl_strings(exec({"busctl", "--system", "get-property", service.name, service.path, service.iface, "Profiles"}, 1500).output,
				"\"Profile\" s ");
			if (info.profiles.empty()) info.profiles = {"power-saver", "balanced", "performance"};
			//? GNOME lists profiles from highest to lowest performance
			std::ranges::reverse(info.profiles);
			break;
		}
	}

	string profile_name(const string& profile) {
		if (profile == "performance") return "Performance";
		if (profile == "balanced") return "Balanced";
		if (profile == "power-saver") return "Power Saver";
		return capitalize(profile);
	}

	void set_profile(const string& profile) {
		const auto& service = ppd_services[ppd_index];
		current.profile = profile;
		Runner::queue("power_profile", {{"busctl", "--system", "set-property", service.name, service.path, service.iface, "ActiveProfile", "s", profile}},
			5000, false, "Failed to set power mode");
	}

	void run_action(Action action) {
		switch (action) {
			case Action::Suspend:
				Runner::queue("power_action", {{"systemctl", "suspend"}}, 10000, false, "Failed to suspend");
				break;
			case Action::Restart:
				Runner::queue("power_action", {{"systemctl", "reboot"}}, 10000, false, "Failed to restart");
				break;
			case Action::PowerOff:
				Runner::queue("power_action", {{"systemctl", "poweroff"}}, 10000, false, "Failed to power off");
				break;
			case Action::LogOut: {
				vector<vector<string>> cmds = {{"gnome-session-quit", "--logout", "--no-prompt"}};
				if (const auto session = std::getenv("XDG_SESSION_ID"); session != nullptr)
					cmds.push_back({"loginctl", "terminate-session", session});
				Runner::queue("power_action", std::move(cmds), 10000, true, "Failed to log out");
				break;
			}
		}
	}

	void lock_screen() {
		Runner::queue("lock", {
			{"gdbus", "call", "--session", "--dest", "org.gnome.ScreenSaver", "--object-path", "/org/gnome/ScreenSaver", "--method", "org.gnome.ScreenSaver.Lock"},
			{"loginctl", "lock-session"},
		}, 5000, true, "Failed to lock screen");
	}

	void screenshot() {
		//? The screenshot portal opens the interactive GNOME Shell screenshot UI
		if (command_exists("gdbus")) {
			Runner::queue("screenshot", {
				{"gdbus", "call", "--session", "--dest", "org.freedesktop.portal.Desktop", "--object-path", "/org/freedesktop/portal/desktop",
				 "--method", "org.freedesktop.portal.Screenshot.Screenshot", "", "{'interactive': <true>}"},
			}, 5000, false, "Failed to take screenshot");
		}
		else if (not spawn_detached({"gnome-screenshot", "--interactive"}))
			Global::notify("No screenshot tool found");
	}

	void open_settings(const string& panel) {
		vector<string> cmd = {"gnome-control-center"};
		if (not panel.empty()) cmd.push_back(panel);
		if (not spawn_detached(cmd))
			Global::notify("GNOME Settings (gnome-control-center) not found");
	}
}

namespace Desktop {
	desktop_info current;

	namespace {
		const string color_schema = "org.gnome.settings-daemon.plugins.color";
		const string interface_schema = "org.gnome.desktop.interface";
		const string notifications_schema = "org.gnome.desktop.notifications";
	}

	void collect(desktop_info& info) {
		std::error_code ec;
		const fs::path rfkill_dir = "/sys/class/rfkill";
		int radios = 0, blocked = 0;
		if (fs::is_directory(rfkill_dir, ec)) {
			for (const auto& entry : fs::directory_iterator(rfkill_dir, ec)) {
				radios++;
				if (readint(entry.path() / "soft") == 1 or readint(entry.path() / "hard") == 1) blocked++;
			}
		}
		info.has_rfkill = (radios > 0);
		info.airplane = (radios > 0 and blocked == radios);

		if (not command_exists("gsettings")) return;
		auto night_light = exec({"gsettings", "get", color_schema, "night-light-enabled"});
		if (night_light.status != 0) return;
		info.available = true;
		info.night_light = (trimmed(night_light.output) == "true");
		info.dark_style = (trimmed(exec({"gsettings", "get", interface_schema, "color-scheme"}).output) == "'prefer-dark'");
		info.dnd = (trimmed(exec({"gsettings", "get", notifications_schema, "show-banners"}).output) == "false");
	}

	void set_night_light(bool on) {
		current.night_light = on;
		Runner::queue("night_light", {{"gsettings", "set", color_schema, "night-light-enabled", (on ? "true" : "false")}}, 3000, false, "Failed to toggle Night Light");
	}

	void set_dark_style(bool on) {
		current.dark_style = on;
		Runner::queue("dark_style", {{"gsettings", "set", interface_schema, "color-scheme", (on ? "prefer-dark" : "default")}}, 3000, false, "Failed to toggle Dark Style");
	}

	void set_dnd(bool on) {
		current.dnd = on;
		Runner::queue("dnd", {{"gsettings", "set", notifications_schema, "show-banners", (on ? "false" : "true")}}, 3000, false, "Failed to toggle Do Not Disturb");
	}

	void set_airplane(bool on) {
		current.airplane = on;
		Runner::queue("airplane", {{"rfkill", (on ? "block" : "unblock"), "all"}}, 5000, false, "Failed to toggle Airplane Mode");
	}
}
