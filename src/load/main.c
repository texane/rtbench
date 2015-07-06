/* load by creating cpu, network and memory bound threads */


#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netdb.h>


/* sigint catcher */

static volatile unsigned int is_sigint;

static void on_sigint(int x)
{
  is_sigint = 1;
}


/* network bound thread */

static void* net_main(void* args)
{
  static const size_t n = 4096;
  fd_set wset;
  uint8_t* buf;
  int fd;
  int err;
  ssize_t nsent;
  struct addrinfo  ai;
  struct addrinfo* aip = NULL;
  const struct sockaddr* saddr;
  socklen_t slen;

  buf = malloc(n);
  if (buf == NULL) goto on_error_0;
  memset(buf, 0x2a, n);

  memset(&ai, 0, sizeof(ai));
  ai.ai_family = AF_INET;
  ai.ai_socktype = SOCK_DGRAM;

  if (getaddrinfo("172.24.154.217", "4242", &ai, &aip)) goto on_error_1;

  saddr = (const struct sockaddr*)aip->ai_addr;
  slen = (socklen_t)aip->ai_addrlen;
  fd = socket(saddr->sa_family, SOCK_DGRAM, IPPROTO_UDP);
  if (fd == -1) goto on_error_2;
  if (fcntl(fd, F_SETFL, O_NONBLOCK)) goto on_error_3;

  while (is_sigint == 0)
  {
    FD_ZERO(&wset);
    FD_SET(fd, &wset);

    errno = 0;
    err = select(fd + 1, NULL, &wset, NULL, NULL);
    if (err != 1)
    {
      if (is_sigint == 1) break ;
      goto on_error_2;
    }

    nsent = sendto(fd, buf, n, 0, saddr, slen);
    if (nsent <= 0) goto on_error_2;
  }

 on_error_3:
  shutdown(fd, SHUT_RDWR);
  close(fd);
 on_error_2:
  freeaddrinfo(aip);
 on_error_1:
  free(buf);
 on_error_0:
  return NULL;
}


/* cpu bound thread */

static void* cpu_main(void* args)
{
  const double y = 3.1415;
  const double yy = 8.1415;
  volatile double x = y;

  while (is_sigint == 0) x = x * y + yy;

  return NULL;
}


/* mem bound thread */

static void* mem_main(void* args)
{
  static const size_t n = 16 * 1024 * 1024;
  uint8_t* p;

  p = malloc(n);
  if (p == NULL) goto on_error;

  while (is_sigint == 0) memset(p, 0, n);

  free(p);
 on_error:
  return NULL;
}


/* main */

int main(int ac, char** av)
{
  size_t i;
  pthread_t t[3];
  void* (*f[3])(void*) = { net_main, cpu_main, mem_main };
  const size_t n = sizeof(t) / sizeof(t[0]);

  is_sigint = 0;
  signal(SIGINT, on_sigint);

  for (i = 0; i != n; ++i) pthread_create(&t[i], NULL, f[i], NULL);
  for (i = 0; i != n; ++i) pthread_join(t[i], NULL);

  return 0;
}
