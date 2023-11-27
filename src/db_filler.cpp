#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <mqueue.h>
#include <unistd.h>

//#include <sys/socket.h>
//#include <sys/un.h>
#include <sys/stat.h>

#include <sqlite3.h>
#include <vector>

#include "sqlite.h"
#include "Message.h"

using Msg = Message<std::string_view>;

volatile std::sig_atomic_t stop;

class Server {
public:
	Server() {}
	~Server();

	int open();
	void close();

	static void unlink();

	std::string_view read();
private:
#if 0
	int sock = -1;
#else
	mqd_t mq = -1;
#endif
	std::unique_ptr<char[]> buf;
	unsigned buf_len;

	static const char queue_name[];
};

const char Server::queue_name[] = "/db_filler";

Server::~Server()
{
	close();
}

std::string_view Server::read()
{
	auto rd = mq_receive(mq, buf.get(), buf_len, NULL);
	if (rd < 0) {
		if (!stop)
			std::cerr << "cannot read: " << strerror(errno) << "\n";
		return "";
	}

	return std::string_view(buf.get(), rd);
}

void Server::close()
{
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

static Server server;


static int busy_handler(void *data, int count)
{
	static const auto WAIT_INTVAL = std::chrono::milliseconds(20);
	static const auto WAIT_TIMEOUT = std::chrono::minutes(20) / WAIT_INTVAL;

	if (count >= WAIT_TIMEOUT)
		return 0;

	std::this_thread::sleep_for(WAIT_INTVAL);

	return 1;
}

class SQLConn {
public:
	SQLConn() {}

	int openDB();
	int prepDB();

	int begin();
	int end();

	int handleMessage(const Msg &msg);
private:
	int bindAndStep(SQLStmtHolder &ins, const Msg &msg);

	SQLHolder sqlHolder;
	SQLStmtHolder insSrc;
	SQLStmtHolder insStr;
	SQLStmtHolder insMem;
	SQLStmtHolder insUse;
};

int SQLConn::openDB()
{
	sqlite3 *sql;
	char *err;
	int ret;

	ret = sqlite3_open_v2("structs.db", &sql, SQLITE_OPEN_READWRITE |
			      SQLITE_OPEN_CREATE, NULL);
	sqlHolder.reset(sql);
	if (ret != SQLITE_OK) {
		std::cerr << "db open failed: " << sqlite3_errstr(ret) << "\n";
		return -1;
	}

	ret = sqlite3_exec(sqlHolder, "PRAGMA foreign_keys = ON;", NULL, NULL,
			   &err);
	if (ret != SQLITE_OK) {
		std::cerr << "db PRAGMA failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return -1;
	}

	ret = sqlite3_busy_handler(sqlHolder, busy_handler, nullptr);
	if (ret != SQLITE_OK) {
		std::cerr << "db busy_handler failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return -1;
	}

	static const std::vector<const char *> create_tables {
		"source(id INTEGER PRIMARY KEY, "
			"src TEXT NOT NULL UNIQUE)",
		"struct(id INTEGER PRIMARY KEY, "
			"name TEXT NOT NULL, "
			"attrs TEXT, "
			"src INTEGER NOT NULL REFERENCES source(id), "
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL, "
			"endLine INTEGER, endCol INTEGER, "
			"UNIQUE(name, src, begLine, begCol))",
		"member(id INTEGER PRIMARY KEY, "
			"name TEXT NOT NULL, "
			"struct INTEGER NOT NULL REFERENCES struct(id), "
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL, "
			"endLine INTEGER, endCol INTEGER, "
			"UNIQUE(struct, name))",
		"use(id INTEGER PRIMARY KEY, "
			"member INTEGER NOT NULL REFERENCES member(id), "
			"src INTEGER NOT NULL REFERENCES source(id), "
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL, "
			"endLine INTEGER, endCol INTEGER, "
			"load INTEGER, "
			"UNIQUE(member, src, begLine))",
	};

	for (auto c: create_tables) {
		std::string s("CREATE TABLE IF NOT EXISTS ");
		s.append(c).append(" STRICT;");
		ret = sqlite3_exec(sqlHolder, s.c_str(), NULL, NULL, &err);
		if (ret != SQLITE_OK) {
			std::cerr << "db CREATE failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					err << "\n\t" << s << "\n";
			sqlite3_free(err);
			return -1;
		}
	}

	static const std::vector<const char *> create_views {
		"structs_view AS "
			"SELECT struct.id, struct.name AS struct, struct.attrs, source.src, "
				"struct.begLine || ':' || struct.begCol || '-' || "
				"struct.endLine || ':' || struct.endCol AS location "
			"FROM struct LEFT JOIN source ON struct.src=source.id",
		"members_view AS "
			"SELECT member.id, struct.name AS struct, "
				"member.name AS member, source.src, "
				"member.begLine || ':' || member.begCol || '-' || "
				"member.endLine || ':' || member.endCol AS location "
			"FROM member "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON struct.src=source.id",
		"use_view AS "
			"SELECT use.id, struct.name AS struct, "
				"member.name AS member, source.src, "
				"use.begLine || ':' || use.begCol || '-' || "
				"use.endLine || ':' || use.endCol AS location "
			"FROM use "
			"LEFT JOIN member ON use.member=member.id "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON use.src=source.id",
		"unused_view AS "
			"SELECT struct.name AS struct, struct.attrs, member.name AS member, "
				"source.src, "
				"member.begLine || ':' || member.begCol || '-' || "
				"member.endLine || ':' || member.endCol AS location "
			"FROM member "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON struct.src=source.id "
			"WHERE member.id NOT IN (SELECT member FROM use) "
				"AND struct.name != '<anonymous>' AND struct.name != '<unnamed>' "
				"AND member.name != '<unnamed>'",
	};

	for (auto c: create_views) {
		std::string s("CREATE VIEW IF NOT EXISTS ");
		s.append(c);
		ret = sqlite3_exec(sqlHolder, s.c_str(), NULL, NULL, &err);
		if (ret != SQLITE_OK) {
			std::cerr << "db CREATE failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					err << "\n\t" << s << "\n";
			sqlite3_free(err);
			return -1;
		}
	}

	return 0;
}


int SQLConn::prepDB()
{
	sqlite3_stmt *stmt;
	int ret;

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT OR IGNORE INTO source(src) "
				 "VALUES (:src);",
				 -1, &stmt, NULL);
	insSrc.reset(stmt);
	if (ret != SQLITE_OK) {
		std::cerr << "db prepare failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " <<
			     sqlite3_errmsg(sqlHolder) << "\n";
		return -1;
	}

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT OR IGNORE INTO "
				 "struct(name, attrs, src, begLine, begCol, endLine, endCol) "
				 "SELECT :name, :attrs, id, :begLine, :begCol, :endLine, :endCol "
					"FROM source WHERE src=:src;",
				 -1, &stmt, NULL);
	insStr.reset(stmt);
	if (ret != SQLITE_OK) {
		std::cerr << "db prepare failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " <<
			     sqlite3_errmsg(sqlHolder) << "\n";
		return -1;
	}

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT OR IGNORE INTO "
				 "member(name, struct, begLine, begCol, endLine, endCol) "
				 "SELECT :name, struct.id, :begLine, :begCol, :endLine, :endCol "
					"FROM struct LEFT JOIN source ON struct.src=source.id "
					"WHERE source.src=:src AND "
					"begLine=:strBegLine AND begCol=:strBegCol AND "
					"name=:struct;",
				 -1, &stmt, NULL);
	insMem.reset(stmt);
	if (ret != SQLITE_OK) {
		std::cerr << "db prepare failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " <<
			     sqlite3_errmsg(sqlHolder) << "\n";
		return -1;
	}

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT OR IGNORE INTO "
				 "use(member, src, begLine, begCol, endLine, endCol) "
				 "SELECT (SELECT member.id FROM member "
						"LEFT JOIN struct ON member.struct=struct.id "
						"LEFT JOIN source ON struct.src=source.id "
						"WHERE member.name=:member AND "
						"struct.name=:struct AND "
						"source.src=:strSrc AND "
						"struct.begLine=:strLine AND "
						"struct.begCol=:strCol), "
					"(SELECT id FROM source WHERE src=:use_src), "
					":begLine, :begCol, :endLine, :endCol;",
				 -1, &stmt, NULL);
	insUse.reset(stmt);
	if (ret != SQLITE_OK) {
		std::cerr << "db prepare failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " <<
			     sqlite3_errmsg(sqlHolder) << "\n";
		return -1;
	}

	return 0;
}

