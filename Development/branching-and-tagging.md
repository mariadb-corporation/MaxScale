# Branching and Tagging

## Overview

This document describes how git branches and tags are used in the MaxScale
development.

## Releases

The releases of MaxScale are numbered as `X.Y.Z` where `X` denotes the
major version (currently is `2` and changes very rarely), where `Y` denotes
the minor version (when changed, implies the introduction of new features),
and where `Z` denotes the maintenance version (when changed, implies that
bugs have been fixed).

When `X` changes, `Y` is reset to 0 and when `Y` changes, `Z` is reset to
0.

## Branches

## Two kinds

There are two kinds of minor branches in MaxScale; branches named as
`X.Y`, such as `2.1` and `2.2` and the branch `develop`.

The development of the _next_ minor release takes place in `develop`.

The only development that takes place in an `X.Z` branch are development
related to bug fixes. Occasionally, due to explicit customer demand,
feature development may also take place.

## Maintenance Releases

Suppose the last released version is `2.1.6`.

Any bug fixes related to version `2.1.6` are now pushed to `2.1`. When
there are "enough" bug fixes, or at least one and enough time has passed
since the last release, a new tag is created.
```
git checkout 2.1
git tag -a maxscale-2.1.7-tt1
git push --tags origin
```
The suffix `-tt1` stands for _tentative tag 1_, because before the
test-suite has been run and all packages have been built (using that tag),
we do not know with certainty whether that commit actually will be the
release `2.1.7`.

If there are problems, then those are fixed and a new tag
`maxscale-2.1.7-tt2` is created. That continues until there are no issues
left. Typically a few iterations are needed.

Once all is green, the final tag is created.
```
git checkout maxscale-2.1.7-tt2`
git tag -a maxscale-2.1.7
```
but the packages are not rebuilt. At this point, the tentative tags are
removed:
```
git tag -d maxscale-2.1.7-tt1
git tag -d maxscale-2.1.7-tt2
git push origin :refs/tags/maxscale-2.1.7-tt1
git push origin :refs/tags/maxscale-2.1.7-tt2
```
At this point, the _branch_ `2.1.7` is also created:
```
git checkout maxscale-2.1.7
git checkout -b 2.1.7
git push origin 2.1.7
```
This branch is **only** used for updating the documentation, if there is
an urgent need for doing that. There is always one need; once the packages
have been uploaded to the download site and we know the exact date when
they will be made available, the release date is added in the release notes.
```
git checkout 2.1.7
# Update the release date
git add Documentation/Release-Notes/MaxScale-2.1.7-Release-Notes.md
git commit
git push origin 2.1.7
```
The next step is to merge that branch into the corresponding minor branch.
```
git checkout 2.1
git merge 2.1.7
```
Now the updated minor branch should be merged upwards until we reach
`develop`.
```
git checkout 2.2
git merge 2.1
git checkout develop
git merge 2.2
```

## Feature Releases

Feature releases are always the next minor version and the development
takes place in `develop`.

The procedure is roughly similar to that releated to maintenance releases
but with an inital deviation.

Once the development of the next minor release is close to readiness, the
new minor release branch is created.
```
git checkout develop
git checkout -b 2.3
git push origin 2.3
```
After this, all commits related to the next release, are pushed to the
branch `2.3`. If there already is development related to the minor release
following that, those commits are pushed to `develop`.

Hereafter the procedure is exactly like the one of a maintenance
release. The first tag will be `maxscale-2.3.0-tt1`, the final tag will be
`maxscale-2.3.0` and the branch `2.3.0`.
