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
#define IS_CLIENT -1
#define PORT "42425"  //change this # between 2000-65k before using
#define PORT1 "42426"  //change this # between 2000-65k before using
#endif


#define REAL_GOLD_MESSAGE "You found Real Gold!!"
#define FAKE_GOLD_MESSAGE "You found Fool's Gold!!"
#define EMPTY_MESSAGE_PLAYER_MOVED "m"
#define EMPTY_MESSAGE_PLAYER_NOT_MOVED "n"
#define YOU_WON_MESSAGE "You Won!"
#define CLIENT_LOG_DIR "/tmp/gchase_client.log"
#define SERVER_LOG_DIR "/tmp/gchase_server.log"



struct mapboard{
  int rows;
  int cols;
  pid_t player_pids[5];
  int daemonID;
  unsigned char map[0];
};

void invoke_in_Daemon( void (*f) (string), string);
void init_Server_Daemon(string);
void init_Client_Daemon(string);

vector< char >  perform_IPC_with_server(FILE *fp, int & rows, int & cols, string ip_address);
void perform_IPC_with_client(FILE *fp);
void send_Socket_Message(char PLR_MASK, string msg);
void send_Socket_Player(char PLR_MASK);
void send_Socket_Map(vector< pair<short, char> > mapChangesVector);

void socket_Communication_Handler(FILE *fp);
void process_Socket_Message(FILE *fp, char protocol_type);
void process_Socket_Player(FILE *fp, char protocol_type);
void process_Socket_Map(FILE *fp, char protocol_type);
void handleGameExit(int);
void sendSignalToActivePlayers(mapboard * mbp, int signal_enum);
void sendSignalToActivePlayersOnNode(mapboard * mbp, int signal_enum);
void sendSignalToDaemon(mapboard * mbp, int signal_enum);
void initializeMsgQueue(int thisPlayer);
void initializeMsgQueueInDaemon(int thisPlayer);
void sendMsgFromDaemonToPlayer(int toPlayerInt, string msg);

Map * gameMap = NULL;
mqd_t readqueue_fd; //message queue file descriptor
string mq_name;
sem_t* shm_sem;
mapboard * mbp = NULL;
int thisPlayer = 0, thisPlayerLoc= 0;
char initial_map[2100];
int write_fd = -1;
int read_fd = -1;
mqd_t daemon_readqueue_fds[5]; //message queue file descriptor in daemon
string daemon_mq_names[5];

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

string itos_utility(int i){
  std::stringstream out;
  out << i;
  return out.str();
}

