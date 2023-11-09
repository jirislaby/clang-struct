#!/usr/bin/perl
use strict;
use warnings;
use Getopt::Long;
use JSON;
use Parallel::ForkManager;

my $basepath = "";
my $filter;
my $verbose = 0;
GetOptions(
	"basepath=s"	=> \$basepath,
	"filter=s"	=> \$filter,
	"verbose+"	=> \$verbose)
or die("Error in command line arguments\n");

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

my $period = time();

foreach my $entry (@{$json}) {
	last if $stop;

	my $file = $entry->{'file'};
	next unless ($file =~ /\.c$/);
	next if (defined $filter && $file !~ $filter);

	# we need to flush sqlite busy waiters before they time out
	if ($period - time() > 15 * 60) {
		$period = time();
		print STDERR "Flushing to the database, hold on\n";
		$pm->wait_all_children;
	}

	print "$file\n";
	$pm->start and next;

	if ($verbose) {
	    print "\tCMD=", substr($entry->{'command'}, 0, 50), "\n";
	    print "\tDIR=", $entry->{'directory'}, "\n";
	}

	chdir $entry->{'directory'} or die "cannot cd to $entry->{'directory'}";

	my $cmd = $entry->{'command'};
	$cmd .= ' -w -E -o - | clang -cc1 -analyze -w';
	$cmd .= ' -load clang-struct.so';
	$cmd .= ' -analyzer-checker jirislaby.StructMembersChecker';
	$cmd .= " -analyzer-config jirislaby.StructMembersChecker:basePath=$basepath";
	#print "$cmd\n";
	exec($cmd);
}

print STDERR "Done, waiting for ", scalar $pm->running_procs, " children\n";

$pm->wait_all_children;

1;
