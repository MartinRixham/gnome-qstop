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

//* Collector tests against command output recorded on a Raspberry Pi running GNOME on Ubuntu, where
//* PipeWire/WirePlumber is the sound server without pulseaudio-utils (no pactl) and eth0 has no cable.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include <unistd.h>

#include <gtest/gtest.h>

#include "qstop_draw.hpp"
#include "qstop_shared.hpp"
#include "qstop_tools.hpp"

namespace fs = std::filesystem;

namespace {
	const fs::path fixtures = fs::path(QSTOP_TEST_FIXTURES) / "pi";

	string fixture(const string& name) {
		std::ifstream file(fixtures / name);
		if (not file) throw std::runtime_error("missing fixture " + name);
		std::stringstream ss;
		ss << file.rdbuf();
		return ss.str();
	}

	//* Replaces PATH with a temporary directory of fake commands, each answering exact argument lists with canned
	//* output and exiting 1 for anything else. Scripts use only shell builtins, so no real tool is ever reachable.
	class FakeCommands {
		fs::path dir;
		string old_path;
		bool had_path;
		int counter = 0;

	public:
		FakeCommands() {
			string templ = (fs::temp_directory_path() / "qstop_test_XXXXXX").string();
			if (mkdtemp(templ.data()) == nullptr) throw std::runtime_error("mkdtemp failed");
			dir = templ;
			const char* path = std::getenv("PATH");
			had_path = (path != nullptr);
			if (had_path) old_path = path;
			setenv("PATH", dir.c_str(), 1);
		}

		~FakeCommands() {
			if (had_path) setenv("PATH", old_path.c_str(), 1);
			else unsetenv("PATH");
			std::error_code ec;
			fs::remove_all(dir, ec);
		}

		FakeCommands(const FakeCommands&) = delete;
		FakeCommands& operator=(const FakeCommands&) = delete;

		//* Install <name> answering each space joined argument list in <responses> with its output
		void add(const string& name, const std::map<string, string>& responses) {
			string script = "#!/bin/sh\ncase \"$*\" in\n";
			for (const auto& [args, output] : responses) {
				const fs::path out_file = dir / ("." + name + "_" + to_string(counter++));
				std::ofstream(out_file) << output;
				script += "\t'" + args + "')\n\t\twhile IFS= read -r l || [ -n \"$l\" ]; do printf '%s\\n' \"$l\"; done < '"
					+ out_file.string() + "'\n\t\texit 0 ;;\n";
			}
			script += "esac\nexit 1\n";
			const fs::path cmd = dir / name;
			std::ofstream(cmd) << script;
			fs::permissions(cmd, fs::perms::owner_all);
		}
	};

	//* wpctl as found on the Pi: one HDMI sink at 72%, no sources. Note that WirePlumber resolves
	//* @DEFAULT_AUDIO_SOURCE@ to the sink when there is no source, so it still reports a volume.
	std::map<string, string> pi_wpctl(const string& status, const string& sink_volume) {
		return {
			{"status", status},
			{"get-volume @DEFAULT_AUDIO_SINK@", sink_volume},
			{"get-volume @DEFAULT_SINK@", sink_volume},
			{"get-volume 73", sink_volume},
			{"get-volume @DEFAULT_AUDIO_SOURCE@", fixture("wpctl-get-volume-source.txt")},
			{"get-volume @DEFAULT_SOURCE@", fixture("wpctl-get-volume-source.txt")},
			{"inspect @DEFAULT_AUDIO_SINK@", fixture("wpctl-inspect-sink.txt")},
			{"inspect @DEFAULT_SINK@", fixture("wpctl-inspect-sink.txt")},
			{"inspect 73", fixture("wpctl-inspect-sink.txt")},
			{"inspect @DEFAULT_AUDIO_SOURCE@", fixture("wpctl-inspect-source.txt")},
			{"inspect @DEFAULT_SOURCE@", fixture("wpctl-inspect-source.txt")},
		};
	}

