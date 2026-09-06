/**************************************************************
*    htron.c                                   	              *
*    H-Tron - a DOS Tron game for the Hercules graphics card  *
*    written in Borland Turbo C 2.01                          *
*    Author: RobertK (RobertK@psc.at), December 2016          *
*    Expanded M Ralser 2026, adding Amstrad joystick support  *
*                                                             *
*    Controls:                                                *
*    P1/P2 turn left/right: A/D and Cursor left/right         *
*    (or cursor/Amstrad joystick in one-player mode)          *
*    Pause game: P                                            *
*    End game: Escape                                         *
**************************************************************/

#include <graphics.h>
#include <stdlib.h>
#include <conio.h>
#include <dos.h>
#include <bios.h>

#define POINTS_TO_WIN 5
#define AI_MAX_DISTANCE_TO_CHECK 15

/* Playfield array that holds the information whether
   there is an obstacle at this x,y coordinate.
   Playfield dimensions: 90 x 58
   x=0 to 89
   y=0 to 57
*/
int field[90][58];

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


/* =============================================================
   Amstrad PC1512 / PC1640 keyboard joystick support

   Confirmed on real Amstrad hardware:

       Control     Make     Break
       Fire A      77h      F7h
       Fire B      78h      F8h
       Right       79h      F9h
       Left        7Ah      FAh
       Down        7Bh      FBh
       Up          7Ch      FCh

   The joystick codes are visible on keyboard IRQ 1 but are not
   placed in the BIOS keyboard buffer, so bioskey() cannot see
   them.

   The interrupt acknowledge below is the sequence tested
   successfully on the real PC1512/PC1640:
       read port 60h
       pulse bit 7 of port 61h
       send EOI to the 8259 PIC
   ============================================================= */

#define KBD_DATA_PORT          0x60
#define SYS_PORT_B             0x61
#define PIC_CMD_PORT           0x20
#define PIC_EOI                0x20

#define AMSTRAD_JOY_FIRE_A     0x77
#define AMSTRAD_JOY_FIRE_B     0x78
#define AMSTRAD_JOY_RIGHT      0x79
#define AMSTRAD_JOY_LEFT       0x7A
#define AMSTRAD_JOY_DOWN       0x7B
#define AMSTRAD_JOY_UP         0x7C

#define AMSTRAD_JOY_FIRE_A_UP  0xF7
#define AMSTRAD_JOY_FIRE_B_UP  0xF8
#define AMSTRAD_JOY_RIGHT_UP   0xF9
#define AMSTRAD_JOY_LEFT_UP    0xFA
#define AMSTRAD_JOY_DOWN_UP    0xFB
#define AMSTRAD_JOY_UP_UP      0xFC

/* Input events consumed by the main game loop */
volatile int ev_p1_left  = 0;
volatile int ev_p1_right = 0;
volatile int ev_p2_left  = 0;
volatile int ev_p2_right = 0;
volatile int ev_fire     = 0;
volatile int ev_pause    = 0;
volatile int ev_escape   = 0;
volatile int ev_anykey   = 0;

/* Joystick states, used so one press makes only one 90 degree turn */
volatile int joy_left_down   = 0;
volatile int joy_right_down  = 0;
volatile int joy_up_down     = 0;
volatile int joy_down_down   = 0;
volatile int joy_fire_a_down = 0;
volatile int joy_fire_b_down = 0;

void interrupt (*oldKeyboardISR)(void);


void ClearInputEvents()
{
  disable();

  ev_p1_left=0;
  ev_p1_right=0;
  ev_p2_left=0;
  ev_p2_right=0;
  ev_fire=0;
  ev_pause=0;
  ev_escape=0;
  ev_anykey=0;

  enable();
}


