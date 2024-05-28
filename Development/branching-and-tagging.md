# Branching and Tagging

## Overview

This document describes how git branches and tags are used in the MaxScale
development.

## Release Numbering

The releases of MaxScale are numbered as `YY.MM.N` where `YY` is the year
and `MM` the month when the series in question was first released, and `N` a
running number that is increased every time a release is made.

The only difference between two releases sharing the same `YY.MM` part are
bug-fixes. Rarely, a more recent (bigger `N`) release may also contain some
new features. However, bigger architectural changes are _never_ made, once a
series has been released.

At the time of this writing, there are 5 series; `21.06`, `22.08`,
`23.02`, `23.08` and `24.02`.

## Branches

There are two types of series branches in MaxScale; branches named as
`YY.MM`, such  as `21.06` and `22.08`, and the branch `develop`.

The development of the _next_ series takes place in `develop`.

The only development that takes place in an `YY.MM` branch is related to bug
fixes. Rarely, some feature development may also take place.

## Maintenance Releases

When a maintenance release is made, either due to one having been scheduled
or because an urgent fix is needed, a branch `YY.MM.N+1` is created. `N` is
the running number of the latest release. In that branch, all release
related activity takes place. Finally, when the release has been made, that
branch is then merged back to `YY.MM`.

For instance, suppose the latest release of the series `21.06` is
`21.06.16`. When it is time for the next maintenance release, the following
steps will be taken:
```
git checkout 21.06
git checkout -b 21.06.17`
```
In that branch the release notes will be created, the change log updated,
system tests run, and the packages built. Once everything is ready, the
release will be tagged and the branch merged back to the series branch.
```
git checkout 21.06.17
git tag -a maxscale-21.06.17
git checkout 21.06
git merge 21.06.17
```
The branch `21.06` will subsequently be merged to the next series branch,
i.e. `22.08`, and so on, until `develop` is reached.

Thereafter, the branch `21.06.17` is **only** touched, if the documentation
needs to be updated.

## Feature Releases

New features are in general only introduced in a new series and the
development takes place in `develop`.

The procedure is roughly similar to that releated to maintenance releases
but with an inital deviation.

Once the development of the next series is close to readiness, the new
series branch is created and it will be named according to the current
date. For instance, suppose that happens in February 2025. In that case,
the steps will be as follows:
```
git checkout develop
git checkout -b 25.02
git push origin 25.02
```
After this, all commits related to the next series, are pushed to the
branch `25.02`. If there already is development related to the series
following that, those commits are pushed to `develop`.

When `25.02` is ready to be released, a branch `25.02.0` is created and the
release related activities take place there.
