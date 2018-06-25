#!/usr/bin/perl
#
# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2022-01-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

#
# @file check_maxscale_resources.pl - Nagios plugin for MaxScale resources
#
# Revision History
#
# Date         Who                     Description
# 06-03-2015   Massimiliano Pinto      Initial implementation
# 20-05-2016   Massimiliano Pinto      Maxadmin can connect with UNIX domain socket
#                                      in maxscale server only.
#                                      Commands changed with "ssh -i /somepath/id_rsa user@maxscalehost maxadmin ...."
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

Usage: $curr_script [-r <resource>] [-H <host>] [-u <user>] [-S <socket>] [-m <maxadmin>] [-h]

Options:
       -r <resource>	= modules|services|filters|listeners|servers|sessions
       -h		= provide this usage message
       -H <host>	= which host to connect to with SSH
       -u <user>	= username to connect to maxscale host via SSH (same user is used for maxadmin authentication)
       -i <identity>	= identity file to use for <user> at <host>
       -m <maxadmin>	= /path/to/maxadmin
       -S <socket>      = UNIX socket path between maxadmin and maxscale (default is /tmp/maxadmin.sock)
EOF
	exit $rc;
}

%opts =(
    'r' => 'services',          # default maxscale resource to show
    'h' => '',                  # give help
    'H' => 'localhost',         # host
    'u' => 'root',              # username
    'm' => '/usr/local/mariadb-maxscale/bin/maxadmin',    # maxadmin
);

my $MAXADMIN_DEFAULT = $opts{'m'};

getopts('r:hH:u:i:S:m:', \%opts)
    or usage( $ERRORS{"UNKNOWN"} );
usage( $ERRORS{'OK'} ) if $opts{'h'};

my $MAXADMIN_RESOURCE =  $opts{'r'};
my $MAXADMIN = $opts{'m'};
my $MAXADMIN_SOCKET = $opts{'S'};
my $MAXSCALE_HOST_IDENTITY_FILE = $opts{'i'};

if (!defined $MAXSCALE_HOST_IDENTITY_FILE || length($MAXSCALE_HOST_IDENTITY_FILE) == 0) {
   die "$curr_script: ssh identity file for user $opts{'u'} is required";
}

if (!defined $MAXADMIN || length($MAXADMIN) == 0) {
	$MAXADMIN = $MAXADMIN_DEFAULT;
}

if (defined $MAXADMIN_SOCKET && length($MAXADMIN_SOCKET) > 0) {
        $MAXADMIN_SOCKET = ' -S ' . $MAXADMIN_SOCKET;
} else {
        $MAXADMIN_SOCKET = '';
}
# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("UNKNOWN: No response from MaxScale server (alarm)\n");
     exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);

my $command = "ssh -i " . $MAXSCALE_HOST_IDENTITY_FILE . ' ' . $opts{'u'} . '@' . $opts{'H'} . ' ' . $MAXADMIN . $MAXADMIN_SOCKET . ' ' . " list " . $MAXADMIN_RESOURCE;

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
        $resource_match = "Name";
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

