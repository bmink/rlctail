P = rlcprint
OBJS = main.o
CFLAGS = -g -Wall -Wstrict-prototypes
LDLIBS = -lb

$(P): $(OBJS)
	$(CC) -o $(P) $(LDFLAGS) $(OBJS) $(LDLIBS)

clean:
	rm -f *o; rm -f $(P)

install: $(P)
	cp $(P) ~/bin
