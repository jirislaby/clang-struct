#ifndef SQLCONN_H
#define SQLCONN_H

#include "Message.h"
#include "sqlite.h"

template <typename T>
class SQLConn {
public:
	using Msg = Message<T>;

	SQLConn() {}

	int open(const std::string &dbFile = "structs.db");

	int begin();
	int end();

	int handleMessage(const Msg &msg);
private:
	int openDB(const std::string &dbFile);
	int prepDB();

	int bindAndStep(SQLStmtHolder &ins, const Msg &msg);

	SQLHolder sqlHolder;
	SQLStmtHolder insSrc;
	SQLStmtHolder insStr;
	SQLStmtHolder insMem;
	SQLStmtHolder insUse;
};

#endif // SQLCONN_H
