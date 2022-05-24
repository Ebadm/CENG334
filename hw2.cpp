#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <iterator>
#include <time.h>
#include <sys/time.h>
#include "hw2_output.c"

using namespace std;

int work=1 , last_state=0, break_over = 0, stop = 0, count = 0;
int active_priv,active_smokers = 0,nextOrdervar = 0;

pthread_mutex_t mut;                    //Mutex to protect locking of grid
pthread_mutex_t bud_mutex;              //break procedure protection mutex
pthread_mutex_t wait_mutex;             //wait procedure protection mutex
pthread_cond_t  condOrder;              //Condition variable to put a private on break back to work
pthread_cond_t  waitCondpriv;           //Used to instantly give order to a private who's waiting for next bud pickup
pthread_cond_t  waitCondsmoker;         //Used to instantly give order to a smoker who's waiting for next cig throw
pthread_cond_t  nextOrder;              //Makes sure that the order is printed before agent's response to it
pthread_cond_t  someUnlocks;            //Signals that there were some unlocks on the grid
pthread_barrier_t barrier;              //synch privates threads so they take the order together
pthread_barrier_t barrier_fin_call;     //synch privates threads so the next immediate call waits for prev call procedure to finish.
pthread_barrier_t smokerbarrier;        //synch smokers so they take the order together
pthread_barrier_t smokers_fin_call;     //synch smokers threads so the next immediate call waits for prev call procedure to finish.



struct SneakySmoker
{
    int sid;
    int smoke_time;
    int cell_count;
    vector<vector<int>> smoke_area_amount;
    void initialise_smokers(int id, int smoking_time , int count){
        sid = id;
        smoke_time = smoking_time;
        cell_count = count;
    }
    void add_area_amount(int cell_X, int cell_Y, int cig_count){
        vector<int> temp;
        temp.push_back(cell_X);
        temp.push_back(cell_Y);
        temp.push_back(cig_count);
        smoke_area_amount.push_back(temp);
    }

};

struct ProperPrivate
{
    int gid;
    int area_X;
    int area_Y;
    int wait_time_ms;
    int areas_to_gather;
    std::vector <std::pair<int,int>> areas;
    void initialise_ProperPrivate(int id, int x_area , int y_area, int time, int total_areas){
        gid = id;
        area_X = x_area;
        area_Y = y_area;
        wait_time_ms = time;
        areas_to_gather = total_areas;
    }
    void add_area(int first, int second){
        areas.push_back({first,second});
    }
};

struct thread_args                    
{
    ProperPrivate *priv;
    SneakySmoker*  smoker;
    int** grid;
    int** locker_grid;
    int** SSintersect;
};

void simple_tokenizer(vector<string>* input_lines,vector<string>* temp)
{
    string line = input_lines->front(); 
    input_lines->erase(input_lines->begin()); 
    temp->clear();
    stringstream ss(line);
    string word;
    while (ss >> word) {
        temp->push_back(word);
    }
}

void CTIMEWAIT(int TIMEOUT_MS, int priv1_or_smoker0){

    struct timeval ctv;
    struct timespec ts,waiting_time;

    gettimeofday(&ctv, NULL);
    waiting_time.tv_sec = ctv.tv_sec;
    waiting_time.tv_nsec = ctv.tv_usec*1000;

    waiting_time.tv_sec += (TIMEOUT_MS / 1000);
    waiting_time.tv_nsec += (TIMEOUT_MS % 1000) * 1000000;

    if (waiting_time.tv_nsec >= 1000000000) {
        waiting_time.tv_nsec -= 1000000000;
        waiting_time.tv_sec++;
    }
    if (priv1_or_smoker0){
        int e = pthread_cond_timedwait(&waitCondpriv,&wait_mutex, &waiting_time);
    }
    else{
        int e = pthread_cond_timedwait(&waitCondsmoker,&wait_mutex, &waiting_time);
    }
}


