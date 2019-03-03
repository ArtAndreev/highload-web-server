all: server

server:
	gcc -Wall main.c serve.c http.c -o bin/server
