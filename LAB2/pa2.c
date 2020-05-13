#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "logger.h"
#include "ipc_io.h"
#include "ipc.h"
#include "ipc.c"
#include "banking.h"


int confer_all(IOLinker * io_fd, Message * msg) {

    send_multicast(io_fd, msg);

    for (local_id from = 1; from <= io_fd->subprocs_num; from++)
        if (from != io_fd->balance.s_id) {
            Message * new_msg = (Message *) malloc(sizeof(Message));
            receive(io_fd, from, new_msg);
            free(new_msg);
        }

    return 0;
}


IOLinker create_pipes(local_id subproc_num) {
    IOLinker io_fd;
    io_fd.subprocs_num = subproc_num;

    for (int src = 0; src <= subproc_num; src++)
        for (int dst = 0; dst <= subproc_num; dst++)
            if (src != dst){
                int fds[2];
                pipe(fds);

                // for non-blocking read
                fcntl(fds[0], F_SETFL, O_NONBLOCK);

                io_fd.read_fd[src][dst] = fds[0];
                io_fd.write_fd[src][dst] = fds[1];
            }

    return io_fd;
}


int filter_pipes(IOLinker *io_fd){
    for(int src = 0; src <= io_fd->subprocs_num; src++)
        for(int dst = 0; dst <= io_fd->subprocs_num; dst++) {
            if (src != io_fd->balance.s_id) {
                close(io_fd->write_fd[src][dst]);
                io_fd->write_fd[src][dst] = 0;
            }
            if (dst != io_fd->balance.s_id) {
                close(io_fd->read_fd[src][dst]);
                io_fd->read_fd[src][dst] = 0;
            }
        }
    return 0;
}


void transfer(void * parent_data, local_id src, local_id dst, balance_t amount)
{
    IOLinker * io_fd = (IOLinker *) parent_data;

    Message start_of_transfer_msg;
    start_of_transfer_msg.s_header.s_type = TRANSFER;
    start_of_transfer_msg.s_header.s_local_time = get_physical_time();
    start_of_transfer_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_of_transfer_msg.s_header.s_payload_len = sizeof(TransferOrder);

    TransferOrder order;
    order.s_src = src;
    order.s_dst = dst;
    order.s_amount = amount;

    memcpy(&start_of_transfer_msg.s_payload, &order, sizeof(TransferOrder));
    Message * end_of_transfer_msg = (Message *) malloc(sizeof(Message));

    send(io_fd, src, &start_of_transfer_msg);
    receive(io_fd, dst, end_of_transfer_msg);

//    end_of_transfer_msg->s_header.s_type == ACK

}


int run_subproc(IOLinker io_fd) {
    filter_pipes(&io_fd);

    // starting
    Message start_msg;
    sprintf(start_msg.s_payload, log_started_fmt, io_fd.balance.s_id, getpid(), getppid());
    start_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_msg.s_header.s_type = STARTED;
    start_msg.s_header.s_payload_len = strlen(start_msg.s_payload);

    log_write_all(start_msg.s_payload);

    confer_all(&io_fd, &start_msg);

    log_all_started(io_fd.balance.s_id);

    // --------------------------------------
    // actual work

    int running = 1;
    do{
        Message * msg = (Message *) malloc(sizeof(Message));
        receive_any(&io_fd, msg);

        switch (msg->s_header.s_type){
            TransferOrder * transfer;
            case TRANSFER:
                transfer = (TransferOrder*) msg->s_payload;

                if (transfer->s_src == io_fd.balance.s_id){
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance -= transfer->s_amount;
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_physical_time();
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;
                    io_fd.balance.s_history_len++;

                    send(&io_fd, transfer->s_dst, msg);
                }
                else{
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance += transfer->s_amount;
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_physical_time();
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;
                    io_fd.balance.s_history_len++;


                }
                break;

            case STOP:


                running = 0;
                break;
        }

        free(msg);
    }
    while(running);

    // --------------------------------------
    // finishing
    Message done_msg;
    sprintf(done_msg.s_payload, log_done_fmt, io_fd.balance.s_id);
    done_msg.s_header.s_magic = MESSAGE_MAGIC;
    done_msg.s_header.s_type = DONE;
    done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);

    log_write_all(done_msg.s_payload);

    confer_all(&io_fd, &done_msg);

    log_all_done(io_fd.balance.s_id);

    return 0;
}


int run_proc(IOLinker io_fd, pid_t * pids){
    io_fd.balance.s_id = 0;
    filter_pipes(&io_fd);

    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }
    log_all_started(io_fd.balance.s_id);

    bank_robbery(&io_fd, io_fd.subprocs_num);

    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }
    log_all_done(io_fd.balance.s_id);


    for (int i = 1; i <= io_fd.subprocs_num; i++)
        waitpid(pids[i], NULL, 0);
    return 0;
}


int main(int argc, char **argv) {
    local_id subprocs_num = -1;
    int * init_balances;

    if (argc < 3)
        return -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i < argc - 1){
            subprocs_num = (local_id)strtol(argv[++i], NULL, 10);
            init_balances = (int*) malloc(sizeof(int) * subprocs_num);
            for (int j = 0; j < subprocs_num; j++)
                init_balances[j] = (int)strtol(argv[i + j], NULL, 10);
        }
    }

    pid_t *pids = (pid_t *)malloc((subprocs_num + 1) * sizeof(pid_t));
    pids[PARENT_ID] = getpid();

    IOLinker io_fd = create_pipes(subprocs_num);

    log_begin();

    for (int i = 1; i <= subprocs_num; i++) {
        pid_t fork_pid = fork();

        if (fork_pid == 0) {
            io_fd.balance.s_id = i;
            io_fd.balance.s_history_len = 0;
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance = init_balances[i - 1];
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_physical_time();
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;
            io_fd.balance.s_history_len++;


            return run_subproc(io_fd);
        }

        if (fork_pid != 0)
            pids[i] = fork_pid;
    }

    run_proc(io_fd, pids);

//    print_history()

    log_finish();
    free(pids);

    return 0;
}
