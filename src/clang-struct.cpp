#include <sqlite3.h>

#include <filesystem>
#include <memory>
//#include <sstream>
//#include <utility>
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
class MyChecker final : public Checker</*check::Bind,*/ /*check::Location,*/
		check::EndOfTranslationUnit> {
	//using RWSet = std::map<std::string, llvm::SmallVector<SourceRange, 10>>;

	//mutable std::string strName;
	//mutable bool expanded;
	//mutable std::set<std::pair<bool, const Stmt *>> visited;
	//mutable llvm::SmallVector<std::pair<SourceRange, std::string>, 10> members;
	/*
	mutable RWSet read;
	mutable RWSet written;

	const BugType BTrd { this, "Struct member is only read",
			     categories::LogicError };
	const BugType BTwr { this, "Struct member is only written",
			     categories::LogicError };
	const BugType BTunused { this, "Struct member is unused",
				 categories::LogicError };*/

private:
	SQLHolder openDB() const;
/*
	template <typename T>
	bool forEachChildren(const Stmt *stmt,
			     const std::function<bool(const T *)> &CB) const;

	void reportBug(SourceManager &SM, BugReporter &BR, const BugType &BT,
		       const std::string &strName, const std::string &name,
		       const std::string &dir, const SourceRange &range,
		       const RWSet::mapped_type &ranges = {}) const;

	void expand(const RecordDecl *ST) const;
	void handleEntry(const SourceRange &SR, const RecordDecl *ST,
			 const ValueDecl *M, bool isLoad) const;
	void handleME(const Stmt *S, const MemberExpr *ME, bool isLoad,
		      CheckerContext &C) const;*/

public:
  /*void checkBind(const SVal &loc, const SVal &val, const Stmt *S,
		 CheckerContext &C) const;*/
  //void checkPreStmt(const UnaryOperator *UO, CheckerContext &C) const;
#if 0
  void checkLocation(const SVal &loc, bool isLoad, const Stmt *S,
		     CheckerContext &C) const;
#endif
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
				 AnalysisManager &A, BugReporter &BR) const;
};

class MatchCallback : public MatchFinder::MatchCallback {
public:
	MatchCallback(SourceManager &SM, SQLHolder &sqlHolder,
		      SQLStmtHolder &insSrc, SQLStmtHolder &insStr,
		      SQLStmtHolder &insMem, SQLStmtHolder &insUse) :
		SM(SM), sqlHolder(sqlHolder), insSrc(insSrc), insStr(insStr),
		insMem(insMem), insUse(insUse) { }

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
};

}

#if 0
REGISTER_MAP_WITH_PROGRAMSTATE(Read, SymbolRef, bool);
REGISTER_MAP_WITH_PROGRAMSTATE(Written, SymbolRef, bool);

class MyVisitor final : public BugReporterVisitor {
public:
    MyVisitor(SymbolRef Sym) : Sym(Sym) {}
  virtual PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
					   BugReporterContext &BRC,
					   PathSensitiveBugReport &BR) override {
		/*if (N->getLocation().getKind() != ProgramPoint::PostStoreKind)
			return nullptr;*/

		auto state = N->getState();
		auto ch = state->get<Changed>(Sym);
		if (!ch)
			return nullptr;

		//llvm::errs() << __func__ << " BR=" << &BR << " loc=";
#ifdef DUMP_LOC
		N->getLocation().dump();
		llvm::errs() << "\n";
#endif
#ifdef DUMP_STATE
		N->getState()->dump();
		llvm::errs() << "\n";
#endif
		if (*ch != 2) {
			//llvm::errs() << "\tIGN due CUR ch=" << *ch << "\n";
			return nullptr;
		}

		state = N->getFirstPred()->getState();
		ch = state->get<Changed>(Sym);
		if (!ch || *ch == 2) {
			//llvm::errs() << "\tIGN due PRED ch=" << *ch << "\n";
			return nullptr;
		}

		//llvm::errs() << "\tTAKING\n";

		const auto S = N->getStmtForDiagnostics();
		if (!S)
		  return nullptr;

		auto NCtx = N->getLocationContext();
		auto L = PathDiagnosticLocation::createBegin(S, BRC.getSourceManager(), NCtx);
		if (!L.isValid() || !L.asLocation().isValid())
		  return nullptr;
		llvm::errs() << "L=";
		L.dump();
		return std::make_shared<PathDiagnosticEventPiece>(L, "Originated here");
	}
	virtual void Profile(llvm::FoldingSetNodeID &ID) const override {
	    static int X = 0;
	    ID.AddPointer(&X);
	    ID.Add(Sym);
	}
