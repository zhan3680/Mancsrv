#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>

#define MAXNAME 80  /* maximum permitted name size, not including '\0' */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* maximum permitted message size, not including \0 */
#define INFINITY (-1000) /*number of pebbles when a player connects but has not entered his/her full name yet*/

int port = 3000;//do not need to change this;
int listenfd;

struct player {
    int fd;
    char has_read[MAXNAME+1]; /*this stores what we have read from this user, if we found a newline in has_read, then copy this to name and add player to game (by setting has_a_name to 1)*/
    int room; /*number of bytes left in has_read*/
    int inbuf; /*number of bytes in has_read*/
    char *after; /*start point of next read on has_read*/
    char name[MAXNAME+1]; /*stores the name of the player*/
    int pits[NPITS+1];  // pits[0..NPITS-1] are the regular pits 
                        // pits[NPITS] is the end pit
    int has_a_name; /*1 means this player has entered a full name, 0 means otherwise*/
    int turn; /*1 means it's this player's turn, anything written by a player with turn=0 will be ignored*/
    int disconnected; /*1 means this player has already disconnected*/ 
    struct player *next; /*pointing to next player*/                                        
};                                                                                                                                              
struct player *playerlist = NULL;                                      
                                                                  
                                                                 
extern void parseargs(int argc, char **argv);          
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  
extern void broadcast(char *s);                   
extern int accept_new_player();  
extern void join_game(struct player *cur_player, int num_pebbles,int *num_players);     
extern int make_move(struct player *cur_palyer); /*important assumption: we assume the user will not type ctrl+D before fully entering his/her move*/                                      
extern int find_newline_permissively(const char *buf, int n);
extern int replicate_name(char *buf);
extern int read_move(struct player *cur_player);                                                   
extern void read_and_ignore_general_input(struct player *cur_player);                                
extern void broadcast_game_status(int option, struct player *cur_player);                
extern void delete_player(struct player *player);
extern void disconnect_player(struct player *disconnected_player, fd_set **all_fds, int *num_players);
extern void leave_only_connected_players(fd_set **all_fds, int *num_players, struct player *this_turn, int *turn_needs_update);
extern void write2(struct player *player, char *msg, int msg_length);
extern struct player* find_next_available_player(struct player *cur_player);
extern void close2(int fd);


