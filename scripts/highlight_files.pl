#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;
use DBI;
use Getopt::Long;
use Term::ANSIColor;

my $basepath = '.';
my $dbfile = 'structs.db';
my $filter;
my $silent = 0;
my $skip = '';
my $verbose = 0;
GetOptions(
	"basepath=s"	=> \$basepath,
	"filter=s"	=> \$filter,
	"silent+"	=> \$silent,
	"skip"		=> \$skip,
	"verbose+"	=> \$verbose)
or die("Error in command line arguments\n");

die unless (-f $dbfile);

my $dbh = DBI->connect("dbi:SQLite:dbname=$dbfile", undef, undef,
	{ AutoCommit => 0 }) ||
	die "connect to db error: " . DBI::errstr;

END {
	$dbh->disconnect if (defined $dbh);
}

my $query = q@SELECT source.src AS src, begLine, begCol, endCol, load @ .
		q@FROM use LEFT JOIN source ON use.src = source.id @ .
		q@ORDER BY src, begLine, begCol;@;
my $stmt = $dbh->prepare($query) or die "cannot prepare";
$stmt->execute or die "cannot execute";

# res: file => [ line => [ [ beg, end, load ], ... ] ]
my %res;
while (my $row = $stmt->fetchrow_hashref) {
	my $file = $$row{'src'};
	push @{$res{$file}{$$row{'begLine'}}}, [$$row{'begCol'}, $$row{'endCol'}, $$row{'load'}];
}

foreach my $file (keys %res) {
#foreach my $file ('include/linux/range.h') {
	my $hash = $res{$file};

	open(my $fh, "$basepath/$file") or die "cannot open " . $file;
	my $line_no = 1;
	while (my $line = <$fh>) {
		my $columns = $$hash{$line_no++};
		my $line_len = length $line;
		my $res = $line;
		if (defined $columns) {
			#print Dumper($columns);
			my $last_end = 0;
			$res = "";
			foreach my $e (@{$columns}) {
				my $beg = @{$e}[0] - 1;
				my $end = @{$e}[1] - 1;

				next if ($beg < $last_end);
				last if ($beg >= $line_len);

				my $load = @{$e}[2] // -1;
				my $color = 'yellow';
				$color = 'green' if ($load == 1);
				$color = 'red' if ($load == 0);
				$res .= substr($line, $last_end, $beg - $last_end);
				$res .= colored(substr($line, $beg, $end - $beg), $color);
				$last_end = $end;
			}
			$res .= substr($line, $last_end);
			#print $line;
			print $res if $silent;
		}
		print $res unless $silent;
	}
	close $fh;
}

1;
