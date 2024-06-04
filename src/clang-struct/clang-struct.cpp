#include <filesystem>
#include <set>

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include "../Message.h"

#ifdef STANDALONE
#include "../sqlconn.h"
#else
#include <fcntl.h>
#include <mqueue.h>

#include <sys/stat.h>
#endif

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;
using Msg = Message<std::string>;

class Connection {
public:
	Connection() {}

	virtual int open() = 0;
	virtual void write(const Msg &msg) = 0;
};

#ifdef STANDALONE
class SQLConnection : public Connection {
public:
	SQLConnection(const std::string &dbFile) : Connection(), dbFile(dbFile) {}

	virtual int open();
	virtual void write(const Msg &msg);

private:
	const std::string dbFile;
	SQLConn<std::string> sql;
};
#else
class MQConnection : public Connection {
public:
	MQConnection() : Connection() {}
	~MQConnection();

	virtual int open();
	virtual void write(const Msg &msg);

private:
#if 0
	int sock = -1;
#else
	mqd_t mq = -1;
#endif
};
#endif

namespace {
class MyChecker final : public Checker<check::EndOfTranslationUnit> {
public:
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
				 AnalysisManager &A, BugReporter &BR) const;
};

class MatchCallback : public MatchFinder::MatchCallback {
public:
	MatchCallback(SourceManager &SM, Connection &conn, int runId,
		      std::filesystem::path &basePath) :
		SM(SM), conn(conn), runId(runId), basePath(basePath) { }

	void run(const MatchFinder::MatchResult &res);
private:
	void bindLoc(Msg &msg, const SourceRange &SR);
	std::string getSrc(const SourceLocation &SLOC);
	void addRun(Msg &msg);
	void addSrc(Msg &msg, const std::string &src);

	void handleUse(const SourceRange &initSR, const NamedDecl *ND, const RecordDecl *RD,
		       int load, bool implicit);
	void handleUse(const MemberExpr *ME, const RecordDecl *RD, int load) {
		handleUse(ME->getSourceRange(), ME->getMemberDecl(), RD, load, false);
	}
	void handleME(const MemberExpr *ME, int store);
	void handleRD(const RecordDecl *RD);
	void handleILE(const InitListExpr *ILE);

	static std::string getNDName(const NamedDecl *ND);
	static std::string getRDName(const RecordDecl *RD);

	SourceManager &SM;

	Connection &conn;
	int runId;
	std::filesystem::path &basePath;
	std::set<const MemberExpr *> visited;
	std::set<std::string> sources;
};

}

#ifdef STANDALONE
int SQLConnection::open()
{
	return sql.open(dbFile);
}

void SQLConnection::write(const Msg &msg)
{
	sql.handleMessage(msg);
}

#else
MQConnection::~MQConnection()
{
#if 0
	if (sock >= 0)
		close(sock);
#else
	if (mq >= 0)
		mq_close(mq);
#endif
}

int MQConnection::open()
{
#if 0
	sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (sock < 0) {
		llvm::errs() << "cannot create socket: " << strerror(errno) << "\n";
		return -1;
	}

	struct sockaddr_un addr = {
		.sun_family = AF_UNIX,
		.sun_path = "db_filler",
	};

	int ret = connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		llvm::errs() << "cannot connect: " << strerror(errno) << "\n";
		return -1;
	}
#else
	mq = mq_open("/db_filler", O_WRONLY,  0600, NULL);
	if (mq < 0) {
		llvm::errs() << "cannot open msg queue: " << strerror(errno) << "\n";
		return -1;
	}
#endif

	return 0;
}

void MQConnection::write(const Msg &msg)
{
#if 0
	auto ret = ::write(sock, msg.c_str(), msg.length());
	if (ret < 0) {
		llvm::errs() << "write: " << strerror(errno) << "\n";
		return;
	}

	if ((size_t)ret != msg.length())
		llvm::errs() << "stray write: " << ret << "/" << msg.length() << "\n";
#else
	auto msgStr = msg.serialize();

	//std::cerr << "sending: " << msg << "\n";

	if (mq_send(mq, msgStr.c_str(), msgStr.length(), 0) < 0) {
		llvm::errs() << "mq_send: " << strerror(errno) << "\n";
		return;
	}
#endif
}
#endif

