#include <stdio.h>
#include "ftree.h"
#ifndef PORT
    #define PORT 51012
#endif

int main(int argc, const char * argv[]) {
    if (argc != 1) {
        perror("input: no variables are needed in total for fcopy_server");
        exit(EXIT_FAILURE);
    }
    
    fcopy_server(PORT);
    
    return 0;
}
