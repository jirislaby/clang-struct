#ifndef SQL_H
#define SQL_H

#include <iostream>
#include <memory>

#include <mysql.h>

using SQLUnique = std::unique_ptr<MYSQL, void (*)(MYSQL *)>;
using SQLStmtUnique = std::unique_ptr<MYSQL_STMT, void (*)(MYSQL_STMT *)>;

struct SQLHolder : public SQLUnique {
	SQLHolder() : SQLHolder(nullptr) { }

	SQLHolder(MYSQL *sql) : SQLUnique(sql, [](MYSQL *sql) {
	      mysql_close(sql);
	}) {}

	operator MYSQL *() { return get(); }
};

struct SQLStmtHolder : public SQLStmtUnique {
	SQLStmtHolder() : SQLStmtHolder(nullptr) { }

	SQLStmtHolder(MYSQL_STMT *stmt) : SQLStmtUnique(stmt, [](MYSQL_STMT *stmt) {
	      mysql_stmt_close(stmt);
	}) {}

	operator MYSQL_STMT *() { return get(); }
};

struct SQLStmtResetter {
	SQLStmtResetter(MYSQL *sql, MYSQL_STMT *stmt) : sql(sql), stmt(stmt) { }
	~SQLStmtResetter() {
		int ret = mysql_stmt_reset(stmt);
		if (ret) {// ret != SQLITE_CONSTRAINT) {
		    std::cerr << "stmt reset failed (" << __LINE__ << "): " <<
				mysql_error(sql) << " err=" << mysql_errno(sql) << "\n";
		}
	}
private:
	MYSQL *sql;
	MYSQL_STMT *stmt;
};

#endif
