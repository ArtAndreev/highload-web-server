all: server

server:
	gcc -Wall -Wextra -Werror -Iinclude -levent -levent_pthreads -lpthread \
		src/main.c src/serve.c src/http.c src/buffer.c src/file.c -o bin/server
