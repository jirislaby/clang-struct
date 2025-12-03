#include <charconv>
#include <iostream>

#include "sqlconn.h"

bool SQLConn::createDB()
{
	static const Tables tables {
		{ "run", {
			"id INTEGER PRIMARY KEY",
			"version TEXT",
			"sha TEXT",
			"filter TEXT",
			"skip INTEGER NOT NULL CHECK(skip IN (0, 1))",
			"timestamp TEXT NOT NULL DEFAULT (STRFTIME('%Y-%m-%d %H:%M:%f', 'NOW', 'localtime'))",
		}},
		{ "source", {
			"id INTEGER PRIMARY KEY",
			"run INTEGER REFERENCES run(id) ON DELETE CASCADE",
			"src TEXT NOT NULL UNIQUE",
		}},
		{ "struct", {
			"id INTEGER PRIMARY KEY",
			"run INTEGER REFERENCES run(id) ON DELETE CASCADE",
			"parent INTEGER REFERENCES struct(id) ON DELETE CASCADE",
			"type TEXT NOT NULL CHECK(type IN ('s', 'u'))",
			"name TEXT NOT NULL",
			"attrs TEXT",
			"packed INTEGER NOT NULL CHECK(packed IN (0, 1))",
			"inMacro INTEGER NOT NULL CHECK(inMacro IN (0, 1))",
			"src INTEGER NOT NULL REFERENCES source(id) ON DELETE CASCADE",
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL",
			"endLine INTEGER, endCol INTEGER",
			"UNIQUE(name, src, begLine, begCol)",
		}},
		{ "member", {
			"id INTEGER PRIMARY KEY",
			"run INTEGER REFERENCES run(id) ON DELETE CASCADE",
			"name TEXT NOT NULL",
			"struct INTEGER NOT NULL REFERENCES struct(id) ON DELETE CASCADE",
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL",
			"endLine INTEGER, endCol INTEGER",
			"uses INTEGER NOT NULL DEFAULT 0",
			"loads INTEGER NOT NULL DEFAULT 0",
			"stores INTEGER NOT NULL DEFAULT 0",
			"implicit_uses INTEGER NOT NULL DEFAULT 0",
			"UNIQUE(struct, name, begLine, begCol)",
			"CHECK(endLine >= begLine)",
			"CHECK(uses >= loads + stores)",
			"CHECK(uses >= implicit_uses)",
		}},
		{ "use", {
			"id INTEGER PRIMARY KEY",
			"run INTEGER REFERENCES run(id) ON DELETE CASCADE",
			"member INTEGER NOT NULL REFERENCES member(id) ON DELETE CASCADE",
			"src INTEGER NOT NULL REFERENCES source(id) ON DELETE CASCADE",
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL",
			"endLine INTEGER, endCol INTEGER",
			"load INTEGER CHECK(load IN (0, 1))",
			"implicit INTEGER NOT NULL CHECK(implicit IN (0, 1))",
			"UNIQUE(member, src, begLine)",
			"CHECK(endLine >= begLine)",
		}},
	};

	static const Triggers triggers {
		{ "TRIG_use_A_INS AFTER INSERT ON use", "UPDATE member SET uses = uses+1, "
			"loads = loads + (NEW.load IS 1), "
			"stores = stores + (NEW.load IS 0), "
			"implicit_uses = implicit_uses + (NEW.implicit == 1) "
			"WHERE id = NEW.member" },
	};

	static const Views views {
		{ "struct_view",
			"SELECT struct.id, struct.run AS run, type, "
				"struct.name AS struct, attrs, packed, inMacro, source.src, "
				"struct.begLine || ':' || struct.begCol || '-' || "
				"struct.endLine || ':' || struct.endCol AS location "
			"FROM struct LEFT JOIN source ON struct.src=source.id"
		},
		{ "member_view",
			"SELECT member.id, member.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"member.begLine || ':' || member.begCol || '-' || "
				"member.endLine || ':' || member.endCol AS location, "
				"uses, loads, stores, implicit_uses "
			"FROM member "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON struct.src=source.id"
		},
		{ "use_view",
			"SELECT use.id, use.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"use.begLine || ':' || use.begCol || '-' || "
				"use.endLine || ':' || use.endCol AS location, load, implicit "
			"FROM use "
			"LEFT JOIN member ON use.member=member.id "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON use.src=source.id"
		},
		{ "unused_view",
			"SELECT member.run AS run, struct.name AS struct, struct.attrs, "
				"member.name AS member, source.src, "
				"member.begLine || ':' || member.begCol || '-' || "
				"member.endLine || ':' || member.endCol AS location "
			"FROM member "
			"LEFT JOIN struct ON member.struct=struct.id "
			"LEFT JOIN source ON struct.src=source.id "
			"WHERE member.id NOT IN (SELECT member FROM use) "
				"AND struct.name != '<anonymous>' AND struct.name != '<unnamed>' "
				"AND member.name != '<unnamed>'"
		},
	};

	return createTables(tables) && createTriggers(triggers) && createViews(views);
}

