all : remoteDM

#CC = /opt/buildroot-gcc342/bin/mipsel-linux-gcc
LDFLAGS += -lpthread

remoteDM: client.o
	$(CC) $^ $(LDFLAGS) -o $@

clean:
	$(RM) -f *.o remoteDM
