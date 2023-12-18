#!/usr/bin/bash

set -e

FILE="$1"
DB='structs.db'
CLANG='clang'

"$CLANG" -cc1 -analyze -load ../src/clang-struct/clang-struct-sa.so -analyzer-checker jirislaby.StructMembersChecker "$FILE"

test -f "$DB"

SQL=`sed -n 's@.*SQL: @@ p' "$FILE"`
SQLITE=(sqlite3 -batch -noheader -csv "$DB" "$SQL")
EXPECT=`sed -n 's@.*EXPECT: @@ p' "$FILE"`

if ! "${SQLITE[@]}" | grep -q "$EXPECT"; then
	echo "EXPECTED: $EXPECT"
	echo "GOT:"
	"${SQLITE[@]}"
	exit 1
fi
