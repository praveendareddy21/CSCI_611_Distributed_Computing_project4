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
#define PORT "42425"  //change this # between 2000-65k before using
#define PORT1 "42426"  //change this # between 2000-65k before using
#endif


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


Map * gameMap = NULL;
mqd_t readqueue_fd; //message queue file descriptor
string mq_name;
sem_t* shm_sem;
mapboard * mbp = NULL;
int thisPlayer = 0, thisPlayerLoc= 0;
char initial_map[2100];
int write_fd = -1;
int read_fd = -1;

//####################################################### test map util #######################################

#include"test_main_util.cpp"

//###########################################################################################################
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

void socket_Message_signal_handler(int){

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

vector<pair<short,char> >  getMapChangeVector(){
  vector<pair<short,char> > mapChangesVector;

  mapChangesVector.push_back(make_pair(0,'A'));
  mapChangesVector.push_back(make_pair(1,'B'));
  mapChangesVector.push_back(make_pair(2,'C'));
  mapChangesVector.push_back(make_pair(3,'D'));

return mapChangesVector;
}

void process_Socket_Message(FILE *fp, char protocol_type){

  int msg_length = 0;
  char msg_cstring[100];

  READ <int>(read_fd, &msg_length, sizeof(int));
  READ <char>(read_fd, msg_cstring, msg_length*sizeof(char));

  fprintf(fp, "in server : msglen %d - msg - %s\n",msg_length, msg_cstring);

}

void process_Socket_Player(FILE *fp, char protocol_type){
  fprintf(fp, "in process_Socket_Player %d\n",protocol_type);

  sem_wait(shm_sem);
  for(int i = 0; i < 5;i++ ){
    if(i==0 &&  (protocol_type & G_PLR0) && mbp->player_pids[i] == -1){ // p1 joined
      fprintf(fp, "player 1 found\n");
      mbp->player_pids[i] = getpid();

    }
    if(i==0 &&  (protocol_type & G_PLR0 == 0) && mbp->player_pids[i] != -1){ // p1 left
      fprintf(fp, "player 1 Left\n");
      mbp->player_pids[i] = -1;

    }


    if ( i==1 && (protocol_type & G_PLR1) && mbp->player_pids[i] == -1){ // p2 joined
      fprintf(fp, "player 2 found\n");
      mbp->player_pids[i] = getpid();

    }
    if(i==1 &&  (protocol_type & G_PLR1 == 0) && mbp->player_pids[i] != -1){ // p2 left
      fprintf(fp, "player 2 Left\n");
      mbp->player_pids[i] = -1;

    }

    if ( i==2 && (protocol_type & G_PLR2) && mbp->player_pids[i] == -1){ // p3 joined
      fprintf(fp, "player 3 found\n");
      mbp->player_pids[i] = getpid();

    }
    if(i==2 &&  (protocol_type & G_PLR2 == 0) && mbp->player_pids[i] != -1){ // p3 left
      fprintf(fp, "player 3 Left\n");
      mbp->player_pids[i] = -1;

    }

    if ( i==3 && (protocol_type & G_PLR3) && mbp->player_pids[i] == -1){ // p4 joined
      fprintf(fp, "player 4 found\n");
      mbp->player_pids[i] = getpid();

    }
    if(i==3 &&  (protocol_type & G_PLR3 == 0) && mbp->player_pids[i] != -1){ // p4 left
      fprintf(fp, "player 4 Left\n");
      mbp->player_pids[i] = -1;

    }

    if ( i==4 && (protocol_type & G_PLR4) && mbp->player_pids[i] == -1){
      fprintf(fp, "player 5 found\n");
      mbp->player_pids[i] = getpid();

    }
    if(i==4 &&  (protocol_type & G_PLR4 == 0) && mbp->player_pids[i] != -1){ // p5 left
      fprintf(fp, "player 5 Left\n");
      mbp->player_pids[i] = -1;

    }

  }// end of for loop
  sem_post(shm_sem);

  //sendSignalToActivePlayersOnNode(mbp, SIGUSR1);

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

  /*
  fprintf(fp, "in server : Vector_size %d\n",mapChangesVector.size());
  fprintf(fp, "in server : printing mapChangesVector\n");

  for(int i = 0; i<Vector_size; i++){
    changedMapId = mapChangesVector[i].first;
    changedMapValue = mapChangesVector[i].second;
    fprintf(fp, "Id : %d --- Value : %c\n", changedMapId, changedMapValue);
  }
  */

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
  else if (protocol_type == 1 ){// TODO
    if(IS_CLIENT){
      fprintf(fp, "read protocol_type - break for Blocking read.\n");
      char protocol_type1 = 1;
      WRITE <char>(write_fd, &protocol_type1, sizeof(char));

      fprintf(fp,"All done in demon, Killing daemon with pid -%d now.\n", getpid());
      close(write_fd);
      close(read_fd);
      fclose(fp);
      exit(0);
    }
    else{
      fprintf(fp, "read protocol_type - break for Blocking read.\n");
      fprintf(fp,"All done in demon, Killing daemon with pid -%d now.\n", getpid());
      close(write_fd);
      close(read_fd);
      fclose(fp);
      exit(0);
    }
  }
}

void socket_break_Read_signal_handler(int){
  char protocol_type1 = 1;
  WRITE <char>(write_fd, &protocol_type1, sizeof(char));
}

void setUpDaemonSignalHandlers(){

  struct sigaction break_read; //TODO
  break_read.sa_handler = socket_break_Read_signal_handler;
  break_read.sa_flags=0;
  sigemptyset(&break_read.sa_mask);
  sigaction(SIGINT, &break_read, NULL);

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
  FILE * fp = fopen ("/home/red/611_project/CSCI_611_Distributed_Computing_project4/gchase_server.log", "w+");
  fprintf(fp, "Logging info from daemon with pid : %d\n", getpid());
  fflush(fp);

  sem_wait(shm_sem);
  mbp = readSharedMemory();
  rows = mbp->rows;
  cols = mbp->cols;


  for (int i=0; i < rows*cols; i++)
      initial_map[i] =  mbp->map[i];
  initial_map[rows*cols] = '\0';

  fprintf(fp, "readSharedMemory done. rows - %d cols - %d\n", rows, cols);
  fflush(fp);

  mbp->daemonID = getpid();
  sem_post(shm_sem);

  perform_IPC_with_client(fp);
  char active_plr_mask = getActivePlayersMask();
  WRITE <char>(write_fd, &active_plr_mask, sizeof(char));


  read_fd = get_Read_Socket_fd(fp);

  setUpDaemonSignalHandlers();
  fflush(fp);


  fprintf(fp, "Entering infinite loop with blocking read now.\n");
  fflush(fp);

  while(1){
    socket_Communication_Handler(fp); // takes care of read_fd and exit
    sleep(1);
  }

  // control never reaches here
  fprintf(fp,"All done in Server demon, Killing daemon with pid -%d now.\n", getpid());
  fclose(fp);
  exit(0);

}

void intialize_active_plr_client(FILE *fp, char protocol_type){
  fprintf(fp, "in intialize_active_plr_client %d\n",protocol_type);

  for(int i = 0; i < 5;i++ ){
    if(i==0 &&  (protocol_type & G_PLR0) && mbp->player_pids[i] == -1){
      fprintf(fp, "player 1 found\n");
      mbp->player_pids[i] = getpid();

    }
    if ( i==1 && (protocol_type & G_PLR1) && mbp->player_pids[i] == -1){
      fprintf(fp, "player 2 found\n");
      mbp->player_pids[i] = getpid();

    }
    if ( i==2 && (protocol_type & G_PLR2) && mbp->player_pids[i] == -1){
      fprintf(fp, "player 3 found\n");
      mbp->player_pids[i] = getpid();

    }
    if ( i==3 && (protocol_type & G_PLR3) && mbp->player_pids[i] == -1){
      fprintf(fp, "player 4 found\n");
      mbp->player_pids[i] = getpid();

    }
    if ( i==4 && (protocol_type & G_PLR4) && mbp->player_pids[i] == -1){
      fprintf(fp, "player 5 found\n");
      mbp->player_pids[i] = getpid();

    }
  }// end of for loop

}

void init_Client_Daemon(string ip_address){
  int rows, cols, goldCount, fd;

  FILE * fp = fopen ("/home/red/611_project/CSCI_611_Distributed_Computing_project4/gchase_client.log", "w+");
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


  for(int i = 0; i < 5;i++ ){
  if(i==0 &&  (active_plr_mask & G_PLR0) && mbp->player_pids[i] == -1){
    mbp->player_pids[i] = getpid();
  }
  if ( i==1 && (active_plr_mask & G_PLR1) && mbp->player_pids[i] == -1){
    mbp->player_pids[i] = getpid();
  }
  if ( i==2 && (active_plr_mask & G_PLR2) && mbp->player_pids[i] == -1){
    mbp->player_pids[i] = getpid();
  }
  if ( i==3 && (active_plr_mask & G_PLR3) && mbp->player_pids[i] == -1){
    mbp->player_pids[i] = getpid();
  }
  if ( i==4 && (active_plr_mask & G_PLR4) && mbp->player_pids[i] == -1){
    mbp->player_pids[i] = getpid();
  }
}

  for (int i=0; i < rows*cols; i++)
      mbp->map[i] = mbpVector[i];

  write_fd = get_Write_Socket_fd(fp);
  mbp->daemonID = getpid();
  sem_post(shm_sem);
  setUpDaemonSignalHandlers();


  fprintf(fp, "Entering infinite loop with blocking read now.\n");
  fflush(fp);

  while(1){
    socket_Communication_Handler(fp); // takes care of read_fd and exit
    sleep(1);
  }

  // control never reaches here
  fprintf(fp,"All done in Client demon, Killing daemon with pid -%d now.\n", getpid());
  fclose(fp);
  exit(0);
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
  open("/home/red/611_project/CSCI_611_Distributed_Computing_project4/g.log", O_RDWR); //fd 2


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

void handleGameExit(int){
  // clean ups all game's stuff when exiting forceful or otherwise
  delete gameMap;

  sem_wait(shm_sem);
  mbp->map[thisPlayerLoc] &= ~thisPlayer;
  mbp->player_pids[getPlayerFromMask(thisPlayer)] = -1;
  sem_post(shm_sem);
  sendSignalToActivePlayers(mbp, SIGUSR1);
  bool isBoardEmpty = isGameBoardEmpty(mbp); //TODO
  //mq_close(readqueue_fd);
  //mq_unlink(mq_name.c_str());


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
  //action_to_take.sa_handler=receiveMessage;
  //sigemptyset(&action_to_take.sa_mask);
  //action_to_take.sa_flags=0;
  //sigaction(SIGUSR2, &action_to_take, NULL);

}

void sendSignalToDaemon(mapboard * mbp, int signal_enum){
    if(mbp->daemonID != -1 ){
       kill(mbp->daemonID, signal_enum);}
}

int main(int argc, char *argv[])
{

  int rows, cols, goldCount, keyInput = 0, currPlaying = -1, fd;
  bool thisPlayerFoundGold = false , thisQuitGameloop = false, inServerNode = false, inClientNode = false;
  char * mapFile = "mymap.txt",* daemon_server_ip;
  string ip_address = "localpost";
  const char * notice;
  unsigned char * mp; //map pointer
  vector<vector< char > > mapVector;
  if(IS_CLIENT){ //argc == 2){ // ip to connect daemon server
    daemon_server_ip = argv[1];
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
      //sem_wait(shm_sem);

      //sem_post(shm_sem);

    }

  }else{
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

      if(inServerNode){
      sendSignalToDaemon(mbp, SIGHUP);
      sendSignalToDaemon(mbp, SIGUSR1);
    }
      if(inClientNode){
      sendSignalToDaemon(mbp, SIGHUP);
      sendSignalToDaemon(mbp, SIGUSR1);
    }
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
     //initializeMsgQueue(thisPlayer);
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

           sendSignalToDaemon(mbp, SIGUSR1);

         }


         if(thisQuitGameloop)
          break;

       }
       else if(keyInput == 109){ // key m for message
         int toPlayerInt = getPlayerFromMask((*gameMap).getPlayer(getActivePlayersMask()) );
         string msg = (*gameMap).getMessage();
         //sendMsgToPlayer(thisPlayer, toPlayerInt, msg, true);
       }
       else if(keyInput == 98){ // key b for broadcast
         string msg = (*gameMap).getMessage();
         sendSignalToDaemon(mbp, SIGINT);
         //sendMsgBroadcastToPlayers(thisPlayer, msg);
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