int SQLConn::begin()
{
	char *err;
	int ret = sqlite3_exec(sqlHolder, "BEGIN;", NULL, NULL, &err);
	if (ret != SQLITE_OK) {
		std::cerr << "db BEGIN failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return -1;
	}

	return 0;
}

int SQLConn::end()
{
	char *err;
	int ret = sqlite3_exec(sqlHolder, "END;", NULL, NULL, &err);
	if (ret != SQLITE_OK) {
		std::cerr << "db BEGIN failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return -1;
	}

	return 0;
}

int SQLConn::bindAndStep(SQLStmtHolder &ins, const Msg &msg)
{
	SQLStmtResetter insSrcResetter(sqlHolder, ins);
	int ret;

	for (auto e: msg) {
		const auto [type, key, val] = e;

		std::string bind(":");
		bind.append(key);
		auto bindIdx = sqlite3_bind_parameter_index(ins, bind.c_str());
		if (!bindIdx) {
			std::cerr << "no index found for key=" << key << "\n";
			std::cerr << "\t" << msg << "\n";
			return -1;
		}

		if (type == Msg::TYPE::TEXT) {
			ret = sqlite3_bind_text(ins, bindIdx, val.data(), val.length(), SQLITE_TRANSIENT);
		} else if (type == Msg::TYPE::INT) {
			try {
				auto i = std::stoi(std::string(val));
				ret = sqlite3_bind_int(ins, bindIdx, i);
			} catch (std::invalid_argument const& ex) {
				std::cerr << "bad int val=\"" << val << "\"\n";
				return -1;
			}
		} else {
			std::cerr << "bad type: " << msg << "\n";
			abort();
		}

		if (ret != SQLITE_OK) {
			std::cerr << "db bind failed (" << __LINE__ << "/" << key << "/" <<
				     val << "): " <<
				     sqlite3_errstr(ret) << " -> " <<
				     sqlite3_errmsg(sqlHolder) << "\n";
			return -1;
		}
	}

	ret = sqlite3_step(ins);
	if (ret != SQLITE_DONE) {
		std::cerr << "db step failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " <<
			     sqlite3_errmsg(sqlHolder) << "\n";
		std::cerr << "\t" << msg << "\n";
		return -1;
	}

	return 0;
}

