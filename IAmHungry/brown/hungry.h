#include <windows.h>

#define VERSION_STRING "2.0"

#define MIN_BOARD 2
#define MAX_BOARD 13

/* These must agree with the corresponding defines in gtp.c. */
#define EMPTY 0
#define WHITE 1
#define BLACK 2

/* Used in the final_status[] array. */
#define DEAD 0
#define ALIVE 1
#define SEKI 2
#define WHITE_TERRITORY 3
#define BLACK_TERRITORY 4
#define UNKNOWN 5

typedef struct Node{
	int wins;
	int visits;
	int x,y;
	int more;
	struct Node *child,*sibling,*parent;
} Node;

typedef struct Stone{
	int next_stone;
	int head;
	int liberty;
} Stone;

typedef struct Bundle{
	int *board;
	Stone *go_string;
	int *fake_liberty;
	int *final_status;
	int ko_i, ko_j;
	int last_i,last_j;
	int *kong;
	int step;
} Bundle;

typedef struct MyData 
{
  Bundle *bundle;
  Node *node;
  int color;
}MyDATA, *PMYDATA;


extern float komi;
extern int board_size;

extern Bundle *current;
extern int NUM_VISITS;
extern int NUM_THREAD;
extern int RUN_TIME;


void init_board(Bundle *bundle);
void free_board(Bundle *bundle);

void play_simulation_thread(Bundle *bundle,Node *n,int color);

void signal(HANDLE handle);
void wait(HANDLE handle);

void init_brown(void);
void clear_board(Bundle *bundle);
int board_empty(Bundle *bundle);
int get_board(Bundle *bundle,int i, int j);
int get_string(Bundle *bundle,int i, int j, int *stonei, int *stonej);
int legal_move(Bundle *bundle,int i, int j, int color);
void play_move(Bundle *bundle,int i, int j, int color);
void generate_move(int *i, int *j, int color);
void compute_final_status(Bundle *bundle);
int get_final_status(Bundle *bundle,int i, int j);
void set_final_status(Bundle *bundle,int i, int j, int status);
int valid_fixed_handicap(int handicap);
void place_fixed_handicap(Bundle *bundle,int handicap);
void place_free_handicap(Bundle *bundle,int handicap);

/*
void init_node(int x,int y,Node *node);
void create_children(Node* node,int color);
Node* get_best_children(Node *node);
void free_tree(Node *n);
void update_node(int result,Node *n);
Node* uct_select(Node *n);
Node* get_best_child(Node *root);
void uct_search(int *x,int *y,int color);
int play_simulation(Node *n,int color);
void s_generate_move(Bundle *bb,int *i, int *j, int color);
int play_random_game(int color);*/

void backup();
void recovery();
float get_uct(Node *parent,Node *child);
void init_node(int x,int y,Node *node,Node* parent);
void create_children(Bundle *bundle,Node* node,int color);
void free_tree(Node *n);
void update_node(int result,Node *n);
Node* uct_select(Node *n);
Node* get_best_child(Node *root);
void uct_search(int *x,int *y,int color);
void play_simulation(Bundle *bundle,Node *n,int color);
void s_generate_move(Bundle *bundle,int *i, int *j, int color);
int play_random_game(Bundle *bundle,Node* node,int color);
void clone(Bundle *b1,Bundle *b2);
int get_final_score(Bundle *bundle,int color);
int get_final_win(Bundle *bundle,int color);

void play_random_game_thread(Bundle *bundle, Node *n, int color);
void insert1(Bundle *bundle,int x,int y);
void insert2(Bundle *bundle,int x,int y);

/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
