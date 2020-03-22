#include <stdio.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "tokenize.h"
#include "tcp-utils.h"

/*
 * Global configuration variables.
 */
const char* path[] = {"/bin","/usr/bin",0}; // path, null terminated (static)
const char* prompt = "\nsshell> ";            // prompt
const char* config = "shconfig";            // configuration file

/*
 * Typical reaper.
 */
void zombie_reaper (int signal) {
    int ignore;
    while (waitpid(-1, &ignore, WNOHANG) >= 0)
        /* NOP */;
}

/*
 * Dummy reaper, set as signal handler in those cases when we want
 * really to wait for children.  Used by run_it().
 *
 * Note: we do need a function (that does nothing) for all of this, we
 * cannot just use SIG_IGN since this is not guaranteed to work
 * according to the POSIX standard on the matter.
 */
void block_zombie_reaper (int signal) {
    /* NOP */
}

/*
 * run_it(c, a, e, p) executes the command c with arguments a and
 * within environment p just like execve.  In addition, run_it also
 * awaits for the completion of c and returns a status integer (which
 * has the same structure as the integer returned by waitpid). If c
 * cannot be executed, then run_it attempts to prefix c successively
 * with the values in p (the path, null terminated) and execute the
 * result.  The function stops at the first match, and so it executes
 * at most one external command.
 */
int run_it (const char* command, char* const argv[], char* const envp[], const char** path) {

    // we really want to wait for children so we inhibit the normal
    // handling of SIGCHLD
    signal(SIGCHLD, block_zombie_reaper);

    int childp = fork();
    int status = 0;

    if (childp == 0) { // child does execve
#ifdef DEBUG
        fprintf(stderr, "%s: attempting to run (%s)", __FILE__, command);
        for (int i = 1; argv[i] != 0; i++)
            fprintf(stderr, " (%s)", argv[i]);
        fprintf(stderr, "\n");
#endif
        execve(command, argv, envp);     // attempt to execute with no path prefix...
        for (size_t i = 0; path[i] != 0; i++) { // ... then try the path prefixes
            char* cp = new char [strlen(path[i]) + strlen(command) + 2];
            sprintf(cp, "%s/%s", path[i], command);
#ifdef DEBUG
            fprintf(stderr, "%s: attempting to run (%s)", __FILE__, cp);
            for (int i = 1; argv[i] != 0; i++)
                fprintf(stderr, " (%s)", argv[i]);
            fprintf(stderr, "\n");
#endif
            execve(cp, argv, envp);
            delete [] cp;
        }

        // If we get here then all execve calls failed and errno is set
        char* message = new char [strlen(command)+10];
        sprintf(message, "exec %s", command);
        perror(message);
        delete [] message;
        exit(errno);   // crucial to exit so that the function does not
                       // return twice!
    }

    else { // parent just waits for child completion
        waitpid(childp, &status, 0);
        // we restore the signal handler now that our baby answered
        signal(SIGCHLD, zombie_reaper);
        return status;
    }
}

/*
 * Implements the internal command `more'.  In addition to the file
 * whose content is to be displayed, the function also receives the
 * terminal dimensions.
 */
void do_more(const char* filename, const size_t hsize, const size_t vsize) {
    const size_t maxline = hsize + 256;
    char* line = new char [maxline + 1];
    line[maxline] = '\0';

    // Print some header (useful when we more more than one file)
    printf("--- more: %s ---\n", filename);

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        sprintf(line, "more: %s", filename);
        perror(line);
        delete [] line;
        return;
    }

    // main loop
    while(1) {
        for (size_t i = 0; i < vsize; i++) {
            int n = readline(fd, line, maxline);
            if (n < 0) {
                if (n != recv_nodata) {  // error condition
                    sprintf(line, "more: %s", filename);
                    perror(line);
                }
                // End of file
                close(fd);
                delete [] line;
                return;
            }
            line[hsize] = '\0';  // trim longer lines
            printf("%s\n", line);
        }
        // next page...
        printf(":");
        fflush(stdout);
        fgets(line, 10, stdin);
        if (line[0] != ' ') {
            close(fd);
            delete [] line;
            return;
        }
    }
    delete [] line;
}


int connect_it(const char* host, const unsigned short port) {
    int sd;
    // interrupted system calls will be reissued
    while ((sd = connectbyportint(host, port)) < 0 && errno == EINTR)
        /* NOP*/ ;
    if (sd == err_host) {
        fprintf(stderr, "Cannot find host %s.\n", host);
        return -1;
    }
    if (sd < 0) {
        perror("connectbyport");
        return -1;
    }
    // we now have a valid, connected socket
    printf("Connected to %s on port %d.\n", host, port);
    return sd;
}


