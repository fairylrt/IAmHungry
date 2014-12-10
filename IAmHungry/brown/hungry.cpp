#include<windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include<stdio.h>

#include "hungry.h"
#include <time.h>

/* The GTP specification leaves the initial board size and komi to the
 * discretion of the engine. We make the uncommon choices of 6x6 board
 * and komi -3.14.
 */
int board_size = 6;
float komi = -3.14;

FILE *log_file;

int NUM_VISITS=2;
int NUM_THREAD=4;
int RUN_TIME=9940;


#define MAX_SIM 100000
#define MAX_THREAD 10



Bundle *current;
Bundle *backup_board;


HANDLE wrt,mutex,chd;
int readcount;

/* Offsets for the four directly adjacent neighbors. Used for looping. */
 int deltai[4] = {-1, 1, 0, 0};
 int deltaj[4] = {0, 0, -1, 1};

/* Macros to convert between 1D and 2D coordinates. The 2D coordinate
 * (i, j) points to row i and column j, starting with (0,0) in the
 * upper left corner.
 */
#define POS(i, j) ((i) * board_size + (j))
#define I(pos) ((pos) / board_size)
#define J(pos) ((pos) % board_size)

/* Macro to find the opposite color. */
#define OTHER_COLOR(color) (WHITE + BLACK - (color))

void
init_brown()
{
	 srand((int)time(0));
  int k;
  int i, j;

  /* The GTP specification leaves the initial board configuration as
   * well as the board configuration after a boardsize command to the
   * discretion of the engine. We choose to start with up to 20 random
   * stones on the board.
   */

    log_file=fopen("log_file.txt","w");
  fprintf(log_file,"start\n");
  fclose(log_file);

  current=(Bundle *)malloc(sizeof(Bundle));
  backup_board=(Bundle *)malloc(sizeof(Bundle));
  init_board(current);
  init_board(backup_board);
}

void init_board(Bundle *bundle){
	bundle->board= (int *)malloc(board_size*board_size*sizeof(int));
	int i;
	for(i=0;i<board_size*board_size;i++)
		bundle->board[i]=EMPTY;
	bundle->go_string=(Stone *)malloc(board_size*board_size*sizeof(Stone));
	for(i=0;i<board_size*board_size;i++)
		bundle->go_string[i].next_stone=-1;
	bundle->fake_liberty=(int *)malloc(board_size*board_size*sizeof(int));
	for(i=0;i<board_size*board_size;i++)
		bundle->fake_liberty[i]=-1;
	bundle->final_status=(int *)malloc(board_size*board_size*sizeof(int));
	for(i=0;i<board_size*board_size;i++)
		bundle->final_status[i]=-1;
	bundle->ko_i=-1;
	bundle->ko_j=-1;
	bundle->last_i=-1;
	bundle->last_j=-1;
	bundle->step=0;
}

void free_board(Bundle *bundle){
	free(bundle->board);
	free(bundle->go_string);
	free(bundle->fake_liberty);
	free(bundle->final_status);
	free(bundle);
}

//清空棋盘
void clear_board(Bundle *bundle)
{
	int i;
	for(i=0;i<board_size*board_size;i++)
		bundle->board[i]=0;
}

//判断board上有子吗？
int board_empty(Bundle *bundle){
  int i;
  for (i = 0; i < board_size * board_size; i++)
    if (bundle->board[i] != EMPTY)
      return 0;

  return 1;
}


//获得i,j的颜色

int get_board(Bundle *bundle,int i, int j)
{
  return bundle->board[i * board_size + j];
}

// 根据一个点获得所在棋串
int get_string(Bundle *bundle,int i, int j, int *stonei, int *stonej)
{
  int num_stones = 0;
  int pos = POS(i, j);
  do {
    stonei[num_stones] = I(pos);
    stonej[num_stones] = J(pos);
    num_stones++;
    pos = bundle->go_string[pos].next_stone;
  } while (pos != POS(i, j));

  return num_stones;
}

 int
pass_move(int i, int j)
{
  return i == -1 && j == -1;
}
 int
on_board(int i, int j)
{
  return i >= 0 && i < board_size && j >= 0 && j < board_size;
}


