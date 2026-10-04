#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <ctime>
#include <string>
#include <map>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define OPENAI_SERVER 1

int send_to_doubao();

int send_to_openai(char *offer, char *answer);