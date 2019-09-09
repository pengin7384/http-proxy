CC = gcc
TARGET = proxy
FLAGS = -pthread

$(TARGET) : main.o proxy.o proxy_util.o
	$(CC) -o $(TARGET) main.o proxy.o proxy_util.o $(FLAGS)

main.o : main.c
	$(CC) -c -o main.o main.c

proxy.o : proxy.c
	$(CC) -c -o proxy.o proxy.c $(FLAGS)

proxy_util.o : proxy_util.c
	$(CC) -c -o proxy_util.o proxy_util.c

clean : 
	rm *.o proxy



