/**************************************************************
*    htron.c                                   	              *
*    H-Tron - a DOS Tron game for CGA 320 x 200  *
*    written in Borland Turbo C 2.01                          *
*    Author: RobertK (RobertK@psc.at), December 2016          *
*    CGA conversion and Amstrad Joytick by M Ralser 2026      *
*                                                             *
*    Controls:                                                *
*    P1/P2 turn left/right: A/D and Cursor left/right         *
*    Native Amstrad joystick IRQ1 support for Player Two      *
*    Pause game: P                                            *
*    End game: Escape                                         *
**************************************************************/

/*
   CGA graphics notes:
   - Uses Borland BGI CGA driver in 320x200 4-color mode (CGAC1).
   - Uses CGA palette 1: Player 1 cyan, Player 2 magenta, walls white.
   - Amstrad joystick handling is unchanged from the tested VGA version.
*/
#include <graphics.h>
#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <dos.h>

#define FIELD_WIDTH 40
#define FIELD_HEIGHT 33
#define BLOCK_WIDTH 8
#define BLOCK_HEIGHT 6
#define PLAYFIELD_TOP 3

#define POINTS_TO_WIN 5
#define AI_MAX_DISTANCE_TO_CHECK 15

/* CGA 320x200 palette 1 colours.
   Standard CGA cannot show cyan and yellow simultaneously.
   CGAC1 provides cyan, magenta and white. */
#define P1_COLOR 1
#define P2_COLOR 2
#define WALL_COLOR 3
#define SCORE_COLOR 3

/* Amstrad PC1512/PC1640 joystick raw scan codes.
   The Amstrad BIOS normally translates joystick directions to
   cursor-key scan codes, but accepting the raw values as well
   makes the game tolerant of systems/configurations that expose them. */
#define AMSTRAD_JOY_FIRE_A       0x77
#define AMSTRAD_JOY_FIRE_B       0x78
#define AMSTRAD_JOY_RIGHT        0x79
#define AMSTRAD_JOY_LEFT         0x7A
#define AMSTRAD_JOY_DOWN         0x7B
#define AMSTRAD_JOY_UP           0x7C

#define AMSTRAD_JOY_FIRE_A_UP    0xF7
#define AMSTRAD_JOY_FIRE_B_UP    0xF8
#define AMSTRAD_JOY_RIGHT_UP     0xF9
#define AMSTRAD_JOY_LEFT_UP      0xFA
#define AMSTRAD_JOY_DOWN_UP      0xFB
#define AMSTRAD_JOY_UP_UP        0xFC

/* Playfield array that holds the information whether
   there is an obstacle at this x,y coordinate.
   CGA playfield grid dimensions: 40 x 33
   x=0 to 39
   y=0 to 32
   Rows 0..2 are reserved for the score area;
   row 3 is the upper playfield wall, as in the original.
*/
int field[FIELD_WIDTH][FIELD_HEIGHT];

/* indicates whether the player has hit an obstacle */
int p1Crashed;
int p2Crashed;

/* player coordinate variables */
int p1x,p1y,p2x,p2y;

/* player direction variables in radian degrees */
/* 0 (or 360) = right, 90 = up, 180 = left, 270 = down */
int p1dir,p2dir;

/* player score variables */
int p1Score,p2Score;

/* One or two players */

int numberOfPlayers;

/* -------------------------------------------------------------
   Low-level keyboard / Amstrad joystick input

   The PC1512/PC1640 joystick produces raw scan codes on IRQ 1,
   but these codes are not placed in the BIOS keyboard buffer.
   Therefore bioskey() cannot see them.

   Confirmed on a real Amstrad:
       Left   = 7Ah
       Right  = 79h
       Fire B = 78h

   During game play we temporarily replace INT 09h and read port
   60h directly.  Normal keyboard controls are decoded here too.
   ------------------------------------------------------------- */

#define KBD_DATA_PORT 0x60
#define SYS_PORT_B    0x61
#define PIC_CMD_PORT  0x20
#define PIC_EOI       0x20

