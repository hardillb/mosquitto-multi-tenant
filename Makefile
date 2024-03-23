PLUGIN_NAME=mosquitto_multi_tenant
MOSQUTITTO_SRC=../mosquitto
CC=gcc
CFLAGS=-I. -I${MOSQUTITTO_SRC}/include -fPIC -Wall -ggdb -O3 -Wconversion -Wextra -std=gnu99 
LDFLAGS=-fPIC -shared
LIBADD=

OBJS:=${PLUGIN_NAME}.o

all : binary

binary : ${PLUGIN_NAME}.so ${PLUGIN_NAME}.a

${OBJS} : %.o: %.c ${EXTRA_DEPS}
	${CROSS_COMPILE}${CC} $(CPPFLAGS) $(CFLAGS) -c $< -o $@

${PLUGIN_NAME}.a : ${OBJS} ${OBJS_EXTERNAL}
	${CROSS_COMPILE}$(AR) cr $@ $^

${PLUGIN_NAME}.so : ${OBJS} ${OBJS_EXTERNAL}
	${CROSS_COMPILE}${CC} $(LDFLAGS) $^ -o $@ ${LIBADD}

clean:
	rm -rf ${PLUGIN_NAME}.a ${PLUGIN_NAME}.o ${PLUGIN_NAME}.so
