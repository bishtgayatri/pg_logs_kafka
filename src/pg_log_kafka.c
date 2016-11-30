/*
 * pg_log_kafka - PostgreSQL extension to send logs messages to Apache Kafka
 *
 * Copyright (c) 2016
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>
#include <errno.h>

#include "postgres.h"
#include "fmgr.h"
#include "executor/spi.h"
#include "access/xact.h"
#include "utils/builtins.h"
#include "librdkafka/rdkafka.h"
#include "utils/elog.h"

extern int errno ;
PG_MODULE_MAGIC;

void _PG_init(void);
void _PG_fini(void);

struct config get_config(char *filename);

static void sendLogsToKafka(ErrorData *edata);
struct config getKafkaConfigurations();
void getFileName(char *fileFullName);
static emit_log_hook_type prev_log_hook = NULL;
static rd_kafka_t *GRK = NULL;
static rd_kafka_t *get_rk(); 
FILE *fp;
#define MAXBUF 1024 
#define DELIM "="

struct config
{
   char broker_host[MAXBUF];
   int broker_port;
   char topic_name[MAXBUF];
};

static struct config configstruct;
 
/**
 *  * Message delivery report callback.
 *   * Called once for each message.
 *    * See rkafka.h for more information.
 *     */
static void rk_msg_delivered(rd_kafka_t *rk, void *payload, size_t len,
                             int error_code, void *opaque, void *msg_opaque) {
  if (error_code)
    elog(WARNING, "%% Message delivery failed: %s\n",
         rd_kafka_err2str(error_code));
  	fprintf(stderr, "Messsage delivery success.\n");
}

/**
 *  * Kafka logger callback
 *   */
static void rk_logger(const rd_kafka_t *rk, int level, const char *fac,
                      const char *buf) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  fprintf(stderr, "%u.%03u RDKAFKA-%i-%s: %s: %s\n", (int)tv.tv_sec,
          (int)(tv.tv_usec / 1000), level, fac, rd_kafka_name(rk), buf);
}

static void rk_destroy() {
  rd_kafka_t *rk = get_rk();
  rd_kafka_destroy(rk);
  GRK = NULL;
}


void _PG_init() 
{ 
	//RegisterXactCallback(pg_xact_callback, NULL);
    configstruct = getKafkaConfigurations();
	prev_log_hook = emit_log_hook;
    emit_log_hook = sendLogsToKafka;
}

void _PG_fini(void) {
	emit_log_hook = prev_log_hook;
	fclose(fp);
} 

struct config getKafkaConfigurations() 
{       
 struct config configstruct;
        char fileName[1024];
	getFileName(fileName);
        fp = fopen (fileName, "r");
        if (fp != NULL)
        { 
                char line[MAXBUF];
                int i = 0;
                while(fgets(line, sizeof(line), fp) != NULL)
                {
                        char *cfline;
                        cfline = strstr((char *)line,DELIM);
                        cfline = cfline + strlen(DELIM);
                        if (i == 0){
                                memcpy(configstruct.broker_host,cfline,strlen(cfline));
				configstruct.broker_host[strlen(cfline)-1]='\0';

                        } else if (i == 1){
                               	configstruct.broker_port = atoi(cfline);
                        } else if (i == 2){
 				memcpy(configstruct.topic_name,cfline,strlen(cfline));
		                configstruct.topic_name[strlen(cfline)-1]='\0';
                	}      	
			i++;
		}
	}
	fclose(fp);
	return configstruct;
}

static rd_kafka_t *get_rk() {
  if (GRK) {
    return GRK;
  }
  rd_kafka_t *rk;
  char errstr[512];
  char *brokers;
  
  brokers = (char *)malloc(1024);
  memset(brokers,0,1024); 
  strcpy(brokers,configstruct.broker_host); 
  sprintf(brokers,"%s:%d",configstruct.broker_host,configstruct.broker_port);
  rd_kafka_conf_t *conf = rd_kafka_conf_new();
  if (rd_kafka_conf_set(conf, "compression.codec", "snappy", errstr,
                        sizeof(errstr)) != RD_KAFKA_CONF_OK) {
    elog(WARNING, "%s\n", errstr);
  }
  /* set message delivery callback */
  rd_kafka_conf_set_dr_cb(conf, rk_msg_delivered);
   /* get producer handle to kafka */
  if (!(rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr)))) {
    elog(WARNING, "%% Failed to create new producer: %s\n", errstr);
    goto broken;
  }
  /* set logger */
  rd_kafka_set_logger(rk, rk_logger);
  rd_kafka_set_log_level(rk, LOG_INFO);
  
 /* add brokers */
  if (rd_kafka_brokers_add(rk, brokers) == 0) {
    elog(WARNING, "%% No valid brokers specified\n");
    goto broken;
  } 
  GRK = rk;
  return rk; 
 
  broken:
  	rd_kafka_destroy(rk);
  	return NULL;
}


static void sendLogsToKafka(ErrorData *edata) {
     char topic[MAXBUF];
     void *msg = edata->message;
     size_t msg_len = strlen(msg);
     strcpy(topic,configstruct.topic_name);
     /* create topic */
     rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
     rd_kafka_t *rk = get_rk();
     if (!rk) {
      fprintf(stderr,"**************broker is null***************\n");
     }
     rd_kafka_topic_t *rkt = rd_kafka_topic_new(rk, topic, topic_conf);
   
     /* using random partition for now */
     int partition = RD_KAFKA_PARTITION_UA;
 
     /* send/produce message. */
     int rv = rd_kafka_produce(rkt, partition, RD_KAFKA_MSG_F_COPY, msg, msg_len,
                              NULL, 0, NULL);
                 
     if (rv == -1) {
      /* poll to handle delivery reports */
      rd_kafka_poll(rk, 0);
    }

    /* destroy kafka topic */
    rd_kafka_topic_destroy(rkt);
}

void getFileName(char *fileFullName) {
			char* fileConf = KAFKA_PATH;
  			char lastCharStr=fileConf[(strlen(fileConf)-1)];
 			char* fileName="kafka.conf";
  			char* fileSep = "/";
		        memset(fileFullName,0,1024);
			#ifdef __linux__    
 				fileSep = "/";
 		   	#elif __APPLE__
 		   	   	fileSep = "/";
 		   	#else
 		   	    fileSep = "\\";
 		   	#endif	  
            if(lastCharStr == '/' || lastCharStr == '\\') 
	 	  	{
				strcat(fileFullName,fileConf);
    			strcat(fileFullName,fileName);
  			} else {
   				strcat(fileFullName,fileConf);
				strcat(fileFullName,fileSep);
   				strcat(fileFullName,fileName);
  			}
 	}
