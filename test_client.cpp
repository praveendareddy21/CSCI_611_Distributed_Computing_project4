//Socket client
#include <sys/types.h>
#include<sys/socket.h>
#include<unistd.h> //for read/write
#include<netdb.h>
#include<string.h> //for memset
#include<stdio.h> //for fprintf, stderr, etc.
#include<stdlib.h> //for exit
#include<errno.h>
#include"fancyRW.h"
#include"goldchase.h"
#include<iostream>
#include<string>
#include<vector>



using namespace std;

int write_fd = -1;
int read_fd = -1;

void send_Socket_Message(char PLR_MASK, string msg);
void send_Socket_Player(char PLR_MASK);
void send_Socket_Map(vector< pair<short, char> > mapChangesVector);

int get_Write_Socket_fd(){
  int sockfd; //file descriptor for the socket
  int status; //for error checking

  //change this # between 2000-65k before using
  const char* portno="42424";

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints)); //zero out everything in structure
  hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
  hints.ai_socktype=SOCK_STREAM; // TCP stream sockets

  struct addrinfo *servinfo;
  //instead of "localhost", it could by any domain name
  if((status=getaddrinfo("localhost", portno, &hints, &servinfo))==-1)
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

  printf("Connected to server.\n");
  return sockfd;
}


void send_Socket_Player(char PLR_MASK){
  char protocol_type = G_SOCKPLR;
  protocol_type |= PLR_MASK;

  WRITE <char>(write_fd, &protocol_type, sizeof(char));
  return;
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

vector<pair<short,char> >  getMapChangeVector(){
  vector<pair<short,char> > mapChangesVector;

  mapChangesVector.push_back(make_pair(0,'A'));
  mapChangesVector.push_back(make_pair(1,'B'));
  mapChangesVector.push_back(make_pair(2,'C'));
  mapChangesVector.push_back(make_pair(3,'D'));

return mapChangesVector;
}

int main()
{
  write_fd = get_Write_Socket_fd();
  char player_mask = ' ';


  send_Socket_Player(player_mask);

  send_Socket_Message(player_mask, "Hello World!!");

  vector<pair<short,char> > mapChangesVector = getMapChangeVector();
  send_Socket_Map(mapChangesVector);


  char protocol_type = 1;
  WRITE <char>(write_fd, &protocol_type, sizeof(char));


  printf("Exiting socket connection.\n");
  close(write_fd);
  return 1;
}
