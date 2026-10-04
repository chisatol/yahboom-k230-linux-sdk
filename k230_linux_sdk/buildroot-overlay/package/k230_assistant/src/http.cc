#include "http.h"
#include "llm_interface.h"

void http_request(char *offer, char *answer){
    #if defined(OPENAI_SERVER)
        send_to_openai(offer,answer);
    #elif defined(DOUBAO_SERVER)
        printf("%s\n","DouBao Server is not support!");
    #endif
    printf("%s\n","send an http post request, and obtain the response data through a callback. send offer,get answer!");
}
