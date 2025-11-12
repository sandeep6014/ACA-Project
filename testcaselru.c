#include <stdio.h>
#include <stdlib.h>

#define CACHE_SIZE 16
#define NUM_ACCESSES 20

typedef struct {
    int tag;
    int valid;
} cache_block;

int block_size = 32;
int associativity = 1;

int cache_accesses = 0;
int cache_hits = 0;
int cache_misses = 0;
int writebacks = 0;

void init_cache(cache_block cache[], int size) {
    int i;
    for (i = 0; i < size; i++) {
        cache[i].valid = 0;
        cache[i].tag = -1;
    }
}

int access_cache(cache_block cache[], int address) {
    int cache_index = (address / block_size) % CACHE_SIZE;
    cache_accesses++;

    if (cache[cache_index].valid && cache[cache_index].tag == (address / block_size)) {
        cache_hits++;
        return 1;
    } else {
        cache_misses++;

        if (cache[cache_index].valid) {
            writebacks++;
        }

        cache[cache_index].tag = (address / block_size);
        cache[cache_index].valid = 1;

        return 0;
    }
}

void generate_accesses(int accesses[], int num) {
    int i;
    for (i = 0; i < num; i++) {
        accesses[i] = rand() % 1024;
    }
}

void print_cache_stats() {
    printf("\n--- CACHE STATS ---\n");
    printf("Cache Accesses : %d\n", cache_accesses);
    printf("Cache Hits     : %d\n", cache_hits);
    printf("Cache Misses   : %d\n", cache_misses);
    printf("Writebacks     : %d\n", writebacks);
    printf("Miss Rate      : %.2f%%\n", (cache_misses * 100.0) / cache_accesses);
    printf("Writeback Rate : %.2f%%\n", (writebacks * 100.0) / cache_accesses);
}

int main(int argc, char *argv[]) {
    cache_block cache[CACHE_SIZE];
    int accesses[NUM_ACCESSES];
    int i;

    if (argc < 3) {
        printf("Usage: %s <block_size> <associativity>\n", argv[0]);
        return 1;
    }

    block_size = atoi(argv[1]);
    associativity = atoi(argv[2]);

    printf("Running with Block Size = %d bytes, Associativity = %d-way\n\n",
            block_size, associativity);

    init_cache(cache, CACHE_SIZE);
    generate_accesses(accesses, NUM_ACCESSES);

    for (i = 0; i < NUM_ACCESSES; i++) {
        printf("Accessing address: %d\n", accesses[i]);
        access_cache(cache, accesses[i]);
    }

    print_cache_stats();
    return 0;
}

