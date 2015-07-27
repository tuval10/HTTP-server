/* ~~~~~~~~~~ThreadsQueue.h~~~~~~~~~~ */
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/wait.h> // for wait macros etc
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <dirent.h>
#include <fcntl.h>  // for open flags
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

// structs
typedef struct list_node {
	int key;
	struct list_node * next;
} list_node;

typedef list_node * node_link;

typedef struct threads_queue{
	node_link head, tail;
	int max_size;
	int size;
	int liveness; // 1 when alive, 0 when killed
	pthread_mutex_t mutex;
	pthread_cond_t cond_not_full;
	pthread_cond_t cond_not_empty;
	pthread_cond_t cond_empty;
} threads_queue;

typedef threads_queue * queue_link;

//functions
queue_link init_queue(int max_size);
int enqueue(queue_link ql, int key);
int dequeue(queue_link ql);
void kill_queue(queue_link ql, int threadNum);
int queue_size(queue_link ql);
void destroy_queue(queue_link ql);

/* ~~~~~~~~~~ThreadsQueue.h end~~~~~~~~~~ */

/* ~~~~~~~~~~~~~~~ThreadsQueue.c~~~~~~~~~~~~~~~~~ */

queue_link init_queue(int max_size){
	queue_link q_link;
	assert( ( q_link = (queue_link) malloc(sizeof(threads_queue)) ) != NULL && "malloc failure");
	q_link -> liveness = 1;
	q_link -> size = 0;
	q_link -> max_size = max_size;
	assert( (! pthread_mutex_init( &(q_link -> mutex), NULL) ) && "mutex initializing failure");
	assert( (! pthread_cond_init( &(q_link -> cond_not_empty), NULL)) && "mutex condition initializing failure");
	return q_link;
}

// insert to the queue, return 1 on success 0 on failure
int enqueue(queue_link ql, int key){
	
	//initializing the item
	node_link item_link;
	assert(	( item_link = (node_link) malloc(sizeof(list_node)) ) != NULL && "malloc failure");
	item_link -> key = key;
	item_link -> next = NULL;
	//locking the queue and trying to push it	
	assert( ! pthread_mutex_lock( &(ql -> mutex) ));
	if (( ql->size == ql->max_size) || (! ql -> liveness)){ //can't get value because it was killed by SIGKILL
		assert( ! pthread_mutex_unlock( &(ql -> mutex) ));
		return 0;
	}
	if( ql -> size == 0)
		ql -> head = ql -> tail = item_link;
	else
		ql -> tail -> next = item_link;
	++ ql->size;
	//signal it isn't empty
	pthread_cond_signal( &(ql -> cond_not_empty) );
	pthread_mutex_unlock( &(ql -> mutex) );
	return 1;
}

// return the socket fd from the program queue, 
// if socket is dead (SIGKILL was sent) and the queue is empty returns -1
int dequeue(queue_link ql){
	node_link first_item_link;
	int key;
	
	assert( ! pthread_mutex_lock( &(ql -> mutex) ));
	while( ql -> size == 0 && ql -> liveness == 1) //alive but empty
		pthread_cond_wait( &(ql -> cond_not_empty), &(ql->mutex) );
	if(ql -> size == 0){ //dead and empty
		assert( ! pthread_mutex_unlock( &(ql -> mutex) ));
		return -1;
	}
	// not empty - alive or dead	
	first_item_link = ql -> head;
	ql -> head = ql -> head -> next;
	if(ql -> head == NULL)
		ql -> tail == NULL;
	-- ql -> size;
	assert( ! pthread_mutex_unlock( &(ql -> mutex) ));
	
	//end of locked segment
	key =  first_item_link -> key;
	free (first_item_link);
	return key;
}

void kill_queue(queue_link ql, int threadNum){
	int i;
	assert( ! pthread_mutex_lock( &(ql -> mutex) ));
	ql -> liveness = 0;
	assert( ! pthread_mutex_unlock( &(ql -> mutex) ));
	// cleaning the queue
	while (ql -> size != 0) {
		assert( ! pthread_mutex_lock( &(ql -> mutex) ));
		pthread_cond_signal( &(ql -> cond_not_empty) );
		assert( ! pthread_mutex_unlock( &(ql -> mutex) ));
	}
	//terminating the threads
	for(i = 0 ; i < threadNum ; ++i){
		assert( ! pthread_mutex_lock( &(ql -> mutex) ));
		pthread_cond_signal( &(ql -> cond_not_empty) );
		assert( ! pthread_mutex_unlock( &(ql -> mutex) ));
	}
}

