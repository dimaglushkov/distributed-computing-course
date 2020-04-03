#ifndef LAB1_LOGGER_H
#define LAB1_LOGGER_H

#include "stdio.h"
#include <unistd.h>
#include "sys/types.h"

#include "common.h"
#include "ipc.h"
#include "pa1.h"

FILE *events_log_file;

void log_begin()
{
    events_log_file = fopen(events_log, "a");
}

void log_finish()
{
    fclose(events_log_file);
}

void log_write_all(char * str)
{
    fputs(str, events_log_file);
    printf("%s", str);
}

#endif //LAB1_LOGGER_H
