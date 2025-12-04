// SPDX-License-Identifier: GPL-2.0-only

#include <csignal>
#include <cxxopts.hpp>
#include <iostream>

#include <fcntl.h>
#include <unistd.h>

//#include <sys/socket.h>
//#include <sys/un.h>
#include <sys/stat.h>

#include <sl/helpers/Color.h>

#include "server.h"
#include "sqlconn.h"

using Clr = SlHelpers::Color;

namespace {

volatile std::sig_atomic_t stop;

Server server;
SQLConn sqlConn;

void sig(int sig)
{
	stop = true;
	server.close();
	if (sig == SIGABRT)
		_exit(EXIT_FAILURE);
}

} // namespace

int main(int argc, char **argv)
{
	signal(SIGABRT, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	bool autocommit = false;
	cxxopts::Options options { argv[0], "Fill in structs.db" };
	options.add_options()
		("h,help", "Print this help message")
		("a,autocommit", "Autocommit instead of transactions",
		 cxxopts::value(autocommit)->default_value("false"))
		("u,unlink", "Unlink the queue before any other work")
	;

	try {
		const auto opts = options.parse(argc, argv);
		if (opts.contains("help")) {
			std::cout << options.help();
			return 0;
		}
		if (opts.contains("unlink"))
			server.unlink();
	} catch (const cxxopts::exceptions::parsing &e) {
		Clr(std::cerr, Clr::RED) << "arguments error: " << e.what();
		std::cerr << options.help();
		return EXIT_FAILURE;
	}

	if (server.open() < 0)
		return EXIT_FAILURE;

	if (!sqlConn.open()) {
		Clr(std::cerr, Clr::RED) << sqlConn.lastError();
		return EXIT_FAILURE;
	}
	if (!autocommit && !sqlConn.begin()) {
		Clr(std::cerr, Clr::RED) << sqlConn.lastError();
		return EXIT_FAILURE;
	}

	Message<std::string_view> msg;
	bool should_commit = false;

	while (true) {
		auto msgStr = server.read();
		if (stop || !msgStr)
			break;

		if (msgStr->empty()) {
			if (should_commit) {
				std::cerr << "commiting\n";
				if (!sqlConn.end() || !sqlConn.begin())
					return EXIT_FAILURE;
				should_commit = false;
			}
			continue;
		}

		msg.deserialize(*msgStr);

		//std::cerr << "===" << msg << "\n";

		sqlConn.handleMessage(msg);
		should_commit = !autocommit;
	}

	if (!autocommit) {
		std::cerr << "commiting\n";
		if (!sqlConn.end())
			return EXIT_FAILURE;
	}
	std::cerr << "bye\n";

	return 0;
}