volatile int ev_p1_left  = 0;
volatile int ev_p1_right = 0;
volatile int ev_p2_left  = 0;
volatile int ev_p2_right = 0;
volatile int ev_fire     = 0;
volatile int ev_escape   = 0;
volatile int ev_pause    = 0;
volatile int ev_anykey   = 0;

volatile int joy_left_down   = 0;
volatile int joy_right_down  = 0;
volatile int joy_up_down     = 0;
volatile int joy_down_down   = 0;
volatile int joy_fire_a_down = 0;
volatile int joy_fire_b_down = 0;
volatile int ext_prefix      = 0;

void interrupt (*oldKeyboardISR)(void);

void ClearInputEvents()
{
  disable();

  ev_p1_left=0;
  ev_p1_right=0;
  ev_p2_left=0;
  ev_p2_right=0;
  ev_fire=0;
  ev_escape=0;
  ev_pause=0;
  ev_anykey=0;

  enable();
}

void interrupt GameKeyboardISR(void)
{
  unsigned char sc;

  unsigned char pb;

  sc=inp(KBD_DATA_PORT);

  /*
     AMSTRAD PC1512/PC1640 keyboard acknowledge.
     Pulse Port B bit 7, then restore it low.
  */
  pb=inp(SYS_PORT_B);
  outp(SYS_PORT_B,pb | 0x80);
  outp(SYS_PORT_B,pb & 0x7F);

  /* Extended-key prefix used by cursor keys on many AT keyboards */
  if (sc==0xE0)
  {
    ext_prefix=1;
    outp(PIC_CMD_PORT,PIC_EOI);
    return;
  }

  /* -------- Confirmed Amstrad joystick BREAK codes -------- */
  if (sc==AMSTRAD_JOY_LEFT_UP)   joy_left_down=0;
  if (sc==AMSTRAD_JOY_RIGHT_UP)  joy_right_down=0;
  if (sc==AMSTRAD_JOY_UP_UP)     joy_up_down=0;
  if (sc==AMSTRAD_JOY_DOWN_UP)   joy_down_down=0;
  if (sc==AMSTRAD_JOY_FIRE_A_UP) joy_fire_a_down=0;
  if (sc==AMSTRAD_JOY_FIRE_B_UP) joy_fire_b_down=0;

  /* Only act on MAKE codes below. */
  if ((sc & 0x80)==0)
  {
    ev_anykey=1;

    /*
       Confirmed on the user's real PC1512/PC1640 joystick:
         Left  7A / FA       Right 79 / F9
         Up    7C / FC       Down  7B / FB
         FireA 77 / F7       FireB 78 / F8

       HTRON uses Left and Right as relative turning controls.
       Up/Down are tracked so the handler fully understands the
       joystick, but they do not steer the light cycle.
    */
    if (sc==AMSTRAD_JOY_LEFT)
    {
      if (!joy_left_down)
      {
        ev_p2_left++;
        joy_left_down=1;
      }
    }
    else if (sc==AMSTRAD_JOY_RIGHT)
    {
      if (!joy_right_down)
      {
        ev_p2_right++;
        joy_right_down=1;
      }
    }
    else if (sc==AMSTRAD_JOY_UP)
    {
      joy_up_down=1;
    }
    else if (sc==AMSTRAD_JOY_DOWN)
    {
      joy_down_down=1;
    }
    else if (sc==AMSTRAD_JOY_FIRE_A)
    {
      if (!joy_fire_a_down)
      {
        ev_fire=1;
        joy_fire_a_down=1;
      }
    }
    else if (sc==AMSTRAD_JOY_FIRE_B)
    {
      if (!joy_fire_b_down)
      {
        ev_fire=1;
        joy_fire_b_down=1;
      }
    }

    /* Normal PC keyboard */
    else if (sc==0x1E) ev_p1_left++;   /* A */
    else if (sc==0x20) ev_p1_right++;  /* D */
    else if (sc==0x4B) ev_p2_left++;   /* Cursor Left */
    else if (sc==0x4D) ev_p2_right++;  /* Cursor Right */
    else if (sc==0x39) ev_fire=1;      /* Space */
    else if (sc==0x19) ev_pause=1;     /* P */
    else if (sc==0x01) ev_escape=1;    /* Escape */
  }

  ext_prefix=0;

  /* End of interrupt to the 8259 PIC */
  outp(PIC_CMD_PORT,PIC_EOI);
}