int main(int argc, char **argv) {
    /*ignore the SIGPIPE signal*/
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        exit(-1);
    } 
    
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();

    /*initializations prepared for select*/
    int max_fd=listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);
    
    struct player *this_turn=NULL; /*used to keep track of the player who is supposed to make move in this round, in this round, msg sent by anyone else will be ignored*/
    int num_registered=0; /*number of clients in the linked list*/
    int num_players=0; /*number of players that has jointed the game*/
    int num_pebbles; /*pebble initialization for newly joined players*/
    
    int move_result; /*indicates whether the player whose turn it is enters a valid move index input, or disconnected*/
    struct player *player_ptr=NULL;
    fd_set *all_fds_addr; /*address of all_fds*/
    int turn_needs_update=0; /*1 means turn needs to be updated by leave_only function*/
    
    while (!game_is_over()) {
           fd_set all_players = all_fds; /*make a copy of all_fds*/
           int nready = select(max_fd + 1, &all_players, NULL, NULL, NULL);
           if (nready == -1) {
               perror("server: select");
               exit(1);
           }
	   
	   /*if the listenfd is ready, connect new_player*/
	   if (FD_ISSET(listenfd, &all_players)) {
	       int new_player_fd = accept_new_player();
               if (new_player_fd > max_fd) {
                   max_fd = new_player_fd; /*max_fd never decreases*/
               }
               FD_SET(new_player_fd, &all_fds);
	       num_registered++;
               printf("Accepted connection from %d-th candidates\n",num_registered);
           }
	   	   
	   /*else if a player fd is ready, then case a. it has no name, then get its name and allow him join to game; case b. it has a full name, then make his move*/
	   player_ptr=playerlist;
	   while(player_ptr!=NULL) {
	        
                if (FD_ISSET(player_ptr->fd, &all_players)) {
                    if(player_ptr->has_a_name==0&&player_ptr->disconnected==0){ /*case a*/	       
		       num_pebbles=compute_average_pebbles();
		       join_game(player_ptr,num_pebbles,&num_players);		       		       
		    }else if(player_ptr->has_a_name==1&&player_ptr->disconnected==0){/*case b*/ 
		       if(player_ptr->turn==1){
		          player_ptr->turn=0;                             
			  //printf("current turn belongs to %s\n",player_ptr->name);
			  this_turn=player_ptr;
			  turn_needs_update=1; 
		          
			    move_result=make_move(player_ptr);
			    if(move_result==0){ /*stop making move when make_move() returns 0*/
			       broadcast_game_status(0,NULL);
			    }else if(move_result==1){ /*ask the same player to make move again if make_move() returns 1*/
			       broadcast_game_status(0,NULL);			       
			       turn_needs_update=0;
			       player_ptr->turn=1; 
			          char message2[MAXMESSAGE+1]={'\0'};
			          if(sprintf(message2,"%s, your move please?\r\n",player_ptr->name)<0){
	                             perror("sprintf");
	                             exit(1);
                                  }
                                  write2(player_ptr,message2,strlen(message2)+1);
		               
			    }
			  			  
		       }else if(player_ptr->turn==0){
		          read_and_ignore_general_input(player_ptr);
                          char message8[MAXMESSAGE+1];
			  if(sprintf(message8,"Not your turn, please be patient\r\n")<0){
			     perror("sprintf");
		             exit(1);
			  }
			  write2(player_ptr,message8,strlen(message8)+1);			      
		       }
		    }
                }                 
		player_ptr=player_ptr->next;		
	   } //while(player_ptr!=NULL); a round of game is over, now clean up the board and update turn whenever necessary;
	   all_fds_addr=&all_fds;
	   leave_only_connected_players(&all_fds_addr, &num_players, this_turn, &turn_needs_update); 
	           
    }

    broadcast("Game over!\r\n");
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\n", p->name, points); 
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg);
    }

    return 0;
}


/*check if command line args are in the right format*/
void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
        case 'p':
            port = strtol(optarg, NULL, 0);  
            break;
        default:
            status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


/*create a listen fd to listen to future connect requests*/
void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, 
               (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}


/*calculate the average number of pebbles in the non_end pits of the players currently in the game (connected and joined)*/
int compute_average_pebbles() { 
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        if(p->has_a_name==1&&p->disconnected==0){
	   nplayers++;
           for (i = 0; i < NPITS; i++) {
                npebbles += p->pits[i];
           }
	}
    }
    if(nplayers==0){ 
       return 0;
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


/*check if the game is over*/
int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
       return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        if(p->has_a_name==1&&p->disconnected==0){ 
           int is_all_empty = 1;
           for (i = 0; i < NPITS; i++) {
                if (p->pits[i]) {
                    is_all_empty = 0;
                }
           }
           if (is_all_empty) {
               return 1;
           }
        }
    }
    return 0;
}


/* add new player that connects to server to the player list, but do not read its name, i.e. everyone does_not_have_a_name initially*/
int accept_new_player(){
     int i;
     struct player *new_player=(struct player *)malloc(sizeof(struct player));
     new_player->next=NULL;
     if(playerlist==NULL){
        playerlist=new_player;
     }else{
        new_player->next=playerlist;
        playerlist=new_player; 
     }
     for(i=0;i<NPITS;i++){
         (new_player->pits)[i]=INFINITY;
     }
     (new_player->pits)[NPITS]=0;
     for(i=0;i<MAXNAME+1;i++){
	(new_player->name)[i]='\0';
	(new_player->has_read)[i]='\0';   
     }
     new_player->room=MAXNAME+1;
     new_player->inbuf=0;
     new_player->after=new_player->has_read;
     new_player->has_a_name=0;
     new_player->turn=0; 
     new_player->disconnected=0;
     int player_fd=accept(listenfd,NULL,NULL);
     if (player_fd < 0) {
        perror("accept");
        close(listenfd);
	free(new_player);
        exit(1);
     }
     new_player->fd=player_fd;
     char message[MAXMESSAGE+1]={'\0'};
     if(sprintf(message,"Welcome to Mancala. What is your name?\r\n")<0){
	perror("sprintf");
	exit(1);
     }
     write2(new_player,message,strlen(message)+1);

     return player_fd;
}


