#include <iostream>
#include <iterator>
#include <algorithm>
#include <unistd.h>
#include <climits>
#include <sys/wait.h>
#include "CRTurnQueue.hpp"

#ifndef NUM_ITERS
#define NUM_ITERS 1000000
#endif

#ifndef NUM_RUNS
#define NUM_RUNS 3
#endif

#define NUM_THREAD 8

#define size_lt unsigned long long
#define SHM_G "GLOBAL"
#define SHM_Q1 "NODE_POOL_1"
#define SHM_Q2 "NODE_POOL_2"
#define SHM_Q3 "NODE_POOL_3"
#define SHM_Q4 "NODE_POOL_4"


pthread_barrier_t *barrier_t;
size_lt *reqs;

static void *sender(void *params);

static void *intermediate(void *params);

static void *receiver(void *params);

static inline size_lt elapsed_time_ns(size_lt ns) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000L + t.tv_nsec - ns;
}

int assign_thread_to_core(int core_id, pthread_t pthread) {
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    return pthread_setaffinity_np(pthread, sizeof(cpu_set_t), &cpuset);
}

typedef struct params {
    int id;
    CRTurnQueue<size_lt> *q12;
    CRTurnQueue<size_lt> *q23;
    CRTurnQueue<size_lt> *q32;
    CRTurnQueue<size_lt> *q21;
    size_lt buf;
} params;

void shm_cleanup() {
    shared_memory_object::remove(SHM_G);
    shared_memory_object::remove(SHM_Q1);
    shared_memory_object::remove(SHM_Q2);
    shared_memory_object::remove(SHM_Q3);
    shared_memory_object::remove(SHM_Q4);

    struct shm_remove {
        shm_remove() {
            shared_memory_object::remove(SHM_G);
            shared_memory_object::remove(SHM_Q1);
            shared_memory_object::remove(SHM_Q2);
            shared_memory_object::remove(SHM_Q3);
            shared_memory_object::remove(SHM_Q4);
        }

        ~shm_remove() {
            shared_memory_object::remove(SHM_G);
            shared_memory_object::remove(SHM_Q1);
            shared_memory_object::remove(SHM_Q2);
            shared_memory_object::remove(SHM_Q3);
            shared_memory_object::remove(SHM_Q4);
        }
    } remover;
}

int main() {

    shm_cleanup();
    size_t size = 50000000; // 50 MB

    fixed_managed_shared_memory managed_shm(create_only, SHM_G, size, (void *) 0x1000000000L);

    barrier_t = managed_shm.construct<pthread_barrier_t>(anonymous_instance)();
    pthread_barrierattr_t barattr;
    pthread_barrierattr_setpshared(&barattr, PTHREAD_PROCESS_SHARED);
    pthread_barrier_init(barrier_t, &barattr, NUM_THREAD * 3);
    pthread_barrierattr_destroy(&barattr);

    fixed_managed_shared_memory shm1(create_only, SHM_Q1, size);
    fixed_managed_shared_memory shm2(create_only, SHM_Q2, size);
    fixed_managed_shared_memory shm3(create_only, SHM_Q3, size);
    fixed_managed_shared_memory shm4(create_only, SHM_Q4, size);
    CRTurnQueue<size_lt> *q12 = managed_shm.construct<CRTurnQueue<size_lt>>(anonymous_instance)(&shm1,
                                                                                                NUM_THREAD * 2);
    CRTurnQueue<size_lt> *q23 = managed_shm.construct<CRTurnQueue<size_lt>>(anonymous_instance)(&shm2,
                                                                                                NUM_THREAD * 2);
    CRTurnQueue<size_lt> *q32 = managed_shm.construct<CRTurnQueue<size_lt>>(anonymous_instance)(&shm3,
                                                                                                NUM_THREAD * 2);
    CRTurnQueue<size_lt> *q21 = managed_shm.construct<CRTurnQueue<size_lt>>(anonymous_instance)(&shm4,
                                                                                                NUM_THREAD * 2);
    params p[NUM_THREAD];
    for (int i = 0; i < NUM_THREAD; i++) {
        p[i].id = i;
        p[i].q12 = q12;
        p[i].q23 = q23;
        p[i].q32 = q32;
        p[i].q21 = q21;
    }

    reqs = managed_shm.construct<size_lt>(anonymous_instance)[NUM_ITERS]();
    for (int i = 0; i < NUM_ITERS; i++) reqs[i] = i;


    if (fork() == 0) {
        pthread_t s_threads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&s_threads[i], nullptr, sender, &p[i]);
            assign_thread_to_core(i, s_threads[i]);
        }

        for (unsigned long s_thread : s_threads) {
            pthread_join(s_thread, nullptr);
        }
        size_lt avg = 0, min = INT_MAX, max = 0;
        for (auto &i : p) {
            avg += i.buf;
            min = min > i.buf ? i.buf : min;
            max = max > i.buf ? max : i.buf;
        }
        printf("Avg time taken: %f ns | Max: %llu | Min: %llu\n", ((double) avg) / NUM_THREAD, max, min);
        printf("Process 1 completed\n");
        exit(0);
    }

    if (fork() == 0) {
        pthread_t i_threads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&i_threads[i], nullptr, intermediate, &p[i]);
            assign_thread_to_core(i + NUM_THREAD, i_threads[i]);
        }

        for (unsigned long i_thread : i_threads) {
            pthread_join(i_thread, nullptr);
        }
        printf("Process 2 completed\n");
        exit(0);
    }

    if (fork() == 0) {
        pthread_t r_threads[NUM_THREAD];
        for (int i = 0; i < NUM_THREAD; i++) {
            pthread_create(&r_threads[i], nullptr, receiver, &p[i]);
            assign_thread_to_core(i + NUM_THREAD * 2, r_threads[i]);
        }
        for (unsigned long r_thread : r_threads) {
            pthread_join(r_thread, nullptr);
        }
        printf("Process 3 completed\n");
        exit(0);
    }

    int status;
    while (wait(&status) > 0) std::cout << "Process Exit status: " << status << std::endl;
}

