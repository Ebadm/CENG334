#include <stdio.h>
#include <iostream>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parser.c"
#include "fat32.h"
#include <string>
#include <vector>
#include <ctime>

using namespace std;


BPB_struct BPB;
uint8_t* g_Fat = NULL;

int current_cluster = 0;
int prev_cluster = 0;
int current_FATentry = 2;
FILE* disk;
FatFileEntry dir;
int FATentry;

string currDirectoryStr = "/> ";
string prev_cmd = "/> ";



int LBAToOffset(int32_t cluster) //sector to go to.
{
    if (cluster == 0)
        cluster = 2;
    return ( (cluster - 2) * BPB.SectorsPerCluster * BPB.BytesPerSector) + (BPB.BytesPerSector * BPB.ReservedSectorCount) + (BPB.NumFATs * BPB.extended.FATSize * BPB.BytesPerSector);
}

int getNextCluster(){

    if (current_cluster == 0){
        current_cluster = 2;
    }
    int FATblock_offset = (BPB.BytesPerSector * BPB.ReservedSectorCount) + (current_cluster * 4);
    fseek(disk, FATblock_offset, SEEK_SET);
    fread(&FATentry, sizeof(int), 1, disk);
    FATentry = FATentry & 0x0FFFFFFF;
    if (FATentry >= 0x0FFFFFF0){
        return -1;
    }
    return FATentry;
}

void setFATnext(int prevFAT, int newFAT){

    int FATblock_offset = (BPB.BytesPerSector * BPB.ReservedSectorCount) + (prevFAT * 4);
    fseek(disk, FATblock_offset, SEEK_SET);
    fread(&FATentry, sizeof(int), 1, disk);
    if(FATentry >= 0x0FFFFFF0){
        FATentry = newFAT;
        fseek(disk, FATblock_offset, SEEK_SET);
        fwrite(&FATentry, sizeof(FATentry), 1, disk);
    }
}

int getEmptyCluster(){


    int after = 0;
    int FATentries = (BPB.extended.FATSize * BPB.BytesPerSector)/4;
    for (int i = 2; i < FATentries; i++){
        int FATblock_offset = (BPB.BytesPerSector * BPB.ReservedSectorCount) + (i* 4);
        fseek(disk, FATblock_offset, SEEK_SET);
        fread(&FATentry, sizeof(int), 1, disk);
        FATentry = FATentry & 0x0FFFFFFF;
        if (FATentry == 0x00000000){
            FATentry = 0x0FFFFFFF; 
            fseek(disk, FATblock_offset, SEEK_SET);
            fwrite(&FATentry, sizeof(FATentry), 1, disk);
            return i;
        }
    }
    return -1;
}

unsigned char ChkSum (unsigned char *pFcbName){
  short FcbNameLen;
  unsigned char Sum;

  Sum = 0;                                                                                                                                                                                           
  for (FcbNameLen = 11; FcbNameLen != 0; FcbNameLen--)
    {   
      // NOTE: The operation is an unsigned char rotate right                 
      Sum = ((Sum & 1) ? 0x80 : 0) + (Sum >> 1) + *pFcbName++;
    }   
  return (Sum);
}

void changetoString( uint16_t* ucs2,int ucs2len, char *tmp)
{
    tmp[0] = '\0';
    int  nn;
    int  result = 0;
    uint8_t* pp = (uint8_t *)ucs2;
    for (nn = 0; nn < ucs2len; pp += 2, nn++) {
        int  c = (pp[1] << 8) | pp[0];
        if(c == 0xffff){
            break;
        }
        tmp[nn] = c;
    }
    tmp[nn] = '\0';
}

void init_BPB(){

    fseek(disk,0,SEEK_SET);
    fread(&BPB, sizeof(BPB_struct), 1, disk); 
    //printf("%d %d %d %d %d %d\n", BPB.BytesPerSector,BPB.SectorsPerCluster , BPB.ReservedSectorCount, BPB.NumFATs, BPB.RootEntryCount, BPB.extended.FATSize);
}

void print_entries(vector<string> entries){
    for(int i=0; i < entries.size(); i++){
        if (i+1 == entries.size()){
            cout << entries.at(i) << endl;
        }
        else{
            cout << entries.at(i) << ' ';
        }
    }
}

