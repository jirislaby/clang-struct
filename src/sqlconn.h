#ifndef SQLCONN_H
#define SQLCONN_H

#include "Message.h"
#include "sqlite.h"

template <typename T>
class SQLConn {
public:
	using Msg = Message<T>;

	SQLConn() {}

	int open();

	int begin();
	int end();

	int handleMessage(const Msg &msg);
private:
	int openDB();
	int prepDB();

	int bindAndStep(SQLStmtHolder &ins, const Msg &msg);

	SQLHolder sqlHolder;
	SQLStmtHolder insSrc;
	SQLStmtHolder insStr;
	SQLStmtHolder insMem;
	SQLStmtHolder insUse;
};

#endif // SQLCONN_H