//////////////////////////////////////////Private Routine & Helper Functions//////////////////////////////
void unlock_area(int starting_x, int starting_y, int area_x_bound, int area_y_bound, thread_args str_args){

    for (int i = starting_x; i < starting_x + area_x_bound; i++) {       
        for (int j = starting_y; j < starting_y + area_y_bound; j++) {
            str_args.locker_grid[i][j] = 0;
        }
    }

}

void sync_orders(thread_args str_args, hw2_actions action){

    pthread_mutex_unlock(&bud_mutex);
    pthread_barrier_wait(&barrier);
    pthread_mutex_lock(&bud_mutex);
    
    while(nextOrdervar == 0)
        pthread_cond_wait(&nextOrder,&bud_mutex);

    hw2_notify(action, str_args.priv->gid, 0, 0);

    pthread_mutex_unlock(&bud_mutex);
    pthread_barrier_wait(&barrier_fin_call);
    pthread_mutex_lock(&bud_mutex);
}

bool break_procedure(int starting_x, int starting_y, int area_x_bound, int area_y_bound, thread_args str_args, int locked){
    int i , j;
    pthread_mutex_lock(&bud_mutex);
    if (work == 0 || stop == 1){    //if break or stop command comes
        if (work == 0){

            sync_orders(str_args, PROPER_PRIVATE_TOOK_BREAK);
            if (locked == 1){
                unlock_area(starting_x,starting_y,area_x_bound,area_y_bound,str_args);
                pthread_cond_broadcast(&someUnlocks);
            }

            while (work == 0)
                pthread_cond_wait(&condOrder,&bud_mutex);
        }
        if (stop == 1){

            sync_orders(str_args, PROPER_PRIVATE_STOPPED);

            pthread_mutex_unlock(&bud_mutex);
            pthread_exit(NULL);
        }
        else{
            sync_orders(str_args, PROPER_PRIVATE_CONTINUED);
            work = 1;                                                                      
            break_over = 1;
            pthread_mutex_unlock(&bud_mutex);
            return true;
        }
    }
    pthread_mutex_unlock(&bud_mutex);
    return false;
}

bool can_lock(int** locking_grid, int starting_x, int starting_y, int area_X, int area_Y){
    int i,j,x,y;
    int cannotLock_flag = 0;
    for (i = starting_x; i < starting_x + area_X; i++) {               // check and lock grid
        for (j = starting_y; j < starting_y +  area_Y; j++) {
            if (locking_grid[i][j] != 0){
                cannotLock_flag = 1;
                break;
            }
            else{
                locking_grid[i][j] = 1;
            }
        }
        if (cannotLock_flag){
            break;
        }
    }
    if (cannotLock_flag){                               // unlock grid if can't lock whole area

        int loop_size = starting_y + area_Y;
        for (x = starting_x; x <= i; x++) {              
            if ( x == i ) {      
                loop_size = j;
            }
            for (y = starting_y; y < loop_size; y++) {
                locking_grid[x][y] = 0;
            }
        }
        return false;
    }
    return true;
}

void pickup_cigs(int starting_x, int starting_y, int area_x_bound, int area_y_bound, thread_args str_args){

    for (int i = starting_x; i < starting_x + area_x_bound; i++) {              // pick up cigarettes 1 by 1
        for (int j = starting_y; j < starting_y + area_y_bound; j++) {
            while (str_args.grid[i][j] > 0){

                pthread_mutex_lock(&wait_mutex);
                CTIMEWAIT(str_args.priv->wait_time_ms,1);
                pthread_mutex_unlock(&wait_mutex);

                if (break_procedure(starting_x,starting_y,str_args.priv->area_X,str_args.priv->area_Y,str_args,1)){
                    return;
                }
                str_args.grid[i][j] = str_args.grid[i][j] - 1;
                hw2_notify(PROPER_PRIVATE_GATHERED, str_args.priv->gid, i, j);      //Right after picking up a cigbutt:
            }
        }
    }
    if(break_over == 1){ break_over = 0;}
    return;
}

