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
#include <clocale>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

#include <langinfo.h>
#include <pthread.h>
#include <unistd.h>

#include "qstop.hpp"
#include "qstop_config.hpp"
#include "qstop_draw.hpp"
#include "qstop_input.hpp"
#include "qstop_menu.hpp"
#include "qstop_shared.hpp"
#include "qstop_theme.hpp"
#include "qstop_tools.hpp"

using std::cout;
using std::flush;
using std::min;

namespace fs = std::filesystem;

using namespace Tools;

namespace Global {
	const string Version = "1.0.0";

	atomic<bool> quitting (false);
	atomic<bool> should_quit (false);
	atomic<bool> should_sleep (false);
	atomic<bool> resized (false);
	string exit_error_msg;

	string overlay;
	string notification;
	uint64_t notification_expires = 0;

	const string fg_white = "\x1b[1;97m";
	const string fg_green = "\x1b[1;92m";
	const string fg_red = "\x1b[0;91m";

	void notify(const string& msg, uint64_t ms) {
		notification = msg;
		notification_expires = time_ms() + ms;
		Runner::run();
	}
}

namespace Shared {
	std::mutex mtx;
}

//* ------------------------------------------------ Runner ------------------------------------------------------------

namespace Runner {
	atomic<bool> active (false);
	atomic<bool> stopping (false);

	namespace {
		std::mutex mtx;
		std::condition_variable cv;
		bool pending = false;
		bool pending_collect = false;
		bool pending_force = false;
		std::thread runner_thread;

		atomic<bool> collecting (false);
		atomic<bool> recollect (false);
		atomic<bool> collected_once (false);

		//* Data domains, so collected data is discarded only for domains with actions in flight
		enum Domain { D_Audio, D_Display, D_Network, D_Bluetooth, D_Power, D_Desktop, D_None, D_Count };
		array<atomic<uint64_t>, D_Count> generations{};
		array<atomic<int>, D_Count> running{};

		Domain domain_of(const string& id) {
			if (id.starts_with("volume") or id.starts_with("mic") or is_in(id, "sink", "source")) return D_Audio;
			if (id.contains("brightness")) return D_Display;
			if (id.starts_with("wifi") or id == "wired" or id.starts_with("vpn_")) return D_Network;
			if (id.starts_with("bt_")) return D_Bluetooth;
			if (id == "power_profile") return D_Power;
			if (is_in(id, "night_light", "dark_style", "dnd", "airplane")) return D_Desktop;
			return D_None;
		}

		struct worker {
			vector<vector<string>> cmds;
			uint64_t timeout_ms = 0;
			bool until_success = false;
			string error_msg;
			bool has_pending = false;
			bool busy = false;
		};

		//? Guarded by Runner::mtx
		std::unordered_map<string, worker> workers;

		void worker_loop(const string id) {
			const Domain domain = domain_of(id);
			while (true) {
				worker job;
				{
					std::lock_guard lck(mtx);
					auto& w = workers.at(id);
					if (not w.has_pending or stopping) {
						w.busy = false;
						break;
					}
					job = w;
					w.has_pending = false;
					w.cmds.clear();
				}

				bool success = false;
				string output;
				for (const auto& cmd : job.cmds) {
					if (stopping) break;
					auto result = exec(cmd, job.timeout_ms, true);
					success = (result.status == 0);
					output = (result.timed_out ? "timed out" : result.output);
					if (job.until_success and success) break;
				}

				if (not success and not job.error_msg.empty() and not stopping) {
					string reason = output.substr(0, output.find('\n'));
					if (reason.starts_with("Error: ")) reason.erase(0, 7);
					std::lock_guard lck(Shared::mtx);
					Global::notify(job.error_msg + (reason.empty() ? "" : ": " + reason), 8000);
				}
			}
			--running.at(domain);
			if (not stopping) run(true);
		}

