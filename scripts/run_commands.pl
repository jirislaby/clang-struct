#!/usr/bin/perl
use strict;
use warnings;
use Digest::SHA qw(sha1_hex);
use DBI;
use DBD::SQLite::Constants qw(SQLITE_CONSTRAINT_UNIQUE);
use Cwd 'abs_path';
use Error qw(:try);
use File::Spec;
use Git;
use Getopt::Long;
use JSON;
use Parallel::ForkManager;

my $basepath = "";
my $dbfile = 'structs.db';
my $filter;
my $run_id;
my $silent = 0;
my $skip = 0;
my $verbose = 0;
GetOptions(
	"basepath=s"	=> \$basepath,
	"filter=s"	=> \$filter,
	"run=i"		=> \$run_id,
	"silent+"	=> \$silent,
	"skip"		=> \$skip,
	"verbose+"	=> \$verbose)
or die("Error in command line arguments\n");

my %skip_files;

my $dbh = DBI->connect("dbi:SQLite:dbname=$dbfile", undef, undef,
	{ AutoCommit => 0, sqlite_extended_result_codes	=> 1 }) ||
	die "connect to db error: " . DBI::errstr;

$dbh->do('CREATE TABLE IF NOT EXISTS config(id INTEGER PRIMARY KEY, sha TEXT UNIQUE, config TEXT) STRICT;') ||
	die "cannot CREATE TABLE config";
$dbh->do(<<'EOF'
CREATE TABLE IF NOT EXISTS run(
	id INTEGER PRIMARY KEY,
	version TEXT,
	sha TEXT,
	filter TEXT,
	config INTEGER REFERENCES config(id),
	skip INTEGER NOT NULL CHECK(skip IN (0, 1)),
	timestamp TEXT NOT NULL DEFAULT (STRFTIME('%Y-%m-%d %H:%M:%f', 'NOW', 'localtime'))
) STRICT;
EOF
) || die "cannot CREATE TABLE run";

if (defined $run_id) {
	$dbh->selectrow_hashref('SELECT id FROM run WHERE id = ?', undef, $run_id) or die "no such run_id: $run_id";
} else {
	my $config = "";
	open(my $config_f, "<.config") or die "cannot read .config";
	while (<$config_f>) {
		next if /^#/;
		next if /^$/;
		$config .= $_;
	}
	close $config_f;

	my $config_sha = sha1_hex($config);
	my $ins = $dbh->prepare('INSERT INTO config(sha, config) VALUES (?, ?)') || die "cannot prepare";
	$ins->{PrintError} = 0;
	$ins->execute($config_sha, $config) or $ins->err == SQLITE_CONSTRAINT_UNIQUE or die $dbh->errstr;

	my $sha;
	my $version;
	foreach my $srctree (qw|. source|) {
		next unless (-d "$srctree/.git");

		my $repo = Git->repository(Directory => "$srctree/.git");
		try {
			$sha = $repo->command_oneline([ 'rev-parse', '--verify', 'HEAD' ], STDERR => 0);
			$sha = substr($sha, 0, 12);
		} catch Git::Error::Command with {
		};
		try {
			$version = $repo->command_oneline([ 'describe', '--contains',
				'--exact-match', 'HEAD'], STDERR => 0);
			$version =~ s/\^0$//;
		} catch Git::Error::Command with {
		};
		last;
	}


	$ins = $dbh->prepare('INSERT INTO run(version, sha, filter, config, skip) ' .
		'SELECT ?, ?, ?, id, ? FROM config WHERE sha = ?') || die "cannot prepare";
	$ins->execute($version, $sha, $filter, $skip, $config_sha);
	$run_id = $dbh->last_insert_id();
	$dbh->commit;
}

if ($skip) {
	%skip_files = map {
		my $abs = File::Spec->catfile($basepath, @{$_}[0]);
		$abs = abs_path($abs);
		$abs => 1
	} $dbh->selectall_array(q@SELECT src FROM source WHERE src LIKE '%.c';@);
}
$dbh->disconnect;

my $json;
{
	local $/;
	open(my $j, "<compile_commands.json") or die 'no compile_commands.json';
	$json = <$j>;
	close $j;
}

$json = JSON->new->allow_nonref->decode($json);

sub getNumCpu() {
	open my $cpuinfo, '/proc/cpuinfo' or die "cannot open cpuinfo";
	my $ret = scalar (map /^processor/, <$cpuinfo>);
	close $cpuinfo;

	return $ret;
}

sub time_m_s($) {
	my $t = shift;
	return int($t / 60) . "m" . $t % 60 . "s";
}

my $pm = Parallel::ForkManager->new(getNumCpu());
$pm->set_waitpid_blocking_sleep(0);
my $stop = 0;

sub stop() {
	print STDERR "Stopping on signal!\n";
	$stop = 1;
}
$SIG{'INT'} = \&stop;
$SIG{'TERM'} = \&stop;

my $daemon = fork();
if (!$daemon) {
	exec('db_filler');
	die;
}

my $start_time = time;
my $period = $start_time;
my $counter = 0;
my $count = scalar @{$json};
my $remaining = $count;

foreach my $entry (@{$json}) {
	last if $stop;

	$remaining--;
	my $file = $entry->{'file'};
	next unless ($file =~ /\.c$/);
	next if (defined $filter && $file !~ $filter);
	if ($skip_files{$file}) {
		print "$file skipped\n";
		next;
	}

	$counter++;
	if ($silent == 0) {
		print "$counter|$remaining $file\n";
	} elsif ($silent == 1) {
		if (time() - $period > 60) {
			$period = time();
			my $elapsed = $period - $start_time;
			my $projected = $elapsed * $count / ($count - $remaining);
			print STDERR "Processed $counter files in a minute. $remaining remaining. ",
				time_m_s($elapsed), "/", time_m_s($projected), "\n";

			$counter = 0;
		}
	}

	$pm->start and next;

	if ($verbose) {
	    print "\tCMD=", substr($entry->{'command'}, 0, 50), "\n";
	    print "\tDIR=", $entry->{'directory'}, "\n";
	}

	chdir $entry->{'directory'} or die "cannot cd to $entry->{'directory'}";

	my $cmd = $entry->{'command'};
	$cmd =~ s/\bccache\s+//;
	$cmd =~ s/\s+-W[^\s]+//g;
	$cmd =~ s/\s+-c\b//;
	$cmd =~ s@\s+-o\s*[^\s]+@ -o /dev/null@;
	$cmd .= ' -w --analyze --analyzer-no-default-checks';
	$cmd .= ' -Xclang -load -Xclang clang-struct.so';
	$cmd .= ' -Xclang -analyzer-checker -Xclang jirislaby.StructMembersChecker';
	$cmd .= " -Xclang -analyzer-config -Xclang jirislaby.StructMembersChecker:basePath=$basepath";
	$cmd .= " -Xclang -analyzer-config -Xclang jirislaby.StructMembersChecker:runId=$run_id";
	#print "$cmd\n";
	exec($cmd);
}

print STDERR "Done, waiting for ", scalar $pm->running_procs, " children\n";

$pm->wait_all_children;

kill 'TERM', $daemon;
wait;

1;