int legal_move(Bundle *bundle,int i, int j, int color)
{
  int other = OTHER_COLOR(color);
  
  /* Pass is always legal. */
  if (pass_move(i, j))
    return 1;

  /* Already occupied. */
  if (get_board(bundle,i, j) != EMPTY)
    return 0;

  /* Illegal ko recapture. It is not illegal to fill the ko so we must
   * check the color of at least one neighbor.//recapture打劫？
   */
  if (i == bundle->ko_i && j == bundle->ko_j
      && ((on_board(i - 1, j) && get_board(bundle,i - 1, j) == other)
	  || (on_board(i + 1, j) && get_board(bundle,i + 1, j) == other)))
    return 0;

  return 1;
}

/* 棋串除了libi,libj还有其他地方有气吗
 */


 int has_additional_liberty(Bundle *bundle,int i, int j, int libi, int libj)
{
	int k;
	for(k=0;k<4;k++){
	  int bi=libi+deltai[k];
	  int bj=libj+deltaj[k];
	  if (on_board(bi, bj)){
		if (get_board(bundle,bi,bj)!=EMPTY)
		  bundle->fake_liberty[bundle->go_string[POS(bi,bj)].head]--;
	  }
	}
	int ans;
	if (bundle->fake_liberty[bundle->go_string[POS(i,j)].head]==0)
		ans=0;
	else
		ans=1;
	for(k=0;k<4;k++){
	  int bi=libi+deltai[k];
	  int bj=libj+deltaj[k];
	  if (on_board(bi, bj)){
		if (get_board(bundle,bi,bj)!=EMPTY)
		  bundle->fake_liberty[bundle->go_string[POS(bi,bj)].head]++;
	  }
	}
	return ans;
}


/* Does (ai, aj) provide a liberty for a stone at (i, j)? */
//ai,aj给liberty气吗？（用于判断是否为自杀）
//给气的方式有两种，一，那个地方为空，二，那个地方的子是跟自己一个颜色的，而且那个地方的子还有气（就是不会填进去就正好被围起来。）


 int suicide(Bundle *bundle,int i, int j, int color)
{
  int k;
  int ans=1;
  for(k=0;k<4;k++){
	  int bi=i+deltai[k];
	  int bj=j+deltaj[k];
	  if (on_board(bi, bj)){
	  if (get_board(bundle,bi,bj)==EMPTY)
		ans=0;
	  else
		bundle->fake_liberty[bundle->go_string[POS(bi,bj)].head]--;
	  }
  }
  if (ans==1)
	  for(k=0;k<4;k++){
		  int bi=i+deltai[k];
		  int bj=j+deltaj[k];
		  if (on_board(bi, bj)){
			if (get_board(bundle,bi,bj)==color){
				if (bundle->fake_liberty[bundle->go_string[POS(bi,bj)].head]>0){
					ans=0;
					break;
				}
			}
			else if (get_board(bundle,bi,bj)==OTHER_COLOR(color) && bundle->fake_liberty[bundle->go_string[POS(bi,bj)].head]==0){
				ans=0;
				break;
			}
		  }
	  }
  for(k=0;k<4;k++){
	  int bi=i+deltai[k];
	  int bj=j+deltaj[k];
	  if (on_board(bi, bj)){
		  if (get_board(bundle,bi,bj)!=EMPTY){
			bundle->fake_liberty[bundle->go_string[POS(bi,bj)].head]++;
		}
	  }
  }
  return ans;
}

/* Remove a string from the board array. There is no need to modify
 * the next_stone array since this only matters where there are
 * stones present and the entire string is removed.
 */
 int remove_string(Bundle *bundle,int i, int j)
{
  int pos = POS(i, j);
  int removed = 0;
  do {
    bundle->board[pos] = EMPTY;
	bundle->go_string[pos].head=-1;
    removed++;
	pos = bundle->go_string[pos].next_stone;
  } while (pos != POS(i, j));

  int k;
  do {
	  int ai=I(pos);
	  int aj=J(pos);
	 for(k=0;k<4;k++){
	  int bi=ai+deltai[k];
	  int bj=aj+deltaj[k];
	  if (on_board(bi, bj) && get_board(bundle,bi,bj)!=EMPTY)
		bundle->fake_liberty[bundle->go_string[POS(bi,bj)].head]++;
	}
	pos = bundle->go_string[pos].next_stone;
  } while (pos != POS(i, j));

  return removed;
}

