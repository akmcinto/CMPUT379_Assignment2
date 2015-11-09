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
void forkfunc(pid_t procid, int numsecs, int pipefd[2]);

pid_t childpids[128];
int childcount = 0; // number of children currently monitoring a process

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
  FILE *pp;

  // Array for holding each line of the file read in
  //char procname[255]; // for saving read from file
  //char savedprocnames[128][255]; // Array set after each read of the file, compared with current running procs
  char procname[128][255]; 
  int numsecs;
  //pid_t procid;
  //pid_t savedprocids[128]; // Processes children are monitoring
  pid_t procid[128];
  char cmdline[269]; // for creating pgrep command (255 plus extra for command)
  int freeindices[128]; // array of indices of children in childpids that are not monitoring any process
  int freeindex = -1; // End of the freeindices array.  -1 means no free children.
  
  time_t currtime; // Time for log files
  int killcount = 0; // Total processes killed for final log output

  // Array for pipes
  int pipefds[128][2];
  ssize_t main_readreturn;
  size_t returnmesssize = 7;
  char rmessage[returnmesssize];
  
  // Check if there were actually any process with that name
  int entered; 
  int count = 0;

  /* Reading from conig file */
  while (fscanf(f, "%s %d", procname[count], &numsecs) != EOF) {
    // Find PIDs for the program
    count++;
  }

  /* Checking for and monitoring processes */
  int k;
  for (k = 0; k < count; k++) {
    sprintf(cmdline, "ps -C %s -o pid=", procname[k]);
    pp = popen(cmdline, "r");
    entered = 0;
    while (fscanf(pp, "%d", &procid[childcount]) != EOF) {
      entered = 1;
      
      // Initialize monitoring in log file
      time(&currtime);
      fprintf(LOGFILE, "[%.*s] Info: Initializing monitoring of process %s (PID %d).\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname[k], procid[childcount]);
      
      fflush(LOGFILE);

      // Open pipe - write to 1, read from 0
      if (pipe(pipefds[childcount]) < 0) {
	printf("Pipe error!");
      }  else {
	// Set pipe to no block on read
	fcntl(*pipefds[childcount], F_SETFL, O_NONBLOCK);
      }   

      forkfunc(procid[childcount], numsecs, pipefds[childcount]);
  
    }

    // end of pid while loop
    if (entered == 0) {
      time(&currtime);
      fprintf(LOGFILE, "[%.*s] Info: No '%s' process found.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procname[childcount]);
      fflush(LOGFILE);
    }
  }
  // end of proc name while loop
  int i;
  //while (1) {
  for (i = 0; i < childcount; i++) {
    main_readreturn = read(pipefds[childcount][0], rmessage, returnmesssize);
    if (main_readreturn == -1) {
      // No message yet
    } else if (main_readreturn > 0) {
      if (strcmp(rmessage, "killed\n") == 0) {
	killcount++;

	// Write message to logfile 
	time(&currtime);
	fprintf(LOGFILE, "[%.*s] Action: PID %d (%s) killed after exceeding %d seconds.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), procid[childcount], procname[childcount], numsecs);
	fflush(LOGFILE);
      }
      // Add child pid to list of available ones


      // Remove child pid from list of running ones
      childpids[i] = 0;
      childcount--;
    } else {
      // Pipe closed
    }
  }
  //}
  time(&currtime);
  fprintf(LOGFILE, "[%.*s] Info: Exiting. %d process(es) killed.\n", (int) strlen(ctime(&currtime))-1, ctime(&currtime), killcount);
  fflush(LOGFILE);

  return;
}

void forkfunc(pid_t procid, int numsecs, int pipefd[2]) {
  pid_t pid;

  if ((pid = fork()) < 0) {
    printf("fork() error!");
  } 

  else if (pid == 0) {  // Child process
    /*if (procid[childcount] == 0) { // procid = 0 means no new process to monitor
      read(pipefds[childcount][0], &procid[childcount], 4);
      }*/

    // Wait for amount of time
    sleep(numsecs);
	
    // Kill monitored process
    int killed = kill(procid, SIGKILL);
    // If the process is actually killed, print to log
    if (killed == 0) {
      

      // Write to pipe then wait for a read
      write(pipefd[1], "killed\n", 7); 
    }
    else {
      write(pipefd[1], "nokill\n", 7); 
    }
		
  } 
  else { // parent process
    childpids[childcount] = pid;
    childcount++;
  } 
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

