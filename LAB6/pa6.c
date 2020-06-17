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
    Message request_msg;

    // form request message
    request_msg.s_header.s_magic = MESSAGE_MAGIC;
    request_msg.s_header.s_type = CS_REQUEST;
    memcpy(&request_msg.s_payload, blank_str, strlen(blank_str));
    request_msg.s_header.s_payload_len = strlen(request_msg.s_payload) + 1;
    set_time_msg(&request_msg);

    int waiting = 1;
    io_fd->queue[io_fd->balance.s_id] = *io_fd->lamport_time_p;

    do {
        for (int k = 1; k <= io_fd->subprocs_num; ++k) {
            if (k != io_fd->balance.s_id)
                if (io_fd->forks[k].is_clear == 0 && io_fd->tokens[k].is_with_me == 1 && io_fd->forks[k].is_with_me == 1) {
                    // this process has no fork anymore
                    io_fd->forks[k].is_with_me = 0;

                    // sending fork to needed process
                    Message tmp_msg;
                    tmp_msg.s_header.s_magic = MESSAGE_MAGIC;
                    tmp_msg.s_header.s_type = CS_REPLY;
                    memcpy(&tmp_msg.s_payload, blank_str, strlen(blank_str));
                    tmp_msg.s_header.s_payload_len = strlen(tmp_msg.s_payload) + 1;
                    set_time_msg(&tmp_msg);

                    send(io_fd, k, &tmp_msg);
                }
        }

        int num_of_acceptions = 0;
        for (int j = 1; j <= io_fd->subprocs_num; j++) {
            if (j != io_fd->balance.s_id && io_fd->forks[j].is_with_me == 1) {
                if (io_fd->forks[j].is_clear == 1 || io_fd->tokens[j].is_with_me == 0) {
                    num_of_acceptions++;
                }
            }
        }

        if (num_of_acceptions == io_fd->subprocs_num - 1) {
            // enter cs
            waiting = 0;
        } else {
            // send token on condition
            for (int i = 1; i <= io_fd->subprocs_num; i++) {
                if (i != io_fd->balance.s_id)
                    if (io_fd->tokens[i].is_with_me == 1 && io_fd->forks[i].is_with_me == 0) {
                        io_fd->tokens[i].is_with_me = 0;
                        send(io_fd, i, &request_msg);
                    }
            }
        }

        // if this procees is last it's not possible to receive messages
        if (io_fd->num_of_done != io_fd->subprocs_num) {
            Message *msg = (Message *) malloc(sizeof(Message));
            receive_any(io_fd, msg);

            switch (msg->s_header.s_type) {
                case CS_REQUEST:
                    // now it has marker from last sender
                    io_fd->tokens[io_fd->last_sender].is_with_me = 1;

                    if (io_fd->forks[io_fd->last_sender].is_clear == 0 && waiting != 0) {
                        // this process has no fork anymore
                        io_fd->forks[io_fd->last_sender].is_with_me = 0;

                        msg->s_header.s_type = CS_REPLY;
                        set_time_msg(msg);
                        send(io_fd, io_fd->last_sender, msg);
                    }

                    break;

                case CS_REPLY:
                    // gain fork
                    io_fd->forks[io_fd->last_sender].is_with_me = 1;
                    // make it "clean"
                    io_fd->forks[io_fd->last_sender].is_clear = 1;

                    break;

                case DONE:
                    io_fd->num_of_done++;

                    break;
            }
            free(msg);
        }

    } while (waiting);

    return 0;
}


int release_cs(const void * self) {
    IOLinker * io_fd = (IOLinker *) self;

    // making forks "dirty"
    for (int i = 1; i <= io_fd->subprocs_num; ++i) {
        if (i != io_fd->balance.s_id)
            io_fd->forks[i].is_clear = 0;
    }

    return 0;
}


