# FileSynchronizationProgram
This program can help copy and transmit files from a client machine to a server machine; Can be used to
create local copies if the client and the server are the same computer; Need internet

How to use the program?
1. use command "make" to create the executables(on both the server machine and the client machine)
2. run the executable with name "rcopy_server" on the server machine, this will start the server
3. To transmit the files: run the executable with name "rcopy_client" on the client machine with 3 arguments: the source path(full path) of the file(or directory) that you want to transmit on the client machine, the destination path(full path) of the directory(must exist) on the server machine where you want the file to be transmitted to, the ip address of the server machine
