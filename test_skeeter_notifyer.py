# -*- coding: utf-8 -*-
"""
test_skeeter_notifier.py

This is a Python script to test the skeeter program.

It will call pg_notify in the database so the subscriber can report
"""
import logging
import random
import signal
import sys
from threading import Event

import psycopg2

from rita_skeeter import _load_config

_low_delay = 0.0
_high_delay = 3.0
_low_data_size = 0
_high_data_size = 8000

def _initialize_logging():
    handler = logging.StreamHandler()
    formatter = logging.Formatter(
        '%(asctime)s %(levelname)-8s %(name)-20s: %(message)s')
    handler.setFormatter(formatter)
    logging.root.addHandler(handler)
    logging.root.setLevel(logging.DEBUG)
    
def _create_signal_handler(halt_event):
    def cb_handler(*_):
        halt_event.set()
    return cb_handler

def _set_signal_handler(halt_event):
    """
    set a signal handler to set halt_event when SIGTERM is raised
    """
    signal.signal(signal.SIGTERM, _create_signal_handler(halt_event))

def main():
    """
    main entry point

    returns 0 for success
            1 for failure
    """
    _initialize_logging()
    log = logging.getLogger("main")
    log.info("program starts")

    config = _load_config()

    database_connection = \
        psycopg2.connect(database=config["postgresql-dbname"])

    return_value = 0

    halt_event = Event()
    _set_signal_handler(halt_event)
    while not halt_event.is_set():
        channel = random.choice(config["channels"])
        data_size = random.randint(_low_data_size, _high_data_size-1)
        data = 'a' * data_size
        log.info("notifying {0} with {1} bytes".format(channel, data_size))
        try:
            cursor = database_connection.cursor()
            cursor.execute("select pg_notify(%s, %s);", [channel, data, ])
            cursor.close()
            database_connection.commit()
        except KeyboardInterrupt:
            log.info("keyboard interrupt")
            halt_event.set()
        except Exception as instance:
            log.exception(str(instance))
            return_value = 1
            halt_event.set()
        else:
            halt_event.wait(random.uniform(_low_delay, _high_delay))

    log.info("program terminates with return_value {0}".format(return_value))
    database_connection.close()

    return return_value

if __name__ == "__main__":
    sys.exit(main())

