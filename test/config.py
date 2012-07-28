# -*- coding: utf-8 -*-
"""
config.py

parse skeeterrc so the test programs use the same parameters as skeeter
"""
import logging
import os.path
import sys

class ConfigError(Exception):
    pass

_default_path = os.path.expanduser("~/.skeeterrc")
_postgresql_tag = "postgresql-"

def load_config():
    """
    load the config file, either from a path specified on the comandline
    or from the default location
    """
    log = logging.getLogger("load_config")
    if len(sys.argv) == 1:
        config_path = _default_path
    elif len(sys.argv) == 2:
        config_path = sys.argv[1]
    elif len(sys.argv) == 3 and sys.argv[1] == "-c":
        config_path = sys.argv[2]
    else:
        raise ConfigError("unparsable commandline {0}".format(sys.argv))

    config = {"database-credentials" : dict()}

    log.info("reading config from {0}".format(config_path))
    for line in open(config_path):
        line = line.strip()
        if len(line) == 0 or line.startswith("#"):
            continue
        key, value = line.split("=")
        key = key.strip()
        value = value.strip()
        
        if key.startswith(_postgresql_tag):
            key = key[len(_postgresql_tag):]
            if key == "dbname":
                key = "database"
            config["database-credentials"][key] = value
        elif key == "channels":
            config[key] = [c.strip() for c in value.split(",")]
        else:
            config[key] = value

    return config

