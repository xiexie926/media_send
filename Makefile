.PHONY:clean

media_send:
	gcc -o media_send media_send.c main.c  

clean:
	rm -rf *.o media_send