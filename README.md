# pg_logs_kafka
**pg_logs_kafka** It is a postgres extension. It sends postgres logs to a configured Kafka topic.

### Dependencies

* PostgreSQL
* librdkafka
* libsnappy
* zlib

### Instructions
* Installation command - make && make install.
* Enable the extension after installation completion - 
      shared_preload_libraries = 'pg_logs_kafka.so'
* add kafka and topic details in the conf/kafka.conf  
