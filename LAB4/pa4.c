#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>


#include "logger.h"
#include "ipc_io.h"
#include "ipc.h"
#include "banking.h"


timestamp_t lamport_time = 0;


void set_time_msg(Message * msg){
    lamport_time++;
    msg->s_header.s_local_time = get_lamport_time();
}


int confer_all(IOLinker * io_fd, Message * msg) {
    set_time_msg(msg);
    send_multicast(io_fd, msg);

    for (local_id from = 1; from <= io_fd->subprocs_num; from++)
        if (from != io_fd->balance.s_id) {
            Message * new_msg = (Message *) malloc(sizeof(Message));
            receive(io_fd, from, new_msg);
            free(new_msg);
        }

    return 0;
}


timestamp_t get_lamport_time() {
    return lamport_time;
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
    start_of_transfer_msg.s_header.s_local_time = get_lamport_time();
    start_of_transfer_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_of_transfer_msg.s_header.s_payload_len = sizeof(TransferOrder);

    TransferOrder order;
    order.s_src = src;
    order.s_dst = dst;
    order.s_amount = amount;

    memcpy(&start_of_transfer_msg.s_payload, &order, sizeof(TransferOrder));
    Message * end_of_transfer_msg = (Message *) malloc(sizeof(Message));

    set_time_msg(&start_of_transfer_msg);
    send(io_fd, src, &start_of_transfer_msg);
    receive(io_fd, dst, end_of_transfer_msg);
    free(end_of_transfer_msg);

}


int request_cs(const void * self) {
    IOLinker * io_fd = (IOLinker *) self;


    char * blank_str = "blank";
    Message init_request_msg;
    init_request_msg.s_header.s_magic = MESSAGE_MAGIC;
    init_request_msg.s_header.s_type = CS_REQUEST;
    memcpy(&init_request_msg.s_payload, blank_str, strlen(blank_str));
    init_request_msg.s_header.s_payload_len = strlen(init_request_msg.s_payload) + 1;

    set_time_msg(&init_request_msg);
    send_multicast(io_fd, &init_request_msg);

//    printf("\tProcess %d: sent multicast\n", io_fd->balance.s_id);

    int waiting_in_queue = 1;
    int next_to_cs = 1;
    timestamp_t min_time = INT16_MAX;

    io_fd->queue[io_fd->balance.s_id] = *io_fd->lamport_time_p;

    do {
        Message *msg = (Message *) malloc(sizeof(Message));
        receive_any(io_fd, msg);

        switch (msg->s_header.s_type) {
            case CS_REQUEST:
//printf("\tProcess %d: CS_REQUEST from %d\n", io_fd->balance.s_id, io_fd->last_sender);
//                 check if its a proper time
                io_fd->received_at[io_fd->last_sender] = msg->s_header.s_local_time;
                io_fd->queue[io_fd->last_sender] = msg->s_header.s_local_time;

                msg->s_header.s_type = CS_REPLY;
                set_time_msg(msg);
                send(io_fd, io_fd->last_sender, msg);
//printf("\tProcess %d: queue", io_fd->balance.s_id);
//for (int i = 1; i <= io_fd->subprocs_num; i++) {
//    printf("%d ", io_fd->queue[i]);
//}
//printf("| %d\n", io_fd->queue[io_fd->balance.s_id]);
                break;

            case CS_REPLY:
//printf("\tProcess %d: CS_REPLY from %d\n", io_fd->balance.s_id, io_fd->last_sender);
                io_fd->received_at[io_fd->last_sender] = msg->s_header.s_local_time;
//printf("\tProcess %d: queue", io_fd->balance.s_id);
//for (int i = 1; i <= io_fd->subprocs_num; i++) {
//    printf("%d ", io_fd->queue[i]);
//}
//printf("| %d\n", io_fd->queue[io_fd->balance.s_id]);
                break;

            case CS_RELEASE:
//printf("\tProcess %d: CS_RELEASE from %d\n", io_fd->balance.s_id, io_fd->last_sender);
                io_fd->received_at[io_fd->last_sender] = msg->s_header.s_local_time;
                io_fd->queue[io_fd->last_sender] = INT16_MAX;
                min_time = INT16_MAX;
                break;

            case DONE:
                io_fd->received_at[io_fd->last_sender] = msg->s_header.s_local_time;
                io_fd->num_of_done++;
                break;
        }
        free(msg);

        int received_from_everyone = 1;
        for (int i = 1; i <= io_fd->subprocs_num; i++){
            if (io_fd->received_at[i] <= io_fd->queue[io_fd->balance.s_id] && io_fd->received_at[i] > 0 && i != io_fd->balance.s_id)
                received_from_everyone = 0;
            if (io_fd->queue[i] < min_time){
                next_to_cs = i;
                min_time = io_fd->queue[i];
            }
        }
//        printf("\tProcess %d: next_to_cs = %d\n", io_fd->balance.s_id, next_to_cs);
//        printf("\tProcess %d: min_time = %d\n", io_fd->balance.s_id, min_time);
//        printf("\tProcess %d: received_from ", io_fd->balance.s_id);
//        for (int i = 1; i <= io_fd->subprocs_num; i++) {
//            printf("%d ", io_fd->received_at[i]);
//        }
//        printf("| %d\n", io_fd->received_at[io_fd->balance.s_id]);

        if (received_from_everyone && next_to_cs == io_fd->balance.s_id)
            waiting_in_queue = 0;

    } while(waiting_in_queue);

    return 0;
}


int release_cs(const void * self) {
    IOLinker * io_fd = (IOLinker *) self;

    char * blank_str = "blank";
    Message release_msg;
    release_msg.s_header.s_magic = MESSAGE_MAGIC;
    release_msg.s_header.s_type = CS_RELEASE;
    memcpy(&release_msg.s_payload, blank_str, strlen(blank_str));
    release_msg.s_header.s_payload_len = strlen(release_msg.s_payload) + 1;

    set_time_msg(&release_msg);
    send_multicast(io_fd, &release_msg);

    return 0;
}


