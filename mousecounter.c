/* LICENSE=GPL */

/*
** mouse counter (counts rel events)
** written by vordah@gmail.com
*/

/* compile with libusb, use gcc -lusb -lpthread */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <asm/types.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/hid.h>

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;

#define TIMEOUT 3000
#define WAITINBETWEEN 500000

/* From HID specification */
#define SET_REQUEST 0x21
#define SET_REPORT 0x09 
#define OUTPUT_REPORT_TYPE 0x02

/* how many controlling threads */
#define THREADS 2

int rc[THREADS];
pthread_t thread[THREADS];

/* Global list of FDs each object is interested in,
** and FDs that there have been activity on. Each object
** adds itself to the fd_request_* lists on creation and removes
** itself on deletion, and checks the fd_* lists on poll.
*/
static fd_set fd_request_read, fd_request_write, fd_request_except;
static fd_set fd_read, fd_write, fd_except;
int fd_count = 0;

struct inputdevice {
  struct usb_device *dev; /* libusb dev */
  struct usb_dev_handle *mouse;
  int uifd, evfd; /* os-side uinput fd (read/write) and event (readonly) */
  struct uinput_user_dev device;
  unsigned int strength, delay, count;
  int countdown; /* effect countdown counter */
  struct timeval timer_cont; /* continuation timer */
  unsigned char led; /* virtual LED */
  char data[4]; /* previous data received from the mouse */
  int leftshift, leftctrl, leftalt;
  int parenthesis;
  struct input_event last_cursor;
  char *name; /* device name */
};

struct inputdevice inputdevice;


/* register the mouse in uinput system as
** pointing device with
** a number of keys, relative pointers and leds
** then it will automatically appear as /dev/input/mouse*
*/
int inremap_register(struct inputdevice *mouse)
{
  int aux, fd;

  /* open event device file */
  if(mouse->name == NULL)
  {
    perror("no input event device specified");
    return -1;
  }
  mouse->evfd = open(mouse->name, O_RDONLY);
  if(mouse->evfd < 0)
  {
    perror("open event device");
    return -1;
  }

  return 0;
}


/* unregister the mouse from uinput system
*/
int inremap_unregister(struct inputdevice *mouse)
{
  /* event de-initialization */
  close(mouse->evfd);

  return 0;
}


void *input_handler(void *data)
{
  struct inputdevice *mouse = (struct inputdevice *) data;
  struct input_event event;
  int i, j, ret, change, fd, retval;
  char buffer[4];
  unsigned long evdata[EV_MAX];
  int counter[2] = { 0, 0 }; /* counter vars */

#if 0
  printf("open\n");
  fd = open("/dev/input/bt_remote", O_RDONLY);
#endif
  fd = mouse->evfd;

  memset(evdata, 0, sizeof evdata);
  ioctl(fd, EVIOCGBIT(0, EV_MAX), evdata);
  evdata[0] = 1;
  if(ioctl(fd, EVIOCGRAB, evdata) == 0)
    printf("exclusive access granted\n");

  printf("success\n");
  for(retval = 0; retval >= 0;)
  {
    retval = read(fd, &event, sizeof(struct input_event));

#if 0
    printf("Event: time %ld.%06ld, type %d, code %d (0x%03x), value %d\n",
           event.time.tv_sec, event.time.tv_usec, event.type,
           event.code, event.code, event.value);
#endif

    if(event.type == 2 && event.code < sizeof(counter))
    {
      counter[event.code] += event.value;
      printf("x=%+08d y=%+08d\n", counter[0], counter[1]);
    }

#if 0
    if(key_convert(mouse, &event))
    {
#if 1
      printf("converted\n");
#endif
#if 0
      write(mouse->uifd, &event, sizeof(struct input_event));
#endif
    }
#endif

  }
  
  return NULL;
}


void terminate(int signal)
{
//  pthread_cancel(thread[0]);
  pthread_cancel(thread[1]);
}

struct timeval tv;
int sec,msec,oldsec,dif,sec0;
int count = 0;

void timer_handler(int signum)
{
  struct inputdevice *mouse = &inputdevice;
  struct input_event syn, event;

  pthread_mutex_lock(&mutex1);
  
  gettimeofday(&tv,NULL);
  sec = (tv.tv_sec-sec0)*1000 + (tv.tv_usec/1000);
  dif = sec-oldsec;
  oldsec = sec;
#if 0
  printf("count %d delay %d  \n", ++count,dif);
#endif
  syn.type = EV_SYN;
  syn.code = SYN_REPORT;
  syn.value = 0;

  event = mouse->last_cursor;
  event.value = 0;

  write(mouse->uifd, &event, sizeof(struct input_event));
  write(mouse->uifd, &syn, sizeof(struct input_event));

  pthread_mutex_unlock(&mutex1);
}

int main(int argc, char *argv[])
{
  int i, n = THREADS;
  struct inputdevice *mouse = &inputdevice;
  struct sigaction sa;

  memset(mouse, 0, sizeof(*mouse));

  if(argc >= 1 && argc <= 2)
  {
    if(argc > 1)
      mouse->name = strdup(argv[1]);
    else
    {
      fprintf(stderr, "Usage: %s /dev/input/eventX\n", argv[0]);
      return 9;
    }
  }

  if(inremap_register(mouse))
  {
    printf("Cannot open device\n");
    return -1;
  }
  
  printf("Mouse counter\n");
  /* Create 1 independant thread
  ** normal remete events (kays)
  */

  i = 1;
  if( (rc[i] = pthread_create( &thread[i], NULL, &input_handler, (void *) (mouse) )) )
    printf("Thread creation failed: %d\n", rc[i]);

  /* graceful exit handler */
  signal(SIGINT, &terminate);
  signal(SIGTERM, &terminate);

  /* timer handler (to synthesize cursor keys release events) */
  memset(&sa, 0, sizeof (sa));    /* Install timer_handler   */
  sa.sa_handler = &timer_handler;
  sigaction(SIGALRM, &sa, NULL);

  /* Wait till threads are complete before main continues. Unless we  */
  /* wait we run the risk of executing an exit which will terminate   */
  /* the process and all threads before the threads have completed.   */
  
  // pthread_join( thread[0], NULL);
  pthread_join( thread[1], NULL);

  inremap_unregister(mouse);
  printf("exit.\n");
  
  return 0;
}
