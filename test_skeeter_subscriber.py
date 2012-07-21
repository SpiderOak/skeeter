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
import time

import zmq

from rita_skeeter import _load_config

class SequenceError(Exception):
    pass

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

def _process_one_event(expected_sequence, topic, meta, data):
    log = logging.getLogger("event")

    meta_dict = dict()
    for entry in meta.strip().split(";"):
        [key, value] = entry.split("=")
        meta_dict[key.strip()] = value.strip()

    # every event should have a sequence and a timestamp
    meta_dict["timestamp"] = time.ctime(int(meta_dict["timestamp"]))
    meta_dict["sequence"] = int(meta_dict["sequence"])

    if topic == "heartbeat":
        connect_time = int(meta_dict["connected"])
        if connect_time == 0:
            connect_str = "*not connected*"
        else:
            connect_str = time.ctime(connect_time)
        line = "{0:30} {1:20} {2:8} connected={3}".format(
            meta_dict["timestamp"], 
            topic, 
            meta_dict["sequence"], 
            connect_str)
    else:
        line = "{0:30} {1:20} {2:8} data_bytes={3}".format(
            meta_dict["timestamp"], 
            topic, 
            meta_dict["sequence"], 
            len(data))

    log.info(line)

    if expected_sequence[topic] is None:
        expected_sequence[topic] = meta_dict["sequence"] + 1
    elif meta_dict["sequence"] == expected_sequence[topic]:
        expected_sequence[topic] += 1
    else:
        message = \
            "{0} out of sequence expected {1} found {2}".format(
                topic, expected_sequence[topic], meta_dict["sequence"])
        raise SequenceError(message)

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

    expected_sequence = dict()

    log.info("subscribing to heartbeat")
    sub_socket.setsockopt(zmq.SUBSCRIBE, "heartbeat".encode("utf-8"))
    expected_sequence["heartbeat"] = None

    for channel in config["channels"]:
        log.info("subscribing to {0}".format(channel))
        sub_socket.setsockopt(zmq.SUBSCRIBE, channel.encode("utf-8"))
        expected_sequence[channel] = None

    log.info("connecting sub_socket to {0}".format(config["pub_socket_uri"]))
    sub_socket.connect(config["pub_socket_uri"])

    return_value = 0

    halt_event = Event()
    _set_signal_handler(halt_event)
    while not halt_event.is_set():
        try:
            topic_bytes = sub_socket.recv()
            topic = topic_bytes.decode("utf-8")
            assert sub_socket.rcvmore
            meta_bytes = sub_socket.recv()
            meta = meta_bytes.decode("utf-8")
            if sub_socket.rcvmore:
                data_bytes = sub_socket.recv()
                data = data_bytes.decode("utf-8")
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
            _process_one_event(expected_sequence, topic, meta, data)

    log.info("program terminates with return_value {0}".format(return_value))
    sub_socket.close()
    zeromq_context.term()

    return return_value

if __name__ == "__main__":
    sys.exit(main())

