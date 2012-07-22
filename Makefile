# get the Postgresql libpq locations in a portable way
PG_CONFIG = pg_config
PG_INCLUDEDIR := $(shell $(PG_CONFIG) --includedir)
PG_LIBDIR := $(shell $(PG_CONFIG) --libdir)

CFLAGS=-g -O2 -Wall -Wextra -Isrc -I$(PG_INCLUDEDIR) -DNDEBUG $(OPTFLAGS)

SOURCES=$(wildcard src/*.c)
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

TARGET=skeeter

all: $(TARGET)

skeeter: $(OBJECTS)
	$(CC) -o $(TARGET) $(OBJECTS) -L$(PG_LIBDIR) -lzmq -lpq

dev: CFLAGS=-g -Wall -Isrc -I$(PG_INCLUDEDIR) -Wall -Wextra $(OPTFLAGS)
dev: all

clean:
	rm -f $(OBJECTS)
	rm -f $(TARGET)

