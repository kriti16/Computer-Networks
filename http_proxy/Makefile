CC=g++
CFLAGS= -g -Wall -Werror

all: proxy

proxy: proxy.cpp proxy_parse.cpp proxy_parse.h
	$(CC) $(CFLAGS) -o proxy_parse.o -c proxy_parse.cpp
	$(CC) $(CFLAGS) -o proxy.o -c proxy.cpp
	$(CC) $(CFLAGS) -o proxy proxy_parse.o proxy.o

clean:
	rm -f proxy *.o

tar:
	tar -cvzf ass1.tgz proxy.cpp README Makefile proxy_parse.cpp proxy_parse.h
