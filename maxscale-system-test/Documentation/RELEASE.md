# Release process

## Pre-release Checklist

* Create new release notes and add all fixed bugs, use a previous one as a template
* Add link to release notes and document major changes in Changelog.md

## 1. Tag

Release builds are always made using a tag. However, the used
tag is a _tentative_ tag, to ensure that there never is a need
to _move_ any tag, should the release have to be modified after
it has been tagged. All that is needed is to create a new
tentative tag.

The source for release `x.y.z` is tagged with `maxscale-x.y.z-ttN`
where `N` is 1 for the first attempt and incremented in case the
`x.y.z` source still needs to be modified and the release rebuilt.

The final tag `maxscale-x.y.z` is created _after_ the packages have
been published and we are certain they will not be rebuilt, without
also the version number being changed.

So, ensure that the `maxscale-x.y.z-ttN` has been created and pushed
to the repository.

```
git push origin refs/tags/maxscale-x.y.z-ttN
```

**NOTE** The tentative suffix - `-ttN` - is **only** used when
specifying the tag for the build, it must **not** be present in
any other names or paths.

## 2. Build and upgrade test

The Jenkins
[build_for_release](http://127.0.0.1:8089/job/build_for_release/)
job should be used for building the packages.

Note that the above will not work unless you have set up an
ssh tunnel to Jenkins:
```
$ ssh -f -N -L 8089:127.0.0.1:8089 vagrant@max-tst-01.mariadb.com
```

### Parameters to define

#### `scm_source`

This is the tag that is used to build the release.

```
refs/tags/maxscale-x.y.z-ttN
```
#### `version_number`

The version number of this release in x.y.z format. This will create two packages; maxscale-x.y.z-release and maxscale-x.y.z-debug.

```
x.y.z
```

#### `old_target`

The previous released version, used by upgrade tests.

```
x.y.z
```

### 1.4.x build

Use the [build_all](http://127.0.0.1:8089/job/build_all/) job.

For `1.4` builds the default values of the following parameters
should be changed:

#### use_mariadbd

```
yes
```

#### cnf_file

```
maxscale.cnf.minimum.1.4.4
```

#### maxadmin_command

```
maxadmin -pmariadb show services
```

## 3. Copying to code.mariadb.com

ssh to `code.mariadb.com` with your LDAP credentials.

Create directories and copy repositories files:

```bash
cd  /home/mariadb-repos/mariadb-maxscale/
mkdir x.y.z
mkdir x.y.z-debug
cd x.y.z
rsync -avz  --progress --delete -e ssh vagrant@max-tst-01.mariadb.com:/home/vagrant/repository/maxscale-x.y.z-release/mariadb-maxscale/ .
cd ../x.y.z-debug
rsync -avz  --progress --delete -e ssh vagrant@max-tst-01.mariadb.com:/home/vagrant/repository/maxscale-x.y.z-debug/mariadb-maxscale/ .
```

## 4. Email webops-requests@mariadb.com

Email example:

Subject: `MaxScale x.y.z release`

```
Hello,

Please publish Maxscale x.y.z binaries on web page.
Repos are on code.mariadb.com /home/mariadb-repos/mariadb-maxscale/x.y.z

symlink 'x.y' should be set to 'x.y.z'
symlink 'latest' [should|should NOT] be set to 'x.y.z'

Also please make sure that debug binaries are not visible from
https://mariadb.com/my_portal/download/maxscale
```
Replace `x.y.z` with the correct version.

**NOTE** Sometimes - especially at _big_ releases when the exact release
date is fixed in advance - the following steps 5, 6 and 7 are done right
after the packages have been uploaded, to ensure that the steps 4 and 8
can be done at the same time.

## 5. Create the final tag

Once the packages have been made available for download, create
the final tag
```
$ git checkout maxscale-x.y.z-ttN
$ git tag -a -m "Tag for MaxScale x.y.z" maxscale-x.y.z
$ git push origin refs/tags/maxscale-x.y.z
```
and remove the tentative tag(s)
```
$ git tag -d maxscale-x.y.z-ttN
$ git push origin :refs/tags/maxscale-x.y.z-ttN
```

## 6. Create the branch

Release `x.y.z` is typically developed in the branch `x.y`.
Once `x.y.z` has been released, the branch `x.y.z` also needs
to be created.
```
$ git checkout maxscale-x.y.z
$ git checkout -b x.y.z
$ git push origin x.y.z
```

## 7. Update the release date

Once the branch `x.y.z` has been created and the actual release
date of the release is known, update the release date in the
release notes.
```
$ git checkout x.y.z
$ # Update release date in .../MaxScale-x.y.z-Release-Notes.md
$ git add .../MaxScale-x.y.z-Release-Notes.md
$ git commit -m "Update release date"
$ git push origin x.y.z
```

**NOTE** The `maxscale-x.y.z` tag is **not** moved. That is, the
release date is _not_ available in the documentation marked with
the tag `maxscale-x.y.z` but _is_ available in the branch marked
with `x.y.z`.

Merge `x.y.z` to `x.y`.
```
$ git checkout x.y
$ git merge x.y.z
```

At this point, the last commits on branches `x.y` and `x.y.z`
should be the same and the commit should be the one containing the
update of the release date. Further, that commit should be the only
difference between the branches and the tag `maxscale-x.y.z`.
**Check that indeed is the case**.

## 8. Update documentation

Email webops-requests@mariadb.com with a mail containing the
following.
```
subject: Please update MaxScale x.y knowledge base
body:
---
Hi,

Please update https://mariadb.com/kb/en/mariadb-enterprise/mariadb-maxscale-XY/

from https://github.com/mariadb-corporation/MaxScale/tree/x.y.z/Documentation

using the "new" algorithm that does not honor single line-breaks.

Br,
  Your Name
```
## 9. Send release email to mailing list

Email maxscale@googlegroups.com with a mail containing the following.
```
subject: MariaDB MaxScale x.y.z available for download
body:
---
Hi,

We are happy to announce that MariaDB MaxScale x.y.z GA is now available for download. This is a bugfix release.

The Jira list of fixed issues can be found here(ADD LINK HERE).

* [MXS-XYZ] BUG FIX DESCRIPTION HERE

Binaries:
https://mariadb.com/downloads/maxscale
https://mariadb.com/my_portal/download

Documentation:
LINK TO KB DOCUMENTATION HERE

Release notes:
LINK TO RELEASE NOTES HERE

Source code:
LINK TO maxscale-x.y.z TAG HERE

Please report any issues on Jira:
https://jira.mariadb.org/projects/MXS/issues

On behalf of the entire MaxScale team,

YOUR NAME HERE
```
