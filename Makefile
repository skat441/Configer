CC := gcc
CFLAGS := -Wall -Wextra -std=c11
LDFLAGS := -lncurses -lcrypto

TARGET := src/TUI/main

SRCS := \
	src/TUI/main.c \
	src/Cisco\ xRV/Cisco_telnet_get.c \
	src/Cisco\ xRV/Cisco_telnet_send.c \
	src/MikroTik/MikroTik_scp_get.c \
	src/MikroTik/MikroTik_scp_send.c \
	src/Juniper\ vMX/Juniper_vmx_backup.c \
	src/store/credential_store.c

OBJS := $(SRCS:.c=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o "$@" $(OBJS) $(LDFLAGS)

%.o: %.c src/network_ops.h
	$(CC) $(CFLAGS) -c "$<" -o "$@"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
