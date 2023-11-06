#ifndef SQLITE_H
#define SQLITE_H

#include <llvm/Support/raw_ostream.h>

#include <memory>

#include <sqlite3.h>

using SQLUnique = std::unique_ptr<sqlite3, void (*)(sqlite3 *)>;
using SQLStmtUnique = std::unique_ptr<sqlite3_stmt, void (*)(sqlite3_stmt *)>;

struct SQLHolder : public SQLUnique {
	SQLHolder(sqlite3 *sql) : SQLUnique(sql, [](sqlite3 *sql) {
	      sqlite3_close(sql);
	}) {}

	operator sqlite3 *() { return get(); }
};

struct SQLStmtHolder : public SQLStmtUnique {
	SQLStmtHolder(sqlite3_stmt *stmt) : SQLStmtUnique(stmt, [](sqlite3_stmt *stmt) {
	      sqlite3_finalize(stmt);
	}) {}

	operator sqlite3_stmt *() { return get(); }
};

struct SQLStmtResetter {
	SQLStmtResetter(sqlite3 *sql, sqlite3_stmt *stmt) : sql(sql), stmt(stmt) { }
	~SQLStmtResetter() {
		int ret = sqlite3_reset(stmt);
		if (ret != SQLITE_OK) {
		    llvm::errs() << "stmt reset failed (" << __LINE__ << "): " <<
				sqlite3_errstr(ret) << " -> " <<
				sqlite3_errmsg(sql) << "\n";
		}
	}
private:
	sqlite3 *sql;
	sqlite3_stmt *stmt;
};

#endif
