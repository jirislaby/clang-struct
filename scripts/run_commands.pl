#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;
use JSON;
use Parallel::ForkManager;

#sub get_cmdline($) {
#	my @cmd = split /\s+/, shift;
#
#	@cmd = map {
#		if (/^-[cfm]/ || /^-Wp/ || /^-Werror/ || /^-nostdinc/ ||
#				/^--target/) {
#			()
#		} else {
#			($_);
#		}
#	} @cmd;
#	print Dumper(\@cmd);
#
#	return @cmd;
#}

my $json;
{
	local $/;
	open(my $j, "<compile_commands.json") or die 'no compile_commands.json';
	$json = <$j>;
	close $j;
}

$json = JSON->new->allow_nonref->decode($json);

sub getNumCpu() {
	return 1;
	open my $cpuinfo, '/proc/cpuinfo' or die "cannot open cpuinfo";
	my $ret = scalar (map /^processor/, <$cpuinfo>);
	close $cpuinfo;

	return $ret;
}

my $pm = Parallel::ForkManager->new(getNumCpu());
my $stop = 0;

sub stop() {
	print STDERR "Stopping on signal!\n";
	$stop = 1;
}
$SIG{'INT'} = \&stop;
$SIG{'TERM'} = \&stop;

foreach my $entry (@{$json}) {
	last if $stop;
	$pm->start and next;

	print $entry->{'file'}, "\n";
	print "\tCMD=", substr($entry->{'command'}, 0, 50), "\n";
	print "\tDIR=", $entry->{'directory'}, "\n";

	chdir $entry->{'directory'} or die "cannot cd to $entry->{'directory'}";

	#	my @cmd = get_cmdline($entry->{'command'});
	#	splice @cmd, 1, 0, qw|-cc1 -analyze
	#		-load ../../clang-struct/src/clang-struct.so
	#		-analyzer-checker jirislaby.StructMembersChecker|;
	#
	#	system(@cmd) == 0 or die "cannot exec '" . join(' ', @cmd) . "'";
	my $cmd = $entry->{'command'};
	$cmd .= ' -w -E -o - | clang -cc1 -analyze -w';
	$cmd .= ' -load clang-struct.so';
	$cmd .= ' -analyzer-checker jirislaby.StructMembersChecker';
	#print "$cmd\n";
	exec($cmd);
}

print STDERR "Done, waiting for ", scalar $pm->running_procs, " children\n";

$pm->wait_all_children;

1;