int run_subproc(IOLinker io_fd) {
    Message done_msg;
    Message start_msg;

    filter_pipes(&io_fd);

    // start block

    sprintf(start_msg.s_payload, log_started_fmt, get_lamport_time(), io_fd.balance.s_id,
            getpid(), getppid(), io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
    start_msg.s_header.s_magic = MESSAGE_MAGIC;
    start_msg.s_header.s_type = STARTED;
    start_msg.s_header.s_payload_len = strlen(start_msg.s_payload);

    log_write_all(start_msg.s_payload);
    confer_all(&io_fd, &start_msg);
    log_all_started(get_lamport_time(), io_fd.balance.s_id);

    char log_loop_operation[strlen(log_loop_operation_fmt)];

    if (io_fd.use_critical_section) {

        // actual work block

        int num_to_send = io_fd.balance.s_id * 5;

        // critical section
        for (int i = 1; i <= num_to_send; i++) {
            request_cs(&io_fd);
            sprintf(log_loop_operation, log_loop_operation_fmt, io_fd.balance.s_id, i, (io_fd.balance.s_id * 5));
            print(log_loop_operation);
            release_cs(&io_fd);
        }

        // finish block

        // return forks to other processes as we don't need them anymore
        for (int k = 1; k <= io_fd.subprocs_num; ++k) {
            if (k != io_fd.balance.s_id)
                if (io_fd.forks[k].is_clear == 0 && io_fd.tokens[k].is_with_me == 1 && io_fd.forks[k].is_with_me == 1) {
                    // this process has no fork anymore
                    io_fd.forks[k].is_with_me = 0;

                    // sending fork to needed process
                    char * blank_str = "blank";
                    Message tmp_msg;
                    tmp_msg.s_header.s_magic = MESSAGE_MAGIC;
                    tmp_msg.s_header.s_type = CS_REPLY;
                    memcpy(&tmp_msg.s_payload, blank_str, strlen(blank_str));
                    tmp_msg.s_header.s_payload_len = strlen(tmp_msg.s_payload) + 1;
                    set_time_msg(&tmp_msg);

                    send(&io_fd, k, &tmp_msg);
                }
        }

        sprintf(done_msg.s_payload, log_done_fmt, get_lamport_time(), io_fd.balance.s_id,
                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
        done_msg.s_header.s_magic = MESSAGE_MAGIC;
        done_msg.s_header.s_type = DONE;
        done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);
        log_write_all(done_msg.s_payload);

        send_multicast(&io_fd, &done_msg);

        // if current process finishes faster than others,
        // it will reply on incoming requests
        while(io_fd.num_of_done < io_fd.subprocs_num){

            Message *msg = (Message *) malloc(sizeof(Message));
            receive_any(&io_fd, msg);

            // maybe it is redundant block
            switch (msg->s_header.s_type) {
                case CS_REQUEST:
                    //io_fd->tokens[io_fd->last_sender].is_with_me = 1;

                    if (io_fd.forks[io_fd.last_sender].is_clear == 0) {
                        // this process has no fork anymore
                        io_fd.forks[io_fd.last_sender].is_with_me = 0;

                        msg->s_header.s_type = CS_REPLY;
                        //msg->s_header.s_local_time = 0;
                        set_time_msg(msg);
                        send(&io_fd, io_fd.last_sender, msg);
                    }

                    break;

                case DONE:
                    io_fd.num_of_done++;

                    break;
            }

            free(msg);
        }

        log_all_done(get_lamport_time(), io_fd.balance.s_id);

    } else {

        // actual work block

        for (int i = 1; i <= io_fd.balance.s_id * 5; i++) {
            sprintf(log_loop_operation, log_loop_operation_fmt, io_fd.balance.s_id, i, (io_fd.balance.s_id * 5));
            print(log_loop_operation);
        }

        // finish block

        sprintf(done_msg.s_payload, log_done_fmt, get_lamport_time(), io_fd.balance.s_id,
                io_fd.balance.s_history[io_fd.balance.s_history_len - 1].s_balance);
        done_msg.s_header.s_magic = MESSAGE_MAGIC;
        done_msg.s_header.s_type = DONE;
        done_msg.s_header.s_payload_len = strlen(done_msg.s_payload);

        log_write_all(done_msg.s_payload);
        confer_all(&io_fd, &done_msg);
        log_all_done(get_lamport_time(), io_fd.balance.s_id);
    }

    free(io_fd.received_at);
    free(io_fd.queue);

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

            io_fd.balance.s_id = i;
            io_fd.balance.s_history_len = 0;
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance = init_balances[i - 1];
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_time = get_lamport_time();
            io_fd.balance.s_history[io_fd.balance.s_history_len].s_balance_pending_in = 0;
            io_fd.balance.s_history_len++;

            io_fd.num_of_done = 1;
            io_fd.use_critical_section = use_critical_section;
            io_fd.queue = (timestamp_t*) malloc(sizeof(timestamp_t) * (subprocs_num + 1));
            io_fd.received_at = (timestamp_t*) malloc(sizeof(timestamp_t) * (subprocs_num + 1));

            // processes with higher indexes have more forks
            // those who have no fork gain marker
            for (int j = 1; j <= subprocs_num; j++)
                if (i != j) {
                    if (j < i) {
                        io_fd.forks[j].is_with_me = 1;
                        io_fd.forks[j].is_clear = 0;
                        io_fd.tokens[j].is_with_me = 0;
                    } else {
                        io_fd.forks[j].is_with_me = 0;
                        io_fd.forks[j].is_clear = 0;
                        io_fd.tokens[j].is_with_me = 1;
                    }
                } else {
                    io_fd.forks[j].is_with_me = -1;
                    io_fd.forks[j].is_clear = -1;
                    io_fd.tokens[j].is_with_me = -1;
                }

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
