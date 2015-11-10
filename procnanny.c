#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "memwatch.h"

void killprevprocnanny( void );
void runmonitoring( char *, FILE * );
void forkfunc(pid_t procid, int numsecs, int pipefd[2], int returnpipefd[2]);
int readconfigfile(char *cmdarg);
int getpids(char *procname, FILE *LOGFILE);
void handlesighup( int );

// Global variable declarations
pid_t childpids[128];
int childcount; // number of children currently monitoring a process

char procname[128][255]; // for saving read from file
int numsecs[128];

pid_t procid[128]; // pids corresonding to each proces in config file
  
size_t returnmesssize = sizeof(char)*7;

// Signal flags
int hupflag = 1;

int main(int argc, char *argv[])
{
  mwInit();

  // Log file
  char *logloc = getenv("PROCNANNYLOGS");
  FILE *LOGFILE = fopen(logloc, "w");

  signal(SIGHUP, handlesighup);

  // kill any other procnannies
  killprevprocnanny();

  // Set up monitoring
  runmonitoring(argv[1], LOGFILE);

  fflush(LOGFILE);
  fclose(LOGFILE); 
  mwTerm();
  return 0;
}

void runmonitoring(char *cmdarg, FILE *LOGFILE) {
  
  char procnamesforlog[128][255];  // Array for holding each line of the file read in
  int numsecsperprocess[128]; // Will contain amounts of time per pid, not just process name
  pid_t allprocids[128]; // Processes children are monitoring  
  int freeindices[128]; // array of indices of children in childpids that are not monitoring any process
  int freeindex = -1; // End of the freeindices array.  -1 means no free children.
  time_t currtime; // Time for log files
  int killcount = 0; // Total processes killed for final log output
  // Array for pipes
  int pipefds[128][2];
  int returnpipefds[128][2];
  ssize_t main_readreturn;
  char rmessage[returnmesssize];
 
  // Read each line of config file, count is number of lines read (number of process names)
  

  childcount = 0;

  // Counter variables
  int i, j, k;

  while (1) {
    int count;
    if (hupflag == 1) {
      count = readconfigfile(cmdarg);
      hupflag = 0;
    }

    for (k = 0; k < count; k++) { // names
      // Get corrensponding pids
      int pidcount = getpids(procname[k], LOGFILE);

      for (j = 0; j < pidcount; j++) { // pids
	int monitored = 0;
	int m;
	// Check if already monitored
	for (m = 0; m < childcount; m++) {
	  if (allprocids[m] == procid[j]) {
	    monitored = 1;
	  }
	}
      
	if (monitored == 0) { // if not monitored

	  // Initialize monitoring in log file
	  time(&currtime);
	  fprintf(LOGFILE, "[%.*s] Info: Initializing monitoring of process %s (PID %d).\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname[k], procid[j]);
	  fflush(LOGFILE);

	  if (freeindex > -1) {
	    allprocids[freeindices[freeindex]] = procid[j];
	    memcpy(procnamesforlog[freeindices[freeindex]], procname[k], 255);
	    numsecsperprocess[freeindices[freeindex]] = numsecs[k];

	    write(pipefds[freeindices[freeindex]][1], &allprocids[freeindices[freeindex]], sizeof(pid_t)); 
	    write(pipefds[freeindices[freeindex]][1], &numsecsperprocess[freeindices[freeindex]], sizeof(int));

	    freeindex--;
	  } else {
	    
	    if (pipe(pipefds[childcount]) < 0) { // Open pipe for parent to write to child - write to 1, read from 0
	      printf("Pipe error!");
	    }  else {
	      fcntl(*pipefds[childcount], F_SETFL, O_NONBLOCK);// Set pipe to no block on read
	    }   
	    
	    if (pipe(returnpipefds[childcount]) < 0) { // Set pipe for child to write to parent
	      printf("Pipe error!");
	    }  else {
	      fcntl(*returnpipefds[childcount], F_SETFL, O_NONBLOCK); // Set pipe to no block on read
	    }   
	    memcpy(procnamesforlog[childcount], procname[k], 255);  
	    numsecsperprocess[childcount] = numsecs[k];    
	    allprocids[childcount] = procid[j];

	    forkfunc(allprocids[childcount], numsecsperprocess[childcount], 
		     pipefds[childcount], returnpipefds[childcount]);
	    childcount++;
	  }	  
	}
      }
    }

    for (i = 0; i < childcount; i++) {
      main_readreturn = read(returnpipefds[i][0], rmessage, returnmesssize);
      if (main_readreturn == -1) {
	// No message yet

      } else if (main_readreturn > 0) {
	if (strcmp(rmessage, "killed\n") == 0) {
	  killcount++;
	  // Write message to logfile 
	  time(&currtime);
	  fprintf(LOGFILE, "[%.*s] Action: PID %d (%s) killed after exceeding %d seconds.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), allprocids[i], procnamesforlog[i], numsecsperprocess[i]);
	  fflush(LOGFILE);
	}
	// Add child pid to list of available ones
	freeindex++;
	freeindices[freeindex] = i;
	// Remove child pid from list of running ones
	allprocids[i] = 0;
	write(pipefds[i][1], &allprocids[i], sizeof(pid_t)); 

      } else {
	// Pipe closed
      }
    }

    sleep(5);
  
  }
  time(&currtime);
  fprintf(LOGFILE, "[%.*s] Info: Exiting. %d process(es) killed.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), killcount);
  fflush(LOGFILE);

  return;
}

void forkfunc(pid_t procid, int numsecs, int pipefd[2], int returnpipefd[2]) {
  pid_t pid;

  if ((pid = fork()) < 0) {
    printf("fork() error!");
  } 
  else if (pid == 0) {  // Child process
    while (1) {
      while (procid == 0) { // procid = 0 means no new process to monitor
	read(pipefd[0], &procid, sizeof(pid_t));
	//printf("%d\n", procid);
	read(pipefd[0], &numsecs, sizeof(int));
      	//printf("%d\n", numsecs);
      }

      // Wait for amount of time
      sleep(numsecs);
	
      // Kill monitored process
      int killed = kill(procid, SIGKILL);
      // If the process is actually killed, print to log
      if (killed == 0) {
	// Write to pipe then wait for a read
	write(returnpipefd[1], "killed\n", returnmesssize); 
      }
      else {
	write(returnpipefd[1], "nokill\n", returnmesssize); 
      }
      
      procid = 0;
    }
  } 
  else { // parent process
    
  } 
}

int getpids(char *procname, FILE *LOGFILE) {
  char cmdline[269]; // for creating pgrep command (255 plus extra for command)
  int count = 0;
  FILE *pp;

  sprintf(cmdline, "ps -C %s -o pid=", procname);
  pp = popen(cmdline, "r");
  while (fscanf(pp, "%d", &procid[count]) != EOF) {	
    count++;
  }
    
  if (count == 0) {
    time_t currtime;
    time(&currtime);
    fprintf(LOGFILE, "[%.*s] Info: No '%s' process found.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname);
    fflush(LOGFILE);
  }
  return count;
}

int readconfigfile(char *cmdarg) {
  int count = 0;
  FILE *f = fopen(cmdarg, "r");
  /* Reading from conig file */
  while (fscanf(f, "%s %d", procname[count], &numsecs[count]) != EOF) {
    // Find PIDs for the program
    count++;
  }
  return count;
}

// kill any previous instances of procnanny
void killprevprocnanny() {
  char pgrepcmd[20]; // for creating pgrep command (255 plus extra for command)
  FILE *pni;
  pid_t procnanid;
  pid_t mypid = getpid();
  int killval;

  // Find PIDs for the program
  sprintf(pgrepcmd, "pgrep %s", "procnanny");
  pni = popen(pgrepcmd, "r");
  
  while (fscanf(pni, "%d", &procnanid) != EOF) {
    if (procnanid != mypid) {
      killval = kill(procnanid, SIGKILL);
      if (killval != 0) {
	printf("Failed to kill old procnanny process.\n");
      }
    }
  }
  return;
}

void handlesighup(int signum) {
  hupflag = 1;
}

void handlesigint() {

}

