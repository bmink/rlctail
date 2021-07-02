P = rlctail
OBJS = main.o cJSON.o cJSON_helper.o
CFLAGS = -g -Wall -Wstrict-prototypes
LDLIBS = -lb -lcurl

$(P): $(OBJS)
	$(CC) -o $(P) $(LDFLAGS) $(OBJS) $(LDLIBS)

clean:
	rm -f *o; rm -f $(P)

install: $(P)
	cp $(P) ~/bin
