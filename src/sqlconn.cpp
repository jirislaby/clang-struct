#include <charconv>
#include <chrono>
#include <thread>
#include <vector>

#include "sqlconn.h"

static int busy_handler(void *data, int count)
{
	static const auto WAIT_INTVAL = std::chrono::milliseconds(20);
	static const auto WAIT_TIMEOUT = std::chrono::minutes(20) / WAIT_INTVAL;

	if (count >= WAIT_TIMEOUT)
		return 0;

	std::this_thread::sleep_for(WAIT_INTVAL);

	return 1;
}

template <typename T>
int SQLConn<T>::openDB()
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
		"run(id INTEGER PRIMARY KEY, "
			"version TEXT, "
			"sha TEXT, "
			"filter TEXT, "
			"skip INTEGER NOT NULL CHECK(skip IN (0, 1)), "
			"timestamp TEXT NOT NULL DEFAULT (STRFTIME('%Y-%m-%d %H:%M:%f', 'NOW', 'localtime')))",
		"source(id INTEGER PRIMARY KEY, "
			"run INTEGER REFERENCES run(id), "
			"src TEXT NOT NULL UNIQUE)",
		"struct(id INTEGER PRIMARY KEY, "
			"run INTEGER REFERENCES run(id), "
			"parent INTEGER REFERENCES struct(id) ON DELETE CASCADE, "
			"type TEXT NOT NULL CHECK(type IN ('s', 'u')), "
			"name TEXT NOT NULL, "
			"attrs TEXT, "
			"packed INTEGER NOT NULL CHECK(packed IN (0, 1)), "
			"inMacro INTEGER NOT NULL CHECK(inMacro IN (0, 1)), "
			"src INTEGER NOT NULL REFERENCES source(id) ON DELETE CASCADE, "
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL, "
			"endLine INTEGER, endCol INTEGER, "
			"UNIQUE(name, src, begLine, begCol))",
		"member(id INTEGER PRIMARY KEY, "
			"run INTEGER REFERENCES run(id), "
			"name TEXT NOT NULL, "
			"struct INTEGER NOT NULL REFERENCES struct(id) ON DELETE CASCADE, "
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL, "
			"endLine INTEGER, endCol INTEGER, "
			"uses INTEGER NOT NULL DEFAULT 0, "
			"loads INTEGER NOT NULL DEFAULT 0, "
			"stores INTEGER NOT NULL DEFAULT 0, "
			"implicit_uses INTEGER NOT NULL DEFAULT 0, "
			"UNIQUE(struct, name, begLine, begCol), "
			"CHECK(endLine >= begLine), "
			"CHECK(uses >= loads + stores), "
			"CHECK(uses >= implicit_uses))",
		"use(id INTEGER PRIMARY KEY, "
			"run INTEGER REFERENCES run(id), "
			"member INTEGER NOT NULL REFERENCES member(id) ON DELETE CASCADE, "
			"src INTEGER NOT NULL REFERENCES source(id) ON DELETE CASCADE, "
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL, "
			"endLine INTEGER, endCol INTEGER, "
			"load INTEGER CHECK(load IN (0, 1)), "
			"implicit INTEGER NOT NULL CHECK(implicit IN (0, 1)), "
			"UNIQUE(member, src, begLine), "
			"CHECK(endLine >= begLine))"
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

	static const std::vector<std::pair<const char *, const char *>> create_triggers {
		{ "TRIG_use_A_INS AFTER INSERT ON use", "UPDATE member SET uses = uses+1, "
			"loads = loads + (NEW.load IS 1), "
			"stores = stores + (NEW.load IS 0), "
			"implicit_uses = implicit_uses + (NEW.implicit == 1) "
			"WHERE id = NEW.member" },
	};

	for (auto c: create_triggers) {
		std::string s("CREATE TRIGGER IF NOT EXISTS ");
		s.append(c.first).append(" FOR EACH ROW BEGIN ").append(c.second).append("; END;");
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
		"struct_view AS "
			"SELECT struct.id, struct.run AS run, type, "
				"struct.name AS struct, attrs, packed, inMacro, source.src, "
				"struct.begLine || ':' || struct.begCol || '-' || "
				"struct.endLine || ':' || struct.endCol AS location "
			"FROM struct LEFT JOIN source ON struct.src=source.id",
		"member_view AS "
			"SELECT member.id, member.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"member.begLine || ':' || member.begCol || '-' || "
				"member.endLine || ':' || member.endCol AS location, "
				"uses, loads, stores, implicit_uses "
			"FROM member "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON struct.src=source.id",
		"use_view AS "
			"SELECT use.id, use.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"use.begLine || ':' || use.begCol || '-' || "
				"use.endLine || ':' || use.endCol AS location, load, implicit "
			"FROM use "
			"LEFT JOIN member ON use.member=member.id "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON use.src=source.id",
		"unused_view AS "
			"SELECT member.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
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


template <typename T>
int SQLConn<T>::prepDB()
{
	sqlite3_stmt *stmt;
	int ret;

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT INTO source(run, src) "
				 "VALUES (:run, :src);",
				 -1, &stmt, NULL);
	insSrc.reset(stmt);
	if (ret != SQLITE_OK) {
		std::cerr << "db prepare failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " <<
			     sqlite3_errmsg(sqlHolder) << "\n";
		return -1;
	}

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT INTO "
				 "struct(run, type, name, attrs, packed, inMacro, src, begLine, begCol, endLine, endCol) "
				 "SELECT :run, :type, :name, :attrs, :packed, :inMacro, id, :begLine, :begCol, :endLine, :endCol "
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
				 "INSERT INTO "
				 "member(run, name, struct, begLine, begCol, endLine, endCol) "
				 "SELECT :run, :name, struct.id, :begLine, :begCol, :endLine, :endCol "
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
				 "INSERT INTO "
				 "use(run, member, src, begLine, begCol, endLine, endCol, load, implicit) "
				 "SELECT :run, (SELECT member.id FROM member "
						"LEFT JOIN struct ON member.struct=struct.id "
						"LEFT JOIN source ON struct.src=source.id "
						"WHERE member.name=:member AND "
						"struct.name=:struct AND "
						"source.src=:strSrc AND "
						"struct.begLine=:strLine AND "
						"struct.begCol=:strCol), "
					"(SELECT id FROM source WHERE src=:use_src), "
					":begLine, :begCol, :endLine, :endCol, :load, :implicit;",
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

template <typename T>
int SQLConn<T>::open()
{
	if (openDB() < 0)
		return -1;
	if (prepDB() < 0)
		return -1;

	return 0;
}

template <typename T>
int SQLConn<T>::begin()
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

template <typename T>
int SQLConn<T>::end()
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

template <typename T>
int SQLConn<T>::bindAndStep(SQLStmtHolder &ins, const Msg &msg)
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
			auto end = val.data() + val.size();
			int i;
			auto res = std::from_chars(val.data(), end, i);
			if (res.ptr != end) {
				std::cerr << "bad int val=\"" << val << "\"\n";
				return -1;
			}
			ret = sqlite3_bind_int(ins, bindIdx, i);
		} else if (type == Msg::TYPE::NUL) {
			ret = sqlite3_bind_null(ins, bindIdx);
		} else {
			std::cerr << "bad type: " << msg << "\n";
			abort();
		}

		if (ret != SQLITE_OK) {
			std::cerr << "db bind failed (" << __LINE__ << " key=\"" << key <<
				     "\" val=\"" << val << "\"): " <<
				     sqlite3_errstr(ret) << " -> " <<
				     sqlite3_errmsg(sqlHolder) << "\n";
			return -1;
		}
	}

	ret = sqlite3_step(ins);
	if (ret != SQLITE_DONE && sqlite3_extended_errcode(sqlHolder) != SQLITE_CONSTRAINT_UNIQUE) {
		std::cerr << "db step failed (" << __LINE__ << "): " <<
			     sqlite3_errstr(ret) << " -> " <<
			     sqlite3_errmsg(sqlHolder) << "\n";
		std::cerr << "\t" << msg << "\n";
		return -1;
	}

	return 0;
}

template <typename T>
int SQLConn<T>::handleMessage(const Msg &msg)
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

#ifdef STANDALONE
template class SQLConn<std::string>;
#else
template class SQLConn<std::string_view>;
#endif
