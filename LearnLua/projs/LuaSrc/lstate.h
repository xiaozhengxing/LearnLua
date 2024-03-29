/*
** $Id: lstate.h,v 2.133 2016/12/22 13:08:50 roberto Exp $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"

#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*

** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized;
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).

*/


struct lua_longjmp;  /* defined in ldo.c */


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/* extra stack space to handle TM calls and some other extras */
#define EXTRA_STACK   5


#define BASIC_STACK_SIZE        (2*LUA_MINSTACK)


/* kinds of Garbage Collection */
#define KGC_NORMAL	0
#define KGC_EMERGENCY	1	/* gc was forced by an allocation failure */


typedef struct stringtable {//短字符串表,
  TString **hash;//一个TString hash表, 每个元素都链接一个链表,给链表中的字符串所计算出来的hash值都一样,
  int nuse;  /* number of elements */
  int size;
} stringtable;


/*
** Information about a call.
** When a thread yields, 'func' is adjusted to pretend that the
** top function has only the yielded values in its stack; in that
** case, the actual 'func' value is saved in field 'extra'.
** When a function calls another with a continuation, 'extra' keeps
** the function index so that, in case of errors, the continuation
** function can be called with the correct top.
*/
typedef struct CallInfo {
  StkId func;  /* function index in the stack */
  StkId	top;  /* top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Lua functions */
      StkId base;  /* base for this function */
      const Instruction *savedpc;
    } l;
    struct {  /* only for C functions */
      lua_KFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      lua_KContext ctx;  /* context info. in case of yields */
    } c;
  } u;
  ptrdiff_t extra;
  short nresults;  /* expected number of results from this function */
  unsigned short callstatus;
} CallInfo;


/*
** Bits in CallInfo status
*/
#define CIST_OAH	(1<<0)	/* original value of 'allowhook' */
#define CIST_LUA	(1<<1)	/* call is running a Lua function */
#define CIST_HOOKED	(1<<2)	/* call is running a debug hook */
#define CIST_FRESH	(1<<3)	/* call is running on a fresh invocation
                                   of luaV_execute */
#define CIST_YPCALL	(1<<4)	/* call is a yieldable protected call */
#define CIST_TAIL	(1<<5)	/* call was tail called */
#define CIST_HOOKYIELD	(1<<6)	/* last hook called yielded */
#define CIST_LEQ	(1<<7)  /* using __lt for __le */
#define CIST_FIN	(1<<8)  /* call is running a finalizer */

#define isLua(ci)	((ci)->callstatus & CIST_LUA)

/* assume that CIST_OAH has offset 0 and that 'v' is strictly 0/1 */
#define setoah(st,v)	((st) = ((st) & ~CIST_OAH) | (v))
#define getoah(st)	((st) & CIST_OAH)


/*
** 'global state', shared by all threads of this state, 全局状态机,
*/
typedef struct global_State {
  lua_Alloc frealloc;  /* function to reallocate memory */
  void *ud;         /* auxiliary data to 'frealloc' */
  l_mem totalbytes;  /* number of bytes currently allocated - GCdebt */
  l_mem GCdebt;  /* bytes allocated not yet compensated by the collector */
  lu_mem GCmemtrav;  /* memory traversed by the GC */
  lu_mem GCestimate;  /* an estimate of the non-garbage memory in use */
  stringtable strt;  /* hash table for strings, 注意stringtable中只保存短字符串(<40) */
  TValue l_registry;
  unsigned int seed;  /* randomized seed for hashes */
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcrunning;  /* true if GC is running */
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  GCObject *tobefnz;  /* list of userdata to be GC */
  GCObject *fixedgc;  /* list of objects not to be collected, 比如保留字符串对应的TString就会放到这里面(见luaX_init()) */
  struct lua_State *twups;  /* list of threads with open upvalues */
  unsigned int gcfinnum;  /* number of finalizers to call in each GC step */
  int gcpause;  /* size of pause between successive GCs */
  int gcstepmul;  /* GC 'granularity' */
  lua_CFunction panic;  /* to be called in unprotected errors */
  struct lua_State *mainthread;
  const lua_Number *version;  /* pointer to version number */
  TString *memerrmsg;  /* memory-error message */
  TString *tmname[TM_N];  /* array with tag-method names,存放"__index"等字符串{见 luaT_eventname} */
  struct Table *mt[LUA_NUMTAGS];  /* metatables for basic types, 除了UserData和Table, 对于其他类型, 每个类型共用一个global元表 */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API,一个快速的缓存,新建字符串(长或短)时会现在这里面找,找不到则新建字符串,并将字符串更新到strcache中 */
} global_State;