// 判断两个子是不是在一个棋串内
// 可能可以优化一下算法
 int same_string(Bundle *bundle,int pos1, int pos2)
{
	return (bundle->go_string[pos1].head==bundle->go_string[pos2].head);
  /*int pos = pos1;
  do {
    if (pos == pos2)
      return 1;
    pos = bundle->next_stone[pos];
  } while (pos != pos1);
  
  return 0;*/
}

 //weiqi性质，两个棋串合并，气相加。
 //若无期，则weiqi为0
 //先分气再合并和先合并再分期一样。
 void combine(Bundle *bundle,int pos1,int pos2){
	 
		int pos = pos2;
		int lib1=bundle->fake_liberty[bundle->go_string[pos2].head];
		do {
		  bundle->go_string[pos].head=bundle->go_string[pos1].head;
			pos = bundle->go_string[pos].next_stone;
		} while (pos != pos2);
		int tmp = bundle->go_string[pos2].next_stone;
		bundle->go_string[pos2].next_stone = bundle->go_string[pos1].next_stone;
		bundle->go_string[pos1].next_stone = tmp;
		bundle->fake_liberty[bundle->go_string[pos1].head]+=lib1;

 }

void play_move(Bundle *bundle,int i, int j, int color)
{
	bundle->step++;
	bundle->last_i=i;
	bundle->last_j=j;
  int pos = POS(i, j);
  int captured_stones = 0;
  int k;

  /* Reset the ko point. */
  bundle->ko_i = -1;
  bundle->ko_j = -1;

  //如果是Pass就什么都不做
  if (pass_move(i, j))
    return;

  /* 如果是自杀，就把棋串撤了！！！！！！！！！！好像有点问题，应该是先判断对方有没有无气的子
   */
  /*
  if (suicide(bundle,i, j, color)) {
    for (k = 0; k < 4; k++) {
      int ai = i + deltai[k];
      int aj = j + deltaj[k];
      if (on_board(ai, aj)
	  && get_board(bundle,ai, aj) == color)
	remove_string(bundle,ai, aj);
    }
    return;
  }*/

  bundle->board[pos] = color;
  bundle->go_string[pos].next_stone = pos;
  bundle->go_string[pos].head=pos;//保证了next_stone[pos]不会受之前在这个点上的子的影响


  int lib=0;

  //四周的串先减去一个气
  for (k = 0; k < 4; k++) {
    int ai = i + deltai[k];
    int aj = j + deltaj[k];
	if (on_board(ai, aj)){
		int c=get_board(bundle,ai,aj);
		if (c==EMPTY)
			lib++;
		else 
			--bundle->fake_liberty[bundle->go_string[POS(ai,aj)].head];	
		}
  }

  bundle->fake_liberty[bundle->go_string[pos].head]=lib;
  //颜色一样的合并，不一样的提子
  for (k = 0; k < 4; k++) {
    int ai = i + deltai[k];
    int aj = j + deltaj[k];
	if (on_board(ai, aj)){
		int c=get_board(bundle,ai,aj);
		if (c==color){
			if (!same_string(bundle,pos,POS(ai,aj))){
				combine(bundle,pos,POS(ai,aj));
			}
		}
		else 
			{
				if (c==OTHER_COLOR(color)){
					if (bundle->fake_liberty[bundle->go_string[POS(ai,aj)].head]==0){
						captured_stones+=remove_string(bundle,ai,aj);
					}
				}
			}
	}
  }

  /*
  //把对方的子吃了
  // Not suicide. Remove captured opponent strings. 
  for (k = 0; k < 4; k++) {
    int ai = i + deltai[k];
    int aj = j + deltaj[k];
    if (on_board(ai, aj)
	&& get_board(bundle,ai, aj) == OTHER_COLOR(color)
	&& !has_additional_liberty(bundle,ai, aj, i, j))
      captured_stones += remove_string(bundle,ai, aj);
  }*/

  /* Put down the new stone. Initially build a single stone string by
   * setting next_stone[pos] pointing to itself.
   */
  /*
  bundle->board[pos] = color;
  bundle->next_stone[pos] = pos;//保证了next_stone[pos]不会受之前在这个点上的子的影响
  */
  /* 合并棋串（四周都要看一下）
   */

  /*
  for (k = 0; k < 4; k++) {
    int ai = i + deltai[k];
    int aj = j + deltaj[k];
    int pos2 = POS(ai, aj);
    //	 因为有可能两个方向的子原来在一个棋串里，所以，连接的时候要保证不会重复添加进棋串
     
    if (on_board(ai, aj) && bundle->board[pos2] == color && !same_string(bundle,pos, pos2)) {
      // The strings are linked together simply by swapping the the
       // next_stone pointers.
       
      int tmp = bundle->next_stone[pos2];
      bundle->next_stone[pos2] = bundle->next_stone[pos];
      bundle->next_stone[pos] = tmp;
    }
  }*/
  
  /* 防止回提
   */

  if (captured_stones == 1 && bundle->go_string[pos].next_stone == pos) {
    int ai, aj;
    /* Check whether the new string has exactly one liberty. If so it
     * would be an illegal ko capture to play there immediately. We
     * know that there must be a liberty immediately adjacent to the
     * new stone since we captured one stone.
     */
    for (k = 0; k < 4; k++) {
      ai = i + deltai[k];
      aj = j + deltaj[k];
      if (on_board(ai, aj) && get_board(bundle,ai, aj) == EMPTY)
	     break;
    }
    
    if (!has_additional_liberty(bundle,i, j, ai, aj)) {
      bundle->ko_i = ai;
      bundle->ko_j = aj;
    }
  }

}

