#!/bin/bash

# simple script to run the python test_skeeter_notifier

set -x
set -e

PYTHON="python3.2"
SKEETER="${HOME}/skeeter"
export PYTHONPATH="${SKEETER}"
$PYTHON "${SKEETER}/test/test_skeeter_notifyer.py" $1

RETVAL=$?
if [ $RETVAL -eq 0 ]; then
    echo Success 
    exit 0 
fi

echo Failure
exit $?



