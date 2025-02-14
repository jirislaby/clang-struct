#include <sstream>
#include <vector>

#include <mysqld_error.h>

#include "sqlconn.h"

void joinVec(std::ostringstream &ss, const std::vector<const char *> &vec,
	     const std::string &sep = ", ")
{
	for (auto i = vec.begin(), end = vec.end(); i != end; ++i) {
		ss << *i;
		if (i != end - 1)
			ss << sep;
	}
}

int SQLConn::openDB(const std::string &host, const unsigned int &port)
{
	MYSQL *sql;
	int ret;

	sql = mysql_init(NULL);
	if (!sql) {
		std::cerr << "db init failed\n";
		return -1;
	}

	sqlHolder.reset(sql);

	unsigned int proto = MYSQL_PROTOCOL_TCP;
	if (mysql_optionsv(sqlHolder, MYSQL_OPT_PROTOCOL, &proto)) {
		std::cerr << "db set protocol failed: " << mysql_error(sqlHolder) << "\n";
		return -1;
	}

	if (!mysql_real_connect(sqlHolder, host.c_str(), "clang_struct", "pass", "clang_struct",
				port, nullptr, 0)) {
		std::cerr << "db connection failed: " << mysql_error(sqlHolder) << "\n";
		return -1;
	}

	static const std::vector<std::pair<const char *, std::vector<const char *>>> create_tables {
		{ "run", {
			"id INTEGER UNSIGNED AUTO_INCREMENT PRIMARY KEY",
			"version TEXT",
			"sha VARCHAR(128)",
			"filter TEXT",
			"skip BOOLEAN NOT NULL",
			"timestamp TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP",
		}},
		{ "source", {
			"id INTEGER UNSIGNED AUTO_INCREMENT PRIMARY KEY",
			"run INTEGER UNSIGNED REFERENCES run(id)",
			"src TEXT NOT NULL UNIQUE",
		}},
		{ "struct", {
			"id INTEGER UNSIGNED AUTO_INCREMENT PRIMARY KEY",
			"run INTEGER UNSIGNED REFERENCES run(id)",
			"parent INTEGER UNSIGNED REFERENCES struct(id) ON DELETE CASCADE",
			"type CHAR(1) NOT NULL CHECK(type IN ('s', 'u'))",
			"name TEXT NOT NULL",
			"attrs TEXT",
			"packed BOOLEAN NOT NULL",
			"inMacro BOOLEAN NOT NULL",
			"src INTEGER UNSIGNED NOT NULL REFERENCES source(id) ON DELETE CASCADE",
			"begLine INTEGER UNSIGNED NOT NULL, begCol INTEGER UNSIGNED NOT NULL",
			"endLine INTEGER UNSIGNED, endCol INTEGER UNSIGNED",
			"UNIQUE(name, src, begLine, begCol)",
		}},
		{ "member", {
			"id INTEGER UNSIGNED AUTO_INCREMENT PRIMARY KEY",
			"run INTEGER UNSIGNED REFERENCES run(id)",
			"name TEXT NOT NULL",
			"struct INTEGER UNSIGNED NOT NULL REFERENCES struct(id) ON DELETE CASCADE",
			"begLine INTEGER UNSIGNED NOT NULL, begCol INTEGER UNSIGNED NOT NULL",
			"endLine INTEGER UNSIGNED, endCol INTEGER UNSIGNED",
			"uses INTEGER UNSIGNED NOT NULL DEFAULT 0",
			"loads INTEGER UNSIGNED NOT NULL DEFAULT 0",
			"stores INTEGER UNSIGNED NOT NULL DEFAULT 0",
			"implicit_uses INTEGER UNSIGNED NOT NULL DEFAULT 0",
			"UNIQUE(struct, name, begLine, begCol)",
			"CHECK(endLine >= begLine)",
			"CHECK(uses >= loads + stores)",
			"CHECK(uses >= implicit_uses)",
		}},
		{ "`use`", {
			"id INTEGER UNSIGNED AUTO_INCREMENT PRIMARY KEY",
			"run INTEGER UNSIGNED REFERENCES run(id)",
			"member INTEGER UNSIGNED NOT NULL REFERENCES member(id) ON DELETE CASCADE",
			"src INTEGER UNSIGNED NOT NULL REFERENCES source(id) ON DELETE CASCADE",
			"begLine INTEGER UNSIGNED NOT NULL, begCol INTEGER UNSIGNED NOT NULL",
			"endLine INTEGER UNSIGNED, endCol INTEGER UNSIGNED",
			"`load` BOOLEAN",
			"implicit BOOLEAN NOT NULL",
			"UNIQUE(member, src, begLine)",
			"CHECK(endLine >= begLine)",
		}},
	};

	for (auto c: create_tables) {
		std::ostringstream ss;
		ss << "CREATE TABLE IF NOT EXISTS " << c.first << '(';
		joinVec(ss, c.second);
		ss << ");";
		ret = mysql_query(sqlHolder, ss.str().c_str());
		if (ret) {
			std::cerr << "db CREATE failed (" << __LINE__ << "): " <<
					mysql_error(sqlHolder) << "\n\t" << ss.str() << "\n";
			return -1;
		}
	}

	static const std::vector<std::pair<const char *, const char *>> create_triggers {
		{ "TRIG_use_A_INS AFTER INSERT ON `use`", "UPDATE member SET uses = uses+1, "
			"loads = loads + (NEW.load <=> TRUE), "
			"stores = stores + (NEW.load <=> FALSE), "
			"implicit_uses = implicit_uses + (NEW.implicit IS TRUE) "
			"WHERE id = NEW.member" },
	};

	for (auto c: create_triggers) {
		std::string s("CREATE TRIGGER IF NOT EXISTS ");
		s.append(c.first).append(" FOR EACH ROW ").append(c.second).append(";");
		ret = mysql_query(sqlHolder, s.c_str());
		if (ret) {
			std::cerr << "db CREATE failed (" << __LINE__ << "): " <<
					mysql_error(sqlHolder) << "\n\t" << s << "\n";
			return -1;
		}
	}

	static const std::vector<std::pair<const char *, const char *>> create_views {
		{ "struct_view",
			"SELECT struct.id, struct.run AS run, type, "
				"struct.name AS struct, attrs, packed, inMacro, source.src, "
				"CONCAT(struct.begLine, ':', struct.begCol, '-', "
				"struct.endLine, ':', struct.endCol) AS location "
			"FROM struct LEFT JOIN source ON struct.src=source.id"
		},
		{ "member_view",
			"SELECT member.id, member.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"CONCAT(member.begLine, ':', member.begCol, '-', "
				"member.endLine, ':', member.endCol) AS location, "
				"uses, loads, stores, implicit_uses "
			"FROM member "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON struct.src=source.id"
		},
		{ "use_view",
			"SELECT `use`.id, `use`.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"CONCAT(`use`.begLine, ':', `use`.begCol, '-', "
				"`use`.endLine, ':', `use`.endCol) AS location, `load`, implicit "
			"FROM `use` "
			"LEFT JOIN member ON use.member=member.id "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON use.src=source.id"
		},
		{ "unused_view",
			"SELECT member.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"CONCAT(member.begLine, ':', member.begCol, '-', "
				"member.endLine, ':', member.endCol) AS location "
			"FROM member "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON struct.src=source.id "
			"WHERE member.id NOT IN (SELECT member FROM `use`) "
				"AND struct.name != '<anonymous>' AND struct.name != '<unnamed>' "
				"AND member.name != '<unnamed>'"
		},
	};

	for (auto c: create_views) {
		std::string s("CREATE VIEW IF NOT EXISTS ");
		s.append(c.first).append(" AS ").append(c.second).append(";");
		ret = mysql_query(sqlHolder, s.c_str());
		if (ret) {
			std::cerr << "db CREATE failed (" << __LINE__ << "): " <<
					mysql_error(sqlHolder) << "\n\t" << s << "\n";
			return -1;
		}
	}

	return 0;
}

SQLStmtHolder SQLConn::stmtPrepare(const std::string &query)
{
	SQLStmtHolder s(mysql_stmt_init(sqlHolder));
	if (!s) {
		std::cerr << "db stmt init failed (" << query << "): " <<
			     mysql_error(sqlHolder) << "\n";
		return nullptr;
	}

	if (mysql_stmt_prepare(s, query.c_str(), query.length())) {
		std::cerr << "db prepare failed (" << query << "): " <<
			     mysql_error(sqlHolder) << "\n";
		return nullptr;
	}

	return s;
}

int SQLConn::prepDB()
{
	insSrc = stmtPrepare("INSERT INTO source(run, src) "
			     "VALUES (?, ?);");
	if (!insSrc)
		return -1;

	insStr = stmtPrepare("INSERT INTO "
			     "struct(run, type, name, attrs, packed, inMacro, src, begLine, begCol, endLine, endCol) "
			     "SELECT ?, ?, ?, ?, ?, ?, id, ?, ?, ?, ? "
			     "FROM source WHERE src=?");
	if (!insStr)
		return -1;

	insMem = stmtPrepare("INSERT INTO "
			     "member(run, name, struct, begLine, begCol, endLine, endCol) "
			     "SELECT ?, ?, struct.id, ?, ?, ?, ? "
			     "FROM struct LEFT JOIN source ON struct.src=source.id "
			     "WHERE source.src=? AND "
			     "begLine=? AND begCol=? AND "
			     "name=?;");
	if (!insMem)
		return -1;

	// triggers in mariadb do not support updating the same as nested select table
	insUseMem = stmtPrepare("SELECT member.id FROM member "
				"LEFT JOIN struct ON member.struct=struct.id "
				"LEFT JOIN source ON struct.src=source.id "
				"WHERE member.name=? AND "
				"member.begLine=? AND "
				"member.begCol=? AND "
				"struct.name=? AND "
				"struct.begLine=? AND "
				"struct.begCol=? AND "
				"source.src=? "
				"FOR UPDATE INTO @member_id;");
	if (!insUseMem)
		return -1;

	insUse = stmtPrepare("INSERT INTO "
			     "`use`(run, member, src, begLine, begCol, endLine, endCol, `load`, implicit) "
			     "SELECT ?, @member_id, id, ?, ?, ?, ?, ?, ? FROM source WHERE src=?;");
	if (!insUse)
		return -1;

	return 0;
}

int SQLConn::open(const std::string &host, const unsigned int &port)
{
	if (openDB(host, port) < 0)
		return -1;
	if (prepDB() < 0)
		return -1;

	return 0;
}

int SQLConn::begin()
{
	if (mysql_query(sqlHolder, "BEGIN;")) {
		std::cerr << "db BEGIN failed (" << __LINE__ << "): " <<
			     mysql_error(sqlHolder) << "\n";
		return -1;
	}

	return 0;
}

int SQLConn::end()
{
	if (mysql_query(sqlHolder, "COMMIT;")) {
		std::cerr << "db COMMIT failed (" << __LINE__ << "): " <<
			     mysql_error(sqlHolder) << "\n";
		return -1;
	}

	return 0;
}

int SQLConn::bindAndExec(MYSQL_STMT *stmt, MYSQL_BIND *bind, const int line)
{
	if (mysql_stmt_bind_param(stmt, bind)) {
		std::cerr << "db bind failed (" << line << "): " <<
			     mysql_error(sqlHolder) << "\n";
		return -1;
	}

	for (unsigned i = 0; i < 3; i++) {
		int ret = mysql_stmt_execute(stmt);
		if (!ret)
			return 0;
		if (mysql_stmt_errno(stmt) == ER_DUP_ENTRY)
			return 0;
		if (mysql_stmt_errno(stmt) != ER_LOCK_DEADLOCK)
			break;
	}

	std::cerr << "db exec failed (" << line << "): " <<
		     mysql_error(sqlHolder) << "\n";
	return -1;
}

void SQLConn::addSrc(const Source &src)
{
	unsigned int run = src.runId;
	my_bool run_is_null = src.runId < 0;

	MYSQL_BIND bind[] = {
		{
			.is_null = &run_is_null,
			.buffer = &run,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(src.src.c_str()),
			.buffer_length = src.src.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		},
	};

	bindAndExec(insSrc, bind, __LINE__);
}

void SQLConn::addStruct(const Struct &_struct)
{
	unsigned int run = _struct.runId;
	my_bool run_is_null = _struct.runId < 0;
	unsigned int packed = _struct.packed;
	unsigned int inMacro = _struct.inMacro;

	MYSQL_BIND bind[] = {
		{
			.is_null = &run_is_null,
			.buffer = &run,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(_struct.type.c_str()),
			.buffer_length = _struct.type.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		}, {
			.buffer = const_cast<char *>(_struct.name.c_str()),
			.buffer_length = _struct.name.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		}, {
			.buffer = const_cast<char *>(_struct.attrs.c_str()),
			.buffer_length = _struct.attrs.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		}, {
			.buffer = &packed,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = &inMacro,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<unsigned *>(&_struct.loc.begLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&_struct.loc.begCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&_struct.loc.endLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<unsigned *>( &_struct.loc.endCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(_struct.src.c_str()),
			.buffer_length = _struct.src.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		},
	};

	bindAndExec(insStr, bind, __LINE__);
}

void SQLConn::addMember(const Member &member)
{
	unsigned int run = member.runId;
	my_bool run_is_null = member.runId < 0;

	MYSQL_BIND bind[] = {
		{
			.is_null = &run_is_null,
			.buffer = &run,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(member.name.c_str()),
			.buffer_length = member.name.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		}, {
			.buffer = const_cast<unsigned *>(&member.loc.begLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&member.loc.begCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&member.loc.endLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<unsigned *>(&member.loc.endCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(member.src.c_str()),
			.buffer_length = member.src.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		}, {
			.buffer = const_cast<unsigned *>(&member.strBegLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&member.strBegCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(member._struct.c_str()),
			.buffer_length = member._struct.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		},
	};

	bindAndExec(insMem, bind, __LINE__);
}

void SQLConn::addUse(const Use &use)
{
	unsigned int run = use.runId;
	my_bool run_is_null = use.runId < 0;
	unsigned int load = use.load;
	my_bool load_is_null = use.load < 0;
	unsigned int implicit = use.implicit;

	MYSQL_BIND bindUseMem[] = {
		{
			.buffer = const_cast<char *>(use.member.c_str()),
			.buffer_length = use.member.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		}, {
			.buffer = const_cast<unsigned *>(&use.memLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&use.memCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(use._struct.c_str()),
			.buffer_length = use._struct.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		}, {
			.buffer = const_cast<unsigned *>(&use.strLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&use.strCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(use.strSrc.c_str()),
			.buffer_length = use.strSrc.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		},
	};
	MYSQL_BIND bindUse[] = {
		{
			.is_null = &run_is_null,
			.buffer = &run,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<unsigned *>(&use.loc.begLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&use.loc.begCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer =  const_cast<unsigned *>(&use.loc.endLine),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<unsigned *>(&use.loc.endCol),
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.is_null = &load_is_null,
			.buffer = &load,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = &implicit,
			.buffer_type = MYSQL_TYPE_LONG,
			.is_unsigned = true,
		}, {
			.buffer = const_cast<char *>(use.useSrc.c_str()),
			.buffer_length = use.useSrc.length(),
			.buffer_type = MYSQL_TYPE_STRING,
		},
	};

	if (bindAndExec(insUseMem, bindUseMem, __LINE__))
		std::cerr <<
			     "mem=" << use.member << ", " <<
			     "str=" << use._struct << ", " <<
			     "strSrc=" << use.strSrc << ", " <<
			     "strL=" << use.strLine << ", " <<
			     "strCol=" << use.strCol <<
			     "\n";
	bindAndExec(insUse, bindUse, __LINE__);
}
