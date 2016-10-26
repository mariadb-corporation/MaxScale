#!/usr/bin/perl

my $host = $ENV{'node_000_network'};
my $port = $ENV{'node_000_port'};
my $user = $ENV{'node_user'};
my $password = $ENV{'node_password'};
my $test_dir = $ENV{'test_dir'};

use strict;
use DBI;

my $dsn = "DBI:mysql:database=test;host=$host;port=$port;mysql_use_result=0;mysql_server_prepare=1;mysql_ssl_client_key=$test_dir/ssl-cert/client-key.pem;mysql_ssl_client_cert=$test_dir/ssl-cert/client-cert.pem;";
my $dbh = DBI->connect($dsn, $user, $password) or die "Failed to connect!";
my $sth = $dbh->prepare("SELECT id, \@\@server_id from test.t1 where id=(?)");

$sth->bind_param(1, "%");  # placeholders are numbered from 1

for (my $i=0; $i<100000; $i++) {
    if ($i % 5000 == 0) {
        print "$i\n";
    }
    if (defined($sth)) {
        $sth->execute($i) or warn "Did not execute successfully: ".$dbh->errstr;
        #DBI::dump_results($sth);
    }
}

$sth->finish();
$dbh->disconnect() or warn "Did not successfully disconnect from backend!";