void get_candidate_moves(Bundle *bundle,int candidate_moves[][MAX_BOARD],int color){
  int i;
	int ai, aj;
  int k;

  for (ai = 0; ai < board_size; ai++)
    for (aj = 0; aj < board_size; aj++) {
		candidate_moves[ai][aj]=0;
		if (legal_move(bundle,ai, aj, color) && !suicide(bundle,ai, aj, color)) {
			if (!suicide(bundle,ai, aj, OTHER_COLOR(color)))
				candidate_moves[ai][aj]=1;
		else {
			for (k = 0; k < 4; k++) {
				int bi = ai + deltai[k];
				int bj = aj + deltaj[k];
				if (on_board(bi, bj) && get_board(bundle,bi, bj) == OTHER_COLOR(color)) {
					candidate_moves[ai][aj]=1;
					break;
				}
			}
		}
	  }
	}

	 if(bundle->step<50){
 for(i=0;i<board_size;++i){
 candidate_moves[0][i]=0;
 candidate_moves[board_size-1][i]=0;
 candidate_moves[i][0]=0;
 candidate_moves[i][board_size-1]=0;
 }
 }
 if(bundle->step < 30){
 for(i=0;i<board_size;++i){
 candidate_moves[1][i]=0;
 candidate_moves[board_size-2][i]=0;
 candidate_moves[i][1]=0;
 candidate_moves[i][board_size-2]=0;
 }
 }
	
}
/* Generate a move. */
void generate_move(int *i, int *j, int color)
{
    uct_search(i,j,color);
	log_file=fopen("log_file.txt","at");
	fprintf(log_file,"@@@@@\n%d %d\n@@@@@@\n",*i,*j);
	fclose(log_file);
}
/* Generate a move. */
void s_generate_move(Bundle *bundle,int *i, int *j, int color){
  int moves[MAX_BOARD * MAX_BOARD];
  int num_moves = 0;
  int move;
  int ai, aj;

  int candidate_moves[MAX_BOARD][MAX_BOARD];
  
  get_candidate_moves(bundle,candidate_moves,color);
  memset(moves, 0, sizeof(moves));
  for (ai = 0; ai < board_size; ai++)
    for (aj = 0; aj < board_size; aj++) {
      if (candidate_moves[ai][aj])
		  moves[num_moves++] = POS(ai, aj);
    }

  if (num_moves > 0) {
    move = moves[rand() % num_moves];
    *i = I(move);
    *j = J(move);
  }
  else {
    /* But pass if no move was considered. */
    *i = -1;
    *j = -1;
  }
}

/* Set a final status value for an entire string. */
 void
set_final_status_string(Bundle *bundle,int pos, int status)
{
  int pos2 = pos;
  do {
    bundle->final_status[pos2] = status;
	pos2 = bundle->go_string[pos2].next_stone;
  } while (pos2 != pos);
}

/* Compute final status. This function is only valid to call in a
 * position where generate_move() would return pass for at least one
 * color.
 *
 * Due to the nature of the move generation algorithm, the final
 * status of stones can be determined by a very simple algorithm:
 *
 * 1. Stones with two or more liberties are alive with territory.
 * 2. Stones in atari are dead.
 *
 * Moreover alive stones are unconditionally alive even if the
 * opponent is allowed an arbitrary number of consecutive moves.
 * Similarly dead stones cannot be brought alive even by an arbitrary
 * number of consecutive moves.
 *
 * Seki is not an option. The move generation algorithm would never
 * leave a seki on the board.
 *
 * Comment: This algorithm doesn't work properly if the game ends with
 *          an unfilled ko. If three passes are required for game end,
 *          that will not happen.
 */
