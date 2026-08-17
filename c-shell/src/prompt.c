#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>

static char homeDir[4096];
char currDir[4096];
void prompt_init(void) {
    getcwd(homeDir, sizeof(homeDir));
}


char* getUsername(void) {
    struct passwd *pw = getpwuid(geteuid());
    if (pw != NULL && pw->pw_name != NULL) {
        return pw->pw_name;
    }
    return "username"; // this happens in the case the getpwuid doesn't work.
}

char* getPath(char* str1,char* str2){
    //(str1 = curr, str2 = home) directory
    if(strcmp(str1,str2) == 0) return ""; // same string = only ~ 
    if(strncmp(str1,str2,strlen(str2)) == 0) return (str1 + strlen(str2));
    else return str1;
}

void promptPrinter(){
    //Definations
    char host[256];
    //Code
        // prompt_init(); this will be innited in main so that the home directory is not changed during the execution.
    char* userName = getUsername();
    strcpy(currDir,homeDir);
    gethostname(host, sizeof(host));
    //Print for test
        // printf("Home dir = %s\nCurr dir = %s\n",homeDir,currDir);
        // printf("Name of the user %s\n",userName);
        // printf("Host : %s\n",host);
    //Final Print
    printf("<%s@%s:~%s>",userName,host,getPath(currDir,homeDir));
}

// int main(){
//     promptPrinter();
//     return 0;
// }