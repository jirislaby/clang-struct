#include <sqlite3.h>

#include <filesystem>
#include <thread>

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include "sqlite.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

namespace {
class MyChecker final : public Checker<check::EndOfTranslationUnit> {

private:
	SQLHolder openDB() const;

public:
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
				 AnalysisManager &A, BugReporter &BR) const;
};

class MatchCallback : public MatchFinder::MatchCallback {
public:
	MatchCallback(SourceManager &SM, SQLHolder &sqlHolder,
		      SQLStmtHolder &insSrc, SQLStmtHolder &insStr,
		      SQLStmtHolder &insMem, SQLStmtHolder &insUse,
		      std::filesystem::path &basePath) :
		SM(SM), sqlHolder(sqlHolder), insSrc(insSrc), insStr(insStr),
		insMem(insMem), insUse(insUse), basePath(basePath) { }

	void run(const MatchFinder::MatchResult &res);
private:
	int bindLoc(SQLStmtHolder &stmt, const SourceRange &SR);
	std::filesystem::path getSrc(const SourceLocation &SLOC);

	void handleUse(const MemberExpr *ME, const RecordType *ST);
	void handleME(const MemberExpr *ME);
	void handleRD(const RecordDecl *RD);

	static std::string getRDName(const RecordDecl *RD);

	SourceManager &SM;

	SQLHolder &sqlHolder;
	SQLStmtHolder &insSrc;
	SQLStmtHolder &insStr;
	SQLStmtHolder &insMem;
	SQLStmtHolder &insUse;
	std::filesystem::path &basePath;
};

}

int MatchCallback::bindLoc(SQLStmtHolder &stmt, const SourceRange &SR)
{
	int ret;

	ret = sqlite3_bind_int(stmt,
			       sqlite3_bind_parameter_index(stmt, ":begLine"),
			       SM.getPresumedLineNumber(SR.getBegin()));
	if (ret != SQLITE_OK)
		return ret;
	ret = sqlite3_bind_int(stmt,
			       sqlite3_bind_parameter_index(stmt, ":begCol"),
			       SM.getPresumedColumnNumber(SR.getBegin()));
	if (ret != SQLITE_OK)
		return ret;
	ret = sqlite3_bind_int(stmt,
			       sqlite3_bind_parameter_index(stmt, ":endLine"),
			       SM.getPresumedLineNumber(SR.getEnd()));
	if (ret != SQLITE_OK)
		return ret;
	return sqlite3_bind_int(stmt,
				sqlite3_bind_parameter_index(stmt, ":endCol"),
				SM.getPresumedColumnNumber(SR.getEnd()));
}

std::filesystem::path MatchCallback::getSrc(const SourceLocation &SLOC)
{
	auto src = SM.getPresumedLoc(SLOC).getFilename();

	auto ret = std::filesystem::canonical(src);

	if (!basePath.empty())
		ret = std::filesystem::relative(ret, basePath);

	return ret;
}

