/* H-Tron GEM: Open Watcom 16-bit DOS / OpenGEM GEMBIND.
   Original H-Tron by RobertK (2016); GEM adaptation 2026.
   Classic Hercules game rules: one arena, 0/1/2 players, turning trails. */
#include "ppdgem.h"
#ifndef MU_TIMER
#define MU_TIMER 0x0020
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dos.h>
#define GW 64
#define GH 32
#define WIN_KIND (NAME|CLOSER|MOVER|SIZER)
#define TICK_SLOW 160
#define TICK_MEDIUM 120
#define TICK_FAST 100
static WORD tick_ms=TICK_MEDIUM;
static WORD vh,win,wx,wy,ww,wh,deskx,desky,deskw,deskh;
static WORD cellw,cellh,boardx,boardy,gem_char_width=8;
/* OpenGEM indexed color: set VDI palette entry 3 to maroon where supported. */
#define TRON1_COLOR 3
#define TRON2_COLOR 4
#define BORDER_COLOR 8
static WORD maroon_rgb[3]={502,0,0};
static WORD grey_rgb[3]={650,650,650};
static unsigned char field[GH][GW];
static int px[2],py[2],dir[2],alive[2],score[2];
static int players=1,running=0,paused=0,quit=0;
static int match_over=0, demo_wait=0;
/* First to two victories is best-of-three; drawn rounds are replayed. */
static char name[]="H-Tron for GEM";
static char line[90];
/* Native resource-free AES Desk menu, following LMS-GEM. */
static OBJECT menu_tree[10];
static char desk_title[]=" Desk ";
static char about_title[]="  About H-Tron GEM ";
static char hall_title[]="  Hall of Fame    ";
static char separator[]="--------------------";
static char blank[]="                    ";
static void menu_obj(WORD n,WORD next,WORD head,WORD tail,UWORD type,
                     UWORD flags,LPVOID spec,WORD x,WORD y,WORD w,WORD h){
 OBJECT *o=&menu_tree[n];
 o->ob_next=next;o->ob_head=head;o->ob_tail=tail;
 o->ob_type=type;o->ob_flags=flags;o->ob_state=NORMAL;o->ob_spec=spec;
 o->ob_x=x;o->ob_y=y;o->ob_width=w;o->ob_height=h;
}
static void setup_menu(WORD sw,WORD sh){
 WORD i;
 menu_obj(0,-1,1,4,G_IBOX,NONE,0L,0,0,80,25);
 menu_obj(1,4,2,2,G_BOX,NONE,(LPVOID)0x00001100L,0,0,80,0x0201);
 menu_obj(2,1,3,3,G_IBOX,NONE,0L,2,0,15,0x0301);
 menu_obj(3,2,-1,-1,G_TITLE,NONE,desk_title,0,0,6,0x0301);
 menu_obj(4,0,5,5,G_IBOX,NONE,0L,0,0x0301,80,23);
 menu_obj(5,4,6,9,G_BOX,NONE,(LPVOID)0x00ff1100L,2,0,20,4);
 menu_obj(6,7,-1,-1,G_STRING,NONE,about_title,0,0,20,1);
 menu_obj(7,8,-1,-1,G_STRING,NONE,hall_title,0,1,20,1);
 menu_obj(8,9,-1,-1,G_STRING,NONE,separator,0,2,20,1);
 menu_obj(9,5,-1,-1,G_STRING,LASTOB,blank,0,3,20,1);
 menu_tree[8].ob_state=DISABLED;menu_tree[9].ob_state=DISABLED;
 for(i=0;i<10;i++)rsrc_obfix(menu_tree,i);
 menu_tree[0].ob_width=sw;menu_tree[0].ob_height=sh;
 menu_tree[1].ob_width=sw;menu_tree[2].ob_width=sw-menu_tree[2].ob_x*2;
 menu_tree[4].ob_width=sw;menu_tree[4].ob_height=sh-menu_tree[4].ob_y;
}