void interrupt GameKeyboardISR(void)
{
  unsigned char sc;
  unsigned char pb;

  /* Read raw keyboard / joystick scan code */
  sc=inp(KBD_DATA_PORT);

  /*
     Amstrad PC1512/PC1640 keyboard acknowledge.
     This is essential; without it the machine can lock after
     the first keyboard/joystick interrupt.
  */
  pb=inp(SYS_PORT_B);
  outp(SYS_PORT_B,pb | 0x80);
  outp(SYS_PORT_B,pb & 0x7F);

  /* ---------- Amstrad joystick BREAK codes ---------- */
  if (sc==AMSTRAD_JOY_LEFT_UP)   joy_left_down=0;
  if (sc==AMSTRAD_JOY_RIGHT_UP)  joy_right_down=0;
  if (sc==AMSTRAD_JOY_UP_UP)     joy_up_down=0;
  if (sc==AMSTRAD_JOY_DOWN_UP)   joy_down_down=0;
  if (sc==AMSTRAD_JOY_FIRE_A_UP) joy_fire_a_down=0;
  if (sc==AMSTRAD_JOY_FIRE_B_UP) joy_fire_b_down=0;

  /* ---------- MAKE codes only ---------- */
  if ((sc & 0x80)==0)
  {
    ev_anykey=1;

    /* Native Amstrad joystick: Player Two */
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

    /* Original keyboard controls */
    else if (sc==0x1E) ev_p1_left++;   /* A */
    else if (sc==0x20) ev_p1_right++;  /* D */
    else if (sc==0x4B) ev_p2_left++;   /* Cursor Left */
    else if (sc==0x4D) ev_p2_right++;  /* Cursor Right */
    else if (sc==0x39) ev_fire=1;      /* Space */
    else if (sc==0x19) ev_pause=1;     /* P */
    else if (sc==0x01) ev_escape=1;    /* Escape */
  }

  /* End IRQ1 */
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

  oldKeyboardISR=getvect(0x09);
  setvect(0x09,GameKeyboardISR);
}


void RestoreKeyboard()
{
  setvect(0x09,oldKeyboardISR);
}


void DrawBlock(int x, int y)
/* x,y: block coordinates */
/* x=0 to 89, y=0 to 57 */
{
  int xCoord;
  int yCoord;

  if (x<0 || y<0 || x>89 || y>57) return;

  /* Transform to graphics coordinates */
  xCoord=x*8;
  yCoord=y*6;

  /* draw block */
  rectangle(xCoord,yCoord,xCoord+7,yCoord+5);

  /* set obstacle marker */
  field[x][y]=1;
}


void InitPlayfield()
{
  int x,y;
  char textP1[25];
  char textP2[25];
  char far * textScore = "S C O R E";

  /* clear playfield array */
  for(x=0;x<90;x++)
   for(y=0;y<58;y++)
    field[x][y]=0;

  /* Draw border */
  rectangle(0,0,719,347);  /* outer border */
  rectangle(7,6,712,18);   /* score area */
  rectangle(7,23,712,342); /* inner border (playfield) */

  /* draw P1 and P2 score */
  /*
  if (p1Score>0)
    for(x=0;x<p1Score;x++)
     line(15+x*4,9,15+x*4,15);
  if (p2Score>0)
    for(x=0;x<p2Score;x++)
      line(704-x*4,9,704-x*4,15);
  */
  if (numberOfPlayers==0)
  {
   sprintf(textP1,"Computer One: %i",p1Score);
   sprintf(textP2,"Computer Two: %i",p2Score);
  }
  else if (numberOfPlayers==1)
  {
   sprintf(textP1,"Computer: %i",p1Score);
   sprintf(textP2,"Human: %i",p2Score);
  }
  else
  {
   sprintf(textP1,"Player One: %i",p1Score);
   sprintf(textP2,"Player Two: %i",p2Score);
  }
  outtextxy(30,9,textP1);
  outtextxy(691-textwidth(textP2),9,textP2);
  outtextxy(360-(textwidth(textScore)/2),9,textScore);

  /* Mark border as obstacles in playfield array */
  for(x=0;x<90;x++)
  {
    field[x][3]=1;
    field[x][57]=1;
  }
  for(y=0;y<58;y++)
  {
    field[0][y]=1;
    field[89][y]=1;
  }

  /* Initiate player positions, directions and crash indicators */
  p1x=15;
  p1y=29;
  p2x=74;
  p2y=29;
  p1dir=0;
  p2dir=180;
  p1Crashed=0;
  p2Crashed=0;

  /* Draw players at their start positions */
  DrawBlock(p1x,p1y);
  DrawBlock(p2x,p2y);
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

  /* Collision detection */
  if (field[p1x][p1y]==1)
  {
    p1Crashed=1;
    p2Score++;
  }
  if (field[p2x][p2y]==1)
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
  if (p1Crashed==0) DrawBlock(p1x,p1y);
  if (p2Crashed==0) DrawBlock(p2x,p2y);

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
  while(field[x+distance][y]==0 && distance<=maxDistanceToCheck)
  {
    distance++;
  }
  return distance;
}