		void collect_all() {
			array<uint64_t, D_Count> start_generations{};
			for (size_t i = 0; i < D_Count; i++) start_generations[i] = generations[i];
			bool lists;
			{
				std::lock_guard lck(Shared::mtx);
				lists = Menu::active;
			}

			Audio::audio_info audio;
			Display::display_info display;
			Network::net_info net;
			Bluetooth::bt_info bt;
			Power::power_info power;
			Desktop::desktop_info desktop;
			{
				auto audio_future = std::async(std::launch::async, [&]{ Audio::collect(audio); });
				auto net_future = std::async(std::launch::async, [&]{ Network::collect(net, lists); });
				auto bt_future = std::async(std::launch::async, [&]{ Bluetooth::collect(bt); });
				auto power_future = std::async(std::launch::async, [&]{ Power::collect(power); });
				auto desktop_future = std::async(std::launch::async, [&]{ Desktop::collect(desktop); });
				Display::collect(display);
				audio_future.get();
				net_future.get();
				bt_future.get();
				power_future.get();
				desktop_future.get();
			}

			std::lock_guard lck(Shared::mtx);

			//? Discard results that may predate a queued action, the finished action triggers a new collect
			const auto fresh = [&](Domain domain) { return running[domain] == 0 and start_generations[domain] == generations[domain]; };
			if (fresh(D_Audio)) Audio::current = std::move(audio);
			if (fresh(D_Display)) Display::current = std::move(display);
			if (fresh(D_Network)) {
				//? Keep the last scanned network list if it wasn't collected this time
				if (not lists) net.networks = std::move(Network::current.networks);
				Network::current = std::move(net);
			}
			if (fresh(D_Bluetooth) and fresh(D_Desktop)) {
				//? bluetoothctl is skipped while backing off, keep previous state then
				if (bt.available or not Bluetooth::current.available) Bluetooth::current = std::move(bt);
			}
			if (fresh(D_Power)) Power::current = std::move(power);
			if (fresh(D_Desktop)) Desktop::current = std::move(desktop);
		}

		void collector() {
			try {
				collect_all();
			}
			catch (const std::exception& e) {
				Global::exit_error_msg = "Exception in collector thread -> " + string(e.what());
				Global::should_quit = true;
				Input::interrupt();
			}
			collected_once = true;
			collecting = false;
			if (stopping) return;
			if (recollect.exchange(false)) run(true);
			else run();
		}

		void _runner() {
			while (true) {
				bool collect, force;
				{
					std::unique_lock lck(mtx);
					cv.wait(lck, []{ return pending or stopping.load(); });
					if (stopping) break;
					collect = pending_collect;
					force = pending_force;
					pending = pending_collect = pending_force = false;
				}
				atomic_lock active_lock(active);

				//? Collection runs in its own thread so input stays responsive while commands are slow
				if (collect) {
					if (collecting.exchange(true)) recollect = true;
					else std::thread(collector).detach();
				}

				if (not collected_once) continue;

				try {
					std::lock_guard lck(Shared::mtx);
					if (stopping or not Term::initialized) continue;
					cout << Draw::draw_all(force) << flush;
				}
				catch (const std::exception& e) {
					Global::exit_error_msg = "Exception in runner thread -> " + string(e.what());
					Global::should_quit = true;
					Input::interrupt();
					break;
				}
			}
		}
	}

	void run(bool collect, bool force_redraw) {
		{
			std::lock_guard lck(mtx);
			pending = true;
			pending_collect = pending_collect or collect;
			pending_force = pending_force or force_redraw;
		}
		cv.notify_one();
	}

	void queue(const string& id, vector<vector<string>> cmds, uint64_t timeout_ms, bool until_success, const string& error_msg) {
		if (cmds.empty() or stopping) return;
		const Domain domain = domain_of(id);
		++generations.at(domain);
		std::lock_guard lck(mtx);
		auto& w = workers[id];
		w.cmds = std::move(cmds);
		w.timeout_ms = timeout_ms;
		w.until_success = until_success;
		w.error_msg = error_msg;
		w.has_pending = true;
		if (not w.busy) {
			w.busy = true;
			++running.at(domain);
			std::thread(worker_loop, id).detach();
		}
	}

	void start() {
		runner_thread = std::thread(_runner);
	}

	void stop() {
		{
			std::lock_guard lck(mtx);
			stopping = true;
		}
		cv.notify_all();
		if (runner_thread.joinable()) runner_thread.join();
	}
}

//* ----------------------------------------------- Signals ------------------------------------------------------------