static int changed=0, lastx[2],lasty[2],lastalive[2];
/* Original DOS scoring: 20/second, 60/turn, 400 for surviving. */
#define HOF_COUNT 4
#define HOF_FILE "HTRONGEM.HOF"
static long points=0, final_points=0;
static int round_ticks=0, entry_mode=0, name_len=0;
static char player_name[31];
struct HofEntry { char name[31]; long score; };
static struct HofEntry hof[HOF_COUNT];
static int hof_count=0;
static void load_hof(void){
 FILE *f=fopen(HOF_FILE,"r"); char buf[100],*sep; int n;
 hof_count=0;if(!f)return;
 while(hof_count<HOF_COUNT&&fgets(buf,sizeof(buf),f)){
  sep=strchr(buf,'|');if(!sep)continue;*sep++=0;
  n=(int)strlen(sep);while(n&&(sep[n-1]=='\n'||sep[n-1]=='\r'))sep[--n]=0;
  if(!sep[0])continue;
  hof[hof_count].score=atol(buf);
  strncpy(hof[hof_count].name,sep,30);hof[hof_count].name[30]=0;
  hof_count++;
 }
 fclose(f);
}
static void save_hof(void){
 FILE *f=fopen(HOF_FILE,"w");int i;if(!f)return;
 for(i=0;i<hof_count;i++)fprintf(f,"%ld|%s\n",hof[i].score,hof[i].name);
 fclose(f);
}
static void redraw(WORD,WORD,WORD,WORD);
static void insert_hof(void);
static void new_match(int);
/* Modal GEM pop-up windows: preserve the game board beneath them. */
static WORD popup= -1;
static WORD popx,popy,popw,poph;
static void popup_text(WORD x,WORD y,const char *str){
 vst_color(vh,1);v_gtext(vh,x,y,(char *)str);
}
/* Two mouse-operable GEM-style actions in the Hall of Fame window. */
static WORD btn_y,btn_h,btn_w,btn_play_x,btn_exit_x;
static void popup_buttons(void){
 WORD a[4];
 btn_h=21;btn_w=(popw-36)/2;
 if(btn_w<45)btn_w=45;
 btn_y=popy+poph-btn_h-9;
 btn_play_x=popx+12;btn_exit_x=popx+popw-btn_w-12;
 a[1]=btn_y;a[3]=btn_y+btn_h;
 vsf_interior(vh,1);vsf_color(vh,8);
 a[0]=btn_play_x;a[2]=btn_play_x+btn_w;v_bar(vh,a);
 a[0]=btn_exit_x;a[2]=btn_exit_x+btn_w;v_bar(vh,a);
 popup_text(btn_play_x+7,btn_y+15,"Play again");
 popup_text(btn_exit_x+7,btn_y+15,"Exit");
}
static void popup_draw(const char *heading,const char *body,int entry){
 WORD a[4],x,y,w,h,clip[4];char scoreline[75];int i;
 if(popup<0)return;
 wind_get(popup,WF_FIRSTXYWH,&x,&y,&w,&h);
 while(w>0&&h>0){
  a[0]=x;a[1]=y;a[2]=x+w-1;a[3]=y+h-1;
  vs_clip(vh,1,a);
  vsf_interior(vh,1);vsf_color(vh,0);
  a[0]=popx;a[1]=popy;a[2]=popx+popw-1;a[3]=popy+poph-1;
  v_bar(vh,a);
  popup_text(popx+12,popy+20,heading);
  if(entry){
   sprintf(scoreline,"Score: %ld",final_points);
   popup_text(popx+12,popy+43,scoreline);
   popup_text(popx+12,popy+65,"Enter name (max 30 chars):");
   sprintf(scoreline,"> %.30s_",player_name);
   popup_text(popx+12,popy+88,scoreline);
   popup_text(popx+12,popy+110,"ENTER saves  ESC cancels");
  }else{
   if(!hof_count)popup_text(popx+12,popy+43,"No scores recorded yet.");
   for(i=0;i<hof_count;i++){
    sprintf(scoreline,"%d. %-20.20s %ld",i+1,hof[i].name,hof[i].score);
    popup_text(popx+12,popy+43+i*21,scoreline);
   }
   popup_buttons();
  }
  wind_get(popup,WF_NEXTXYWH,&x,&y,&w,&h);
 }
 clip[0]=clip[1]=clip[2]=clip[3]=0;vs_clip(vh,0,clip);
}
static int popup_open(char *title){
 WORD x,y,w,h;
 popup=wind_create(NAME|MOVER|CLOSER,deskx,desky,deskw,deskh);
 if(popup<0){form_alert(1,"[3][Cannot create GEM popup][OK]");return 0;}
 wind_set(popup,WF_NAME,FP_OFF(title),FP_SEG(title),0,0);
 w=deskw>390?390:deskw-12;h=deskh>215?215:deskh-12;
 x=deskx+(deskw-w)/2;y=desky+(deskh-h)/2;
 wind_open(popup,x,y,w,h);
 wind_get(popup,WF_WORKXYWH,&popx,&popy,&popw,&poph);
 return 1;
}
static void popup_close(void){
 if(popup>=0){wind_close(popup);wind_delete(popup);popup=-1;}
 redraw(wx,wy,ww,wh);
}
/* Returns 0=closed/cancel, 1=Play again, 2=Exit. */
static int popup_event(char *title,int entry){
 WORD msg[8],ev;UWORD mx,my,mb,ks,key,clicks;
 int c,choice=0;
 if(!popup_open(title))return 0;
 popup_draw(title,"",entry);
 for(;;){
  ev=evnt_multi(MU_MESAG|MU_KEYBD|MU_BUTTON,
    1,1,1,0,0,0,0,0,0,0,0,0,0,msg,0,0,
    &mx,&my,&mb,&ks,&key,&clicks);
  if(ev&MU_MESAG){
   if(msg[0]==WM_REDRAW){
    if(msg[3]==popup)popup_draw(title,"",entry);
    else if(msg[3]==win)redraw(msg[4],msg[5],msg[6],msg[7]);
   }
   if(msg[0]==WM_CLOSED&&msg[3]==popup)break;
   if(msg[0]==WM_TOPPED&&msg[3]==popup)
    wind_set(popup,WF_TOP,0,0,0,0);
  }
  if(ev&MU_BUTTON && !entry){
   if(my>=btn_y&&my<btn_y+btn_h){
    if(mx>=btn_play_x&&mx<btn_play_x+btn_w){choice=1;break;}
    if(mx>=btn_exit_x&&mx<btn_exit_x+btn_w){choice=2;break;}
   }
  }
  if(ev&MU_KEYBD){
   c=key&255;
   if(c==27)break;
   if(c==13){if(entry)insert_hof();else choice=1;break;}
   if(entry){
    if(c==8){if(name_len)player_name[--name_len]=0;}
    else if(c>=32&&c<=126&&c!='|'&&name_len<30){
     player_name[name_len++]=(char)c;player_name[name_len]=0;
    }
    popup_draw(title,"",1);
   }
  }
 }
 popup_close();return choice;
}
static void show_hof(void){
 static char t[]="H-Tron Hall of Fame";
 int choice=popup_event(t,0);
 if(choice==1){new_match(players);redraw(wx,wy,ww,wh);}
 else if(choice==2)quit=1;
}
static void show_info(void){
 form_alert(1,"[1][H-Tron for OpenGEM|Original Hercules DOS game:|RobertK, December 2016|GEM / DOS expansion: 2026|github.com/marralser/H-TRON][OK]");
}
static int qualifies(long n){return n>0&&(hof_count<HOF_COUNT||n>hof[hof_count-1].score);}
static void insert_hof(void){
 int i,j;
 if(!name_len)strcpy(player_name,"PLAYER");
 for(i=0;i<hof_count&&hof[i].score>=final_points;i++);
 if(i>=HOF_COUNT)return;
 if(hof_count<HOF_COUNT)hof_count++;
 for(j=hof_count-1;j>i;j--)hof[j]=hof[j-1];
 strcpy(hof[i].name,player_name);hof[i].score=final_points;
 save_hof();entry_mode=0;
}
static void score_round_end(int won){
 if(players!=1)return;
 /* Award turn and survival bonuses for each round, but never submit
    a Hall of Fame result before the whole best-of-three match ends. */
 points+=60L;
 if(won)points+=400L;
 if(score[0]<2 && score[1]<2)return;
 if(qualifies(points)){
  final_points=points;entry_mode=1;name_len=0;player_name[0]=0;
 }
}

