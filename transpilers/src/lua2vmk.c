/*
** $Id: lua2vmk.c $
** transpiler lua to vmk files
** See Copyright Notice in vmk.h
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#define BUFFER_SIZE 8192

// Change 'local' to 'lck', 'function' to 'fn' and 'string.' to 'str.'
void replaceContentInFile(const char *filePath) {
    FILE *file = fopen(filePath, "r");
    if (!file) {
        perror("Error opening file");
        return;
    }
    
    char tempPath[1024];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", filePath);
    FILE *tempFile = fopen(tempPath, "w");
    if (!tempFile) {
        perror("Error creating temp file");
        fclose(file);
        return;
    }
    
    char buffer[BUFFER_SIZE];
    while (fgets(buffer, BUFFER_SIZE, file)) {
        char tempBuffer[BUFFER_SIZE];
        char *src = buffer, *dest = tempBuffer;

        while (*src) {
            if (strncmp(src, "local", 5) == 0 && !isalnum((unsigned char)src[5])) {
                strcpy(dest, "lck");
                src += 5;
                dest += 3;
            } else if (strncmp(src, "function", 8) == 0 && !isalnum((unsigned char)src[8])) {
                strcpy(dest, "fn");
                src += 8;
                dest += 2;
            } else if ((src == buffer || (!isalnum((unsigned char)src[-1]) && src[-1] != '_')) 
                && strncmp(src, "string.", 7) == 0) {
                strcpy(dest, "str.");
                src += 7;
                dest += 4;
            } else {
                *dest++ = *src++;
            }
        }
        *dest = '\0';
        fputs(tempBuffer, tempFile);
    }
    
    fclose(file);
    fclose(tempFile);
    
    rename(tempPath, filePath);
}

// replace the file extension from .lua to .vmk
void renameFileIfNeeded(const char *oldPath) {
    char newPath[1024];
    snprintf(newPath, sizeof(newPath), "%s", oldPath);
    
    char *pos;
    while ((pos = strstr(newPath, "lua")) != NULL) {
        memcpy(pos, "vmk", 3);
    }
    
    if (strcmp(oldPath, newPath) != 0) {
        rename(oldPath, newPath);
    }
}

// recursive trace the directory and process .lua files
void traverseDirectory(const char *dirPath) {
    DIR *dir = opendir(dirPath);
    if (!dir) {
        perror("Error opening directory");
        return;
    }
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char fullPath[1024];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", dirPath, entry->d_name);
        
        struct stat pathStat;
        stat(fullPath, &pathStat);
        
        if (S_ISDIR(pathStat.st_mode)) {
            traverseDirectory(fullPath);
        } else if (strstr(entry->d_name, ".lua")) {
            replaceContentInFile(fullPath);
            renameFileIfNeeded(fullPath);
        }
    }
    closedir(dir);
}

// calling traversedirectory in the current dir
int main() {
    traverseDirectory(".");
    return 0;
}
