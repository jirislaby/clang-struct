#ifndef SQLCONN_H
#define SQLCONN_H

#include <sl/sqlite/SQLiteSmart.h>
#include <sl/sqlite/SQLConn.h>

#include "Message.h"

class SQLConn : public SlSqlite::SQLConn {
public:
	SQLConn() {}

	bool open(const std::filesystem::path &dbFile = "structs.db") noexcept {
		return SlSqlite::SQLConn::open(dbFile, SlSqlite::CREATE);
	}

	template <typename T>
	int handleMessage(const Message<T> &msg);
private:
	virtual bool createDB() override;
	virtual bool prepDB() override;

	template <typename T>
	int bindAndStep(SlSqlite::SQLStmtHolder &ins, const Message<T> &msg);

	SlSqlite::SQLStmtHolder insSrc;
	SlSqlite::SQLStmtHolder insStr;
	SlSqlite::SQLStmtHolder insMem;
	SlSqlite::SQLStmtHolder insUse;
};

#endif // SQLCONN_H
