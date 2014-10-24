#!/usr/bin/perl

my $host = $ENV{'repl_000'};
my $port = $ENV{'repl_port_000'};
my $user = $ENV{'repl_User'};
my $password = $ENV{'repl_Password'};

use strict;
use DBI;

my $dsn = "DBI:mysql:database=test;host=$host;port=$port;mysql_use_result=0;mysql_server_prepare=1";
my $dbh = DBI->connect($dsn, $user, $password) or die "Failed to connect!";
my $sth = $dbh->prepare("SELECT id, \@\@server_id from test.t1 where id=(?)");

$sth->bind_param(1, "%");  # placeholders are numbered from 1

for (my $i=0; $i<100000; $i++) {
    print "$i\n";
    if (defined($sth)) {
        $sth->execute($i) or warn "Did not execute successfully: ".$dbh->errstr;
        #DBI::dump_results($sth);
    }
}

$sth->finish();
$dbh->disconnect() or warn "Did not successfully disconnect from backend!";


