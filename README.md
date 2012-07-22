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

Acknowledgements
----------------

This code is heavily influenced by Zed Shaw's ['Learn C the Hard Way'](http://c.learncodethehardway.org/book/)

dbg.h is copied directly from LCTHW also, the use of [bstrings](http://bstring.sourceforge.net/)

You should read the truly excellent [0mq Guide](http://zguide.zeromq.org/)

Contact us
----------

This is an open source project from [SpiderOak](https://SpiderOak.com)

send mail to Doug Fort dougfort@spideroak.com