	std::map<string, string> pi_nmcli(const string& device_list, const string& eth0_show) {
		return {
			{"-t -f DEVICE,TYPE,STATE,CONNECTION device", device_list},
			{"-t -f WIFI radio", fixture("nmcli-radio-wifi.txt")},
			{"-t -f NAME,TYPE,ACTIVE connection show", fixture("nmcli-connection-show.txt")},
			{"-t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list --rescan no", fixture("nmcli-wifi-list.txt")},
			{"-t -f GENERAL.STATE,WIRED-PROPERTIES.CARRIER device show eth0", eth0_show},
			{"-t -f WIRED-PROPERTIES.CARRIER device show eth0", eth0_show.substr(eth0_show.find('\n') + 1)},
		};
	}

	bool has_toggle(const string& id) {
		return std::ranges::any_of(Draw::Toggles::items(), [&](const auto& t) { return t.id == id; });
	}
}

//? ------------------------------------------ Audio without pactl (PipeWire) -------------------------------------------

class PipeWireAudio : public ::testing::Test {
protected:
	FakeCommands commands;
	Audio::audio_info info;

	void SetUp() override {
		Audio::current = {};
	}

	void collect(const string& status, const string& sink_volume) {
		commands.add("wpctl", pi_wpctl(status, sink_volume));
		Audio::collect(info);
		Audio::current = info;
	}
};

TEST_F(PipeWireAudio, IsAvailableWithoutPactl) {
	collect(fixture("wpctl-status.txt"), fixture("wpctl-get-volume-sink.txt"));
	EXPECT_TRUE(info.available);
	EXPECT_TRUE(info.has_sink);
}

TEST_F(PipeWireAudio, ReadsDefaultSinkVolume) {
	collect(fixture("wpctl-status.txt"), fixture("wpctl-get-volume-sink.txt"));
	EXPECT_EQ(info.volume, 72);
	EXPECT_FALSE(info.muted);
}

TEST_F(PipeWireAudio, ReadsMutedSink) {
	string status = fixture("wpctl-status.txt");
	const string unmuted = "[vol: 0.72]";
	ASSERT_NE(status.find(unmuted), string::npos);
	status.replace(status.find(unmuted), unmuted.size(), "[vol: 0.72 MUTED]");

	collect(status, "Volume: 0.72 [MUTED]\n");
	EXPECT_TRUE(info.muted);
	EXPECT_EQ(info.volume, 72);
}

TEST_F(PipeWireAudio, ListsSinkWithDescription) {
	collect(fixture("wpctl-status.txt"), fixture("wpctl-get-volume-sink.txt"));
	ASSERT_EQ(info.sinks.size(), 1u);
	EXPECT_EQ(info.sinks.front().description, "Built-in Audio Digital Stereo (HDMI)");
	EXPECT_EQ(info.sink, info.sinks.front().name);
}

TEST_F(PipeWireAudio, NoMicrophoneWhenThereAreNoSources) {
	collect(fixture("wpctl-status.txt"), fixture("wpctl-get-volume-sink.txt"));
	EXPECT_FALSE(info.has_source);
	EXPECT_TRUE(info.sources.empty());
}

TEST_F(PipeWireAudio, ShowsVolumeSliderOnly) {
	collect(fixture("wpctl-status.txt"), fixture("wpctl-get-volume-sink.txt"));
	const auto sliders = Draw::Sliders::items();
	ASSERT_EQ(sliders.size(), 1u);
	EXPECT_EQ(sliders.front().id, "volume");
	EXPECT_EQ(sliders.front().value, 72);
	EXPECT_FALSE(sliders.front().expandable);
}

//? ---------------------------------------------- Wired without a cable -----------------------------------------------

class WiredToggle : public ::testing::Test {
protected:
	FakeCommands commands;
	Network::net_info info;

	void SetUp() override {
		Network::current = {};
	}

	void collect(const string& device_list, const string& eth0_show) {
		commands.add("nmcli", pi_nmcli(device_list, eth0_show));
		Network::collect(info, true);
		Network::current = info;
	}
};

TEST_F(WiredToggle, HiddenWhenEthernetUnavailable) {
	collect(fixture("nmcli-device.txt"), fixture("nmcli-device-show-eth0.txt"));
	EXPECT_FALSE(has_toggle("wired"));
	EXPECT_TRUE(has_toggle("wifi"));
	EXPECT_EQ(info.wifi_connection, "CommunityFibre10Gb_E8993");
}

