PROGS = digitalDJ

all: $(PROGS)

CC = g++

LFLAGS = -ljack -lespeak -lpthread -lQtGui -lQtCore -lglib-2.0 -lxmmsclient++-glib
CFLAGS = -Wall -c -I/usr/include/QtCore -I/usr/include/QtGui -I/usr/local/include/xmms2 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include
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

clean:
	rm -f ${OBJS} $(PROGS:%=%.o)
