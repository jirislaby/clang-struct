// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <sl/sqlite/SQLiteSmart.h>
#include <sl/sqlite/SQLConn.h>

// only to create and init the real structs.db
class SQLConnReal : public SlSqlite::SQLConn {
public:
	SQLConnReal() {}

	bool open(const std::filesystem::path &dbPath) noexcept {
		return SlSqlite::SQLConn::open(dbPath, SlSqlite::CREATE);
	}

private:
	virtual bool createDB() override;
};

namespace ClangStruct {

class SQLConn : public SlSqlite::SQLConn {
public:
	struct Location {
		unsigned begLine;
		unsigned begCol;
		unsigned endLine;
		unsigned endCol;
	};

	struct Struct {
		const std::string &name;
		Location loc;
		const std::string &type;
		const std::string &attrs;
		bool packed;
		bool inMacro;
		const std::string &src;
	};

	struct Ref {
		const std::string &name;
		unsigned line;
		unsigned col;
	};

	struct Member {
		const std::string &name;
		const std::string &src;
		Location loc;
		Ref str;
	};

	struct Use {
		const std::string &src;
		Location loc;
		int load;
		bool implicit;
		const std::string &strSrc;
		Ref str;
		const std::string &member;
	};

	SQLConn() {}

	bool open() noexcept {
#define USE_TEMPORARY 0
#if USE_TEMPORARY
		static const std::filesystem::path tempDBName("");
#else
		static const std::filesystem::path tempDBName("structsTemp" +
							      std::to_string(getpid()) + ".db");
#endif
		return SlSqlite::SQLConn::open(tempDBName, SlSqlite::CREATE);
	}

	void commonBinding(Binding &b, const Location &loc) const {
		b.emplace_back(":begLine", loc.begLine);
		b.emplace_back(":begCol", loc.begCol);
		b.emplace_back(":endLine", loc.endLine);
		b.emplace_back(":endCol", loc.endCol);
	}

	bool insertStructTemp(const struct Struct &str) const {
		Binding b{
			{ ":type", str.type },
			{ ":name", str.name },
			{ ":attrs", str.attrs },
			{ ":packed", str.packed },
			{ ":inMacro", str.inMacro },
			{ ":src", str.src },
			{ ":attrs", str.attrs },
		};
		commonBinding(b, str.loc);
		return insert(insStrTemp, b);
	}
	bool insertMemberTemp(const struct Member &member) const {
		Binding b{
			{ ":name", member.name },
			{ ":src", member.src },
			{ ":str", member.str.name },
			{ ":strBegLine", member.str.line },
			{ ":strBegCol", member.str.col },
		};
		commonBinding(b, member.loc);
		return insert(insMemTemp, b);
	}
	bool insertUseTemp(const struct Use &use) const {
		Binding b{
			{ ":src", use.src },
			{ ":load", valueOrNull(use.load >= 0, use.load) },
			{ ":implicit", use.implicit },
			{ ":strSrc", use.strSrc },
			{ ":str", use.str.name },
			{ ":strBegLine", use.str.line },
			{ ":strBegCol", use.str.col },
			{ ":member", use.member },
		};
		commonBinding(b, use.loc);
		return insert(insUseTemp, b);
	}

	bool move(const std::filesystem::path &dbPath, int runId) {
		if (!openReal(dbPath))
			return false;
		const auto run = valueOrNull(runId >= 0, runId);
		const auto &tx = beginAuto();
		return tx &&	insert(moveSrc, { { ":run", run } }) &&
				insert(moveStr, { { ":run", run } }) &&
				insert(moveMem, { { ":run", run } }) &&
				insert(moveUse, { { ":run", run } });
	}

private:
	virtual bool createDB() override;
	virtual bool prepDB() override;

	bool openReal(const std::filesystem::path &dbPath) {
		{
			SQLConnReal real;
			if (!real.open(dbPath)) {
				m_lastError.reset() << real.lastError();
				return false;
			}
		}
		return attach(dbPath, "real") && prepReal();
	}
	bool prepReal();

	SlSqlite::SQLStmtHolder insStrTemp;
	SlSqlite::SQLStmtHolder insMemTemp;
	SlSqlite::SQLStmtHolder insUseTemp;
	SlSqlite::SQLStmtHolder moveSrc;
	SlSqlite::SQLStmtHolder moveStr;
	SlSqlite::SQLStmtHolder moveMem;
	SlSqlite::SQLStmtHolder moveUse;
};

}
