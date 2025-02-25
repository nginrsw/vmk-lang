/*
** $Id: vmk2lua.c $
** transpiler vmk to lua files
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

// Change 'lck' to 'local', 'fn' to 'function' and 'str.' to 'string.'
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
            if (strncmp(src, "lck", 3) == 0 && !isalnum((unsigned char)src[3])) {
                strcpy(dest, "local");
                src += 3;
                dest += 5;
            } else if (strncmp(src, "fn", 2) == 0 && !isalnum((unsigned char)src[2])) {
                strcpy(dest, "function");
                src += 2;
                dest += 8;
			} else if ((src == buffer || (!isalnum((unsigned char)src[-1]) && src[-1] != '_')) 
			    && strncmp(src, "str.", 4) == 0) {
			    strcpy(dest, "string.");
			    src += 4;
			    dest += 7;
			}
			 else {
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

// replace the file extension from .vmk to .lua
void renameFileIfNeeded(const char *oldPath) {
    char newPath[1024];
    snprintf(newPath, sizeof(newPath), "%s", oldPath);
    
    char *pos;
    while ((pos = strstr(newPath, "vmk")) != NULL) {
        memcpy(pos, "lua", 3);
    }
    
    if (strcmp(oldPath, newPath) != 0) {
        rename(oldPath, newPath);
    }
}

// recursive trace the directory and process .vmk files
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
        } else if (strstr(entry->d_name, ".vmk")) {
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