void print_entries_nline(vector<string> entries){
    for(int i=0; i < entries.size(); i++){
        cout << entries.at(i) << endl;
    }
}

vector<int> gotoCluster(char *dirname)
{
    vector<int> result;
    bool found = 1;
    int entries_count = 0;
    string lfnName = "";
    string totalName = "";
    char * tmp = (char *)malloc(20);
    char * fullName;
    int offset = LBAToOffset(current_cluster);
    //printf("%d \n", offset);
    fseek(disk, offset, SEEK_SET);  
    fread(&dir, 32, 1, disk);   //read an entry
    int offset_count = 0;
    int init_cluster = current_cluster;
    int file_count = 0;

    while(true){
        if (offset_count >= BPB.BytesPerSector*BPB.SectorsPerCluster){
            int pcluster = current_cluster;
            current_cluster = getNextCluster();
            if (current_cluster == -1){
                current_cluster = pcluster;
                found = 0;
                break;
            }
            int offset = LBAToOffset(current_cluster);
            fseek(disk, offset, SEEK_SET);  
            fread(&dir, 32, 1, disk);
            offset_count = 0;
            continue;
        }
        if (dir.lfn.sequence_number == 0xE5){
            fread(&dir, 32, 1, disk);
            entries_count++;
            continue;
        }
        if (dir.lfn.sequence_number == 0x00){
            found = 0;
            break;
        }
        if(dir.lfn.attributes == 0x0f && dir.lfn.reserved == 0x00){ //LFN
            changetoString( dir.lfn.name1 , sizeof(dir.lfn.name1)/2 , tmp);
            lfnName += string(tmp,strlen(tmp));
            changetoString( dir.lfn.name2 , sizeof(dir.lfn.name2)/2 , tmp);
            lfnName += string(tmp,strlen(tmp));
            changetoString( dir.lfn.name3, sizeof(dir.lfn.name3)/2 , tmp);
            lfnName += string(tmp,strlen(tmp));
        }
        else{                           //SFN       //strok it on ~

            file_count++;
            if(init_cluster != 0 && entries_count == 1){
                if (!strcmp("..", dirname)){
                    current_cluster = (dir.msdos.eaIndex << 2) + dir.msdos.firstCluster;
                    result.push_back(found);
                    return result;
                }
            }

            totalName  += string((char *)dir.msdos.filename);
            totalName  += string((char *)dir.msdos.extension);
            string token = totalName .substr(0, totalName.find('~'));

            if (!strcmp(token.c_str(), dirname) && dir.msdos.attributes == 0x10){ //Found Directory to CD into
                current_cluster = (dir.msdos.eaIndex << 2) + dir.msdos.firstCluster;
                result.push_back(found);
                return result;
            }
            totalName.clear(); 
        }
 
        totalName  = lfnName + totalName;
        lfnName.clear();
        fread(&dir, 32, 1, disk);
        entries_count++;
        offset_count += 32;
    }
    result.push_back(found);
    result.push_back(current_cluster);
    result.push_back(offset_count);
    result.push_back(file_count);

    current_cluster = init_cluster;

    return result;
}

void update_cmd(char* inp_arg){

    if (!strcmp("..",inp_arg)){
        int found = currDirectoryStr.find_last_of("/");
        currDirectoryStr = found == 0? "/> " : (currDirectoryStr.substr(0,found) + "> ");
    }
    else{
        if (currDirectoryStr[currDirectoryStr.length()-3] !=  '/')
            currDirectoryStr.insert(currDirectoryStr.length()-2, "/");

        currDirectoryStr.insert(currDirectoryStr.length()-2, inp_arg);
    }
}

void gotoRoot(){
    current_cluster = 0;
    int offset = LBAToOffset(current_cluster);
    fseek(disk, offset, SEEK_SET); 
    currDirectoryStr = "/> ";
}

