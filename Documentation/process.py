#!/usr/bin/env python3

import sys
import csv

bugs = []
new_features = []

for row in csv.DictReader(sys.stdin):
    if row['Issue Type'] == 'Bug':
        bugs.append(row)
    elif row['Issue Type'] == 'New Feature':
        new_features.append(row)

if len(new_features) > 0:
    print("## New Features")
    print()

    for f in new_features:
        print("* [" + f['Issue key'] + "](https://jira.mariadb.org/browse/" + f['Issue key'] + ") " + f['Summary'])
    print()


print("## Bug fixes")
print()

for b in bugs:
    print("* [" + b['Issue key'] + "](https://jira.mariadb.org/browse/" + b['Issue key'] + ") " + b['Summary'])

print()
