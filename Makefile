PROGS = digitalDJ

all: $(PROGS)

CC = g++

LFLAGS = -lFestival -lestools -lestbase -leststring -lclutter-1.0 -lcogl-pango\
				 -lcogl -ljack -lpthread -lglib-2.0 -lxmmsclient++-glib \
         -lxmmsclient++ -lxmmsclient -lxmmsclient-glib \
         $(shell pkg-config --libs gtkmm-3.0) $(shell pkg-config --libs libnotify) \
				 -lncurses

CFLAGS = -Wall -c -I /usr/include/xmms2 -I /usr/include/glib-2.0 \
				 -I /usr/lib/x86_64-linux-gnu/glib-2.0/include \
				 -I/usr/include/libnotify $(shell pkg-config --cflags gtkmm-3.0) \
				 $(shell pkg-config --cflags libnotify) \
				 -I/usr/include/festival -I/usr/include/speech_tools \
				 -I/usr/local/include -I/usr/include/cogl -I/usr/include/clutter-1.0 \
         -I/usr/include/json-glib-1.0

LIBS =

SRCS = digitalDJ.cpp
OBJS = $(SRCS:.cpp=.o)
HDRS =

.SUFFIXES:

.SUFFIXES: .cpp

%.o : %.cpp
	$(CC) ${CFLAGS} $<

digitalDJ: ${OBJS}
	${CC} -o $@ ${OBJS} ${LFLAGS}

#notification: ${OBJS}
#	${CC} -o $@ ${OBJS} ${LFLAGS}

clean:
	rm -f ${OBJS} $(PROGS:%=%.o)
