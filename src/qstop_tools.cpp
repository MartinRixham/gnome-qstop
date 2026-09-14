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

#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "qstop_tools.hpp"

using std::cout;
using std::flush;
using std::max;

extern char** environ;

namespace fs = std::filesystem;

namespace Fx {
	const string e = "\x1b[";
	const string b = e + "1m";
	const string ub = e + "22m";
	const string d = e + "2m";
	const string ud = e + "22m";
	const string i = e + "3m";
	const string ui = e + "23m";
	const string ul = e + "4m";
	const string uul = e + "24m";
	const string bl = e + "5m";
	const string ubl = e + "25m";
	const string s = e + "9m";
	const string us = e + "29m";

	const string reset_base = e + "0m";

	string reset = reset_base;

	const std::regex color_regex("\x1b\\[\\d+;?\\d*;?\\d*;?\\d*;?\\d*(m){1}");
}

namespace Term {

	atomic<bool> initialized{};
	atomic<int> width{};
	atomic<int> height{};
	string fg, bg;

	const string hide_cursor = Fx::e + "?25l";
	const string show_cursor = Fx::e + "?25h";
	const string alt_screen = Fx::e + "?1049h";
	const string normal_screen = Fx::e + "?1049l";
	const string clear = Fx::e + "2J" + Fx::e + "0;0f";
	const string clear_end = Fx::e + "0J";
	const string mouse_on = Fx::e + "?1002h" + Fx::e + "?1015h" + Fx::e + "?1006h";
	const string mouse_off = Fx::e + "?1002l" + Fx::e + "?1015l" + Fx::e + "?1006l";
	const string sync_start = Fx::e + "?2026h";
	const string sync_end = Fx::e + "?2026l";

	namespace {
		struct termios initial_settings;

		//* Toggle terminal input echo
		bool echo(bool on=true) {
			struct termios settings;
			if (tcgetattr(STDIN_FILENO, &settings)) return false;
			if (on) settings.c_lflag |= ECHO;
			else settings.c_lflag &= ~(ECHO);
			return 0 == tcsetattr(STDIN_FILENO, TCSANOW, &settings);
		}

		//* Toggle need for return key when reading input
		bool linebuffered(bool on=true) {
			struct termios settings;
			if (tcgetattr(STDIN_FILENO, &settings)) return false;
			if (on) settings.c_lflag |= ICANON;
			else {
				settings.c_lflag &= ~(ICANON);
				settings.c_cc[VMIN] = 0;
				settings.c_cc[VTIME] = 0;
			}
			if (tcsetattr(STDIN_FILENO, TCSANOW, &settings)) return false;
			if (on) setlinebuf(stdin);
			else setbuf(stdin, nullptr);
			return true;
		}
	}

	bool refresh(bool only_check) {
		struct winsize wsize {};
		if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wsize) < 0 or (wsize.ws_col == 0 and wsize.ws_row == 0)) {
			auto dev_tty = open("/dev/tty", O_RDONLY | O_CLOEXEC);
			if (dev_tty == -1) return false;
			ioctl(dev_tty, TIOCGWINSZ, &wsize);
			close(dev_tty);
		}
		if (width != wsize.ws_col or height != wsize.ws_row) {
			if (not only_check) {
				width = wsize.ws_col;
				height = wsize.ws_row;
			}
			return true;
		}
		return false;
	}

	bool init() {
		if (not initialized) {
			initialized = (bool)isatty(STDIN_FILENO);
			if (initialized) {
				tcgetattr(STDIN_FILENO, &initial_settings);

				//? Disable stream sync and ties, only once since init is called again when resuming from SIGTSTP
				static bool streams_setup = false;
				if (not streams_setup) {
					cout.sync_with_stdio(false);
					cout.tie(nullptr);
					streams_setup = true;
				}
				echo(false);
				linebuffered(false);
				refresh();

				cout << alt_screen << hide_cursor << flush;
			}
		}
		return initialized;
	}

	void restore() {
		if (initialized) {
			tcsetattr(STDIN_FILENO, TCSANOW, &initial_settings);
			cout << clear << Fx::reset_base << mouse_off << normal_screen << show_cursor << flush;
			initialized = false;
		}
	}
}

namespace Tools {

	namespace {
		//* Decode next UTF8 character in <str> from <pos>, returning byte length and terminal column width
		std::pair<size_t, int> next_char(const std::string_view str, size_t pos, bool wide) {
			mbstate_t state{};
			wchar_t wc{};
			size_t n = mbrtowc(&wc, str.data() + pos, str.size() - pos, &state);
			if (n == static_cast<size_t>(-1) or n == static_cast<size_t>(-2)) return {1, 1};
			if (n == 0) n = 1;
			if (not wide) return {n, 1};
			const int w = wcwidth(wc);
			return {n, (w < 0 ? 1 : w)};
		}
	}

