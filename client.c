#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include<signal.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#define FALSE 0
#define TRUE 1

#define DEFAULT_SERDEVICE  "/dev/ttyUSB0"
#define BAUDRATE B115200

#define CHAR_HDLC_END 0x7E

static volatile int STOP=FALSE; 

static int client_sockfd, client_serfd;
static struct termios client_oldtio;

static int init_serial(char *sername);
static void *serial_thread(void *arg);
static void socket_thread(void);

static void when_sigint(int n)
{
    printf("Recv SIGINT\n");
    STOP = TRUE;
    tcsetattr(client_serfd,TCSANOW,&client_oldtio);
    close(client_sockfd);
    close(client_serfd);
    exit(0);
}

int main(int argc,char *argv[])
{
    char *sername;
    int sockfd, serfd;
    int len;
    struct sockaddr_in address;
    int result;
    pthread_t thread;

    if(argc < 2){
        printf("Usage: %s <address> [device]\n", argv[0]);
        printf("Default device: %s\n", DEFAULT_SERDEVICE);
        return 0;
    }
    sername = DEFAULT_SERDEVICE;
    if (argc > 2)
        sername = argv[2];


    if((sockfd = socket(AF_INET,SOCK_STREAM,0)) == -1){
        perror("sock");
        exit(1);
    }

    serfd=init_serial(sername);

    if (serfd < 0)
    {
        perror("init_serial");
        exit(EXIT_FAILURE);
    }

    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(2500);
    inet_pton(AF_INET,argv[1],&address.sin_addr);
    len = sizeof(address);

    if((result = connect(sockfd, (struct sockaddr *)&address, len))==-1)
    {
        perror("connect");
        exit(EXIT_FAILURE);
    }

    client_sockfd = sockfd;
    client_serfd = serfd;

    signal(SIGINT, when_sigint);

    if(pthread_create(&thread, NULL, serial_thread, NULL)!=0)
    {  
        perror("pthread_create");  
        return -1;
    }
    socket_thread();

    exit(0);
}

static int init_serial(char *sername)
{
    int fd;
    struct termios oldtio,newtio;

    fd = open(sername, O_RDWR | O_NOCTTY ); 
    if (fd <0) {perror(sername); return(-1); }

    tcgetattr(fd,&oldtio); /* save current port settings */
    tcgetattr(fd,&client_oldtio); /* save current port settings */

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 4;   /* blocking read until 4 chars received */

    tcflush(fd, TCIFLUSH);
    tcsetattr(fd,TCSANOW,&newtio);

    return fd;


    //tcsetattr(fd,TCSANOW,&oldtio);
}

static void socket_thread(void)
{
    unsigned char recv_buf[4096], recbuf[4100];
    int rec_len;
    int num_read, start, end;
    int i;

    rec_len = 0;
    for(;;)
    {
        num_read = recv(client_sockfd,recv_buf,sizeof(recv_buf),0);
        if(num_read == -1) {
            perror("recv");
        } else if (num_read== 0) {
            printf("Server Close\n");
            when_sigint(0);
        }
        else if (num_read > 0)
        {
            printf("Socket Debug: read=%d, left=%d, [0]=%u\n", num_read, rec_len, recv_buf[0]);
            if (num_read+rec_len > 4100)
                rec_len = 0;
            memcpy(recbuf+rec_len, recv_buf, num_read);
            rec_len += num_read;
            start = end = 0;
            for (i=0; i<rec_len; i++)
            {
                if (recbuf[i] == CHAR_HDLC_END)
                {
                    if (client_serfd != 0)
                        write(client_serfd, recbuf+start, end-start+1);
                    start = end = i+1;
                    continue;
                }
                end++;
            }
            rec_len = end - start;
            if (rec_len > 0)
                memmove(recbuf, recbuf+start, rec_len);
        }
    }
}

static void *serial_thread(void *arg)
{
    unsigned char read_buf[4096], recbuf[4100];
    int rec_len;
    int num_read, start, end;
    int i;
    unsigned char ch;

    rec_len = 0;
    while (STOP==FALSE) {
#if 1 /* optimize */
        num_read = read(client_serfd, read_buf, sizeof(read_buf));
        if (num_read > 0)
        {
            printf("Serial Debug: read=%d, left=%d\n", num_read, rec_len);
            if (num_read+rec_len > 4100)
                rec_len = 0;
            memcpy(recbuf+rec_len, read_buf, num_read);
            rec_len += num_read;
            start = end = 0;
            for (i=0; i<rec_len; i++)
            {
                if (recbuf[i] == CHAR_HDLC_END)
                {
                    if (client_sockfd != 0)
                        send(client_sockfd, recbuf+start, end-start+1, 0);
                    start = end = i+1;
                    continue;
                }
                end++;
            }
            rec_len = end - start;
            //printf("Debug: start=%d, end=%d, rec_len=%d\n", start, end, rec_len);
            if (rec_len > 0)
                memmove(recbuf, recbuf+start, rec_len);
        }
#else
        read(client_serfd,&ch,1);
        recbuf[rec_len++] = ch;

        if(ch == CHAR_HDLC_END)
        {
            if (client_sockfd != 0)
                send(client_sockfd,recbuf,rec_len,0);
            rec_len = 0;
        }
#endif
    }
}

