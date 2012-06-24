# -*- coding: utf-8 -*-
"""
test_skeeter_subscriber.py

This is a Python script to test the skeeter program by subscribing to
the PUB socket and reporting notifications.
"""
import logging
import signal
import sys
from threading import Event

import zmq

from rita_skeeter import _load_config

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

    zeromq_context = zmq.Context()

    config = _load_config()

    sub_socket = zeromq_context.socket(zmq.SUB)

    hwm = config.get("hwm")
    if hwm is not None:
        log.info("setting sub_socket HWM to {0}".format(hwm))
        sub_socket.setsockopt(zmq.HWM, hwm)

    log.info("subscribing to heartbeat")
    sub_socket.setsockopt(zmq.SUBSCRIBE, "heartbeat".encode("utf-8"))
    for channel in config["channels"]:
        log.info("subscribing to {0}".format(channel))
        sub_socket.setsockopt(zmq.SUBSCRIBE, channel.encode("utf-8"))

    log.info("connecting sub_socket to {0}".format(config["pub_socket_uri"]))
    sub_socket.connect(config["pub_socket_uri"])

    return_value = 0

    halt_event = Event()
    _set_signal_handler(halt_event)
    while not halt_event.is_set():
        try:
            topic = sub_socket.recv()
            assert sub_socket.rcvmore
            message = sub_socket.recv()
            if sub_socket.rcvmore:
                data = sub_socket.recv()
            else:
                data = ""
        except KeyboardInterrupt:
            log.info("keyboard interrupt")
            halt_event.set()
        except Exception as instance:
            log.exception(str(instance))
            return_value = 1
            halt_event.set()
        else:
            log.info("{0} {1} bytes".format(topic.decode("utf-8"), 
                                            len(message)))

    log.info("program terminates with return_value {0}".format(return_value))
    sub_socket.close()
    zeromq_context.term()

    return return_value

if __name__ == "__main__":
    sys.exit(main())

