#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "reveal.h"

#define MAX_PATH 4096
#define BIGGER_BUFFER (MAX_PATH * 2) // for paths that are might be longer
#define MAX_ENTRIES 4096

static int cmp(const void *a,const void *b){
    return strcmp(*(const char **)a, *(const char **)b);
} // comparision function for sorting 2 strings 

static int read_prev_dir(const char* home_dir,char* out,int out_size){
    char path[BIGGER_BUFFER];
    snprintf(path, sizeof(path), "%s/prev_dir.txt", home_dir);

    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;

    char line[BIGGER_BUFFER];
    if (fgets(line, sizeof(line), fp) == NULL) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' || isspace((unsigned char)line[len - 1]))) line[--len] = '\0';
    if (len == 0) return 0;
    snprintf(out, out_size, "%s", line);
    return 1;
}

static void reveal_list_dir(const char *path, const char *display_prefix, int show_all, int recursive) {
    DIR *dir = opendir(path);
    if (dir == NULL) return;

    char *names[MAX_ENTRIES];
    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        if (!show_all && entry->d_name[0] == '.')
            continue;
        names[count] = strdup(entry->d_name);
        count++;
        if (count >= MAX_ENTRIES) break;
    }
    closedir(dir);

    qsort(names, count, sizeof(char *), cmp);

    for (int i = 0; i < count; i++) {
        char fullpath[MAX_PATH];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, names[i]);

        struct stat st;
        int is_dir = (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode));

        if (display_prefix == NULL || strlen(display_prefix) == 0) {
            if (is_dir)
                printf("%s/\n", names[i]);
            else
                printf("%s\n", names[i]);
        } else {
            if (is_dir)
                printf("%s/%s/\n", display_prefix, names[i]);
            else
                printf("%s/%s\n", display_prefix, names[i]);
        }
    }

    if (recursive) {
        for (int i = 0; i < count; i++) {
            char fullpath[MAX_PATH];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", path, names[i]);

            struct stat st;
            if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode)) {
                char new_prefix[MAX_PATH];
                if (display_prefix == NULL || strlen(display_prefix) == 0)
                    snprintf(new_prefix, sizeof(new_prefix), "%s", names[i]);
                else
                    snprintf(new_prefix, sizeof(new_prefix), "%s/%s", display_prefix, names[i]);

                reveal_list_dir(fullpath, new_prefix, show_all, recursive);
            }
        }
    }

    for (int i = 0; i < count; i++) free(names[i]);
}



static int resolve_reveal_path(const char* arg,char *resolved,int resolved_size,const char* home_dir){
    char cwd[MAX_PATH];
    if(getcwd(cwd, sizeof(cwd)) == NULL) return -1;

    if(arg == NULL || strcmp(arg, ".") == 0) snprintf(resolved, resolved_size, "%s", cwd);
    else if(strcmp(arg, "~") == 0) snprintf(resolved, resolved_size, "%s", home_dir);
    else if(strcmp(arg, "..") == 0) {
        snprintf(resolved, resolved_size, "%s", cwd);
        char *slash = strrchr(resolved, '/');
        if(slash) {
            if(slash == resolved) resolved[1] = '\0';
            else *slash = '\0';
        }
    } 
    else if(strcmp(arg, "-") == 0) {
        char prev_dir[BIGGER_BUFFER];
        if(!read_prev_dir(home_dir, prev_dir, sizeof(prev_dir))) return -1;
        snprintf(resolved, resolved_size, "%s", prev_dir);
    } 
    else {
        if(arg[0] == '/') {
            snprintf(resolved, resolved_size, "%s", arg);
        } else if(arg[0] == '~' && arg[1] == '/') {
            snprintf(resolved, resolved_size, "%s/%s", home_dir, arg + 2);
        } else {
            snprintf(resolved, resolved_size, "%s/%s", cwd, arg);
        }
    }

    struct stat st;
    if(stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    return 0;
}

void reveal_command(char **args, int argc, const char *home_dir){
    int reveal_all = 0, recursive = 0;
    char *path_arg = NULL;
    int path_arg_count = 0;

    for(int i=1;i<argc;i++){
        if(args[i][0] == '-' && strlen(args[i]) > 1 && (args[i][1] == 'a' || args[i][1] == 't')){
            for(int j=1;j< (int)strlen(args[i]);j++){
                if(args[i][j] == 'a') reveal_all = 1;
                else if(args[i][j] == 't') recursive = 1;
                else{
                    printf("reveal: invalid syntax\n");
                }
            }
        }
        else{
            path_arg_count += 1;
            // strcpy(path_arg,args[i]); 
            path_arg = args[i];
        }
    }

    if(path_arg_count > 1){
        printf("reveal: invalid syntax\n");
    }

    char resolved[BIGGER_BUFFER];
    if(resolve_reveal_path(path_arg, resolved, sizeof(resolved), home_dir) != 0){
        printf("reveal: no such directory\n");
        return;
    }
    reveal_list_dir(resolved, "", reveal_all, recursive);

}
