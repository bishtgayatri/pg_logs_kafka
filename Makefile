KAFKA_CONF_PATH   = /opt/pgdata/kafka-conf
PG_CPPFLAGS += -std=c11 -I/usr/local/include -I/usr/pgsql-9.5/include/server -DKAFKA_PATH='"$(KAFKA_CONF_PATH)"'
SHLIB_LINK  += -L/usr/local/lib -L/usr/pgsql-9.5/lib -lrdkafka -lz -lpthread

EXTENSION    = kafka
EXTVERSION   = $(shell grep default_version $(EXTENSION).control | \
               sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")
DATA         = $(filter-out $(wildcard sql/*--*.sql),$(wildcard sql/*.sql))
DOCS         = $(wildcard doc/*.*)
TESTS        = $(wildcard test/sql/*.sql)
REGRESS      = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test
MODULE_big   = $(patsubst src/%.c,%,$(wildcard src/*.c))
OBJS         = src/pg_log_kafka.o
#$(shell export KAFKA_PATH=/opt/)
PG_CONFIG    = pg_config
PG91         = $(shell $(PG_CONFIG) --version | grep -qE " 8\.| 9\.0" && echo no || echo yes)

ifeq ($(PG91),yes)
all: copy	
copy:
	mkdir -p $(KAFKA_CONF_PATH)
	cp -f conf/kafka.conf $(KAFKA_CONF_PATH)
DATA = $(wildcard sql/*--*.sql) sql/$(EXTENSION)--$(EXTVERSION).sql
EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql
endif
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
