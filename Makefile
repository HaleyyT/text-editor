# TODO: make sure the rules for server client and markdown filled!
CC := gcc
CFLAGS := -Wall -Wextra -std=c11

all: server client

#server: built from server.c + markdown.o
server: server.o markdown.o 
	$(CC) $(CFLAGS) server.o markdown.o -o server 

client: client.o markdown.o
	$(CC) $(CFLAGS) client.o markdown.o -o client

server.o: source/server.c
	$(CC) $(CFLAGS) -Ilibs -c source/server.c -o server.o

client.o: source/client.c
	$(CC) $(CFLAGS) -Ilibs -c source/client.c -o client.o

markdown.o: source/markdown.c
	$(CC) $(CFLAGS) -Ilibs -c source/markdown.c -o markdown.o


clean:
	rm -f *.o server client
