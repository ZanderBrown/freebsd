#$FreeBSD$

LIB=	heimipcs
PRIVATELIB=
LDADD=	-lheimbase -lroken
LDFLAGS=	-pthread
DPADD=	${LIBHEIMBASE} ${LIBROKEN}

SRCS=	\
	server.c \
	common.c

CFLAGS+=	-I${KRB5DIR}/lib/roken \
		-I${KRB5DIR}/base \
		-I${KRB5DIR}/lib/ipc -I.

.include <bsd.lib.mk>

.PATH: ${KRB5DIR}/lib/ipc