private:
    SymbolRef Sym;
};

void MyChecker::checkBind(const SVal &loc, const SVal &val, const Stmt *S,
			  CheckerContext &C) const
{
	return;
	auto state = C.getState();

#ifdef BIND_DEBUG
	llvm::errs() << __func__ << "\n";
	S->dumpColor();
	llvm::errs() << "loc=";
	loc.dump();
	llvm::errs() << " locreg=";
	loc.getAsRegion()->dump();
	llvm::errs() << " val=";
	val.dump();
	llvm::errs() << "\n";
#endif

	auto intVal = val.getAs<nonloc::ConcreteInt>();
	if (!intVal) {
	    llvm::errs() << "\tNOTINT\n";
		return;
	}

	//llvm::errs() << "\tintVal=" << intVal->getValue().getExtValue() << "\n";

	//C.addTransition(state->set<Changed>(loc.getAsLocSymbol(), intVal->getValue().getExtValue()));

#ifdef DUMP_STATE
	state->dump();
	llvm::errs() << "\n";
#endif
#if 0
	if (val.isConstant(3)) {
		//auto &BR = C.getBugReporter();
		auto N = C.generateNonFatalErrorNode();
		if (!N)
			return;
		auto B = std::make_unique<PathSensitiveBugReport>(BT,
								  BT.getDescription(),
								  N);
		B->addRange(S->getSourceRange());
		B->addVisitor<MyVisitor>(loc.getAsLocSymbol());
		C.emitReport(std::move(B));
		/*BR.EmitBasicReport(nullptr, this, "vic jak 3",
				   categories::LogicError, "tu",
				   PathDiagnosticLocation(S,
							  C.getSourceManager(),
							  C.getLocationContext()));*/
		//C.addSink();
	}
#endif
}

void MyChecker::checkPreStmt(const UnaryOperator *UO,
			     CheckerContext &C) const
{
	return;
	llvm::errs() << __func__ << "\n";
	UO->dumpColor();
	auto E = UO->getSubExpr();
	auto SVal = C.getSVal(E);
	SVal.dump();
	llvm::errs() << "\n";
}
#endif
#if 0
template <typename T>
bool MyChecker::forEachChildren(const Stmt *stmt,
				const std::function<bool(const T *)> &CB) const
{
	if (auto ME = llvm::dyn_cast<const T>(stmt))
		return CB(ME);

	for (auto ch : stmt->children())
		if (forEachChildren(ch, CB))
			return true;

	return false;
}

void MyChecker::expand(const RecordDecl *ST) const
{
	for (const auto &f : ST->fields()) {
		/*f->dumpColor();
		llvm::errs() << __func__ << " " << f->getNameAsString() << "\n";*/
		members.push_back(std::make_pair(f->getSourceRange(),
						 f->getNameAsString()));
	}
	expanded = true;
}

void MyChecker::handleEntry(const SourceRange &SR, const RecordDecl *ST,
			    const ValueDecl *M, bool isLoad) const
{
	/*llvm::errs() << "load=" << isLoad << " " << ST->getNameAsString() <<
			"->" << M->getNameAsString() << "\n";*/

	if (strName != ST->getNameAsString())
		return;

	if (!expanded)
		expand(ST);

	auto &container = isLoad ? read[M->getNameAsString()] :
			written[M->getNameAsString()];

	container.push_back(SR);
}

