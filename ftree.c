#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <libgen.h>
#include <sys/types.h>
#include "ftree.h"
#include "hash.h"

/*
 * helper: read all from fd_read_from and write to fd_write_to
 */
void read_and_write(int fd_read_from, int fd_write_to, char *path, int user, size_t f_size){ // user is 1 if client is using this function. otherwise, if server is using this, user is 0
    int total_bytes_read = 0;
    while(1){
        if(user == 0){
            if(total_bytes_read == f_size){
                break;
            }
        }
        // Read data into buffer
        char *buffer = malloc(sizeof(char) * 128);
        
        long bytes_read = read(fd_read_from, buffer, sizeof(buffer));
        
        if(user == 0){
            total_bytes_read += bytes_read;
        }
        
        if(bytes_read == 0){ // We are done reading from the file
            break;
        }
        if(bytes_read < 0){
            perror("this side: read from file into buffer");
            close(fd_read_from);
            close(fd_write_to);
            break;
        }
        
        // Write data into destination
        void *p = buffer; // p keeps track of where we are in the buffer
        while(bytes_read > 0){
            long bytes_written = write(fd_write_to, p, bytes_read);
            if(bytes_written <= 0){
                fprintf(stderr, "this side: write from buffer to destination, file path: %s %s", path, strerror(errno));
                close(fd_read_from);
                close(fd_write_to);
                break;
            }
            bytes_read -= bytes_written;
            p += bytes_written;
        }

        free(buffer);
    }

}

/*
 * client-side
 */
int *send_a_file(char *src_path, char *dest_path, int sock_fd){
    int *result = malloc(sizeof(int)); // this function will return 0 if transmit successfully, ow return 1 for MATCH_ERROR received
    *result = 0;
    // first create and send the struct
    struct stat status;
    if(lstat(src_path, &status) == -1){
        perror("client: lstat");
        close(sock_fd);
        exit(1);
    }
    FILE *f = fopen(src_path, "r"); // open file to calc the hash value
    if(f == NULL){
        perror("client: fopen");
        close(sock_fd);
        exit(1);
    }
    struct fileinfo this_file;
    strcpy(this_file.path, dest_path);
    this_file.mode = status.st_mode;
    strncpy(this_file.hash, hash(f), 8);
    this_file.size = htonl(status.st_size);
    
    fclose(f);
    if(write(sock_fd, &this_file, sizeof(struct fileinfo)) <= 0){ // write the struct contains file info info to socket
        perror("client: write the struct into socket");
        close(sock_fd);
        exit(1);
    }

    // wait for and read server's response
    int indicator;
    long numread = read(sock_fd, &indicator, sizeof(indicator));
    
    if(numread == -1){
        perror("client: read server's response for struct");
        close(sock_fd);
        exit(1);
    }
    // continue after receiving the response
    if(indicator == MISMATCH){ // if MISMATCH is received, then client will transmit the file to server
        // set file descriptor
        int fd = open(src_path, O_RDWR);
        if(fd == -1){
            perror("client: open");
            close(sock_fd);
            exit(1);
        }
        
        // read from the file and write to socket
        errno = 0;
        read_and_write(fd, sock_fd, src_path, 1, status.st_size);
        if(errno){
            close(fd);
            close(sock_fd);
            exit(1);
        }
        close(fd);
        // wait for and read server's response
        int buf2;
        long numread = read(sock_fd, &buf2, sizeof(buf2));
        if(numread == -1){
            perror("client: read server's response for transmit");
            close(sock_fd);
            exit(1);
        }
        if(buf2 == TRANSMIT_ERROR){
            fprintf(stderr, "server: transmit, file path: %s %s", src_path, strerror(errno));
            close(sock_fd);
            exit(1);
        }
    }
    if(indicator == MATCH_ERROR){
        *result = 1;
    }
    return result;
}

