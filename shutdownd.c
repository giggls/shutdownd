#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <string.h>
#include <glob.h>
#include <linux/input.h>

#include "cmdline.h"

#define INPUTDEVGLOB "/dev/input/event*"

/* clig command line Parameters*/  
Cmdline *cmd;

struct key_state {
  unsigned short code;
  unsigned int state;
  unsigned int first;
};

void daemonize(char *dir) {
  if (fork()!=0) exit(0);
  setsid();
  if (fork()!=0) exit(0);
  chdir(dir);
  umask(0);
  close(0);
  close(1);
  close(2);
  /*STDIN*/
  open("/dev/null",O_RDONLY);
  /*STDOUT*/
  open("/dev/null",O_WRONLY);
  /*STDERR*/
  open("/dev/null",O_WRONLY);
}

int isinlist(unsigned int value) {
  int i;
  for (i=0;i<cmd->codeC;i++) {
    if ((unsigned) cmd->code[i]==value) return(i);
  }
  return(-1);
}

static int read_bw(int fd, void *buf,size_t typesize, size_t count){
  
  size_t bytes;
  ssize_t r;

  bytes = count*typesize;
  while( bytes>0 ) { 
    r = read(fd, buf, bytes);
    if( r<1 ) return(1);
    bytes -= r;
    buf += r;
  }
  return(0);
}

/* Simple replacement for system() from libc without calling of /bin/sh */
 int exec (const char *command,int wait) {
  int status;
  static int channel;
  static int first=0;
  static char *dummy;
  static char **com_p;

  pid_t pid;
  size_t i;

  if (first==1) {
    free(dummy);
    free(com_p);
    close(channel);
  }
   
  dummy=malloc(strlen(command)+1);
  strcpy(dummy,command);

  com_p=malloc(sizeof(char*));

  com_p[0]=strtok(dummy," ");
  i=1;
  while (com_p[i-1]!=NULL) {
    com_p=realloc(com_p,sizeof(char*)*(i+1));
    com_p[i]=strtok(NULL," ");
    i++;
  }
   
  pid = fork ();
  if (pid == 0)
    {
      /* child process, call command */
      execvp(com_p[0], com_p);
      _exit (EXIT_FAILURE);
   }
  else if (pid < 0)
    
    /* error on fork, this should actually never happen  */
    status = -1;
  else
    /* parent process, wait untig child terminates like system() if desired */

  if (wait==1) {
    waitpid(pid,&status,0);
    status=WEXITSTATUS(status);
  } else {
    return pid;  }

  first=1;
  return status;
}


int main(int argc, char **argv) {

  int ipdev,res,idx,found;
  struct input_event inp;
  struct key_state *kstates;
  unsigned i;
  struct timeval startuptime;
  char name[256] = "Unknown";
  glob_t globbuf;

  cmd = parseCmdline(argc, argv);

  /* look for all available input devices */
  globbuf.gl_offs = 2;
  glob(INPUTDEVGLOB, GLOB_DOOFFS, NULL, &globbuf);

  if (cmd->queryP) {
    printf("Accessable devices:\n");
  }

  found=0;
  /* process any file which matched our glob pattern */
  for (i=globbuf.gl_offs;i<globbuf.gl_pathc+globbuf.gl_offs;i++) {
    if (-1 == (ipdev = open(globbuf.gl_pathv[i], O_RDONLY))) {
      if (cmd->debugP)
	fprintf(stderr,
		"unable to open event device: %s\n",globbuf.gl_pathv[i]);
    } else {
      ioctl(ipdev, EVIOCGNAME(sizeof(name)), name);
      if (cmd->queryP) {
	printf("%s: \"%s\"\n",globbuf.gl_pathv[i],name);
      } else {
	/* compare current device with the desired one */
	if (strcmp(cmd->device,name)==0) {
	  if (cmd->debugP)
	    fprintf(stderr,"found desired device: >>%s<< (%s)\n",
		    name,globbuf.gl_pathv[i]);
	  found=1;
	  break;
	}
      }
      close(ipdev);
    }
  }

  if (cmd->queryP) {
    exit(EXIT_SUCCESS);
  }

  if (!found) {
    fprintf(stderr,"device not found: >>%s<<\n",cmd->device);
    exit(EXIT_FAILURE);
  }

  if ((cmd->codeC != cmd->edgeC) || (cmd->codeC != cmd->commandC)) {
    fprintf(stderr,
	    "#scancodes(%d), edges(%d) and commands(%d) need to be the same\n"
	    ,cmd->codeC,cmd->edgeC,cmd->commandC);
    
    exit(EXIT_FAILURE);
  }

  kstates = (struct key_state *) malloc(cmd->codeC*sizeof(struct key_state));


  for (i=0;i<(unsigned)cmd->codeC;i++) {
    kstates[i].code=cmd->code[i];
    kstates[i].state=0;
    kstates[i].first=1;
  }

  if (!cmd->foregroundP && !cmd->debugP) {
    openlog(Program,LOG_PID,LOG_DAEMON);
    daemonize("/");
  }

  gettimeofday(&startuptime, NULL);
  while(1) {
    read_bw(ipdev,&inp,sizeof(struct input_event),1);
    /* only keyboard events are interesting */
    if ((inp.type==EV_KEY) || (inp.type==EV_SW)) {
      /* is this a key which matches one from our list? */
      if ((idx=isinlist(inp.code))!=-1) {
	/* ignore first event when happening less than a second
	   after the start of the main loop */
	if (kstates[idx].first==1) {
	  kstates[idx].first=0;
	  if ((inp.time.tv_sec-startuptime.tv_sec) <1.0) {
	    if (cmd->debugP)
	      fprintf(stderr,"ignored startup event code %d edge: %d\n",
		      inp.code,inp.value);
	    kstates[idx].state=inp.value;
	    continue;
	  }
	}

	/* detect edges */
	if ((0==kstates[idx].state) && (inp.value==1)) {
	  if (cmd->debugP) fprintf(stderr,"positive edge, code %d\n",inp.code);
	  if (cmd->edge[idx] == 1) {
	    if (cmd->debugP)
	      fprintf(stderr,"command on positive edge: >%s<\n",
		      cmd->command[idx]);
	    syslog(LOG_NOTICE,"running command: %s",cmd->command[idx]);
	    res=exec(cmd->command[idx],1);
	    if (cmd->debugP)
	      fprintf(stderr,"Return value of command: %d\n",res);
	  }
	}
	if ((1==kstates[idx].state) && (inp.value==0)) {
	  if (cmd->debugP) fprintf(stderr,"negative edge, code %d\n",inp.code);
	  if (cmd->edge[idx] == 0) {
	    if (cmd->debugP) 
	      fprintf(stderr,"command on negative edge: >%s<\n",
		     cmd->command[idx]);
	    syslog(LOG_NOTICE,"running command: %s",cmd->command[idx]);
	    res=exec(cmd->command[idx],1);
	    if (cmd->debugP) 
	      fprintf(stderr,"Return value of command: %d\n",res);
	  }
	}
	kstates[idx].state=inp.value;
      }
    }
  }

  exit(EXIT_SUCCESS);
}
