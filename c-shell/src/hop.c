#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "hop.h" 

// IMPORTANT ASSUMPTION: hop() function takes in argc, *argv[]

#define STRING_SIZE 4096
char* home_directory;

typedef struct HopNode{
    char path[STRING_SIZE];
    int freq;
    long last_visit;
    struct HopNode *next;
}HopNode;

static HopNode *hopps = NULL;
static char previous_dir[STRING_SIZE] = ""; // initially none

static void get_history_path(char* buffer, int size_of_buffer){
    char* home = home_directory; // retutns the PATH of the home directory
    if(home == NULL){
        buffer[0] = '\0';
        return; // empty string
    }
    snprintf(buffer, size_of_buffer, "%s/hop_history.txt", home);
}

static void load_history(void){
    char history_path[STRING_SIZE];

    get_history_path(history_path,sizeof(history_path));
    if(history_path[0] == '\0') return;

    FILE* file = fopen(history_path,"r");
    if(file == NULL) return; // some error while opening

    char line[STRING_SIZE];

    while(fgets(line, sizeof(line), file) != NULL){
        char current_path_loaded[STRING_SIZE];
        int current_freq_loaded;
        long current_last_visit_loaded;

        if(sscanf(line, "%4095[^\t]\t%d\t%ld",current_path_loaded,&current_freq_loaded,&current_last_visit_loaded) !=3) continue;

        HopNode *node = malloc(sizeof(HopNode));
        if(node == NULL) break;

        strncpy(node->path, current_path_loaded,STRING_SIZE - 1);
        node->path[STRING_SIZE - 1] = '\0';

        node->freq = current_freq_loaded;
        node->last_visit = current_last_visit_loaded;

        node->next = hopps;
        hopps = node;
    }

    fclose(file);
}

static HopNode *find_node(char* path){

    HopNode *temp = hopps;

    while(temp != NULL){
        if(strcmp(temp->path,path) == 0) return temp;
        temp = temp->next;
    }
    return NULL;
}

static HopNode *create_node(char *path){
    HopNode *new_node = malloc(sizeof(HopNode));
    if (new_node == NULL) return NULL;

    strncpy(new_node->path, path, STRING_SIZE - 1);
    new_node->path[STRING_SIZE - 1] = '\0';
    new_node->freq = 1;
    new_node->last_visit = time(NULL);
    new_node->next = NULL;
    return new_node;
}

static bool directory_exists(char* path){
    struct stat st;

    if(stat(path, &st) != 0) return false;

    return S_ISDIR(st.st_mode);
}

static void record_visit(char* path){
    HopNode *node = find_node(path);
    
    // if found update it
    if(node != NULL){
        node->freq += 1;
        node->last_visit = time(NULL);
        return;
    }

    //if not found, add it

    HopNode *new_node = create_node(path);

    if(new_node == NULL) return; // error 

    new_node->next = hopps;
    hopps = new_node;
}

static void save_history(void){
    char history_path[STRING_SIZE];
    get_history_path(history_path, sizeof(history_path));

    if (history_path[0] == '\0') return;
    FILE *file = fopen(history_path, "w");
    if (file == NULL) return;

    HopNode *current = hopps;
    while (current != NULL){
        fprintf(file,"%s\t%d\t%ld\n",current->path,current->freq,current->last_visit);
        current = current->next;
    }

    fclose(file);
}

static int change_directory(char* target){
    char current_dir[STRING_SIZE];

    if(getcwd(current_dir,STRING_SIZE) == 0) return 0; // error in getting curr dir

    if(chdir(target) != 0) return 0; // not able to change 

    strcpy(previous_dir,current_dir);

    char new_dir[STRING_SIZE];

    if(getcwd(new_dir,STRING_SIZE) != NULL){
        record_visit(new_dir);
        save_history();
    }
    return 1;
}

static HopNode *find_best_match(char *name){
    HopNode *curr = hopps;
    HopNode *best = NULL;
    long long best_score = -1;

    while(curr != NULL){
        if(strstr(curr->path,name) != NULL){// found something common
            if(!directory_exists(curr->path)){
                curr = curr->next;
                continue;
            }
            long long score = (long long)curr->freq*1000000000LL + curr->last_visit;

            if (score > best_score) {
                best_score = score;
                best = curr;
            }
        }
        curr = curr->next;
    }
    return best;
}


int hop(int argc, char* argv[],char* home_dir){
    static bool is_initialized = false;
    home_directory = home_dir;    
    if(!is_initialized){
        load_history();
        is_initialized = true;
    }

    if(argc == 1){
        char *home = home_directory;
        if (home == NULL || !change_directory(home)) printf("hop: no such directory\n");
        return 0;
    }

    for(int i=1;i<argc;i++){
        char *argument = argv[i];

        if(strcmp(argument,"~") == 0){
            // write ~
            char *home = home_directory;
            if (home == NULL || !change_directory(home)) printf("hop: no such directory\n");
            continue;
        }

        if(strcmp(argument,".") == 0) continue;

        if(strcmp(argument,"..") == 0){
            change_directory("..");
            continue;
        }

        if(strcmp(argument,"-") == 0){
            if(previous_dir[0] == '\0') continue;
            change_directory(previous_dir);
            continue;
        }

        if(change_directory(argument)){
            continue;
        }

        HopNode *match = find_best_match(argument);

        if(match != NULL){
            if(!change_directory(match->path)){
                printf("hop: no such directory\n");
            }
        }else{
            printf("hop: no such directory\n");
        }

    }
    return 0;
}