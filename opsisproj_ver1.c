#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
#include <time.h>
#include <fcntl.h>

#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */

struct arrToken {
    char elements[255][80];
    int elementsSize;
};

struct token {
    char firstTok[255];
    char secTok[255];
};

struct nodeAlias {
    char fakeCmd[255];
    char realCmd[255];
    struct nodeAlias *next;
};
typedef struct nodeAlias AliasElement;
AliasElement *aliasLL = NULL;

struct nodeCmd {
    char cmd[255];
    int isValid;
    struct nodeCmd *next;
};
typedef struct nodeCmd Command;
Command *cmdLL = NULL;

void insertCmd(Command **header, char givenCmd[255], int valid)
{
    Command *p, *temp;

    // create node to insert and assign values to its fields
    p = (Command *) malloc(sizeof(Command));
    strcpy(p->cmd,givenCmd);
    p->isValid=valid;
    p->next=NULL;

    // if LL empty
    if (*header == NULL) {
        *header=p;
    } else {// if LL is not empty
        // assign temp to header to point same node
        temp = *header;
        // iterate it until the last node
        while (temp->next != NULL) {
            temp = temp->next;
        }
        // add new node into last
        temp->next = p;
    }
}

void insertAlias(AliasElement **header, char fakeCmd[255], char realCmd[255])
{
    AliasElement *p, *temp;

    // create node to insert and assign values to its fields
    p = (AliasElement *) malloc(sizeof(AliasElement));
    strcpy(p->fakeCmd,fakeCmd);
    strcpy(p->realCmd,realCmd);
    p->next=NULL;

    // if LL empty
    if (*header == NULL) {
        *header=p;
    } else {// if LL is not empty
        // assign temp to header to point same node
        temp = *header;
        // iterate it until the last node
        while (temp->next != NULL) {
            temp = temp->next;
        }
        // add new node into last
        temp->next = p;
    }
}

void removeAlias(AliasElement **head_ref, char fakeCmd[255])
{
    // Store head node
    AliasElement* temp = *head_ref;
    AliasElement *prev = NULL;

    // If head node itself holds the key to be deleted
    if (temp != NULL && !strcmp(fakeCmd,temp->fakeCmd)) {
        *head_ref = temp->next;   // Changed head
        free(temp);               // free old head
        return;
    }

    // Search for the key to be deleted, keep track of the
    // previous node as we need to change 'prev->next'
    while (temp != NULL && strcmp(fakeCmd, temp->fakeCmd) != 0) {
        prev = temp;
        temp = temp->next;
    }

    // If key was not present in linked list
    if (temp == NULL || prev == NULL)
        return;

    // Unlink the node from linked list
    prev->next = temp->next;
    free(temp);  // Free memory

}

char *hasAlias(char cmd[255]) {
    AliasElement *test2 = aliasLL;
    while (test2 != NULL) {
        if (!strncmp(cmd,test2->fakeCmd,strlen(cmd))) {
            return test2->realCmd;
        }
        test2 = test2->next;
    }
    return NULL;
}

// it parses PATH and gets binary paths and then search given binName in their folder, if it finds return binPath(e.g usr/bin)
char *hasCmd(char binName[255]) {
    char *ptr = getenv("PATH");
    char *binPath;
    DIR *dir;
    struct dirent *ent;
    while ((binPath = strtok(ptr, ":")) != NULL) { // split PATH into sub-paths by using ':' delimiter
        if ((dir = opendir (binPath)) != NULL) {
            while ((ent = readdir (dir)) != NULL) {
                if (!strncmp(binName,ent->d_name,strlen(binName))) { // check if our given binName is in the selected sub-path(e.g usr/bin)
                    strcat(binPath,"/"); // concatenate '/'
                    strcat(binPath,binName); // concatenate our given binName (e.g ls,pwd)
                    return binPath; //result will be something like usr/bin/ls
                }
            }
            closedir (dir);
        }
        ptr = NULL;
    }
    return NULL;
}

int contains(char strInp[255], char delimiter[255]) {
    char tempStr[255];
    memset(tempStr,'\0', sizeof(tempStr));
    strcpy(tempStr,strInp);
    if (strstr(tempStr, delimiter) != NULL) {
        return 1;
    }
    return 0;
}

// it uses strtok internally but doesnt touch/modify given char array parameter (so this function behaves like pass-by-value like java)
struct token customStrTok(char str[255], char delim[255]) {
    struct token mytoken;
    char tempStr[255];
    memset(tempStr,'\0', sizeof(tempStr));
    strcpy(tempStr,str); // abc|xyz

    char *tempStr2 = (char *) malloc(sizeof(char) * strlen(str));
    strcpy(tempStr2,str);
    char *strAfterDelim = strstr(tempStr2,delim);
    if (strAfterDelim != NULL) { // means str contains the given delimiter
        int delimIndex = (int) (strAfterDelim - tempStr2); // use pointer-arithmetic to find delimiter index
        int afterDelimIndex = delimIndex + ((int) strlen(delim));
        char *firstTok = (char *) malloc(sizeof(char) * (delimIndex));
        strncpy(firstTok,&tempStr[0],(size_t) (delimIndex));
        firstTok[delimIndex] = '\0';
        char *secTok = (char *) malloc(sizeof(char) * (strlen(str) - afterDelimIndex));
        strncpy(secTok,&tempStr[afterDelimIndex],(strlen(str) - afterDelimIndex));
        secTok[(strlen(str) - afterDelimIndex)] = '\0';
        strcpy(mytoken.firstTok,firstTok);
        strcpy(mytoken.secTok,secTok);
    }
    return mytoken;
}

