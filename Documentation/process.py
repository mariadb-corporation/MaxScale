#!/usr/bin/env python3

import sys
import csv
import itertools

# Loop over issues. If an issue has a label that starts with 'CVE-',
# assume the label is a CVE id. If an issue has multiple CVE labels,
# the issue will be added multiple times (by design).
#
def find_cves(issues):
    cves=[]

    for i in issues:
        labels_field = i.get('Labels')
        if labels_field:
            labels=labels_field.split(',')
            for label in labels:
                if label[0:4] == 'CVE-':
                    cve = {};
                    cve['Id'] = label
                    cve['Issue'] = i
                    cves.append(cve)

    return cves

def print_cves(header, cves):
    print(header)
    print()

    for cve in cves:
        id = cve['Id']
        i = cve['Issue']
        print("* [" + id + "](https://www.cve.org/CVERecord?id=" + id + ") Fixed by [" + i['Issue key'] + "](https://jira.mariadb.org/browse/" + i['Issue key'] + ") " + i['Summary'])

    print()


bugs = []
new_features = []
tasks = []

reader = csv.reader(sys.stdin.readlines())
field_names = next(reader)

for row in reader:
    # In case there are multiple values of a particular field, collect
    # all values separated by a ','.
    groups = itertools.groupby(zip(field_names, row), key=lambda x: x[0])
    row = dict([(k, ','.join([v[1] for v in g])) for k, g in groups])

    if row['Issue Type'] == 'Bug':
        bugs.append(row)
    elif row['Issue Type'] == 'New Feature':
        new_features.append(row)
    elif row['Issue Type'] == 'Task':
        tasks.append(row)

# Check if some bug-fix fixes a CVE. These are assumed to be CVEs of MaxScale.
cves = find_cves(bugs)

if len(cves) > 0:
    print_cves("## CVEs resolved.", cves)

# If there are tasks, check if any of them fixes a CVE, which is assumed
# to be a non-MaxScale one; e.g. a CVE of an external library.
if len(tasks) > 0:
    cves = find_cves(tasks)

    if len(cves) > 0:
        print_cves("## External CVEs resolved.", cves)

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
