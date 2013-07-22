#pragma once
#include <math.h>
#include <stdlib.h>


extern double epoch_lth;
extern int epoch_cnt;
extern double drop_rate;

void* read_from_clients(void*);
void* write_to_clients(void*);
void* read_from_server(void*);
void* write_to_server(void*);
void* epoch_for_a_client(void*);
void* epoch_clientside(void*);
float gen_random();
