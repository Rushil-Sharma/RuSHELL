    #include <stdio.h>
    #include <stdlib.h>
    #include <unistd.h>
    #include <string.h>
    #include <pwd.h>

    #include "prompt.h"

    #define STRING_SIZE 4096

    static char home_dir[STRING_SIZE];
    char curr_dir[STRING_SIZE];
    char* prompt_init(void){
        getcwd(home_dir, sizeof(home_dir));
        return home_dir;
    }


    char* get_username(void){
        struct passwd *pw = getpwuid(geteuid());
        if(pw != NULL && pw->pw_name != NULL){
            return pw->pw_name;
        }
        return "username"; // this happens in the case the getpwuid doesn't work.
    }

    char* get_path(char* str1, char* str2){
        // (str1 = curr, str2 = home) directory
        int len2 = strlen(str2);
        if(strcmp(str1, str2) == 0) return "~";
        if(strncmp(str1, str2, len2) == 0 && str1[len2] == '/'){
            static char buf[STRING_SIZE];
            snprintf(buf, sizeof(buf), "~%s", str1 + len2);
            return buf;
        }
        return str1;
    }

    char* prompt_printer(){
        //Definations
        char host[256];
        static char prompt_final[STRING_SIZE];
        //Code
            // prompt_init(); this will be innited in main so that the home directory is not changed during the execution.
        char* user_name = get_username();
        getcwd(curr_dir, sizeof(curr_dir));
        gethostname(host, sizeof(host));
        //Print for test
            // printf("Home dir = %s\nCurr dir = %s\n",home_dir,curr_dir);
            // printf("Name of the user %s\n",user_name);
            // printf("Host : %s\n",host);
        //Final Print
        sprintf(prompt_final,"<%s@%s:%s>",user_name,host,get_path(curr_dir,home_dir));
        return prompt_final;
    }

    // int main(){
    //     prompt_printer();
    //     return 0;
    // }