vector< char >  perform_IPC_with_server(FILE *fp, int & rows, int & cols, string ip_address){
  int sockfd, status; //file descriptor for the socket
  const char* portno= PORT;
  char * ip_cstr = new char[ip_address.length()+1];
  strcpy(ip_cstr, ip_address.c_str());

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints)); //zero out everything in structure
  hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
  hints.ai_socktype=SOCK_STREAM; // TCP stream sockets

  struct addrinfo *servinfo;
  //instead of "localhost", it could by any domain name
  if((status=getaddrinfo(ip_cstr, portno, &hints, &servinfo))==-1)
  {fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));exit(1);}

  sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

  if((status=connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
  {perror("connect");exit(1);}

  //release the information allocated by getaddrinfo()
  freeaddrinfo(servinfo);
  fprintf(fp, "Connected to server.\n");

  char initial_map[2100];

  READ<int>(sockfd, &rows, sizeof(int));
  READ<int>(sockfd, &cols, sizeof(int));

  vector< char >  mbpVector(rows*cols , '*');
  fprintf(fp, "reading from server done. rows - %d cols - %d\n", rows,cols);

  READ<char>(sockfd, initial_map, (rows*cols + 1)*sizeof(char));

  for (int i=0; i < rows*cols; i++)
      mbpVector[i] = initial_map[i];

  fprintf(fp, "reading from server done map - %s\n", initial_map);
  read_fd = sockfd;
  delete [] ip_cstr;
  //close(sockfd);
  return mbpVector;
}

void perform_IPC_with_client(FILE *fp){
  int sockfd, status, rows, cols, iter = 0; //file descriptor for the socket
  const char* portno = PORT;
  struct addrinfo hints;

  memset(&hints, 0, sizeof(hints)); //zero out everything in structure
  hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
  hints.ai_socktype=SOCK_STREAM; // TCP stream sockets
  hints.ai_flags=AI_PASSIVE; //file in the IP of the server for me

  struct addrinfo *servinfo;
  if((status=getaddrinfo(NULL, portno, &hints, &servinfo))==-1)
  {fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));exit(1);}

  sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

  /*avoid "Address already in use" error*/
  int yes=1;
  if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))==-1)
  {perror("setsockopt");exit(1);}

  //We need to "bind" the socket to the port number so that the kernel
  //can match an incoming packet on a port to the proper process
  if((status=bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
  {perror("bind");exit(1);}

  //when done, release dynamically allocated memory
  freeaddrinfo(servinfo);

  if(listen(sockfd,1)==-1)
  {perror("listen");exit(1);}

  fprintf(fp, "Blocking, waiting for client to connect\n");

  struct sockaddr_in client_addr;
  socklen_t clientSize=sizeof(client_addr);
  int new_sockfd;
  if((new_sockfd=accept(sockfd, (struct sockaddr*) &client_addr, &clientSize))==-1)
  {perror("accept");exit(1);}

  fprintf(fp, "Connected to client.\n");

  rows = mbp->rows;
  cols = mbp->cols;

  WRITE<int>(new_sockfd, &rows, sizeof(int));
  WRITE<int>(new_sockfd, &cols, sizeof(int));
  WRITE<char>(new_sockfd, initial_map, (rows*cols + 1)*sizeof(char));

  fprintf(fp, "Writing to client completed.\n");
  write_fd = new_sockfd;
  //close(new_sockfd);
}

int get_Read_Socket_fd(FILE * fp){
  int sockfd; //file descriptor for the socket
 int status; //for error checking

 fprintf(fp, "Attempting to get Read Socket fd for Socket protocol IPC.\n");
 fflush(fp);
 //change this # between 2000-65k before using
 const char* portno = PORT1;
 struct addrinfo hints;
 memset(&hints, 0, sizeof(hints)); //zero out everything in structure
 hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
 hints.ai_socktype=SOCK_STREAM; // TCP stream sockets
 hints.ai_flags=AI_PASSIVE; //file in the IP of the server for me

 struct addrinfo *servinfo;
 if((status=getaddrinfo(NULL, portno, &hints, &servinfo))==-1)
 {
   fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
   exit(1);
 }
 sockfd=socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);

 /*avoid "Address already in use" error*/
 int yes=1;
 if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int))==-1)
 {
   perror("setsockopt");
   exit(1);
 }

 //We need to "bind" the socket to the port number so that the kernel
 //can match an incoming packet on a port to the proper process
 if((status=bind(sockfd, servinfo->ai_addr, servinfo->ai_addrlen))==-1)
 {
   perror("bind");
   exit(1);
 }
 //when done, release dynamically allocated memory
 freeaddrinfo(servinfo);

 if(listen(sockfd,1)==-1)
 {
   perror("listen");
   exit(1);
 }

 fprintf(fp, "Blocking, waiting for client to connect\n");
 fflush(fp);

 struct sockaddr_in client_addr;
 socklen_t clientSize=sizeof(client_addr);
 int new_sockfd;
 if((new_sockfd=accept(sockfd, (struct sockaddr*) &client_addr, &clientSize))==-1)
 {
   perror("accept");
   exit(1);
 }
 fprintf(fp, "Connected to client.\n");
 fprintf(fp, "returning Read Socket fd for Socket protocol IPC as %d.\n",new_sockfd);
 return new_sockfd;
}


int get_Write_Socket_fd(FILE * fp, string ip_address){
  int sockfd; //file descriptor for the socket
  int status; //for error checking

  fprintf(fp, "Attempting to get Write Socket fd for Socket protocol IPC.\n");
  fflush(fp);
  //change this # between 2000-65k before using
  const char* portno = PORT1;
  char * ip_cstr = new char[ip_address.length()+1];
  strcpy(ip_cstr, ip_address.c_str());

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints)); //zero out everything in structure
  hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
  hints.ai_socktype=SOCK_STREAM; // TCP stream sockets

  struct addrinfo *servinfo;
  //instead of "localhost", it could by any domain name
  if((status=getaddrinfo(ip_cstr, portno, &hints, &servinfo))==-1)
  {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
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
  delete [] ip_cstr;

  fprintf(fp, "Connected to server.\n");
  fprintf(fp, "returning Write Socket fd for Socket protocol IPC as %d.\n", sockfd);
  return sockfd;
}

