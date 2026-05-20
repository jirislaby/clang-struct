// SPDX-License-Identifier: GPL-2.0-only

#include <cerrno>
#include <cstring>
#include <iostream>

#include "server.h"

using namespace ClangStruct;

const constexpr std::string_view Server::queue_name { "/db_filler" };

Server::~Server()
{
	close();
}

std::optional<std::string_view> Server::read()
{
	struct timespec timeout = {
		.tv_sec = time(NULL) + 5,
	};
	auto rd = ::mq_timedreceive(mq, buf.data(), buf.size(), NULL, &timeout);
	if (rd < 0) {
		if (errno == ETIMEDOUT)
			return "";
		if (!stop)
			std::cerr << "cannot read: " << strerror(errno) << "\n";
		return std::nullopt;
	}

	return std::string_view(buf.data(), rd);
}

void Server::close()
{
	stop = true;
	if (mq >= 0) {
		unlink();
		::mq_close(mq);
		mq = -1;
	}
}

void Server::unlink()
{
	::mq_unlink(queue_name.data());
}

int Server::open()
{
	mq = ::mq_open(queue_name.data(), O_CREAT | O_EXCL | O_RDONLY, 0600,
		     nullptr);
	if (mq < 0) {
		std::cerr << "cannot open msg queue: " << strerror(errno) << "\n";
		return -1;
	}

	mq_attr attr;
	if (::mq_getattr(mq, &attr) < 0) {
		std::cerr << "cannot get msg attr: " << strerror(errno) << "\n";
		return -1;
	}

	buf.resize(attr.mq_msgsize);

	return 0;
}
