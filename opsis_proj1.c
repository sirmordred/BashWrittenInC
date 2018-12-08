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

// File_Flags used in openFile() method
#define CREATE_FLAG_OW (O_RDWR | O_CREAT | O_TRUNC) // overwrite
#define CREATE_FLAG_AD (O_RDWR | O_CREAT | O_APPEND) // append
#define CREATE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) 

#define MAX_LINE 80 /* 80 chars per line, per command, should be enough. */
#define DEBUGGABLE 0

int isShellRunning = 1;
pid_t foregroundProcPid = -1; // pid value of currently running foreground process (we can have only one fg process at the same time)
pid_t backgroundProcPidArr[80]; // pid value array of background processes (we can have multiple bg processes at the same time)
int bgProcCounter = 0;

struct arrToken {
    char *elements[80];
    int elementsSize;
};

struct nodeCmd {
    char cmd[255];
    char inputFilePath[255];
    char outputFilePath[255];
    int pipeRequired;
    /* 0 FOR do not pipe
       1 FOR pipe to next child
    */
    int fileIOType;
    /* fileIOType:
	0 do nothing
	1 FOR read file
	2 FOR write-overwrite file
	3 FOR write-append file
	4 FOR write-stderr file
	5 FOR read and write-overwrite
	6 FOR read and write-append
	7 FOR read and write-stderr
     */
    struct nodeCmd *next;
};
typedef struct nodeCmd Cmd;

struct nodeAlias {
    char fakeCmd[255];
    char realCmd[255];
    struct nodeAlias *next;
};
typedef struct nodeAlias Alias;

Cmd *commandLL = NULL;
Alias *aliasLL = NULL;

char *trimTrailingAndEndingChar(char *str, char trimChar) {
    char *end;

    // Trim leading space
    while((unsigned char) *str == trimChar) {
        str++;
    }

    if(*str == 0)  // All spaces?
        return str;

    // Trim trailing space
    end = str + strlen(str) - 1;
    while(end > str && (unsigned char) *end == trimChar) {
        end--;
    }

    // Write new null terminator character
    end[1] = '\0';

    return str;
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
    Cmd *tempPtr;
    tempPtr = *header;
    while (tempPtr != NULL) {
        printf("cmd: %s inputFilePath: %s outputFilePath: %s pipeReq: %d fileIOType: %d\n",
                tempPtr->cmd,tempPtr->inputFilePath,tempPtr->outputFilePath,tempPtr->pipeRequired,tempPtr->fileIOType);
        tempPtr = tempPtr->next;
    }
}

int checkFileInputOfCmds(Cmd **header) {
    Cmd *tempPtr;
    tempPtr = *header;
    while (tempPtr != NULL) {
	// check if inputfilePath is not empty(strlen...) AND file is not exist
	if (strlen(tempPtr->inputFilePath) > 0 && access(tempPtr->inputFilePath, F_OK) == -1) { 
		// it means inputFilePath is not exist so return 0 (false)
		printf("ERROR: File %s is not exist\n",tempPtr->inputFilePath);
		return 0;
	}
        tempPtr = tempPtr->next;
    }
    return 1;
}

int sizeOfLL(Cmd **header) {
    int ret = 0;
    Cmd *tempPtr;
    tempPtr = *header;
    while (tempPtr != NULL) {
        ret++;
        tempPtr = tempPtr->next;
    }
    return ret;
}

