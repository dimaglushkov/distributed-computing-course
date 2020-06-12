#ifndef __IFMO_DISTRIBUTED_CLASS_IPC_IO__H
#define __IFMO_DISTRIBUTED_CLASS_IPC_IO__H

#include "banking.h"

typedef struct {
    local_id subprocs_num;
    BalanceHistory balance;
    int use_critical_section;
    local_id last_sender;
    timestamp_t * lamport_time_p;
    timestamp_t * queue;
    int write_fd[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1],
         read_fd[MAX_PROCESS_ID + 1][MAX_PROCESS_ID + 1];
} IOLinker;


#endif // __IFMO_DISTRIBUTED_CLASS_IPC_IO__H