void clean_quit(int sig) {
	if (Global::quitting) return;
	Global::quitting = true;
	Runner::stop();

	Config::write();

	if (Term::initialized) {
		Term::restore();
	}

	if (not Global::exit_error_msg.empty()) {
		sig = 1;
		std::cerr << Global::fg_red << "ERROR: " << Global::fg_white << Global::exit_error_msg << Fx::reset_base << std::endl;
	}

	//? Detached action threads may still be running, skip static destructors
	std::quick_exit((sig != -1 ? sig : 0));
}

static void _signal_handler(const int sig) {
	switch (sig) {
		case SIGINT:
		case SIGTERM:
		case SIGHUP:
			Global::should_quit = true;
			break;
		case SIGTSTP:
			Global::should_sleep = true;
			break;
		case SIGCONT:
		case SIGWINCH:
			Global::resized = true;
			break;
		case SIGUSR1:
			// Input::poll interrupt
			break;
	}
}

//* Restore terminal and stop the process, resuming from here on SIGCONT
static void _sleep() {
	Global::should_sleep = false;
	std::unique_lock lck(Shared::mtx);
	Term::restore();
	kill(getpid(), SIGSTOP);
	Term::init();
	if (not Config::getB("disable_mouse")) cout << Term::mouse_on << flush;
	lck.unlock();
	Global::resized = true;
}

//* ----------------------------------------------- CLI ----------------------------------------------------------------

namespace {
	struct cli_args {
		std::optional<bool> low_color;
		std::optional<bool> tty;
		std::optional<int> update_ms;
	};

	void usage() {
		cout << Global::fg_green << "qstop" << Fx::reset_base << " - GNOME quick settings in the terminal, v" << Global::Version << "\n\n"
			<< Fx::b << "Usage:" << Fx::ub << " qstop [OPTIONS]\n\n"
			<< Fx::b << "Options:\n" << Fx::ub
			<< "  -h, --help            show this help message and exit\n"
			<< "  -v, --version         show version info and exit\n"
			<< "  -lc, --low-color      disable truecolor, converts 24-bit colors to 256-color\n"
			<< "  -t, --tty-on          force (ON) tty mode, max 16 colors and tty friendly graph symbols\n"
			<< "  +t, --tty-off         force (OFF) tty mode\n"
			<< "  -u, --update <ms>     set the system state update rate in milliseconds\n"
			<< std::endl;
	}

	cli_args argumentParser(int argc, const char* argv[]) {
		cli_args args;
		for (int i = 1; i < argc; i++) {
			const string argument = argv[i];
			if (is_in(argument, "-v", "--version")) {
				cout << "qstop version: " << Fx::b << Global::Version << Fx::ub << std::endl;
				std::exit(0);
			}
			else if (is_in(argument, "-h", "--help")) {
				usage();
				std::exit(0);
			}
			else if (is_in(argument, "-lc", "--low-color")) {
				args.low_color = true;
			}
			else if (is_in(argument, "-t", "--tty-on")) {
				args.tty = true;
			}
			else if (is_in(argument, "+t", "--tty-off")) {
				args.tty = false;
			}
			else if (is_in(argument, "-u", "--update")) {
				if (++i >= argc or not isint(argv[i]) or not Config::intValid("update_ms", argv[i])) {
					std::cerr << Global::fg_red << "ERROR: " << Global::fg_white << "--update requires a value between 500 and 60000" << Fx::reset_base << std::endl;
					std::exit(1);
				}
				args.update_ms = std::stoi(argv[i]);
			}
			else {
				std::cerr << Global::fg_red << "ERROR: " << Global::fg_white << "Unknown argument: " << argument << Fx::reset_base << "\n\n";
				usage();
				std::exit(1);
			}
		}
		return args;
	}
}

