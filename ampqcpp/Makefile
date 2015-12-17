CXX      = g++
CFLAGS   = -Wall -g
CPPFLAGS = $(CFLAGS)  -I../staticdeps/include

LIBRARIES= rabbitmq pthread

LIBNAME  = amqpcpp
LIBFILE  = lib$(LIBNAME).a

SOURCES  = AMQP.cpp AMQPBase.cpp AMQPException.cpp AMQPMessage.cpp AMQPExchange.cpp AMQPQueue.cpp
OBJECTS  = $(SOURCES:.cpp=.o)


all: $(LIBFILE)
	cp $(LIBFILE) ../staticdeps/lib
	cp AMQPcpp.h ../staticdeps/include

$(LIBFILE): $(OBJECTS)
	$(AR) rcs $@ $(OBJECTS)


clean:
	rm -f $(OBJECTS) $(LIBFILE)