void* proper_routine(void* args) {

    int i, j, v, x , y;
    thread_args str_args = *(thread_args *) args;
    hw2_notify(PROPER_PRIVATE_CREATED, str_args.priv->gid, 0, 0);  //Immediately after creation (thread starting)
    int size = str_args.priv->areas.size();

    for(int v=0; v < size; v++){
        int gotlock_flag=0;
        int starting_x = str_args.priv->areas.at(v).first;
        int starting_y = str_args.priv->areas.at(v).second;
        pthread_mutex_lock(&mut);
        if(can_lock(str_args.locker_grid,starting_x,starting_y,str_args.priv->area_X,str_args.priv->area_Y)){
            gotlock_flag = 1;
        }
        if (gotlock_flag == 0){
            pthread_cond_wait(&someUnlocks,&mut);
            pthread_mutex_unlock(&mut);
            break_procedure(starting_x,starting_y,str_args.priv->area_X,str_args.priv->area_Y,str_args,0);
            v = v-1;
            continue;
        }
        pthread_mutex_unlock(&mut);

        if (break_procedure(starting_x,starting_y,str_args.priv->area_X,str_args.priv->area_Y,str_args,1)){
            v = v-1;
            continue;
        }
        hw2_notify(PROPER_PRIVATE_ARRIVED, str_args.priv->gid , starting_x, starting_y); //After locking-down an area for gathering

        pickup_cigs(starting_x,starting_y,str_args.priv->area_X,str_args.priv->area_Y,str_args);

        if (break_over == 1){
            v--;
            continue;
        }
        
        pthread_mutex_lock(&mut);
        unlock_area(starting_x,starting_y,str_args.priv->area_X,str_args.priv->area_Y,str_args);
        pthread_mutex_unlock(&mut);

        hw2_notify(PROPER_PRIVATE_CLEARED, str_args.priv->gid, 0, 0);  //Once an area has been completely emptied of cigbutts and the proper private is leaving
        pthread_cond_broadcast(&someUnlocks);

        break_procedure(starting_x,starting_y,str_args.priv->area_X,str_args.priv->area_Y,str_args,0);

    }
    hw2_notify(PROPER_PRIVATE_EXITED, str_args.priv->gid, 0, 0); //finished emptying all its assigned areas and is about to exit
    
    active_priv--;
    pthread_barrier_init(&barrier, NULL, active_priv);
    pthread_barrier_init(&barrier_fin_call, NULL, active_priv);
    pthread_exit(NULL);
}




//////////////////////////////////////////Smoker Routine & Helper Functions//////////////////////////////
void unlock_area_sneakysmokers(int starting_x,int starting_y, thread_args str_args){

    for (int i = starting_x-1; i < starting_x + 2; i++) {
        for (int j = starting_y-1; j < starting_y +  2; j++) {
            if (str_args.SSintersect[i][j] < 2){
                str_args.locker_grid[i][j] = 0;
                str_args.SSintersect[i][j] = 0;
            }
            else {
                str_args.SSintersect[i][j]--;
            }
        }
    }
    if ( str_args.locker_grid[starting_x][starting_y] == 3){
        str_args.locker_grid[starting_x][starting_y] = 2;
    }
}

bool can_lock_smoker(int** locking_grid, int starting_x, int starting_y, int** SSintersect){
    int i,j,x,y;
    int cannotLock_flag = 0;
    if (locking_grid[starting_x][starting_y] == 3){                 //If already a smoker in that cell
        return false;
    }


    for (i = starting_x-1; i < starting_x + 2; i++) {               // check and lock grid for smoker
        for (j = starting_y-1; j < starting_y +  2; j++) {
            if (locking_grid[i][j] == 1){
                cannotLock_flag = 1;
                break;
            }
        }
        if (cannotLock_flag){
            break;
        }
    }
    if (cannotLock_flag == 0){                               //lock grid if can lock whole area
        for (i = starting_x-1; i < starting_x + 2; i++) {               //lock grid for smoker
            for (j = starting_y-1; j < starting_y +  2; j++) {
                SSintersect[i][j]++;
                if (locking_grid[i][j] != 3){
                    locking_grid[i][j] = 2;
                }
            }
        }
        locking_grid[starting_x][starting_y] = 3;                       //signifies cell of smoker

        return true;

    }
    return false;
}