void
compute_final_status(Bundle *bundle)
{
  int i, j;
  int pos;
  int k;

  for (pos = 0; pos < board_size * board_size; pos++)
    bundle->final_status[pos] = UNKNOWN;
  
  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++)
      if (get_board(bundle,i, j) == EMPTY)
	for (k = 0; k < 4; k++) {
	  int ai = i + deltai[k];
	  int aj = j + deltaj[k];
	  if (!on_board(ai, aj))
	    continue;
	  /* When the game is finished, we know for sure that (ai, aj)
           * contains a stone. The move generation algorithm would
           * never leave two adjacent empty vertices. Check the number
           * of liberties to decide its status, unless it's known
           * already.
	   *
	   * If we should be called in a non-final position, just make
	   * sure we don't call set_final_status_string() on an empty
	   * vertex.
	   */
	  pos = POS(ai, aj);
	  if (bundle->final_status[pos] == UNKNOWN) {
	    if (get_board(bundle,ai, aj) != EMPTY) {
	      if (has_additional_liberty(bundle,ai, aj, i, j))
		set_final_status_string(bundle,pos, ALIVE);
	      else
		set_final_status_string(bundle,pos, DEAD);
	    }
	  }
	  /* Set the final status of the (i, j) vertex to either black
           * or white territory.
	   */
	  if (bundle->final_status[POS(i, j)] == UNKNOWN) {
	    if ((bundle->final_status[pos] == ALIVE) ^ (get_board(bundle,ai, aj) == WHITE))
	      bundle->final_status[POS(i, j)] = BLACK_TERRITORY;
	    else
	      bundle->final_status[POS(i, j)] = WHITE_TERRITORY;
	  }
	}
}

int
get_final_status(Bundle *bundle,int i, int j)
{
  return bundle->final_status[POS(i, j)];
}

void
set_final_status(Bundle *bundle,int i, int j, int status)
{
  bundle->final_status[POS(i, j)] = status;
}

/* Valid number of stones for fixed placement handicaps. These are
 * compatible with the GTP fixed handicap placement rules.
 */
int
valid_fixed_handicap(int handicap)
{
  if (handicap < 2 || handicap > 9)
    return 0;
  if (board_size % 2 == 0 && handicap > 4)
    return 0;
  if (board_size == 7 && handicap > 4)
    return 0;
  if (board_size < 7 && handicap > 0)
    return 0;
  
  return 1;
}

/* Put fixed placement handicap stones on the board. The placement is
 * compatible with the GTP fixed handicap placement rules.
 */
void
place_fixed_handicap(Bundle *bundle,int handicap)
{
  int low = board_size >= 13 ? 3 : 2;
  int mid = board_size / 2;
  int high = board_size - 1 - low;
  
  if (handicap >= 2) {
    play_move(bundle,high, low, BLACK);   /* bottom left corner */
    play_move(bundle,low, high, BLACK);   /* top right corner */
  }
  
  if (handicap >= 3)
    play_move(bundle,low, low, BLACK);    /* top left corner */
  
  if (handicap >= 4)
    play_move(bundle,high, high, BLACK);  /* bottom right corner */
  
  if (handicap >= 5 && handicap % 2 == 1)
    play_move(bundle,mid, mid, BLACK);    /* tengen */
  
  if (handicap >= 6) {
    play_move(bundle,mid, low, BLACK);    /* left edge */
    play_move(bundle,mid, high, BLACK);   /* right edge */
  }
  
  if (handicap >= 8) {
    play_move(bundle,low, mid, BLACK);    /* top edge */
    play_move(bundle,high, mid, BLACK);   /* bottom edge */
  }
}

/* Put free placement handicap stones on the board. We do this simply
 * by generating successive black moves.
 */
void
place_free_handicap(Bundle *bundle,int handicap)
{
  int k;
  int i, j;
  
  for (k = 0; k < handicap; k++) {
    generate_move(&i, &j, BLACK);
    play_move(bundle,i, j, BLACK);
  }
}


/***********************************************************************************************************/