static void bar(WORD x,WORD y,WORD w,WORD h,WORD color){
 WORD a[4];if(w<1||h<1)return;
 a[0]=x;a[1]=y;a[2]=x+w-1;a[3]=y+h-1;
 vsf_interior(vh,1);vsf_color(vh,color);v_bar(vh,a);
}
/* Request RGB #800000 (approximately 502/1000 red) in palette index 3.
   GEM palette support varies by video driver; fixed palettes may ignore it. */
static void configure_colors(void){
 vs_color(vh,TRON1_COLOR,maroon_rgb);
 vs_color(vh,BORDER_COLOR,grey_rgb);
}
/* The excluded outermost grid cells ARE the grey wall.  Unlike a one-pixel
   outline at the outside edge, the visible wall now fills the collision cells,
   so a cycle stops immediately adjacent to it with no apparent gap. */
static void border(void){
 WORD totalw=GW*cellw,totalh=GH*cellh;
 bar(boardx,boardy,totalw,cellh,BORDER_COLOR);
 bar(boardx,boardy+(GH-1)*cellh,totalw,cellh,BORDER_COLOR);
 bar(boardx,boardy+cellh,cellw,totalh-2*cellh,BORDER_COLOR);
 bar(boardx+(GW-1)*cellw,boardy+cellh,cellw,totalh-2*cellh,BORDER_COLOR);
}
static void layout(void){
 cellw=(ww-12)/GW;cellh=(wh-43)/GH;
 if(cellw<1)cellw=1;if(cellh<1)cellh=1;
 boardx=wx+6;boardy=wy+21;
}
static void new_round(void){
 int x,y;
 for(y=0;y<GH;y++)for(x=0;x<GW;x++)field[y][x]=0;
 px[0]=GW/4;py[0]=GH/2;dir[0]=1;
 px[1]=GW*3/4;py[1]=GH/2;dir[1]=3;
 alive[0]=alive[1]=1;
 field[py[0]][px[0]]=1;field[py[1]][px[1]]=2;
 running=1;paused=0;round_ticks=0;entry_mode=0;demo_wait=0;
}
static void new_match(int newplayers){
 players=newplayers;
 score[0]=score[1]=0;
 points=0;
 match_over=0;
 new_round();
}
static int vacant(int x,int y){
 return x>0&&y>0&&x<GW-1&&y<GH-1&&!field[y][x];
}
/* Both CPUs independently choose an open direction with a short lookahead.
   Deterministic and cheap enough for an 8088 / V30. */
