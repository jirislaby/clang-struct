#include <map>
#include <memory>
#include <sstream>
#include <utility>

#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

using namespace clang;
using namespace clang::ento;

namespace {
class MyChecker final : public Checker</*check::Bind,*/ check::Location,
		check::EndOfTranslationUnit> {
	using RWSet = std::map<std::string, llvm::SmallVector<SourceRange, 10>>;

	mutable std::string strName;
	mutable bool expanded;
	mutable std::set<std::pair<bool, const Stmt *>> visited;
	mutable llvm::SmallVector<std::pair<SourceRange, std::string>, 10> members;
	mutable RWSet read;
	mutable RWSet written;

	const BugType BTrd { this, "Struct member is only read",
			     categories::LogicError };
	const BugType BTwr { this, "Struct member is only written",
			     categories::LogicError };
	const BugType BTunused { this, "Struct member is unused",
				 categories::LogicError };

private:
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
		  CheckerContext &C) const;

public:
  /*void checkBind(const SVal &loc, const SVal &val, const Stmt *S,
		 CheckerContext &C) const;*/
  //void checkPreStmt(const UnaryOperator *UO, CheckerContext &C) const;
  void checkLocation(const SVal &loc, bool isLoad, const Stmt *S,
		     CheckerContext &C) const;
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
				 AnalysisManager &A, BugReporter &BR) const;
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

	/*llvm::errs() << __func__ << " ";
	S->getSourceRange().dump(C.getSourceManager());*/
	//S->dumpColor();

	forEachChildren<MemberExpr>(S, [this, &S, &isLoad, &C](const MemberExpr *ME) {
		handleME(S, ME, isLoad, C);
		return false;
	});
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
