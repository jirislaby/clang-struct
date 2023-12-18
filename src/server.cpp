#include <cerrno>
#include <cstring>
#include <iostream>

#include "server.h"

const char Server::queue_name[] = "/db_filler";

Server::~Server()
{
	close();
}

std::optional<std::string_view> Server::read()
{
	struct timespec timeout = {
		.tv_sec = time(NULL) + 5,
	};
	auto rd = mq_timedreceive(mq, buf.get(), buf_len, NULL, &timeout);
	if (rd < 0) {
		if (errno == ETIMEDOUT)
			return "";
		if (!stop)
			std::cerr << "cannot read: " << strerror(errno) << "\n";
		return std::nullopt;
	}

	return std::string_view(buf.get(), rd);
}

void Server::close()
{
	stop = true;
#if 0
	if (sock >= 0) {
		::close(sock);
		sock = -1;
	}
#else
	if (mq >= 0) {
		mq_unlink(queue_name);
		mq_close(mq);
		mq = -1;
	}
#endif
}

void Server::unlink()
{
	mq_unlink(queue_name);
}

int Server::open()
{
#if 0
	sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (sock < 0) {
		std::cerr << "cannot create socket: " << strerror(errno) << "\n";
		return -1;
	}

	int one = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
		std::cerr << "cannot setsockopt: " << strerror(errno) << "\n";
		return -1;
	}

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = "db_filler",
	};

	int ret = bind(sock, (const struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		std::cerr << "cannot bind: " << strerror(errno) << "\n";
		return -1;
	}

	ret = listen(sock, 20);
	if (ret < 0) {
		std::cerr << "cannot listen: " << strerror(errno) << "\n";
		return -1;
	}

	while (true) {
		int data_socket = accept(sock, NULL, NULL);
		if (data_socket < 0) {
			std::cerr << "cannot listen: " << strerror(errno) << "\n";
			return -1;
		}

		while (true) {
			char buf[128];
			ssize_t rd = ::read(data_socket, buf, sizeof(buf));
			if (!rd)
				break;
			if (rd < 0) {
				std::cerr << "bad read: " << strerror(errno) << "\n";
				break;
			}
			write(STDOUT_FILENO, buf, rd);
		}

		::close(data_socket);
	}
#else
	mq = mq_open(queue_name, O_CREAT | O_EXCL | O_RDONLY,  0600, NULL);
	if (mq < 0) {
		std::cerr << "cannot open msg queue: " << strerror(errno) << "\n";
		return -1;
	}

	mq_attr attr;
	if (mq_getattr(mq, &attr) < 0) {
		std::cerr << "cannot get msg attr: " << strerror(errno) << "\n";
		return -1;
	}

	buf_len = attr.mq_msgsize;
	buf = std::make_unique<char[]>(buf_len);
#endif

	return 0;
}