/*after a player enters his full valid name, we allow it to join the game and set up its corresponding fields*/
void join_game(struct player *cur_player,int num_pebbles,int *num_players){

     int nreads;
     int where=-1;
     int needs_new_name=0;
     int i;
     nreads=read(cur_player->fd,cur_player->after,cur_player->room);
     if(nreads<0){
        perror("read");
	exit(1);
     }else if(nreads==0){
        cur_player->disconnected=1;     
     }else{ //nreads>0
	   cur_player->inbuf+=nreads;
	   if((where = find_newline_permissively(cur_player->has_read, cur_player->inbuf)) > 0) {
	      (cur_player->has_read)[where]='\0';
	       if(!replicate_name(cur_player->has_read)){
		  strncpy(cur_player->name,cur_player->has_read,where+1);
		  (cur_player->name)[where]='\0';
		  cur_player->has_a_name=1;
		  *num_players=*num_players+1;
		  if((*num_players)==1){
		      for(i=0;i<NPITS;i++){
		         (cur_player->pits)[i]=4;
		      }
		  }else{
		      for(i=0;i<NPITS;i++){
		         (cur_player->pits)[i]=num_pebbles;
		      }
		  }
		  char joining_msg[MAXMESSAGE+1]={'\0'};
		  if(sprintf(joining_msg,"%s has joined our game\r\n",cur_player->name)<0){
	             perror("sprintf");
	             exit(1);
	          }
		  broadcast(joining_msg);
		  printf("%s has joined our game\n",cur_player->name);
		  broadcast_game_status(1,cur_player);	      
		  if((*num_players)==1){
		      cur_player->turn=1;
                      char message9[MAXMESSAGE+1]={'\0'};  
                      if(sprintf(message9,"%s, your move please?\r\n",cur_player->name)<0){
	                 perror("sprintf");
	                 exit(1);
                      }
                      write2(cur_player,message9,strlen(message9)+1);
		  }
		      
		}else{
		      needs_new_name=1;
		      
		}
	   }else if(where==0){
	         needs_new_name=3;
	         
	   }else{ //where<0
	      cur_player->after=&((cur_player->has_read)[cur_player->inbuf]);
	      cur_player->room=MAXNAME+1-cur_player->inbuf;
	      if(cur_player->room<=0&&where<0){
	         needs_new_name=2;
	         
	      }
           }
     }             
     

     /*if needs_new_name*/
     if(needs_new_name==1 || needs_new_name==2 || needs_new_name==3){
	char message[MAXMESSAGE+1]={'\0'};
	if(needs_new_name==1){ /*repetitive username*/
	   if(sprintf(message,"Username already exists, please enter another one\r\n")<0){
	      perror("sprintf");
	      exit(1);
	   }
	   write2(cur_player,message,strlen(message)+1);
	   for(i=0;i<MAXNAME+1;i++){ //refresh;
	       (cur_player->name)[i]='\0';
	       (cur_player->has_read)[i]='\0';   
	   }
	   cur_player->room=MAXNAME+1;
           cur_player->inbuf=0;
           cur_player->after=cur_player->has_read;
	}else if(needs_new_name==2){ /*username too long*/
           
	   if(sprintf(message,"Username is too long, goodbye and have a nice day\r\n")<0){
	      perror("sprintf");
	      exit(1);
	   }
	   write2(cur_player,message,strlen(message)+1);
	   cur_player->disconnected=1;
	}else{ /*username is empty*/
	   if(sprintf(message,"Username cannot be empty, please enter another one\r\n")<0){
	      perror("sprintf");
	      exit(1);
	   }
	   write2(cur_player,message,strlen(message)+1);
	   for(i=0;i<MAXNAME+1;i++){
	       (cur_player->name)[i]='\0';
	       (cur_player->has_read)[i]='\0';   
	   }
	   cur_player->room=MAXNAME+1;
           cur_player->inbuf=0;
           cur_player->after=cur_player->has_read;
	}
     }


}


