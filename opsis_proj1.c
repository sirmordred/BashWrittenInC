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

#define DEBUGGABLE 1
#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */

struct arrToken {
    char *elements[80];
    int elementsSize;
};

struct cmdNode {
    char cmd[255];
    char inputFilePath[255];
    char outputFilePath[255];
    int pipeRequired;
    int fileIOType;
    struct cmdNode *next;
};
typedef struct cmdNode Cmd;
Cmd *commandLL = NULL;

struct nodeProcess {
    pid_t pidVal; // pid value of that process
    int type; // 0 == FOREGROUND, 1 == BACKGROUND 
    struct nodeProcess *next;
};
typedef struct nodeProcess ProcessElement;
ProcessElement *processLL = NULL;

struct nodeAlias {
    char fakeCmd[255];
    char realCmd[255];
    struct nodeAlias *next;
};
typedef struct nodeAlias AliasElement;
AliasElement *aliasLL = NULL;

char *trimWhiteSpace(char *str)
{
    char *end;

    // Trim leading space
    while(isspace((unsigned char)*str)) {
        str++;
    }

    if(*str == 0)  // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) {
        end--;
    }

    // Write new null terminator character
    end[1] = '\0';

    return str;
}

int isEmpty(char *inpStr) {
    if (strlen(inpStr) == 0) {
        return 1;
    }
    return 0;
}

void makeEmpty(char *inpStr) {
    strcpy(inpStr,"");
}

void listCommands(Cmd **header) {
    Cmd *test;
    test = *header;
    while (test != NULL) {
        printf("cmd: %s inputFilePath: %s outputFilePath: %s pipeReq: %d fileIOType: %d\n",
                test->cmd,test->inputFilePath,test->outputFilePath,test->pipeRequired,test->fileIOType);
        test = test->next;
    }
}

int sizeOfLL(Cmd **header) {
    int ret = 0;
    Cmd *test;
    test = *header;
    while (test != NULL) {
        ret++;
        test = test->next;
    }
    return ret;
}

