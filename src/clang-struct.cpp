#include <filesystem>
#include <set>

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include "sqlconn.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

namespace {
class MyChecker final : public Checker<check::EndOfTranslationUnit> {
public:
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
				 AnalysisManager &A, BugReporter &BR) const;
};

class MatchCallback : public MatchFinder::MatchCallback {
public:
	MatchCallback(SourceManager &SM, SQLConn &conn, int runId,
		      std::filesystem::path &basePath) :
		SM(SM), conn(conn), runId(runId), basePath(basePath) { }

	void run(const MatchFinder::MatchResult &res);
private:
	void bindLoc(SQLConn::Location &loc, const SourceRange &SR);
	std::string getSrc(const SourceLocation &SLOC);
	void addSrc(const std::string &src);

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

	SQLConn &conn;
	int runId;
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

	auto ret = std::filesystem::canonical(src);

	if (!basePath.empty())
		ret = std::filesystem::relative(ret, basePath);

	return ret.string();
}

void MatchCallback::addSrc(const std::string &src)
{
	if (!sources.insert(src).second)
		return;

	conn.addSrc({ runId, src });
}

void MatchCallback::handleUse(const SourceRange &initSR, const NamedDecl *ND, const RecordDecl *RD,
			      int load, bool implicit)
{
	auto strLoc = RD->getBeginLoc();
	auto memLoc = ND->getBeginLoc();
	auto strSrc = getSrc(strLoc);
	auto useSrc = getSrc(initSR.getBegin());

	addSrc(useSrc);

	SQLConn::Use use {
		.runId = runId,
		.member = getNDName(ND),
		.memLine = SM.getPresumedLineNumber(memLoc),
		.memCol = SM.getPresumedColumnNumber(memLoc),
		._struct = getRDName(RD),
		.strLine = SM.getPresumedLineNumber(strLoc),
		.strCol = SM.getPresumedColumnNumber(strLoc),
		.strSrc = strSrc,
		.useSrc = useSrc,
		.load = load,
		.implicit = implicit,
	};

	bindLoc(use.loc, initSR);
	conn.addUse(use);
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

	addSrc(src);

	std::string type;
	if (RD->isStruct())
		type = "s";
	else if (RD->isUnion())
		type = "u";
	else {
		llvm::errs() << "unnamed attribute in:\n";
		RD->dumpColor();
		return;
	}

	std::stringstream ss;
	bool cont = false;
	bool packed = false;
	for (const auto &f : RD->attrs()) {
		// implicit attrs don't have names
		if (!f->getAttrName()) {
			if (!f->isImplicit()) {
				llvm::errs() << "unnamed attribute in:\n";
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
		.runId = runId,
		.name = RDName,

		.type = type,
		.attrs = ss.str(),
		.packed = packed,
		.inMacro = RDSR.getBegin().isMacroID(),
		.src = src,
	};
	bindLoc(str.loc, RDSR);
	conn.addStruct(str);

	for (const auto &f : RD->fields()) {
		//f->dumpColor();
		/*llvm::errs() << __func__ << ": " << RD->getNameAsString() <<
				"." << f->getNameAsString() << "\n";*/
		auto SR = f->getSourceRange();
		SQLConn::Member member {
			.runId = runId,
			.name = getNDName(f),
			._struct = RDName,
			.src = src,
			.strBegLine = SM.getPresumedLineNumber(RDSR.getBegin()),
			.strBegCol = SM.getPresumedColumnNumber(RDSR.getBegin()),
		};

		bindLoc(member.loc, SR);
		conn.addMember(member);
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
	SQLConn conn;

	auto host = A.getAnalyzerOptions().getCheckerStringOption(this, "dbHost");
	auto port = A.getAnalyzerOptions().getCheckerIntegerOption(this, "dbPort");

	if (conn.open(host.str(), port) < 0)
		return;

	//TU->dumpColor();

	auto basePathStr = A.getAnalyzerOptions().getCheckerStringOption(this, "basePath");
	std::filesystem::path basePath(basePathStr.str());
	auto runId = A.getAnalyzerOptions().getCheckerIntegerOption(this, "runId");

	MatchCallback CB(A.getSourceManager(), conn, runId, basePath);

	//conn.begin();
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
	//conn.end();
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
			    "dbHost", "localhost",
			    "Database host to connect to (default = localhost)",
			    "released");
  registry.addCheckerOption("int", "jirislaby.StructMembersChecker",
			    "dbPort", "0",
			    "Database port to connect to (default = 0)",
			    "released");
}

extern "C" const char clang_analyzerAPIVersionString[] =
		CLANG_ANALYZER_API_VERSION_STRING;