bool SQLConn::prepDB()
{
	const Statements stmts {
		{ insSrc, "INSERT INTO source(run, src) VALUES (:run, :src);" },
		{ insStr, "INSERT INTO "
				"struct(run, type, name, attrs, packed, inMacro, src, begLine, begCol, endLine, endCol) "
				"VALUES (:run, :type, :name, :attrs, :packed, :inMacro, "
				"(SELECT id FROM source WHERE src=:src), "
				":begLine, :begCol, :endLine, :endCol);" },
		{ insMem, "INSERT INTO "
				"member(run, name, struct, begLine, begCol, endLine, endCol) "
				"VALUES (:run, :name, "
				"(SELECT id "
				  "FROM struct "
				  "WHERE src = (SELECT id FROM source WHERE src = :src) AND "
				  "begLine = :strBegLine AND begCol = :strBegCol AND "
				  "name = :struct), "
				":begLine, :begCol, :endLine, :endCol);" },
		{ insUse, "INSERT INTO "
				"use(run, member, src, begLine, begCol, endLine, endCol, load, implicit) "
				"VALUES (:run, "
				  "(SELECT id FROM member "
				    "WHERE name = :member AND "
				    "struct = (SELECT id FROM struct "
					"WHERE name = :struct AND begLine = :strLine AND "
					"begCol = :strCol AND "
					"src = (SELECT id FROM source WHERE src = :strSrc))), "
				  "(SELECT id FROM source WHERE src = :use_src), "
				":begLine, :begCol, :endLine, :endCol, :load, :implicit);" },
	};
	return prepareStatements(stmts);
}

template <typename T>
int SQLConn::bindAndStep(SlSqlite::SQLStmtHolder &ins, const Message<T> &msg)
{
	using Msg = Message<T>;
	SlSqlite::SQLStmtResetter insSrcResetter(sqlHolder, ins);

	for (auto e: msg) {
		const auto [type, key, val] = e;

		std::string bindKey(":");
		bindKey.append(key);

		bool ret;
		if (type == Msg::TYPE::TEXT) {
			ret = bind(ins, bindKey, val, true);
		} else if (type == Msg::TYPE::INT) {
			auto end = val.data() + val.size();
			int i;
			auto res = std::from_chars(val.data(), end, i);
			if (res.ptr != end) {
				std::cerr << "bad int val=\"" << val << "\"\n";
				return -1;
			}
			ret = bind(ins, bindKey, i);
		} else if (type == Msg::TYPE::NUL) {
			ret = bind(ins, bindKey, std::monostate());
		} else {
			std::cerr << "bad type: " << msg << "\n";
			abort();
		}

		if (!ret) {
			std::cerr << lastError() << '\n';
			return -1;
		}
	}

	if (!step(ins)) {
		std::cerr << lastError() << '\n';
		std::cerr << "\t" << msg << "\n";
		return -1;
	}

	return 0;
}

template <typename T>
int SQLConn::handleMessage(const Message<T> &msg)
{
	using Msg = Message<T>;
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
template int SQLConn::handleMessage(const Message<std::string> &msg);
#else
template int SQLConn::handleMessage(const Message<std::string_view> &msg);
#endif