int quick_judge(int *x, int *y, int color) {
	if (current->step / 2 + 1 <= 8) {
		if (get_board(current, 3, 2) == EMPTY) {
			*x = 3;
			*y = 2;
			return 1;
		}
		if (get_board(current, 3, 2) == OTHER_COLOR(color)
				&& get_board(current, 4, 4) == EMPTY) {
			*x = 4;
			*y = 4;
			return 1;
		}
		if (get_board(current, 3, 4) == EMPTY) {
			*x = 3;
			*y = 4;
			return 1;
		}
		if (get_board(current, 3, 4) == OTHER_COLOR(color)
				&& get_board(current, 4, 2) == EMPTY) {
			*x = 4;
			*y = 2;
			return 1;
		}

		if (get_board(current, 9, 10) == EMPTY) {
			*x = 9;
			*y = 10;
			return 1;
		}
		if (get_board(current, 9, 10) == OTHER_COLOR(color)
				&& get_board(current, 8, 8) == EMPTY) {
			*x = 8;
			*y = 8;
			return 1;
		}
		if (get_board(current, 9, 8) == EMPTY) {
			*x = 9;
			*y = 8;
			return 1;
		}
		if (get_board(current, 9, 8) == OTHER_COLOR(color)
				&& get_board(current, 8, 10) == EMPTY) {
			*x = 8;
			*y = 10;
			return 1;
		}
		if (get_board(current, 3, 10) == EMPTY) {
			*x = 3;
			*y = 10;
			return 1;
		}
		if (get_board(current, 3, 10) == OTHER_COLOR(color)
				&& get_board(current, 4, 8) == EMPTY) {
			*x = 4;
			*y = 8;
			return 1;
		}

		if (get_board(current, 3, 8) == EMPTY) {
			*x = 3;
			*y = 8;
			return 1;
		}
		if (get_board(current, 3, 8) == OTHER_COLOR(color)
				&& get_board(current, 4, 10) == EMPTY) {
			*x = 4;
			*y = 10;
			return 1;
		}
		if (get_board(current, 9, 2) == EMPTY) {
			*x = 9;
			*y = 2;
			return 1;
		}
		if (get_board(current, 9, 2) == OTHER_COLOR(color)
				&& get_board(current, 8, 4) == EMPTY) {
			*x = 8;
			*y = 4;
			return 1;
		}
		if (get_board(current, 9, 4) == EMPTY) {
			*x = 9;
			*y = 4;
			return 1;
		}
		if (get_board(current, 9, 4) == OTHER_COLOR(color)
				&& get_board(current, 8, 2) == EMPTY) {
			*x = 8;
			*y = 2;
			return 1;
		}
	}
	int ai, aj;
	int k;
	for (ai = 0; ai < board_size; ai++) {
		for (aj = 0; aj < board_size; aj++) {
			//仅有一口气的棋串，必须吃掉
			if (legal_move(current, ai, aj, color)
					&& !suicide(current, ai, aj, color)) {
				for (k = 0; k < 4; k++) {
					int ei = ai + deltai[k];
					int ej = aj + deltaj[k];

					if (on_board(ei,
							ej) && get_board(current,ei, ej) == OTHER_COLOR(color))
						if (!has_additional_liberty(current, ei, ej, ai, aj)) {
							*x = ai;
							*y = aj;
							return 1;
						}
					
				}
			}
		}
	}
	return 0;
}
void uct_search(int *x,int *y,int color){
	log_file=fopen("log_file.txt","at");
	fprintf(log_file,"uct_search\n");
	fclose(log_file);

	if (quick_judge(x, y, color))
		return;

	Node *root =(Node *) malloc(sizeof(Node));
	init_node(-1,-1,root,NULL);
	create_children(current,root,color);
	if (root->child==NULL){
		*x=-1;
		*y=-1;
		return;
	}
	int start_time=clock();
	//init thread
	readcount=0;
	mutex=CreateMutex(NULL,FALSE,NULL);
	wrt=CreateMutex(NULL,FALSE,NULL);
	chd=CreateMutex(NULL,FALSE,NULL);
	//
	play_simulation_thread(current,root,color);
	//close
	CloseHandle(mutex);
	CloseHandle(wrt);
	CloseHandle(chd);
	//
	Node *n=get_best_child(root);
	*x=n->x;
	*y=n->y;
	
	log_file=fopen("log_file.txt","at");
	fprintf(log_file,"####\ntime spent: %dms\nsimulate count: %d\n####\n",clock()-start_time,root->visits);

	free_tree(root);
	fclose(log_file);
}

