CC = gcc

.PHONY:clean

SRS_ALL   :=$(wildcard ./*.c) $(wildcard ./librtmp/src/*.c)
OBJS_ALL :=$(SRS_ALL:%.c=%.o)

CFLAGS += -Ilibrtmp/ -I./ -Ilibffmpeg/include/
LIB_PATH +=-L./librtmp/ -L./libffmpeg/lib
LIBS +=-lrtmp -lssl -lcrypto \
	-lavfilter \
	-lavformat \
	-lavdevice \
	-lavcodec \
	-lswscale \
	-lavutil \
	-lswresample \
	-lm \
	-lrt \
	-lz \
	-lpthread \

%.o:%.c
	$(CC) $(CFLAGS) -fpic -c $< -o $@
media_send: $(OBJS_ALL)
	cd librtmp;make;cd ..
	$(CC) -o $@ $^ $(LIB_PATH)  $(LIBS)

clean:
	rm -rf *.o media_send
	cd  librtmp;make clean