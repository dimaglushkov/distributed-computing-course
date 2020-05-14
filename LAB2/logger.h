#ifndef LAB1_LOGGER_H
#define LAB1_LOGGER_H

#include "stdio.h"
#include <unistd.h>
#include "sys/types.h"

#include "common.h"
#include "ipc.h"
#include "pa2345.h"
#include "banking.h"

FILE *events_log_file;


void log_begin() {
    events_log_file = fopen(events_log, "a");
}


void log_finish() {
    fclose(events_log_file);
}


void log_write_all(char * str) {
    fputs(str, events_log_file);
    printf("%s", str);
}


void log_all_started(timestamp_t time, local_id id) {
    char all_started[strlen(log_received_all_started_fmt)];
    sprintf(all_started, log_received_all_started_fmt, time, id);
    log_write_all(all_started);
}


void log_all_done(timestamp_t time, local_id id) {
    char all_done[strlen(log_received_all_done_fmt)];
    sprintf(all_done, log_received_all_done_fmt, time, id);
    log_write_all(all_done);
}




#endif //LAB1_LOGGER_H
