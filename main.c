#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include "network.h"

//struct work_queue_item:
struct work_queue_item {
    int sock;
    struct work_queue_item *next;
    char *IPaddr;
    int port;
    char *time_stamp;
    char *http_info;
    int success_code;
    int response_size;
    char *response; // ADDED THIS
};

// global variable; can't be avoided because
// of asynchronous signal interaction
int still_running = TRUE;
struct work_queue_item *head = NULL;
struct work_queue_item *tail = NULL;
pthread_mutex_t work_mutex;
pthread_cond_t work_cond;
int queue_count = 0;

// if the path is a file:
//	function returns the size of the file
//if it is not a file, returns -1
int response(char* path) {
    struct stat buf;
    stat(path, &buf);
    if (S_ISREG(buf.st_mode) == 1) {
	return 1; 
    }
    else {
	return -1;    
    }
}

// updates word to only contain valid characters, returns new length
int shortenString(char* word) { 
    int i=0;
    for (; i<strlen(word); i++) {
	if (word[i]<=33) {  		  // file only contains / , #s, chars ???
	    word[i]='\0';		  // end where there is no valid chacter
	    return (i-1);
        }
    }
    return i;
}

// deletes the first character of a string
void ignore_first(char* word) {
    int i=1;
    for (; i<strlen(word)+1; i++) {
        word[i-1] = word[i];
    }
    word[strlen(word)] = '\0';
}

void write_to_file(struct work_queue_item item) {
    FILE *f = fopen("weblog.txt", "a"); //Creates an empty file for writing. If a file 						with the same name already exists it is added to 
    if (f == NULL)
    {
        printf("Error opening file\n");
    }
    printf("writing %s to file \n", item.http_info);
    fprintf(f,"%s:%d %s \"GET /%s\" %d %d\n", 
		item.IPaddr,
		item.port,
		item.time_stamp,
		item.http_info,
		item.success_code,
		item.response_size); // everything seems right with this, there is just line 						break in the middle ???
    fclose(f);
}

void print_list() {
    printf("--------------Printing The List--------------\n");
    int c = 0;
    struct work_queue_item* q = head;
    while (q != NULL) {
	c++;
	printf("%d)\n", c);
        printf("IP Address: %s\n", q->IPaddr);
        printf("Port: %d\n", q->port);
        printf("Time Stamp: %s\n", q->time_stamp);
	printf("File Name: %s\n", q->http_info);
	printf("Success Code: %d\n", q->success_code);
        printf("Response: %s\n", q->response);
	printf("Size: %d\n", q->response_size);
        printf("--------------Finished Printing the List--------------\n");
        q = q->next;
    }
    if (c==0) {
        printf("Nothing in Queue\n");
        printf("--------------Finished Printing the List--------------\n");
    }
}

void signal_handler(int sig) {
    still_running = FALSE;
}



void usage(const char *progname) {
    fprintf(stderr, "usage: %s [-p port] [-t numthreads]\n", progname);
    fprintf(stderr, "\tport number defaults to 3000 if not specified.\n");
    fprintf(stderr, "\tnumber of threads is 1 by default.\n");
    exit(0);
}

//concatenates 2 strings
//would rather use the C function char* strcat(char*, char*) but it wasn't working
char* concat(char* s1, char* s2, int len1, int len2) { 
    int total_len = len1 + len2;
    char* s_to_return = (char*) malloc(sizeof(char)*total_len + 1); //still need to worry about FREEING!
    int i=0;
    for (; i<strlen(s1); i++) {
	s_to_return[i] = s1[i];
    }
    int offset = strlen(s1);
    int j=0;
    for (; j<strlen(s2); j++) {
	s_to_return[j+offset] = s2[j];
    }
    s_to_return[total_len+1]= '\0';
    return s_to_return;
}

void add_to_tail(struct work_queue_item *item) {
    if (head == NULL) {
	head = item;
	tail = item;
    }
    else {
	tail->next = item;
	tail = item;
    }
}

void remove_from_head() {
    if (head->next == NULL) {  //one thing in list
	head = NULL;
	tail = NULL;
    }
    else {	//more than one thing in list
	head = head->next;
    }
}

char* create_response(char* path, struct work_queue_item new_item) {
    int file_size;
    FILE* f = fopen(path, "r");
    if (f==NULL) {
        printf("Error opening file\n");
        exit(1);
    }
    fseek(f, 0L, SEEK_END); 
    file_size = ftell(f); // get the size of the file
    fseek(f, 0L, SEEK_SET); // seek back to beginning of file
    char* start_response = HTTP_200;
    char rest_response[file_size];
    fgets(rest_response, file_size, f);
    char* res = concat(start_response, rest_response, strlen(start_response), strlen(rest_response));
    char* res_m = (char*) malloc(sizeof(char)*strlen(res)+1);
    res_m = res;
    return res_m;
}

