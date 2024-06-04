#!/usr/bin/bash

set -e

FILE="$1"
DB=`mktemp structs-XXXXXXXXXX.db`
CLANG='clang'

"$CLANG" -cc1 -analyze -load ../src/clang-struct/clang-struct-sa.so \
	-analyzer-checker jirislaby.StructMembersChecker \
	-analyzer-config jirislaby.StructMembersChecker:dbFile="$DB" \
	"$FILE"

test -f "$DB"

trap "rm -f '$DB'" EXIT

SQL=`sed -n 's@.*SQL: @@ p' "$FILE"`
SQLITE=(sqlite3 -batch -noheader -csv "$DB")
EXPECT=`sed -n 's@.*EXPECT: @@ p' "$FILE"`

if ! "${SQLITE[@]}" "$SQL" | grep -q "$EXPECT"; then
	echo "EXPECTED: $EXPECT"
	echo "GOT:"
	"${SQLITE[@]}" "$SQL"
	"${SQLITE[@]}" .dump
	exit 1
fi