void MyChecker::handleME(const Stmt *S, const MemberExpr *ME, bool isLoad,
			 CheckerContext &C) const
{
	//auto &SM = C.getSourceManager();

	/*llvm::errs() << __func__ << " ";
	ME->getSourceRange().dump(C.getSourceManager());*/
	//ME->dump();
	//S->getSourceRange().dump(SM);

	auto T = ME->getBase()->getType();
	if (auto PT = T->getAs<PointerType>())
		T = PT->getPointeeType();

	if (auto ST = T->getAsStructureType()) {
		handleEntry(S->getSourceRange(), ST->getDecl(),
			    ME->getMemberDecl(), isLoad);
	}
}


void MyChecker::checkLocation(const SVal &loc, bool isLoad, const Stmt *S,
			      CheckerContext &C) const
{
	if (!visited.insert(std::make_pair(isLoad, S)).second)
		return;

	if (strName.empty()) {
		strName = C.getAnalysisManager().getAnalyzerOptions().
			getCheckerStringOption(this, "strName").str();
		llvm::errs() << __func__ << " strName=" << strName << "\n";
		assert(!strName.empty());
	}

	llvm::errs() << __func__ << " ";
	S->getSourceRange().dump(C.getSourceManager());
	S->dumpColor();
#if 0
	forEachChildren<MemberExpr>(S, [this, &S, &isLoad, &C](const MemberExpr *ME) {
		handleME(S, ME, isLoad, C);
		return false;
	});
#endif
}

void MyChecker::reportBug(SourceManager &SM, BugReporter &BR, const BugType &BT,
			  const std::string &strName, const std::string &name,
			  const std::string &dir, const SourceRange &range,
			  const RWSet::mapped_type &ranges) const
{
	std::stringstream desc;
	desc << strName << "->" << name << " is only " << dir;

	auto B = std::make_unique<BasicBugReport>(BT, desc.str(),
				PathDiagnosticLocation(range.getBegin(), SM));

	B->addRange(range);

	for (auto &R : ranges) {
		B->addNote(dir + " here",
			   PathDiagnosticLocation(R.getBegin(), SM),
			   { R });
	}

	BR.emitReport(std::move(B));
}

void MyChecker::checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
					  AnalysisManager &A,
					  BugReporter &BR) const
{
	if (!expanded) {
		llvm::errs() << "ERROR: struct " << strName << " not found\n";
		return;
	}

	auto &SM = A.getSourceManager();

	for (const auto &m : members) {
		auto &loc = m.first;
		auto &name = m.second;
		if (!read[name].empty() && !written[name].empty())
			continue;
		if (!read[name].empty()) {
			llvm::errs() << "ERROR: '" << strName << "->" << name << "' only read\n";
			reportBug(SM, BR, BTrd, strName, name, "read", loc, read[name]);
			continue;
		}
		if (!written[name].empty()) {
			llvm::errs() << "ERROR: '" << strName << "->" << name << "' only written\n";
			reportBug(SM, BR, BTwr, strName, name, "written", loc, written[name]);
			continue;
		}
		llvm::errs() << "ERROR: '" << strName << "->" << name << "' unused\n";
		reportBug(SM, BR, BTrd, strName, name, "unused", loc);
	}
}
#else

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

	return std::filesystem::canonical(src);
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

	std::this_thread::sleep_for(count * std::chrono::milliseconds(50));

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

	MatchFinder F;
	MatchCallback CB(A.getSourceManager(), sqlHolder, insSrc, insStr,
			 insMem, insUse);
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
#endif

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<MyChecker>("jirislaby.StructMembersChecker",
				 "Searches for unused struct members",
				 "");
  registry.addCheckerOption("string", "jirislaby.StructMembersChecker",
			    "strName", "",
			    "Name of the structure to look into",
			    "released");
}

extern "C" const char clang_analyzerAPIVersionString[] =
		CLANG_ANALYZER_API_VERSION_STRING;
