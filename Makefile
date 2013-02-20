CC=g++
CFLAGS=-c -Wall
LDFLAGS=../lib/.libs/libwebsockets.a -lz
SOURCES=lws-server.c HierarchicalHash.cpp cJSON.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=lws-server

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@  $(LDFLAGS) 

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm *.o $(EXECUTABLE) *~
