CONTIKI_PROJECT = leach
all: $(CONTIKI_PROJECT)

#UIP_CONF_IPV6=1

CONTIKI = ../contiki-2.7
include $(CONTIKI)/Makefile.include
