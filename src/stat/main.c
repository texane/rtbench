/* The goal is to measure software realtime capabilities. The metric used */
/* is latency between IRQ generation by HDL and servicing by software. */
/* Also, the software looks for missed IRQ, meaning the deadline could */
/* not be reached. */

/* The HDL is configured by software to generate periodic IRQs. When the */
/* HDL generates an IRQ, it keeps the start time which is then used by */
/* software to compute the latency. Also, the HDL keeps the count of IRQs */
/* generated so far to allow the software to detect missed IRQs. */

/* from a software perspective, the HDL exports the following registers */
/* REG_CTL<31>      : 0 for stop, 1 for start */
/* REG_CTL<23:0>    : frequency divider */
/* REG_MAGIC (ronly): magic number */
/* REG_FCLK (ronly) : the internal clock frequency */
/* REG_START (ronly): the IRQ start time, in REG_FCLK units */
/* REG_NOW (ronly)  : the current time, in REG_FCLK units */
/* REG_COUNT (ronly): number of IRQ generated so far */


#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/types.h>
#include "libuirq.h"
#include "libepci.h"


#define CONFIG_DEBUG 1
#if (CONFIG_DEBUG == 1)
#include <stdio.h>
#define ASSUME(__x) \
do { if (!(__x)) printf("[!] %d\n", __LINE__); } while (0)
#define PRINTF(__s, ...) \
do { printf(__s, ## __VA_ARGS__); } while (0)
#define PERROR() \
do { printf("[!] %s,%d\n", __FILE__, __LINE__); } while (0)
#else
#define ASSUME(__x)
#define PRINTF(__s, ...)
#define PERROR()
#endif


/* command line parsing */

typedef struct cmdline
{
  uint32_t irq_fgen;
  uint32_t irq_count;
} cmdline_t;

static uint32_t get_num(const char* s)
{
  int base = 10;
  if ((strlen(s) > 2) && (s[0] == '0') && (s[1] == 'x')) base = 16;
  return (uint32_t)strtoul(s, NULL, base);
}

static int get_cmdline(cmdline_t* cmd, size_t ac, char** av)
{
  /* -freq <freq_hz>: the IRQ generation frequency */
  /* -count <count>: how many IRQ to generate. 0 or none is infinit. */

  size_t i;

  if (ac & 1) goto on_error;

  cmd->irq_fgen = 1000;
  cmd->irq_count = 0;

  for (i = 0; i != ac; i += 2)
  {
    if (strcmp(av[i], "-freq") == 0) cmd->irq_fgen = get_num(av[i + 1]);
    else if (strcmp(av[i], "-count") == 0) cmd->irq_count = get_num(av[i + 1]);
    else goto on_error;
  }

  return 0;
 on_error:
  return -1;
}


/* realtime tasking */

typedef struct rtask
{
  int (*fn)(void*);
  void* args;
  pthread_t thread;
  int err;
} rtask_handle_t;

static void* rtask_entry(void* args)
{
  rtask_handle_t* const rtask = (rtask_handle_t*)args;

  /* warning: using a realtime scheduling policy reduces latency */
  /* but prevent the waiting thread to be scheduled. thus, the */
  /* scheduling parmaters should be adjusted if the occupancy */
  /* becomes higher than a given threshold. */

  /* note: setting the thread scheduling priority in pthread_create */
  /* attributes did not work, so we do this here */

  static const int policy = SCHED_FIFO;
  struct sched_param param;

  rtask->err = -1;

  param.sched_priority = sched_get_priority_max(policy);
  if (pthread_setschedparam(pthread_self(), policy, &param)) goto on_error;

  rtask->err = rtask->fn(rtask->args);

 on_error:
  return NULL;
}

static int rtask_start(rtask_handle_t* rtask, int (*fn)(void*), void* args)
{
  rtask->fn = fn;
  rtask->args = args;
  pthread_create(&rtask->thread, NULL, rtask_entry, rtask);

  return 0;
}

static int rtask_wait(rtask_handle_t* rtask)
{
  pthread_join(rtask->thread, NULL);
  return rtask->err;
}


/* register access */

#define REG_BAR 0x01
#define REG_BASE 0x80
#define REG_CTL 0x00
#define REG_TOGL 0x08
#define REG_MAGIC 0x0c
#define REG_FCLK 0x10
#define REG_START 0x14
#define REG_NOW 0x18
#define REG_COUNT 0x1c

static void reg_write(epcihandle_t epci, size_t off, uint32_t x)
{
  epci_wr32_reg(epci, REG_BASE + off, x);
}

static void reg_read(epcihandle_t epci, size_t off, uint32_t* x)
{
  epci_rd32_reg(epci, REG_BASE + off, x);
}

__attribute__((unused))
static void reg_read_togl(epcihandle_t epci, uint32_t* x)
{
  reg_read(epci, REG_TOGL, x);
}

static void reg_read_magic(epcihandle_t epci, uint32_t* x)
{
  reg_read(epci, REG_MAGIC, x);
}

static void reg_write_ctl(epcihandle_t epci, uint32_t x)
{
  reg_write(epci, REG_CTL, x);
}

static void reg_read_fclk(epcihandle_t epci, uint32_t* x)
{
  reg_read(epci, REG_FCLK, x);
}

static void reg_read_start(epcihandle_t epci, uint32_t* x)
{
  reg_read(epci, REG_START, x);
}

static void reg_read_now(epcihandle_t epci, uint32_t* x)
{
  reg_read(epci, REG_NOW, x);
}

__attribute__((unused))
static void reg_read_count(epcihandle_t epci, uint32_t* x)
{
  reg_read(epci, REG_COUNT, x);
}


/* application specific realtime logic */

typedef struct rtask_arg
{
  /* command line */
  cmdline_t* cmd;

  /* latency histogram */
#define LAT_MAX_US 1000000
#define LAT_RES_US 1
#define LAT_MAX_COUNT (LAT_MAX_US / LAT_RES_US)
  uint32_t* lat_hist;

  /* number of handled IRQs */
  size_t irq_count;

  /* number of missed irqs */
  size_t irq_missed;

} rtask_arg_t;

/* sigint catcher */

static volatile unsigned int is_sigint;

static void on_sigint(int x)
{
  is_sigint = 1;
}

static int enable_ebone_slave_interrupt(void)
{
  /* ebm0 documentation: ebm0_pcie_a.pdf */

  epcihandle_t bar0_handle;
  uint32_t x;

  bar0_handle = epci_open("10ee:eb01", NULL, 0);
  if (bar0_handle == EPCI_BAD_HANDLE) return -1;

  /* control register 0 */
  /* ebone slave interrupt enable (bit 9) */
  /* global interrupt enable (bit 31) */
  epci_rd32_reg(bar0_handle, 0x0, &x);
  x |= (1 << 31) | (1 << 9);
  epci_wr32_reg(bar0_handle, 0x0, x);

  /* which slave triggers an interrupt can be known */
  /* using status register 1 (offset 0x14) */

  epci_close(bar0_handle);

  return 0;
}

static int rtask_main(void* p)
{
  rtask_arg_t* const arg = (rtask_arg_t*)p;
  cmdline_t* const cmd = arg->cmd;
  uirq_handle_t uirq;
  epcihandle_t epci;
  uint32_t mask;
  uint32_t irq_fclk;
  uint32_t x;
  uint32_t xx;
  uint32_t xxx;
  int err = -1;

  /* initialize uirq */

  if (enable_ebone_slave_interrupt())
  {
    PERROR();
    goto on_error_0;
  }

  if (uirq_init_lib())
  {
    PERROR();
    goto on_error_0;
  }

  if (uirq_open(&uirq))
  {
    PERROR();
    goto on_error_1;
  }

  if (uirq_set_mask(&uirq, 1 << 1, 1))
  {
    PERROR();
    goto on_error_2;
  }

  is_sigint = 0;
  signal(SIGINT, on_sigint);

  epci = epci_open("10ee:eb01", NULL, REG_BAR);
  if (epci == EPCI_BAD_HANDLE)
  {
    PERROR();
    goto on_error_2;
  }

  /* check magic */

  reg_read_magic(epci, &x);
  if (x != 0xbadcafee)
  {
    PERROR();
    goto on_error_3;
  }

#if 0 /* test irq generation */
  x = 0;
  while (1)
  {
    uint32_t togl_count;

    reg_write_ctl(epci, x);
    x ^= 1 << 30;

    reg_read_togl(epci, &togl_count);
    printf("0x%08x\n", togl_count);

    if (uirq_wait(&uirq, 1000, &mask) == 0) printf("mask: 0x%08x\n", mask);

    usleep(1000000);
  }
#endif /* test irq generation */

  /* compute frequency divider and start irq generation */
  /* irq_fdiv * 1 / irq_fclk = 1 / irq_fgen */
  /* irq_fdiv = irq_fclk / irq_fgen */

  reg_read_fclk(epci, &irq_fclk);
  x = irq_fclk / cmd->irq_fgen;
  if (x == 0)
  {
    PERROR();
    goto on_error_3;
  }

  reg_write_ctl(epci, (1 << 31) | x);

  arg->irq_missed = 0;
  for (arg->irq_count = 0; 1; ++arg->irq_count)
  {
    err = uirq_wait(&uirq, 1000, &mask);
    if (err == -1)
    {
      PERROR();
      goto on_error_3;
    }

    err = 0;

    if (mask == 0) goto skip_irq;

    /* compute latency (ie. now - start) */
    /* beware the underflow */

    reg_read_start(epci, &x);
    reg_read_now(epci, &xx);
    if (xx < x) xxx = ((uint32_t)-1) - x + xx;
    else xxx = xx - x;

    /* convert from fclk to microseconds */

    xxx = (uint32_t)(((uint64_t)xxx * (uint64_t)1000000) / (uint64_t)irq_fclk);

    /* update histogram or missed */

    if (xxx < LAT_MAX_COUNT) ++arg->lat_hist[xxx];
    else ++arg->irq_missed;

#if 0
    /* check for missed irq */

    reg_read_count(epci, &x);
    if (arg->irq_count != (x + 1)) break ;
#endif

  skip_irq:
    if (is_sigint) break ;
    if ((cmd->irq_count > 0) && (arg->irq_count == cmd->irq_count)) break ;
  }

  err = 0;

 on_error_3:
  reg_write_ctl(epci, 0);
  epci_close(epci);
 on_error_2:
  uirq_close(&uirq);
 on_error_1:
  /* uirq_fini_lib(); */
 on_error_0:
  return err;
}


/* main */

int main(int ac, char** av)
{
  size_t i;
  cmdline_t cmd;
  rtask_handle_t rtask;
  rtask_arg_t arg;
  int err = -1;

  if (get_cmdline(&cmd, (size_t)ac - 1, av + 1)) goto on_error_0;
  arg.cmd = &cmd;

  /* allocate latency history */

  arg.lat_hist = malloc(LAT_MAX_COUNT * sizeof(uint32_t));
  if (arg.lat_hist == NULL) goto on_error_1;
  for (i = 0; i != LAT_MAX_COUNT; ++i) arg.lat_hist[i] = 0;

  arg.irq_count = 0;

  /* start wait realtime task */

  if (rtask_start(&rtask, rtask_main, (void*)&arg)) goto on_error_1;
  err = rtask_wait(&rtask);
  /* if (err) goto on_error_1; */

  /* report latencies */
  printf("# irq_count : %zu\n", arg.irq_count);
  printf("# irq_missed: %zu\n", arg.irq_missed);

  for (i = 0; i != LAT_MAX_COUNT; ++i)
  {
    if (arg.lat_hist[i] == 0) continue ;
    printf("%zu %u\n", i * LAT_RES_US, arg.lat_hist[i]);
  }
  
 on_error_1:
  free(arg.lat_hist);
 on_error_0:
  return err;
}
