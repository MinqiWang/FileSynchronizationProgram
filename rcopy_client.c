#include <stdio.h>
#include <string.h>
#include "ftree.h"
#ifndef PORT
    #define PORT 51012
#endif

int main(int argc, const char * argv[]) {
    if (argc != 4) {
        perror("input: 3 variables are needed for fcopy_client.");
        exit(EXIT_FAILURE);
    }
    char *src_path = malloc(strlen(argv[1]));
    char *dest_path = malloc(strlen(argv[2]));
    char *host = malloc(strlen(argv[3]));
    strcpy(src_path, argv[1]);
    strcpy(dest_path, argv[2]);
    strcpy(host, argv[3]);
    fcopy_client(src_path, dest_path, host, PORT);
    return 0;
}