void MatchCallback::bindLoc(Msg &msg, const SourceRange &SR)
{
	msg.add("begLine", SM.getPresumedLineNumber(SR.getBegin()));
	msg.add("begCol", SM.getPresumedColumnNumber(SR.getBegin()));
	msg.add("endLine", SM.getPresumedLineNumber(SR.getEnd()));
	msg.add("endCol", SM.getPresumedColumnNumber(SR.getEnd()));
}

std::string MatchCallback::getSrc(const SourceLocation &SLOC)
{
	auto src = SM.getPresumedLoc(SLOC).getFilename();

	auto ret = std::filesystem::canonical(src);

	if (!basePath.empty())
		ret = std::filesystem::relative(ret, basePath);

	return ret.string();
}

void MatchCallback::addRun(Msg &msg)
{
	if (runId >= 0)
		msg.add("run", runId);
	else
		msg.add("run");
}

void MatchCallback::addSrc(Msg &msg, const std::string &src)
{
	if (!sources.insert(src).second)
		return;

	msg.renew(Msg::KIND::SOURCE);

	addRun(msg);
	msg.add("src", src);
	conn.write(msg);
}

void MatchCallback::handleUse(const SourceRange &initSR, const NamedDecl *ND, const RecordDecl *RD,
			      int load, bool implicit)
{
	auto strLoc = RD->getBeginLoc();
	auto strSrc = getSrc(strLoc);
	auto useSrc = getSrc(initSR.getBegin());
	Msg msg;

	addSrc(msg, useSrc);

	msg.renew(Msg::KIND::USE);
	addRun(msg);
	msg.add("member", getNDName(ND));
	msg.add("struct", getRDName(RD));
	msg.add("strSrc", strSrc);
	msg.add("strLine", SM.getPresumedLineNumber(strLoc));
	msg.add("strCol", SM.getPresumedColumnNumber(strLoc));
	msg.add("use_src", useSrc);
	if (load < 0)
		msg.add("load");
	else
		msg.add("load", load);
	msg.add("implicit", implicit);

	bindLoc(msg, initSR);

	conn.write(msg);
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
	Msg msg;

	addSrc(msg, src);

	msg.renew(Msg::KIND::STRUCT);
	addRun(msg);
	msg.add("name", RDName);

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

	msg.add("type", type);
	msg.add("attrs", ss.str());
	msg.add("packed", packed);
	msg.add("inMacro", RDSR.getBegin().isMacroID());
	msg.add("src", src);
	bindLoc(msg, RDSR);
	conn.write(msg);

	for (const auto &f : RD->fields()) {
		//f->dumpColor();
		/*llvm::errs() << __func__ << ": " << RD->getNameAsString() <<
				"." << f->getNameAsString() << "\n";*/
		auto SR = f->getSourceRange();
		msg.renew(Msg::KIND::MEMBER);
		addRun(msg);
		msg.add("name", getNDName(f));
		msg.add("struct", RDName);
		msg.add("src", src);
		msg.add("strBegLine", SM.getPresumedLineNumber(RDSR.getBegin()));
		msg.add("strBegCol", SM.getPresumedColumnNumber(RDSR.getBegin()));

		bindLoc(msg, SR);
		conn.write(msg);
	}
}

void MatchCallback::handleILE(const InitListExpr *ILE)
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
			// implicit initializers has invalid SR
			if (!SR.isValid())
				SR = ILE->getSourceRange();

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
		handleILE(ILE);
	}
}

void MyChecker::checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
					  AnalysisManager &A,
					  BugReporter &BR) const
{
#ifdef STANDALONE
	auto dbFile = A.getAnalyzerOptions().getCheckerStringOption(this, "dbFile");
	SQLConnection conn(dbFile.str());
#else
	MQConnection conn;
#endif

	if (conn.open() < 0)
		return;

	//TU->dumpColor();

	auto basePathStr = A.getAnalyzerOptions().getCheckerStringOption(this, "basePath");
	std::filesystem::path basePath(basePathStr.str());
	auto runId = A.getAnalyzerOptions().getCheckerIntegerOption(this, "runId");

	MatchCallback CB(A.getSourceManager(), conn, runId, basePath);

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
#ifdef STANDALONE
  registry.addCheckerOption("string", "jirislaby.StructMembersChecker",
			    "dbFile", "structs.db",
			    "Name of the database file to store into",
			    "released");
#endif
}

extern "C" const char clang_analyzerAPIVersionString[] =
		CLANG_ANALYZER_API_VERSION_STRING;
