#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "parser.h"
#include "parser.c"
#include <iostream>
#include <vector>
#include <fcntl.h>

using namespace std;

void print(vector<char *> const &a) {

    cout << "[";

   for(int i=0; i < a.size(); i++)
        cout << a.at(i) << ", ";
    cout << " ]\n";
}

void printArr(char* a[], int len){

    cout << "Printing Array: [";
    for (int i = 0; i < len; i++) {
        cout << a[i] << ' ';
    }
    cout << "]\n";
}

void print2(vector <vector<char *>> const &a) {

   cout << "The vector elements are : \n[";
   for(int i=0; i < a.size(); i++){
    print(a[i]);
   }
   cout << "]" << endl;
}


struct b_commands       //List to store bundle commands
{
    vector <vector<char *>> command_lst ;  //commands in a bundle 
    string bundle_name;                   //bundle's name
    int bundle_id;                        //bundle's id
};


int max_process_num(vector<b_commands>& all_bundles){

   int maxres = 0;
   for(int i=0; i < all_bundles.size(); i++){
        if (all_bundles[i].command_lst.size() > maxres){
            maxres = all_bundles[i].command_lst.size();
        }
   }
   return maxres;
}


int execute_bundles(vector<b_commands> all_bundles, parsed_input * parsing, int ind){

    int bundle_count = parsing->command.bundle_count;
    int pids[bundle_count];
    string bundleName;

    int i, index, next_index, file;
    int b_PROCESS_NUM, nextb_PROCESS_NUM, MAX_PROCESS_NUM;

    MAX_PROCESS_NUM = max_process_num(all_bundles);

    int second_pipes[bundle_count][MAX_PROCESS_NUM][2];
    int pipes[bundle_count][2];
    int repeater_pipe[bundle_count][2];

    int repeater;

    for (int z = 0; z < bundle_count; z++) {            //Create Pipes
        if (pipe(pipes[z]) == -1) {
                printf("Error with creating pipe\n");
                return 1;
        }
    }

    cout << "EXECUTING BUNDLES" << endl;

    for (i = 0; i < bundle_count; i++) {                    //go over all exec bundles
 
        bundleName = parsing->command.bundles[i].name;
        for (int y = 0; y < bundle_count; y++) {   
            if ( bundleName == all_bundles[y].bundle_name){  //find index of bundle in vector
                    index = y;
            }
        }

        b_PROCESS_NUM = all_bundles[index].command_lst.size();

        int pids[b_PROCESS_NUM];

        if (i + 1 < bundle_count){                                //To create secondary pipes
            bundleName = parsing->command.bundles[i+1].name;
            for (int y = 0; y < bundle_count; y++) {   
                if ( bundleName == all_bundles[y].bundle_name){   //find index of next bundle in vector
                        next_index = y;
                }
            }

            nextb_PROCESS_NUM = all_bundles[next_index].command_lst.size(); //get processes of next bundle

            for (int z = 0; z < nextb_PROCESS_NUM; z++) {           //Create Second Pipes
                if (pipe(second_pipes[i][z]) == -1) {
                    printf("Error with creating pipe\n");
                    return 1;
                }
            }

        }
        
        if (i == 0){        //first bundle
            cout << "First bundle" << endl;

            char * INPUT = parsing->command.bundles[i].input;   

            for (int j = 0; j < b_PROCESS_NUM; j++) {       //run processes of first bundle
                //cout << "j = " << j << endl;
                pids[j] = fork();
                if (pids[j] == 0) {
                    //Child Process
                    cout << "Child Created?? for  " << j << endl;

                    if (INPUT){     //Add input to the argument list of all the processes
                        file = open(INPUT, O_RDONLY, 0777);
                        dup2(file,STDIN_FILENO);
                        close(file);
                    }
                    for (int k = 0; k < bundle_count; k++) {    //close unnecessary pipes
                        if (i != k) {
                            close(pipes[k][0]);
                            close(pipes[k][1]);
                        }
                    }
                    close(pipes[i][0]);
                    dup2(pipes[i][1],STDOUT_FILENO);     //write to pipe
                    close(pipes[i][1]);                  //close write end

                    int arr_size = all_bundles[index].command_lst[j].size()+1;

                    char* arr[arr_size];
                    int k=0;
                    for ( ; k < arr_size-1; k++){
                        arr[k] = all_bundles[index].command_lst[j][k];
                    }
                    arr[k] = NULL;
                    
                    execvp(arr[0],  arr);
                    return 0;
                }
            }   
        }
        else if ( (i+1) == bundle_count){
            cout << "last bundle" << endl;

            char * OUTPUT = parsing->command.bundles[bundle_count-1].output;

            for (int j = 0; j < b_PROCESS_NUM; j++) {       //run processes of a sucessor bundle
                pids[j] = fork();
                if (pids[j] == 0) {
                    //Child Process
                    int child_file;
                    if (OUTPUT){                    //Output redirection block
                        cout << "I HAVE OUTPUT FILE" << OUTPUT << endl;
                        child_file = open(OUTPUT, O_CREAT | O_APPEND  | O_WRONLY, 0777);
                        dup2(child_file,STDOUT_FILENO);
                        close(child_file);
                    }
                    for (int k = 0; k < bundle_count; k++) {    //close unnecessary pipes
                        if ( (i-1) != k) {
                            close(pipes[k][0]);
                            close(pipes[k][1]);
                        }
                    }
                    close(pipes[i-1][1]);
                    dup2(pipes[i-1][0], STDIN_FILENO);
                    close(pipes[i-1][0]);  ///??

                    int arr_size = all_bundles[index].command_lst[j].size()+1;

                    char* arr[arr_size];
                    int k=0;
                    for ( ; k < arr_size-1; k++){
                        arr[k] = all_bundles[index].command_lst[j][k];
                    }
                    arr[k] = NULL;
                    //cout << "DOOO" << endl;
                    execvp(arr[0],  arr);
                    return 0;
                }
            }
        }
        else{
            cout << "middle bundle" << endl;
            for (int j = 0; j < b_PROCESS_NUM; j++) {       //run processes of a sucessor bundle

                pids[j] = fork();
                if (pids[j] == 0) {
                    //Child Process

                for (int k = 0; k < bundle_count; k++) {    //close unnecessary pipes
                        if ( (i-1) != k && i != k) {
                            close(pipes[k][0]);
                            close(pipes[k][1]);
                        }
                }
                //close(second_pipes[i-1][j][1]);
                //dup2(second_pipes[i-1][j][0], STDIN_FILENO);
                //close(second_pipes[i-1][j][0]);
                close(pipes[i-1][1]);
                dup2(pipes[i-1][0], STDIN_FILENO);
                close(pipes[i-1][0]);

                close(pipes[i][0]);
                dup2(pipes[i][1],STDOUT_FILENO);
                close(pipes[i][1]);   

                    int arr_size = all_bundles[index].command_lst[j].size()+1;

                    char* arr[arr_size];
                    int k=0;
                    for ( ; k < arr_size-1; k++){
                        arr[k] = all_bundles[index].command_lst[j][k];
                    }
                    arr[k] = NULL;

                    execvp(arr[0],  arr);
                    return 0;
                }
            }
        }
        for (int i = 0; i < b_PROCESS_NUM; i++) { //reap all the child processes
                wait(NULL);
        }
        if ( i+1 < bundle_count){       //check if not last bundle or not
            /*
            repeater = fork();
            if (repeater == 0){         //repeater process, receive inputs
                
                for (int j = 0; j < b_PROCESS_NUM; j++) {
                    close(pipes[i][1]);
                    dup2(pipes[i][0], STDIN_FILENO);      //read from all proceses
                    close(pipes[i][0]);
                }
                cout << "COMBINED INPUT-------------------" << endl;
                for (int j = 0; j < nextb_PROCESS_NUM; j++) {
                    close(second_pipes[i][j][0]);
                    dup2(second_pipes[i][j][1], STDOUT_FILENO); //wrong here
                    close(second_pipes[i][j][1]);
                }
                //execlp("cat", "cat", NULL);
                //STDIN --> STDOUT(Doesn't Work, PUT CAT)
                return 0;
            }*/

        }

    }
    for (int z = 0; z < bundle_count; z++) {            //Create Pipes
        close(pipes[z][0]);
        close(pipes[z][1]);
    }

    return 0;
}