void ChangeDirectory(char* path){

    if (path == NULL){
        gotoRoot();
        return;
    }

    int prevtokencluster;
    prev_cluster = current_cluster;
    prev_cmd = currDirectoryStr;
    bool reset = false;

    char * token = strtok(path, "/");
    //Check if it is relative or absolute path
    if (token != NULL && current_cluster){
        current_cluster = 0;
        gotoCluster(token);
        if (current_cluster == 0){      //Not absolute path
            current_cluster = prev_cluster;
        }
        else{
            gotoRoot();                 //Absolute Path
        }
    }

    // loop through the string to extract all other tokens
    while( token != NULL ) {
        prevtokencluster = current_cluster;
        gotoCluster(token);
        if (current_cluster == prevtokencluster && strcmp(".", token)){
            reset = true;
            break;
        }
        int offset = LBAToOffset(current_cluster);
        fseek(disk, offset, SEEK_SET);
        if (prevtokencluster != current_cluster)
            update_cmd(token);
        token = strtok(NULL, "/");
    }
    if (reset == true){
        current_cluster = prev_cluster;
        int offset = LBAToOffset(current_cluster);
        fseek(disk, offset, SEEK_SET);
        currDirectoryStr = prev_cmd;
    }
}

int bitExtracted(int number, int k, int p)
{
    return (((1 << k) - 1) & (number >> (p - 1)));
}

