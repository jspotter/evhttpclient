CURRENT_DIR = $(shell pwd)
CC = g++
CC_OPTS = -O3 -Wall -ggdb3
CC_LINKS = -lm
LIBRARY = libevhttpclient.so

# libev
EVINC  = /usr/include
EVLIB  = /usr/lib
EVLIBS = -L${EVLIB} -lev

# includes and libs
INCS = -I. -I${EVINC}
LIBS = -L. ${EVLIBS} -Wl,-rpath=${CURRENT_DIR}

all: clean build tests

clean:
	rm -f *.o *.so
	rm -f tests/basic
	rm -f tests/multiple
	rm -f tests/multiple_timeout
	rm -f tests/server

build:
	$(CC) -c -fpic -I. $(INCS) http_parser.c evhttpclient.cpp $(CC_OPTS)
	$(CC) -shared -o $(LIBRARY) http_parser.o evhttpclient.o $(LIBS) $(CC_OPTS) $(CC_LINKS)

tests: tests/basic tests/multiple tests/multiple_timeout tests/server

tests/basic:
	$(CC) $(INCS) -o tests/basic tests/basic.cpp $(LIBS) $(CC_OPTS) $(CC_LINKS) -levhttpclient
	
tests/multiple:
	$(CC) $(INCS) -o tests/multiple tests/multiple.cpp $(LIBS) $(CC_OPTS) $(CC_LINKS) -levhttpclient
	
tests/multiple_timeout:
	$(CC) $(INCS) -o tests/multiple_timeout tests/multiple_timeout.cpp $(LIBS) $(CC_OPTS) $(CC_LINKS) -levhttpclient
	
tests/server:
	$(CC) $(INCS) -o tests/server tests/server.cpp $(LIBS) $(CC_OPTS) $(CC_LINKS) -levhttpclient

