CC     = clang
CFLAGS = -Wall -Wextra -O2 -I.
LIBS   = -lcurl
UNAME  = $(shell uname)

.PHONY: all clean

all: bin/orca

bin/rtti: orca.c
	mkdir -p bin build
	$(CC) $(CFLAGS) -Drtti -o bin/rtti orca.c

build/rtti.h: bin/rtti orca.c
	./bin/rtti orca.c > build/rtti.h

ifeq ($(UNAME),Darwin)
bin/orca: orca.c build/rtti.h
	mkdir -p bin
	$(CC) $(CFLAGS) -arch arm64 -arch x86_64 -o bin/orca orca.c $(LIBS)
else
bin/orca: orca.c build/rtti.h
	mkdir -p bin
	$(CC) $(CFLAGS) -o bin/orca orca.c $(LIBS)
endif

clean:
	rm -rf bin build