int AICheckDistanceWest(int x,int y,int maxDistanceToCheck)
{
  int distance;
  distance=1;
  while(field[x-distance][y]==0 && distance<=maxDistanceToCheck)
  {
    distance++;
  }
  return distance;
}

int AICheckDistanceNorth(int x,int y,int maxDistanceToCheck)
{
  int distance;
  distance=1;
  while(field[x][y-distance]==0 && distance<=maxDistanceToCheck)
  {
    distance++;
  }
  return distance;
}

int AICheckDistanceSouth(int x,int y,int maxDistanceToCheck)
{
  int distance;
  distance=1;
  while(field[x][y+distance]==0 && distance<=maxDistanceToCheck)
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
  printf("A DOS game for the Hercules graphics card,\n");
  printf("written 2016 by RobertK (RobertK@psc.at), expanded 2026 by M Ralser.\n\n");
  printf("This is the classical overhead view \"Tron\" game from movie of the same title\n");
  printf("(motorcycles that leave \"wall trails\" behind them). If you crash into a wall,\n");
  printf("your opponent scores a point. The player who first reaches a score of %i wins.\n\n",POINTS_TO_WIN);

  printf("Controls:\n");
  printf("P1/P2 turn left/right: A/D and cursor left/right\n");
  printf("(or cursor/Amstrad joystick left/right in one-player mode)\n");
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

  /* Detect the graphics driver and mode */
  detectgraph(&grd,&grm);

  /* initialize the graphics mode with initgraph */
  initgraph(&grd, &grm, "");
  gresult = graphresult();
  if(gresult != grOk)
  {
    printf(grapherrormsg(gresult));
    getch();
    return;
  }

  /* getgraphmode returns the current graphics mode */
  /* grm = getgraphmode(); */
  /* printf("current mode: %d", grm); */
  /* getch(); */

  /* setgraphmode sets the system to graphics mode, clears the screen */
  setgraphmode(HERCMONOHI);

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
	   Install the raw keyboard handler for this round.
	   This is required because the Amstrad joystick does not
	   appear in the BIOS keyboard buffer.
	*/
	InstallGameKeyboard();

	if (numberOfPlayers>0)
	{
	  ClearInputEvents();

	  /* Space, Fire A or Fire B starts the round */
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
	     Process events collected by the IRQ1 handler.
	     The original keyboard controls remain available and
	     Player Two additionally accepts the Amstrad joystick.
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

	    /* Pause until another keyboard/joystick MAKE code arrives */
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

	  /* Sleep for 50 milliseconds.
	     note: the delay() function was introduced in Turbo C 2.0
	     (it is not available in Turbo C 1.0) */
	  delay(50);
        }

	/* Restore normal BIOS keyboard handling after this round */
	RestoreKeyboard();

	/* wait a quarter of a second after a crash has
	   happened before clearing the screen */
	if (p1Crashed || p2Crashed) delay(250);
    }

    cleardevice();
    sprintf(textFinalScore,"Final Score: %i : %i",p1Score,p2Score);
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