static int free_run(int x,int y,int d){
 int n=0;
 while(n<12){
  x+=(d==1)-(d==3);
  y+=(d==2)-(d==0);
  if(!vacant(x,y))break;
  n++;
 }
 return n;
}
static void cpu_turn(int i){
 int d=dir[i],l=(d+3)%4,r=(d+1)%4;
 int x=px[i],y=py[i],f,fl,fr;
 f=free_run(x,y,d);
 fl=free_run(x,y,l);
 fr=free_run(x,y,r);
 if(!f || (f<3 && (fl>f+1 || fr>f+1))){
  if(fl>=fr && fl>0)dir[i]=l;
  else if(fr>0)dir[i]=r;
 }
}
static void tick(void){
 int i,nx[2],ny[2],hit[2];
 changed=0;
 if(!running||paused||entry_mode||match_over)return;
 if(players==1 && ++round_ticks>=1000/tick_ms){points+=20L;round_ticks=0;}
 /* Right cycle (index 1) is the human in solo mode. */
 if(players==0){cpu_turn(0);cpu_turn(1);}
 else if(players==1)cpu_turn(0);
 for(i=0;i<2;i++){
  nx[i]=px[i]+(dir[i]==1)-(dir[i]==3);
  ny[i]=py[i]+(dir[i]==2)-(dir[i]==0);
  hit[i]=!vacant(nx[i],ny[i]);
 }
 if(nx[0]==nx[1]&&ny[0]==ny[1])hit[0]=hit[1]=1;
 for(i=0;i<2;i++){
  lastalive[i]=0;
  if(hit[i])alive[i]=0;
  else {px[i]=nx[i];py[i]=ny[i];field[ny[i]][nx[i]]=(unsigned char)(i+1);
        lastx[i]=nx[i];lasty[i]=ny[i];lastalive[i]=1;changed=1;}
 }
 if(!alive[0]||!alive[1]){
  running=0;
  if(alive[0])score[0]++;
  if(alive[1])score[1]++;
  if(score[0]>=2 || score[1]>=2)match_over=1;
  score_round_end(alive[1]);
  if(players==0)demo_wait=7;
 }
}
static void draw(void){
 int x,y,c;
 bar(wx,wy,ww,wh,0);
 sprintf(line,"TRON 1 %d : %d TRON 2  (best of 3)",score[0],score[1]);
 vst_color(vh,1);v_gtext(vh,wx+6,wy+14,line);

 for(y=0;y<GH;y++)for(x=0;x<GW;x++){
  c=field[y][x];
  if(x==0||y==0||x==GW-1||y==GH-1)continue;
  if(c)bar(boardx+x*cellw,boardy+y*cellh,cellw,cellh,c==1?TRON1_COLOR:TRON2_COLOR);
 }
 border();
 if(entry_mode)strcpy(line,"Match finished - new Hall of Fame entry!");
 else if(match_over)strcpy(line,"Match finished - SPACE for a new match");
 else if(!running)strcpy(line,"SPACE start  0 demo  1 solo  2 duel  H hall  I info");
 else if(paused)strcpy(line,"PAUSED - press P to continue");
 else strcpy(line,"Arrows: right Tron  |  A/D: left Tron  |  P: pause");
 vst_color(vh,1);
 {WORD textx=wx+(ww-(WORD)strlen(line)*gem_char_width)/2;
  if(textx<wx+2)textx=wx+2;
  v_gtext(vh,textx,boardy+GH*cellh+13,line);
 }
}
/* Draw just the two newly occupied cells, respecting GEM's visible rectangles.
   Never erase the background in this routine. */