bool stop_smoker(int starting_x, int starting_y, thread_args str_args){
    int i , j;
    pthread_mutex_lock(&bud_mutex);
    if (stop == 1){
            unlock_area_sneakysmokers(starting_x,starting_y,str_args);
            pthread_cond_broadcast(&someUnlocks);
            pthread_mutex_unlock(&bud_mutex);
            pthread_barrier_wait(&smokerbarrier);
            pthread_mutex_lock(&bud_mutex);
            pthread_cond_wait(&nextOrder,&bud_mutex);
            hw2_notify(SNEAKY_SMOKER_STOPPED, str_args.smoker->sid, 0, 0);
            pthread_mutex_unlock(&bud_mutex);
            pthread_barrier_wait(&smokers_fin_call);
            pthread_mutex_lock(&bud_mutex);

            pthread_mutex_unlock(&bud_mutex);
            pthread_exit(NULL);
        }
    pthread_mutex_unlock(&bud_mutex);
    return false;
}

pair<int,int> next_cig_loc(int i, int starting_x, int starting_y ){

    pair<int,int> result;
    i = i % 8;
    switch(i){
        case 0:
            result = {starting_x-1,starting_y-1};
            break;
        case 1:
            result = {starting_x-1,starting_y};
            break;
        case 2:
            result = {starting_x-1,starting_y+1};
            break;
        case 3:
            result = {starting_x,starting_y+1};
            break;
        case 4:
            result = {starting_x+1,starting_y+1};
            break;
        case 5:
            result = {starting_x+1,starting_y};
            break;
        case 6:
            result = {starting_x+1,starting_y-1};
            break;
        case 7:
            result = {starting_x,starting_y-1};
            break;
        default:
            break;

    }
    return result;
}

void smoke_ciggs(thread_args str_args, int starting_x, int starting_y, int cigs_to_smoke){
    int i , j;
    int smoked_cigs = 0;
    int finish = 0;
    pair<int,int> loc;

    for (i = 0; i < cigs_to_smoke; i++) {

        pthread_mutex_lock(&wait_mutex);
        CTIMEWAIT(str_args.smoker->smoke_time,0);
        pthread_mutex_unlock(&wait_mutex);

        stop_smoker(starting_x,starting_y,str_args);           
        loc = next_cig_loc(i,starting_x,starting_y);
        str_args.grid[loc.first][loc.second] = str_args.grid[loc.first][loc.second] + 1;
        hw2_notify(SNEAKY_SMOKER_FLICKED, str_args.smoker->sid, loc.first, loc.second);
    }

    return;
}

