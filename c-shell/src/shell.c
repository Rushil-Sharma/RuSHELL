#include <stdio.h>
#include "prompt.h"

int main(){
    //INIT THE HOME DIR
    prompt_init();
    
    while(1){
        //Definations
        char command[4096];
        char *prompt;
        //Code
        prompt = promptPrinter(); 
        printf(prompt); // prints the prompt
        scanf("%s",command);
    }
    return 0;
}