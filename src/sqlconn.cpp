// SPDX-License-Identifier: GPL-2.0-only

#include "sqlconn.h"

using namespace ClangStruct;

bool SQLConn::createDB()
{
	static const Tables tables {
		{ "structTemp", {
			"type TEXT NOT NULL CHECK(type IN ('s', 'u'))",
			"name TEXT NOT NULL",
			"attrs TEXT",
			"packed INTEGER NOT NULL CHECK(packed IN (0, 1))",
			"inMacro INTEGER NOT NULL CHECK(inMacro IN (0, 1))",
			"src TEXT NOT NULL",
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL",
			"endLine INTEGER, endCol INTEGER",
		}},
		{ "memberTemp", {
			"name TEXT NOT NULL",
			"src TEXT NOT NULL",
			"struct TEXT NOT NULL",
			"strBegLine INTEGER NOT NULL, strBegCol INTEGER NOT NULL",
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL",
			"endLine INTEGER, endCol INTEGER",
		}},
		{ "useTemp", {
			"src TEXT NOT NULL",
			"load INTEGER CHECK(load IN (0, 1))",
			"implicit INTEGER NOT NULL CHECK(implicit IN (0, 1))",
			"strSrc TEXT NOT NULL",
			"struct TEXT NOT NULL",
			"strBegLine INTEGER NOT NULL, strBegCol INTEGER NOT NULL",
			"member TEXT NOT NULL",
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL",
			"endLine INTEGER, endCol INTEGER",
		}},
	};
	return createTables(tables);
}

bool SQLConn::prepDB()
{
	const Statements stmts {
		{ insStrTemp, "INSERT INTO structTemp(type, name, attrs, packed, inMacro, src, "
				"begLine, begCol, endLine, endCol) "
			      "VALUES (:type, :name, :attrs, :packed, :inMacro, :src, "
				":begLine, :begCol, :endLine, :endCol);" },
		{ insMemTemp, "INSERT INTO memberTemp(name, src, struct, strBegLine, strBegCol, "
				"begLine, begCol, endLine, endCol) "
			      "VALUES (:name, :src, :str, :strBegLine, :strBegCol, "
				":begLine, :begCol, :endLine, :endCol);" },
		{ insUseTemp, "INSERT INTO useTemp(src, load, implicit, strSrc, "
				"struct, strBegLine, strBegCol, member, "
				"begLine, begCol, endLine, endCol) "
			      "VALUES (:src, :load, :implicit, :strSrc, "
				":str, :strBegLine, :strBegCol, :member, "
				":begLine, :begCol, :endLine, :endCol);" },
	};

	return prepareStatements(stmts);
}

bool SQLConn::prepReal()
{
	const Statements stmts {
		{ moveSrc, "INSERT INTO source(run, src) "
			   "SELECT :run, src FROM structTemp "
			   "WHERE true ON CONFLICT DO NOTHING;" },
		{ moveStr, "INSERT INTO struct(run, type, name, attrs, packed, inMacro, src, "
			     "begLine, begCol, endLine, endCol) "
			   "SELECT :run, type, name, attrs, packed, inMacro, source.id, "
			     "begLine, begCol, endLine, endCol "
			     "FROM structTemp "
			     "JOIN source ON structTemp.src = source.src AND source.run IS :run "
			   "WHERE true ON CONFLICT DO NOTHING;" },
		{ moveMem, "INSERT INTO member(run, name, struct, begLine, begCol, endLine, endCol) "
			   "SELECT :run, mem.name, struct.id, mem.begLine, mem.begCol, "
			     "mem.endLine, mem.endCol "
			   "FROM memberTemp AS mem "
			   "JOIN source ON source.run IS :run AND mem.src = source.src "
			   "JOIN struct ON struct.run IS :run AND struct.src = source.id AND "
			     "struct.name = struct AND "
			     "struct.begLine = strBegLine AND struct.begCol = strBegCol "
			   "WHERE true ON CONFLICT DO NOTHING;" },
		{ moveUse, "INSERT INTO use(run, member, src, load, implicit, "
			     "begLine, begCol, endLine, endCol) "
			   "SELECT :run, member.id, source.id, use.load, use.implicit, "
			     "use.begLine, use.begCol, use.endLine, use.endCol "
			   "FROM useTemp AS use "
			   "JOIN source ON source.run IS :run AND use.src = source.src "
			   "JOIN member ON member.run IS :run AND member.name = use.member "
			   "JOIN struct ON struct.run IS :run AND struct.name = use.struct AND "
			     "struct.begLine = use.strBegLine AND struct.begCol = use.strBegCol AND "
			     "struct.src = (SELECT id FROM source WHERE src = use.strSrc) "
			   "WHERE true ON CONFLICT DO NOTHING;" },
	};
	return prepareStatements(stmts);
}

bool SQLConnReal::createDB()
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
			"load INTEGER CHECK(load IN (0, 1))",
			"implicit INTEGER NOT NULL CHECK(implicit IN (0, 1))",
			"begLine INTEGER NOT NULL, begCol INTEGER NOT NULL",
			"endLine INTEGER, endCol INTEGER",
			"UNIQUE(member, src, begLine, begCol)",
			"CHECK(endLine >= begLine)",
		}},
	};

	static const Triggers triggers {
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