void init_node(int x,int y,Node* node,Node *parent){
	node->x=x;
	node->y=y;
	node->child=NULL;
	node->sibling=NULL;
	node->visits=0;
	node->wins=0;
	node->more=0;
	node->parent=parent;
}

void create_children(Bundle *bundle,Node* node,int color){
	int i,j;
	int candidate_moves[MAX_BOARD][MAX_BOARD];
	Node *last=NULL;
	get_candidate_moves(bundle,candidate_moves,color);
	wait(chd);
	if (node->child==NULL){
		for(i=0;i<board_size;i++)
			for(j=0;j<board_size;j++)
				if (candidate_moves[i][j]){
					Node *tmp =(Node *) malloc(sizeof(Node));
					init_node(i,j,tmp,node);
					tmp->sibling=last;
					last=tmp;
				}
		node->child=last;
		if (node->child==NULL){
			Node *tmp =(Node *) malloc(sizeof(Node));
			init_node(-1,-1,tmp,node);
			node->child=tmp;
			tmp->sibling=NULL;
		}
	}
	signal(chd);
}


Node* get_best_child(Node *node){
	if(node->child==NULL)
		return NULL;
	Node* n=node->child;
	int max_visits=n->visits;
	Node *max_node=n;
	log_file=fopen("log_file.txt","at");
	fprintf(log_file,"*****************************\n");
	fprintf(log_file,"step: %d\n",current->step/2+1); 
	while(n->sibling)
	{
		n=n->sibling;
		fprintf(log_file,"%d %d %d %f\n",n->x,n->y,n->visits,(float)n->wins/n->visits);
		if (max_node->visits<n->visits)
		{
			max_node=n;
		}
		else if (max_node->visits-n->visits<10 && max_node->more>n->more){
			max_node=n;
		}
	}
	fprintf(log_file,"best child:%d %d %d %f %f\n",max_node->x,max_node->y,max_node->visits,(float)max_node->wins/max_node->visits
		,(float)max_node->more/max_node->visits);
	fprintf(log_file,"*****************************\n");
	fclose(log_file);
	return max_node;
}

void free_tree(Node *n){
	if (n->child)
		free_tree(n->child);
	if(n->sibling)
		free_tree(n->sibling);
	free(n);
}

//color是下一步要走的人的颜色，node是现在这步
void play_simulation(Bundle *bundle,Node *n,int color){
	int randomresult=0;
	
	//if (n!=NULL){
	while (1){
		if (n->child==NULL && n->visits<NUM_VISITS){
			break;
		}
		else{
			if(n->child==NULL)
				create_children(bundle,n,color);
			Node *next=uct_select(n);
			//if(next!=NULL)
				play_move(bundle,next->x,next->y,color);
			n=next;
			color=OTHER_COLOR(color);
		}
	}
	play_random_game(bundle,n,color);
	//}
	//else
		//play_random_game(bundle,n,color);
}


DWORD WINAPI play_thread(PVOID Param)
{
  PMYDATA parameter;
  parameter = (PMYDATA)Param;
  Bundle *bundle=parameter->bundle;
  Node *root=parameter->node;
  int color=parameter->color;

  Bundle *s_bundle=(Bundle *)malloc(sizeof(Bundle));
	init_board(s_bundle);
	clone(s_bundle,bundle);
	
	
	clock_t start_time;
	start_time=clock();
	while((clock()-start_time)<RUN_TIME){
		play_simulation(s_bundle,root,color);
		clone(s_bundle,bundle);
	}
  free_board(s_bundle);
  return 0;
}

void play_simulation_thread(Bundle *bundle,Node *n,int color){
  PMYDATA param[MAX_THREAD];
  HANDLE  hThread[MAX_THREAD];
  DWORD   hid[MAX_THREAD];
    int i;
	for(i=0;i<NUM_THREAD;i++){
		param[i]=(PMYDATA) malloc(sizeof(MyDATA));
		param[i]->bundle = (Bundle *)malloc(sizeof(Bundle));
		init_board(param[i]->bundle);
		clone(param[i]->bundle,bundle);
		param[i]->node = n;
		param[i]->color = color;
		hThread[i] = CreateThread( NULL,  0,  play_thread,  param[i], 0, &hid[i]);
	}
	// Wait until all threads have terminated.
  WaitForMultipleObjects(NUM_THREAD, hThread, TRUE, INFINITE);
  for(i=0;i<NUM_THREAD;i++){
		free_board(param[i]->bundle);
	}
 
    // Close all thread handles and free memory allocations.
 
  for(i=0; i<NUM_THREAD; i++)
  {
      CloseHandle(hThread[i]);

  } 
}