void* smoker_routine(void* args) {

    thread_args str_args = *(thread_args *) args;
    hw2_notify(SNEAKY_SMOKER_CREATED, str_args.smoker->sid, 0, 0);
    int size = str_args.smoker->cell_count;

    for(int v=0; v < size; v++){

        int gotlock_flag=0;
        int starting_x = str_args.smoker->smoke_area_amount[v][0];
        int starting_y = str_args.smoker->smoke_area_amount[v][1];
        int cig_count = str_args.smoker->smoke_area_amount[v][2];

        pthread_mutex_lock(&mut);
        if(can_lock_smoker(str_args.locker_grid,starting_x,starting_y,str_args.SSintersect)){
            gotlock_flag = 1;
        }
        if (gotlock_flag == 0){
        pthread_cond_wait(&someUnlocks,&mut);
        pthread_mutex_unlock(&mut);
        stop_smoker(starting_x,starting_y,str_args);
        v = v-1;
        continue;
        }
        pthread_mutex_unlock(&mut);

        hw2_notify(SNEAKY_SMOKER_ARRIVED, str_args.smoker->sid, starting_x, starting_y);

        smoke_ciggs(str_args,starting_x,starting_y,cig_count);

        pthread_mutex_lock(&mut);
        unlock_area_sneakysmokers(starting_x,starting_y,str_args);
        pthread_mutex_unlock(&mut);

        hw2_notify(SNEAKY_SMOKER_LEFT, str_args.smoker->sid, 0, 0);
        pthread_cond_broadcast(&someUnlocks);
        stop_smoker(starting_x,starting_y,str_args);
    }

    hw2_notify(SNEAKY_SMOKER_EXITED, str_args.smoker->sid, 0, 0);
    active_smokers--;
    pthread_barrier_init(&smokerbarrier,NULL,active_smokers);
    pthread_barrier_init(&smokers_fin_call,NULL,active_smokers);

    pthread_exit(NULL);
}


