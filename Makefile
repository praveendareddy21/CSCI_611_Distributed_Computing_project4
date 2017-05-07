all: goldchase

goldchase: test_main.cpp libmap.a goldchase.h
	g++ test_main.cpp -o goldchase -L. -lpthread -lmap -lpanel -lncurses -lrt


libmap.a: Screen.o Map.o
	ar -r libmap.a Screen.o Map.o

Map.o: Map.cpp Map.h
	g++ -c Map.cpp

clean:
	rm -f Screen.o Map.o libmap.a goldchase
	
