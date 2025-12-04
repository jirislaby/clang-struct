// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <csignal>
#include <optional>
#include <memory>
#include <string_view>

#include <mqueue.h>

class Server {
public:
	Server() {}
	~Server();

	int open();
	void close();

	static void unlink();

	std::optional<std::string_view> read();
private:
#if 0
	int sock = -1;
#else
	mqd_t mq = -1;
#endif
	std::unique_ptr<char[]> buf;
	unsigned buf_len;
	volatile std::sig_atomic_t stop;

	static const char queue_name[];
};
