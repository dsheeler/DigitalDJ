PROGNAME = digitalDJ
PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

CC = g++ -std=c++17

LFLAGS = /usr/lib/libFestival.so -lestools -lestbase -leststring \
				 -ljack -lpthread -lglib-2.0 -lasound -lomp\
				 $(shell pkg-config --libs xmms2-client-cpp) \
				 $(shell pkg-config --libs xmms2-client-cpp-glib) \
				 $(shell pkg-config --libs gtkmm-3.0) \
				 $(shell pkg-config --libs libnotify) \
				 -lncurses

CFLAGS = -Wall -c -I /usr/include/glib-2.0 \
				 -I /usr/lib/x86_64-linux-gnu/glib-2.0/include \
				 $(shell pkg-config --cflags gtkmm-3.0) \
				 $(shell pkg-config --cflags libnotify) \
				 $(shell pkg-config --cflags xmms2-client-cpp) \
				 $(shell pkg-config --cflags xmms2-client-cpp-glib) \
				 -I/usr/include/speech_tools \
				 -I/usr/local/include  \
         -I/usr/include/json-glib-1.0

LIBS =

SRCS = digitalDJ.cpp
OBJS = $(SRCS:.cpp=.o)
HDRS =

all: $(PROGNAME)

debug: CFLAGS += -g
debug: $(PROGNAME)

.SUFFIXES:

.SUFFIXES: .cpp

%.o : %.cpp
	$(CC) ${CFLAGS} $<

digitalDJ: ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

install: all
	install $(PROGNAME) $(BINDIR)

uninstall:
	rm -f $(BINDIR)/$(PROGNAME)

clean:
	rm -f ${OBJS} $(PROGNAME:%=%.o)
