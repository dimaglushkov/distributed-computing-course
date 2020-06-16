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

    io_fd->sent_at[io_fd->balance.s_id] = *io_fd->lamport_time_p;


    int waiting_in_queue = 1;
    int left_to_receive = io_fd->subprocs_num - 1;

    for (int i = 1; i <= io_fd->subprocs_num; i++)
        io_fd->deferred_reply[i] = 0;


    do {
        Message *msg = (Message *) malloc(sizeof(Message));
        receive_any(io_fd, msg);

        switch (msg->s_header.s_type) {
            case CS_REQUEST:
                io_fd->received_time[io_fd->last_sender] = msg->s_header.s_local_time;
//                printf("\tProcess %d: received request from %d with %d at %d\n", io_fd->balance.s_id, io_fd->last_sender, msg->s_header.s_local_time, io_fd->sent_at[io_fd->balance.s_id]);

                if (msg->s_header.s_local_time > io_fd->sent_at[io_fd->balance.s_id]
                    || (msg->s_header.s_local_time == io_fd->sent_at[io_fd->balance.s_id] && io_fd->balance.s_id < io_fd->last_sender)) {
                    io_fd->sent_at[io_fd->last_sender] = msg->s_header.s_local_time;
                    msg->s_header.s_type = CS_REPLY;
                    set_time_msg(msg);
//                    printf("\tProcess %d: replying to %d\n", io_fd->balance.s_id, io_fd->last_sender);
                    send(io_fd, io_fd->last_sender, msg);
                } else {
                    io_fd->deferred_reply[io_fd->last_sender] = 1;
//                    printf("\tProcess %d: deferring %d\n", io_fd->balance.s_id, io_fd->last_sender);

                }

                break;

            case CS_REPLY:
//                printf("\tProcess %d: received reply from %d\n", io_fd->balance.s_id, io_fd->last_sender);
                left_to_receive--;
                break;

            case DONE:
                io_fd->received_time[io_fd->last_sender] = msg->s_header.s_local_time;
                io_fd->num_of_done++;
                break;
        }
        free(msg);

        if (!left_to_receive)
            waiting_in_queue = 0;

    } while(waiting_in_queue);

    return 0;
}


int release_cs(const void * self) {
    IOLinker * io_fd = (IOLinker *) self;
    char *blank_str = "blank";
    Message deferred_reply_msg;
    deferred_reply_msg.s_header.s_magic = MESSAGE_MAGIC;
    deferred_reply_msg.s_header.s_type = CS_REPLY;
    memcpy(&deferred_reply_msg.s_payload, blank_str, strlen(blank_str));
    deferred_reply_msg.s_header.s_payload_len = strlen(deferred_reply_msg.s_payload) + 1;

    set_time_msg(&deferred_reply_msg);

    for (int i = 1; i <= io_fd->subprocs_num; i++)
        if(io_fd->deferred_reply[i])
            send(io_fd, i, &deferred_reply_msg);

    return 0;
}


int run_subproc(IOLinker io_fd) {
    Message done_msg;
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
            io_fd.sent_at[i] = INT16_MAX;
            io_fd.received_time[i] = 0;
        }

        for (int i = 1; i <= num_to_send; ++i) {
            request_cs(&io_fd);
            sprintf(log_loop_operation, log_loop_operation_fmt, io_fd.balance.s_id, i, (io_fd.balance.s_id * 5));
            print(log_loop_operation);
            release_cs(&io_fd);
        }


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
    free(io_fd.received_time);
    free(io_fd.sent_at);
    free(io_fd.deferred_reply);

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
                init_balances[j] = 0;
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
            io_fd.sent_at = (timestamp_t*) malloc(sizeof(timestamp_t) * (subprocs_num + 1));
            io_fd.received_time = (timestamp_t*) malloc(sizeof(timestamp_t) * (subprocs_num + 1));
            io_fd.deferred_reply = (int*) malloc(sizeof(int) * (subprocs_num + 1));
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
