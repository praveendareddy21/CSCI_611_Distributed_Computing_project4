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