void long_sleep(){
  sleep(30);
}

void send_Socket_Player(char PLR_MASK){
  char protocol_type = G_SOCKPLR;
  protocol_type |= PLR_MASK;

  WRITE <char>(write_fd, &protocol_type, sizeof(char));
  return;
}

void socket_Player_signal_handler(int){
  char plr_mask = getActivePlayersMask();
  send_Socket_Player(plr_mask);
}

void send_Socket_Message(char PLR_MASK, string msg){
  int msg_length = msg.length() + 1;
  char protocol_type = G_SOCKMSG;
  char *cstr = new char[msg_length];

  strcpy(cstr, msg.c_str());
  protocol_type |= PLR_MASK;

  printf("in client : msglen %d - msg - %s\n",msg_length, cstr);

  WRITE <char>(write_fd, &protocol_type, sizeof(char));
  WRITE <int>(write_fd, &msg_length, sizeof(int));
  WRITE <char>(write_fd, cstr, msg_length*sizeof(char));

  delete [] cstr;
}

string receiveMessagebyDaemon(mqd_t read_fd){
  int err;
  char msg[251]; //a char array for the message
  memset(msg, 0, 251); //zero it out (if necessary)
  string msg_str  = "";
  struct sigevent mq_notification_event;
  mq_notification_event.sigev_notify=SIGEV_SIGNAL;
  mq_notification_event.sigev_signo=SIGUSR2;
  mq_notify(read_fd, &mq_notification_event);

  while((err=mq_receive(read_fd, msg, 250, NULL))!=-1)
  {
      msg_str = msg;
    //call postNotice(msg) for your Map object;
    //cout << "Message received: " << msg << endl;
    memset(msg, 0, 251);//set all characters to '\0'
  }
  //we exit while-loop when mq_receive returns -1
  //if errno==EAGAIN that is normal: there is no message waiting
  if(errno!=EAGAIN)
  {
    perror("mq_receive");
  }
  return msg_str;

}

void socket_Message_signal_handler(int){
  char p_mask = G_SOCKMSG;
  string msg_str;

  for(int i = 0; i<5;i++){
    if (daemon_readqueue_fds[i] != -1){
        msg_str = receiveMessagebyDaemon(daemon_readqueue_fds[i]);

        if(msg_str != "" && i == 0){
          send_Socket_Message(p_mask | G_PLR0, msg_str);
        }
        if(msg_str != "" && i == 1){
          send_Socket_Message(p_mask | G_PLR1, msg_str);
        }
        if(msg_str != "" && i == 2){
          send_Socket_Message(p_mask | G_PLR2, msg_str);
        }
        if(msg_str != "" && i == 3){
          send_Socket_Message(p_mask | G_PLR3, msg_str);
        }
        if(msg_str != "" && i == 4){
          send_Socket_Message(p_mask | G_PLR4, msg_str);
        }
    }
  } // end of for

}

void send_Socket_Map(vector<pair<short,char> > mapChangesVector){
  char protocol_type = 0, changedMapValue;
  short changedMapId;
  int Vector_size = mapChangesVector.size();

  WRITE <char>(write_fd, &protocol_type, sizeof(char));

  WRITE <int>(write_fd, &Vector_size, sizeof(int));

  for(int i = 0; i<Vector_size; i++){
    changedMapId = mapChangesVector[i].first;
    changedMapValue = mapChangesVector[i].second;

    WRITE <short>(write_fd, &changedMapId, sizeof(short));
    WRITE <char>(write_fd, &changedMapValue, sizeof(char));
  }

}

