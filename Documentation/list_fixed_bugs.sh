#!/bin/bash


if [ $# -ne 1 ]
then
    echo "USAGE: $0 VERSION"
    exit 1
fi

# Script location
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

version=$1
curl -s "https://jira.mariadb.org/sr/jira.issueviews:searchrequest-csv-current-fields/temp/SearchRequest.csv?jqlQuery=project+%3D+MXS+AND+issuetype+%3D+Bug+AND+status+%3D+Closed+AND+fixVersion+%3D+$version" | $DIR/process.pl
