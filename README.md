Skeeter
=======

Synopsis
--------

This program is named after the ubiquitous reporter Rita Skeeter from the 
Harry Potter novels. Like Rita, this program will LISTEN (to postgres) and
PUBlish (to zeromq).

We at SpiderOak developed this to reduce the number of open database 
connectons.  Instead of every program opening a database connection to 
listen for Postgresql notifications, they can subscribe to a channel
on the PUB server. 

Our servers are currently  running Ubuntu 10.4 LTS. Skeeter is mainstream code, 
so it should run on any reasonably current Linux.

Usage
-----

> `skeeter [-c=<path-to-config-file>]`

skeeter will look for the config file at `${HOME}/.skeeterrc` by default.

Building
--------

Development build:
> `make dev`

Production build
> `make`

The development build is good for use with [valgrind](http://valgrind.org/)

Dependencies
------------

We use Postgresql 9.x. In addition to the server, you will need the development
library. On Ubuntu: `sudo apt-get install libpq-dev`

We use a simple ZeroMQ PUB server. You will need the ZeroMQ development
library. On Ubuntu: `sudo apt-get install libzmq-dev`.

Configuration
-------------

Skeeter uses a config file to define parameters that are site specific, or
tunable. By default the config file is located at `${HOME}/.skeeterrc`
but you can override this from the command line.

We have included a sample config file, `skeeterrc` with comments 
documentating the options.

Testing/Example Code
--------------------

We don't have any unit tests (sorry Zed). We do have a test framework 
consisting of two python programs:

* `test_skeeter_notifyer.py`

    This program reads the skeeterrc config file and posts random
    notifications to the database.

* `test_skeeter_subscriber.py`

    This program reads the skeeterrc config file and subscribes to the 
    0mq PUB socket. It logs reported events to stdout. 

Acknowledgements
----------------

This code is heavily influenced by Zed Shaw's ['Learn C the Hard Way'](http://c.learncodethehardway.org/book/)

dbg.h is copied directly from LCTHW also, the use of [bstrings](http://bstring.sourceforge.net/)

You should read the truly excellent [0mq Guide](http://zguide.zeromq.org/)

Contact us
----------

This is an open source project from [SpiderOak](https://SpiderOak.com)

send mail to Doug Fort dougfort@spideroak.com

