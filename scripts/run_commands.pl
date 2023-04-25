#!/usr/bin/perl
use strict;
use warnings;
use Data::Dumper;
use JSON;

sub get_cmdline($) {
	my @cmd = split /\s+/, shift;

	@cmd = map {
		if (/^-[cfm]/ || /^-Wp/ || /^-Werror/ || /^-nostdinc/ ||
				/^--target/) {
			()
		} else {
			($_);
		}
	} @cmd;
	print Dumper(\@cmd);

	return @cmd;
}

my $json;
{
	local $/;
	open(my $j, "<compile_commands.json") or die 'no compile_commands.json';
	$json = <$j>;
	close $j;
}

$json = JSON->new->allow_nonref->decode($json);

foreach my $entry (@{$json}) {
	print $entry->{'file'}, "\n";
	print "\tCMD=", substr($entry->{'command'}, 0, 130), "\n";
	print "\tDIR=", $entry->{'directory'}, "\n";

	chdir $entry->{'directory'} or die "cannot cd to $entry->{'directory'}";

	my @cmd = get_cmdline($entry->{'command'});
	splice @cmd, 1, 0, qw|-cc1 -analyze
		-load ../../clang-struct/src/clang-struct.so 
		-analyzer-checker jirislaby.StructMembersChecker|;

	system(@cmd) == 0 or die "cannot exec '" . join(' ', @cmd) . "'";

	last;
}

1;