/*return the position (index) of any of '\n' or '\r\n', (but not '\r') or -1 if none of them were found in the first n indexes in buf*/
int find_newline_permissively(const char *buf, int n) {
    int i;
    for(i=0;i<n-1;i++){
        if(buf[i]=='\r'){
	   if(buf[i+1]=='\n'){
	      return i;
	   }
	}else if(buf[i]=='\n'){
	   return i;
	}
    }
    if(buf[n-1]=='\n'){
       return n-1;
    }
    return -1;
}


/*return 1 iff the name in buf is the same as one of the names of players having joined the game*/
int replicate_name(char *buf){
    struct player *temp=playerlist;
    while(temp!=NULL){
          if(temp->has_a_name==1&&temp->disconnected==0){
             if(strcmp(buf,temp->name)==0){
	        return 1;
	     }
	  }
    	  temp=temp->next;
    }
    return 0;
}


/*make move according to the move input of the user, and ask for another move input if what the user entered is not a valid pit index*/
int make_move(struct player *cur_player){
     int move_index;
     int valid_move_index=0;
     //int some_one_has_dropped;
     while(!valid_move_index){
           move_index=read_move(cur_player);
	   if(move_index>=0&&move_index<=NPITS-1&&(cur_player->pits)[move_index]!=0){
	      valid_move_index=1;
	   }else if(move_index==-12580){/*player has disconnected*/
	      return 2;
	   }else{
	        if(move_index==-12345){
	           printf("That is strange, either the user has entered -12345 by coincidence(check this, and it's OK if so), or something wrong happed in read_move()\n");
	        }
	        char message3[MAXMESSAGE+1]={'\0'};
                if(sprintf(message3,"Please enter another move\r\n")<0){
	           perror("sprintf");
	           exit(1);
                }
	        write2(cur_player,message3,strlen(message3)+1);      
	      }
     }

     printf("It is %s's turn to move\n",cur_player->name);

     char message2[MAXMESSAGE+1]={'\0'};
     if(sprintf(message2,"%s made a move from his %d-th pit\r\n",cur_player->name,move_index)<0){
	perror("sprintf");
	exit(1);
     }
     broadcast(message2);
     int num_pebbles=(cur_player->pits)[move_index];
     for(int i=move_index+1;i<=NPITS;i++){
         if(num_pebbles==0){
	    return 0;
	 }
	 num_pebbles-=1;
	 (cur_player->pits)[move_index]-=1;
	 (cur_player->pits)[i]+=1;
     }
     if(num_pebbles==0){
	return 1;
     }
     struct player *temp=find_next_available_player(cur_player); 
     if(temp==NULL){
        temp=playerlist;
	if(temp!=NULL){
	   if(temp->has_a_name!=1 || temp->disconnected!=0){
	      temp=find_next_available_player(temp);
	   }
	}
     }
     while(num_pebbles>0&&temp!=NULL){
           if(temp->fd!=cur_player->fd){/*external move*/
              for(int i=0;i<NPITS;i++){ 
                  if(num_pebbles==0){
	             return 0;
	          }
	          num_pebbles-=1;
	          (cur_player->pits)[move_index]-=1;
	          (temp->pits)[i]+=1;
              }
	      temp=find_next_available_player(temp);
              if(temp==NULL){
                 temp=playerlist;
		 if(temp!=NULL){ 
	            if(temp->has_a_name!=1 || temp->disconnected!=0){
	               temp=find_next_available_player(temp);
	            }
	         }
              }
           }else{/*internl move*/
	      for(int i=0;i<=NPITS;i++){ 
                  if(num_pebbles==0){
	             return 0;
	          }
	          num_pebbles-=1;
	          (cur_player->pits)[move_index]-=1;
	          (temp->pits)[i]+=1;
              }
	      if(num_pebbles==0){
	         return 1;
              }
	      temp=find_next_available_player(temp);
              if(temp==NULL){ 
                 temp=playerlist;
                 if(temp!=NULL){ 
	            if(temp->has_a_name!=1 || temp->disconnected!=0){
	               temp=find_next_available_player(temp);
	            }
	         }
	      }
	   }
     }
     return 0;
}


