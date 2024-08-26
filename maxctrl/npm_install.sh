#!/bin/bash

cd "$PWD/maxctrl" || exit 1

# The pipefail option will cause the command to return the exit code of the
# first failing command in the pipeline rather than the default of returning the
# exit code of the last command in the pipeline.
set -o pipefail

# Piping the output through `tee` works around a problem in npm where it always
# prints verbose output: https://github.com/npm/cli/issues/3314
npm install --production --no-audit --no-fund |& tee