void socket_Map_signal_handler(int){
  int rows, cols;
  vector<pair<short,char> > mapChangesVector;
  rows = mbp->rows;
  cols = mbp->cols;

  for (int i=0; i < rows*cols; i++){
      if(initial_map[i] !=  mbp->map[i]){
        mapChangesVector.push_back(make_pair(i, mbp->map[i] ));
        initial_map[i] =  mbp->map[i];
      }
  }
  if(mapChangesVector.size() > 0){
    send_Socket_Map(mapChangesVector);
  }

}

void process_Socket_Message(FILE *fp, char active_plr_mask){

  int msg_length = 0, toPlayerInt;
  char msg_cstring[100];

  READ <int>(read_fd, &msg_length, sizeof(int));
  READ <char>(read_fd, msg_cstring, msg_length*sizeof(char));

  fprintf(fp, "in process_Socket_Message : msglen %d - msg - %s\n",msg_length, msg_cstring);
  string msg(msg_cstring);

  for(int i = 0; i < 5;i++ ){
    if(i==0 &&  (active_plr_mask & G_PLR0) ){
      fprintf(fp, "sending Message to Player %d", i+1);
      sendMsgFromDaemonToPlayer(i, msg);
    }
    if(i==1 &&  (active_plr_mask & G_PLR1) ){
      fprintf(fp, "sending Message to Player %d", i+1);
      sendMsgFromDaemonToPlayer(i, msg);
    }
    if(i==2 &&  (active_plr_mask & G_PLR2) ){
      fprintf(fp, "sending Message to Player %d", i+1);
      sendMsgFromDaemonToPlayer(i, msg);
    }
    if(i==3 &&  (active_plr_mask & G_PLR3) ){
      fprintf(fp, "sending Message to Player %d", i+1);
      sendMsgFromDaemonToPlayer(i, msg);
    }
    if(i==4 &&  (active_plr_mask & G_PLR4) ){
      fprintf(fp, "sending Message to Player %d", i+1);
      sendMsgFromDaemonToPlayer(i, msg);
    }
  }//end of for loop

}

void process_Socket_Player(FILE *fp, char protocol_type){
  fprintf(fp, "in process_Socket_Player %d\n",protocol_type);

  sem_wait(shm_sem);
  for(int i = 0; i < 5;i++ ){
      if(i==0 &&  (protocol_type & G_PLR0) && mbp->player_pids[i] == -1){ // p1 joined
        fprintf(fp, "player 1 found\n");
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);
      }
      if(i==0 &&  !(protocol_type & G_PLR0) && mbp->player_pids[i] != -1){ // p1 left
        fprintf(fp, "player 1 Left\n");
        mbp->player_pids[i] = -1;

        mq_close(daemon_readqueue_fds[i]);
        mq_unlink(daemon_mq_names[i].c_str());
        daemon_readqueue_fds[i] = -1;
      }


      if ( i==1 && (protocol_type & G_PLR1) && mbp->player_pids[i] == -1){ // p2 joined
        fprintf(fp, "player 2 found\n");
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);

      }
      if(i==1 &&  !(protocol_type & G_PLR1) && mbp->player_pids[i] != -1){ // p2 left
        fprintf(fp, "player 2 Left\n");
        mbp->player_pids[i] = -1;

        mq_close(daemon_readqueue_fds[i]);
        mq_unlink(daemon_mq_names[i].c_str());
        daemon_readqueue_fds[i] = -1;
      }

      if ( i==2 && (protocol_type & G_PLR2) && mbp->player_pids[i] == -1){ // p3 joined
        fprintf(fp, "player 3 found\n");
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);

      }
      if(i==2 &&  !(protocol_type & G_PLR2) && mbp->player_pids[i] != -1){ // p3 left
        fprintf(fp, "player 3 Left\n");
        mbp->player_pids[i] = -1;

        mq_close(daemon_readqueue_fds[i]);
        mq_unlink(daemon_mq_names[i].c_str());
        daemon_readqueue_fds[i] = -1;
      }

      if ( i==3 && (protocol_type & G_PLR3) && mbp->player_pids[i] == -1){ // p4 joined
        fprintf(fp, "player 4 found\n");
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);

      }
      if(i==3 &&  !(protocol_type & G_PLR3) && mbp->player_pids[i] != -1){ // p4 left
        fprintf(fp, "player 4 Left\n");
        mbp->player_pids[i] = -1;

        mq_close(daemon_readqueue_fds[i]);
        mq_unlink(daemon_mq_names[i].c_str());
        daemon_readqueue_fds[i] = -1;
      }

      if ( i==4 && (protocol_type & G_PLR4) && mbp->player_pids[i] == -1){
        fprintf(fp, "player 5 found\n");
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);

      }
      if(i==4 &&  !(protocol_type & G_PLR4) && mbp->player_pids[i] != -1){ // p5 left
        fprintf(fp, "player 5 Left\n");
        mbp->player_pids[i] = -1;

        mq_close(daemon_readqueue_fds[i]);
        mq_unlink(daemon_mq_names[i].c_str());
        daemon_readqueue_fds[i] = -1;
      }

    }// end of for loop
  sem_post(shm_sem);

  if(protocol_type == -128 ){
    fprintf(fp, "No active players left in Game.\n");
    fflush(fp);
    //socket_break_Read_signal_handler();
    char protocol_type1 = 1;
    WRITE <char>(write_fd, &protocol_type1, sizeof(char));

  }

}

