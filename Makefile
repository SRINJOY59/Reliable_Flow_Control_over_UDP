CC = gcc
LIBRARY = libksocket.a

all: $(LIBRARY) initksocket user1 user2
#run2 should be executed before run1
run1: user1
	./user1 127.0.0.1 8081 127.0.0.1 5076

run2: user2
	./user2 127.0.0.1 5076 127.0.0.1 8081





$(LIBRARY): ksocket.o
	ar rcs $(LIBRARY) ksocket.o

user1: user1.o $(LIBRARY)
	$(CC) -o user1 user1.o -L. -lksocket 

user1.o: user1.c ksocket.h
	$(CC) -c user1.c

user2: user2.o $(LIBRARY)
	$(CC) -o user2 user2.o -L. -lksocket 

user2.o: user2.c ksocket.h
	$(CC) -c user2.c

initksocket: initksocket.o $(LIBRARY)
	$(CC) -o initksocket initksocket.o -L. -lksocket -pthread

initksocket.o: initksocket.c ksocket.h
	$(CC) -c initksocket.c

ksocket.o: ksocket.c ksocket.h
	$(CC) -c ksocket.c

clean:
	rm -f *.o user1 user2 $(LIBRARY) initksocket