char *hasOnlyOneDel(char comm[255]) {
    int i = 0;
    char del[255];
    memset(del,'\0', sizeof(del));
    char tempStr[255];
    memset(tempStr,'\0', sizeof(tempStr));
    strcpy(tempStr,comm);
    if (contains(tempStr,">")) {
        i++;
        strcpy(del,">");
    }
    if (contains(tempStr,"<")) {
        i++;
        strcpy(del,"<");
    }
    if (contains(tempStr,"|")) {
        i++;
        strcpy(del,"|");
    }
    if (contains(tempStr,">>")) {
        i++;
        strcpy(del,">>");
    }
    if (contains(tempStr,"2>")) {
        i++;
        strcpy(del,"2>");
    }
    if (i == 1) {
        char *delimiter = (char *) malloc(sizeof(char) * strlen(del));
        strcpy(delimiter,del);
        return delimiter;
    } else {
        return NULL;
    }
}


void parseCmd(char command[255]) {
    char *c = hasOnlyOneDel(command);
    if (c != NULL) {
        struct token tok1 = customStrTok(command,c);
        if (!strcmp(c,"<")) {
            insertCmd(&cmdLL,tok1.secTok,1);
            insertCmd(&cmdLL,tok1.firstTok,1);
        } else if (!strcmp(c,">>")) {
            insertCmd(&cmdLL,tok1.firstTok,1);
            insertCmd(&cmdLL,tok1.secTok,1);
        } else if (!strcmp(c,"2>")) {
            insertCmd(&cmdLL,tok1.firstTok,1);
            insertCmd(&cmdLL,tok1.secTok,1);
        } else if (!strcmp(c,">")) {
            insertCmd(&cmdLL,tok1.firstTok,1);
            insertCmd(&cmdLL,tok1.secTok,1);
        } else if (!strcmp(c,"|")) {
            insertCmd(&cmdLL,tok1.firstTok,1);
            insertCmd(&cmdLL,tok1.secTok,1);
        }
        return;
    }
    if (!contains(command,"|") && !contains(command,"<")
        && !contains(command,">") && !contains(command,"2>")
        && !contains(command,">>")) {
        insertCmd(&cmdLL,command,1);
        return;
    }
    if (contains(command,"|")) {
        struct token tokRes1 = customStrTok(command,"|");
        parseCmd(tokRes1.firstTok);
        parseCmd(tokRes1.secTok);
    } else if (contains(command,"2>")) {
        struct token tokRes2 = customStrTok(command,"2>");
        parseCmd(tokRes2.firstTok);
        parseCmd(tokRes2.secTok);
    } else if (contains(command,">>")) {
        struct token tokRes2 = customStrTok(command,">>");
        parseCmd(tokRes2.firstTok);
        parseCmd(tokRes2.secTok);
    } else if (contains(command,">")) {
        struct token tokRes2 = customStrTok(command,">");
        parseCmd(tokRes2.firstTok);
        parseCmd(tokRes2.secTok);
    } else if (contains(command,"<")) {
        struct token tokRes3 = customStrTok(command,"<");
        parseCmd(tokRes3.firstTok);
        parseCmd(tokRes3.secTok);
    }
}

char *arrToStr(char *args[], int size, int index) {
    if (index < size) {
        char tempStr[255];
        memset(tempStr,'\0', sizeof(tempStr));
        for (int i = index; i < size; i++) {
            strcat(tempStr,args[i]);
            if (i != (size - 1)) {
                strcat(tempStr," ");
            }
        }
        char *retStr = (char *) malloc(sizeof(char) * strlen(tempStr));
        strcpy(retStr,tempStr);
        return retStr;
    }
    return NULL;
}

struct arrToken strToArr(char *cmdStr) {
    struct arrToken retArr;
    char separatedArg[255];
    memset(separatedArg,'\0', sizeof(separatedArg));
    int j = 0;
    int argCount = 0;
    for (int i = 0; i < strlen(cmdStr); i++) { /* examine every character in the inputBuffer */
        switch (cmdStr[i]) {
            case ' ':
            case '\t':               /* argument separators */
                strcpy(retArr.elements[argCount++],separatedArg);
                memset(separatedArg,'\0', sizeof(separatedArg)); // clear
                j = 0;
                break;
            default:
                separatedArg[j++] = cmdStr[i];
                if (i == (strlen(cmdStr) - 1)) {
                    strcpy(retArr.elements[argCount++],separatedArg);
                    memset(separatedArg,'\0', sizeof(separatedArg)); // clear
                    j = 0;
                }
                break;
        }
    }
    retArr.elementsSize = argCount;
    return retArr;
}