int SQLConn::handleMessage(const Msg &msg)
{
	auto kind = msg.getKind();

	if (kind == Msg::KIND::SOURCE)
		return bindAndStep(insSrc, msg);
	if (kind == Msg::KIND::STRUCT)
		return bindAndStep(insStr, msg);
	if (kind == Msg::KIND::MEMBER)
		return bindAndStep(insMem, msg);
	if (kind == Msg::KIND::USE)
		return bindAndStep(insUse, msg);

	std::cerr << "bad message kind: " << kind << "\n";
	std::cerr << "\t" << msg << "\n";

	return -1;
}

SQLConn sqlConn;

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

	if (sqlConn.openDB() < 0)
		return EXIT_FAILURE;
	if (sqlConn.prepDB() < 0)
		return EXIT_FAILURE;
	if (!autocommit && sqlConn.begin() < 0)
		return EXIT_FAILURE;

	Msg msg;

	while (true) {
		auto msgStr = server.read();
		if (stop || msgStr.empty())
			break;

		msg.deserialize(msgStr);

		//std::cerr << "===" << msg << "\n";

		sqlConn.handleMessage(msg);
	}

	if (!autocommit) {
		std::cerr << "commiting\n";
		if (sqlConn.end() < 0)
			return EXIT_FAILURE;
	}
	std::cerr << "bye\n";

	return 0;
}