int *send_a_dir(char *src_path, char *dest_path, int sock_fd){
    int *result = malloc(sizeof(int)); // this function will return 0 if transmit successfully, or return 1 for MATCH_ERROR received
    *result = 0;
    // first send the dir itself
    int *result1 = send_a_file(src_path, dest_path, sock_fd);
    // check if MATCH_ERROR is received, if it is, then no need to go into this directory
    if(*result1 == 0){ // no Error occured
        // loop the directory and send the files inside it
        struct dirent *dp;
        DIR *dfd;
        if((dfd = opendir(src_path)) == NULL){
            perror("client: opendir");
            close(sock_fd);
            exit(1);
        }
        char *file_src_path = malloc(sizeof(char) * MAXPATH);
        char *file_dest_path = malloc(sizeof(char) * MAXPATH);
        while((dp = readdir(dfd)) != NULL){

            // ignore any file whose name starts with "."
            if(dp->d_name[0] != '.'){
                sprintf(file_src_path, "%s/%s", src_path, dp->d_name);
                sprintf(file_dest_path, "%s/%s", dest_path, dp->d_name);
                if(dp->d_type == DT_DIR){

                    int *result2 = send_a_dir(file_src_path, file_dest_path, sock_fd);
                    if( *result2 == 1){
                        *result = 1;
                    }
                    free(result2);
                }
                if(dp->d_type == DT_REG){
                    int *result3 = send_a_file(file_src_path, file_dest_path, sock_fd);
                    if( *result3 == 1){
                        *result = 1;
                    }
                    free(result3);
                }
            }
        }
        free(file_src_path);
        free(file_dest_path);
    }
    else{ // MATCH_ERROR occurs
        *result = 1;
    }
    free(result1);
    return result;
}

int fcopy_client(char *src_path, char *dest_path, char *host, int port){
    int result = 0;
    // setting up the socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    // setting up the sockaddr_in to connect to
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    memset(&server.sin_zero, 0, 8);
    
    char *ip = malloc(sizeof(char) * (strlen(host) + 1));
    strcpy(ip, host);
    if(inet_pton(AF_INET, ip, &server.sin_addr) < 0){ // convert address in string to binary numbers, then store in sockadd(server)
        perror("client: inet_pton");
        close(sock_fd);
        exit(1);
    }
    free(ip); // free ip here since I dont use it anymore
    // Connect to the server
    if(connect(sock_fd, (struct sockaddr *) &server, sizeof(server)) == -1){
        perror("client: connect");
        close(sock_fd);
        exit(1);
    } // once connect, ready to send files
    // send file(s) to the server
    // first check if this is a directory or not
    struct stat buf;
    char *src = malloc(sizeof(char) * (strlen(src_path) + 1));
    strcpy(src, src_path);
    if(lstat(src, &buf) == -1){
        perror("client: lstat");
        close(sock_fd);
        exit(1);
    }
    // skip symbolic links
    // don't check the prefix "." here
    if(!S_ISLNK(buf.st_mode)){ // not a symbolic link
        // calc the expected path of this file in the server machine
        char *absolute_path = realpath(src, NULL); // to be safe, calc the absolute path using realpath
        
        char *absolute_path_copy = strdup(absolute_path);  // basename() might change the original path, so creat a copy on heap to store it
        
        char *base_name_sotre = basename(absolute_path_copy); // the name calc(returned) by basename, which is on the stack
        char *base_name = malloc(sizeof(char) * (strlen(base_name_sotre) + 1)); // to be safe, restore it on the heap
        strcpy(base_name, base_name_sotre);
        
        char *dest = malloc(MAXPATH);
        strcpy(dest, realpath(dest_path, NULL));
        strcat(dest, "/");
        strcat(dest, base_name);
        // check if the file is a directory or a regular file, treat it differently
        if(S_ISREG(buf.st_mode)){ // case: regular file
            result = *(send_a_file(src, dest, sock_fd));
        }
        if(S_ISDIR(buf.st_mode)){ // case: directory
            result = *(send_a_dir(src, dest, sock_fd));
        }
        free(dest);
        free(base_name);
        free(absolute_path); // need to be freed because it is returned by realpath
        free(absolute_path_copy);
    }
    free(src);
    close(sock_fd);
    return result;
}

/*
 * server-side
 */
