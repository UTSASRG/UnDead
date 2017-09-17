CCXX = g++
CC = gcc

### Optional Flags for Output ###
# -DDETAILREPORT : report code lines
# -DREPORTFILE : write deadlocks into a .report file
# -DENABLE_LOG : write recorded dependencies into a .synclog file

# ALL FLAGS
#CFLAGS= -g -O2 -I. -DDISABLE_INIT_CHECK -DMONITOR_THREAD -DENABLE_ANALYZER -DENABLE_PREVENTION -DUSING_SIGUSR1 -DUSING_SIGUSR2 -DENABLE_LOG -DREPORTFILE -DDETAILREPORT -fPIC -std=c++11 

# UNDEAD
CFLAGS= -g -O2 -I. -DDISABLE_INIT_CHECK -DMONITOR_THREAD -DENABLE_ANALYZER -DENABLE_PREVENTION -DUSING_SIGUSR2 -fPIC -std=c++11

# UNDEAD-LOG
#CFLAGS= -g -O2 -I. -DUSING_SIGUSR2 -fPIC -std=c++11

LD = $(CCXX)
LDFLAGS = -lpthread -ldl -shared -fPIC 

TARGET = libundead.so 

SRCS = $(wildcard *.c)
CPP_SRCS = $(wildcard *.cpp)
OBJS = $(patsubst %.c,%.o,$(SRCS))
CPP_OBJS = $(patsubst %.cpp,%.o,$(CPP_SRCS))

OBJECTS_AS = #lowlevellock.o

all: $(TARGET)

$(TARGET) : $(OBJS) $(OBJECTS_AS) $(CPP_OBJS)
	$(LD) -o $@ $^ $(LDFLAGS)
%.o : %.c
	$(CC) $(CFLAGS) -c $<
%.o : %.cpp
	$(CCXX) $(CFLAGS) -c $<
%.o : %.S
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f libundead.so *.o