void InstallGameKeyboard()
{
  ClearInputEvents();

  joy_left_down=0;
  joy_right_down=0;
  joy_up_down=0;
  joy_down_down=0;
  joy_fire_a_down=0;
  joy_fire_b_down=0;
  ext_prefix=0;

  oldKeyboardISR=getvect(0x09);
  setvect(0x09,GameKeyboardISR);
}

void RestoreKeyboard()
{
  setvect(0x09,oldKeyboardISR);
}



void DrawBlock(int x, int y, int color)
/* x,y: block coordinates */
/* x=0 to 39, y=0 to 32 */
{
  int xCoord;
  int yCoord;

  if (x<0 || y<0 || x>=FIELD_WIDTH || y>=FIELD_HEIGHT) return;

  /* Transform to graphics coordinates */
  xCoord=x*BLOCK_WIDTH;
  yCoord=y*BLOCK_HEIGHT;

  /* draw block in the player's colour */
  setcolor(color);
  rectangle(xCoord,yCoord,xCoord+BLOCK_WIDTH-1,yCoord+BLOCK_HEIGHT-1);

  /* set obstacle marker */
  field[x][y]=1;
}


void InitPlayfield()
{
  int x,y;
  char textP1[25];
  char textP2[25];
  char far * textScore = "SCORE";

  /* clear playfield array */
  for(x=0;x<FIELD_WIDTH;x++)
   for(y=0;y<FIELD_HEIGHT;y++)
    field[x][y]=0;

  /*
     CGA layout:
       rows 0..2  = score/header area (18 pixels)
       row 3      = upper playfield wall
       row 32     = lower playfield wall

     40 * 8 = 320 pixels wide
     33 * 6 = 198 pixels high
  */
  setcolor(WALL_COLOR);
  rectangle(0,0,319,197);
  line(0,17,319,17);

  /* Compact score labels so they fit in 320 pixels. */
  if (numberOfPlayers==0)
  {
   sprintf(textP1,"CPU1:%i",p1Score);
   sprintf(textP2,"CPU2:%i",p2Score);
  }
  else if (numberOfPlayers==1)
  {
   sprintf(textP1,"CPU:%i",p1Score);
   sprintf(textP2,"YOU:%i",p2Score);
  }
  else
  {
   sprintf(textP1,"P1:%i",p1Score);
   sprintf(textP2,"P2:%i",p2Score);
  }

  setcolor(P1_COLOR);
  outtextxy(8,5,textP1);

  setcolor(P2_COLOR);
  outtextxy(312-textwidth(textP2),5,textP2);

  setcolor(SCORE_COLOR);
  outtextxy(160-(textwidth(textScore)/2),5,textScore);

  /*
     Do NOT mark the outermost drawable cells as occupied.
     The cycles may enter x=0, x=39, y=3 and y=32, so their
     visible blocks can actually touch the drawn border.
     Going beyond those cells is detected as a crash.
  */

  /* Start players in the middle of the CGA playfield. */
  p1x=10;
  p1y=18;
  p2x=29;
  p2y=18;

  p1dir=0;
  p2dir=180;
  p1Crashed=0;
  p2Crashed=0;

  DrawBlock(p1x,p1y,P1_COLOR);
  DrawBlock(p2x,p2y,P2_COLOR);
}