void process_Socket_Map(FILE *fp, char protocol_type){
  int Vector_size;
  char changedMapValue;
  short changedMapId;

  vector<pair<short,char> > mapChangesVector;

  READ <int>(read_fd, &Vector_size, sizeof(int));
  fprintf(fp, "in server : Vector_size %d\n",Vector_size);

  for (int i=0; i<Vector_size; i++){

    READ <short>(read_fd, &changedMapId, sizeof(short));
    READ <char>(read_fd, &changedMapValue, sizeof(char));

    mapChangesVector.push_back(make_pair(changedMapId,changedMapValue));
  }

  sem_wait(shm_sem);
  for(int i = 0; i<Vector_size; i++){
    mbp->map[mapChangesVector[i].first] = mapChangesVector[i].second;
  }
  sem_post(shm_sem);
  sendSignalToActivePlayersOnNode(mbp, SIGUSR1);

}


void socket_Communication_Handler(FILE *fp){
  char protocol_type = ' ' ; int return_code;

  fprintf(fp, "Attempting to start Socket communications protocol\n");
  return_code = READ <char>(read_fd, &protocol_type, sizeof(char));
  if(return_code == -1){
    fprintf(fp, "Attempting to start Socket returned error Code -1.\n");
    fflush(fp);
  }

  if (protocol_type&G_SOCKPLR ){
    fprintf(fp, "read protocol_type - Socket_Player from client.\n");
    process_Socket_Player(fp, protocol_type);

  }
  else if (protocol_type&G_SOCKMSG ){
    fprintf(fp, "read protocol_type - Socket_Message from client.\n");
    process_Socket_Message(fp, protocol_type);

  }
  else if (protocol_type == 0 ){
    fprintf(fp, "read protocol_type - Socket_Map from client.\n");
    process_Socket_Map(fp, protocol_type);

  }
  else if (protocol_type == 1 ){
      fprintf(fp, "read protocol_type - break for Blocking read.\n");
      char protocol_type1 = 2;
      WRITE <char>(write_fd, &protocol_type1, sizeof(char));

      fprintf(fp,"All done in demon, Killing daemon with pid -%d now.\n", getpid());
      close(write_fd);
      close(read_fd);
      fclose(fp);
      exit(0);
  }
  else if (protocol_type == 2 ){
    fprintf(fp, "read protocol_type - break for Blocking read.\n");

    fprintf(fp,"Cleaning up SHM and semaphore in Daemon.\n", getpid());
    fflush(fp);

    shm_unlink(SHM_NAME);
    sem_close(shm_sem);
    sem_unlink(SHM_SM_NAME);

    fprintf(fp,"All done in demon, Killing daemon with pid -%d now.\n", getpid());
    close(write_fd);
    close(read_fd);
    fclose(fp);
    exit(0);
  }
}

