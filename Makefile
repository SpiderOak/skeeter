CFLAGS=-g -O2 -Wall -Wextra -Isrc -DNDEBUG $(OPTFLAGS)

SOURCES=$(wildcard *.c)
OBJECTS=$(patsubst %.c,%.o,$(SOURCES))

TARGET=skeeter

all: $(TARGET)

skeeter: $(OBJECTS)
	$(CC) -o $(TARGET) $(OBJECTS) -lzmq

dev: CFLAGS=-g -Wall -Isrc -Wall -Wextra $(OPTFLAGS)
dev: all

clean:
	rm -f $(OBJECTS)
	rm -f $(TARGET)

