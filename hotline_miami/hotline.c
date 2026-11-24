#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {int x,y,alive;} Enemy;
typedef struct {FuriMutex* lock; int x,y,dx,dy,stage,mode,score,weapon,ammo,mask,tick; bool held[4]; Enemy e[6];} Game;
static bool wall(int x,int y){return x<3||x>124||y<13||y>60||(((x>=39&&x<=42)||(x>=83&&x<=86))&& (y<29||y>42));}
static void reset(Game*g){g->x=12;g->y=35;g->dx=1;g->dy=0;g->ammo=8;g->weapon=0;g->tick=0;for(int i=0;i<6;i++){g->e[i]=(Enemy){55+(i%3)*28,19+(i/3)*34,1};}g->mode=1;}
static void person(Canvas*c,int x,int y,int dx,int dy,int phase,bool player){
if(!dx&&!dy)dx=1;
int px=-dy,py=dx;
canvas_draw_line(c,x-dx*3+px*2,y-dy*3+py*2,x-dx*(5+phase)+px*2,y-dy*(5+phase)+py*2);
canvas_draw_line(c,x-dx*3-px*2,y-dy*3-py*2,x-dx*(6-phase)-px*2,y-dy*(6-phase)-py*2);
canvas_draw_box(c,x-3,y-3,7,7);
canvas_draw_line(c,x+px*4,y+py*4,x+dx*4+px*3,y+dy*4+py*3);
canvas_draw_line(c,x-px*4,y-py*4,x+dx*4-px*3,y+dy*4-py*3);
canvas_set_color(c,ColorWhite);canvas_draw_disc(c,x+dx,y+dy,2);
canvas_set_color(c,ColorBlack);canvas_draw_dot(c,x+dx*2,y+dy*2);
if(player){canvas_draw_line(c,x+dx*4,y+dy*4,x+dx*8,y+dy*8);}
}
static void furniture(Canvas*c){
canvas_draw_frame(c,5,15,19,10);canvas_draw_box(c,6,16,4,8);canvas_draw_line(c,11,17,21,17);
canvas_draw_frame(c,48,15,17,9);canvas_draw_line(c,48,19,64,19);
canvas_draw_frame(c,95,15,24,10);canvas_draw_box(c,97,17,5,6);canvas_draw_box(c,112,17,5,6);
canvas_draw_frame(c,7,48,22,10);canvas_draw_frame(c,10,50,16,6);
canvas_draw_frame(c,50,48,19,10);canvas_draw_box(c,52,50,5,6);
canvas_draw_frame(c,104,47,15,11);canvas_draw_disc(c,111,52,3);
for(int x=6;x<125;x+=8)for(int y=27;y<47;y+=8)canvas_draw_dot(c,x,y);
}
static void draw(Canvas*c,void*p){Game*g=p;furi_mutex_acquire(g->lock,FuriWaitForever);canvas_clear(c);canvas_set_font(c,FontSecondary);
if(g->mode!=1){canvas_set_font(c,FontPrimary);canvas_draw_str(c,4,14,"HOTLINE MIAMI");canvas_set_font(c,FontSecondary);canvas_draw_str(c,4,29,g->mode==0?"OK: Start  Left/Right: Mask":g->mode==2?"Paused - OK: Resume":g->mode==3?"Wasted - OK: Retry":"Clear! OK: Next");canvas_draw_str(c,4,43,"Arrows: Move  OK: Attack");canvas_draw_str(c,4,56,"Hold OK: Weapon  Back: Menu");}
else{char s[64];snprintf(s,sizeof(s),"L%d %s %d  P:%d",g->stage,g->weapon?"GUN":"BAT",g->ammo,g->score);canvas_draw_str(c,2,9,s);canvas_draw_frame(c,1,11,126,52);for(int x=39;x<=83;x+=44){canvas_draw_box(c,x,13,4,16);canvas_draw_box(c,x,43,4,18);}furniture(c);person(c,g->x,g->y,g->dx,g->dy,(g->tick/5)%2,true);for(int i=0;i<6;i++){Enemy*e=&g->e[i];if(e->alive){int dx=(g->x>e->x)-(g->x<e->x),dy=(g->y>e->y)-(g->y<e->y);if(abs(g->x-e->x)>abs(g->y-e->y))dy=0;else dx=0;person(c,e->x,e->y,dx,dy,(g->tick/6)%2,false);}else{canvas_draw_frame(c,e->x-4,e->y-2,9,5);canvas_draw_line(c,e->x-5,e->y,e->x+5,e->y);canvas_draw_dot(c,e->x+6,e->y+3);}}}furi_mutex_release(g->lock);}
static void input(InputEvent*e,void*p){furi_message_queue_put(p,e,0);}
static void attack(Game*g){if(g->weapon&&g->ammo<=0)return;if(g->weapon)g->ammo--;int best=-1,dist=999;for(int i=0;i<6;i++){Enemy*e=&g->e[i];int a=e->x-g->x,b=e->y-g->y,d=abs(a)+abs(b);if(!e->alive||d> (g->weapon?65:12))continue;if(g->weapon&&(a*g->dx+b*g->dy<=0||abs(a*g->dy-b*g->dx)>5))continue;bool blocked=false;for(int k=1;k<d;k++)if(wall(g->x+a*k/d,g->y+b*k/d))blocked=true;if(!blocked&&d<dist){best=i;dist=d;}}if(best>=0){g->e[best].alive=0;g->score+=100;}}
int32_t hotline_main(void*ctx){UNUSED(ctx);Game*g=calloc(1,sizeof(Game));g->lock=furi_mutex_alloc(FuriMutexTypeNormal);g->stage=1;FuriMessageQueue*q=furi_message_queue_alloc(16,sizeof(InputEvent));ViewPort*v=view_port_alloc();view_port_draw_callback_set(v,draw,g);view_port_input_callback_set(v,input,q);Gui*gui=furi_record_open(RECORD_GUI);gui_add_view_port(gui,v,GuiLayerFullscreen);bool run=true;while(run){InputEvent e;bool got=furi_message_queue_get(q,&e,33)==FuriStatusOk;furi_mutex_acquire(g->lock,FuriWaitForever);if(got){int k=e.key;if(k>=InputKeyUp&&k<=InputKeyRight){g->held[k]=e.type!=InputTypeRelease;if(e.type==InputTypeShort&&g->mode==0)g->mask^=1;}if(e.type==InputTypeShort&&k==InputKeyBack){if(g->mode==0)run=false;else if(g->mode==1)g->mode=2;else g->mode=0;}if(k==InputKeyOk){if(e.type==InputTypeLong&&g->mode==1)g->weapon^=1;if(e.type==InputTypeShort){if(g->mode==1)attack(g);else if(g->mode==2)g->mode=1;else{if(g->mode==4)g->stage++;reset(g);}}}}if(g->mode==1){int dx=g->held[InputKeyRight]-g->held[InputKeyLeft],dy=g->held[InputKeyDown]-g->held[InputKeyUp];if(dx||dy){g->dx=dx;g->dy=dy;if(dx&&dy)g->dy=0;if(!wall(g->x+dx,g->y))g->x+=dx;if(!wall(g->x,g->y+dy))g->y+=dy;}int alive=0;g->tick++;for(int i=0;i<6;i++){Enemy*a=&g->e[i];if(!a->alive)continue;alive++;int x=g->x-a->x,y=g->y-a->y;if(abs(x)+abs(y)<5)g->mode=3;if(g->tick%4==0&&abs(x)+abs(y)<38){int sx=(x>0)-(x<0),sy=(y>0)-(y<0);if(!wall(a->x+sx,a->y))a->x+=sx;if(!wall(a->x,a->y+sy))a->y+=sy;}}if(!alive)g->mode=4;}furi_mutex_release(g->lock);view_port_update(v);}gui_remove_view_port(gui,v);view_port_free(v);furi_record_close(RECORD_GUI);furi_message_queue_free(q);furi_mutex_free(g->lock);free(g);return 0;}