void setUpDaemonSignalHandlers(){

  struct sigaction exit_action;
  exit_action.sa_handler = socket_Player_signal_handler;
  exit_action.sa_flags=0;
  sigemptyset(&exit_action.sa_mask);
  sigaction(SIGHUP, &exit_action, NULL);

  struct sigaction my_sig_handler;
  my_sig_handler.sa_handler = socket_Map_signal_handler;
  sigemptyset(&my_sig_handler.sa_mask);
  my_sig_handler.sa_flags=0;
  sigaction(SIGUSR1, &my_sig_handler, NULL);

  struct sigaction action_to_take;
  //action_to_take.sa_handler=read_message;
  action_to_take.sa_handler=socket_Message_signal_handler;
  sigemptyset(&action_to_take.sa_mask);
  action_to_take.sa_flags=0;
  sigaction(SIGUSR2, &action_to_take, NULL);

}

void init_Server_Daemon(string ip_address){
  int rows, cols;
  FILE * fp = fopen (SERVER_LOG_DIR, "w+");
  fprintf(fp, "Logging info from daemon with pid : %d\n", getpid());
  fflush(fp);
  setUpDaemonSignalHandlers();
  sem_wait(shm_sem);
  mbp = readSharedMemory();
  rows = mbp->rows;
  cols = mbp->cols;


  for (int i=0; i < rows*cols; i++)
      initial_map[i] =  mbp->map[i];
  initial_map[rows*cols] = '\0';

  fprintf(fp, "readSharedMemory done. rows - %d cols - %d\n", rows, cols);
  fflush(fp);

  daemon_readqueue_fds[0] = -1;daemon_readqueue_fds[1] = -1;daemon_readqueue_fds[2] = -1;daemon_readqueue_fds[3] = -1;daemon_readqueue_fds[4] = -1;
  mbp->daemonID = getpid();
  sem_post(shm_sem);

  perform_IPC_with_client(fp);
  char active_plr_mask = getActivePlayersMask();
  WRITE <char>(write_fd, &active_plr_mask, sizeof(char));


  read_fd = get_Read_Socket_fd(fp);


  fflush(fp);


  fprintf(fp, "Entering infinite loop with blocking read now.\n");
  fflush(fp);

  while(1){
    socket_Communication_Handler(fp); // takes care of read_fd and exit
    //sleep(1);
  }
}

void intialize_active_plr_client(char active_plr_mask){
    for(int i = 0; i < 5;i++ ){
      if(i==0 &&  (active_plr_mask & G_PLR0) && mbp->player_pids[i] == -1){
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);
      }
      if ( i==1 && (active_plr_mask & G_PLR1) && mbp->player_pids[i] == -1){
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);
      }
      if ( i==2 && (active_plr_mask & G_PLR2) && mbp->player_pids[i] == -1){
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);
      }
      if ( i==3 && (active_plr_mask & G_PLR3) && mbp->player_pids[i] == -1){
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);
      }
      if ( i==4 && (active_plr_mask & G_PLR4) && mbp->player_pids[i] == -1){
        mbp->player_pids[i] = getpid();
        initializeMsgQueueInDaemon(i);
      }
    }

}

void init_Client_Daemon(string ip_address){
  int rows, cols, goldCount, fd;
  daemon_readqueue_fds[0] = -1;daemon_readqueue_fds[1] = -1;daemon_readqueue_fds[2] = -1;daemon_readqueue_fds[3] = -1;daemon_readqueue_fds[4] = -1;

  FILE * fp = fopen (CLIENT_LOG_DIR, "w+");
  fprintf(fp, "Logging info from daemon with pid : %d\n", getpid());
  fprintf(fp, "Received IP Address as : %s\n", ip_address.c_str());
  fprintf(fp,"Attempting ClientDaemon Initialize IPC now.\n");


  vector< char >  mbpVector = perform_IPC_with_server(fp, rows, cols, ip_address);
  fprintf(fp, "Reading from server IPC for mbpVector done. rows - %d cols - %d\n", rows,cols);

  char active_plr_mask;
  READ <char>(read_fd, &active_plr_mask, sizeof(char));
  fprintf(fp, "Reading from server IPC for Active players done - %d\n",active_plr_mask );
  fprintf(fp, "Completed Client Daemon Initialize IPC. \n");
  fflush(fp);

  shm_sem=sem_open(SHM_SM_NAME,O_CREAT,S_IRUSR|S_IWUSR,0);

  mbp = initSharedMemory(rows, cols);

  //fprintf(fp,"checkpoint 2.\n");
  //fflush(fp);

  mbp->rows = rows;
  mbp->cols = cols;
  mbp->player_pids[0] = -1; mbp->player_pids[1] = -1;mbp->player_pids[2] = -1;mbp->player_pids[3] = -1;mbp->player_pids[4] = -1;


  intialize_active_plr_client(active_plr_mask);

  for (int i=0; i < rows*cols; i++)
      mbp->map[i] = mbpVector[i];

  write_fd = get_Write_Socket_fd(fp, ip_address);
  mbp->daemonID = getpid();
  sem_post(shm_sem);
  setUpDaemonSignalHandlers();


  fprintf(fp, "Entering infinite loop with blocking read now.\n");
  fflush(fp);

  while(1){
    socket_Communication_Handler(fp); // takes care of read_fd and exit
    //sleep(1);
  }
}


