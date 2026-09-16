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

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

using std::atomic;
using std::string;
using std::vector;

void clean_quit(int sig);

namespace Global {
	extern const string Version;
	extern atomic<bool> quitting;
	extern atomic<bool> should_quit;
	extern atomic<bool> should_sleep;
	extern atomic<bool> resized;
	extern string exit_error_msg;

	//* Overlay drawn on top of boxes when a menu is active (Shared::mtx)
	extern string overlay;

	//* Transient message shown in the status box, e.g. a failed action (Shared::mtx)
	extern string notification;
	extern uint64_t notification_expires;

	//* Show <msg> in the status box for <ms> milliseconds, caller must hold Shared::mtx
	void notify(const string& msg, uint64_t ms = 5000);
}

namespace Shared {
	//* Guards all collected data, focus, menu and draw state shared between the input, runner and action threads
	extern std::mutex mtx;
}

namespace Runner {
	extern atomic<bool> active;
	extern atomic<bool> stopping;

	//* Wake the runner thread to redraw, optionally collecting new data first and clearing the screen
	void run(bool collect = false, bool force_redraw = false);

	//* Run <cmds> in order on a background worker named <id>
	//* Newer commands for the same <id> replace any not yet started, so repeated slider steps coalesce
	//* If <until_success> only runs commands until one exits successfully (for fallback alternatives)
	void queue(const string& id, vector<vector<string>> cmds, uint64_t timeout_ms = 10000, bool until_success = false, const string& error_msg = "");

	void start();
	void stop();
}

namespace Audio {
	struct device {
		string name, description;
	};

	struct audio_info {
		bool available = false;
		bool wpctl = false; //? Controlled through WirePlumber's wpctl, otherwise pactl
		bool has_sink = false, has_source = false;
		int volume = 0;
		bool muted = false;
		int mic_volume = 0;
		bool mic_muted = false;
		string sink, source;
		vector<device> sinks, sources;
	};

	extern audio_info current;

	void collect(audio_info& info);

	void set_volume(int percent);
	void set_mute(bool mute);
	void set_mic_volume(int percent);
	void set_mic_mute(bool mute);
	void set_sink(const string& name);
	void set_source(const string& name);
}

namespace Display {
	struct backlight {
		bool present = false;
		string subsystem, name;
		int value = 0, max = 0;
		int percent() const { return (max > 0 ? (int)((int64_t)value * 100 / max) : 0); }
	};

	struct display_info {
		backlight screen, kbd;
	};

	extern display_info current;

	void collect(display_info& info);

	void set_brightness(int percent);
	void set_kbd_brightness(int percent);
}

namespace Network {
	struct wifi_network {
		string ssid, security;
		int signal = 0;
		bool active = false;
		bool known = false;
	};

	struct vpn_connection {
		string name;
		bool active = false;
	};

	struct net_info {
		bool available = false;
		bool has_wifi = false, wifi_enabled = false;
		string wifi_device, wifi_connection, wifi_state;
		bool has_wired = false, wired_connected = false;
		string wired_device, wired_connection;
		vector<wifi_network> networks;
		vector<vpn_connection> vpns;
	};

	extern net_info current;

	//* Collect device state, also scans visible networks if <networks> is true
	void collect(net_info& info, bool networks);

	void set_wifi(bool enabled);
	void set_wired(bool connected);
	void connect(const wifi_network& network, const string& password = "");
	void disconnect_wifi();
	void rescan();
	void set_vpn(const string& name, bool active);
}

namespace Bluetooth {
	struct bt_device {
		string address, name;
		bool connected = false;
	};

	struct bt_info {
		bool available = false;
		bool powered = false;
		vector<bt_device> devices;
		int connected_count() const;
	};

	extern bt_info current;

	void collect(bt_info& info);

	void set_power(bool on);
	void set_connected(const bt_device& device, bool connect);
}

namespace Power {
	struct battery_info {
		bool present = false;
		int percent = 0;
		string status;
		int64_t seconds = -1;
	};

	struct power_info {
		battery_info battery;
		bool has_profiles = false;
		string profile;
		vector<string> profiles;
	};

	extern power_info current;

	void collect(power_info& info);

	//* Return a human readable name for power profile <profile>
	string profile_name(const string& profile);
	void set_profile(const string& profile);

	enum class Action { Suspend, Restart, PowerOff, LogOut };
	void run_action(Action action);

	void lock_screen();
	void screenshot();

	//* Open GNOME Settings, optionally at <panel>
	void open_settings(const string& panel = "");
}

namespace Desktop {
	struct desktop_info {
		bool available = false;
		bool night_light = false;
		bool dark_style = false;
		bool dnd = false;
		bool has_rfkill = false;
		bool airplane = false;
	};

	extern desktop_info current;

	void collect(desktop_info& info);

	void set_night_light(bool on);
	void set_dark_style(bool on);
	void set_dnd(bool on);
	void set_airplane(bool on);
}
