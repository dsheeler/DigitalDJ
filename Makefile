PROGS = digitalDJ

all: $(PROGS)

CC = g++

LFLAGS = -lFestival -lestools -lestbase -leststring
LFLAGS += 
LFLAGS += -ljack -lespeak -lpthread -lglib-2.0 -lxmmsclient++-glib -lxmmsclient++ -lxmmsclient -lxmmsclient-glib
LFLAGS +=  $(shell pkg-config --libs gtkmm-3.0) $(shell pkg-config --libs libnotify)

CFLAGS = -Wall -c -I -I /usr/local/include/xmms2 -I /usr/include/glib-2.0 -I /usr/lib/x86_64-linux-gnu/glib-2.0/include 
CFLAGS +=  -I/usr/include/libnotify $(shell pkg-config --cflags gtkmm-3.0) $(shell pkg-config --cflags libnotify)
CFLAGS += -I/usr/include/festival -I/usr/include/speech_tools -I/usr/local/include/xmms2
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
