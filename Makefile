all: server

server:
	gcc -Wall main.c serve.c http.c -levent -levent_pthreads -lpthread -o bin/server
