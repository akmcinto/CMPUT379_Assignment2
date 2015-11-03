#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include "memwatch.h"

void killprevprocnanny( void );
void runmonitoring( char *, FILE * );

int main(int argc, char *argv[])
{
  mwInit();

  // Log file
  char *logloc = getenv("PROCNANNYLOGS");
  FILE *LOGFILE = fopen(logloc, "w");

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
  
  // Open input file
  FILE *f = fopen(cmdarg, "r");
  
  // Array for holding each line of the file read in
  char procname[255]; // for saving read from file
  char cmdline[269]; // for creating pgrep command (255 plus extra for command)
  int numsecs;
  pid_t pid;
  FILE *pp;
  pid_t procid;

  pid_t childpids[128];
  int childcount = 0;
  time_t currtime;
  pid_t freechildren[128];
  int freecount = 0;
  int status = 0;
  int killcount = 0;

  // Array for pipes
  int pipefds[128][2];
  
  // Check if there were actually any process with that name
  int entered; 

  while (fscanf(f, "%s %d", procname, &numsecs) != EOF) {
    // Find PIDs for the program
    sprintf(cmdline, "ps -C %s -o pid=", procname);
    pp = popen(cmdline, "r");

    entered = 0;
    while (fscanf(pp, "%d", &procid) != EOF) {
      entered = 1;

      // Initialize monitoring in log file
      time(&currtime);
      fprintf(LOGFILE, "[%.*s] Info: Initializing monitoring of process %s (PID %d).\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname, procid);
      
      fflush(LOGFILE);

      // Open pipe - write to 1, read from 0
      if pipe(pipefds[childcount] < 0) {
	printf("Pipe error!");
      }
      // Set pipe to no block on read
      fcntl(pipefds[childcount], F_SETFL, O_NONBLOCK);

      if ((pid = fork()) < 0) {
	printf("fork() error!");
      } 

      else if (pid == 0) {  // Child process
        while (1) { // Run indefinitely
	  
	  if (procid == 0) { // procid = 0 means no new process to monitor
	    read(fd[0], procid, 4);
	  }
	  // Wait for amount of time
	  sleep(numsecs);
	
	  // Kill monitored process
	  int killed = kill(procid, SIGKILL);
	  // If the process is actually killed, print to log
	  if (killed == 0) {
	    time(&currtime);
	    fprintf(LOGFILE, "[%.*s] Action: PID %d (%s) killed after exceeding %d seconds.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procid, procname, numsecs);
	    fflush(LOGFILE);

	    // Write to pipe then wait for a read
	    write(fd[1], "killed\n", 7); 
	  }
	  else {
	    write(fd[1], "no kill\n", 8); 
	  }
	}	
      } 
      else { // parent process
	childpids[childcount] = pid;
	childcount++;
      }         
    }

    // end of pid while loop
    if (entered == 0) {
      time(&currtime);
      fprintf(LOGFILE, "[%.*s] Info: No '%s' process found.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname);
      fflush(LOGFILE);
    }
  }
  // end of proc name while loop
  while (1) {
    int i;
    for (i = 0; i < childcount; i++) {
      pid_t childstatus = waitpid(childpids[i], &status, WNOHANG);
      if (childstatus == -1) {
	printf("Error with waitpid!");
      } else if (childstatus == 0) {
	// Child still running
      } else if (childstatus == childpids[i]) {
	if (WEXITSTATUS(status) == 7) {
	  killcount++;
	}
	// Add child pid to list of available ones
	freechildren[freecount] = childpids[i];
	freecount++;

	// Remove child pid from list of running ones
	childpids[i] = 0;
	childcount--;
      }
    }
  }
  time(&currtime);
  fprintf(LOGFILE, "[%.*s] Info: Exiting. %d process(es) killed.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), killcount);
  
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