void MovePlayers()
{
  switch(p1dir)
  {
    case 0:   p1x=p1x+1; break;
    case 90:  p1y=p1y-1; break;
    case 180: p1x=p1x-1; break;
    case 270: p1y=p1y+1; break;
  }
  switch(p2dir)
  {
    case 0:   p2x=p2x+1; break;
    case 90:  p2y=p2y-1; break;
    case 180: p2x=p2x-1; break;
    case 270: p2y=p2y+1; break;
  }

  /*
     Collision detection.
     Allow the outermost visible grid cells themselves, so the
     cycle reaches the border graphically. A crash occurs only
     when it tries to move beyond the drawable playfield, or
     into an existing trail.
  */
  if (p1x<0 || p1x>=FIELD_WIDTH ||
      p1y<PLAYFIELD_TOP || p1y>=FIELD_HEIGHT)
  {
    p1Crashed=1;
    p2Score++;
  }
  else if (field[p1x][p1y]==1)
  {
    p1Crashed=1;
    p2Score++;
  }

  if (p2x<0 || p2x>=FIELD_WIDTH ||
      p2y<PLAYFIELD_TOP || p2y>=FIELD_HEIGHT)
  {
    p2Crashed=1;
    p1Score++;
  }
  else if (field[p2x][p2y]==1)
  {
    p2Crashed=1;
    p1Score++;
  }
  /* no points when both players crashed */
  if (p1Crashed==1 && p2Crashed==1)
  {
   p1Score--;
   p2Score--;
  }

  /* Draw players at their new positions */
  /* (unless a collision has been detected) */
  if (p1Crashed==0) DrawBlock(p1x,p1y,P1_COLOR);
  if (p2Crashed==0) DrawBlock(p2x,p2y,P2_COLOR);

}

void AIComputerPlayer(int *x, int *y, int *dir)
{
  /* Let the computer player decide whether to turn left or right,
     or continue straight on.
     The coordinate and direction variables are passed by reference,
     so we can use this function for either computer player one or two */

  /* number of empty grid blocks before an obstacle
     from the AI player's point of view */
  int distanceFront,distanceLeft,distanceRight;

  int maxDistanceToChk;
  maxDistanceToChk=AI_MAX_DISTANCE_TO_CHECK;

  /* To make the AI player's movements less predictable,
  we occasionally make him a little short-sighted */
  maxDistanceToChk=maxDistanceToChk-random(10);

  /* Check the distance from the AI player's position
     to the next obstacle in the front, left and right
     direction from the AI player's point of view. According
     to the players direction, we have to use the respective
     grid direction */
  switch(*dir)
  {
    case 0: /* right */
       distanceFront=AICheckDistanceEast(*x,*y,maxDistanceToChk);
       distanceLeft=AICheckDistanceNorth(*x,*y,maxDistanceToChk);
       distanceRight=AICheckDistanceSouth(*x,*y,maxDistanceToChk);
       break;
    case 90: /* up */
       distanceFront=AICheckDistanceNorth(*x,*y,maxDistanceToChk);
       distanceLeft=AICheckDistanceWest(*x,*y,maxDistanceToChk);
       distanceRight=AICheckDistanceEast(*x,*y,maxDistanceToChk);
       break;
    case 180: /* left */
       distanceFront=AICheckDistanceWest(*x,*y,maxDistanceToChk);
       distanceLeft=AICheckDistanceSouth(*x,*y,maxDistanceToChk);
       distanceRight=AICheckDistanceNorth(*x,*y,maxDistanceToChk);
       break;
    case 270: /* down */
       distanceFront=AICheckDistanceSouth(*x,*y,maxDistanceToChk);
       distanceLeft=AICheckDistanceEast(*x,*y,maxDistanceToChk);
       distanceRight=AICheckDistanceWest(*x,*y,maxDistanceToChk);
       break;
  }

  /* Now we decide what the AI player shall do */
  if ((distanceFront>=distanceLeft) && (distanceFront>=distanceRight))
  {
    /* clear sailing ahead, nothing to do */
  }
  else if ((distanceFront<distanceLeft) || (distanceFront<distanceRight))
  {
    /* now we know that it would be safer to turn either left or right */
    /* let us check which direction would be better */
    if (distanceLeft>distanceRight)
    {
    	/* turn left */
    	*dir = *dir+90;
    	if (*dir>=360) *dir=0;
    }
    else if (distanceLeft<distanceRight)
    {
        /* turn right */
	*dir = *dir-90;
	if (*dir<0) *dir=270;
    }
    else /* distanceLeft == distanceRight */
    {
	/* randomly turn either left or right */
	if (random(100)<50)
	{
	  /* turn left */
	  *dir = *dir+90;
	  if (*dir>=360) *dir=0;
	}
	else
	{
	  /* turn right */
	  *dir = *dir-90;
	  if (*dir<0) *dir=270;
	}
    }
  }
}

