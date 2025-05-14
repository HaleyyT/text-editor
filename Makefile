# TODO: make sure the rules for server client and markdown filled!
CC := gcc
CFLAGS := -Wall -Wextra -std=c11

all: server client

#server: built from server.c + markdown.o
server: server.o markdown.o 
	$(CC) $(CFLAGS) server.o markdown.o -o server 

client: client.o markdown.o
	$(CC) $(CFLAGS) client.o markdown.o -o client

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

markdown.o: markdown.c
	$(CC) $(CFLAGS) -c markdown.c


clean:
	rm -f *.o server client