int execute_bundle_only_one(vector<b_commands> all_bundles, parsed_input * parsing, int ind){

    int PROCESS_NUM;
    int file,file2;
    if (parsing->command.bundle_count == 1){      //make sure called for 1 bundle

        int pid;
        PROCESS_NUM = all_bundles[ind].command_lst.size(); //Number of processes in a bundle

        char * INPUT = parsing->command.bundles->input;    
        char * OUTPUT = parsing->command.bundles->output;


        int pids[PROCESS_NUM];
        for (int j = 0; j < PROCESS_NUM; j++) { //Each loop creates 1 child which runs 1 process.
            pids[j] = fork();                   //Create Child
            if (pids[j] == 0) {
                //Child Process

                if (INPUT){     //Add input to the argument list of all the processes
                    file = open(INPUT, O_RDONLY, 0777);
                    dup2(file,STDIN_FILENO);
                    close(file);
                }
                if (OUTPUT){                    //Output redirection block
                    file = open(OUTPUT, O_CREAT | O_APPEND  | O_WRONLY, 0777);
                    dup2(file,STDOUT_FILENO);
                    close(file);
                }

                int arr_size = all_bundles[ind].command_lst[j].size()+1;
                char* arr[arr_size];
                int i=0;
                for ( ; i < arr_size-1 ; i++){
                    arr[i] = all_bundles[ind].command_lst[j][i]; //fill up array with arguments
                }
                arr[i] = NULL;
                execvp(arr[0],  arr);       //execute the process
                return 0;                   //if process fails kill the child
            }
        }
        //Wait for all children to die
        for (int i = 0; i < PROCESS_NUM; i++) {
            wait(NULL);
        }

    }
    return 0;
}



