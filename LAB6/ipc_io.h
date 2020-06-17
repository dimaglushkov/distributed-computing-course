#ifndef __IFMO_DISTRIBUTED_CLASS_IPC_IO__H
#define __IFMO_DISTRIBUTED_CLASS_IPC_IO__H

#include "banking.h"

typedef struct {
    int is_with_me;
    int is_clear;
} Fork;

typedef struct {
    int is_with_me;
} Token;

typedef struct {
    local_id subprocs_num;
    int use_critical_section;

    local_id last_sender;
    local_id num_of_done;
    timestamp_t * lamport_time_p;
    timestamp_t * queue;
    timestamp_t * received_at;

    Fork forks[MAX_PROCESS_ID + 2];
    Token tokens[MAX_PROCESS_ID + 2];
    BalanceHistory balance;
    int write_fd[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1],
         read_fd[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1];
} IOLinker;

#endif // __IFMO_DISTRIBUTED_CLASS_IPC_IO__H