static void *sender(void *par) {
    auto *p = (params *) par;
    CRTurnQueue<size_lt> *q12 = p->q12;
    CRTurnQueue<size_lt> *q21 = p->q21;
    int id = p->id;
    size_lt start[NUM_RUNS];
    size_lt end[NUM_RUNS];
    size_lt delay[NUM_RUNS];
    size_lt time[NUM_RUNS];
    size_lt *res;

    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t);

        delay[j] = elapsed_time_ns(0);
        start[j] = elapsed_time_ns(0);
        for (int i = 0; i < NUM_ITERS; i++) {
            q12->enqueue(&reqs[i], id);
            while ((res = q21->dequeue(id)) == nullptr);
        }
        end[j] = elapsed_time_ns(0);
        if (res != nullptr) printf("Verify S: %llu\n", *res);
        if (res == nullptr) printf("Verify is NULL");

        size_lt timer_overhead = start[j] - delay[j];
        time[j] = end[j] - start[j] - timer_overhead;
        printf("Time taken for #%d: %llu ns | Per iter: %f ns | ID: %d\n", j, time[j],
               ((double) time[j]) / NUM_ITERS, id);
//        printf("Verify S: %llu\n", *res);
    }
    size_lt avg = 0;
    for (unsigned long long j : time) {
        avg += llround(((double) j) / NUM_ITERS);
    }
    printf("Sender completed\n");
    p->buf = llround(((double) avg) / NUM_RUNS);
    return 0;
}

static void *intermediate(void *par) {
    auto *p = (params *) par;
    CRTurnQueue<size_lt> *q12 = p->q12;
    CRTurnQueue<size_lt> *q23 = p->q23;
    CRTurnQueue<size_lt> *q32 = p->q32;
    CRTurnQueue<size_lt> *q21 = p->q21;
    int id = p->id + NUM_THREAD;
    size_lt *res;
    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t);
        for (int i = 0; i < NUM_ITERS; i++) {
            while ((res = q12->dequeue(id)) == nullptr);
            q23->enqueue(res, id);
            while ((res = q32->dequeue(id)) == nullptr);
            q21->enqueue(res, id);
        }
    }
//    printf("Verify: %llu\n", *res);
    printf("Intermediate completed\n");
    return 0;
}

static void *receiver(void *par) {
    auto *p = (params *) par;
    CRTurnQueue<size_lt> *q23 = p->q23;
    CRTurnQueue<size_lt> *q32 = p->q32;
//    int id = p->id;
    int id = 0;
    size_lt *res;
    for (int j = 0; j < NUM_RUNS; j++) {
        pthread_barrier_wait(barrier_t);
        for (int i = 0; i < NUM_ITERS; i++) {
            while ((res = q23->dequeue(id)) == nullptr);
            q32->enqueue(res, id);
        }
    }
    printf("Receiver completed\n");
//    printf("Verify R: %d\n", s23->buf);
    return 0;
}