/*
** 'per thread' state
* lua_State.tt = LUA_TTHREAD
*/
struct lua_State {
  CommonHeader;
  
  lu_byte status;

  global_State *l_G;//全局状态机,

  unsigned short nci;  /* number of items in 'ci' list, callinfo双向链表,一共有多少个callInfo */
  CallInfo base_ci;  /* CallInfo for first level (C calling Lua) */
  CallInfo *ci;  /* call info for current function, 当前运行函数 */

  StkId top;  /* first free slot in the stack, L->top指向的是当前空闲位置 */
  StkId stack_last;  /* last free slot in the stack */
  StkId stack;  /* stack base, 栈底位置? */
  
  const Instruction *oldpc;  /* last pc traced */
  UpVal *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  struct lua_State *twups;  /* list of threads with open upvalues */
  struct lua_longjmp *errorJmp;  /* current error recover point, 当前执行的luaD_rawrunprotected中所在的jmp_buf点 */

  /*hook相关, 服务于debug模块*/
  volatile lua_Hook hook;
  ptrdiff_t errfunc;  /* current error handling function (stack index) */
  int stacksize; /*当前栈空间大小?*/
  int basehookcount;
  int hookcount;
  l_signalT hookmask;
  lu_byte allowhook;
  
  unsigned short nny;  /* number of non-yieldable calls in stack */
  unsigned short nCcalls;  /* number of nested C calls */
};


#define G(L)	(L->l_G)


/*
** Union of all collectable objects (only for conversions)
*/
union GCUnion {
  GCObject gc;  /* common header */
  struct TString ts;
  struct Udata u;
  union Closure cl;
  struct Table h;
  struct Proto p;
  struct lua_State th;  /* thread */
};

//o(类型为GCObject*),将其类型强转为 GCUnion*
#define cast_u(o)	cast(union GCUnion *, (o))

/* macros to convert a GCObject into a specific value */

//取o中的TString地址,
//o(类型为GCObject*, 保存内容为string),将其类型强转为 GCUnion*,并取GCUnion.ts(类型为TString)的地址,
#define gco2ts(o)  \
	check_exp(novariant((o)->tt) == LUA_TSTRING, &((cast_u(o))->ts))

//取o中的UData地址,
//o(类型为GCObject*, 保存内容为UserData),将其类型强转为 GCUnion*,并取GCUnion.u(类型为Udata)的地址,
#define gco2u(o)  check_exp((o)->tt == LUA_TUSERDATA, &((cast_u(o))->u))

//取o中的LClosure(lua closure)地址,
//o(类型为GCObject*, 保存内容为Closure),将其类型强转为 GCUnion*,并取GCUnion.cl.l(类型为Closure.LClosure)的地址,
#define gco2lcl(o)  check_exp((o)->tt == LUA_TLCL, &((cast_u(o))->cl.l))

//取o中的CClosure(c closure)地址,
//o(类型为GCObject*, 保存内容为Closure),将其类型强转为 GCUnion*,并取GCUnion.cl.c(类型为Closure.CClosure)的地址,
#define gco2ccl(o)  check_exp((o)->tt == LUA_TCCL, &((cast_u(o))->cl.c))

//取o中的Closure(也是个union,包含 LClosure和CClosure)地址,
//o(类型为GCObject*, 保存内容为Closure),将其类型强转为 GCUnion*,并取GCUnion.cl(类型为Closure)的地址,
#define gco2cl(o)  \
	check_exp(novariant((o)->tt) == LUA_TFUNCTION, &((cast_u(o))->cl))

//取o中的Table地址
//o(类型为GCObject*, 保存内容为Table),将其类型强转为 GCUnion*,并取GCUnion.h(类型为Table)的地址,
#define gco2t(o)  check_exp((o)->tt == LUA_TTABLE, &((cast_u(o))->h))

#define gco2p(o)  check_exp((o)->tt == LUA_TPROTO, &((cast_u(o))->p))

//
#define gco2th(o)  check_exp((o)->tt == LUA_TTHREAD, &((cast_u(o))->th))


/* macro to convert a Lua object into a GCObject
 * 将v(类型为指针)强转为Union,并取Union.gc的地址,可以看做将v变换一下,返回v对应的GCObject指针,
 */
#define obj2gco(v) \
	check_exp(novariant((v)->tt) < LUA_TDEADKEY, (&(cast_u(v)->gc)))


/* actual number of total bytes allocated */
#define gettotalbytes(g)	cast(lu_mem, (g)->totalbytes + (g)->GCdebt)

LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_freeCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);


#endif