TEST_F(WiredToggle, ShownWhenCablePluggedInButDisconnected) {
	string devices = fixture("nmcli-device.txt");
	const string unavailable = "eth0:ethernet:unavailable:";
	ASSERT_NE(devices.find(unavailable), string::npos);
	devices.replace(devices.find(unavailable), unavailable.size(), "eth0:ethernet:disconnected:");

	collect(devices, "GENERAL.STATE:30 (disconnected)\nWIRED-PROPERTIES.CARRIER:on\n");
	ASSERT_TRUE(has_toggle("wired"));
	EXPECT_FALSE(info.wired_connected);
}

TEST_F(WiredToggle, ShownWhenConnected) {
	string devices = fixture("nmcli-device.txt");
	const string unavailable = "eth0:ethernet:unavailable:";
	ASSERT_NE(devices.find(unavailable), string::npos);
	devices.replace(devices.find(unavailable), unavailable.size(), "eth0:ethernet:connected:netplan-eth0");

	collect(devices, "GENERAL.STATE:100 (connected)\nWIRED-PROPERTIES.CARRIER:on\n");
	ASSERT_TRUE(has_toggle("wired"));
	EXPECT_TRUE(info.wired_connected);
	EXPECT_EQ(info.wired_connection, "netplan-eth0");
}

//? ------------------------------------------ pactl fallback -----------------------------------------------------------

namespace {
	const string pactl_sinks = "Sink #52\n\tName: alsa_output.hdmi-stereo\n\tDescription: Built-in Audio Digital Stereo (HDMI)\n"
		"\tMute: no\n\tVolume: front-left: 26214 /  40% / -23.88 dB,   front-right: 26214 /  40% / -23.88 dB\n";
	const string pactl_sources = "Source #53\n\tName: alsa_output.hdmi-stereo.monitor\n\tDescription: Monitor of Built-in Audio\n"
		"\tMute: no\n\tVolume: front-left: 65536 / 100% / 0.00 dB,   front-right: 65536 / 100% / 0.00 dB\n";

	std::map<string, string> pactl_responses() {
		return {
			{"get-default-sink", "alsa_output.hdmi-stereo\n"},
			{"get-default-source", "alsa_output.hdmi-stereo.monitor\n"},
			{"list sinks", pactl_sinks},
			{"list sources", pactl_sources},
		};
	}
}

TEST(PactlFallback, UsedWhenWpctlMissing) {
	FakeCommands commands;
	commands.add("pactl", pactl_responses());
	Audio::audio_info info;
	Audio::collect(info);
	EXPECT_TRUE(info.available);
	EXPECT_FALSE(info.wpctl);
	EXPECT_TRUE(info.has_sink);
	EXPECT_EQ(info.sink, "alsa_output.hdmi-stereo");
	EXPECT_EQ(info.volume, 40);
	EXPECT_FALSE(info.has_source);
}

TEST(PactlFallback, UsedWhenWpctlCannotReachPipeWire) {
	FakeCommands commands;
	commands.add("wpctl", {});
	commands.add("pactl", pactl_responses());
	Audio::audio_info info;
	Audio::collect(info);
	EXPECT_TRUE(info.available);
	EXPECT_FALSE(info.wpctl);
	EXPECT_EQ(info.volume, 40);
}

TEST(PactlFallback, NotUsedWhenWpctlWorks) {
	FakeCommands commands;
	commands.add("wpctl", pi_wpctl(fixture("wpctl-status.txt"), fixture("wpctl-get-volume-sink.txt")));
	commands.add("pactl", pactl_responses());
	Audio::audio_info info;
	Audio::collect(info);
	EXPECT_TRUE(info.wpctl);
	EXPECT_EQ(info.volume, 72);
	EXPECT_EQ(info.sink, "73");
}

TEST_F(WiredToggle, HiddenWhenEthernetUnmanaged) {
	string devices = fixture("nmcli-device.txt");
	const string unavailable = "eth0:ethernet:unavailable:";
	ASSERT_NE(devices.find(unavailable), string::npos);
	devices.replace(devices.find(unavailable), unavailable.size(), "eth0:ethernet:unmanaged:");

	collect(devices, "GENERAL.STATE:10 (unmanaged)\nWIRED-PROPERTIES.CARRIER:on\n");
	EXPECT_FALSE(has_toggle("wired"));
}
