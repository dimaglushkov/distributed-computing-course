#ifndef LAB1_LOGGER_H
#define LAB1_LOGGER_H

#include "stdio.h"
#include <unistd.h>
#include "sys/types.h"

#include "common.h"

typedef struct {
    pid_t * pids;
    int pids_num;
} PidsType;

FILE *events_log_file;

void log_begin()
{
    events_log_file = fopen(events_log, "w");
}

void log_finish()
{
    fclose(events_log_file);
}

void log_write_all(PidsType pidsType)
{

}

void log_started_msg()
{
    pid_t pid = getpid();
    pid_t ppid = getppid();
    PidsType pidsType;
    pidsType.pids[0] = pid;
    pidsType.pids[1] = ppid;
    pidsType.pids_num = 2;
    log_write_all(pidsType);
}

#endif //LAB1_LOGGER_H
