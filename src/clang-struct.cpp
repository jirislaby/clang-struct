// SPDX-License-Identifier: GPL-2.0-only

#include <filesystem>
#include <set>

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include "Lock.h"
#include "sqlconn.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;
using namespace ClangStruct;

namespace {

class MyChecker final : public Checker<check::EndOfTranslationUnit> {
public:
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
				 AnalysisManager &A, BugReporter &BR) const;
};

class MatchCallback : public MatchFinder::MatchCallback {
public:
	MatchCallback(SourceManager &SM, const SQLConn &conn, std::filesystem::path &basePath) :
		SM(SM), conn(conn), basePath(basePath) { }

	void run(const MatchFinder::MatchResult &res);
private:
	void bindLoc(SQLConn::Location &loc, const SourceRange &SR);
	std::string getSrc(const SourceLocation &SLOC);

	void handleUse(const SourceRange &initSR, const NamedDecl *ND, const RecordDecl *RD,
		       int load, bool implicit);
	void handleUse(const MemberExpr *ME, const RecordDecl *RD, int load) {
		handleUse(ME->getSourceRange(), ME->getMemberDecl(), RD, load, false);
	}
	void handleME(const MemberExpr *ME, int store);
	void handleRD(const RecordDecl *RD);
	void handleILE(const InitListExpr *ILE, ASTContext *AC);

	static std::string getNDName(const NamedDecl *ND);
	static std::string getRDName(const RecordDecl *RD);

	SourceManager &SM;

	const SQLConn &conn;
	std::filesystem::path &basePath;
	std::set<const MemberExpr *> visited;
	std::set<std::string> sources;
};

}

void MatchCallback::bindLoc(SQLConn::Location &loc, const SourceRange &SR)
{
	loc.begLine = SM.getPresumedLineNumber(SR.getBegin());
	loc.begCol = SM.getPresumedColumnNumber(SR.getBegin());
	loc.endLine = SM.getPresumedLineNumber(SR.getEnd());
	loc.endCol = SM.getPresumedColumnNumber(SR.getEnd());
}

std::string MatchCallback::getSrc(const SourceLocation &SLOC)
{
	auto src = SM.getPresumedLoc(SLOC).getFilename();
	std::filesystem::path p(src);

	p = p.lexically_normal();

	if (!basePath.empty()) {
		auto rel = p.lexically_relative(basePath);
		if (!rel.empty())
			p = std::move(rel);
	}

	return p.string();
}

void MatchCallback::handleUse(const SourceRange &initSR, const NamedDecl *ND, const RecordDecl *RD,
			      int load, bool implicit)
{
	auto strLoc = RD->getBeginLoc();
	auto strSrc = getSrc(strLoc);
	auto useSrc = getSrc(initSR.getBegin());

	SQLConn::Use use {
		.src = useSrc,
		.load = load,
		.implicit = implicit,
		.strSrc = strSrc,
		.str = {
			.name = getRDName(RD),
			.line = SM.getPresumedLineNumber(strLoc),
			.col = SM.getPresumedColumnNumber(strLoc),
		},
		.member = getNDName(ND),
	};

	bindLoc(use.loc, initSR);
	if (!conn.insertUseTemp(use)) {
		llvm::errs() << __func__ << ": cannot insert use: " << conn.lastError() << '\n';
		return;
	}
}

void MatchCallback::handleME(const MemberExpr *ME, int store)
{
	if (!visited.insert(ME).second)
		return;

	//ME->dumpColor();
	//auto &SM = C.getSourceManager();

	auto T = ME->getBase()->getType();
	if (auto PT = T->getAs<PointerType>())
		T = PT->getPointeeType();

	if (auto ST = T->getAsStructureType()) {
		handleUse(ME, ST->getDecl(), store);
	} else if (auto RD = T->getAsRecordDecl()) {
		handleUse(ME, RD, store);
	} else {
		llvm::errs() << __PRETTY_FUNCTION__ << ": unhandled type\n";
		ME->getSourceRange().dump(SM);
		ME->dumpColor();
		T->dump();
		abort();
	}
}

std::string MatchCallback::getNDName(const NamedDecl *ND)
{
	if (!ND->getIdentifier())
		return "<unnamed>";

	return ND->getNameAsString();
}

std::string MatchCallback::getRDName(const RecordDecl *RD)
{
	if (RD->isAnonymousStructOrUnion())
		return "<anonymous>";

	return getNDName(RD);
}