	size_t ulen(const std::string_view str, bool wide) {
		if (not wide)
			return std::ranges::count_if(str, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; });
		size_t len = 0;
		for (size_t pos = 0; pos < str.size();) {
			const auto [n, w] = next_char(str, pos, true);
			len += w;
			pos += n;
		}
		return len;
	}

	string uresize(const string& str, const size_t len, bool wide) {
		if (len < 1 or str.empty())
			return "";
		size_t cols = 0, pos = 0;
		while (pos < str.size()) {
			const auto [n, w] = next_char(str, pos, wide);
			if (cols + w > len) break;
			cols += w;
			pos += n;
		}
		return str.substr(0, pos);
	}

	string s_replace(const string& str, const string& from, const string& to) {
		string out = str;
		if (from.empty()) return out;
		for (size_t pos = 0; (pos = out.find(from, pos)) != string::npos; pos += to.size())
			out.replace(pos, from.size(), to);
		return out;
	}

	string_view ltrim(string_view str, const string_view t_str) {
		while (not t_str.empty() and str.starts_with(t_str))
			str.remove_prefix(t_str.size());
		return str;
	}

	string_view rtrim(string_view str, const string_view t_str) {
		while (not t_str.empty() and str.ends_with(t_str))
			str.remove_suffix(t_str.size());
		return str;
	}

	string ljust(string str, const size_t x, bool utf, bool wide, bool limit) {
		const size_t len = (utf ? ulen(str, wide) : str.size());
		if (limit and len > x)
			return (utf ? uresize(str, x, wide) : str.substr(0, x));
		return str + string(max((int)x - (int)len, 0), ' ');
	}

	string rjust(string str, const size_t x, bool utf, bool wide, bool limit) {
		const size_t len = (utf ? ulen(str, wide) : str.size());
		if (limit and len > x)
			return (utf ? uresize(str, x, wide) : str.substr(0, x));
		return string(max((int)x - (int)len, 0), ' ') + str;
	}

	string cjust(string str, const size_t x, bool utf, bool wide, bool limit) {
		const size_t len = (utf ? ulen(str, wide) : str.size());
		if (limit and len > x)
			return (utf ? uresize(str, x, wide) : str.substr(0, x));
		const int pad = max((int)x - (int)len, 0);
		return string((int)std::ceil(pad / 2.0), ' ') + str + string(pad / 2, ' ');
	}

	string sec_to_hm(int64_t seconds) {
		if (seconds < 0) return "";
		const int64_t hours = seconds / 3600;
		const int64_t minutes = (seconds % 3600) / 60;
		return to_string(hours) + ':' + (minutes < 10 ? "0" : "") + to_string(minutes);
	}

	std::string operator*(const string& str, int64_t n) {
		if (n < 1 or str.empty()) {
			return "";
		}
		else if (n == 1) {
			return str;
		}

		string new_str;
		new_str.reserve(str.size() * n);

		for (; n > 0; n--)
			new_str.append(str);

		return new_str;
	}

	string strf_time(const string& strf) {
		auto in_time_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::tm bt {};
		std::stringstream ss;
		ss << std::put_time(localtime_r(&in_time_t, &bt), strf.c_str());
		return ss.str();
	}

	string hostname() {
		char host[256];
		if (gethostname(host, sizeof(host)) != 0) return "";
		host[sizeof(host) - 1] = '\0';
		return host;
	}

	string username() {
		auto user = getenv("LOGNAME");
		if (user == nullptr or strlen(user) == 0) user = getenv("USER");
		if (user == nullptr or strlen(user) == 0) {
			if (const auto* pw = getpwuid(getuid()); pw != nullptr) return pw->pw_name;
			return "";
		}
		return user;
	}

	atomic_lock::atomic_lock(atomic<bool>& atom) : atom(atom) {
		this->atom.store(true);
	}

	atomic_lock::~atomic_lock() noexcept {
		this->atom.store(false);
	}

	string readfile(const std::filesystem::path& path, const string& fallback) {
		std::error_code ec;
		if (not fs::exists(path, ec)) return fallback;
		string out;
		try {
			std::ifstream file(path);
			for (string readstr; getline(file, readstr); out += readstr + (file.eof() ? "" : "\n"));
		}
		catch (const std::exception&) {
			return fallback;
		}
		return (out.empty() ? fallback : out);
	}

	namespace {
		//* Environment for child processes with a stable locale for parsing output
		vector<char*> child_env(vector<string>& storage) {
			storage.clear();
			for (char** env = environ; env != nullptr and *env != nullptr; ++env) {
				const string_view var = *env;
				if (var.starts_with("LC_ALL=") or var.starts_with("LANGUAGE=")) continue;
				storage.emplace_back(var);
			}
			storage.emplace_back("LC_ALL=C.UTF-8");
			vector<char*> envp;
			envp.reserve(storage.size() + 1);
			for (auto& var : storage) envp.push_back(var.data());
			envp.push_back(nullptr);
			return envp;
		}

		//* Spawn <args> with stdin from /dev/null and stdout to <out_fd> (or /dev/null if -1)
		pid_t spawn(const vector<string>& args, int out_fd, bool merge_stderr, bool new_session) {
			posix_spawn_file_actions_t actions;
			posix_spawn_file_actions_init(&actions);
			posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
			if (out_fd >= 0) posix_spawn_file_actions_adddup2(&actions, out_fd, STDOUT_FILENO);
			else posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
			if (merge_stderr and out_fd >= 0) posix_spawn_file_actions_adddup2(&actions, out_fd, STDERR_FILENO);
			else posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

			//? Children must not inherit the blocked signal mask used by qstop threads
			posix_spawnattr_t attr;
			posix_spawnattr_init(&attr);
			sigset_t empty_mask, default_mask;
			sigemptyset(&empty_mask);
			sigfillset(&default_mask);
			posix_spawnattr_setsigmask(&attr, &empty_mask);
			posix_spawnattr_setsigdefault(&attr, &default_mask);
			short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
			if (new_session) flags |= POSIX_SPAWN_SETSID;
			posix_spawnattr_setflags(&attr, flags);

			vector<char*> argv;
			argv.reserve(args.size() + 1);
			for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
			argv.push_back(nullptr);
			vector<string> env_storage;
			auto envp = child_env(env_storage);

			pid_t pid = -1;
			if (posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), envp.data()) != 0) pid = -1;
			posix_spawn_file_actions_destroy(&actions);
			posix_spawnattr_destroy(&attr);
			return pid;
		}

		int wait_child(pid_t pid) {
			int status = 0;
			while (waitpid(pid, &status, 0) < 0) {
				if (errno != EINTR) return -1;
			}
			return (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		}
	}

	ExecResult exec(const vector<string>& args, uint64_t timeout_ms, bool merge_stderr) {
		ExecResult result;
		if (args.empty()) return result;

		int pipefd[2];
		if (pipe2(pipefd, O_CLOEXEC) != 0) return result;

		const pid_t pid = spawn(args, pipefd[1], merge_stderr, false);
		close(pipefd[1]);
		if (pid < 0) {
			close(pipefd[0]);
			return result;
		}

		const uint64_t deadline = time_ms() + timeout_ms;
		char buf[4096];
		while (true) {
			const int64_t remaining = (int64_t)deadline - (int64_t)time_ms();
			if (remaining <= 0) {
				result.timed_out = true;
				kill(pid, SIGKILL);
				break;
			}
			struct pollfd pfd { pipefd[0], POLLIN, 0 };
			const int ready = poll(&pfd, 1, (int)remaining);
			if (ready < 0) {
				if (errno == EINTR) continue;
				break;
			}
			if (ready == 0) continue;
			const ssize_t count = read(pipefd[0], buf, sizeof(buf));
			if (count > 0) result.output.append(buf, count);
			else if (count == 0 or (errno != EINTR and errno != EAGAIN)) break;
		}
		close(pipefd[0]);
		result.status = wait_child(pid);
		if (result.timed_out) result.status = -1;
		return result;
	}

	bool spawn_detached(const vector<string>& args) {
		if (args.empty() or not command_exists(args.front())) return false;
		//? Double fork through setsid so the launched program is reparented and never becomes a zombie of qstop
		vector<string> cmd = {"setsid", "-f"};
		cmd.insert(cmd.end(), args.begin(), args.end());
		const pid_t pid = spawn(cmd, -1, false, false);
		if (pid < 0) return false;
		return wait_child(pid) == 0;
	}

	bool command_exists(const string& name) {
		if (name.empty()) return false;
		if (name.contains('/')) return access(name.c_str(), X_OK) == 0;
		const char* path = getenv("PATH");
		if (path == nullptr) return false;
		for (const auto& dir : ssplit(path, ':')) {
			if (access((fs::path(dir) / name).c_str(), X_OK) == 0) return true;
		}
		return false;
	}
}
