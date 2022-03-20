#include <stdio.h>
#include <stdlib.h>
#include "easyloggingpp/easylogging++.h"

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv) {
    START_EASYLOGGINGPP(argc, argv);
    LOG(INFO) << "Starting Live Streaming";
    return 0;
}