void MatchCallback::handleRD(const RecordDecl *RD)
{
	//RD->dumpColor();

	auto RDSR = RD->getSourceRange();
	auto RDName = getRDName(RD);
	auto src = getSrc(RDSR.getBegin());

	std::string type;
	if (RD->isStruct())
		type = "s";
	else if (RD->isUnion())
		type = "u";
	else {
		llvm::errs() << src << ": unknown RD type:\n";
		RD->dumpColor();
		return;
	}

	std::stringstream ss;
	bool cont = false;
	bool packed = false;
	for (const auto &f : RD->attrs()) {
		// implicit attrs don't have names
		// so VisibilityAttr do not
		if (!f->getAttrName()) {
			if (!f->isImplicit() && !llvm::isa<VisibilityAttr>(f)) {
				llvm::errs() << src << ": unnamed attribute: ";
				f->printPretty(llvm::errs(), RD->getASTContext().getPrintingPolicy());
				llvm::errs() << " in:\n";
				RD->dumpColor();
			}
			continue;
		}
		if (cont)
			ss << "|";
		auto attr = f->getNormalizedFullName();
		if (attr == "packed")
			packed = true;
		ss << attr;
		cont = true;
	}

	SQLConn::Struct str {
		.name = RDName,
		.type = type,
		.attrs = ss.str(),
		.packed = packed,
		.inMacro = RDSR.getBegin().isMacroID(),
		.src = src,
	};

	bindLoc(str.loc, RDSR);
	if (!conn.insertStructTemp(str)) {
		llvm::errs() << __func__ << ": cannot insert struct: " << conn.lastError() << '\n';
		return;
	}

	for (const auto &f : RD->fields()) {
		//f->dumpColor();
		/*llvm::errs() << __func__ << ": " << RD->getNameAsString() <<
				"." << f->getNameAsString() << "\n";*/
		auto SR = f->getSourceRange();
		SQLConn::Member member {
			.name = getNDName(f),
			.src = src,
			.str = {
				.name = RDName,
				.line = SM.getPresumedLineNumber(RDSR.getBegin()),
				.col = SM.getPresumedColumnNumber(RDSR.getBegin()),
			},
		};

		bindLoc(member.loc, SR);
		if (!conn.insertMemberTemp(member)) {
			llvm::errs() << __func__ << ": cannot insert member: " <<
					conn.lastError() << '\n';
			return;
		}
	}
}

void MatchCallback::handleILE(const InitListExpr *ILE, ASTContext *AC)
{
	auto T = ILE->getType().getCanonicalType();
	if (auto RT = T->getAsStructureType()) {
		auto RD = RT->getDecl();
		//RD->dumpColor();
		for (auto field: RD->fields()) {
			SourceRange SR;
			bool implicit = true;
			auto idx = field->getFieldIndex();
			if (idx < ILE->getNumInits()) {
				auto init = ILE->getInit(idx);
				if (!init) {
					llvm::errs() << "null initializer for " <<
							field->getFieldIndex() << "\n";
					field->dumpColor();
					RD->dumpColor();
					ILE->dumpColor();
					abort();
				}

				SR = init->getSourceRange();
				implicit = llvm::isa<ImplicitValueInitExpr>(init);

				/*llvm::errs() << "field " << field->getFieldIndex() << "\n";
				field->dumpColor();
				llvm::errs() << "init\n";
				init->dumpColor();
				SR.dump(SM);*/
			}
			// implicit initializers have invalid SR, so have nested ILEs
			if (SR.isInvalid()) {
				auto parent = DynTypedNode::create(*ILE);
				auto &map = AC->getParentMapContext();
				for (unsigned jumps = 1;; jumps++) {
					SR = parent.getSourceRange();
					if (SR.isValid())
						break;
					parent = map.getParents(parent)[0];
					if (!parent.get<InitListExpr>()) {
						llvm::errs() << "idx=" << idx << " jumps=" <<
								jumps << "\n";
						field->dumpColor();
						RD->dumpColor();
						ILE->dumpColor();
						parent.dump(llvm::errs(), *AC);
						abort();
					}
				}
			}

			handleUse(SR, field, RD, 0, implicit);
		}
	} else if (T->isUnionType()) {
	} else if (!T->isConstantArrayType() && !llvm::isa<TypeOfType>(T) &&
		   !llvm::isa<BuiltinType>(T) && !llvm::isa<PointerType>(T)) {
		llvm::errs() << __PRETTY_FUNCTION__ << ": unhandled type\n";
		ILE->getSourceRange().dump(SM);
		ILE->dumpColor();
		T->dump();
		abort();
	}
}

