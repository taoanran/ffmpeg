#include ../config.mak
SRC=../
CC=gcc
INCLUDE += -I$(SRC)/ \
	   -I$(SRC)/../SDL-1.2.15/include/
ALIBSPATH=/usr/local/lib
ALIBS+= \
       $(ALIBSPATH)/libavdevice.a \
       $(ALIBSPATH)/libavfilter.a \
       $(ALIBSPATH)/libswscale.a \
       $(ALIBSPATH)/libswresample.a \
       $(ALIBSPATH)/libavformat.a \
       $(ALIBSPATH)/libavcodec.a \
       $(ALIBSPATH)/libavutil.a

SHARE_LIBS=-ldl -lasound -L/usr/local/lib -lSDL -lpthread -lm -lz -lrt 
EXTRA_CFLAGS += $(INCLUDE)
CFG_DEBUG = -g
SOURCE=demo.c
TARGET=demo


$(TARGET): $(SOURCE)
	$(CC) -o $@ $^ $(EXTRA_CFLAGS) $(ALIBS) $(SHARE_LIBS)

clean:
	rm *.o $(TARGET) -rf