void invoke_in_Daemon( void (*f) (string)  ,string ip_address){

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
  open("/dev/null", O_RDWR); //fd 2


  umask(0);
  chdir("/");
  (*f)(ip_address);
}

void refreshMap(int){
  if(gameMap != NULL){
    (*gameMap).drawMap();
  }
}

void sendSignalToActivePlayers(mapboard * mbp, int signal_enum){
  for(int i=0; i<5; i++){
    if(mbp->player_pids[i] != -1 && i != getPlayerFromMask(thisPlayer)
    && mbp->daemonID != -1 && mbp->player_pids[i] != mbp->daemonID){ // to other active players only
      kill(mbp->player_pids[i], signal_enum);
    }
  }
}

void sendSignalToActivePlayersOnNode(mapboard * mbp, int signal_enum){
  for(int i=0; i<5; i++){
    if(mbp->player_pids[i] != -1
      && mbp->daemonID != -1
       && mbp->player_pids[i] != mbp->daemonID ){// called by daemon to others
      kill(mbp->player_pids[i], signal_enum);
    }
  }
}

void sendSignalToDaemon(mapboard * mbp, int signal_enum){
    if(mbp->daemonID != -1 ){
       kill(mbp->daemonID, signal_enum);}
}

void handleGameExit(int){
  // clean ups all game's stuff when exiting forceful or otherwise
  delete gameMap;

  sem_wait(shm_sem);
  mbp->map[thisPlayerLoc] &= ~thisPlayer;
  mbp->player_pids[getPlayerFromMask(thisPlayer)] = -1;
  sem_post(shm_sem);
  sendSignalToActivePlayers(mbp, SIGUSR1);
  sendSignalToDaemon(mbp, SIGHUP);
  sendSignalToDaemon(mbp, SIGUSR1);

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

void initializeMsgQueueInDaemon(int thisPlayerNumber){
  daemon_mq_names[thisPlayerNumber] = MSG_QUEUE_PREFIX;
  daemon_mq_names[thisPlayerNumber] = daemon_mq_names[thisPlayerNumber] + itos_utility(thisPlayerNumber);
  struct mq_attr mq_attributes;
  mq_attributes.mq_flags=0;
  mq_attributes.mq_maxmsg=10;
  mq_attributes.mq_msgsize=120;

  if((daemon_readqueue_fds[thisPlayerNumber] = mq_open(daemon_mq_names[thisPlayerNumber].c_str(), O_RDONLY|O_CREAT|O_EXCL|O_NONBLOCK,
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

void sendMsgFromDaemonToPlayer(int toPlayerInt, string msg){
  mqd_t writequeue_fd;
  string msg_queue_name = MSG_QUEUE_PREFIX, msg_queue_suffix;

  msg_queue_suffix = itos_utility(toPlayerInt);
  msg_queue_name = msg_queue_name + msg_queue_suffix;



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
  string msg = "Player #" + itos_utility(getPlayerFromMask(thisPlayer)+1) + " won!";

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
  action_to_take.sa_handler=receiveMessage;
  sigemptyset(&action_to_take.sa_mask);
  action_to_take.sa_flags=0;
  sigaction(SIGUSR2, &action_to_take, NULL);

}

bool hasNonLocalPlayers(){
  int d_pid = mbp->daemonID;
  if (mbp->player_pids[0] == d_pid || mbp->player_pids[1] == d_pid || mbp->player_pids[2] == d_pid || mbp->player_pids[3] == d_pid || mbp->player_pids[4] == d_pid)
    return true;
  else
  return false;

}
int main(int argc, char *argv[])
{

  int rows, cols, goldCount, keyInput = 0, currPlaying = -1, fd;
  bool thisPlayerFoundGold = false , thisQuitGameloop = false, inServerNode = false, inClientNode = false;
  char * mapFile = "mymap.txt",* daemon_server_ip;
  string ip_address = "";
  const char * notice;
  unsigned char * mp; //map pointer
  vector<vector< char > > mapVector;

  if(IS_CLIENT == -1 && argc == 2){ // ip to connect daemon server
    ip_address = argv[1];
    inClientNode = true;
    shm_sem = sem_open(SHM_SM_NAME ,O_RDWR,S_IRUSR|S_IWUSR,1);
    if(shm_sem == SEM_FAILED)//     semaphore and shm not initilized on client;
    {
      invoke_in_Daemon(init_Client_Daemon, ip_address);
      // wait loop until shm is inited by client daemon
      while(1){ // loop until mbp is updated
          sleep(1);
          if ( (fd = shm_open(SHM_NAME, O_RDONLY, S_IRUSR|S_IWUSR)) == -1)
            cout<<"shm not set"<<endl;
          else{
            cout<<"shm set"<<endl;
            break;
          }
      }
    }
  }
  else if(IS_CLIENT == -1 && argc == 1){ // ip to connect daemon server
    inServerNode = true;
  }

  shm_sem = sem_open(SHM_SM_NAME ,O_RDWR,S_IRUSR|S_IWUSR,1);
  if(shm_sem == SEM_FAILED)//     //cout<<"first player"<<endl;
  {
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
       invoke_in_Daemon(init_Server_Daemon, ip_address);
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
     cout<<"shm daemonid "<<mbp->daemonID<<endl;
     sem_post(shm_sem);

     if(inClientNode){
       sendSignalToDaemon(mbp, SIGHUP);
       sendSignalToDaemon(mbp, SIGUSR1);
    }
    if(inServerNode && hasNonLocalPlayers()){
      sendSignalToDaemon(mbp, SIGHUP);
      sendSignalToDaemon(mbp, SIGUSR1);
    }
    if(inServerNode && !hasNonLocalPlayers()){
      for (int i=0; i < rows*cols; i++)
          initial_map[i] =  mbp->map[i];
      initial_map[rows*cols] = '\0';
    }

   }

   /*
   cout<<"all done cleaning up shm now"<<endl;
   handleGameExit(0);
   return 0;
   */

   try
   {
     gameMap = new Map(reinterpret_cast<const unsigned char*>(mbp->map),rows,cols);

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
           sendSignalToDaemon(mbp, SIGUSR1);
           (*gameMap).postNotice(notice);
           (*gameMap).drawMap();
         }
         else if(notice == YOU_WON_MESSAGE ){
           sendSignalToActivePlayers(mbp, SIGUSR1);
           sendSignalToDaemon(mbp, SIGUSR1);
           // broadcast winning msg
           sendWinningMsgBroadcastToPlayers(thisPlayer);
           sendSignalToDaemon(mbp, SIGUSR2);
           (*gameMap).postNotice(notice);
           (*gameMap).drawMap();
         }
         else if(notice == EMPTY_MESSAGE_PLAYER_MOVED ){
           sendSignalToActivePlayers(mbp, SIGUSR1);
           sendSignalToDaemon(mbp, SIGUSR1);
           (*gameMap).drawMap();
         }


         if(thisQuitGameloop)
          break;

       }
       else if(keyInput == 109){ // key m for message
         int toPlayerInt = getPlayerFromMask((*gameMap).getPlayer(getActivePlayersMask()) );
         string msg = (*gameMap).getMessage();
         sendMsgToPlayer(thisPlayer, toPlayerInt, msg, true);
         sendSignalToDaemon(mbp, SIGUSR2);
       }
       else if(keyInput == 98){ // key b for broadcast
         string msg = (*gameMap).getMessage();
         sendMsgBroadcastToPlayers(thisPlayer, msg);
         sendSignalToDaemon(mbp, SIGUSR2);
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