void MatchCallback::handleUse(const MemberExpr *ME, const RecordType *ST)
{
	auto strSrc = getSrc(ST->getDecl()->getBeginLoc());
	auto useSrc = getSrc(ME->getBeginLoc());
	int ret;

	SQLStmtResetter insSrcResetter(sqlHolder, insSrc);
	ret = sqlite3_bind_text(insSrc,
				sqlite3_bind_parameter_index(insSrc, ":src"),
				useSrc.c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_step(insSrc);
	if (ret != SQLITE_DONE) {
		llvm::errs() << "db step failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}

	/*
			":begLine, :begCol, :endLine, :endCol;",*/
	SQLStmtResetter insUseResetter(sqlHolder, insUse);
	ret = sqlite3_bind_text(insUse,
				sqlite3_bind_parameter_index(insUse, ":member"),
				ME->getMemberDecl()->getNameAsString().c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_bind_text(insUse,
				sqlite3_bind_parameter_index(insUse, ":struct"),
				ST->getDecl()->getNameAsString().c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_bind_text(insUse,
				sqlite3_bind_parameter_index(insUse, ":str_src"),
				strSrc.c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_bind_text(insUse,
				sqlite3_bind_parameter_index(insUse, ":use_src"),
				useSrc.c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = bindLoc(insUse, ME->getSourceRange());
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_step(insUse);
	if (ret != SQLITE_DONE) {
		llvm::errs() << "db step failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
}

void MatchCallback::handleME(const MemberExpr *ME)
{
	//ME->dumpColor();

	//auto &SM = C.getSourceManager();

	auto T = ME->getBase()->getType();
	if (auto PT = T->getAs<PointerType>())
		T = PT->getPointeeType();

	if (auto ST = T->getAsStructureType()) {
		handleUse(ME, ST);
	} else if (/*auto RD =*/ T->getAsRecordDecl()) {
		// TODO: anonymous member struct
		//RD->dumpColor();
	} else {
		ME->getSourceRange().dump(SM);
		ME->dumpColor();
		T->dump();
	}
}

std::string MatchCallback::getRDName(const RecordDecl *RD)
{
	return RD->isAnonymousStructOrUnion() ? "<anonymous>" :
						RD->getNameAsString();
}

void MatchCallback::handleRD(const RecordDecl *RD)
{
	//RD->dumpColor();

	auto RDSR = RD->getSourceRange();
	auto src = getSrc(RDSR.getBegin());
	int ret;

	SQLStmtResetter insSrcResetter(sqlHolder, insSrc);
	ret = sqlite3_bind_text(insSrc,
				sqlite3_bind_parameter_index(insSrc, ":src"),
				src.c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_step(insSrc);
	if (ret != SQLITE_DONE) {
		llvm::errs() << "db step failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}

	SQLStmtResetter insStrResetter(sqlHolder, insStr);
	ret = sqlite3_bind_text(insStr,
				sqlite3_bind_parameter_index(insStr, ":name"),
				getRDName(RD).c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_bind_text(insStr,
				sqlite3_bind_parameter_index(insStr, ":src"),
				src.c_str(), -1,
				SQLITE_TRANSIENT);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = bindLoc(insStr, RDSR);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}
	ret = sqlite3_step(insStr);
	if (ret != SQLITE_DONE) {
		llvm::errs() << "db step failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}

	for (const auto &f : RD->fields()) {
		//f->dumpColor();
		/*llvm::errs() << __func__ << ": " << RD->getNameAsString() <<
				"." << f->getNameAsString() << "\n";*/
		auto SR = f->getSourceRange();
		SQLStmtResetter insMemResetter(sqlHolder, insMem);
		ret = sqlite3_bind_text(insMem,
					sqlite3_bind_parameter_index(insMem, ":name"),
					f->getNameAsString().c_str(), -1,
					SQLITE_TRANSIENT);
		if (ret != SQLITE_OK) {
			llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					sqlite3_errmsg(sqlHolder) << "\n";
			return;
		}
		ret = sqlite3_bind_text(insMem,
					sqlite3_bind_parameter_index(insMem, ":struct"),
					getRDName(RD).c_str(), -1,
					SQLITE_TRANSIENT);
		if (ret != SQLITE_OK) {
			llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					sqlite3_errmsg(sqlHolder) << "\n";
			return;
		}
		ret = sqlite3_bind_text(insMem,
					sqlite3_bind_parameter_index(insMem, ":src"),
					src.c_str(), -1,
					SQLITE_TRANSIENT);
		if (ret != SQLITE_OK) {
			llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					sqlite3_errmsg(sqlHolder) << "\n";
			return;
		}
		ret = sqlite3_bind_int(insMem,
				       sqlite3_bind_parameter_index(insMem, ":strBegLine"),
				       SM.getPresumedLineNumber(RDSR.getBegin()));
		if (ret != SQLITE_OK) {
			llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					sqlite3_errmsg(sqlHolder) << "\n";
			return;
		}
		ret = sqlite3_bind_int(insMem,
				       sqlite3_bind_parameter_index(insMem, ":strBegCol"),
				       SM.getPresumedColumnNumber(RDSR.getBegin()));
		if (ret != SQLITE_OK) {
			llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					sqlite3_errmsg(sqlHolder) << "\n";
			return;
		}
		ret = bindLoc(insMem, SR);
		if (ret != SQLITE_OK) {
			llvm::errs() << "db bind failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					sqlite3_errmsg(sqlHolder) << "\n";
			return;
		}
		ret = sqlite3_step(insMem);
		if (ret != SQLITE_DONE) {
		    llvm::errs() << "db step failed (" << __LINE__ << "): " <<
				    sqlite3_errstr(ret) << " -> " <<
				    sqlite3_errmsg(sqlHolder) << "\n";
		    return;
		}
	}
}

void MatchCallback::run(const MatchFinder::MatchResult &res)
{
	if (auto ME = res.Nodes.getNodeAs<MemberExpr>("ME")) {
		handleME(ME);
	}
	if (auto RD = res.Nodes.getNodeAs<RecordDecl>("RD")) {
		if (RD->isThisDeclarationADefinition())
			handleRD(RD);
	}
}

static int busy_handler(void *data, int count)
{
	if (count >= 1000)
		return 0;

	std::this_thread::sleep_for(std::min(count, 10) * std::chrono::milliseconds(50));

	return 1;
}

SQLHolder MyChecker::openDB() const
{
	sqlite3 *sql;
	char *err;
	int ret;

	ret = sqlite3_open_v2("structs.db", &sql, SQLITE_OPEN_READWRITE |
			      SQLITE_OPEN_CREATE, NULL);
	SQLHolder sqlHolder(sql);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db open failed: " << sqlite3_errstr(ret) << "\n";
		return nullptr;
	}

	ret = sqlite3_exec(sqlHolder, "PRAGMA foreign_keys = ON;", NULL, NULL,
			   &err);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db PRAGMA failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return nullptr;
	}

	ret = sqlite3_busy_handler(sqlHolder, busy_handler, nullptr);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db busy_handler failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return nullptr;
	}

	static const llvm::SmallVector<const char *> create_tables {
		"source(id INTEGER PRIMARY KEY, "
			"src TEXT NOT NULL UNIQUE)",
		"struct(id INTEGER PRIMARY KEY, "
			"name TEXT NOT NULL, "
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
			llvm::errs() << "db CREATE failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					err << "\n\t" << s << "\n";
			sqlite3_free(err);
			return nullptr;
		}
	}

	static const llvm::SmallVector<const char *> create_views {
		"structs_view AS "
			"SELECT struct.id, struct.name AS struct, source.src, "
				"struct.begLine || ':' || struct.begCol || "
				"'-' || struct.endLine || ':' || struct.endCol "
				"AS location "
			"FROM struct LEFT JOIN source ON struct.src=source.id",
		"members_view AS "
			"SELECT member.id, struct.name AS struct, "
				"member.name AS member, source.src, "
				"member.begLine || ':' || member.begCol "
				"|| '-' || member.endLine || ':' || "
				"member.endCol AS location "
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
			"SELECT struct, member, src, location "
				"FROM members_view "
				"WHERE id NOT IN (SELECT member FROM use) AND "
					"struct != ''",
	};

	for (auto c: create_views) {
		std::string s("CREATE VIEW IF NOT EXISTS ");
		s.append(c);
		ret = sqlite3_exec(sqlHolder, s.c_str(), NULL, NULL, &err);
		if (ret != SQLITE_OK) {
			llvm::errs() << "db CREATE failed (" << __LINE__ << "): " <<
					sqlite3_errstr(ret) << " -> " <<
					err << "\n\t" << s << "\n";
			sqlite3_free(err);
			return nullptr;
		}
	}

	return sqlHolder;
}

void MyChecker::checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
					  AnalysisManager &A,
					  BugReporter &BR) const
{
	sqlite3_stmt *stmt;
	char *err;
	int ret;

	auto sqlHolder = openDB();

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT OR IGNORE INTO source(src) "
				 "VALUES (:src);",
				 -1, &stmt, NULL);
	SQLStmtHolder insSrc(stmt);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db prepare failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT OR IGNORE INTO "
				 "struct(name, src, begLine, begCol, endLine, endCol) "
				 "SELECT :name, id, :begLine, :begCol, :endLine, :endCol "
					"FROM source WHERE src=:src;",
				 -1, &stmt, NULL);
	SQLStmtHolder insStr(stmt);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db prepare failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
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
	SQLStmtHolder insMem(stmt);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db prepare failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}

	ret = sqlite3_prepare_v2(sqlHolder,
				 "INSERT OR IGNORE INTO "
				 "use(member, src, begLine, begCol, endLine, endCol) "
				 "SELECT (SELECT member.id FROM member "
						"LEFT JOIN struct ON member.struct=struct.id "
						"LEFT JOIN source ON struct.src=source.id "
						"WHERE member.name=:member AND "
						"struct.name=:struct AND "
						"source.src=:str_src), "
					"(SELECT id FROM source WHERE src=:use_src), "
					":begLine, :begCol, :endLine, :endCol;",
				 -1, &stmt, NULL);
	SQLStmtHolder insUse(stmt);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db prepare failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sqlHolder) << "\n";
		return;
	}

	//TU->dumpColor();

	auto basePathStr = A.getAnalyzerOptions().getCheckerStringOption(this, "basePath");
	std::filesystem::path basePath(basePathStr.str());

	MatchFinder F;
	MatchCallback CB(A.getSourceManager(), sqlHolder, insSrc, insStr,
			 insMem, insUse, basePath);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, memberExpr().bind("ME")),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, recordDecl(isStruct()).bind("RD")),
		     &CB);

	ret = sqlite3_exec(sqlHolder, "BEGIN;", NULL, NULL, &err);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db BEGIN failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return;
	}

	F.matchAST(A.getASTContext());

	ret = sqlite3_exec(sqlHolder, "END;", NULL, NULL, &err);
	if (ret != SQLITE_OK) {
		llvm::errs() << "db END failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " << err << "\n";
		sqlite3_free(err);
		return;
	}
}

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<MyChecker>("jirislaby.StructMembersChecker",
				 "Searches for unused struct members",
				 "");
  registry.addCheckerOption("string", "jirislaby.StructMembersChecker",
			    "basePath", "",
			    "Path to resolve file paths against (empty = absolute paths)",
			    "released");
}

extern "C" const char clang_analyzerAPIVersionString[] =
		CLANG_ANALYZER_API_VERSION_STRING;
