all: test_main server client test_client test_server

test_main: test_main.cpp libmap.a goldchase.h
	g++ test_main.cpp -o test_main -L. -lpthread -lmap -lpanel -lncurses -lrt

server: server.cpp test_main.cpp libmap.a goldchase.h
	g++ server.cpp -o server -L. -lpthread -lmap -lpanel -lncurses -lrt

client: client.cpp test_main.cpp libmap.a goldchase.h
	g++ client.cpp -o client -L. -lpthread -lmap -lpanel -lncurses -lrt

test_client: test_client.cpp
	g++ -o test_client test_client.cpp -lpthread -lrt

test_server: test_server.cpp
	g++ -o test_server test_server.cpp -lpthread -lrt

libmap.a: Screen.o Map.o
	ar -r libmap.a Screen.o Map.o

Map.o: Map.cpp Map.h
	g++ -c Map.cpp

clean:
	rm -f Screen.o Map.o libmap.a test_main server client test_client test_server