int do_communicate(int sd, char* const request[], 
                   const char* host, const unsigned short port, const int keepalive) {
    #ifdef DEBUG
    fprintf(stderr, "%s: do_communicate received descriptor %d.\n", __FILE__, sd);
    #endif

    // connect unless already connected
    if (sd < 0) {
        sd = connect_it(host, port);
    }
    if (sd < 0) {
        // failed connection attempt
        // error already reported by connect_it
        #ifdef DEBUG
        fprintf(stderr, "%s: do_communicate returns descriptor %d.\n", __FILE__, sd);
        #endif
        return sd;
    }

    // have socket, will deliver
    for (int i = 0; request[i] != 0; i++) {
        send(sd, request[i], strlen(request[i]), 0);
        if (request[i + 1] != 0)
            send(sd, " ", 1, 0);
    }
    send(sd, "\n", 1, 0);
    // should probably check for sending errors, but they are so
    // unlikely, and we will probably detect the problems when we
    // attempt to receive anyway

    // receive and print answer
    const int ALEN = 256;
    char ans[ALEN];
    int n;
    while ((n = recv_nonblock(sd, ans, ALEN-1, 500)) != recv_nodata) {
        if (n == 0) {
            close(sd);
            printf("Connection closed by %s.\n", host);
            #ifdef DEBUG
            fprintf(stderr, "%s: do_communicate returns descriptor %d.\n", __FILE__, -1);
            #endif
            return -1;
        }
        if (n < 0) {
            // problem with receiving, socket probably unusable
            perror("recv_nonblock");
            shutdown(sd, SHUT_WR);
            close(sd);
            #ifdef DEBUG
            fprintf(stderr, "%s: do_communicate returns descriptor %d.\n", __FILE__, -1);
            #endif
            return -1;
        }
        ans[n] = '\0';
        printf("%s", ans);
        fflush(stdout);
    }

    // close socket if applicable
    if (!keepalive) {
        shutdown(sd, SHUT_WR);
        close(sd);
        #ifdef DEBUG
        fprintf(stderr, "%s: do_communicate returns descriptor %d.\n", __FILE__, sd);
        #endif
        return -1;
    }

    #ifdef DEBUG
    fprintf(stderr, "%s: do_communicate returns descriptor %d.\n", __FILE__, sd);
    #endif
    return sd;
}

