CC = gcc
FLAGS = -Wall -g3
TARGETS = master worker whoServer whoClient
SRC_DIR = ./src

all:$(TARGETS)

master:master.o utils.o hashtable.o list.o
	$(CC) $(FLAGS) -o master master.o utils.o hashtable.o list.o

worker:worker.o list.o hashtable.o patientRecord.o avltree.o utils.o
	$(CC) $(FLAGS) -o worker worker.o list.o hashtable.o patientRecord.o avltree.o utils.o

whoServer:whoServer.o utils.o
	$(CC) $(FLAGS) -o whoServer whoServer.o utils.o -lpthread

whoClient:whoClient.o list.o utils.o
	$(CC) $(FLAGS) -o whoClient whoClient.o list.o utils.o -lpthread

master.o:$(SRC_DIR)/master.c
	$(CC) $(FLAGS) -o master.o -c $(SRC_DIR)/master.c

worker.o:$(SRC_DIR)/worker.c
	$(CC) $(FLAGS) -o worker.o -c $(SRC_DIR)/worker.c

whoServer.o:$(SRC_DIR)/whoServer.c
	$(CC) $(FLAGS) -o whoServer.o -c $(SRC_DIR)/whoServer.c

whoClient.o:$(SRC_DIR)/whoClient.c
	$(CC) $(FLAGS) -o whoClient.o -c $(SRC_DIR)/whoClient.c

list.o:$(SRC_DIR)/list.c
	$(CC) $(FLAGS) -o list.o -c $(SRC_DIR)/list.c

hashtable.o:$(SRC_DIR)/hashtable.c
	$(CC) $(FLAGS) -o hashtable.o -c $(SRC_DIR)/hashtable.c

patientRecord.o:$(SRC_DIR)/patientRecord.c
	$(CC) $(FLAGS) -o patientRecord.o -c $(SRC_DIR)/patientRecord.c

avltree.o:$(SRC_DIR)/avltree.c
	$(CC) $(FLAGS) -o avltree.o -c $(SRC_DIR)/avltree.c

utils.o:$(SRC_DIR)/utils.c
	$(CC) $(FLAGS) -o utils.o -c $(SRC_DIR)/utils.c

.PHONY : clean

clean:
	rm -f $(TARGETS) *.o