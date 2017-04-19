#!/usr/bin/perl
#
# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2019-07-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.
#

#
# @file check_maxscale_monitors.pl - Nagios plugin for MaxScale monitors
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
       -r <resource>	= monitors
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
    'r' => 'monitors',         # default maxscale resource to show
    'h' => '',                 # give help
    'H' => 'localhost',        # host
    'u' => 'root',             # username
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

my $command = "ssh -i " . $MAXSCALE_HOST_IDENTITY_FILE . ' ' . $opts{'u'} . '@' . $opts{'H'} . ' ' . $MAXADMIN . $MAXADMIN_SOCKET . ' ' . " show " . $MAXADMIN_RESOURCE;

#
# print "maxadmin command: $command\n";
#

open (MAXSCALE, "$command 2>&1 |")
   or die "can't get data out of Maxscale: $!";

my $hostname = qx{hostname}; chomp $hostname;
my $waiting_backend = 0;
my $start_output = 0;
my $n_monitors = 0;
my $performance_data="";


my $resource_type = $MAXADMIN_RESOURCE;
chop($resource_type);

my $resource_match = ucfirst("$resource_type Name");

my $this_key;
my %monitor_data;

while ( <MAXSCALE> ) {
    chomp;

    if ( /(Failed|Unable) to connect to MaxScale/ ) {
        printf "CRITICAL: $_\n";
	close(MAXSCALE);
        exit(2);
    }

    if ( /^Monitor\:/ ) {
	$n_monitors++;
	$this_key = 'monitor' . $n_monitors;
	$monitor_data{$this_key} = {
	 '1name'=> '',
	 '2state' => '',
	'3servers' => '',
	 '4interval' => '',
	'5repl_lag' => ''
	};
	
	next;
    }

    next if (/--/ || $_ eq '');

    if ( /Name\:/) {

	my $str;
	my $perf_line;
	my @data_row = split(':', $_);
	my $name = $data_row[1];
	$name =~ s/^\s+|\s+$//g;
	$monitor_data{$this_key}{'1name'}=$name;

    }

    if (/(State\:\s+)(.*)/) {
        $monitor_data{$this_key}{'2state'}=$2;
    }

    if ( /Monitored servers\:/ ) {
        my $server_list;
        my @data_row = split(':', $_);
        shift(@data_row);
        foreach my $name (@data_row) {
            $name =~ s/^\s+|\s+$//g;
            $name =~ s/ //g;
            $server_list .= $name . ":";
        }
        chop($server_list);
        $monitor_data{$this_key}{'3servers'}=$server_list;
    }

    if ( /(Sampling interval\:)\s+(\d+) milliseconds/ ) {
        $monitor_data{$this_key}{'4interval'}=$2;
    }

    if ( /Replication lag\:/ ) {
        my @data_row = split(':', $_);
        my $name = $data_row[1];
        $name =~ s/^\s+|\s+$//g;
        $monitor_data{$this_key}{'5repl_lag'}=$name;
    }
}

for my $key ( sort(keys %monitor_data) ) {
    my $local_hash = {};
    $performance_data .= " $key=";
    $local_hash = $monitor_data{$key};
    my %new_hash = %$local_hash;
    foreach my $key (sort (keys (%new_hash))) {
        $performance_data .= $new_hash{$key} . ";";
    }
    chop($performance_data);
}

if ($n_monitors) {
	printf "OK: %d monitors found |%s\n", $n_monitors, $performance_data;
	close(MAXSCALE);
	exit 0;
} else {
	printf "WARNING: 0 monitors found\n";
	close(MAXSCALE);
	exit 1;
}