int main (int argc, char** argv){
    char line[256];
    parsed_input parsing[256];
    int is_bundle=0;
    char* for_argv [20];

    int x = 0,i,j,k,y;
    int execute_flag = 0;
    int first_bundle_index;

    struct parsed_command *pc = new parsed_command; 
    parsing->command = *pc;
    parsing->argv = for_argv;

    vector<b_commands> all_bundles;
    b_commands temp_b_comm;
    int busy_in_creation=0;

    while (1){


        fgets(line, 256, stdin);
        char bundle_name[20];

        char delimiter[] = " ";
        string firstWord;
        char *secondWord, *remainder, *context;
        int inputLength = strlen(line);
        char *inputCopy = (char*) calloc(inputLength + 1, sizeof(char));
        strncpy(inputCopy, line, inputLength);

        firstWord = strtok_r (inputCopy, delimiter, &context);
        secondWord = strtok_r (NULL, delimiter, &context);
        remainder = context;

        for(int i=0; i < all_bundles.size(); i++){
            if (  firstWord == all_bundles[i].bundle_name || firstWord == all_bundles[i].bundle_name + "\n" ){
                execute_flag = 1;
                first_bundle_index = i;
            }
        }

        if (execute_flag){
            parse(line, 0, parsing);
            if (parsing->command.bundle_count == 1){ 
                int p = execute_bundle_only_one(all_bundles,parsing,first_bundle_index);
                cout << "R: " << p << endl;
            }
            else{
                execute_bundles(all_bundles,parsing,first_bundle_index);
            }
            execute_flag = 0;
        }

        else if (firstWord == "quit\n" && busy_in_creation == 0 ){
            break;                      
        }
        else if (firstWord == "pbc" && busy_in_creation == 0){
            parse(line, 0, parsing);                      //start bundle processing
            busy_in_creation = 1;
        }
        else if (firstWord == "pbs\n" && busy_in_creation == 1){
            busy_in_creation = 0;
            parse(line, 0, parsing);                      //finish bundle processing
            all_bundles.push_back(temp_b_comm);
            temp_b_comm.command_lst.clear();
            temp_b_comm.bundle_name = "";
            temp_b_comm.bundle_id = -1;
            x = 0;
        }
        else{
            vector<char *> line_comm;

            parse(line, 1, parsing);

            if (x == 0){
                temp_b_comm.bundle_name = parsing->command.bundle_name;
                temp_b_comm.bundle_id = parsing->command.bundle_count;
            }


            y = 0;                          //y is for argument check
            while (parsing->argv[y] != NULL){
                line_comm.push_back(parsing->argv[y]);
                y++;
            }

            temp_b_comm.command_lst.push_back(line_comm);

            //print2(temp_b_comm.command_lst);

            x++;

        }
    }
    return 0;
}