CC = gcc

.PHONY:clean

SRS_ALL   :=$(wildcard ./*.c) $(wildcard ./librtmp/src/*.c)
OBJS_ALL :=$(SRS_ALL:%.c=%.o)

CFLAGS += -Ilibrtmp/ -I./
LIB_PATH +=-L./librtmp/

%.o:%.c
	$(CC) $(CFLAGS) -fpic -c $< -o $@
media_send: $(OBJS_ALL)
	cd librtmp;make;cd ..
	$(CC) -o $@ $^ $(LIB_PATH)  -lrtmp -lssl -lcrypto -lz

clean:
	rm -rf *.o media_send
	cd  librtmp;make clean