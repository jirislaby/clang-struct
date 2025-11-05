#include <csignal>
#include <cstring>
#include <iostream>
#include <optional>

#include <fcntl.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>

//#include <sys/socket.h>
//#include <sys/un.h>
#include <sys/stat.h>

#include <sl/helpers/Color.h>

#include "server.h"
#include "sqlconn.h"

using Clr = SlHelpers::Color;

volatile std::sig_atomic_t stop;

static Server server;
static SQLConn sqlConn;

void sig(int sig)
{
	stop = true;
	server.close();
	if (sig == SIGABRT)
		_exit(EXIT_FAILURE);
}

static void usage(const char *exe, const struct option longopts[])
{
	std::cerr << "Options of " << exe << "\n";
	for (auto &opt = longopts; opt->name; opt++)
		std::cerr << "\t" << opt->name << "\n";
}

int main(int argc, char **argv)
{
	signal(SIGABRT, sig);
	signal(SIGINT, sig);
	signal(SIGTERM, sig);

	static const struct option longopts[] = {
		{ "autocommit", 0, NULL, 'a' },
		{ "unlink", 0, NULL, 'u' },
		{}
	};
	bool autocommit = false;
	int opt;

	while ((opt = getopt_long(argc, argv, "au", longopts, NULL)) != -1) {
		switch (opt) {
		case 'a':
			autocommit = true;
			break;
		case 'u':
			server.unlink();
			break;
		default:
			usage(argv[0], longopts);
			return EXIT_FAILURE;
		}
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