string getDateTime(uint16_t dateBlock, uint16_t timeBlock){

    string months[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    short int date,month,minute,hour;

    string result = "";
    date = bitExtracted(dateBlock,5,1);
    month = bitExtracted(dateBlock,4,6);
    minute = bitExtracted(timeBlock,6,6);
    hour = bitExtracted(timeBlock,5,12);

    result = months[month] + " " + to_string(date) + " " + to_string(hour) + ":" + to_string(minute);
    return result;
}

vector<string> traverse_directory(parsed_input* input, bool isDetails)
{

    vector<string> result;
    int entries_count = 0;
    string lfnName = "";
    string totalName = "";
    char * tmp = (char *)malloc(20);
    char * fullName;
    int offset = LBAToOffset(current_cluster);
    fseek(disk, offset, SEEK_SET);  
    fread(&dir, 32, 1, disk);   //read an entry
    int offset_count = 0;
    int init_cluster = current_cluster;

    while(true){
        if (offset_count >= BPB.BytesPerSector*BPB.SectorsPerCluster){
            current_cluster = getNextCluster();
            if (current_cluster == -1){
                break;
            }
            int offset = LBAToOffset(current_cluster);
            fseek(disk, offset, SEEK_SET);  
            fread(&dir, 32, 1, disk);
            offset_count = 0;
            continue;
        }
        if (dir.lfn.sequence_number == 0xE5){
            fread(&dir, 32, 1, disk);
            entries_count++;
            continue;
        }
        if (dir.lfn.sequence_number == 0x00){
            break;
        }
        if(dir.lfn.attributes == 0x0f && dir.lfn.reserved == 0x00){ //LFN
            changetoString( dir.lfn.name1 , sizeof(dir.lfn.name1)/2 , tmp);
            lfnName += string(tmp,strlen(tmp));
            changetoString( dir.lfn.name2 , sizeof(dir.lfn.name2)/2 , tmp);
            lfnName += string(tmp,strlen(tmp));
            changetoString( dir.lfn.name3, sizeof(dir.lfn.name3)/2 , tmp);
            lfnName += string(tmp,strlen(tmp));
        }
        else{                           //SFN       //strok it on ~
            totalName  += string((char *)dir.msdos.filename);
            totalName  += string((char *)dir.msdos.extension);
            string token = totalName .substr(0, totalName.find('~'));
            if(current_cluster == 0 || entries_count > 1){ //All except . and .. entries
                if(isDetails){
                    if(dir.msdos.attributes == 0x10){
                        string infoLine = "drwx------ 1 root root 0 " + getDateTime(dir.msdos.modifiedDate ,dir.msdos.modifiedTime) + " " + token;
                        result.push_back(infoLine);
                    }
                    else if(dir.msdos.attributes == 0x20){
                        string infoLine = "-rwx------ 1 root root " +  to_string(dir.msdos.fileSize) + " " + getDateTime(dir.msdos.modifiedDate ,dir.msdos.modifiedTime) + " " + token;
                        result.push_back(infoLine);
                    }
                }
                else{
                    result.push_back(token);
                }
            }
            totalName.clear(); 
        }
        totalName  = lfnName + totalName;
        lfnName.clear();
        fread(&dir, 32, 1, disk);
        entries_count++;
        offset_count += 32;
    }
    current_cluster = init_cluster;
    return result;
}

void ShowContents(parsed_input* input){

    if (!input->arg1 || strcmp("-l", input->arg1)){      //no -l argument
        if (input->arg1){
            prev_cluster = current_cluster;
            prev_cmd = currDirectoryStr;
            ChangeDirectory(input->arg1);
        }
        vector<string> dir_entries = traverse_directory(input,false);
        print_entries(dir_entries);
        if (input->arg1){
            current_cluster = prev_cluster;
            currDirectoryStr = prev_cmd;
        }
    }
    else{                                               //-l as argument
        if (input->arg2){
            prev_cluster = current_cluster;
            prev_cmd = currDirectoryStr;
            ChangeDirectory(input->arg2);
        }
        vector<string> dir_entries = traverse_directory(input,true);
        print_entries_nline(dir_entries);
        if (input->arg2){
            current_cluster = prev_cluster;
            currDirectoryStr = prev_cmd;
        }
    }
}

//initialise all with FF
// Start from f1

void init_FF(int arr_size, uint16_t* name){

    for (int i=0; i < arr_size  ; i++){
        name[i] = 0xFFFF;
    }
}

int initialise_LFNstr(int len, FatFileLFN& temp, string filename){

    init_FF(5, temp.name1);
    init_FF(6, temp.name2);
    init_FF(2, temp.name3);
    int i = 0;


    do{
        if (i >= 11){
            temp.name3[i-11] = filename[i];
        }
        else if (i >= 5){
            temp.name2[i-5] = filename[i];
        }
        else{
            temp.name1[i] = filename[i];
        }
    }
    while(filename[i] != '\0' && ++i < 13);

    if ( i < 13){
        i++;
    }
    return i;
}

vector<FatFileEntry>createLFNEntries(string fileName, unsigned char checksum){

    vector<FatFileEntry> res;
    char * tmp2 = (char *)malloc(20);
    string tempStr;
    int len = fileName.length();
    int count = 0;
    int c = 0;
    string subFile;

    while (c < len+1){
        FatFileEntry temp;
        subFile = fileName.substr(c,len);
        temp.lfn.sequence_number = 0x41;
        temp.lfn.attributes = 0x0F;
        temp.lfn.reserved = 0x00;
        temp.lfn.firstCluster = 0x0000;
        temp.lfn.checksum = checksum;
        c = c + initialise_LFNstr(len,temp.lfn,subFile);
        res.push_back(temp);
    }
    return res;
}

unsigned char init_ShortEntry(FatFile83 & sf, int indexnum, bool dot, bool dotdot, int dorddclust){



    for (int i=0; i < 8;i++){
        sf.filename[i] = 0x20;
    }
    for (int i=0; i < 3; i++){
        sf.extension[i] = 0x20;
    }

    if (!dot && !dotdot) //if normal entry
    {
        sf.filename[0] = '~';

        vector<char> character;
        while (indexnum > 0)
        {
            int digit = indexnum%10;
            indexnum /= 10;
            character.push_back(digit +'0');
        }

        int i = 1;
        while(!character.empty() && i < 8){
            sf.filename[i] = character.back();
            character.pop_back();
            i++;        
        }

        int cluster = getEmptyCluster();
        cout << "FREE CLUSTER AT : " << cluster << endl;
        sf.eaIndex = cluster >> 16;
        sf.firstCluster = cluster;
    }
    else if (dot){
        sf.filename[0] = '.';
        sf.eaIndex = dorddclust >> 16;
        sf.firstCluster = dorddclust;

    }
    else if (dotdot){
        sf.filename[0] = '.';
        sf.filename[1] = '.';
        sf.eaIndex = dorddclust >> 16;
        sf.firstCluster = dorddclust;
    }

    sf.attributes = 0x10;
    sf.reserved =  0x00;
    sf.creationTimeMs = 0x00;
    sf.fileSize = 0x00000000;
    //TODO

    std::time_t t = std::time(0);
    std::tm* now = std::localtime(&t);

    uint16_t date = 0x00FFFF & (((now->tm_year - 80) << 9) | (now->tm_mon << 5) | now->tm_mday);
    uint16_t time = 0x00FFFF & (now->tm_hour << 11) | (now->tm_min << 5) | (now->tm_sec) ;

    sf.creationTime = time;
    sf.creationDate = date;
    sf.modifiedDate = date;
    sf.modifiedTime = time;
    sf.lastAccessTime = date;
    unsigned char ck = ChkSum(sf.filename);
    return ck;
}

void write_directoryentry(vector<FatFileEntry>& entries, int appendOffset){

    FatFileEntry val;
    int new_cluster = 0;
    int offset;
    //appendOffset = appendOffset - 64;
    char * tmp2 = (char *)malloc(20);
    while(!entries.empty()){
        offset = LBAToOffset(current_cluster);
        fseek(disk, offset+appendOffset, SEEK_SET);
        cout << "Directory writing init offset: " << offset+appendOffset << endl;
        while(appendOffset < BPB.BytesPerSector*BPB.SectorsPerCluster && !entries.empty()){
            val = entries.back();
            fwrite(&val, 32, 1, disk);
            entries.pop_back();
            cout << "wrote entry on : " << appendOffset << " of cluster " << current_cluster << endl;
            appendOffset = appendOffset + 32;
        }
        if (!entries.empty()){
            new_cluster = getEmptyCluster();
            setFATnext(current_cluster,new_cluster);
            current_cluster = new_cluster;
            appendOffset = 0;
        }
    }

}


void MakeDirectory(parsed_input* input){

    vector<FatFileEntry> entries;
    FatFileEntry sf;
    FatFile83 dot;
    FatFile83 dotdot;
    bool isPath, found;
    int appendCluster, appendOffset, indexNum;
    vector<int> res;
    int offset_free;
    unsigned char checksum;
    string path(input->arg1);
    isPath = path.find('/') == -1 ? false : true;
    char * tmp2 = (char *)malloc(20);

    prev_cluster = current_cluster;
    prev_cmd = currDirectoryStr;

    if(isPath){
        string gotoPath = path.substr(0,path.find_last_of('/'));
        char char_path[gotoPath.length() + 1];
        strcpy(char_path, gotoPath.c_str());
        cout << char_path << endl;
        ChangeDirectory(char_path);
        cout << current_cluster << endl;
    }

    path = path.substr(path.find_last_of('/')+1,path.length());
    char foldername[path.length() + 1];
    strcpy(foldername, path.c_str());
    res = gotoCluster(foldername);
    found = res.at(0);
    cout << "FOUND == " << found << endl;
    if (!found){    //if file not found
        appendCluster = res.at(1); appendOffset = res.at(2); indexNum = 1 + res.at(3);
        cout << appendCluster << "->" << appendOffset << " " << indexNum << endl;
        checksum = init_ShortEntry(sf.msdos, indexNum, 0 , 0, 0);
        entries = createLFNEntries(path,checksum);
        entries.insert(entries.begin(), sf);      //Add short entry to the beginning
        int preceding_cluster = current_cluster;
        current_cluster = sf.msdos.eaIndex << 16 | sf.msdos.firstCluster;
        init_ShortEntry(dot, 0, 1, 0, current_cluster);
        int offset = LBAToOffset(current_cluster);
        cout << "OFFSET FOR DOTL " << offset << endl;
        fseek(disk, offset, SEEK_SET);
        fwrite(&dot, 32, 1, disk);
        init_ShortEntry(dotdot, 0, 0,1, preceding_cluster);
        fwrite(&dotdot, 32, 1, disk);
        if (appendCluster != -1){
            current_cluster = appendCluster;
        }
        current_cluster = appendCluster;
        write_directoryentry(entries,appendOffset);
    }

    current_cluster = prev_cluster;
    currDirectoryStr = prev_cmd;

}




void execute(parsed_input* input){

    if (input->type == CD){
        ChangeDirectory(input->arg1);
    }
    else if (input->type == LS){
        ShowContents(input);
    }
    else if (input->type == MKDIR){
        MakeDirectory(input);
    } 



}




int main(int argc, char** argv){

    printf("ARGUMENTS: %d %s\n", argc, argv[1]);
    if (argc < 2) {
        printf("Error, Provide Image file");
        return -1;
    }
    disk = fopen(argv[1], "rw+");
    if (disk == NULL){
        printf("Image does not exist\n");
        return -1;
    }

    init_BPB();

    while(1){
        char line[256];
        parsed_input* inp = new parsed_input;
        cout << currDirectoryStr;
        fgets(line, sizeof(line), stdin);
        parse(inp, line);
        //printf("Input corresponds to = %d %s %s\n", inp->type, inp->arg1, inp->arg2);
        if(inp->type == QUIT){
            break;
        }
        execute(inp);
    }

    return 0;
}

