#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "ipc.h"
#include "ipc.c"

int get_amount_of_child(int argc, char **argv)
{
    if (argc < 2 || strcmp(argv[1], "-p")) {
        perror("Required argument -p not specified.\n");
        return -1;
    }
    return (int)strtol(argv[2], NULL, 10);
}


int wait_for_children(int child_num, pid_t * child_pids)
{

    for (int i = 0; i < child_num; i++)
        waitpid(child_pids[i], NULL, 0);
    return 0;
}

int proc_notify_all()

int run_child()
{
    return 0;
}


int fork_process(int child_num)
{
    pid_t *pids = (pid_t *)malloc((child_num + 1) * sizeof(pid_t));
    pids[PARENT_ID] = getpid();

    for (int i = 1; i <= child_num; i++)
    {
        pid_t fork_pid = fork();

        if (fork_pid == 0)
            return run_child();

        if (fork_pid != 0)
            pids[i] = fork_pid;

    }

    return wait_for_children(child_num, pids);
}


int main(int argc, char **argv)
{

    int child_num = get_amount_of_child(argc, argv);

    return fork_process(child_num);
}