static void draw_new_cells(void){
 WORD x,y,w,h,a[4],x1,y1,x2,y2;
 int i;
 if(!changed)return;
 wind_get(win,WF_FIRSTXYWH,&x,&y,&w,&h);
 while(w>0&&h>0){
  x1=x>wx?x:wx;y1=y>wy?y:wy;
  x2=x+w<wx+ww?x+w:wx+ww;
  y2=y+h<wy+wh?y+h:wy+wh;
  if(x2>x1&&y2>y1){
   a[0]=x1;a[1]=y1;a[2]=x2-1;a[3]=y2-1;
   vs_clip(vh,1,a);
   for(i=0;i<2;i++)if(lastalive[i])
    bar(boardx+lastx[i]*cellw,boardy+lasty[i]*cellh,
        cellw,cellh,i==0?TRON1_COLOR:TRON2_COLOR);
  }
  wind_get(win,WF_NEXTXYWH,&x,&y,&w,&h);
 }
 a[0]=a[1]=a[2]=a[3]=0;vs_clip(vh,0,a);
}
static void redraw(WORD rx,WORD ry,WORD rw,WORD rh){
 WORD x,y,w,h,a[4],x1,y1,x2,y2;
 wind_get(win,WF_FIRSTXYWH,&x,&y,&w,&h);
 while(w>0&&h>0){
  x1=x>rx?x:rx;y1=y>ry?y:ry;
  x2=x+w<rx+rw?x+w:rx+rw;
  y2=y+h<ry+rh?y+h:ry+rh;
  if(x2>x1&&y2>y1){
   a[0]=x1;a[1]=y1;a[2]=x2-1;a[3]=y2-1;
   vs_clip(vh,1,a);draw();
  }
  wind_get(win,WF_NEXTXYWH,&x,&y,&w,&h);
 }
 a[0]=a[1]=a[2]=a[3]=0;vs_clip(vh,0,a);
}
/* Native GEM three-button startup dialog. Buttons return 1, 2, or 3. */
static void welcome_dialog(void){
 WORD choice,speed;
 choice=form_alert(1,
  "[1][Welcome to HTRON for GEM.|How would you like to play?]"
  "[DEMO|1 Player|2 Players]");
 if(choice<1||choice>3)choice=2;
 speed=form_alert(2,
  "[1][Select Tron movement speed:|Slow for low-resolution GEM screens.]"
  "[Slow|Medium|Fast]");
 if(speed==1)tick_ms=TICK_SLOW;
 else if(speed==3)tick_ms=TICK_FAST;
 else tick_ms=TICK_MEDIUM;
 new_match(choice-1);
 redraw(wx,wy,ww,wh);
}
static void keypress(UWORD key){
 int c=key&255,s=(key>>8)&255;
 if(entry_mode)return;
 if(c=='h'||c=='H'){show_hof();return;}
 if(c=='i'||c=='I'){show_info();return;}
 if(c==27){quit=1;return;}
 if(c==' '){if(match_over)new_match(players);else new_round();return;}
 if(c>='0'&&c<='2'){new_match(c-'0');return;}
 if(c=='p'||c=='P'){paused=!paused;return;}
 if(!running||paused)return;
 if(s==75)dir[1]=(dir[1]+3)%4;
 if(s==77)dir[1]=(dir[1]+1)%4;
 if(players==2){
  if(c=='a'||c=='A')dir[0]=(dir[0]+3)%4;
  if(c=='d'||c=='D')dir[0]=(dir[0]+1)%4;
 }
}
WORD GEMAIN(WORD argc, char *argv[]){
 WORD in[11],out[57],i,event,msg[8],cw,ch,bw,bh;
 UWORD mx,my,buttons,shifts,key,clicks;
 if(appl_init(NULL)<0)return 1;
 vh=graf_handle(&cw,&ch,&bw,&bh);
 gem_char_width=cw>0?cw:8;
 for(i=0;i<10;i++)in[i]=1;in[10]=2;
 v_opnvwk(in,&vh,out);
 configure_colors();
 wind_get(0,WF_WORKXYWH,&deskx,&desky,&deskw,&deskh);
 win=wind_create(WIN_KIND,deskx,desky,deskw,deskh);
 if(win<0){v_clsvwk(vh);appl_exit();return 1;}
 wind_set(win,WF_NAME,FP_OFF(name),FP_SEG(name),0,0);
 wind_open(win,deskx+4,desky+4,deskw-8,deskh-8);
 wind_get(win,WF_WORKXYWH,&wx,&wy,&ww,&wh);
 setup_menu(out[0]+1,out[1]+1);menu_bar(menu_tree,TRUE);
 layout();load_hof();new_match(1);running=0;
 graf_mouse(ARROW,0L);
 redraw(wx,wy,ww,wh);
 welcome_dialog();
 while(!quit){
  event=evnt_multi(MU_MESAG|MU_KEYBD|MU_BUTTON|MU_TIMER,1,1,1,
   0,0,0,0,0,0,0,0,0,0,msg,tick_ms,0,
   &mx,&my,&buttons,&shifts,&key,&clicks);
  if(event&MU_KEYBD){
   int oldrunning=running,oldpaused=paused,oldplayers=players;
   int oldentry=entry_mode;
   keypress(key);
   /* Only repaint when the board or status actually changes. */
   if((key&255)==' ' || (players!=oldplayers) ||
      ((key&255)>='0'&&(key&255)<='2') || paused!=oldpaused ||
      (oldrunning!=running) || oldentry || entry_mode)redraw(wx,wy,ww,wh);
  }

  if(event&MU_MESAG){
   if(msg[0]==MN_SELECTED){
    if(msg[3]==3){if(msg[4]==6)show_info();else if(msg[4]==7)show_hof();}
    menu_tnormal(menu_tree,msg[3],TRUE);
   }
   if(msg[0]==WM_REDRAW)redraw(msg[4],msg[5],msg[6],msg[7]);
   if(msg[0]==WM_CLOSED&&msg[3]==win)quit=1;
   if(msg[0]==WM_TOPPED&&msg[3]==win)wind_set(win,WF_TOP,0,0,0,0);
   if((msg[0]==WM_MOVED||msg[0]==WM_SIZED)&&msg[3]==win){
    wind_set(win,WF_CURRXYWH,msg[4],msg[5],msg[6],msg[7]);
    wind_get(win,WF_WORKXYWH,&wx,&wy,&ww,&wh);
    layout();redraw(wx,wy,ww,wh);
   }
  }
  if(event&MU_TIMER){
   int wasrunning=running;
   tick();
   draw_new_cells();
   /* Demo mode plays both cycles and starts another round automatically.
      Keep the completed board on screen briefly before resetting it. */
   if(players==0 && !running && demo_wait>0){
    demo_wait--;
    if(demo_wait==0){
     if(match_over)new_match(0);
     else new_round();
     redraw(wx,wy,ww,wh);
    }
   }
   /* Refresh the score/status only after a round finishes. */
   if(wasrunning&&!running){
    redraw(wx,wy,ww,wh);
    if(entry_mode){
     static char entry_title[]="H-Tron - New High Score";
     popup_event(entry_title,1);
     entry_mode=0;
     show_hof();
    }
   }
  }
 }
 menu_bar(menu_tree,FALSE);
 wind_close(win);wind_delete(win);v_clsvwk(vh);appl_exit();
 return 0;
}
