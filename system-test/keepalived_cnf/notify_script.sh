#!/bin/bash

TYPE=$1
NAME=$2
STATE=$3

OUTFILE=/tmp/state.txt
touch $OUTFILE
#OUTFILE=/home/vagrant/state.txt

case $STATE in
  "MASTER") echo "Setting this MaxScale node to active mode $TYPE $NAME $STATE" > $OUTFILE
                  maxctrl alter maxscale passive false
                  exit 0
                  ;;
  "BACKUP") echo "Setting this MaxScale node to passive mode $TYPE $NAME $STATE" > $OUTFILE
                  maxctrl alter maxscale passive true
                  exit 0
                  ;;
  "FAULT")  echo "MaxScale failed the status check. $TYPE $NAME $STATE" > $OUTFILE
                  maxctrl alter maxscale passive true
                  exit 0
                  ;;
        *)        echo "Unknown state $TYPE $NAME $STATE" > $OUTFILE
                  exit 1
                  ;;
esac