int queue_size(queue_link ql){
	int size;
	assert( ! pthread_mutex_lock( &(ql -> mutex) ));
	size = ql -> size;
	assert( ! pthread_mutex_unlock( &(ql -> mutex) ));
	return size;
}

//destroy the queue - assume it is empty.
void destroy_queue(queue_link ql){
	pthread_mutex_destroy( &(ql -> mutex) );	
	pthread_cond_destroy( &(ql -> cond_not_empty) );
}

/* ~~~~~~~~~~~~~~~ThreadsQueue.c end~~~~~~~~~~~~~~~~~ */

/* ~~~~~~~~~~~~~~~ http_server.h ~~~~~~~~~~~~~~~~~ */
#define MAXBUFF 1024
#define MAX_SIM_CONNECTIONS 10 //it was written in the forum by the TA
#define HTML_DIR_PREFIX "\r\n<html><head>Dir</head><body><h1>"
#define HTML_FILE_PREFIX "<html><head>Dir</head><body>"
#define HTML_DIR_POSTFIX "</h1></body><HTML>"
#define HTML_NEWLINE "<br/>"
#define NEWLINE "\r\n"
#define NOT_FOUND_BODY "\r\n<html><head>Not Found</head>" \
	"<body><h1>sorry the page you are looking for cannot be found." \
	"</h1></body></html>"
#define RESPONSE_PREFIX "HTTP/1.1 "

// macro
#define write_to_socket(connfd, sendBuff) write_to_socket_nbytes(connfd, sendBuff, strlen(sendBuff) )

// globals
queue_link q;
int threadNum;
pthread_t * threadList;
int sockfd;

// functions
void write_to_socket_nbytes(int connfd, char *sendBuff, int nbytes);
void write_response_to_socket(int connfd, int error_num);
void *connection_handler(void *);
static char* create_message(int status);
void sig_handler(int sig);
int make_socket(unsigned short int port);
/* ~~~~~~~~~~~~~~~ http_server.h end ~~~~~~~~~~~~~~~~~ */

/* ~~~~~~~~~~~~~~~ http_server.c ~~~~~~~~~~~~~~~~~ */

/* handling SIGINT */
void sig_handler(int sig){
	int threadIndex;
	signal(sig, SIG_IGN);
	kill_queue(q , threadNum); /* kill the queue and terminates the threads */
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex)
		assert( ! pthread_join(threadList[threadIndex], NULL) ); 
	destroy_queue(q);	
	close(sockfd); /* closing the socket */
	exit(0); /* exit grcefully */
}

/* make a socket and bind it to port & address*/
int make_socket(unsigned short int port){
	struct sockaddr_in name;
	int sock;
	assert( (sock = socket(AF_INET, SOCK_STREAM, 0)) >= 0 && "creating socket failed");
	name.sin_family = AF_INET;
	name.sin_port = htons(port);
	name.sin_addr.s_addr = htonl(INADDR_ANY);
	assert( bind(sock, (struct sockaddr *) &name, sizeof (name)) >= 0 && "bind failure");
	return sock;
}

/*create a message respective to the status number sent */
static char* create_message(int status){
	char *buff;
	buff = calloc(50,sizeof(char));
	switch (status){
		case 200: 	strcpy(buff,"200 OK\r\n"); 							break;
		case 404:	strcpy(buff,"404 Incorrect Path\r\n");				break;
		case 501:	strcpy(buff,"501 Method Not Implemented\r\n");		break;
		case 503:	strcpy(buff,"503 Service Unavailable\r\n");			break;
		default: 	strcpy(buff,"Unknown Error\r\n");					break;
	}
	return buff;
}    

/* thread function - dqueue, read request from the client, parse it and replay. 
 * assume http request valid 
 * */
