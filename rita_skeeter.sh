#!/bin/bash

# simple script to run the python rita_skeeter prototype

set -x
set -e

PYTHON="python3.2"

export PGDATABASE="postgres"

$PYTHON "${HOME}/skeeter/rita_skeeter.py" $1 $2 $3 $4

RETVAL=$?
if [ $RETVAL -eq 0 ]; then
    echo Success 
    exit 0 
fi

echo Failure
exit $?