int serve_client(int fd){ // return 0 if the client is still sending something, return 1 if the client has finished
    int if_match_message = MATCH;
    int if_transmited_message = TRANSMIT_OK;
    // read the struct from socket
    struct fileinfo buf;
    long num_struct_read;
    num_struct_read = read(fd, &buf, sizeof(buf));
    if(num_struct_read < 0){ //************** ?? read in to a struct???
        perror("server: read struct");
        close(fd);
    }
    if(num_struct_read == 0){
        return 1;
    }
    buf.size = ntohl(buf.size);
    
    // if this is a directory, check if it already exists
    if(S_ISDIR(buf.mode)){
        // check if there is a directory existing at the path given
        DIR *dir1 = opendir(buf.path);
        if(dir1){ // directory exists
            closedir(dir1);
        }
        else if(errno == ENOENT || errno == ENOTDIR){ // directory does not exist
            // check if there is a regular file existing at the path given
            FILE *f1 = fopen(buf.path, "r");
            if(f1 == NULL){
                if(errno == ENOENT){ // the regular file does not exist
                    // create a new directory
                    if(mkdir(buf.path, buf.mode) == -1){
                        perror("server: mkdir");
                        fclose(f1);
                        closedir(dir1);
                        close(fd);
                    }
                }
                else{ // other unexpected errors encountered
                    perror("server: fopen");
                    fclose(f1);
                    closedir(dir1);
                    close(fd);
                }
            }
            else{ // the regular file exists
                // tell the client that there is a MATCHERROR
                if_match_message = MATCH_ERROR;
                perror("server: MATCH_ERROR");
                closedir(dir1);
                fclose(f1);
            }
        }
        else{ // other unexpected error for opendir
            perror("server: opendir");
            closedir(dir1);
            close(fd);
        }
    }
    // if this is a file, check if it already exists
    if(S_ISREG(buf.mode)){
        // check if there is a directory existing at the path given
        DIR *dir2 = opendir(buf.path);
        if(dir2){ // directory exists
            // tell the client that there is a MATCHERROR
            if_match_message = MATCH_ERROR;
            perror("server: MATCH_ERROR");
            closedir(dir2);
        }
        else{
            if(errno == ENOENT || errno == ENOTDIR){ // the directory does not exist
                errno = 0; // reset errno for further checking
                // check if there is a regular file existing at the path given
                FILE *f2 = fopen(buf.path, "ab+"); // create if not exists
                if(f2 == NULL){
                    perror("server: fopen");
                    close(fd);
                }
                errno = 0;
                fclose(f2);
                // do not tell the client anything
                // need to check if there is a MATCH or MISMATCH
            }
            else{ // other unexpected errors encountered
                perror("server: opendir");
                close(fd);
            }
        }
    }
    // at this point, either there is a MATCHERROR is encountered or the file/directory at the given path must exist(already exist or not but newly created)
    // if a MATCHERROR is got, should send this to the client directly
    if(if_match_message == MATCH_ERROR){
        if(write(fd, &if_match_message, sizeof(int)) <= 0){
            perror("server: write");
            close(fd);
        }
    }
    else{ // else, continue for transmission
        struct stat this_file;
        if(lstat(buf.path, &this_file) == -1){
            fprintf(stderr, "server: lstat********** %s %s", buf.path, strerror(errno));
            close(fd);
        }
        if(S_ISREG(this_file.st_mode)){ // just consider the case when this is a regular file because there is no need to update a directory
            // check if there is need to update the file, by comparing size and hash
            if(this_file.st_size == buf.size){
                FILE *f3 = fopen(buf.path, "r");
                if(f3 == NULL){
                    perror("server: fopen");
                    close(fd);
                }
                if(strncmp(hash(f3), buf.hash, 8) == 0){ // no need to update, send MATCH
                    if_match_message = MATCH;
                }
                else{ // need to update, send MISMATCH
                    if_match_message = MISMATCH;
                }
                fclose(f3);
            }
            else{ // need to update, send MISMATCH
                if_match_message = MISMATCH;
            }
        }
        
        // send the message to the client
        // do not send MATCH_ERRO once again
        if(if_match_message == MATCH || if_match_message == MISMATCH){
            if(write(fd, &if_match_message, sizeof(int)) <= 0){
                perror("server: write");
                close(fd);
            }
        }
        // update the file if there is a MISMATCH
        if(if_match_message == MISMATCH){
            // open the file
            int fd_file = open(buf.path, O_RDWR);
            if(fd_file == -1){
                perror("server: open");
                close(fd);
                exit(1);
            }
            // read from socket and write to the file
            errno = 0;
            read_and_write(fd, fd_file, buf.path, 0, buf.size);
            
            
            close(fd_file);
            // tell the client if there is a TRANSMIT_OK or TRANSMIT_ERROR
            if(errno){
                if_transmited_message = TRANSMIT_ERROR;
            }
            else{
                if_transmited_message = TRANSMIT_OK;
            }
            
            
            if(write(fd, &if_transmited_message, sizeof(int)) <= 0){
                perror("server: write");
                close(fd);
            }
            
        }
    }
    // whatever the case is, change the permission of this file
    chmod(buf.path, buf.mode);
    return 0;
}

