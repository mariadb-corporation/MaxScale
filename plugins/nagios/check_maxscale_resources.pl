#!/usr/bin/perl
#
#
#
# This file is distributed as part of the MariaDB Corporation MaxScale. It is free
# software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation,
# version 2.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright MariaDB Corporation Ab 2013-2015
#
#

#
# @file check_maxscale_resources.pl - Nagios plugin for MaxScale resources
#
# Revision History
#
# Date         Who                     Description
# 06-03-2015   Massimiliano Pinto      Initial implementation
#

#use strict;
#use warnings;
use Getopt::Std;

my %opts;
my $TIMEOUT = 15;  # we don't want to wait long for a response
my %ERRORS = ('UNKNOWN' , '3',
              'OK',       '0',
              'WARNING',  '1',
              'CRITICAL', '2');

my $curr_script = "$0";
$curr_script =~ s{.*/}{};

sub usage {
	my $rc = shift;

	print <<"EOF";
MaxScale monitor checker plugin for Nagios

Usage: $curr_script [-r <resource>] [-H <host>] [-P <port>] [-u <user>] [-p <pass>] [-m <maxadmin>] [-h]

Options:
       -r <resource>	= modules|services|filters|listeners|servers|sessions
       -h		= provide this usage message
       -H <host>	= which host to connect to
       -P <port>	= port to use
       -u <user>	= username to connect as
       -p <pass>	= password to use for <user> at <host>
       -m <maxadmin>	= /path/to/maxadmin
EOF
	exit $rc;
}

%opts =(
	'r' => 'services',         	# default maxscale resource to show
	'h' => '',                      # give help
	'H' => 'localhost',		# host
	'u' => 'root',			# username
	'p' => '',			# password
	'P' => 6603,			# port
	'm' => '/usr/local/mariadb-maxscale/bin/maxadmin',	# maxadmin
	);

my $MAXADMIN_DEFAULT = $opts{'m'};

getopts('r:hH:u:p:P:m:', \%opts)
    or usage( $ERRORS{"UNKNOWN"} );
usage( $ERRORS{'OK'} ) if $opts{'h'};

my $MAXADMIN_RESOURCE =  $opts{'r'};
my $MAXADMIN = $opts{'m'};
if (!defined $MAXADMIN || length($MAXADMIN) == 0) {
	$MAXADMIN = $MAXADMIN_DEFAULT;
}

-x $MAXADMIN or
    die "$curr_script: Failed to find required tool: $MAXADMIN. Please install it or use the -m option to point to another location.";

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("UNKNOWN: No response from MaxScale server (alarm)\n");
     exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);

my $command = $MAXADMIN . ' -h ' . $opts{'H'} . ' -u ' . $opts{'u'} . ' -p "' . $opts{'p'} . '" -P ' . $opts{'P'} . ' ' . "list " . $MAXADMIN_RESOURCE;

#
# print "maxadmin command: $command\n";
#

open (MAXSCALE, "$command 2>&1 |") or die "can't get data out of Maxscale: $!";

my $hostname = qx{hostname}; chomp $hostname;

my $start_output = 0;
my $n_resources = 0;
my $performance_data="";


my $resource_type = $MAXADMIN_RESOURCE;
chop($resource_type);

my $resource_match = ucfirst("$resource_type Name");

if ($resource_type eq "listener") {
        $resource_match = "Service Name";
}
if ($resource_type eq "filter") {
        $resource_match = "Filter";
}
if ($resource_type eq "server") {
        $resource_match = "Server";
}
if ($resource_type eq "session") {
        $resource_match = "Session";
}

#
# print "Matching [$resource_match]\n";
#

while ( <MAXSCALE> ) {
    chomp;

    if ( /(Failed|Unable) to connect to MaxScale/ ) {
        printf "CRITICAL: $_\n";
	close(MAXSCALE);
        exit(2);
    }

    if ( ! /^$resource_match/ ) {
    } else {
	$start_output = 1;
	next;
    }
    if ($start_output) {
	next if (/--/ || $_ eq '');
	$n_resources++;
	if ($resource_type ne "session") {
		my $str;
		my $perf_line;
		my @data_row = split('\|', $_);
		$performance_data .= "$MAXADMIN_RESOURCE$n_resources=";
		foreach my $val (@data_row) {
			$str = $val;
			$str =~ s/^\s+|\s+$//g;
			$perf_line .= $str . ';';
  		}
		chop($perf_line);
		$performance_data .= $perf_line . ' ';
	}
    }
}

chop($performance_data);

###############################################
#
# print OK or CRITICAL based on $n_resources
#
################################################

if ($n_resources) {
	if ($performance_data eq '') {
		printf "OK: %d $MAXADMIN_RESOURCE found\n", $n_resources;
	} else {
		printf "OK: %d $MAXADMIN_RESOURCE found | %s\n", $n_resources, $performance_data;
	}
	close(MAXSCALE);
	exit 0;
} else {
	printf "CRITICAL: 0 $MAXADMIN_RESOURCE found\n";
	close(MAXSCALE);
	exit 2;
}

