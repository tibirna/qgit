#!/bin/sh

# Generate help.h from README

set -e

infile="$1"
outfile="$2"
tmpfile1="$2.t1"
tmpfile2="$2.t2"

asciidoc --attribute 'newline=\n' --backend=html4 --no-header-footer \
	--out-file="$tmpfile1" "$infile"

sed -n -e 's/"/\\"/g' -e '/Command line arguments/,$s/^.*$/"&\\n"/p' \
	"$tmpfile1" >"$tmpfile2"

cat >"$outfile" <<EOF
/* Help content is generated automatically from README by helpgen script */

static const char* helpInfo =
"<qt>\n"
"<center><h1>QGit Handbook</h1></center>\n"
`cat "$tmpfile2"`
"<qt>\n";
EOF

rm -f "$tmpfile1" "$tmpfile2"
