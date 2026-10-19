#ifndef GEM_COMPAT_H
#define GEM_COMPAT_H

/* Minimal PC GEM AES/VDI declarations.  They deliberately use traditional
   unprototyped declarations because the OpenGEM SDK bindings differ slightly
   between Pacific C, DRI C and Open Watcom. */
typedef int WORD;
typedef unsigned int UWORD;

#define MU_KEYBD  0x0001
#define MU_BUTTON 0x0002
#define MU_MESAG  0x0010
#define MU_TIMER  0x0020 /* GEM AES timer event */

#define NAME   0x0001
#define CLOSER 0x0002
#define FULLER 0x0004
#define MOVER  0x0008
#define SIZER  0x0020

#define WF_NAME       2
#define WF_WORKXYWH   4
#define WF_CURRXYWH   5
#define WF_FULLXYWH   7
#define WF_TOP       10
#define WF_FIRSTXYWH 11
#define WF_NEXTXYWH  12

#define WM_REDRAW 20
#define WM_TOPPED 21
#define WM_CLOSED 22
#define WM_FULLED 23
#define WM_SIZED  27
#define WM_MOVED  28

#define ARROW 0

extern WORD appl_init();
extern WORD appl_exit();
extern WORD graf_handle();
extern WORD graf_mouse();
extern WORD v_opnvwk();
extern WORD v_clsvwk();
extern WORD wind_create();
extern WORD wind_open();
extern WORD wind_close();
extern WORD wind_delete();
extern WORD wind_get();
extern WORD wind_set();
extern WORD wind_update();
extern WORD evnt_multi();
extern WORD fsel_input();
extern WORD fsel_exinput();
extern WORD form_alert();
extern WORD vs_clip();
extern WORD vsf_interior();
extern WORD vsf_color();
extern WORD vs_color();
extern WORD vsl_width();
extern WORD vsl_color();
extern WORD vst_color();
extern WORD vst_height();
extern WORD vst_point();
extern WORD vst_effects();
extern WORD v_bar();
extern WORD v_pline();
extern WORD v_gtext();

#endif
