#include <syscall.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char** argv){
    if(argc < 2){
        printf("Usage: read <file>\n");
        return 1;
    }
    char* file = argv[1];
    printf("Reading file %s\n", file);

    int fd = open(file);
    if(fd < 0){
        printf("Error: Could not open file %s\n", file);
        return 1;
    }
    char* buffer=malloc(3096);
    int bytesRead = read(fd, buffer, 3096);
    if(bytesRead < 0){
        printf("Error: Could not read file %s\n", file);
        return 1;
    }
    buffer[bytesRead] = '\0';
    printf("byte read: %d\n%s\n",bytesRead, buffer);
    close(fd);
    return 0;
}