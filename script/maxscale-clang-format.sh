#!/bin/bash

included_dirs="server include maxutils query_classifier"
excluded_dirs="sqlite-src-3110100"

./list-src -i "$included_dirs" -x "$excluded_dirs" .. | xargs clang-format -style=file -i
