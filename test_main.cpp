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

using namespace std;

#define  SHM_SM_NAME "/PD_semaphore"
#define  SHM_NAME "/PD_SharedMemory"
#define REAL_GOLD_MESSAGE "You found Real Gold!!"
#define FAKE_GOLD_MESSAGE "You found Fool's Gold!!"
#define EMPTY_MESSAGE_PLAYER_MOVED "m"
#define EMPTY_MESSAGE_PLAYER_NOT_MOVED "n"
#define YOU_WON_MESSAGE "You Won!"
#define MSG_QUEUE_PREFIX "/PD_MSG_QUEUE_P"


struct mapboard{
  int rows;
  int cols;
  //unsigned char playing;
  pid_t player_pids[5];
  unsigned char map[0];
};

Map * gameMap = NULL;
mqd_t readqueue_fd; //message queue file descriptor
string mq_name;
sem_t* shm_sem;
mapboard * mbp = NULL;
int thisPlayer = 0, thisPlayerLoc= 0;

mapboard * initSharedMemory(int rows, int columns){
  int fd, size;
  mapboard * mbp;
  fd = shm_open(SHM_NAME,O_RDWR|O_CREAT,S_IRUSR|S_IWUSR);
  size = (rows*columns + sizeof(mapboard));
  ftruncate(fd, size);
  mbp = (mapboard*) mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  return mbp;
}

mapboard * readSharedMemory(){
  int fd, size, rows, columns;
  mapboard * mbp;
  fd = shm_open(SHM_NAME,O_RDWR, S_IRUSR|S_IWUSR);
  read(fd,&rows,sizeof(int));
  read(fd,&columns,sizeof(int));
  size = (rows*columns + sizeof(mapboard));
  ftruncate(fd, size);
  mbp = (mapboard*) mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  return mbp;
}

vector<vector< char > > readMapFromFile(char * mapFile, int &golds){
  vector<vector< char > > mapVector;
  vector< char > temp;
  string line;
  char c;
  ifstream mapStream(mapFile);
  mapStream >>golds;
  mapStream.get(c);

  while(getline(mapStream,line))
  {
     for(int i=0; i < line.length(); i++){
       temp.push_back(line[i]);
     }
     mapVector.push_back(temp);
     temp.clear();
  }
  return mapVector;
}

void initGameMap(mapboard * mbp, vector<vector< char > > mapVector ){
  unsigned char * mp;
  mp = mbp->map;
  for(unsigned i=0;i<mapVector.size();i++){
    for(unsigned j=0;j<mapVector[i].size();j++){
      if(mapVector[i][j]==' ')
        *mp=0;
      else if(mapVector[i][j]== '*')
        *mp=G_WALL;
      mp++;
    }
  }
  return;
}

int placeElementOnMap(mapboard * mbp, int elem){
   srand(time(NULL));
   int pos, total_pos =  mbp->rows * mbp->cols;
   while(1){
     pos = rand() % total_pos;
     //cout<<"rand "<<pos<<endl;

     if(mbp->map[pos] == G_WALL)
      continue;
     if(mbp->map[pos] == G_GOLD)
       continue;
     if(mbp->map[pos] == G_FOOL)
        continue;
     if(mbp->map[pos] == G_PLR0)
        continue;
     if(mbp->map[pos] == G_PLR1)
        continue;
     if(mbp->map[pos] == G_PLR2)
        continue;
     if(mbp->map[pos] == G_PLR3)
       continue;
     if(mbp->map[pos] == G_PLR4)
       continue;


    mbp->map[pos] = elem;
    break;
   }
   return pos;
}

void placeGoldsOnMap(mapboard * mbp, int goldCount){
  if(goldCount > 0)
    placeElementOnMap(mbp, G_GOLD);
  for(int i= 0; i< (goldCount-1) ; i++)
    placeElementOnMap(mbp, G_FOOL);
  return;
}

int placeIncrementPlayerOnMap(mapboard * mbp,int & thisPlayerLoc){
  int thisPlayer = -1;

    if(mbp->player_pids[0] == -1 ){
      mbp->player_pids[0] = getpid();
      thisPlayer = G_PLR0;
    }
    else if(mbp->player_pids[1] == -1 ){
      mbp->player_pids[1] = getpid();
      thisPlayer = G_PLR1;
    }
    else if(mbp->player_pids[2] == -1 ){
      mbp->player_pids[2] = getpid();
      thisPlayer = G_PLR2;
    }
    else if(mbp->player_pids[3] == -1 ){
      mbp->player_pids[3] = getpid();
      thisPlayer = G_PLR3;
    }
    else if(mbp->player_pids[4] == -1 ){
      mbp->player_pids[4] = getpid();
      thisPlayer = G_PLR4;
    }
  thisPlayerLoc = placeElementOnMap(mbp, thisPlayer);
  return thisPlayer;
}


bool isCurrentMoveOffMap(mapboard * mbp, int currentPos , int nextPos){
  unsigned char * mp;
  mp = mbp->map;
  int rows = mbp->rows, cols = mbp->cols;
  if(currentPos < cols && nextPos < 0)
    return true;
  if( currentPos / cols == rows - 1 && nextPos >= rows * cols)
    return true;
  if( currentPos % cols == 0 && nextPos == currentPos -1 )
    return true;
  if( currentPos % cols == cols - 1 && nextPos == currentPos + 1)
    return true;

    return false;
}

