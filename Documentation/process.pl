#!/usr/bin/env perl

# Discard the CSV headers
<>;

while (<>)
{
    # Replace commas that are inside double quotes
    while (s/("[^"]*),([^"]*")/$1$2/g)
    {
        ;
    }
    # Replace the double quotes themselves
    s/"//g;

    # Split the line and grab the issue number and description
    my @parts = split(/,/);
    my $issue = @parts[1];
    my $desc = @parts[0];

    print "* [$issue](https://jira.mariadb.org/browse/$issue) $desc\n";
}
