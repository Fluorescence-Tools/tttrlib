//
// Created by thoma on 31.12.2018.
//
#include "tttr_test.h"


static void show_usage(std::string name) {
    std::cerr << "Usage: " << name
              << "\nOptions:\n"
              << "\t-h \tShow this help message\n"
              << "\t-i \tSpecify the input file\n"
              << "\t-n \tNumber of processed records\n"
              << std::endl;
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        show_usage(argv[0]);
        return 1;
    }
    std::vector<std::string> sources;
    char* input;
    char* header;
    unsigned long long n_ph;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        switch (arg.at(1)) {
            case 'i':
                input = argv[++i];
                std::cout << "Input file: " << input << std::endl;
                break;
            case 'n':
                n_ph = atoi(argv[++i]);
                std::cout << "Number of photons: " << n_ph << std::endl;
                break;
            default:
                show_usage(argv[0]);
                return 0;
        }
    }
    for(int i=0; i<100; i++){
        TTTR reader = TTTR(input, 1);
        reader.read_file();
    }
}