void * connection_manager (void * dummy) {
	int connfd, filefd, nbytes, offset;
	char method[20], path[MAXBUFF], status_line[MAXBUFF], file_buffer[MAXBUFF];
	struct stat st;
	DIR * dir;
	struct dirent * item;
	while( 1 ){
		memset(method,0,20);
		memset(path,0,100);
		memset(file_buffer,0,MAXBUFF);
		memset(status_line,0,MAXBUFF);
		connfd = dequeue(q);
		if(connfd == -1){ /* killing the thread */
			return NULL; 
		}
		recv(connfd, status_line, MAXBUFF , 0); 
		// the TA said in the forum we can assume path doesn't contain spaces
		assert(sscanf(status_line, "%[^ ] %[^ ]", method, path) == 2 && "Error: sscanf failure");
		/* if it's not get or post - not implemented */
		if( strcasecmp(method, "GET") && strcasecmp(method, "POST") ){
			write_response_to_socket(connfd,501); 
			close(connfd);
			continue;
		}
		/* get or post */
		if(stat(path, &st) == 0){ //file is found
			if((st.st_mode & S_IFDIR) == S_IFDIR){ //directory
				assert ( (dir = opendir (path)) != NULL && "Error : opendir failure");
				write_response_to_socket(connfd,200);
				write_to_socket(connfd, HTML_DIR_PREFIX);
				/* print all the files and directories within directory */
				while ((item = readdir (dir)) != NULL) {
					write_to_socket(connfd, item->d_name);
					write_to_socket(connfd, HTML_NEWLINE);
				}
				write_to_socket(connfd, HTML_DIR_POSTFIX);
				closedir (dir);
			}
			else if((st.st_mode & S_IFREG) == S_IFREG){  //file
				assert((filefd = open(path, O_RDONLY)) != -1 );
				write_response_to_socket(connfd,200);
				lseek(filefd, 0 ,SEEK_SET);
				for(	offset = nbytes = 0;
						( nbytes = read(filefd, file_buffer, MAXBUFF)) > 0 ;
						offset += nbytes){

					write_to_socket_nbytes(connfd, file_buffer, nbytes);
					lseek(filefd, offset, SEEK_SET);		
				}
				close(filefd);
			}
			else{ //neither file nor directory - not implemented
				write_response_to_socket(connfd,501);
			}	
		}
		else{ //not found
			write_response_to_socket(connfd,404);
			write_to_socket(connfd, NOT_FOUND_BODY);
		}
		close(connfd);
	}
	return 0;
}

/* get a socket fd and code, and print the appropriate response to the socket */
void write_response_to_socket(int connfd, int code){
	char message[100] = {0}, * code_string;
	strcat(message, RESPONSE_PREFIX);
	code_string = create_message(code);
	strncat(message, code_string, strlen(code_string));
	free(code_string);
	write_to_socket(connfd, message);
}

int main(int argc, char *argv[]){
	int threadIndex = 0, max_request, portNum = 80;
	int connfd = 0;
	struct sockaddr_in serv_addr;	
	struct sigaction sig;
	sig.sa_handler = sig_handler;
	sigaction(SIGINT, &sig, NULL);
	
	assert( (argc == 3 || argc == 4) && "Error: invalid args\n");
	threadNum = strtol(argv[1], NULL, 10);
	max_request = strtol(argv[2], NULL, 10);
	if(argc == 4)
		portNum = strtol(argv[3], NULL, 10);
	assert(portNum >= 0 && "Error : bad port number\n");
	q = init_queue(max_request);
	
	sockfd = make_socket(portNum);
	assert( ! listen(sockfd, MAX_SIM_CONNECTIONS) && "Error : Listen Failed" );
	
	/* creating the threads */
	assert ( (threadList = (pthread_t *) calloc (threadNum, sizeof(pthread_t *))) != NULL);
	for(threadIndex = 0 ; threadIndex < threadNum ; ++threadIndex)
		assert( pthread_create( &threadList[threadIndex] , NULL ,  connection_manager , NULL ) >= 0 && "thread creating failed") ; 
	
	/* handling new connections - enqueue them */
    while( (connfd = accept(sockfd, NULL, NULL)) >= 0){  // new connection
		if( ! enqueue(q, connfd) ){ /* can't enqueue - queue is full */
			write_response_to_socket(connfd, 503);
			close(connfd); 
		}
     }
	 
	assert( connfd >= 0 && "accept failed");
	return 0;
}

void write_to_socket_nbytes(int connfd, char *sendBuff, int nbytes){
	int totalsent, notwritten, nsent;
	/* keep looping until nothing left to write */
	for ( totalsent = 0, notwritten = nbytes ; 
		  notwritten > 0 ;
		  totalsent  += nsent, notwritten -= nsent )
	{
	   assert( (nsent = write(connfd, sendBuff + totalsent, notwritten)) >= 0 && "Error: writing failed");
	}
}

/* ~~~~~~~~~~~~~~~ http_server.c end ~~~~~~~~~~~~~~~~~ */