/* Function to delete the entire linked list */
void deleteLL(Cmd** head_ref)
{
    /* deref head_ref to get the real head */
    Cmd* current = *head_ref;
    Cmd* next;

    while (current != NULL) {
        next = current->next;
        free(current);
        current = next;
    }

    /* deref head_ref to affect the real head back
       in the caller. */
    *head_ref = NULL;
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

void insertAlias(Alias **header, char fakeCmd[255], char realCmd[255])
{
    // check existent alias before, if it has, just update the node
    Alias *tempAliasPtr;
    tempAliasPtr = *header;
    while (tempAliasPtr != NULL) {
	if (!strcmp(tempAliasPtr->fakeCmd,fakeCmd) && !strcmp(tempAliasPtr->realCmd,realCmd)) { // same realCmd, same fakeCmd so same alias, print info to user that it has already aliased
		printf("Entered aliased command is already exist, you can check out with \"alias -l\" (without quotes)\n");
		return; // and return
	}
        tempAliasPtr = tempAliasPtr->next;
    }

    tempAliasPtr = *header;
    while (tempAliasPtr != NULL) {
	if (!strcmp(tempAliasPtr->realCmd,realCmd)) { // if real commands matches (so aliased cmd is already exist)
		strcpy(tempAliasPtr->fakeCmd,fakeCmd); // just update the node's fake cmd
		return; // and return
	}
        tempAliasPtr = tempAliasPtr->next;
    }

    tempAliasPtr = *header;
    while (tempAliasPtr != NULL) {
	if (!strcmp(tempAliasPtr->fakeCmd,fakeCmd)) { // if fake commands matches (so aliased cmd is already exist)
		strcpy(tempAliasPtr->realCmd,realCmd); // just update the node's real cmd
		return; // and return
	}
        tempAliasPtr = tempAliasPtr->next;
    }

    Alias *p, *temp;

    // create node to insert and assign values to its fields
    p = (Alias *) malloc(sizeof(Alias));
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

void removeAlias(Alias **head_ref, char fakeCmd[255])
{
    // Store head node
    Alias* temp = *head_ref;
    Alias *prev = NULL;

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
    Alias *testPtr = aliasLL;
    while (testPtr != NULL) {
        if (!strncmp(cmd,testPtr->fakeCmd,strlen(cmd))) {
            return testPtr->realCmd;
        }
        testPtr = testPtr->next;
    }
    return NULL;
}

void listAliasedCmds() {
    Alias *testPtr;
    testPtr = aliasLL;
    while (testPtr != NULL) {
        printf("Aliased cmd: %s Equivalent cmd: %s\n",testPtr->fakeCmd, testPtr->realCmd);
        testPtr = testPtr->next;
    }
}

// it parses PATH and gets binary paths and then search given binName in their folder, if it finds return binPath(e.g usr/bin)
char *getBinaryPath(char binName[255]) {
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

/****
 0 for READING MODE
 1 for WRITE-OVERWRITE MODE
 2 for WRITE-APPEND MODE
 
 DO NOT FORGET TO CLOSE RETURNED FILE DESCRIPTOR AFTER DUPLICATING IT WITH dup2()
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

void parseCommand(char *args[], int argSize) { // parseCommand() will fill the &commandLL linkedlist
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

void customExecl(struct arrToken arrtok) {
	pid_t childpid;
	char binaryName[255];
	memset(binaryName,'\0',sizeof(binaryName));
	strcpy(binaryName, arrtok.elements[0]); // get binary name which will be executed and store it on binaryName
	
	// find that binary's path and store it on binaryPath
	char binaryPath[255];
	memset(binaryPath,'\0',sizeof(binaryPath));
	strcpy(binaryPath,getBinaryPath(binaryName));

	if (strlen(binaryPath) > 0) { // check if getBinaryPath() method returns non-empty binary path string
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
						arrtok.elements[79], NULL);
	} else {
		perror("Failed to exec, binary is not found on PATH environment\n");
	}
}

void execute(Cmd **header, int isProcessBackground) {  // execute() will execute commands in the &commandLL linkedlist
	Cmd *cmdIterPtr;
	cmdIterPtr = *header;
	int sizeOfCmdList = sizeOfLL(&commandLL);

	pid_t childpid = 0;
	int pipeCounter = 0;
	int pipeFd[sizeOfCmdList-1][2];

	for (int i = 0; i < sizeOfCmdList; i++) {
		if (i < sizeOfCmdList-1) {
			pipe(pipeFd[pipeCounter]);
		}

		// copy data of the commandLL linkedlist's node into the function variables
		char command[255];
		char fInPath[255];
		char fOutPath[255];
		strcpy(command,cmdIterPtr->cmd);
		strcpy(fInPath,cmdIterPtr->inputFilePath);
		strcpy(fOutPath,cmdIterPtr->outputFilePath);
		int pipeReq = cmdIterPtr->pipeRequired;
		int fIOType = cmdIterPtr->fileIOType;

		if ((childpid = fork()) == 0) { // if its child break the loop and exit, otherwise if its main process(parent), dont break
			if (i == 0) { // first process, first cmd
				if (fIOType == 1) { // read
					int fileDesc = openFile(fInPath, 0);
					dup2(fileDesc, STDIN_FILENO);
					close(fileDesc);
				} else if (fIOType == 2) { // write-overwrite
					int fileDesc = openFile(fOutPath, 1);
					dup2(fileDesc, STDOUT_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 3) { // write-append
					int fileDesc = openFile(fOutPath, 2);
					dup2(fileDesc, STDOUT_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 4) { // write-err
					int fileDesc = openFile(fOutPath, 0);
					dup2(fileDesc, STDERR_FILENO);
					close(fileDesc);
				} else 	if (fIOType == 5) { // read and write-overwrite
					int fileDesc1 = openFile(fInPath, 0);
					dup2(fileDesc1, STDIN_FILENO);
					close(fileDesc1);
					int fileDesc2 = openFile(fOutPath, 1); 
					dup2(fileDesc2, STDOUT_FILENO);
					close(fileDesc2);
				} else 	if (fIOType == 6) {  // read and write-append
					int fileDesc1 = openFile(fInPath, 0);
					dup2(fileDesc1, STDIN_FILENO);
					close(fileDesc1);
					int fileDesc2 = openFile(fOutPath, 2);
					dup2(fileDesc2, STDOUT_FILENO);
					close(fileDesc2);
				} else 	if (fIOType == 7) {  // read and write-err
					int fileDesc1 = openFile(fInPath, 1);
					dup2(fileDesc1, STDIN_FILENO);
					close(fileDesc1);
					int fileDesc2 = openFile(fOutPath, 1);
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
			} else if (i == sizeOfCmdList-1) { // last process, last cmd
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
			customExecl(strToArr(trimTrailingAndEndingChar(command, ' '))); // trim trailing and ending whitespaces of 'command' string before executing(whitespaces are added on parseCommand())
			perror("Failed to execute command in executeCmd(...)\n");
		} else if (childpid == -1) {
			perror("Error while creating child process\n");
		} else { // parent process
			if (i > 0) { // close unnecessary "(pipeCounter-1)th pipe" also on parent process, because both childs and parent processes have pipeFd pipes array, and since we are closing parent's pipe, it is not affecting child's pipe
				close(pipeFd[pipeCounter-1][0]);
				close(pipeFd[pipeCounter-1][1]);
			}
			// save child process's pid value in parent process and also ONLY ALLOW one commands can have '&' background
			// multiple commands such as 'ls -l | sort | wc' can't have '&' background processing since its meaningless
			if (sizeOfCmdList != 1 || isProcessBackground != 1) {
				// this block is for foreground processes
				foregroundProcPid = childpid; // save pid of current foreground process to 'foregroundProcPid'
				waitpid(childpid, NULL, 0); // wait childs one by one (in simplefan)
				foregroundProcPid = -1;
			} else {
				// once this blocks run, it means we are executing child process in background so save its pid value to pid array
				backgroundProcPidArr[bgProcCounter++] = childpid;
			}
			pipeCounter++;
			cmdIterPtr = cmdIterPtr->next; // iterate to next command in commandLL linkedlist
		}
	}
	
}

void makeBgProcessesFg() {
	int bgProcSize = bgProcCounter;
	for (int i = 0; i < bgProcSize; i++) {
		foregroundProcPid = backgroundProcPidArr[i]; // since we will wait for bg process(it will become fg process), update currentFgPRocPid
		waitpid(backgroundProcPidArr[i], NULL, 0); // wait bg process childs one by one
		bgProcCounter--; // after waiting background proc, its no more exist so delete also from the bg process array
	}
}

int isRedPipDelimiter(char *str) { // it checks if given string is equal to redirect/pipe delimiters ('<', '>', '>>', '2>', '|')
    if (!strcmp(str, "<") || !strcmp(str, ">") ||
            !strcmp(str, ">>") || !strcmp(str, "2>") ||
            !strcmp(str, "|")) {
        return 1;
    }
    return 0;
}

/* The setup function below will not return any value, but it will just: read
   in the next command line; separate it into distinct arguments (using blanks as
   delimiters), and set the args array entries to point to the beginning of what
   will become null-terminated, C-style strings. */

// It returns 1 if given user entered command is correct, otherwise returns 0
int setup(char inputBuffer[], char *args[],int *argsLenght, int *background, int isStrSupplied) {
	int ret = 1; // return value, default is 1 (so given cmd is correct)
	int length = 0; // # of characters in the command line
	int i = 0;      // loop index for accessing inputBuffer array
	int start = -1;  // index where beginning of next command parameter is
	int ct = 0;     // index of where to place the next parameter into args[]

	if (isStrSupplied == 1) {
		// process supplied string
		length = strlen(inputBuffer);
	} else {
		// read what the user enters on the command line
		length = read(STDIN_FILENO,inputBuffer,MAX_LINE);  
	}

	// 0 is the system predefined file descriptor for stdin (standard input),
	// which is the user's screen in this case. inputBuffer by itself is the
	// same as &inputBuffer[0], i.e. the starting address of where to store
	// the command that is read, and length holds the number of characters
	// read in. inputBuffer is not a null terminated C-string.

	if (length == 0) {
		exit(0);            // ^d was entered, end of user command stream
	}

	// the signal interrupted the read system call

	// if the process is in the read() system call, read returns -1
	// However, if this occurs, errno is set to EINTR. We can check this  value
	// and disregard the -1 value */

	if ((length < 0) && (errno != EINTR)) {
		perror("error reading the command");
		exit(-1);           /* terminate with error code of -1 */
	}

	for (i = 0; i < length; i++) { /* examine every character in the inputBuffer */
		switch (inputBuffer[i]) {
		    case ' ':
		    case '\t' :               /* argument separators */
			if(start != -1) {
			    args[ct] = &inputBuffer[start];    /* set up pointer */
			    ct++;
			}
			inputBuffer[i] = '\0'; /* add a null char; make a C string */
			start = -1;
			break;
		    case '\n':                 /* should be the final char examined */
			if (start != -1) {
			    inputBuffer[i] = '\0';
			    args[ct] = &inputBuffer[start];
			    ct++;
			}
			args[ct] = NULL; /* no more arguments to this command */
			break;
		    default :             /* some other character */
			if (start == -1) {
			    start = i;
			}
		} /* end of switch */
	}    /* end of for */
	args[ct] = NULL; /* just in case the input line was > 80 */

	// ERROR CHECK, check if redirect/pipe delimiters is not on 1sth arg and last arg (çünkü sağı solu dolu olması gerek)
	if (args[0] != NULL) {
		if (isRedPipDelimiter(args[0]) || isRedPipDelimiter(args[ct - 1])) {
			ret = 0;
		}
	}

	if (ret == 1) { // if cmd is still correct, test next error check
		// 2nd ERROR CHECK, check if 2 redirect/pipe delimiters ('<', '>', '>>', '2>', '|') are not successive(peşpeşe)
		for (i = 0; i < ct; i++) {
			if (i + 1 < ct) {
				if (isRedPipDelimiter(args[i]) && isRedPipDelimiter(args[i + 1])) {
					ret = 0;
				}
		    	}
		}
	}

	// Check background processing of childs
	if (!strcmp(args[ct - 1], "&")) { // if last argument is equal to "&"
		args[--ct] = NULL; // both decrease ct and NULL (so delete) arg which contains "&"
		*background = 1; // set background to 1
	}

	if (DEBUGGABLE) {
	     for (i = 0; i < ct; i++)
		printf("args %d = %s\n",i,args[i]);
	}

	*argsLenght = ct; // save argument size

	return ret;
}

void sendSigStop() {
	if (foregroundProcPid != -1) { // check if we(parent) are currently waiting for some process(so check if there is fg process)
		kill(foregroundProcPid, SIGSTOP);
		foregroundProcPid = -1; // since we are not waiting for that fg process anymore, set 'foregroundProcPid' to -1
	}
}

void checkBeforeExit() {
	// Check if there is atleast one background process, if so, then don't exit, just warn the user
	if (bgProcCounter > 0) {
		printf("You cannot exit, there are still background processes\n");
	} else {
		isShellRunning = 0;
	}
}

int detectAliasAndReplaceArgs(char inpBuffer[], char *argArr[], int argArrSize) { // replace all fakeCmd arguments with real equivalent cmds
	Alias *aliasIterPtr;
	int hasAlias = 0;
	for (int i = 0; i < argArrSize; i++) {
		aliasIterPtr = aliasLL;
		while (aliasIterPtr != NULL) {
			if (!strcmp(argArr[i], aliasIterPtr->fakeCmd)) {
				strcpy(argArr[i], aliasIterPtr->realCmd); // replace fakeCmd with the realCmd in argument array
				hasAlias = 1;
			}
			aliasIterPtr = aliasIterPtr->next;
		}
	}
	if (hasAlias == 1) {
		char *newInputBuffer = arrToStr(argArr, argArrSize, 0, -1); // convert the new argument array to charArray(so string)
		int sizeOfNewInputBuffer = strlen(newInputBuffer);
		strcpy(inpBuffer,newInputBuffer); // and copy that new charArray/string into inputBuffer
		inpBuffer[sizeOfNewInputBuffer++] = '\n'; // put \n to make it like user entered input
		inpBuffer[sizeOfNewInputBuffer] = '\0'; // put termination-char to make it C-string
	}
	return hasAlias;
}

int main(void) {
	struct sigaction ctrlZaction;
	ctrlZaction.sa_handler = &sendSigStop;
	ctrlZaction.sa_flags = 0;

	char inputBuffer[MAX_LINE]; //buffer to hold command entered
	int background = 0; // equals 1 if a command is followed by '&', default 0 so wait for all child to process cmd correctly
	char *args[MAX_LINE/2 + 1]; //command line arguments

	if (sigemptyset(&ctrlZaction.sa_mask) == -1 || sigaction(SIGTSTP, &ctrlZaction, NULL) == -1) { // initialize ctrl-z(SIGSTOP) signal catcher
		perror("Failed to initialize signal set");
		exit(1);
	}

	while (isShellRunning) {
		int argumentSize = 0;
		background = 0; // clear variable
		for (int t = 0; t < MAX_LINE/2 + 1; t++) {
			args[t] = NULL; // NULL-ify all the elements of array (initialization)
		}
		memset(inputBuffer,'\0',sizeof(inputBuffer)); // clear variable

		printf("myshell: "); // print cursor

		fflush(stdout); // setup() which initializes arguments for us, it will calls exit() when Control-D is entered
		fflush(stdin);
		if (setup(inputBuffer, args, &argumentSize, &background, 0) == 0) { // if return val is 0 (so user entered input is wrong) just continue
			perror("ERROR: User entered command syntax is wrong, please try again");
			continue;
		}
		fflush(stdin);

		// the steps are:
		// (1) fork a child process using fork()
		// (2) the child process will invoke execv()
		//			(3) if background == 0, the parent will wait,
		// otherwise it will invoke the setup() function again.

		if (argumentSize >= 1) {
			if (strcmp(args[0],"alias") && strcmp(args[0],"unalias")) {
				// if command not starts with "alias ....blabla" AND not starts with "unalias ....blabla"
				// so  its just 'normal' command, it can have aliased commands so "alias detect&replace operation" will be used

				///////// ALIAS DETECTION AND HANDLING BEGIN ////////////
				if (detectAliasAndReplaceArgs(inputBuffer, args, argumentSize)) { // if it returns 1, it means it detected alias and replaced, so call setup() again with new 'inputBuffer' to reproduce new args Array
					setup(inputBuffer, args, &argumentSize, &background, 1); // pass that string to setup() func again
				}
				///////// ALIAS DETECTION AND HANDLING END ////////////
			}

			////////////////// START EXECUTION STAGE OF COMMANDS /////////////////
			if (!strcmp(args[0],"alias")) {
				if (args[1] != NULL) {
					if (!strcmp(args[1],"-l")) {
						listAliasedCmds(); // List aliased cmds
					} else if (argumentSize >= 3) {
						char cmdWillBeAliased[255];
						memset(cmdWillBeAliased, '\0', sizeof(cmdWillBeAliased));
						strcpy(cmdWillBeAliased, arrToStr(args, argumentSize, 1, argumentSize - 1)); // make "quoted real cmd" string from args array and copy it to temp string
						strcpy(cmdWillBeAliased,trimTrailingAndEndingChar(cmdWillBeAliased, '"')); // trim quotes of alias cmd to get real cmd
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
					if (argumentSize == 1 && !strcmp(args[0], "clr")) {
						system("clear");
					} else if (argumentSize == 1 && !strcmp(args[0], "exit")) {
						checkBeforeExit();
					} else if (argumentSize == 1 && !strcmp(args[0], "fg")) {
						makeBgProcessesFg();
					} else {
						// other commands, bridge it to exec function
						parseCommand(args, argumentSize);
						if (DEBUGGABLE) {
							listCommands(&commandLL);
						}
						// check input file existence of commands one by one
						if (checkFileInputOfCmds(&commandLL) == 1) {
							// if it returns 1, so input file exist so correct so just execute cmds
							execute(&commandLL, background);
						}
						deleteLL(&commandLL); // clear command linkedlist
					}
			}
		}
	}

	return 0;
}
