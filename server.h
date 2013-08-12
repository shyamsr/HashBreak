#include <assert.h>

#define SPLIT 400000
#define MAX_WORKERS 120
#define MAX_REQUESTS 15

struct worker {
        int conn_id;
        char hash[44];
        char lower[15];
        char upper[15];
	int  req_index;
        int busy;
};

struct request {
        int conn_id;
        char hash[44];
        uint64_t upper;
        uint64_t next_job;
        int len;
	int pass_not_cracked;
	struct outstanding* rem_q_head;
};

struct outstanding {
	int conn_id;
        char hash[44];
        char lower[15];
        char upper[15];
	struct outstanding* next;
};

volatile sig_atomic_t run = 1;