/*
 read the player's input for move.
 VERY IMPORTANT CLARIFICATION HERE: according to my understanding, if we can read the user imput of their move in a single read, then the while loop below will surely works fine;
 however, it also handles the situation in which the read of the move entered by the user takes several calls.(although this is assumed to not happen, it did happen on one of my classmates)
 the only situation this while loop will get blocked is when the user type part of his input and then manually pressed Ctrl+D,
 however, this situation makes no sense to me here, in reading a user's name, it's fine if he/she enter part of the name and then stops---we just leave him/her there until a full name is given
 here things are different: we definitely need the move from the user whose turn it is before we can move on.  
 */
int read_move(struct player *cur_player){
    char buf[MAXMESSAGE+1] = {'\0'};
    int room=MAXMESSAGE+1;
    int inbuf=0;
    char *after=buf;
    int nreads;
    int where=-1;
    int move_index=-12345;
    while((nreads=read(cur_player->fd,after,room))>0){
          inbuf+=nreads;
	  if((where = find_newline_permissively(buf, inbuf)) > 0) {
	      buf[where]='\0';
              move_index=strtol(buf,NULL,10);
	      break;
	  }
	  after=&(buf[inbuf]);
	  room=MAXMESSAGE+1-inbuf;          
    }
    if(nreads==0){
       cur_player->disconnected=1;
       return -12580;
    }else if(nreads<0){
       perror("read");
       exit(1);
    }
    return move_index;    
}


/*read inputs that can be anything (maybe) other than a single number, however, these will be ignored*/
void read_and_ignore_general_input(struct player *cur_player){
     char buf[3*MAXMESSAGE+1] = {'\0'};
     int nreads;
     nreads=read(cur_player->fd,buf,sizeof(buf));
     if(nreads==0){
        cur_player->disconnected=1;
     }else if(nreads<0){
        perror("read");
        exit(1);
     }
}



/*broadcast a message to all the players who has joined the game*/
void broadcast(char *s){  
     struct player *temp=playerlist; 
     while(temp!=NULL){
           if(temp->has_a_name==1&&temp->disconnected==0){
	      write2(temp,s,strlen(s)+1);	      
	   }
	   temp=temp->next;
     }
}


/*broadcast game status, option=0: to every player who has joined the game; option=1: only to specified player*/
void broadcast_game_status(int option,struct player *cur_player){
     char buf[1024];
     struct player *temp=playerlist;
     while(temp!=NULL){
           for(int j=0;j<1024;j++){
	       buf[j]='\0'; 
	   }
           if(temp->has_a_name==1&&temp->disconnected==0){
	      char message[MAXMESSAGE+1]={'\0'};
	      if(sprintf(message,"%s:  ",temp->name)<0){
	         perror("sprintf");
	         exit(1);
	      }
	      strncpy(buf,message,strlen(message)+1);
	      buf[strlen(message)]='\0';
	      for(int i=0;i<NPITS;i++){
	          char message2[MAXMESSAGE+1]={'\0'};
	          if(sprintf(message2,"[%d]%d ",i,(temp->pits)[i])<0){
	             perror("sprintf");
	             exit(1);
	          }
		  strncat(buf,message2,sizeof(buf)-strlen(buf)-1);
		  //don't need to null terminate here
	      }
	      char message3[MAXMESSAGE+1]={'\0'};
	      if(sprintf(message3," [end pit]%d\r\n",(temp->pits)[NPITS])<0){
	         perror("sprintf");
	         exit(1);
	      }
	      strncat(buf,message3,sizeof(buf)-strlen(buf)-1);
	      //don't need to null terminate here
              if(option==0){
	         broadcast(buf);
	      }else if(option==1){
	        write2(cur_player,buf,strlen(buf)+1);
	      }
	   }
	   temp=temp->next;
     }
}