void MatchCallback::run(const MatchFinder::MatchResult &res)
{
	if (auto ME = res.Nodes.getNodeAs<MemberExpr>("MESTORE"))
		handleME(ME, 0);

	if (auto ME = res.Nodes.getNodeAs<MemberExpr>("MELOAD"))
		handleME(ME, 1);

	if (auto ME = res.Nodes.getNodeAs<MemberExpr>("ME"))
		handleME(ME, -1);

	if (auto RD = res.Nodes.getNodeAs<RecordDecl>("RD")) {
		if (RD->isThisDeclarationADefinition())
			handleRD(RD);
	}
	if (auto ILE = res.Nodes.getNodeAs<InitListExpr>("ILE")) {
		handleILE(ILE, res.Context);
	}
}

void MyChecker::checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
					  AnalysisManager &A,
					  BugReporter &BR) const
{
	SQLConn db;
	if (!db.open()) {
		llvm::errs() << "cannot open db: " << db.lastError() << '\n';
		return;
	}

	//TU->dumpColor();

	auto basePathStr = A.getAnalyzerOptions().getCheckerStringOption(this, "basePath");
	std::filesystem::path basePath(basePathStr.str());
	auto runId = A.getAnalyzerOptions().getCheckerIntegerOption(this, "runId");

	MatchCallback CB(A.getSourceManager(), db, basePath);

	{
	const auto &tx = db.beginAuto();
	MatchFinder FRD;
	FRD.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, recordDecl().bind("RD")),
		     &CB);
	FRD.matchAST(A.getASTContext());

	MatchFinder F;
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource,
			      binaryOperator(isAssignmentOperator(),
					     hasLHS(memberExpr().bind("MESTORE")))),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource,
			      binaryOperator(hasEitherOperand(memberExpr().bind("MELOAD")))),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource,
			      unaryOperator(hasOperatorName("!"), hasUnaryOperand(memberExpr().bind("MELOAD")))),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource,
			      callExpr(forEachArgumentWithParam(memberExpr().bind("MELOAD"), anything()))),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource,
			      memberExpr(has(memberExpr())).bind("MELOAD")),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, memberExpr().bind("ME")),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, initListExpr().bind("ILE")),
		     &CB);

	F.matchAST(A.getASTContext());
	}

	auto dbFile = A.getAnalyzerOptions().getCheckerStringOption(this, "dbFile");
	auto &SM = A.getSourceManager();
	auto FE = SM.getFileEntryRefForID(SM.getMainFileID());
	auto srcRef = FE->getName();
	auto srcPath = std::filesystem::canonical(srcRef.str());
	if (!basePath.empty())
		srcPath = std::filesystem::relative(srcPath, basePath);
	auto src = srcPath.string();

	using Clock = std::chrono::steady_clock;
	using DurDoubleMilli = std::chrono::duration<double, std::milli>;

	auto start = Clock::now();
	Clock::duration timeLock;

	{
		// sqlite is very bad at concurrency
		Lock l;
		timeLock = Clock::now() - start;
		start = Clock::now();
		if (!db.move(dbFile.str(), runId))
			llvm::errs() << "cannot push the data to the db: " << db.lastError() << '\n';
	}
	auto timeDb = Clock::now() - start;

	std::ostringstream ss;
	ss << "TIME " << getpid() << " " << src << std::fixed << std::setprecision(3) <<
	      " LOCK=" << std::chrono::duration_cast<DurDoubleMilli>(timeLock).count() <<
	      " ms DB=" << std::chrono::duration_cast<DurDoubleMilli>(timeDb).count() << " ms\n";
	llvm::errs() << ss.str();
}

extern "C" void clang_registerCheckers(CheckerRegistry &registry) {
  registry.addChecker<MyChecker>("jirislaby.StructMembersChecker",
				 "Searches for unused struct members",
				 "");
  registry.addCheckerOption("int", "jirislaby.StructMembersChecker",
			    "runId", "-1",
			    "ID of the current run",
			    "released");
  registry.addCheckerOption("string", "jirislaby.StructMembersChecker",
			    "basePath", "",
			    "Path to resolve file paths against (empty = absolute paths)",
			    "released");
  registry.addCheckerOption("string", "jirislaby.StructMembersChecker",
			    "dbFile", "structs.db",
			    "Name of the database file to store into",
			    "released");
}

extern "C" const char clang_analyzerAPIVersionString[] =
		CLANG_ANALYZER_API_VERSION_STRING;
