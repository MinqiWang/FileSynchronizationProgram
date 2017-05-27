#include <stdio.h>
#include <stdlib.h>
#include "hash.h"

char *hash(FILE *f){
    char *result = malloc(sizeof(char) * HASH_SIZE);
    // first initialize all charactors to be '\0'
    for (int i = 0; i < HASH_SIZE; i++){
        result[i] = '\0';
    }
    // compute the hash
    char input;
    long index = 0; // to keep track of the position in hash string
    while (fscanf(f, "%c", &input) != EOF) {
        result[index] = result[index] ^ input;
        index ++;
        if (index == HASH_SIZE){
            index = 0;
        }
    }
    return result;
}