bool isCurrentMoveValid(mapboard * mbp, int currentPos , int nextPos){
  unsigned char * mp;
  mp = mbp->map;
  int rows = mbp->rows, cols = mbp->cols;
  if(currentPos < cols && nextPos < 0)
    return false;
  if( currentPos / cols == rows - 1 && nextPos >= rows * cols)
    return false;
  if( currentPos % cols == 0 && nextPos == currentPos -1 )
    return false;
  if( currentPos % cols == cols - 1 && nextPos == currentPos + 1)
    return false;

  if(mp[nextPos] == G_WALL)
    return false;
  else
    return true;
}

const char * performGoldCheck(mapboard * mbp, int currentPos, bool & thisPlayerFoundGold){
  const char * realGoldMessage = REAL_GOLD_MESSAGE;
  const char * fakeGoldMessage = FAKE_GOLD_MESSAGE;
  const char * emptyMessage = EMPTY_MESSAGE_PLAYER_MOVED;

  unsigned char * mp;
  mp = mbp->map;
  if(mp[currentPos] & G_GOLD)
    {
      thisPlayerFoundGold = true;
      mp[currentPos] &= ~G_GOLD;
      return realGoldMessage;
    }
  else if(mp[currentPos] & G_FOOL)
    return fakeGoldMessage;
  else
    return emptyMessage;

}

const char * processPlayerMove(mapboard * mbp, int & thisPlayerLoc, int thisPlayer, int keyInput, bool & thisPlayerFoundGold, bool & thisQuitGameloop){
  unsigned char * mp;
  const char * emptyMessage = EMPTY_MESSAGE_PLAYER_NOT_MOVED;
  const char * youWonMessage = YOU_WON_MESSAGE;
  bool quitGameLoop = false;
  mp = mbp->map;
  int nextPos = 0, cols = mbp->cols, goldCheck = -1;
  switch (keyInput) {
    case 108: // key l move right
      nextPos = thisPlayerLoc + 1;
      break;

    case 104: // key h move left
      nextPos = thisPlayerLoc - 1;
      break;

    case 107: // key k move up
      nextPos = thisPlayerLoc - cols;
      break;

    case 106: // key j move down
      nextPos = thisPlayerLoc + cols ;
      break;

  }

  if((thisPlayerFoundGold) && isCurrentMoveOffMap(mbp, thisPlayerLoc, nextPos) ){
    thisQuitGameloop = true;
    return youWonMessage;
  }

  if(isCurrentMoveValid(mbp, thisPlayerLoc, nextPos) ){
    mp[thisPlayerLoc] &= ~thisPlayer;
    thisPlayerLoc = nextPos;
    mp[thisPlayerLoc] |= thisPlayer;

    return performGoldCheck(mbp, thisPlayerLoc, thisPlayerFoundGold);
  }

  return emptyMessage;
}

bool isGameBoardEmpty(mapboard * mbp){
   if (mbp->player_pids[0] == -1 && mbp->player_pids[1] == -1 && mbp->player_pids[2] == -1 && mbp->player_pids[3] == -1 && mbp->player_pids[4] == -1)
   return true;
   else
   return false;

}

int getPlayerFromMask(int pMask){
  if(pMask & G_PLR0) return 0;
  else if (pMask & G_PLR1) return 1;
  else if (pMask & G_PLR2) return 2;
  else if (pMask & G_PLR3) return 3;
  else if (pMask & G_PLR4) return 4;
  else return -1;
}

unsigned int getActivePlayersMask(){
  unsigned int mask = 0;
  if(mbp->player_pids[0] != -1 ){
    mask |= G_PLR0;
  }
  if(mbp->player_pids[1] !=  -1 ){
    mask |= G_PLR1;
  }
  if(mbp->player_pids[2] !=  -1 ){
    mask |= G_PLR2;
  }
  if(mbp->player_pids[3] !=  -1 ){
    mask |= G_PLR3;
  }
  if(mbp->player_pids[4] !=  -1 ){
    mask |= G_PLR4;
  }
return mask;
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
  bool isBoardEmpty = isGameBoardEmpty(mbp);
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

string itos_utility(int i){
  std::stringstream out;
  out << i;
  return out.str();
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

  int rows, cols, goldCount, keyInput = 0, currPlaying = -1;
  bool thisPlayerFoundGold = false , thisQuitGameloop = false;
  char * mapFile = "mymap.txt";
  const char * notice;
  unsigned char * mp; //map pointer
  vector<vector< char > > mapVector;


  shm_sem = sem_open(SHM_SM_NAME ,O_RDWR,S_IRUSR|S_IWUSR,1);
  if(shm_sem == SEM_FAILED)
  {
     shm_sem=sem_open(SHM_SM_NAME,O_CREAT,S_IRUSR|S_IWUSR,1);
     //cout<<"first player"<<endl;
     mapVector = readMapFromFile(mapFile, goldCount);
     rows = mapVector.size();
     cols = mapVector[0].size();

     sem_wait(shm_sem);
     mbp = initSharedMemory(rows, cols);
     mbp->rows = rows;
     mbp->cols = cols;
     mbp->player_pids[0] = -1; mbp->player_pids[1] = -1;mbp->player_pids[2] = -1;mbp->player_pids[3] = -1;mbp->player_pids[4] = -1;

     initGameMap(mbp, mapVector);
     placeGoldsOnMap(mbp, goldCount);
     thisPlayer = placeIncrementPlayerOnMap(mbp, thisPlayerLoc);
     sem_post(shm_sem);
     //cout<<"shm init done"<<endl;
   }
   else
   {
     //cout<<"not first player"<<endl;
     sem_wait(shm_sem);
     mbp = readSharedMemory();
     rows = mbp->rows;
     cols = mbp->cols;
     thisPlayer = placeIncrementPlayerOnMap(mbp, thisPlayerLoc);
     sem_post(shm_sem);
   }

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