void wait(HANDLE handle){
	WaitForSingleObject(handle,INFINITE);
}
void signal(HANDLE handle){
	ReleaseMutex(handle);
}
void update_node(int result,Node *n){
	wait(wrt);
	int res;
	if (result>0)
		res=1;
	else
		res=-1;
	while (n!=NULL){
		n->wins+=res;
		n->more+=result;
		n->visits++;
		n=n->parent;
		result=-result;
		res=-res;
	}
	signal(wrt);
}

Node* uct_select(Node *node)
{
	/*wait(mutex);
	readcount++;
	if (readcount==1)
		wait(wrt);
	signal(mutex);*/
	if(!node->child)
		return NULL;
	Node* parent=node;
	Node* child=node->child;
	Node* max_node=child;
	float max_uct=get_uct(parent,child);
	while(child->sibling)
	{
		child=child->sibling;
		float tmp_uct=get_uct(parent,child);
		if (max_uct<tmp_uct)
		{
			max_node=child;
			max_uct=tmp_uct;
		}
	}
	/*
	wait(mutex);
	readcount--;
	if (readcount==0)
		signal(wrt);
	signal(mutex);*/
	return max_node;
}

float get_uct(Node *parent,Node *child)
{
	float UCTK=0.5;
	float uct;
	if(child->visits>0)
	{
		float winrate=(float)child->wins/child->visits;
		uct=winrate + UCTK*sqrt(log((float)parent->visits)/child->visits);
	}
	else
		uct = 10000 + 1000*rand();
	return uct;
}

int play_random_game(Bundle *bundle, Node *node, int color) {

	//FILE *run=fopen("sav.sav","w");

	Bundle *s_bundle=(Bundle *)malloc(sizeof(Bundle));
	init_board(s_bundle);
	clone(s_bundle,bundle);

	int pass_num = 0;
	int current_color = color;
	
	int count=0;

	while (pass_num < 2 &&count<300) {
		int i;
		int j;
		count++;
		s_generate_move(bundle, &i, &j, current_color);
		/*if (current_color==WHITE)
		 fprintf(run,"W[%c%c];",'a'+i,'a'+j);
		 else
		 fprintf(run,"B[%c%c];",'a'+i,'a'+j);
		 */
		if (pass_move(i, j))
			pass_num++;
		else
			pass_num = 0;
		play_move(bundle, i, j, current_color);
		current_color = OTHER_COLOR(current_color);
	}
	if (count==300){
		free_board(s_bundle);
		return 0;
	}
	int score = get_final_win(bundle, color);
	free_board(s_bundle);
	update_node(-score, node);
	return score;
}
void clone(Bundle *b1,Bundle *b2){
	int i;
	b1->step=b2->step;
	b1->ko_i=b2->ko_i;
	b1->ko_j=b2->ko_j;
	b1->last_i=b2->last_i;
	b1->last_j=b2->last_j;
	for(i=0;i<board_size*board_size;i++){
		b1->board[i]=b2->board[i];
		b1->go_string[i]=b2->go_string[i];
		b1->fake_liberty[i]=b2->fake_liberty[i];
		b1->final_status[i]=b2->final_status[i];
	}//是不是可以不用？！！！
}

int get_final_score(Bundle *bundle,int color){
	int score =0;
  int i, j;

  compute_final_status(bundle);
  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++) {
      int status = get_final_status(bundle,i, j);
      if (status == BLACK_TERRITORY)
		score--;
      else if (status == WHITE_TERRITORY)
		score++;
      else if ((status == ALIVE) ^ (get_board(bundle,i, j) == WHITE))
		score--;
      else
		score++;
    }
	score=score;
	if (color==WHITE)
		return score;
	else
		return -score;
}

int get_final_win(Bundle *bundle,int color){
	int score =0;
  int i, j;

  compute_final_status(bundle);
  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++) {
      int status = get_final_status(bundle,i, j);
      if (status == BLACK_TERRITORY)
		score--;
      else if (status == WHITE_TERRITORY)
		score++;
      else if ((status == ALIVE) ^ (get_board(bundle,i, j) == WHITE))
		score--;
      else
		score++;
    }
	score=score*10+komi*10; 
	if (color==WHITE)
		return score;
	else
		return -score;

}

/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
