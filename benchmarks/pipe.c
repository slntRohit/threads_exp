//
// Created by rohit on 12/2/2018.
//
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "../utils.h"

#include "pipe.h"

#define TV_1 "/TIME1"
#define TV_2 "/TIME2"

#define NUM_ITERS 1000000

#define size_lt unsigned long long

int p[2];
char r;
char *msg = "r";

size_lt results[NUM_ITERS];
size_lt *timer_us, *timer_ns;

void *send_message();

void *recv_message();

int main(int argc, char **argv) {

    int t1, t2;
    int pid_1, pid_2;


    timer_us = (size_lt *) mmap(NULL, sizeof(size_lt), PROT_READ | PROT_WRITE, MAP_SHARED,
                             create_shm(&t1, TV_1, sizeof(size_lt)), 0);
    timer_ns = (size_lt *) mmap(NULL, sizeof(size_lt), PROT_READ | PROT_WRITE, MAP_SHARED,
                             create_shm(&t2, TV_2, sizeof(size_lt)), 0);


    if (pipe(p) < 0)
        perror("pipe");

    if ((pid_1 = fork()) == 0) {
        pthread_t thread;
        pthread_create(&thread, NULL, send_message, NULL);
        assign_thread_to_core(1, thread);
        int actual = get_core_number(thread);
        pthread_join(thread, NULL);
        exit(0);
    } else {
        pthread_t thread;
        pthread_create(&thread, NULL, recv_message, NULL);
        assign_thread_to_core(2, thread);
        int actual = get_core_number(thread);
        pthread_join(thread, NULL);
    }

    int pid, status;
    while ((pid = wait(&status)) > 0);
    size_lt sum;
    for(int i = 0; i< NUM_ITERS; i++) {
        sum += results[i];
    }
    printf("Total: %llu | Mean: %f", sum, ((double)sum)/ NUM_ITERS);

    munmap(timer_us, sizeof(size_lt));
    munmap(timer_ns, sizeof(size_lt));
    shm_unlink(TV_1);
    shm_unlink(TV_2);
}

static inline void start_timer() {
    *timer_ns = elapsed_time_ns(0);
    *timer_us = elapsed_time_us(0);
}

static inline size_lt stop_timer() {
//    *timer_ns = elapsed_time_ns(*timer_ns);
//    *timer_us = elapsed_time_us(*timer_us);
//    printf("Time taken: %llu us | Per iter: %f us\n", *timer_us, ((double)*timer_us)/NUM_ITERS);
//    printf("Time taken: %llu ns | Per iter: %f ns\n", *timer_ns, ((double)*timer_ns)/NUM_ITERS);
    return elapsed_time_ns(*timer_ns);
}

void *send_message() {
    close(p[0]);
    for (int i = 0; i < NUM_ITERS; i++) {
//        start_timer();
        write(p[1], msg, sizeof(char));
    }
    close(p[1]);
}

void *recv_message() {
    close(p[1]);
    for (int i = 0; i < NUM_ITERS; i++) {
        read(p[0], &r, sizeof(char));
//        results[i] = stop_timer();
        printf("\nread: %d", r);
    }
    stop_timer();
    close(p[0]);
}