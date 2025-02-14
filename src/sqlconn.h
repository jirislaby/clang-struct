#ifndef SQLCONN_H
#define SQLCONN_H

#include <cstring>

#include "sql.h"

class SQLConn {
public:
	struct Source {
		int runId;
		const std::string &src;
	};

	struct Location {
		unsigned begLine;
		unsigned begCol;
		unsigned endLine;
		unsigned endCol;
	};

	struct Struct {
		int runId;
		struct Location loc;

		const std::string &name;
		const std::string &type;
		const std::string &attrs;
		bool packed;
		bool inMacro;
		const std::string &src;
	};

	struct Member {
		int runId;
		struct Location loc;

		const std::string &name;
		const std::string &_struct;
		const std::string &src;
		unsigned strBegLine;
		unsigned strBegCol;
	};

	struct Use {
		int runId;
		struct Location loc;

		const std::string &member;
		unsigned memLine;
		unsigned memCol;
		const std::string &_struct;
		unsigned strLine;
		unsigned strCol;
		const std::string &strSrc;
		const std::string &useSrc;
		int load;
		int implicit;
	};

	SQLConn() {}

	int open(const std::string &host, const unsigned int &port);

	int begin();
	int end();

	void addSrc(const struct Source &src);
	void addStruct(const struct Struct &_struct);
	void addMember(const struct Member &member);
	void addUse(const struct Use &use);
private:
	int openDB(const std::string &host, const unsigned int &port);
	int prepDB();
	SQLStmtHolder stmtPrepare(const std::string &query);
	int bindAndExec(MYSQL_STMT *stmt, MYSQL_BIND *bind, const int line);

	SQLHolder sqlHolder;
	SQLStmtHolder insSrc;
	SQLStmtHolder insStr;
	SQLStmtHolder insMem;
	SQLStmtHolder insUseMem;
	SQLStmtHolder insUse;
};

#endif // SQLCONN_H