//* --------------------------------------------- Main starts here! ---------------------------------------------------
auto qstop_main(int argc, const char* argv[]) -> int {

	//? ------------------------------------------------ INIT ---------------------------------------------------------

	const auto args = argumentParser(argc, argv);

	//? Setup a UTF-8 locale for character width calculations
	if (std::setlocale(LC_ALL, "") == nullptr or not string_view(nl_langinfo(CODESET)).contains("UTF-8")) {
		if (std::setlocale(LC_ALL, "C.UTF-8") == nullptr and std::setlocale(LC_ALL, "en_US.UTF-8") == nullptr) {
			std::cerr << Global::fg_red << "WARNING: " << Global::fg_white << "No UTF-8 locale found, wide characters may misalign." << Fx::reset_base << std::endl;
		}
	}

	//? Config
	vector<string> load_warnings;
	if (auto config_dir = Config::get_config_dir(); config_dir) {
		Config::conf_dir = *config_dir;
		Config::conf_file = *config_dir / "qstop.conf";
		Theme::user_theme_dir = *config_dir / "themes";
	}
	Config::load(Config::conf_file, load_warnings);
	if (Config::current_boxes.empty() and not Config::getS("shown_boxes").empty())
		Config::set_boxes(Config::getS("shown_boxes"));
	if (args.update_ms) Config::set("update_ms", *args.update_ms);

	//? Themes shipped relative to the binary
	std::error_code ec;
	if (const auto exe = fs::read_symlink("/proc/self/exe", ec); not ec) {
		const auto share = exe.parent_path().parent_path() / "share" / "qstop" / "themes";
		if (fs::is_directory(share, ec)) Theme::theme_dir = share;
		else if (fs::is_directory(exe.parent_path().parent_path() / "themes", ec)) Theme::theme_dir = exe.parent_path().parent_path() / "themes";
	}

	if (not Term::init()) {
		std::cerr << Global::fg_red << "ERROR: " << Global::fg_white << "No tty detected!\n"
			<< "qstop needs an interactive shell to run." << Fx::reset_base << std::endl;
		return 1;
	}

	//? Check for tty mode and color depth
	bool tty_mode = Config::getB("force_tty");
	if (const auto term = std::getenv("TERM"); term != nullptr and string_view(term) == "linux") tty_mode = true;
	if (args.tty) tty_mode = *args.tty;
	Config::set("tty_mode", tty_mode);
	Config::set("lowcolor", args.low_color.value_or(not Config::getB("truecolor")));

	Theme::updateThemes();
	Theme::setTheme();

	cout << Term::clear << (Config::getB("disable_mouse") ? "" : Term::mouse_on) << flush;

	//? Block signals in all threads, they are only delivered to the main thread while it waits in Input::poll
	sigset_t mask;
	sigemptyset(&mask);
	for (const int sig : {SIGINT, SIGTERM, SIGHUP, SIGTSTP, SIGCONT, SIGWINCH, SIGUSR1})
		sigaddset(&mask, sig);
	pthread_sigmask(SIG_BLOCK, &mask, &Input::signal_mask);
	struct sigaction action {};
	action.sa_handler = _signal_handler;
	sigemptyset(&action.sa_mask);
	for (const int sig : {SIGINT, SIGTERM, SIGHUP, SIGTSTP, SIGCONT, SIGWINCH, SIGUSR1})
		sigaction(sig, &action, nullptr);
	std::signal(SIGPIPE, SIG_IGN);

	Runner::start();
	Runner::run(true, true);

	if (not load_warnings.empty()) {
		std::lock_guard lck(Shared::mtx);
		Global::notify("Config: " + load_warnings.front(), 10000);
	}

	//? ------------------------------------------------ MAIN LOOP ----------------------------------------------------

	uint64_t next_update = time_ms() + Config::getI("update_ms");
	uint64_t last_second = time_ms() / 1000;

	while (true) {
		if (Global::should_quit) clean_quit(0);
		if (Global::should_sleep) _sleep();
		if (Global::resized) {
			Global::resized = false;
			Term::refresh();
			Runner::run(false, true);
		}

		const uint64_t now = time_ms();
		if (now >= next_update) {
			next_update = now + Config::getI("update_ms");
			Runner::run(true);
		}

		//? Wake at least every second to update the clock and expire notifications
		if (Input::poll(min(next_update - now, 1001 - now % 1000))) {
			do {
				Input::process(Input::get());
			} while (Input::pending() and not Global::should_quit);
		}

		if (const uint64_t second = time_ms() / 1000; second != last_second) {
			last_second = second;
			Runner::run();
		}
	}
}