void insertCommand(Cmd **header, char givenCmd[255], char inpFPath[255], char outFPath[255], int pipeReq, int fileIOType)
{
    Cmd *p, *temp;

    // create node to insert and assign values to its fields
    p = (Cmd *) malloc(sizeof(Cmd));
    strcpy(p->cmd,givenCmd);
    strcpy(p->inputFilePath,inpFPath);
    strcpy(p->outputFilePath,outFPath);
    p->pipeRequired = pipeReq;
    p->fileIOType = fileIOType;
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

void insertProcess(ProcessElement **header, pid_t pidValue, int execType) // 0 == FOREGROUND, 1 == BACKGROUND
{
    ProcessElement *p, *temp;

    // create node to insert and assign values to its fields
    p = (ProcessElement *) malloc(sizeof(ProcessElement));
    p->pidVal = pidValue; // copy pid value
    p->type = execType; // copy execution type(background,foreground)
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
    // check existent alias before, if it has, just update the node
    AliasElement *testIter;
    testIter = *header;
    while (testIter != NULL) {
	if (!strcmp(testIter->fakeCmd,fakeCmd) && !strcmp(testIter->realCmd,realCmd)) { // same realCmd, same fakeCmd so same alias, print info to user that it has already aliased
		printf("Entered aliased command is already exist, you can check out with \"alias -l\" (without quotes)\n");
		return; // and return
	}
        testIter = testIter->next;
    }

    testIter = *header;
    while (testIter != NULL) {
	if (!strcmp(testIter->realCmd,realCmd)) { // if real commands matches (so aliased cmd is already exist)
		strcpy(testIter->fakeCmd,fakeCmd); // just update the node's fake cmd
		return; // and return
	}
        testIter = testIter->next;
    }

    testIter = *header;
    while (testIter != NULL) {
	if (!strcmp(testIter->fakeCmd,fakeCmd)) { // if fake commands matches (so aliased cmd is already exist)
		strcpy(testIter->realCmd,realCmd); // just update the node's real cmd
		return; // and return
	}
        testIter = testIter->next;
    }

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
    char *binPathStr = (char *) malloc(sizeof(char) * 255); // to make it pass-by-value (don't modify given parameter!)
    memset(binPathStr, '\0', sizeof(binPathStr));
    strcpy(binPathStr,binName);

    char *ptr = (char *) malloc(sizeof(char) * 1000);
    memset(ptr,'\0',sizeof(ptr));
    strcpy(ptr, getenv("PATH")); // get path-variables and store them in ptr string

    char *binPath;
    DIR *dir;
    struct dirent *ent;

    while ((binPath = strtok(ptr, ":")) != NULL) { // split PATH into sub-paths by using ':' delimiter
        if ((dir = opendir (binPath)) != NULL) {
            while ((ent = readdir (dir)) != NULL) {
                if (!strncmp(binPathStr,ent->d_name, strlen(ent->d_name))) { // check if our given binName is in the selected sub-path(e.g usr/bin)
                    strcat(binPath,"/"); // concatenate '/'
                    strcat(binPath,binPathStr); // concatenate our given binName (e.g ls,pwd)

		    free(ptr);  // free string which is holding path-variables
		    free(binPathStr); // free temporary string which is holding a copy of binName
		    return binPath;
                }
            }
            closedir(dir);
        }
        ptr = NULL;
    }
    return binPathStr;
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

char *arrToStr(char *args[], int size, int beginIndex, int isLastIndex) {
    int lastIndex = size;
    if (isLastIndex != -1) { // if last index is supplied to function 
	lastIndex = isLastIndex;
    }
    if (beginIndex < lastIndex) {
        char tempStr[255];
        memset(tempStr,'\0', sizeof(tempStr));
        for (int i = beginIndex; i < lastIndex; i++) {
            strcat(tempStr,args[i]);
            if (i != (size - 1)) {
                strcat(tempStr," ");
            }
        }
        char *retStr = (char *) malloc(sizeof(char) * strlen(tempStr));
	memset(retStr,'\0',sizeof(retStr));
        strcpy(retStr,tempStr);
        return retStr;
    }
    return NULL;
}

struct arrToken strToArr(char *cmdStr) {
    char cmdString[255]; // for pass-by-value, (don't modify given parameter)
    memset(cmdString, '\0', sizeof(cmdString));
    strcpy(cmdString,cmdStr);

    struct arrToken retArr;
    for (int f = 0; f < 80; f++) {
	retArr.elements[f] = NULL; // NULL-ify all the elements of array (initialization)
    }
    char separatedArg[255];
    memset(separatedArg,'\0', sizeof(separatedArg));
    int j = 0;
    int argCount = 0;
    for (int i = 0; i < strlen(cmdString); i++) { /* examine every character in the inputBuffer */
        switch (cmdString[i]) {
            case ' ':
            case '\t':               /* argument separators */
		retArr.elements[argCount] = (char *) malloc(sizeof(char) * 255); // alloc 255*char space for storing string
                strcpy(retArr.elements[argCount++],separatedArg);
                memset(separatedArg,'\0', sizeof(separatedArg)); // clear
                j = 0;
                break;
            default:
                separatedArg[j++] = cmdString[i];
                if (i == (strlen(cmdString) - 1)) {
                    retArr.elements[argCount] = (char *) malloc(sizeof(char) * 255); // alloc 255*char space for storing string
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

void trimQuotes(char *inpStr) {
	int firstQuoteIndex = -1;
	int secQuoteIndex = -1;
	for (int i = 0; i < strlen(inpStr); i++) {
		if (inpStr[i] == '"') {
			firstQuoteIndex = i;
			break;
		}
	}
	for (int h = strlen(inpStr) - 1; h >= 0; h--) {
		if (inpStr[h] == '"') {
			secQuoteIndex = h;
			break;
		}
	}
	if (firstQuoteIndex != -1 && secQuoteIndex != -1) {
		for (int j = firstQuoteIndex; j <= secQuoteIndex; j++) {
			if (j == secQuoteIndex) { // "abc" do not swap 'c' with '"' (so secQuoteIndex - 1)
				inpStr[j-1] = '\0';
			} else {
				inpStr[j] = inpStr[j+1];
			}
		}
	}
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

#define CREATE_FLAG_OW (O_RDWR | O_CREAT | O_TRUNC) // overwrite
#define CREATE_FLAG_AD (O_RDWR | O_CREAT | O_APPEND) // append
#define CREATE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) 
/****
 0 for READING MODE
 1 for WRITE-OVERWRITE MODE
 2 for WRITE-APPEND MODE
 
 XXX DO NOT FORGET TO CLOSE RETURNED FILE DESCRIPTOR FROM THIS FUNCTION AFTER DUPLICATING IT WITH dup2()
  ****/
int openFile(char *filePath, int openingType) {
	int fd = -1; // same as original open() func, it returns -1 as error by default
	if (openingType == 0) {
		// open file in "read" mode
		fd = open(filePath, O_RDWR, CREATE_MODE);
	} else if (openingType == 1) {
		// open file in "write-overwrite" mode
		fd = open(filePath, CREATE_FLAG_OW, CREATE_MODE);
	} else {
		// open file in "write-append" mode
		fd = open(filePath, CREATE_FLAG_AD, CREATE_MODE);
	}
	return fd;
}

void list_aliased_cmds() {
    AliasElement *test;
    test = aliasLL;
    while (test != NULL) {
        printf("Aliased cmd: %s Equivalent cmd: %s\n",test->fakeCmd, test->realCmd);
        test = test->next;
    }
}

void executeCmd(struct arrToken arrtok) {
	pid_t childpid;
	char binaryName[255];
	memset(binaryName,'\0',sizeof(binaryName));
	strcpy(binaryName, arrtok.elements[0]); // get binary name which will be executed and store it on binaryName
	
	// find that binary's path and store it on binaryPath
	char binaryPath[255];
	memset(binaryPath,'\0',sizeof(binaryPath));
	strcpy(binaryPath,hasCmd(binaryName));

	if (binaryPath != NULL) {
		// execute it
		execl(binaryPath, binaryName, arrtok.elements[1], arrtok.elements[2], 
						arrtok.elements[3], arrtok.elements[4],
						arrtok.elements[5], arrtok.elements[6],
						arrtok.elements[7], arrtok.elements[8],
						arrtok.elements[9], arrtok.elements[10],
						arrtok.elements[11], arrtok.elements[12],
						arrtok.elements[13], arrtok.elements[14],
						arrtok.elements[15], arrtok.elements[16],
						arrtok.elements[17], arrtok.elements[18],
						arrtok.elements[19], arrtok.elements[20],
						arrtok.elements[21], arrtok.elements[22],
						arrtok.elements[23], arrtok.elements[24],
						arrtok.elements[25], arrtok.elements[26],
						arrtok.elements[27], arrtok.elements[28],
						arrtok.elements[29], arrtok.elements[30],
						arrtok.elements[31], arrtok.elements[32],
						arrtok.elements[33], arrtok.elements[34],
						arrtok.elements[35], arrtok.elements[36],
						arrtok.elements[37], arrtok.elements[38],
						arrtok.elements[39], arrtok.elements[40],
						arrtok.elements[41], arrtok.elements[42],
						arrtok.elements[43], arrtok.elements[44],
						arrtok.elements[45], arrtok.elements[46],
						arrtok.elements[47], arrtok.elements[48],
						arrtok.elements[49], arrtok.elements[50],
						arrtok.elements[51], arrtok.elements[52],
						arrtok.elements[53], arrtok.elements[54],
						arrtok.elements[55], arrtok.elements[56],
						arrtok.elements[57], arrtok.elements[58],
						arrtok.elements[59], arrtok.elements[60],
						arrtok.elements[61], arrtok.elements[62],
						arrtok.elements[63], arrtok.elements[64],
						arrtok.elements[65], arrtok.elements[66],
						arrtok.elements[67], arrtok.elements[68],
						arrtok.elements[69], arrtok.elements[70],
						arrtok.elements[71], arrtok.elements[72],
						arrtok.elements[73], arrtok.elements[74],
						arrtok.elements[75], arrtok.elements[76],
						arrtok.elements[77], arrtok.elements[78],
						arrtok.elements[79]);
	} else {
		// TODO given binary not found in PATH environments, error out
		perror("Failed to exec, binary is not found on PATH environment\n");
	}
}
 
/* The setup function below will not return any value, but it will just: read
in the next command line; separate it into distinct arguments (using blanks as
delimiters), and set the args array entries to point to the beginning of what
will become null-terminated, C-style strings. */

void setup(char inputBuffer[], char *args[],int *argsLenght, int *background, int isStrSupplied)
{
    int length = 0; /* # of characters in the command line */
    int i = 0;      /* loop index for accessing inputBuffer array */
    int start = -1;  /* index where beginning of next command parameter is */
    int ct = 0;     /* index of where to place the next parameter into args[] */
        
    if (isStrSupplied == 1) {
	// process supplied string
	length = strlen(inputBuffer);
    } else {
    /* read what the user enters on the command line */
	length = read(STDIN_FILENO,inputBuffer,MAX_LINE);  
    }

    /* 0 is the system predefined file descriptor for stdin (standard input),
       which is the user's screen in this case. inputBuffer by itself is the
       same as &inputBuffer[0], i.e. the starting address of where to store
       the command that is read, and length holds the number of characters
       read in. inputBuffer is not a null terminated C-string. */

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

    for (i = 0; i < length; i++) { /* examine every character in the inputBuffer */
        switch (inputBuffer[i]) {
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
		    inputBuffer[i] = '\0';
                    args[ct] = &inputBuffer[start];
		    ct++;
		}
                args[ct] = NULL; /* no more arguments to this command */
		break;
	    default :             /* some other character */
		if (start == -1)
		    start = i;
                if (inputBuffer[i] == '&'){ // Add check for that the (&) symbol MUST be the last argument (always)
		    *background  = 1;
                    inputBuffer[i-1] = '\0';
		}
	} /* end of switch */
     }    /* end of for */
     args[ct] = NULL; /* just in case the input line was > 80 */

     if (DEBUGGABLE) {
	     for (i = 0; i < ct; i++)
		printf("args %d = %s\n",i,args[i]);
     }
     *argsLenght = ct;


} /* end of setup routine */

void parseCommand(char *args[], int argSize){
    char strWillBeinserted[255] = "";
    for (int i = 0; i < argSize; i++) {
        if (!strcmp(args[i],"<")) {
            insertCommand(&commandLL,strWillBeinserted,args[i+1],"",0,1);
            makeEmpty(strWillBeinserted);
            i++; // we dont need  i+1 argument anymore so skip it
        } else if (!strcmp(args[i],">")){
            if (isEmpty(strWillBeinserted)) {
                int sizeOfCmdList = sizeOfLL(&commandLL);
                if (sizeOfCmdList > 0) {
                    Cmd *iter = commandLL;
                    for (int h = 0; h < sizeOfCmdList; h++) {
                        if (h == sizeOfCmdList - 1) { // last element
                            strcpy(iter->outputFilePath,args[i+1]);
                            iter->fileIOType = 5;
                            i++; // we dont need i+1 element
                        }
                        iter = iter->next;
                    }
                }
            } else {
                insertCommand(&commandLL,strWillBeinserted,"",args[i+1],0,2);
                makeEmpty(strWillBeinserted);
                i++; // we dont need  i+1 argument anymore so skip it
            }
        } else if (!strcmp(args[i],">>")){
            if (isEmpty(strWillBeinserted)) {
                int sizeOfCmdList = sizeOfLL(&commandLL);
                if (sizeOfCmdList > 0) {
                    Cmd *iter = commandLL;
                    for (int h = 0; h < sizeOfCmdList; h++) {
                        if (h == sizeOfCmdList - 1) { // last element
                            strcpy(iter->outputFilePath,args[i+1]);
                            iter->fileIOType = 6;
                            i++; // we dont need i+1 element
                        }
                        iter = iter->next;
                    }
                }
            } else {
                insertCommand(&commandLL,strWillBeinserted,"",args[i+1],0,3);
                makeEmpty(strWillBeinserted);
                i++; // we dont need  i+1 argument anymore so skip it
            }
        } else if (!strcmp(args[i],"2>")){
            if (isEmpty(strWillBeinserted)) {
                int sizeOfCmdList = sizeOfLL(&commandLL);
                if (sizeOfCmdList > 0) {
                    Cmd *iter = commandLL;
                    for (int h = 0; h < sizeOfCmdList; h++) {
                        if (h == sizeOfCmdList - 1) { // last element
                            strcpy(iter->outputFilePath,args[i+1]);
                            iter->fileIOType = 7;
                            i++; // we dont need i+1 element
                        }
                        iter = iter->next;
                    }
                }
            } else {
                insertCommand(&commandLL,strWillBeinserted,"",args[i+1],0,4);
                makeEmpty(strWillBeinserted);
                i++; // we dont need  i+1 argument anymore so skip it
            }
        } else if (!strcmp(args[i],"|")){
            // since we deal pipe through array and array.element's pipeRequired filed just continue
            // iterate through sortedArray and set 'pipeRequired' field to 1
            if (!isEmpty(strWillBeinserted)) {
                insertCommand(&commandLL,strWillBeinserted,"","",0,0);
                makeEmpty(strWillBeinserted);
            }
            int sizeOfCmdList = sizeOfLL(&commandLL);
            if (sizeOfCmdList > 0) {
                Cmd *iter = commandLL;
                for (int h = 0; h < sizeOfCmdList; h++) {
                    if (h == sizeOfCmdList - 1) { // last element
                        iter->pipeRequired = 1;
                    }
                    iter = iter->next;
                }
            }
        } else {
            strcat(strWillBeinserted,args[i]);
            strcat(strWillBeinserted," ");
        }
        if (i == argSize - 1 && !isEmpty(strWillBeinserted)) { // add remaining commands if left
            insertCommand(&commandLL,strWillBeinserted,"","",0,0);
            makeEmpty(strWillBeinserted);
        }
    }
}

void execute(Cmd **header) {
	Cmd *test;
	test = *header;
	int sizeOfCmdList = sizeOfLL(&commandLL);

	pid_t childpid = 0;
	int pipeCounter = 0;
	int pipeFd[sizeOfCmdList-1][2];

	for (int i = 0; i < sizeOfCmdList; i++) {
		if (i < sizeOfCmdList-1) {
			pipe(pipeFd[pipeCounter]);
		}

		char command[255];
		char fInPath[255];
		char fOutPath[255];
		strcpy(command,test->cmd);
		strcpy(fInPath,test->inputFilePath);
		strcpy(fOutPath,test->outputFilePath);
		int pipeReq = test->pipeRequired;
		int fIOType = test->fileIOType;

		if ((childpid = fork()) == 0) { // if its child break the loop and exit, otherwise if its main process(parent), dont break
			if (i == 0) { // first process first cmd
				if (fIOType == 1) {
					int fileDesc = openFile(fInPath, 0); // read
					dup2(fileDesc, STDIN_FILENO);
					close(fileDesc);
				} else if (fIOType == 2) {
					int fileDesc = openFile(fOutPath, 1); // write-overwrite
					dup2(fileDesc, STDOUT_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 3) {
					int fileDesc = openFile(fOutPath, 2); // write-append
					dup2(fileDesc, STDOUT_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 4) {
					int fileDesc = openFile(fOutPath, 0); // write-err
					dup2(fileDesc, STDERR_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 5) { // in the above read-write modes openFiles() with 1==read-write CREATE_FLAGS
					int fileDesc1 = openFile(fInPath, 1);
					dup2(fileDesc1, STDIN_FILENO);
					close(fileDesc1);
					int fileDesc2 = openFile(fOutPath, 1); // read and write-overwrite
					dup2(fileDesc1, STDOUT_FILENO);
					close(fileDesc2);
				} else 	if (fIOType == 6) {
					int fileDesc1 = openFile(fInPath, 1);
					dup2(fileDesc1, STDIN_FILENO);
					close(fileDesc1);
					int fileDesc2 = openFile(fOutPath, 1); // read and write-append
					dup2(fileDesc2, STDOUT_FILENO);
					close(fileDesc2);
				} else 	if (fIOType == 7) {
					int fileDesc1 = openFile(fInPath, 1);
					dup2(fileDesc1, STDIN_FILENO);
					close(fileDesc1);
					int fileDesc2 = openFile(fOutPath, 1); // read and write-err
					dup2(fileDesc2, STDERR_FILENO);
					close(fileDesc2);
				}
				if (pipeReq == 1) { // if pipe required
					// outputunu pipe'in outputuna kopyala
					dup2(pipeFd[pipeCounter][1], STDOUT_FILENO);
					// close
					close(pipeFd[pipeCounter][0]);
					close(pipeFd[pipeCounter][1]);
				}
			} else if (i == sizeOfCmdList-1) { // last process last cmd
				// inputunu bir önceki pipe'in inputuna kopyala
				dup2(pipeFd[pipeCounter-1][0], STDIN_FILENO);
				// close
				close(pipeFd[pipeCounter-1][0]);
				close(pipeFd[pipeCounter-1][1]);
				if (fIOType == 2) {
					int fileDesc = openFile(fOutPath, 2); // open file desc in 2==writeOverwrite and copy it to stdout and then close file desc
					dup2(fileDesc, STDOUT_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 3) {
					int fileDesc = openFile(fOutPath, 3); // open file desc in 3==writeAppend and copy it to stdout and then close file desc
					dup2(fileDesc, STDOUT_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 4) {
					int fileDesc = openFile(fOutPath, 2); // open file desc in 2==writeOverwrite and copy it to stderr and then close file desc
					dup2(fileDesc, STDERR_FILENO);
					close(fileDesc);
				}
			} else { // middle processes, middle commands
				// hem input hem outputunu kopyala
				dup2(pipeFd[pipeCounter-1][0], STDIN_FILENO);
				dup2(pipeFd[pipeCounter][1], STDOUT_FILENO);
				// close
				close(pipeFd[pipeCounter][0]);
				close(pipeFd[pipeCounter][1]);
				close(pipeFd[pipeCounter-1][0]);
				close(pipeFd[pipeCounter-1][1]);
			}
			executeCmd(strToArr(trimWhiteSpace(command)));
			perror("Failed to execute command in executeCmd(...)\n");
		} else if (childpid == -1) {
			perror("Error while creating child process\n");
		} else { // parent process
			if(i > 0){
				close(pipeFd[pipeCounter-1][0]);
				close(pipeFd[pipeCounter-1][1]);
			}
			pipeCounter++;
			test = test->next; // iterate to next command in commandLL linkedlist
		}
	}
	wait(NULL);
}


int checkBeforeExit() {
	// TODO Check if there is atleast one background process, if so, then don't exit, just warn the user
}

int main(void)
{
	struct sigaction ctrlCaction;
	ctrlCaction.sa_handler = checkBeforeExit;
	ctrlCaction.sa_flags = 0;

	if (sigemptyset(&ctrlCaction.sa_mask) == -1 || sigaction(SIGINT, &ctrlCaction, NULL) == -1) { // initialize ctrl-c(SIGINT) signal catcher
		perror("Failed to initialize signal set");
		exit(1);
	}

	int isShellRunning = 1;
	AliasElement *test1;
	char inputBuffer[MAX_LINE]; //buffer to hold command entered
	int background; // equals 1 if a command is followed by '&'
	char *args[MAX_LINE/2 + 1]; //command line arguments
	while (isShellRunning) {
		int argumentSize = 0;
		background = 0; // clear variable
		for (int t = 0; t < MAX_LINE/2 + 1; t++) {
			args[t] = NULL; // NULL-ify all the elements of array (initialization)
		}
		memset(inputBuffer,'\0',sizeof(inputBuffer)); // clear variable
		printf("myshell: ");
		fflush(stdout);
		//setup() calls exit() when Control-D is entered
		fflush(stdin);
		setup(inputBuffer, args, &argumentSize, &background, 0);
		fflush(stdin);
		// the steps are:
		// (1) fork a child process using fork()
		// (2) the child process will invoke execv()
		//			(3) if background == 0, the parent will wait,
		// otherwise it will invoke the setup() function again.

		int cmdSize = commandSize(args, argumentSize); // parse by '<', '>', '|', '>>', '2>'

		int isAliasDetectRequired = 1;
		if (argumentSize >= 1) {
			if (!strcmp(args[0],"alias")) {
				// command starts with "alias ....blabla"
				//so "alias detect operation" will not be used
				// instead "alias add operation" will be used
				isAliasDetectRequired = 0;
			}
		}

		if (isAliasDetectRequired == 1) {
			// ******* ALIAS DETECTION AND HANDLING START ********* 
			int hasAlias = 0;
			for (int i = 0; i < argumentSize; i++) {
				test1 = aliasLL;
				while (test1 != NULL) {
					if (!strcmp(args[i], test1->fakeCmd)) {
						strcpy(args[i], test1->realCmd); // replace fakeCmd with the realCmd in argument array
						hasAlias = 1;
					}
					test1 = test1->next;
				}
			}
			if (hasAlias == 1) {
				char *newInputBuffer = arrToStr(args, argumentSize, 0, -1); // convert the new argument array to charArray(so string)
				int sizeOfNewInputBuffer = strlen(newInputBuffer);
				char *tempStr = (char *) malloc(sizeof(char) * (sizeOfNewInputBuffer + 1)); // +1 for \n
				strcpy(inputBuffer,newInputBuffer); // and copy that new charArray/string into inputBuffer
				inputBuffer[sizeOfNewInputBuffer++] = '\n'; // put \n to make it like user entered input
				inputBuffer[sizeOfNewInputBuffer] = '\0'; // put termination-char to make it C-string
				setup(inputBuffer, args, &argumentSize, &background, 1); // pass that string to setup() func again
			}
			// ******* ALIAS DETECTION AND HANDLING END *********
		}

		// update cmdSize again (after handling aliased commands) 
		cmdSize = commandSize(args, argumentSize); // parse by '<', '>', '|', '>>', '2>'

		////////////////// START EXECUTION STAGE OF COMMANDS /////////////////
		if (argumentSize >= 1) {
			if (!strcmp(args[0],"alias")) {
				if (args[1] != NULL) {
					if (!strcmp(args[1],"-l")) {
						list_aliased_cmds(); // List aliased cmds
					} else if (argumentSize >= 3) {
						char cmdWillBeAliased[255];
						memset(cmdWillBeAliased, '\0', sizeof(cmdWillBeAliased));
						strcpy(cmdWillBeAliased, arrToStr(args, argumentSize, 1, argumentSize - 1)); // make "quoted real cmd" string from args double-arr and copy it to temp string
						trimQuotes(cmdWillBeAliased); // trim quotes of alias cmd to get real cmd
						if (!strcmp(cmdWillBeAliased,"alias") || !strcmp(args[argumentSize - 1], "alias")) { // don't allow aliasing "alias" command which can create problems
							printf("You can't alias \"alias\" command\n");
						} else {
							insertAlias(&aliasLL, args[argumentSize - 1], cmdWillBeAliased);
						}
					} else {
						printf("Wrong alias usage, alias usage:\n	$myshell: alias \"ls -l\" list\n");
					}
				}
			} else if (!strcmp(args[0],"unalias")) {
				if (args[1] != NULL) {
					removeAlias(&aliasLL, args[1]);
				}
			} else {
				if (cmdSize == 1) { // it means we have only single command(command can be single-arg or multi-arg)
					if (argumentSize == 1) {
						//it means we have single-arg single command (e.g "exit", "clear" etc.)
						if (!strcmp(args[0], "clr")) {
							system("clear");
						} else if (!strcmp(args[0], "exit")) {
							checkBeforeExit();
						} else if (!strcmp(args[0], "fg")) {
							// TODO make background process foreground (one-by-one)
						} else {
							// other commands, bridge it to exec function
							executeCmd(strToArr(userEnteredInput));
						}
					} else {
						//it means we have multi-arg single command (e.g "ls -l", "touch a.txt b.txt" etc.)
						// we dont have piping/redirecting delimiters ('<', '>', '|', '>>', '2>')
						// so we DONT NEED any pipe or redirect, 
						// so just bridge it to exec function 
						executeCmd(strToArr(userEnteredInput));
					}
				} else if (cmdSize == 2) {
					// it means we have exactly=1 delimiter('<', '>', '|', '>>', '2>') 
					// so piping/duping is required
					// TODO we NEED piping or redirecting
					
				} else {
					// it means we have more than>1 delimiter('<', '>', '|', '>>', '2>') 
					// so piping/duping is required
					// TODO we NEED piping or redirecting
				}
			}
		}
	}
	insertCommand(&commandLL,"sort -n","file1.txt","",1,1);
	insertCommand(&commandLL,"wc","","file2.txt",0,2);




	return 0;
}
