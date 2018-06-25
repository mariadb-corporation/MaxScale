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
# @file check_maxscale_threads.pl - Nagios plugin for MaxScale threads and events
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
       -r <resource>	= threads
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
    'r' => 'threads',      # default maxscale resource to show
    'h' => '',             # give help
    'H' => 'localhost',    # host
    'u' => 'root',         # username
    'm' => '/usr/local/mariadb-maxscale/bin/maxadmin',   # maxadmin
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

open (MAXSCALE, "$command 2>&1 |") or die "can't get data out of Maxscale: $!";

my $hostname = qx{hostname}; chomp $hostname;
my $start_output = 0;
my $n_threads = 0;
my $p_threads = 0;
my $performance_data="";


my $resource_type = $MAXADMIN_RESOURCE;
chop($resource_type);

my $resource_match = ucfirst("$resource_type Name");

my $historic_thread_load_average = 0;
my $current_thread_load_average = 0;

my %thread_data;
my %event_data;

my $start_queue_len = 0;

while ( <MAXSCALE> ) {
    chomp;

    if ( /(Failed|Unable) to connect to MaxScale/ ) {
        printf "CRITICAL: $_\n";
	close(MAXSCALE);
        exit(2);
    }

	if ( /Historic Thread Load Average/) {
                my $str;
                my @data_row = split(':', $_);
                foreach my $val (@data_row) {
                        $str = $val;
                        $str =~ s/^\s+|\s+$//g;
                }
		chop($str);
                $historic_thread_load_average = $str;
	}

	if (/Current Thread Load Average/) {
                my $str;
                my @data_row = split(':', $_);
                foreach my $val (@data_row) {
                        $str = $val;
                        $str =~ s/^\s+|\s+$//g;
                }
		chop($str);
                $current_thread_load_average = $str;
	}

	if (/Minute Average/) {
                my $str;
		my $in_str;
                my @data_row = split(',', $_);
                foreach my $val (@data_row) {
			my ($i,$j)= split(':', $val);
                       	$i =~ s/^\s+|\s+$//g;
                       	$j =~ s/^\s+|\s+$//g;
			if ($start_queue_len) {
				$event_data{$i} = $j;
			} else {
				$thread_data{$i} = $j;
			}
		}
	}

	if ( /Pending event queue length averages/) {
		$start_queue_len = 1;
		next;
	}

    if (/^\s+ID/ ) {
	$start_output = 1;
	next;
    }

    if ($start_output && /^\s+\d/) {
	$n_threads++;
	if (/Processing/) {
		$p_threads++;
	}
    }
}

close(MAXSCALE);

$command = "ssh -i " . $MAXSCALE_HOST_IDENTITY_FILE . ' ' . $opts{'u'} . '@' . $opts{'H'} . ' ' . $MAXADMIN . $MAXADMIN_SOCKET . ' ' . " show epoll";

open (MAXSCALE, "$command 2>&1 |") or die "can't get data out of Maxscale: $!";

my $queue_len = 0;

while ( <MAXSCALE> ) {
	chomp;

	if ( /(Failed|Unable) to connect to MaxScale/ ) {
		printf "CRITICAL: $_\n";
		close(MAXSCALE);
		exit(2);
	}

	if ( ! /Current event queue length/ ) {
		next;
	} else {
		my $str;
		my @data_row = split(':', $_);
		foreach my $val (@data_row) {
			$str = $val;
			$str =~ s/^\s+|\s+$//g;
		}
		$queue_len = $str;

		last;
	}
}

my $performance_data_thread = "";
my $performance_data_event = "";

my $in_str;
my $in_key;
my $in_val;

my @new_thread_array = @thread_data{'15 Minute Average', '5 Minute Average', '1 Minute Average'};
my @new_event_array = @event_data{'15 Minute Average', '5 Minute Average', '1 Minute Average'};

$performance_data_thread = join(';', @new_thread_array);
$performance_data_event = join(';', @new_event_array);

$performance_data .= "threads=$historic_thread_load_average;$current_thread_load_average avg_threads=$performance_data_thread avg_events=$performance_data_event";

if (($p_threads < $n_threads) || ($n_threads == 1)) {
	printf "OK: Processing threads: %d/%d Events: %d | $performance_data\n", $p_threads, $n_threads, $queue_len;
	close(MAXSCALE);
	exit 0;
} else {
	printf "WARNING: Processing threads: %d/%d Events: %d | $performance_data\n", $p_threads, $n_threads, $queue_len;
	close(MAXSCALE);
	exit 1;
}

