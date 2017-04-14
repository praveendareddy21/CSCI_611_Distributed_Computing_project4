#include<iostream>
#include "goldchase.h"
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/types.h>
#include "Map.h"
#include <cstring>
#include <signal.h>
#include <mqueue.h>
#include<cstring>
#include <cstdio>
#include <errno.h>
#include <cstdlib>
#include <sstream>
#include "fancyRW.h"
using namespace std;

// uncomment before multi-node

#ifndef SHM_CONSTANTS
#define SHM_CONSTANTS
#define  SHM_SM_NAME "/PD_semaphore"
#define  SHM_NAME "/PD_SharedMemory"
#define MSG_QUEUE_PREFIX "/PD_MSG_QUEUE_P"
#define IS_CLIENT 0
#endif

#define PORT "42425"
#define REAL_GOLD_MESSAGE "You found Real Gold!!"
#define FAKE_GOLD_MESSAGE "You found Fool's Gold!!"
#define EMPTY_MESSAGE_PLAYER_MOVED "m"
#define EMPTY_MESSAGE_PLAYER_NOT_MOVED "n"
#define YOU_WON_MESSAGE "You Won!"



struct mapboard{
  int rows;
  int cols;
  //unsigned char playing;
  pid_t player_pids[5];
  int daemonID;
  unsigned char map[0];
};

void invoke_in_Daemon( void (*f) (string));
void init_Server_Daemon(string);
void init_Client_Daemon(string);


Map * gameMap = NULL;
mqd_t readqueue_fd; //message queue file descriptor
string mq_name;
sem_t* shm_sem;
mapboard * mbp = NULL;
int thisPlayer = 0, thisPlayerLoc= 0;

//####################################################### test map util #######################################

#include"test_main_util.cpp"

//###########################################################################################################
void long_sleep(){
  sleep(30);
}


void init_Server_Daemon(string ip_address){
  int rows, cols;
  sem_wait(shm_sem);
  mbp = readSharedMemory();
  rows = mbp->rows;
  cols = mbp->cols;
  mbp->daemonID = getpid();
  sem_post(shm_sem);

  FILE * fp = fopen ("/home/red/611_project/CSCI_611_Distributed_Computing_project4/gchase_server.log", "w+");
  fprintf(fp, "Logging info from daemon with pid : %d\n", getpid());
  fprintf(fp, "readSharedMemory done. rows - %d cols - %d\n", rows, cols);
  fflush(fp);

  fprintf(fp,"All done in server demon, Killing daemon with pid -%d now.\n", getpid());
  fclose(fp);
  exit(0);

}

void init_Client_Daemon(string ip_address){
  int rows, cols, goldCount, fd;
  char * mapFile = "mymap.txt";

  //vector<vector< char > > mapVector;
  vector<vector< char > > mapVector(26, vector<char> (80, '*'));
  mapVector[0][0] = ' ';mapVector[0][1] = ' ';mapVector[2][2] = ' ';mapVector[3][3] = ' ';

  FILE * fp = fopen ("/home/red/611_project/CSCI_611_Distributed_Computing_project4/gchase_client.log", "w+");
  fprintf(fp, "Logging info from daemon with pid : %d\n", getpid());
  fprintf(fp, "Rece IP Address as : %s\n", ip_address.c_str());


  fprintf(fp,"Reading from mapfile now.\n");
  fflush(fp);

  //mapVector = readMapFromFile(mapFile, goldCount);
  rows = mapVector.size();
  cols = mapVector[0].size();

  fprintf(fp, "read from file done. rows - %d cols - %d\n", rows, cols);
  shm_sem=sem_open(SHM_SM_NAME,O_CREAT,S_IRUSR|S_IWUSR,1);
  fprintf(fp,"checkpoint 0.\n");
  fflush(fp);


  fprintf(fp,"checkpoint 1.\n");
  fflush(fp);

  sem_wait(shm_sem);
  mbp = initSharedMemory(rows, cols);

  fprintf(fp,"checkpoint 2.\n");
  fflush(fp);

  mbp->rows = rows;
  mbp->cols = cols;
  mbp->player_pids[0] = -1; mbp->player_pids[1] = -1;mbp->player_pids[2] = -1;mbp->player_pids[3] = -1;mbp->player_pids[4] = -1;
  mbp->daemonID = -1;
  initGameMap(mbp, mapVector);
  sem_post(shm_sem);

  if ( ( fd = shm_open(SHM_NAME, O_RDONLY, S_IRUSR|S_IWUSR)) != -1)
    fprintf(fp,"Shm open successful in client daemon \n");
  else
    fprintf(fp,"Shm open failed in client daemon \n");



  fprintf(fp,"initilized Shm, posting semaphore \n");


  fprintf(fp,"All done in Cliet demon, Killing daemon with pid -%d now.\n", getpid());
  fclose(fp);
  exit(0);
}