int run_subproc(IOLinker io_fd) {
    Message ack_msg;
    Message done_msg;
    Message balance_history_msg;
    TransferOrder * transfer;
    filter_pipes(&io_fd);

    // starting
    Message start_msg;
    sprintf(start_msg.s_payload, log_started_fmt, get_lamport_time(), io_fd.balance.s_id, getpid(), getppid(), io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
    start_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_msg.s_header.s_type = STARTED;
    start_msg.s_header.s_payload_len = strlen(start_msg.s_payload);
    log_write_all(start_msg.s_payload);
    confer_all(&io_fd, &start_msg);
    log_all_started(get_lamport_time(), io_fd.balance.s_id);

    // --------------------------------------
    // actual work

    char log_loop_operation[strlen(log_loop_operation_fmt)];

    if (!io_fd.use_critical_section) {
        for (int i = 1; i <= io_fd.balance.s_id * 5; i++) {
            sprintf(log_loop_operation, log_loop_operation_fmt, io_fd.balance.s_id, i, (io_fd.balance.s_id * 5));
            print(log_loop_operation);
        }
        sprintf(done_msg.s_payload, log_done_fmt, get_lamport_time(), io_fd.balance.s_id,
                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
        done_msg.s_header.s_magic = MESSAGE_MAGIC;
        done_msg.s_header.s_type = DONE;
        done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);
        log_write_all(done_msg.s_payload);
        confer_all(&io_fd, &done_msg);
        log_all_done(get_lamport_time(), io_fd.balance.s_id);

    } else {
        int num_to_send = io_fd.balance.s_id * 5;
        io_fd.num_of_done = 1;

        for (int i = 0; i <= io_fd.subprocs_num; i++) {
            io_fd.queue[i] = INT16_MAX;
            io_fd.received_at[i] = 0;
        }

        for (int i = 1; i <= num_to_send; ++i) {
            request_cs(&io_fd);
//            printf("\tProcess %d: Entering CS\n", io_fd.balance.s_id);
            sprintf(log_loop_operation, log_loop_operation_fmt, io_fd.balance.s_id, i, (io_fd.balance.s_id * 5));
            print(log_loop_operation);
//            printf("\tProcess %d: Releasing CS\n", io_fd.balance.s_id);
            release_cs(&io_fd);
        }

//        printf("\tProcess %d: num of received dones = %d\n", io_fd.balance.s_id, io_fd.num_of_done);

        sprintf(done_msg.s_payload, log_done_fmt, get_lamport_time(), io_fd.balance.s_id,
                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
        done_msg.s_header.s_magic = MESSAGE_MAGIC;
        done_msg.s_header.s_type = DONE;
        done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);
        log_write_all(done_msg.s_payload);

        send_multicast(&io_fd, &done_msg);

        while(io_fd.num_of_done < io_fd.subprocs_num){
            Message *msg = (Message *) malloc(sizeof(Message));
            receive_any(&io_fd, msg);

            switch (msg->s_header.s_type) {
                case CS_REQUEST:
                    msg->s_header.s_type = CS_REPLY;
                    msg->s_header.s_local_time = 0;
                    send(&io_fd, io_fd.last_sender, msg);
                    break;

                case DONE:
                    io_fd.num_of_done++;
                    break;
            }

            free(msg);
        }
        log_all_done(get_lamport_time(), io_fd.balance.s_id);
    }

    return 0;
}


int run_proc(IOLinker io_fd, pid_t * pids){
    io_fd.balance.s_id = 0;
    filter_pipes(&io_fd);

    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive(&io_fd, from, &msg);
    }
    log_all_started(get_lamport_time(), io_fd.balance.s_id);


    for (int from = 1; from <= io_fd.subprocs_num; from++) {
        Message msg;
        receive_any(&io_fd, &msg);
        if (msg.s_header.s_type != DONE)
            from--;
    }
    log_all_done(get_lamport_time(), io_fd.balance.s_id);


    for (int i = 1; i <= io_fd.subprocs_num; i++)
        waitpid(pids[i], NULL, 0);
    return 0;
}


int main(int argc, char **argv) {
    local_id subprocs_num = -1;
    int * init_balances;
    int use_critical_section = 0;

    if (argc < 3)
        return -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i < argc - 1){
            subprocs_num = (local_id)strtol(argv[++i], NULL, 10);
            init_balances = (int*) malloc(sizeof(int) * subprocs_num);
            for (int j = 0; j < subprocs_num; j++)
                init_balances[j] = (int)strtol(argv[i + j + 1], NULL, 10);
        }
        if (!strcmp(argv[i], "--mutexl"))
            use_critical_section = 1;
    }

    pid_t *pids = (pid_t *)malloc((subprocs_num + 1) * sizeof(pid_t));
    pids[PARENT_ID] = getpid();

    IOLinker io_fd = create_pipes(subprocs_num);
    io_fd.lamport_time_p = &lamport_time;

    log_begin();

    for (int i = 1; i <= subprocs_num; i++) {
        pid_t fork_pid = fork();

        if (fork_pid == 0) {
            io_fd.use_critical_section = use_critical_section;
            io_fd.balance.s_id = i;
            io_fd.balance.s_history_len = 0;
            io_fd.queue = (timestamp_t*) malloc(sizeof(timestamp_t) * (subprocs_num + 1));
            io_fd.received_at = (timestamp_t*) malloc(sizeof(timestamp_t) * (subprocs_num + 1));
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance = init_balances[i - 1];
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_lamport_time();
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