/*delete a player from playerlist*/
void delete_player(struct player *player){
     struct player *cur=playerlist;
     struct player *prev=NULL;
     while(cur!=NULL){
           if(cur->fd==player->fd){
	      if(prev==NULL){
	         playerlist=cur->next;
	      }else{
	         prev->next=cur->next;
	      }
	      return;
	   }
	   prev=cur;
	   cur=cur->next;
     }
     
}


/*delete a disconnected player from playerlist and all_fds and close its fd; also update 'num_players' (declared in main to keep track of number of players who has joined the game)*/
void disconnect_player(struct player *disconnected_player, fd_set **all_fds, int *num_players){
     FD_CLR(disconnected_player->fd,*all_fds);  
     delete_player(disconnected_player);
     if(disconnected_player->has_a_name==1){  
        *num_players=*num_players-1;
     }
     close2(disconnected_player->fd);
     //printf("a player has disconnected\n");
}


/*get rid of all disconnected players, and broadcast their left to all players currently joined in the game*/
void leave_only_connected_players(fd_set **all_fds, int *num_players, struct player *this_turn, int *turn_needs_update){
     struct player *next_turn=NULL;
     if((*turn_needs_update)==1){ 
        next_turn=find_next_available_player(this_turn);
        if(next_turn==NULL){
           next_turn=playerlist;
	   if(next_turn!=NULL){ 
	      if(next_turn->has_a_name!=1 || next_turn->disconnected!=0){
	         next_turn=find_next_available_player(next_turn);
	      }
	   }
        }
     }
     struct player *to_be_disconnected;
     struct player *temp=playerlist;
     while(temp!=NULL){
	   if(temp->disconnected==1){
	      if((*turn_needs_update)==1){ 
	         if(next_turn!=NULL&&temp->fd==next_turn->fd){
	            next_turn=find_next_available_player(next_turn);      
	            if(next_turn==NULL){
                       next_turn=playerlist;
		       if(next_turn!=NULL){
                          if(next_turn->has_a_name!=1 || next_turn->disconnected!=0){
	                     next_turn=find_next_available_player(next_turn);
	                  }
		       }
		    }
	         }
	      }
	      to_be_disconnected=temp;
	      temp=temp->next;
	      disconnect_player(to_be_disconnected,all_fds,num_players);
	      if(to_be_disconnected->has_a_name==1){ 
	         char message[MAXMESSAGE+1]={'\0'};
                 if(sprintf(message,"%s has left our game\r\n",to_be_disconnected->name)<0){
	            perror("sprintf");
	            exit(1);
                 }
	         broadcast(message);
		 printf("%s has left our game\n",to_be_disconnected->name);
	      }else{
	         printf("A player has disconnected before entering a valid full name\n");
	      }
	      free(to_be_disconnected);              
	      continue;
	   }
	   temp=temp->next;
     }
     if((*turn_needs_update)==1){ 
         if(next_turn!=NULL){
	    next_turn->turn=1;
	    char message2[MAXMESSAGE+1]={'\0'};
            if(sprintf(message2,"%s, your move please?\r\n",next_turn->name)<0){
	       perror("sprintf");
	       exit(1);
            }
            write2(next_turn,message2,strlen(message2)+1); 
         }
	 *turn_needs_update=0; 
     }
}


/*extended write function that also takes into account the possiblity that player has disconnected from the game*/
void write2(struct player *player, char *msg, int msg_length){
    if(write(player->fd,msg,msg_length)<0){
       if(errno==EPIPE){
	  player->disconnected=1;
       }else{
	  perror("write");
	  exit(1);
       }
    }
}


/*find the next available player, by available, I mean it has joined the game and has not disconnected*/
struct player* find_next_available_player(struct player *cur_player){
       struct player *temp;
       if(cur_player!=NULL){
          temp=cur_player->next;
          while(temp!=NULL){
                if(temp->has_a_name==1&&temp->disconnected==0){
	           break;
	        }
                temp=temp->next;
          }
       }else{
          temp=NULL;     
       }
       return temp;
}


/*close a fd with error checking*/
void close2(int fd){
     if(close(fd)==-1){
        perror("close:");
	exit(-1);
     }
}