vector<vector< char > >  getMapVectorFromServerDaemon(){
  int rows, cols;
  //vector<vector< char > > mapVector;
  vector<vector< char > > mapVector(26, vector<char> (80, '*'));
  vector< char > temp;
  string line;
  char c;

  FILE * fp = fopen ("/home/red/611_project/CSCI_611_Distributed_Computing_project4/gchase_client.log", "w+");
  fprintf(fp, "Logging info from daemon with pid : %d\n", getpid());
  fflush(fp);
  int sockfd, status; //file descriptor for the socket

  //change this # between 2000-65k before using
  const char* portno=PORT;

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints)); //zero out everything in structure
  hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
  hints.ai_socktype=SOCK_STREAM; // TCP stream sockets

  struct addrinfo *servinfo;
  //instead of "localhost", it could by any domain name
  if((status=getaddrinfo("localhost", portno, &hints, &servinfo))==-1)
  {
    fprintf(fp, "getaddrinfo error: %s\n", gai_strerror(status));
    exit(1);
  }
  sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

  if((status=connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
  {
    perror("connect");
    exit(1);
  }
  //release the information allocated by getaddrinfo()
  freeaddrinfo(servinfo);

  const char* message="One small step for (a) man, one large  leap for Mankind";
  int n;

  WRITE<char> (sockfd, "To", 2);
  WRITE<char> (sockfd, "dd", 2);
  WRITE<char> (sockfd, "Gibso", 5);
  WRITE<char> (sockfd, "n", 1);

  fclose(fp);
  return mapVector;

  fprintf(fp,"client wrote %d characters\n", 11);
  char buffer[100];
  memset(buffer, 0, 100);
  read(sockfd, buffer, 99);
  fprintf(fp, "%s\n", buffer);
  close(sockfd);
  return mapVector;
}



void invoke_in_Daemon( void (*f) (string)){

  if(fork() > 0)
    return;

  if(fork()>0)
  exit(0);

  if(setsid()==-1)//child obtains its own SID & Process Group
    exit(1);
  for(int i=0; i<sysconf(_SC_OPEN_MAX); ++i)
    close(i);
  open("/dev/null", O_RDWR); //fd 0
  open("/dev/null", O_RDWR); //fd 1
  open("/home/red/611_project/CSCI_611_Distributed_Computing_project4/g.log", O_RDWR); //fd 2


  umask(0);
  chdir("/");
  string ip_address = "localhost";
  (*f)(ip_address);

}

void refreshMap(int){
  if(gameMap != NULL){
    (*gameMap).drawMap();
  }
}

void sendSignalToActivePlayers(mapboard * mbp, int signal_enum){
  for(int i=0; i<5; i++){
    if(mbp->player_pids[i] != -1 && i != getPlayerFromMask(thisPlayer) ){ // to other active players only
      kill(mbp->player_pids[i], signal_enum);
    }
  }
}

void handleGameExit(int){
  // clean ups all game's stuff when exiting forceful or otherwise
  delete gameMap;

  sem_wait(shm_sem);
  mbp->map[thisPlayerLoc] &= ~thisPlayer;
  mbp->player_pids[getPlayerFromMask(thisPlayer)] = -1;
  sem_post(shm_sem);
  sendSignalToActivePlayers(mbp, SIGUSR1);
  bool isBoardEmpty = isGameBoardEmpty(mbp); //TODO
  mq_close(readqueue_fd);
  mq_unlink(mq_name.c_str());


  if(isBoardEmpty)
  {
     shm_unlink(SHM_NAME);
     sem_close(shm_sem);
     sem_unlink(SHM_SM_NAME);
  }
  exit(0);

}

void initializeMsgQueue(int thisPlayer){
  mq_name = MSG_QUEUE_PREFIX;
  mq_name = mq_name + itos_utility(getPlayerFromMask(thisPlayer));
  struct mq_attr mq_attributes;
  mq_attributes.mq_flags=0;
  mq_attributes.mq_maxmsg=10;
  mq_attributes.mq_msgsize=120;

  if((readqueue_fd=mq_open(mq_name.c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
          S_IRUSR|S_IWUSR, &mq_attributes))==-1)
  {

    perror("mq_open");
    handleGameExit(0);
  }
  //set up message queue to receive signal whenever message comes in
  struct sigevent mq_notification_event;
  mq_notification_event.sigev_notify=SIGEV_SIGNAL;
  mq_notification_event.sigev_signo=SIGUSR2;
  mq_notify(readqueue_fd, &mq_notification_event);


}

void sendMsgToPlayer(int thisPlayer, int toPlayerInt, string msg, bool is_msg_prefix){
  mqd_t writequeue_fd;
  string msg_queue_name = MSG_QUEUE_PREFIX, msg_queue_suffix, msg_prefix;

  msg_queue_suffix = itos_utility(toPlayerInt);
  msg_queue_name = msg_queue_name + msg_queue_suffix;

  if (is_msg_prefix){
    msg_prefix = "Player #" + itos_utility(getPlayerFromMask(thisPlayer)+1) + " says:";
    msg = msg_prefix + msg;
  }


  if((writequeue_fd=mq_open(msg_queue_name.c_str(), O_WRONLY|O_NONBLOCK))==-1)
  {

    perror("Error in mq_send");
    handleGameExit(0);
  }

  char message_text[251];
  const char *ptr = msg.c_str();
  memset(message_text, 0, 251);
  strncpy(message_text, ptr, 250);

  if(  mq_send(writequeue_fd, message_text, strlen(message_text), 0) == -1)
  {
      perror("Error in mq_send");
      handleGameExit(0);
  }
  mq_close(writequeue_fd);

}

void sendMsgBroadcastToPlayers(int thisPlayer, string msg){
  for(int i=0; i<5; i++){
    if(mbp->player_pids[i] != -1 && i != getPlayerFromMask(thisPlayer) ){
      sendMsgToPlayer(thisPlayer, i, msg, true);}
  }
}

void sendWinningMsgBroadcastToPlayers(int thisPlayer){
  string msg = "Player #" + itos_utility(getPlayerFromMask(thisPlayer)) + " won!";

  for(int i=0; i<5; i++){
    if(mbp->player_pids[i] != -1 && i != getPlayerFromMask(thisPlayer) ){
      sendMsgToPlayer(thisPlayer, i, msg, false);}
  }
}

void receiveMessage(int){
  int err;
  char msg[251]; //a char array for the message
  memset(msg, 0, 251); //zero it out (if necessary)
  struct sigevent mq_notification_event;
  mq_notification_event.sigev_notify=SIGEV_SIGNAL;
  mq_notification_event.sigev_signo=SIGUSR2;
  mq_notify(readqueue_fd, &mq_notification_event);

  while((err=mq_receive(readqueue_fd, msg, 250, NULL))!=-1)
  {
    if(gameMap != NULL)
      (*gameMap).postNotice(msg);
    //call postNotice(msg) for your Map object;
    //cout << "Message received: " << msg << endl;
    memset(msg, 0, 251);//set all characters to '\0'
  }
  //we exit while-loop when mq_receive returns -1
  //if errno==EAGAIN that is normal: there is no message waiting
  if(errno!=EAGAIN)
  {
    perror("mq_receive");
    handleGameExit(0);
  }

}

void setUpSignalHandlers(){
  struct sigaction exit_action;
  exit_action.sa_handler = handleGameExit;
  exit_action.sa_flags=0;
  sigemptyset(&exit_action.sa_mask);
  sigaction(SIGINT, &exit_action, NULL);
  sigaction(SIGTERM, &exit_action, NULL);
  sigaction(SIGHUP, &exit_action, NULL);

  struct sigaction my_sig_handler;
  my_sig_handler.sa_handler = refreshMap;
  sigemptyset(&my_sig_handler.sa_mask);
  my_sig_handler.sa_flags=0;
  sigaction(SIGUSR1, &my_sig_handler, NULL);

  struct sigaction action_to_take;
  //action_to_take.sa_handler=read_message;
  action_to_take.sa_handler=receiveMessage;
  sigemptyset(&action_to_take.sa_mask);
  action_to_take.sa_flags=0;
  sigaction(SIGUSR2, &action_to_take, NULL);

}


int main(int argc, char *argv[])
{

  int rows, cols, goldCount, keyInput = 0, currPlaying = -1, fd;
  bool thisPlayerFoundGold = false , thisQuitGameloop = false, inServerNode = false, inClientNode = false;
  char * mapFile = "mymap.txt",* daemon_server_ip;
  const char * notice;
  unsigned char * mp; //map pointer
  vector<vector< char > > mapVector;
  if(IS_CLIENT){ //argc == 2){ // ip to connect daemon server
    daemon_server_ip = argv[1];
    inClientNode = true;
    shm_sem = sem_open(SHM_SM_NAME ,O_RDWR,S_IRUSR|S_IWUSR,1);
    if(shm_sem == SEM_FAILED)//     semaphore and shm not initilized on client;
    {
      invoke_in_Daemon(init_Client_Daemon);
      // wait loop until shm is inited by client daemon

      while(1){ // loop until mbp is updated
        sleep(2);
        if ( (fd = shm_open(SHM_NAME, O_RDONLY, S_IRUSR|S_IWUSR)) == -1)
          cout<<"shm not set"<<endl;
        else{
          cout<<"shm set"<<endl;
          break;
        }
        sleep(1);
      }
    }

  }else{
    inServerNode = true;
  }


  shm_sem = sem_open(SHM_SM_NAME ,O_RDWR,S_IRUSR|S_IWUSR,1);
  if(shm_sem == SEM_FAILED)//     //cout<<"first player"<<endl;
  {
    /*
     if(inClientNode){
       // setup client demon
       invoke_in_Daemon(init_Client_Daemon);
       //mapVector = getMapVectorFromServerDaemon();
       mapVector = readMapFromFile(mapFile, goldCount); // remove later

     }else{ // not in client node, load map from mapFile
            mapVector = readMapFromFile(mapFile, goldCount);
     } */

     mapVector = readMapFromFile(mapFile, goldCount);
     shm_sem=sem_open(SHM_SM_NAME,O_CREAT,S_IRUSR|S_IWUSR,1);
     rows = mapVector.size();
     cols = mapVector[0].size();

     sem_wait(shm_sem);
     mbp = initSharedMemory(rows, cols);
     mbp->rows = rows;
     mbp->cols = cols;
     mbp->player_pids[0] = -1; mbp->player_pids[1] = -1;mbp->player_pids[2] = -1;mbp->player_pids[3] = -1;mbp->player_pids[4] = -1;
     mbp->daemonID = -1;

     initGameMap(mbp, mapVector);
     placeGoldsOnMap(mbp, goldCount);
     thisPlayer = placeIncrementPlayerOnMap(mbp, thisPlayerLoc);
     sem_post(shm_sem);

     if(inServerNode){
       //set up server node
       invoke_in_Daemon(init_Server_Daemon);
       cout<<"created server daemon"<<endl;

       while(1){ if (mbp->daemonID != -1) break;} // loop until daemonId is updated
       cout<<"shm init done daemonid "<<mbp->daemonID<<endl;
     }

   }
   else
   {
     cout<<"not first player"<<endl;
     sem_wait(shm_sem);
     mbp = readSharedMemory();
     rows = mbp->rows;
     cols = mbp->cols;
     thisPlayer = placeIncrementPlayerOnMap(mbp, thisPlayerLoc);
     sem_post(shm_sem);
   }

   /*
   cout<<"all done cleaning up shm now"<<endl;
   handleGameExit(0);
   return 0;
   */

   try
   {
     //sem_wait(shm_sem);
     gameMap = new Map(reinterpret_cast<const unsigned char*>(mbp->map),rows,cols);
     //sem_post(shm_sem);
     sendSignalToActivePlayers(mbp, SIGUSR1);
     initializeMsgQueue(thisPlayer);
     setUpSignalHandlers();


     while(keyInput != 81){ // game loop  key Q
       keyInput =  (*gameMap).getKey();
       // code for player moves
       if(keyInput ==  108 || keyInput ==  107 || keyInput ==  106 || keyInput ==  104 ) // for l, k, j, h
       { sem_wait(shm_sem);
         notice = processPlayerMove(mbp, thisPlayerLoc,  thisPlayer, keyInput, thisPlayerFoundGold, thisQuitGameloop);
         sem_post(shm_sem);
         if(notice == FAKE_GOLD_MESSAGE || notice == REAL_GOLD_MESSAGE ){
           sendSignalToActivePlayers(mbp, SIGUSR1);
           (*gameMap).postNotice(notice);
           (*gameMap).drawMap();
         }
         else if(notice == YOU_WON_MESSAGE ){
           sendSignalToActivePlayers(mbp, SIGUSR1);
           // broadcast winning msg
           sendWinningMsgBroadcastToPlayers(thisPlayer);
           (*gameMap).postNotice(notice);
           (*gameMap).drawMap();
         }
         else if(notice == EMPTY_MESSAGE_PLAYER_MOVED ){
           sendSignalToActivePlayers(mbp, SIGUSR1);
           (*gameMap).drawMap();
         }


         if(thisQuitGameloop)
          break;

       }
       else if(keyInput == 109){ // key m for message
         int toPlayerInt = getPlayerFromMask((*gameMap).getPlayer(getActivePlayersMask()) );
         string msg = (*gameMap).getMessage();
         sendMsgToPlayer(thisPlayer, toPlayerInt, msg, true);
       }
       else if(keyInput == 98){ // key b for broadcast
         string msg = (*gameMap).getMessage();
         sendMsgBroadcastToPlayers(thisPlayer, msg);
       }


     }// while looop ending
   }
   catch (const runtime_error& error)
   {
     cout<<"runtime_error!!  Window size not large enough"<<endl;
     cout<<"Exiting gracefully"<<endl;
     //sem_post(shm_sem);
   }

   handleGameExit(0);
   return 0;
}
