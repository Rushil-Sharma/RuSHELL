#include <stdio.h>
#include "prompt.h"

int main(){
    //INIT THE HOME DIR
    prompt_init();
    
    while(1){
        promptPrinter(); // prints the prompt
    }
    return 0;
}