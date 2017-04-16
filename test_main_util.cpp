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

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints)); //zero out everything in structure
  hints.ai_family = AF_UNSPEC; //don't care. Either IPv4 or IPv6
  hints.ai_socktype=SOCK_STREAM; // TCP stream sockets

  struct addrinfo *servinfo;
  //instead of "localhost", it could by any domain name
  if((status=getaddrinfo("localhost", portno, &hints, &servinfo))==-1)
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
  close(sockfd);
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
  close(new_sockfd);
}

int get_Read_Socket_fd(){
  int sockfd; //file descriptor for the socket
 int status; //for error checking


 //change this # between 2000-65k before using
 const char* portno = PORT;
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

 printf("Blocking, waiting for client to connect\n");

 struct sockaddr_in client_addr;
 socklen_t clientSize=sizeof(client_addr);
 int new_sockfd;
 if((new_sockfd=accept(sockfd, (struct sockaddr*) &client_addr, &clientSize))==-1)
 {
   perror("accept");
   exit(1);
 }
 printf("Connected to client.\n");
 return new_sockfd;
}

int get_Write_Socket_fd(){
  int sockfd; //file descriptor for the socket
  int status; //for error checking

  //change this # between 2000-65k before using
  const char* portno = PORT;

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
