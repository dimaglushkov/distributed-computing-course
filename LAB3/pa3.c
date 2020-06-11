#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "logger.h"
#include "ipc_io.h"
#include "ipc.h"
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
                fcntl(fds[1], F_SETFL, O_NONBLOCK);

                io_fd.read_fd[src][dst] = fds[0];
                io_fd.write_fd[src][dst] = fds[1];
            }

    return io_fd;
}


int filter_pipes(IOLinker *io_fd){
    for(int src = 0; src <= io_fd->subprocs_num; src++)
        for(int dst = 0; dst <= io_fd->subprocs_num; dst++)
            if (src != dst) {
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
    free(end_of_transfer_msg);

//    end_of_transfer_msg->s_header.s_type == ACK

}


int run_subproc(IOLinker io_fd) {
    Message ack_msg;
    Message done_msg;
    Message balance_history_msg;
    TransferOrder * transfer;
    filter_pipes(&io_fd);

    // starting
    Message start_msg;
    sprintf(start_msg.s_payload, log_started_fmt, get_physical_time(), io_fd.balance.s_id, getpid(), getppid(), io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
    start_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_msg.s_header.s_type = STARTED;
    start_msg.s_header.s_payload_len = strlen(start_msg.s_payload);

    log_write_all(start_msg.s_payload);

    confer_all(&io_fd, &start_msg);

    log_all_started(get_physical_time(), io_fd.balance.s_id);

    // --------------------------------------
    // actual work

    int running = 1;
    do{
        Message * msg = (Message *) malloc(sizeof(Message));
        receive_any(&io_fd, msg);

        switch (msg->s_header.s_type){
            case TRANSFER:
                transfer = (TransferOrder*) msg->s_payload;

                if (transfer->s_src == io_fd.balance.s_id){
                    while (get_physical_time() != io_fd.balance.s_history_len - 1) {
                        io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance =
                                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance;
                        io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = io_fd.balance.s_history_len - 1;
                        io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;

                        io_fd.balance.s_history_len++;
                    }

                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance = io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance - transfer->s_amount;
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_physical_time();
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;
                    io_fd.balance.s_history_len++;

                    send(&io_fd, transfer->s_dst, msg);

                    char transfer_out_str[strlen(log_transfer_out_fmt)];
                    sprintf(transfer_out_str, log_transfer_out_fmt, get_physical_time(), io_fd.balance.s_id, transfer->s_amount, transfer->s_dst);
                    log_write_all(transfer_out_str);
                }
                else{
                    while (get_physical_time() != io_fd.balance.s_history_len - 1) {
                        io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance =
                                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance;
                        io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = io_fd.balance.s_history_len - 1;
                        io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;

                        io_fd.balance.s_history_len++;
                    }

                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance = io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance + transfer->s_amount;
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_physical_time();
                    io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;

                    io_fd.balance.s_history_len++;

                    ack_msg.s_header.s_type = ACK;
                    ack_msg.s_header.s_local_time = get_physical_time();
                    ack_msg.s_header.s_magic = MESSAGE_MAGIC;
                    ack_msg.s_header.s_payload_len = 0;

                    send(&io_fd, 0, &ack_msg);

                    char transfer_in_str[strlen(log_transfer_in_fmt)];
                    sprintf(transfer_in_str, log_transfer_in_fmt, get_physical_time(), io_fd.balance.s_id, transfer->s_amount, transfer->s_src);
                    log_write_all(transfer_in_str);
                }
                break;

            case STOP:
                // --------------------------------------
                // finishing
                sprintf(done_msg.s_payload, log_done_fmt, get_physical_time(), io_fd.balance.s_id, io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
                done_msg.s_header.s_magic = MESSAGE_MAGIC;
                done_msg.s_header.s_type = DONE;
                done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);

                log_write_all(done_msg.s_payload);

                confer_all(&io_fd, &done_msg);

                log_all_done(get_physical_time(), io_fd.balance.s_id);

                // exchanged done with everybody

                balance_history_msg.s_header.s_type = BALANCE_HISTORY;
                balance_history_msg.s_header.s_local_time = get_physical_time();
                balance_history_msg.s_header.s_magic = MESSAGE_MAGIC;
                balance_history_msg.s_header.s_payload_len = sizeof(BalanceHistory);
                memcpy(&balance_history_msg.s_payload, &io_fd.balance, sizeof(BalanceHistory));

                send(&io_fd, 0, &balance_history_msg);

                running = 0;
                break;
        }

        free(msg);
    } while(running);

    return 0;
}


int run_proc(IOLinker io_fd, pid_t * pids){
    Message * balance_history_msg;
    AllHistory all_history;
    time_t max_time = 0;

    all_history.s_history_len = io_fd.subprocs_num;

    io_fd.balance.s_id = 0;
    filter_pipes(&io_fd);


    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }
    log_all_started(get_physical_time(), io_fd.balance.s_id);

    bank_robbery(&io_fd, io_fd.subprocs_num);

    Message stop_msg;
    stop_msg.s_header.s_type = STOP;
    stop_msg.s_header.s_magic = MESSAGE_MAGIC;
    stop_msg.s_header.s_local_time = get_physical_time();
    stop_msg.s_header.s_payload_len = 0;

    confer_all(&io_fd, &stop_msg);
    log_all_done(get_physical_time(), io_fd.balance.s_id);

    for (int i = 0; i < io_fd.subprocs_num; i++) {
        BalanceHistory new_history;
        balance_history_msg = (Message *) malloc(sizeof(Message));
        receive(&io_fd, i + 1, balance_history_msg);
        new_history = *(BalanceHistory*) balance_history_msg->s_payload;
        all_history.s_history[i] = new_history;

        if (new_history.s_history[new_history.s_history_len - 1].s_time > max_time){
            max_time = new_history.s_history[new_history.s_history_len - 1].s_time;
        }

        free(balance_history_msg);
    }

    for (int i = 0; i < io_fd.subprocs_num; i++) {
        time_t cur_max_time = all_history.s_history[i].s_history[all_history.s_history[i].s_history_len - 1].s_time;
        uint8_t cur_len = all_history.s_history[i].s_history_len;

        while (cur_max_time < max_time) {
            all_history.s_history[i].s_history[cur_len].s_balance =
                    all_history.s_history[i].s_history[cur_len - 1].s_balance;
            all_history.s_history[i].s_history[cur_len].s_time = cur_len - 1;
            all_history.s_history[i].s_history[cur_len].s_balance_pending_in = 0;

            cur_len++;
            cur_max_time++;
        }

        all_history.s_history[i].s_history_len = cur_len;
    }

    print_history(&all_history);


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
                init_balances[j] = (int)strtol(argv[i + j + 1], NULL, 10);
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

    log_finish();
    free(init_balances);
    free(pids);

    return 0;
}
