#include <filesystem>

//#include <unistd.h>

#include <fcntl.h>
#include <mqueue.h>

//#include <sys/socket.h>
//#include <sys/un.h>
#include <sys/stat.h>

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "clang/StaticAnalyzer/Frontend/CheckerRegistry.h"

#include "Message.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ento;

class Connection {
public:
	Connection() {}
	~Connection();

	int open();
	void write(const Message &msg);
private:
#if 0
	int sock = -1;
#else
	mqd_t mq = -1;
#endif

};

namespace {
class MyChecker final : public Checker<check::EndOfTranslationUnit> {
public:
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
				 AnalysisManager &A, BugReporter &BR) const;
};

class MatchCallback : public MatchFinder::MatchCallback {
public:
	MatchCallback(SourceManager &SM, Connection &conn,
		      std::filesystem::path &basePath) :
		SM(SM), conn(conn), basePath(basePath) { }

	void run(const MatchFinder::MatchResult &res);
private:
	void bindLoc(Message &msg, const SourceRange &SR);
	std::string getSrc(const SourceLocation &SLOC);

	void handleUse(const MemberExpr *ME, const RecordType *ST);
	void handleME(const MemberExpr *ME);
	void handleRD(const RecordDecl *RD);

	static std::string getNDName(const NamedDecl *ND);
	static std::string getRDName(const RecordDecl *RD);

	SourceManager &SM;

	Connection &conn;
	std::filesystem::path &basePath;
};

}

Connection::~Connection()
{
#if 0
	if (sock >= 0)
		close(sock);
#else
	if (mq >= 0)
		mq_close(mq);
#endif
}

int Connection::open()
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

void Connection::write(const Message &msg)
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

void MatchCallback::bindLoc(Message &msg, const SourceRange &SR)
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

void MatchCallback::handleUse(const MemberExpr *ME, const RecordType *ST)
{
	auto strLoc = ST->getDecl()->getBeginLoc();
	auto strSrc = getSrc(strLoc);
	auto useSrc = getSrc(ME->getBeginLoc());
	Message msg(Message::KIND::SOURCE);

	msg.add("src", useSrc);
	conn.write(msg);

	msg.renew(Message::KIND::USE);
	msg.add("member", getNDName(ME->getMemberDecl()));
	msg.add("struct", getRDName(ST->getDecl()));
	msg.add("strSrc", strSrc);
	msg.add("strLine", SM.getPresumedLineNumber(strLoc));
	msg.add("strCol", SM.getPresumedColumnNumber(strLoc));
	msg.add("use_src", useSrc);

	bindLoc(msg, ME->getSourceRange());

	conn.write(msg);
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
	Message msg(Message::KIND::SOURCE);

	msg.add("src", src);
	conn.write(msg);

	msg.renew(Message::KIND::STRUCT);
	msg.add("name", RDName);

	std::stringstream ss;
	bool cont = false;
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
		ss << f->getNormalizedFullName();
		cont = true;
	}

	msg.add("attrs", ss.str());
	msg.add("src", src);
	bindLoc(msg, RDSR);
	conn.write(msg);

	for (const auto &f : RD->fields()) {
		//f->dumpColor();
		/*llvm::errs() << __func__ << ": " << RD->getNameAsString() <<
				"." << f->getNameAsString() << "\n";*/
		auto SR = f->getSourceRange();
		msg.renew(Message::KIND::MEMBER);
		msg.add("name", getNDName(f));
		msg.add("struct", RDName);
		msg.add("src", src);
		msg.add("strBegLine", SM.getPresumedLineNumber(RDSR.getBegin()));
		msg.add("strBegCol", SM.getPresumedColumnNumber(RDSR.getBegin()));

		bindLoc(msg, SR);
		conn.write(msg);
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

void MyChecker::checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
					  AnalysisManager &A,
					  BugReporter &BR) const
{
	Connection conn;

	if (conn.open() < 0)
		return;

	//TU->dumpColor();

	auto basePathStr = A.getAnalyzerOptions().getCheckerStringOption(this, "basePath");
	std::filesystem::path basePath(basePathStr.str());

	MatchFinder F;
	MatchCallback CB(A.getSourceManager(), conn, basePath);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, memberExpr().bind("ME")),
		     &CB);
	F.addMatcher(traverse(TK_IgnoreUnlessSpelledInSource, recordDecl(isStruct()).bind("RD")),
		     &CB);

	F.matchAST(A.getASTContext());
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