int argSize(char *args[]) {
	int count = 0;
	while(args[count++] != NULL){
	}
	return count - 1;
}

int commandSize(char *args[], int argsSize) {
	int count = 1;
	for (int i = 0; i < argsSize; i++) {
		if (!strcmp(args[i],"|") || !strcmp(args[i],"<") || !strcmp(args[i],">")
			|| !strcmp(args[i],">>") || !strcmp(args[i],"2>")) {
			count++;
		}
	}
	return count;
}
 
/* The setup function below will not return any value, but it will just: read
in the next command line; separate it into distinct arguments (using blanks as
delimiters), and set the args array entries to point to the beginning of what
will become null-terminated, C-style strings. */

void setup(char inputBuffer[], char *args[],int *background, char copyOfInput[])
{
    int length, /* # of characters in the command line */
        i,      /* loop index for accessing inputBuffer array */
        start,  /* index where beginning of next command parameter is */
        ct;     /* index of where to place the next parameter into args[] */
    
    ct = 0;
        
    /* read what the user enters on the command line */
    length = read(STDIN_FILENO,inputBuffer,MAX_LINE);  

    /* 0 is the system predefined file descriptor for stdin (standard input),
       which is the user's screen in this case. inputBuffer by itself is the
       same as &inputBuffer[0], i.e. the starting address of where to store
       the command that is read, and length holds the number of characters
       read in. inputBuffer is not a null terminated C-string. */

    start = -1;
    if (length == 0)
        exit(0);            /* ^d was entered, end of user command stream */

/* the signal interrupted the read system call */
/* if the process is in the read() system call, read returns -1
  However, if this occurs, errno is set to EINTR. We can check this  value
  and disregard the -1 value */
    if ( (length < 0) && (errno != EINTR) ) {
        perror("error reading the command");
	exit(-1);           /* terminate with error code of -1 */
    }

    strcpy(copyOfInput,inputBuffer);

    for (i=0;i<length;i++){ /* examine every character in the inputBuffer */

        switch (inputBuffer[i]){
	    case ' ':
	    case '\t' :               /* argument separators */
		if(start != -1){
                    args[ct] = &inputBuffer[start];    /* set up pointer */
		    ct++;
		}
                inputBuffer[i] = '\0'; /* add a null char; make a C string */
		start = -1;
		break;

            case '\n':                 /* should be the final char examined */
		if (start != -1){
                    args[ct] = &inputBuffer[start];     
		    ct++;
		}
                inputBuffer[i] = '\0';
                args[ct] = NULL; /* no more arguments to this command */
		break;

	    default :             /* some other character */
		if (start == -1)
		    start = i;
                if (inputBuffer[i] == '&'){ // TODO change setup() function to add check for that the (&) symbol MUST be the last argument (always)
		    *background  = 1;
                    inputBuffer[i-1] = '\0';
		}
	} /* end of switch */
     }    /* end of for */
     args[ct] = NULL; /* just in case the input line was > 80 */

	for (i = 0; i <= ct; i++)
		printf("args %d = %s\n",i,args[i]);
} /* end of setup routine */
 
int main(void)
{
            char inputBuffer[MAX_LINE]; /*buffer to hold command entered */
            char userEnteredInput[255];
	    memset(userEnteredInput, '\0', sizeof(userEnteredInput));
            int background; /* equals 1 if a command is followed by '&' */
            char *args[MAX_LINE/2 + 1]; /*command line arguments */
            while (1){
                        background = 0;
                        printf("myshell: ");
			fflush(stdout);
                        /*setup() calls exit() when Control-D is entered */
			fflush(stdin);
                        setup(inputBuffer, args, &background, userEnteredInput);
 			fflush(stdin);
                        /** the steps are:
                        (1) fork a child process using fork()
                        (2) the child process will invoke execv()
						(3) if background == 0, the parent will wait,
                        otherwise it will invoke the setup() function again. */
			// TODO add check if & is the LAST arg, if it is not then print error message
			int argumentSize = argSize(args);
			int cmdSize = commandSize(args, argumentSize);
			if (cmdSize == 1) { // it means we have only single command(command can be single-arg or multi-arg)
				if (argumentSize = 1) {
					//it means we have single-arg single command (e.g "exit", "clear" etc.)
					if (!strcmp(args[0], "clr")) {
						// TODO call System("clr");
					} else if (!strcmp(args[0], "exit")) {
						// TODO call System("exit");
					} else {
						// TODO other commands, bridge it to exec function
					}
				} else {
					//it means we have multi-arg single command (e.g "ls -l", "touch a.txt b.txt" etc.)
					// TODO we dont have piping/redirecting delimiters ('<', '>', '|', '>>', '2>') so we DONT NEED any pipe or redeirect, so jsut bridge it to exec function
				}
			} else if (cmdSize == 1) {
				// it means we have exactly 1 delimiter('<', '>', '|', '>>', '2>') so piping/duping is required
				// TODO we NEED piping or redirecting
			} else {
				// it means we have more than 1 delimiter('<', '>', '|', '>>', '2>') so piping/duping is required
				// TODO we NEED piping or redirecting
			}
            }
}
