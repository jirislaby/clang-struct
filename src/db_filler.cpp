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
#include <sl/helpers/Exception.h>

#include "server.h"
#include "sqlconn.h"

using namespace ClangStruct;

using Clr = SlHelpers::Color;
using RunEx = SlHelpers::RuntimeException;

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

void handledMain(int argc, char **argv)
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
			return;
		}
		if (opts.contains("unlink"))
			server.unlink();
	} catch (const cxxopts::exceptions::parsing &e) {
		Clr(std::cerr, Clr::RED) << "arguments error: " << e.what();
		std::cerr << options.help();
		RunEx("").raise();
	}

	if (server.open() < 0)
		RunEx("server open failed").raise();

	if (!sqlConn.open())
		RunEx(sqlConn.lastError()).raise();

	if (!autocommit && !sqlConn.begin())
		RunEx(sqlConn.lastError()).raise();

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
					RunEx("SQL commit failed").raise();
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
			RunEx("SQL commit failed").raise();
	}
	sqlConn.exec("VACUUM;");
	std::cerr << "bye\n";
}

} // namespace

int main(int argc, char **argv)
{
	try {
		handledMain(argc, argv);
		return 0;
	} catch (const std::runtime_error &e) {
		Clr(std::cerr, Clr::RED) << e.what();
		return EXIT_FAILURE;
	}
}
