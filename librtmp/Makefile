AR = ar

.PHONY:clean

OBJS=rtmp.o log.o amf.o hashswf.o parseurl.o

all:librtmp.a

librtmp.a: $(OBJS)
	$(AR) rs $@ $?
clean:
	rm -f *.o *.a *.so*