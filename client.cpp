/*
    Original author of the starter code
    Tanzir Ahmed
    Department of Computer Science & Engineering
    Texas A&M University
    Date: 2/8/20

    Please include your Name, UIN, and the date below
    Name: Abdul Samad Khan
    UIN: 235000807
    Date: 09/20/2025
*/
#include "common.h"
#include "FIFORequestChannel.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <algorithm>

using namespace std;

static int buffercapacity = MAX_MESSAGE; 

static void ensure_dir(const string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == -1) {
        mkdir(path.c_str(), 0755);
    }
}

static pid_t launch_server_child(int mcap) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
       
        vector<char*> argv;
        argv.push_back(const_cast<char*>("./server"));
        string mstr;
        if (mcap > 0 && mcap != MAX_MESSAGE) {
            argv.push_back(const_cast<char*>("-m"));
            mstr = to_string(mcap);
            argv.push_back(const_cast<char*>(mstr.data()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        perror("execvp(server)");
        _exit(127);
    }
    return pid; // parent
}

static double request_data_point(FIFORequestChannel& ch, int p, double t, int e) {
    datamsg d(p, t, e);
    ch.cwrite((char*)&d, sizeof(d));
    double val = 0.0;
    ch.cread((char*)&val, sizeof(val));
    return val;
}

static __int64_t request_file_size(FIFORequestChannel& ch, const string& fname) {
    filemsg fm(0, 0);
    const int mlen = sizeof(filemsg) + (int)fname.size() + 1;
    vector<char> buf(mlen);
    memcpy(buf.data(), &fm, sizeof(filemsg));
    strcpy(buf.data() + sizeof(filemsg), fname.c_str());
    ch.cwrite(buf.data(), mlen);
    __int64_t fsz = 0;
    ch.cread((char*)&fsz, sizeof(fsz));
    return fsz;
}

static void request_file_chunk(FIFORequestChannel& ch, const string& fname,
                               __int64_t offset, int len, char* out) {
    filemsg fm(offset, len);
    const int mlen = sizeof(filemsg) + (int)fname.size() + 1;
    vector<char> buf(mlen);
    memcpy(buf.data(), &fm, sizeof(filemsg));
    strcpy(buf.data() + sizeof(filemsg), fname.c_str());
    ch.cwrite(buf.data(), mlen);
    ch.cread(out, len);
}

static FIFORequestChannel* request_new_channel(FIFORequestChannel& control) {
    MESSAGE_TYPE mt = NEWCHANNEL_MSG;
    control.cwrite((char*)&mt, sizeof(mt));
    char namebuf[256] = {0};
    control.cread(namebuf, sizeof(namebuf));
    return new FIFORequestChannel(namebuf, FIFORequestChannel::CLIENT_SIDE);
}

static void send_quit(FIFORequestChannel& ch) {
    MESSAGE_TYPE q = QUIT_MSG;
    ch.cwrite((char*)&q, sizeof(q));
}

int main (int argc, char *argv[]) {
    // -------- CLI parsing --------
    int opt;
    int p = -1;
    double t = -1.0;
    int e = -1;
    string filename;
    bool want_new_channel = false;

    // -c has no argument; -m, -p, -t, -e, -f have arguments
    while ((opt = getopt(argc, argv, "cm:p:t:e:f:")) != -1) {
        switch (opt) {
            case 'c': want_new_channel = true; break;
            case 'm': buffercapacity = atoi(optarg); break;
            case 'p': p = atoi(optarg); break;
            case 't': t = atof(optarg); break;
            case 'e': e = atoi(optarg); break;
            case 'f': filename = optarg; break;
        }
    }

  
    pid_t srv = launch_server_child(buffercapacity);
    // allow server to create FIFOs
    usleep(150 * 1000);

   
    FIFORequestChannel control("control", FIFORequestChannel::CLIENT_SIDE);
    FIFORequestChannel* chan = &control;
    vector<FIFORequestChannel*> all_channels;
    all_channels.push_back(&control);

   
    if (want_new_channel) {
        FIFORequestChannel* nc = request_new_channel(control);
        all_channels.push_back(nc);
        chan = nc;
    }

    int exitcode = 0;

   
    if (!filename.empty()) {
        // FILE TRANSFER
        ensure_dir("received");

        __int64_t fsz = request_file_size(*chan, filename);
        if (fsz < 0) {
            cerr << "Server returned negative file size for '" << filename << "'\n";
            exitcode = 1;
        } else {
            string outpath = "received/" + filename;
            // create subdirs if filename has slashes
            size_t slash = outpath.find_last_of('/');
            if (slash != string::npos) {
                ensure_dir(outpath.substr(0, slash));
            }
            FILE* fp = fopen(outpath.c_str(), "wb");
            if (!fp) {
                perror("fopen(received file)");
                exitcode = 1;
            } else {
                vector<char> chunk(buffercapacity);
                __int64_t remaining = fsz;
                __int64_t offset = 0;
                while (remaining > 0) {
                    int to_get = (int)min<__int64_t>(remaining, buffercapacity);
                    request_file_chunk(*chan, filename, offset, to_get, chunk.data());
                    if (fseeko(fp, offset, SEEK_SET) != 0) {
                        perror("fseeko");
                        exitcode = 1; break;
                    }
                    size_t wrote = fwrite(chunk.data(), 1, to_get, fp);
                    if ((int)wrote != to_get) {
                        perror("fwrite");
                        exitcode = 1; break;
                    }
                    offset += to_get;
                    remaining -= to_get;
                }
                fclose(fp);
            }
        }
    } else if (p != -1 && t >= 0.0 && (e == 1 || e == 2)) {
       
        double val = request_data_point(*chan, p, t, e);
        
        cout << fixed << setprecision(3) << val << endl;
    } else if (p != -1 && t < 0.0 && e == -1) {
        // FIRST 1000 DATA POINTS  -> x1.csv
        FILE* fp = fopen("x1.csv", "w");
        if (!fp) {
            perror("fopen(x1.csv)");
            exitcode = 1;
        } else {
            
            for (int i = 0; i < 1000; ++i) {
                double tt = i * 0.004; 
                double e1 = request_data_point(*chan, p, tt, 1);
                double e2 = request_data_point(*chan, p, tt, 2);
                fprintf(fp, "%.3f,%.3f,%.3f\n", tt, e1, e2);
            }
            fclose(fp);
        }
    } else {
        
        cerr << "Usage examples:\n"
             << "  ./client -p 10 -t 59.004 -e 2\n"
             << "  ./client -p 10\n"
             << "  ./client -f 5.csv\n"
             << "  ./client -c -f 5.csv\n";
    }

   
    for (auto* ch : all_channels) send_quit(*ch);
    if (chan != &control) delete chan;

    int status = 0;
    waitpid(srv, &status, 0);
    return exitcode;
}
