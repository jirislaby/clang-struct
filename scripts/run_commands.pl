#!/usr/bin/perl
use strict;
use warnings;
use DBI;
use Cwd 'abs_path';
use File::Spec;
use Getopt::Long;
use JSON;
use Parallel::ForkManager;

my $basepath = "";
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

my %skip_files;
if (-f $dbfile && $skip) {
	my $dbh = DBI->connect("dbi:SQLite:dbname=$dbfile", undef, undef,
		{ AutoCommit => 0 }) ||
		die "connect to db error: " . DBI::errstr;
	%skip_files = map {
		my $abs = File::Spec->catfile($basepath, @{$_}[0]);
		$abs = abs_path($abs);
		$abs => 1
	} $dbh->selectall_array(q@SELECT src FROM source WHERE src LIKE '%.c';@);
	$dbh->disconnect;
}

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

my $period = time();
my $counter = 0;
my $remaining = scalar @{$json};

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
			print STDERR "Processed $counter files in a minute. $remaining remaining.\n";

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
	$cmd =~ s/\s+-W[^\s]+//g;
	$cmd =~ s/\s+-c\b//;
	$cmd =~ s@\s+-o\s*[^\s]+@ -o /dev/null@;
	$cmd .= ' -w --analyze --analyzer-no-default-checks';
	$cmd .= ' -Xclang -load -Xclang clang-struct.so';
	$cmd .= ' -Xclang -analyzer-checker -Xclang jirislaby.StructMembersChecker';
	$cmd .= " -Xclang -analyzer-config -Xclang jirislaby.StructMembersChecker:basePath=$basepath";
	#print "$cmd\n";
	exec($cmd);
}

print STDERR "Done, waiting for ", scalar $pm->running_procs, " children\n";

$pm->wait_all_children;

kill 'TERM', $daemon;
wait;

1;
