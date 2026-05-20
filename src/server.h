// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <csignal>
#include <optional>
#include <string>
#include <string_view>

#include <mqueue.h>

namespace ClangStruct {

class Server {
public:
	Server() {}
	~Server();

	void open();
	void close();

	static void unlink();

	std::optional<std::string_view> read();
private:
	mqd_t mq = -1;
	std::string buf;
	volatile std::sig_atomic_t stop;

	static const std::string_view queue_name;
};

}