/* The following four functions calculate the distance from the
   given position to the next obstacle in grid map direction */
int AICheckDistanceEast(int x,int y,int maxDistanceToCheck)
{
  int distance;
  distance=1;
  while(distance<=maxDistanceToCheck &&
        x+distance<FIELD_WIDTH &&
        field[x+distance][y]==0)
  {
    distance++;
  }
  return distance;
}

int AICheckDistanceWest(int x,int y,int maxDistanceToCheck)
{
  int distance;
  distance=1;
  while(distance<=maxDistanceToCheck &&
        x-distance>=0 &&
        field[x-distance][y]==0)
  {
    distance++;
  }
  return distance;
}

int AICheckDistanceNorth(int x,int y,int maxDistanceToCheck)
{
  int distance;
  distance=1;
  while(distance<=maxDistanceToCheck &&
        y-distance>=PLAYFIELD_TOP &&
        field[x][y-distance]==0)
  {
    distance++;
  }
  return distance;
}

int AICheckDistanceSouth(int x,int y,int maxDistanceToCheck)
{
  int distance;
  distance=1;
  while(distance<=maxDistanceToCheck &&
        y+distance<FIELD_HEIGHT &&
        field[x][y+distance]==0)
  {
    distance++;
  }
  return distance;
}


void main()
{
  int x,y,i,j,exitgame;
  int key;
  int notPlayAgain;
  int grd, grm;
  int gresult;
  char textFinalScore[22];

  notPlayAgain=0;
  randomize();

  clrscr();
  printf("*** H-Tron ***\n");
  printf("A DOS game for a CGA graphics card (320 x 200),\n");
  printf("written 2016 by RobertK (RobertK@psc.at), expanded by M Ralser 2026.\n\n");
  printf("This is the classical overhead view \"Tron\" game from movie of the same title\n");
  printf("(motorcycles that leave \"wall trails\" behind them). If you crash into a wall,\n");
  printf("your opponent scores a point. The player who first reaches a score of %i wins.\n\n",POINTS_TO_WIN);

  printf("Controls:\n");
  printf("P1/P2 turn left/right: A/D and cursor left/right\n");
  printf("Amstrad PC1512/1640 joystick: Left/Right controls Player Two\n");
  printf("(in one-player mode the joystick controls the human player)\n");
  printf("Pause game: P, End Game: Escape\n");
  printf("Press Space or either Amstrad joystick fire button to start a round.\n\n");
  printf("One or two players (1/2)? (or press 0 for demo mode)");
  do
  {
    key=getch();
    if (key=='0') numberOfPlayers=0;
    if (key=='1') numberOfPlayers=1;
    if (key=='2') numberOfPlayers=2;
    if (key==27) /* Escape */
    {
      clrscr();
      return;
    }
  }
  while(key!='0' && key!='1' && key!='2');


  /* === switch to graphics mode: beginning of section === */

  /* Force the Borland BGI CGA driver and its 320 x 200 mode.
     CGA.BGI must be available in the current directory, or
     replace the empty path below with the directory containing
     your BGI drivers (for example "C:\\TC\\BGI"). */
  grd = CGA;
  grm = CGAC1;

  initgraph(&grd, &grm, "");
  gresult = graphresult();
  if(gresult != grOk)
  {
    printf("%s",grapherrormsg(gresult));
    getch();
    return;
  }

  setbkcolor(BLACK);
  setcolor(WALL_COLOR);

  /* === switch to graphics mode: end of section === */



  /* game restart loop */
  while(notPlayAgain<1)
  {
    p1Score=0;
    p2Score=0;
    exitgame=0;

    /* round restart loop */
    while(p1Score<POINTS_TO_WIN && p2Score<POINTS_TO_WIN && exitgame<1)
    {
	cleardevice();
	InitPlayfield();

  	/* workaround to avoid pause immediately after the game starts */
  	delay(10);

	/*
	   The Amstrad joystick is invisible to bioskey(), so install
	   our raw IRQ1 handler for the round.
	*/
	InstallGameKeyboard();

	if (numberOfPlayers>0)
	{
	  /* Wait for keyboard Space or either Amstrad joystick fire button. */
	  ClearInputEvents();
	  while (!ev_fire && !ev_escape)
	    delay(1);

	  if (ev_escape) exitgame=1;
	  ClearInputEvents();
	}
	else delay(1000); /* in demo mode, the round starts automatically */

	/* main game loop */
  	while(exitgame<1 && p1Crashed<1 && p2Crashed<1)
  	{
	  /*
	     Process all input events collected by IRQ1.
	     The counters make very quick key/joystick presses reliable.
	  */

	  if (ev_escape)
	  {
	    ev_escape=0;
	    exitgame=1;
	  }

	  if (ev_pause)
	  {
	    ev_pause=0;
	    ev_anykey=0;

	    /* Original behaviour: pause until another key/control is pressed. */
	    while (!ev_anykey && !ev_escape)
	      delay(1);

	    ev_anykey=0;
	  }

	  while (ev_p2_left>0)
	  {
	    ev_p2_left--;
	    if (numberOfPlayers>0)
	    {
	      p2dir=p2dir+90;
	      if (p2dir>=360) p2dir=0;
	    }
	  }

	  while (ev_p2_right>0)
	  {
	    ev_p2_right--;
	    if (numberOfPlayers>0)
	    {
	      p2dir=p2dir-90;
	      if (p2dir<0) p2dir=270;
	    }
	  }

	  while (ev_p1_left>0)
	  {
	    ev_p1_left--;
	    if (numberOfPlayers==2)
	    {
	      p1dir=p1dir+90;
	      if (p1dir>=360) p1dir=0;
	    }
	  }

	  while (ev_p1_right>0)
	  {
	    ev_p1_right--;
	    if (numberOfPlayers==2)
	    {
	      p1dir=p1dir-90;
	      if (p1dir<0) p1dir=270;
	    }
	  }

	  /* let the computer player decide what to do */
	  /* in one-player mode, Player 1 is controlled by the computer */
	  if (numberOfPlayers<2) AIComputerPlayer(&p1x,&p1y,&p1dir);
	  /* in demo mode, Player 2 is also controlled by the computer */
	  if (numberOfPlayers==0) AIComputerPlayer(&p2x,&p2y,&p2dir);

    	  MovePlayers();

	  /* CGA playfield is physically smaller, so slow the cycles by
	     about 30 percent compared with the VGA version. */
	  delay(65);
        }

	/* Restore the normal BIOS keyboard handler after each round. */
	RestoreKeyboard();

	/* wait a quarter of a second after a crash has
	   happened before clearing the screen */
	if (p1Crashed || p2Crashed) delay(250);
    }

    cleardevice();
    sprintf(textFinalScore,"Final Score: %i : %i",p1Score,p2Score);
    setcolor(SCORE_COLOR);
    outtextxy(40,20,textFinalScore);
    if (p1Score>p2Score)
	if (numberOfPlayers==0)
	  outtextxy(40,40,"Computer Player One wins!");
	else if (numberOfPlayers==2)
	  outtextxy(40,40,"Player One wins!");
	else
	  outtextxy(40,40,"Computer wins!");
    if (p1Score<p2Score)
	if (numberOfPlayers==0)
	  outtextxy(40,40,"Computer Player Two wins!");
	else if (numberOfPlayers==2)
	  outtextxy(40,40,"Player Two wins!");
	else
	  outtextxy(40,40,"You win!");
    if (p1Score==p2Score)
	outtextxy(40,40,"A draw!");
    outtextxy(40,60,"Play again (y/n)?");
    do
    {
    	key=getch();
    	if (key=='n' || key=='N') notPlayAgain=1;

    } while(key!='y' && key!='Y' && key!='n' && key!='N');

  }
  restorecrtmode(); /* switch back to text mode */
  closegraph(); /* to clean up memory */
  clrscr();

  return;
}