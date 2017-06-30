#!/bin/bash

./non_native_setup insertstream
./mysqltest_driver.sh insertstream insertstream 4006