void fcopy_server(int port){
    // Create socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(sock_fd < 0){
        perror("server: socket");
        exit(1);
    }
    // Bind socket to an address
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = INADDR_ANY;
    memset(&server.sin_zero, 0, 8);
    if(bind(sock_fd, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) < 0){
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }
    // Create queue in kernel for new connection requests
    if(listen(sock_fd, 5) < 0){
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }
    while(1){
        // Accept a new connection
        int client_fd = accept(sock_fd, NULL, NULL);
        if(client_fd < 0){
            perror("server: accept");
            close(sock_fd);
            exit(1);
        }
        // Serve client
        int result; // this will be 0 if the client still have something to send to the server, ow this will be 1, used to check if the client is done
        while(1){
            result = serve_client(client_fd);
            if(result == 1){
                break;
            }
        }
    }
}




/* the process:
 1.set up server (fcopy_server)
    1. creat a socket(int, int, int) -> (family, type, protocal)
 2. bind the socket(), bind(sockfd, my_addr, addrlen) -> set up struct first (sin_port, sin_family, sin_addr, sin_zero)
 3. listen() change the mode to listen -> (sd, length of queue)
 4. while loop -> accept information -> (fd, NULL, NULL) 什么时候需要改变socket？
    1.helper funciton : process the struct client write to the server (only need to take the descriptor which contains the info from clients)
        1. read from the socket -> read into a struct
        2. use (path, mode, size, hash) to indicate [match, mismatch, match_error]
            1. if dir
                1. if exist -> check name
                    1. if same -> check size
                    [if exist a dir with same name -> return MATCH_ERROR]
                        1. if same -> return MATCH
                        2. if different -> return MISMATCH
                    2. if different -> return MISMATCH
                2. if not exist -> make dir and write whatever in it -> return MATCH
 
            2. if regular file
                1. if exist -> check name
                    1. if same -> check size
                        1. if same -> check hash
                            1.if same -> return MATCH
                            2.if differnt -> return MISMATCH
                        2. if different -> return MISMATCH
                    2. if different -> return MISMATCH
                2. if not exist -> make a file and write whatever in it -> return MATCH
        3. change the mode of any dir or file.
 
 
 CLIENT
 1. set up the client socket
 2. change the ip addr to binary
 3. connect using the binary addr
 4. creat a file_info struct
    1. get the src path and dest path
    2. real_path to get src path
    3. since basename() might change the src path -> creat a copy
    4. basename(copy) to get the actual file name
    5. combine the file name with dest_path -> get the dest_path
    6. mode, hash, src path is easy to get.
 5. send itself
    1. if dir -> send dir (file_path, dest_path, socket)
    -> SEND_DIR
    2. if regular file -> send file (file_path, dest_path, cokect)
    -> SEND FILE
 
 6. wait the message (MISMATCH, MATCH, MATCH_ERROR)
 7. process the message:
    1. if MATCH_ERROR -> error message print to client
    2. if MISMATCH -> send the file or dir
        1. if dir -> send dir (file_path, dest_path, socket)
            -> SEND_DIR
        2. if regular file -> send file (file_path, dest_path, cokect)
            -> SEND FILE
    3. if MATCH -> no need to send
    4. close file, dir, whatever needs to be closed
*/
