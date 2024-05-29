TARGET	= ace
CC	= gcc
ABC	= /home/shangyang/alanmi-abc-906cecc894b2/

SRCS	= main.c
OBJS	= ${SRCS:.c=.o}
LIB = ${ABC}/libabc.a -lm -ldl -lrt -rdynamic -lreadline -ltermcap -lpthread 
INCLUDE = -I. -I${ABC}/src
CFLAGW  = -Wall -O3
CFLAGS  = -O3

${TARGET}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} ${LIB} ${LIBS} -o ${TARGET}

.SUFFIXES: .c .o

.c.o:
	${CC} ${CFLAGW} ${INCLUDE} -o $@ -c $<

clean:
	rm -f core *~ $(TARGET); \
	rm *.o