int main (int argc, char** argv, char** envp) {
    unsigned short port = 0;      // port for remote connection
    char host[129];               // remote host
    host[128] = '\0';             // make sure we have a string
    int keepalive = 0;            // keep connections alive? (default no)
    int rsocket = -1;             // client socket (not connected = negative value)
    int bgpipe[2];                // pipe that allows bacground commands to announce
                                  // the closure of rsocket.
    int remote_err = 0;           // true whenever remote configuration
                                  // variables are not good.
    size_t hsize = 0, vsize = 0;  // terminal dimensions, read from
                                  // the config file
    char command[129];   // buffer for commands
    command[128] = '\0';
    char* com_tok[129];  // buffer for the tokenized commands
    size_t num_tok;      // number of tokens

    printf("Simple shell v2.0.\n");

    // Config:
    int confd = open(config, O_RDONLY);
    if (confd < 0) {
        perror("config");
        fprintf(stderr, "config: cannot open the configuration file.\n");
        fprintf(stderr, "config: will now attempt to create one.\n");
        confd = open(config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        if (confd < 0) {
            // cannot open the file, giving up.
            perror(config);
            fprintf(stderr, "config: giving up...\n");
            return 1;
        }
        close(confd);
        // re-open the file read-only
        confd = open(config, O_RDONLY);
        if (confd < 0) {
            // cannot open the file, we'll probably never reach this
            // one but who knows...
            perror(config);
            fprintf(stderr, "config: giving up...\n");
            return 1;
        }
    }

    // read terminal size and remote information
    int host_set = 0;
    while (hsize == 0 || vsize == 0 || port == 0 || host_set == 0) {
        int n = readline(confd, command, 128);
        if (n == recv_nodata)
            break;
        if (n < 0) {
            sprintf(command, "config: %s", config);
            perror(command);
            break;
        }
        num_tok = str_tokenize(command, com_tok, strlen(command));
        // note: VSIZE and HSIZE can be present in any order in the
        // configuration file, so we keep reading it until the
        // dimensions are set (or there are no more lines to read)
        if (strcmp(com_tok[0], "VSIZE") == 0 && atol(com_tok[1]) > 0)
            vsize = atol(com_tok[1]);
        else if (strcmp(com_tok[0], "HSIZE") == 0 && atol(com_tok[1]) > 0)
            hsize = atol(com_tok[1]);
        // remote host and port
        else if (strcmp(com_tok[0], "RPORT") == 0 && atoi(com_tok[1]) > 0)
            port = (unsigned short)atoi(com_tok[1]);
        else if (strcmp(com_tok[0], "RHOST") == 0) {
            strncpy(host,com_tok[1],128);
            host_set = 1;
        }
        // lines that do not make sense are hereby ignored
    }
    close(confd);

    if (hsize <= 0) {
        // invalid horizontal size, will use defaults (and write them
        // in the configuration file)
        hsize = 75;
        confd = open(config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        write(confd, "HSIZE 75\n", strlen( "HSIZE 75\n"));
        close(confd);
        fprintf(stderr, "%s: cannot obtain a valid horizontal terminal size, will use the default\n", 
                config);
    }
    if (vsize <= 0) {
        // invalid vertical size, will use defaults (and write them in
        // the configuration file)
        vsize = 40;
        confd = open(config, O_WRONLY|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR);
        write(confd, "VSIZE 40\n", strlen( "VSIZE 40\n"));
        close(confd);
        fprintf(stderr, "%s: cannot obtain a valid vertical terminal size, will use the default\n",
                config);
    }

    if (port <= 0) {
        fprintf(stderr, "%s: remote port not specified or not valid.\n", config);
        remote_err = 1;
    }

    printf("Terminal set to %ux%u.\n", (unsigned int)hsize, (unsigned int)vsize);

    // We attempt to resolve the host to see whether we can connect.
    struct hostent *hinfo;
    hinfo = gethostbyname(host);
    if (hinfo == NULL) {
        if (strlen(host) == 0) {
            strcpy(host, "(null)");
        }
        fprintf(stderr, "%s: host name \"%s\" does not resolve.\n", config, host);
        remote_err = 1;
     }

    if (remote_err)
        fprintf(stderr, "%s: remote commands are disabled.\n", config);

    if (! remote_err) {
        if (keepalive)
            printf("keepalive is on.\n");
        else
            printf("keepalive is off.\n");
        printf("Peer is %s:%d.\n", host, port);
    }

    if (pipe(bgpipe) < 0) {
        perror("comm pipe");
        return 1;
    }

    // install the typical signal handler for zombie cleanup
    // (we will inhibit it later when we need a different behaviour,
    // see run_it)
    signal(SIGCHLD, zombie_reaper);

    // Ignore SIGPIPE
    signal(SIGPIPE, block_zombie_reaper);

    // Command loop:
    while(1) {

        // Re-initialize rsocket from the pipe as appropriate:
        struct pollfd pipepoll;
        pipepoll.fd = bgpipe[0];
        pipepoll.events = POLLIN;
    
        int polled;
        while ((polled = poll(&pipepoll,1,100)) != 0 || 
               (polled == -1 && errno == EINTR) ) {
            if (polled == -1 && errno != EINTR) {
                perror("Pipe poll");
                // Arguably we do not want to kill the program since
                // failing to read from the pipe is an error condition
                // with a limited scope, meaning that most of the
                // program probably functions correctly still.
            }
            if (polled == -1 && errno == EINTR)
                continue;
            char rsocket_str[128];
            rsocket_str[127] = '\0';
            readline(bgpipe[0], rsocket_str, 126);
            rsocket = atoi(rsocket_str);
            #ifdef DEBUG
            fprintf(stderr, "%s: remote socket from pipe: %d\n", __FILE__, rsocket);
            #endif
        }

        // Read input:
        printf("%s",prompt);
        fflush(stdin);
        if (fgets(command, 128, stdin) == 0) {
            // EOF, will quit
            printf("\nBye\n");
            return 0;
        }
        // fgets includes the newline in the buffer, get rid of it
        if(strlen(command) > 0 && command[strlen(command) - 1] == '\n')
            command[strlen(command) - 1] = '\0';

        // Tokenize input:
        char** real_com = com_tok;  // may need to skip the first
                                    // token, so we use a pointer to
                                    // access the tokenized command
        num_tok = str_tokenize(command, real_com, strlen(command));
        real_com[num_tok] = 0;      // null termination for execve

        int remote = 1;   // remote command unless first token is a bang
        if (strcmp(com_tok[0], "!") == 0) { // bang?
            #ifdef DEBUG                    // bang!
            fprintf(stderr, "%s: local command\n", __FILE__);
            #endif
            remote = 0;
            // discard the first token now that we know what is means...
            real_com = com_tok + 1;  
        }

        int bg = 0;
        if (strcmp(com_tok[0], "&") == 0) { // background command coming
#ifdef DEBUG
            fprintf(stderr, "%s: background command\n", __FILE__);
#endif
            bg = 1;
            // discard the first token now that we know that it
            // specifies a background process...
            real_com = com_tok + 1;  
        }

        // ASSERT: num_tok > 0

        // Process input:
        if (strlen(real_com[0]) == 0) // no command, luser just pressed return
            continue;

        else if (remote) {  // remote commands are the simplest, we
                            // just send them to the server
            if (remote_err) {
                fprintf(stderr, "%s: host/port not initialized, remote commands are disabled.\n", __FILE__);
            }
            else {
                if (bg) {  // background    
                    // connect unless already connected
                    if (rsocket < 0) {
                        rsocket = connect_it(host, port);
                        if (rsocket < 0) {
                            // failed connection attempt
                            // error already reported by connect_it
                            continue;
                        }
                    }
                    
                    int bgp = fork();
                    if (bgp == 0) { // child handles execution
                        int rsocket1 = do_communicate(rsocket, real_com, host, port, keepalive);
                        printf("remote & %s done.\n", real_com[0]);
                        #ifdef DEBUG
                        fprintf(stderr, "%s: rsocket before: %d, after: %d\n", __FILE__, rsocket, rsocket1);
                        #endif
                        if (rsocket != rsocket1) { // socket change!
                            char rsocket_str[128];
                            rsocket_str[127] = '\0';
                            snprintf(rsocket_str, 127, "%d\n", rsocket1);
                            write(bgpipe[1], rsocket_str, strlen(rsocket_str));
                            #ifdef DEBUG
                            fprintf(stderr, "%s: remote socket to pipe: %s\n", __FILE__, rsocket_str);
                            #endif
                        }
                        exit(0);
                    }
                    else { // parent goes ahead and accept next command
                        continue;
                    }
                }
                else  // foreground
                    rsocket = do_communicate(rsocket, real_com, host, port, keepalive);
            }
        }

        else if (strcmp(real_com[0], "exit") == 0) {
            // one more thing to take care of: closing the socket
            if (rsocket > 0) {
                printf("Closing connection to %s:%d.\n", host, port);
                shutdown(rsocket, SHUT_WR);
                close(rsocket);
            }
            printf("Bye\n");
            return 0;
        }

        #ifdef DEBUG
        else if (strcmp(real_com[0], "env") == 0) {
            printf("rsocket=%d\n", rsocket);
        }
        #endif

        else if (strcmp(real_com[0], "keepalive") == 0) {
            keepalive = 1;
            printf("keepalive is on.\n");
        }

        else if (strcmp(real_com[0], "close") == 0) {
            keepalive = 0;
            printf("keepalive is off.\n");
            if (rsocket > 0) {
                printf("Closing connection to %s:%d.\n", host, port);
                shutdown(rsocket, SHUT_WR);
                close(rsocket);
                rsocket = -1;
            }
        }

        else if (strcmp(real_com[0], "more") == 0) {
            // note: more never goes into background (any prefixing
            // `&' is ignored)
            if (real_com[1] == 0)
                printf("more: too few arguments\n");
            // list all the files given in the command line arguments
            for (size_t i = 1; real_com[i] != 0; i++) 
                do_more(real_com[i], hsize, vsize);
        }

        else { // external command
            if (bg) {  // background command, we fork a process that
                       // awaits for its completion
                int bgp = fork();
                if (bgp == 0) { // child executing the command
                    int r = run_it(real_com[0], real_com, envp, path);
                    printf("& %s done (%d)\n", real_com[0], WEXITSTATUS(r));
                    if (r != 0) {
                        printf("& %s completed with a non-null exit code\n", real_com[0]);
                    }
                    return 0;
                }
                else  // parent goes ahead and accepts the next command
                    continue;
            }
            else {  // foreground command, we execute it and wait for completion
                int r = run_it(real_com[0], real_com, envp, path);
                if (r != 0) {
                    printf("%s completed with a non-null exit code (%d)\n", real_com[0], WEXITSTATUS(r));
                }
            }
        }
    }
}