//////////////////////////////////////////Main//////////////////////////////
int main() {

    hw2_init_notifier();

    std::ofstream out("out.txt");
    std::streambuf *coutbuf = std::cout.rdbuf(); //save old buf
    std::cerr.rdbuf(out.rdbuf()); //redirect std::cout to out.txt!

    int number_of_sneaky_smokers=0;
    string line;
    std::vector<std::string> temp_vector;
    std::vector<std::string> input_lines;
    std::vector<std::string> order_lines;

    string filename("in.txt");
    ifstream input_file(filename);  

    while (getline(input_file, line)) {
        if (line == "")
            break;
        input_lines.push_back(line);
    }

    simple_tokenizer(&input_lines,&temp_vector);

    int grid_rows = stoi(temp_vector.at(0));            //Grid X - rows
    int grid_columns = stoi(temp_vector.at(1));         //Grid Y - columns
    
    int** GRID = new int*[grid_rows];                
    int ** locking_grid = new int*[grid_rows];
    int ** SSoverlap = new int*[grid_rows];
    for (int i = 0; i < grid_rows; i++) {
        GRID[i] = new int[grid_columns];
        locking_grid[i] = new int[grid_columns];
        SSoverlap[i] = new int[grid_columns];
    }

    for (int i = 0; i < grid_rows; i++) {              //Fill grid array with buds
        simple_tokenizer(&input_lines,&temp_vector);  
        for (int j = 0; j < grid_columns; j++) {
            GRID[i][j] = stoi(temp_vector.at(j));
        }
    }

    simple_tokenizer(&input_lines,&temp_vector);              
    const int proper_private_num = stoi(temp_vector.at(0));      //get number of proper privates

    ProperPrivate properPriv[proper_private_num];         

    for (int i = 0; i < proper_private_num; i++) {        //initialise attributes of all proper privates

        simple_tokenizer(&input_lines,&temp_vector);
        properPriv[i].initialise_ProperPrivate(stoi(temp_vector[0]),stoi(temp_vector[1]),stoi(temp_vector[2]),stoi(temp_vector[3]),stoi(temp_vector[4]));
        for (int j = 0; j < properPriv[i].areas_to_gather ; j++) {
            simple_tokenizer(&input_lines,&temp_vector);  
            properPriv[i].add_area(stoi(temp_vector.at(0)),stoi(temp_vector.at(1)));
        }
    }
    
    for (int i = 0; i < grid_rows; i++) {               //initialise locker grid
        for (int j = 0; j < grid_columns; j++) {
           locking_grid[i][j] = 0;
           SSoverlap[i][j] = 0;
        }
    }

    pthread_t th[proper_private_num];
    active_priv =  proper_private_num+1;
    pthread_mutex_init(&mut,NULL);
    pthread_mutex_init(&bud_mutex,NULL);
    pthread_mutex_init(&wait_mutex,NULL);
    pthread_cond_init(&condOrder, NULL);
    pthread_cond_init(&someUnlocks, NULL);
    pthread_cond_init(&waitCondpriv,NULL);
    pthread_cond_init(&waitCondsmoker,NULL);
    pthread_barrier_init(&barrier, NULL, active_priv);
    pthread_barrier_init(&barrier_fin_call, NULL, active_priv);
    pthread_cond_init(&nextOrder, NULL);
    pthread_barrier_init(&smokerbarrier,NULL,active_smokers);
    pthread_barrier_init(&smokers_fin_call,NULL,active_smokers);
    
    for (int i = 0; i < proper_private_num ; i++) {    //create threads
        thread_args* args = (thread_args*) malloc(sizeof(thread_args));
        (*args).locker_grid = locking_grid;
        (*args).priv = &properPriv[i];
        (*args).grid = GRID;
        if (pthread_create(&th[i], NULL, &proper_routine, args) != 0) {
            perror("Failed to create thread");
        }
    }

    int number_of_orders;
    if (!input_lines.empty()){
        simple_tokenizer(&input_lines,&temp_vector);
        number_of_orders = stoi(temp_vector[0]);      //get number of orders
        for (int i = 0; i < number_of_orders ; i++) { 
            order_lines.push_back(input_lines.front());
            input_lines.erase(input_lines.begin()); 
        }
    }


    if (!input_lines.empty()){
        simple_tokenizer(&input_lines,&temp_vector);   
        number_of_sneaky_smokers = stoi(temp_vector[0]);      //get number of sneaky smokers
    }

    SneakySmoker sneakyS[number_of_sneaky_smokers]; 
    pthread_t ss[number_of_sneaky_smokers];
    active_smokers = number_of_sneaky_smokers+1; 

    for (int i = 0; i < number_of_sneaky_smokers; i++) {        //initialise attributes of all proper privates

        simple_tokenizer(&input_lines,&temp_vector);
        sneakyS[i].initialise_smokers(stoi(temp_vector[0]),stoi(temp_vector[1]),stoi(temp_vector[2]));
        for (int j = 0; j < sneakyS[i].cell_count ; j++) {
            simple_tokenizer(&input_lines,&temp_vector);
            sneakyS[i].add_area_amount(stoi(temp_vector.at(0)),stoi(temp_vector.at(1)),stoi(temp_vector.at(2)));
        }
    }


    for (int i = 0; i < number_of_sneaky_smokers ; i++) {    //create smoker threads

        thread_args* args_sneaky = (thread_args*) malloc(sizeof(thread_args));
        (*args_sneaky).smoker = &sneakyS[i];
        (*args_sneaky).grid = GRID;
        (*args_sneaky).locker_grid = locking_grid;
        (*args_sneaky).SSintersect = SSoverlap;
        
        if (pthread_create(&ss[i], NULL, &smoker_routine, args_sneaky) != 0) {
            perror("Failed to create thread");
        }
    }

    //execute orders
    if (!order_lines.empty()){
   
        int repeating_call = 0;
        for (int i = 0; i < number_of_orders ; i++) { 
            simple_tokenizer(&order_lines,&temp_vector);
            int time_ms = stoi(temp_vector.at(0));
            string command = temp_vector.at(1);
            while(1){
                if (get_timestamp() > time_ms*1000){
                    cerr << "COMMAND IS = " << command << " AT " << get_timestamp() << endl;
                    if (command == "break"){
                        if (work == 0 || stop == 1){ //if break after break/stop
                            repeating_call = 1;
                            hw2_notify(ORDER_BREAK,0, 0, 0);
                            break;
                        }
                        else {
                            last_state = work;
                            work = 0;
                            pthread_cond_broadcast(&someUnlocks);
                            pthread_cond_broadcast(&waitCondpriv);
                            pthread_barrier_wait(&barrier);

                            pthread_mutex_lock(&bud_mutex);
                            hw2_notify(ORDER_BREAK,0, 0, 0);
                            nextOrdervar = 1;
                            //usleep(100);      //small sleep to make sure broadcast signal is sent after threads are waiting on condition
                            pthread_cond_broadcast(&nextOrder);
                            pthread_mutex_unlock(&bud_mutex);
                        }
                    }
                    else if(command == "continue"){
                        if (work == 1 || stop == 1){ //if cont after cont/stop
                            repeating_call = 1;
                            hw2_notify(ORDER_CONTINUE, 0, 0, 0);
                            break;
                        }
                        //cout << "continue comes" << endl;
                        pthread_mutex_lock(&bud_mutex);
                        last_state = work;
                        work = 1;
                        if (last_state == 0)
                            pthread_cond_broadcast(&condOrder);

                        pthread_mutex_unlock(&bud_mutex);

                        pthread_barrier_wait(&barrier);

                        pthread_mutex_lock(&bud_mutex);
                        hw2_notify(ORDER_CONTINUE, 0, 0, 0);
                        nextOrdervar = 1;
                        //usleep(100);      //small sleep to make sure broadcast signal is sent after threads are waiting on condition
                        pthread_cond_broadcast(&nextOrder);
                        pthread_mutex_unlock(&bud_mutex);
                    }
                    else if(command == "stop"){
                        if (stop == 1){ //if cont after cont/stop
                            repeating_call = 1;
                            hw2_notify(ORDER_STOP, 0, 0, 0);
                            break;
                        }
                        stop = 1;
                        work = 1;
                        pthread_cond_broadcast(&someUnlocks);
                        pthread_cond_broadcast(&condOrder);
                        pthread_cond_broadcast(&waitCondpriv); pthread_cond_broadcast(&waitCondsmoker);
                        pthread_barrier_wait(&barrier);
                        if(active_smokers){
                            pthread_barrier_wait(&smokerbarrier);
                        }
                        hw2_notify(ORDER_STOP, 0, 0, 0);
                        //usleep(100);      //small sleep to make sure broadcast signal is sent after threads are waiting on condition
                        pthread_cond_broadcast(&nextOrder);
                        break;
                    }
                    break;
                }
            }
            if (repeating_call == 1){
                repeating_call = 0;
                continue;
            }
            if (stop == 1 && active_smokers){
                pthread_barrier_wait(&smokers_fin_call);
            }
            pthread_barrier_wait(&barrier_fin_call);
            nextOrdervar = 0;
        }
    }

    for (int i = 0; i < proper_private_num ; i++) {         //wait on private threads
        if (pthread_join(th[i], NULL) != 0) {
            perror("Failed to join private thread");
        }
    }
    for (int i = 0; i < number_of_sneaky_smokers ; i++) {   //wait on smoker threads
        if (pthread_join(ss[i], NULL) != 0) {
            perror("Failed to join smoker thread");
        }
    }


    for (int i=0; i< grid_rows; i++) {
            delete[] GRID[i];
            delete[] locking_grid[i];
            delete[] SSoverlap[i];
        }
        delete[] GRID;
        delete[] locking_grid;
        delete[] SSoverlap;


    pthread_mutex_destroy(&mut);
    pthread_mutex_destroy(&bud_mutex);
    pthread_cond_destroy(&condOrder);
    pthread_cond_destroy(&nextOrder);
    pthread_cond_destroy(&someUnlocks);
    pthread_cond_destroy(&waitCondpriv);
    pthread_cond_destroy(&waitCondsmoker);
    pthread_mutex_destroy(&wait_mutex);
    pthread_barrier_destroy(&barrier);
    pthread_barrier_destroy(&smokerbarrier);
    pthread_barrier_destroy(&barrier_fin_call);
    pthread_barrier_destroy(&smokers_fin_call);

    return 0;
}