void* workers() {
    while (1) {
	pthread_mutex_lock(&work_mutex);
	while (queue_count == 0) {
	    pthread_cond_wait(&work_cond, &work_mutex);
	}
	struct work_queue_item *item_removed = head; //always removing the head
	senddata(item_removed->sock, item_removed->response, item_removed->response_size);
        printf("--------------Sending This Data--------------\n");
        printf("Socket Number: %d\n", item_removed->sock);
	printf("Response: %s\n", item_removed->response);
	printf("Response Size: %d\n", item_removed->response_size);
        printf("--------------Finished Printing senddata params--------------\n");
        remove_from_head();
        write_to_file(*item_removed); // log the request
        print_list();
        queue_count--;
        pthread_mutex_unlock(&work_mutex);
    }
}


void runserver(int numthreads, unsigned short serverport) {
//make pool of threads:
    int i = 0;
    pthread_t threads[numthreads];  //array of pthreads to access
    for (; i < numthreads; i++) {
	pthread_create(&threads[i], NULL, &workers, NULL);
    }
    //////////////////////////////////////////////////
    
    
    int main_socket = prepare_server_socket(serverport);
    if (main_socket < 0) {
        exit(-1);
    }
    signal(SIGINT, signal_handler);

    struct sockaddr_in client_address;
    socklen_t addr_len;

    fprintf(stderr, "Server listening on port %d.  Going into request loop.\n", serverport);
    while (still_running) {
        struct pollfd pfd = {main_socket, POLLIN};
        int prv = poll(&pfd, 1, 10000);

        if (prv == 0) {
            continue;
        } else if (prv < 0) {
            PRINT_ERROR("poll");
            still_running = FALSE;
            continue;
        }
        
        addr_len = sizeof(client_address);
        memset(&client_address, 0, addr_len);

        int new_sock = accept(main_socket, (struct sockaddr *)&client_address, &addr_len);
        if (new_sock > 0) {
            
            time_t now = time(NULL);
            fprintf(stderr, "Got connection from %s:%d at %s\n", inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port), ctime(&now));

	
           ////////////////////////////////////////////////////////
           /* You got a new connection.  Hand the connection off
            * to one of the threads in the pool to process the
            * request.
            *
            * Don't forget to close the socket (in the worker thread)
            * when you're done.
            */
           ////////////////////////////////////////////////////////
	    
	    char request_buffer[1024];
	    struct work_queue_item new_item;
	    int result = getrequest(new_sock, request_buffer, 1024);
	    if (result < 0) {
	    	printf("Error. getrequest() failed.\n");
    	    }
            // updating the new item
            printf("GOT A REQUEST: %s\n", request_buffer);
	    new_item.sock = new_sock;
	    new_item.IPaddr = (inet_ntoa(client_address.sin_addr));
	    new_item.port = ntohs(client_address.sin_port);
	    new_item.time_stamp = ctime(&now);
            int len2 = shortenString(request_buffer);
	    new_item.http_info = request_buffer;
            char* path = request_buffer;
            if (request_buffer[0] == '/') {
                ignore_first(request_buffer);
            }
            char* dir = "./";
            path = concat(dir, request_buffer, 2, len2-1);
            printf("NEW PATH = %s\n", path);
            int is_file = response(path);
	    if (is_file < 0) {
		new_item.success_code = 404;
        	char* response = HTTP_404;
        	new_item.response_size = strlen(response);
		new_item.response = response;
	    }
	    else {
	        new_item.success_code = 200;
		char* response = create_response(path, new_item);
                new_item.response = response;
                new_item.response_size = strlen(response);
	    }
            pthread_mutex_lock(&work_mutex);
            add_to_tail(&new_item);
            print_list();
	    queue_count += 1;  //increments queue so workers can work
	    pthread_cond_signal(&work_cond);
	    pthread_mutex_unlock(&work_mutex);
        }
        close(new_sock);
    }
    fprintf(stderr, "Server shutting down.\n");
        
    close(main_socket);

}


int main(int argc, char **argv) {
    unsigned short port = 3000;
    int num_threads = 1;

    int c;
    while (-1 != (c = getopt(argc, argv, "hp:t:"))) {
        switch(c) {
            case 'p':
                port = atoi(optarg);
                if (port < 1024) {
                    usage(argv[0]);
                }
                break;

            case 't':
                num_threads = atoi(optarg);
                if (num_threads < 1) {
                    usage(argv[0]);
                }
                break;
            case 'h':
            default:
                usage(argv[0]);
                break;
        }
    }

    runserver(num_threads, port);
    
    fprintf(stderr, "Server done.\n");
    exit(0);
}
