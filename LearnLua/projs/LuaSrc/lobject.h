/*
** $Id: lobject.h,v 2.117 2016/08/01 19:51:24 roberto Exp $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/*
** Extra tags for non-values
*/
#define LUA_TPROTO	LUA_NUMTAGS		/* function prototypes */
#define LUA_TDEADKEY	(LUA_NUMTAGS+1)		/* removed keys in tables */

/*
** number of all possible tags (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTAGS	(LUA_TPROTO + 2)


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* value)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/


/*
** LUA_TFUNCTION variants:
** 0 - Lua function
** 1 - light C function
** 2 - regular C function (closure)
*/

/* Variant tags for functions */
#define LUA_TLCL	(LUA_TFUNCTION | (0 << 4))  /* Lua closure */
#define LUA_TLCF	(LUA_TFUNCTION | (1 << 4))  /* light C function */
#define LUA_TCCL	(LUA_TFUNCTION | (2 << 4))  /* C closure */


/* Variant tags for strings */
#define LUA_TSHRSTR	(LUA_TSTRING | (0 << 4))  /* short strings */
#define LUA_TLNGSTR	(LUA_TSTRING | (1 << 4))  /* long strings */


/* Variant tags for numbers */
#define LUA_TNUMFLT	(LUA_TNUMBER | (0 << 4))  /* float numbers */
#define LUA_TNUMINT	(LUA_TNUMBER | (1 << 4))  /* integer numbers */


/* Bit mark for collectable types
 * 标志collectable tag
 */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable,
 * 设置collectable tag为1, 返回(t | 1<<6)
 */
#define ctb(t)			((t) | BIT_ISCOLLECTABLE)


/*
** Common type for all collectable objects
*/
typedef struct GCObject GCObject;


/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
* marked用来就是gc标记清除法中用来设置颜色的,
*/
#define CommonHeader	GCObject *next; lu_byte tt; lu_byte marked


/*
** Common type has only the common header
*/
struct GCObject {
  CommonHeader;
};




/*
** Tagged Values. This is the basic representation of values in Lua,
** an actual value plus a tag with its type.
*/

/*
** Union of all Lua values
*/
typedef union Value {
  GCObject *gc;    /* collectable objects */
  void *p;         /* light userdata */
  int b;           /* booleans */
  lua_CFunction f; /* light C functions */
  lua_Integer i;   /* integer numbers */
  lua_Number n;    /* float numbers */
} Value;


#define TValuefields	Value value_; int tt_


typedef struct lua_TValue {
  TValuefields;
} TValue;



/* macro defining a nil value */
#define NILCONSTANT	{NULL}, LUA_TNIL

//取TValue.value_(类型为Value, 是个Union)
//o:类型为TValue*
#define val_(o)		((o)->value_)


/* raw type tag of a TValue
 * o:类型为TValue*,  返回TValue.tt_) {bits 0-7}
 */
#define rttype(o)	((o)->tt_)

/* tag with no variants (bits 0-3)
 * x为tag值,返回其actual tag(bits 0-3,最低的四个bit)
 */
#define novariant(x)	((x) & 0x0F)

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5)
 * o(类型为TValue*),返回TValue.tt_中的 bits 0-5(variant tag + actual tag)
 */
#define ttype(o)	(rttype(o) & 0x3F)

/* type tag of a TValue with no variants (bits 0-3)
 * 返回actual tag(bits 0-3, 最低的四个bit)
 * o:类型为TValue*
 */
#define ttnov(o)	(novariant(rttype(o)))


/* Macros to test type */
//判断"o.tt_ == t"是否成立{包含所有tag,包括collectable tag}, o类型为 TValue*
#define checktag(o,t)		(rttype(o) == (t))

//判断o(类型为TValue*)的actual tag(bits 0-3)是不是等于t
#define checktype(o,t)		(ttnov(o) == (t))

//判断o(类型为TValue*)是不是数字(integer/float)
#define ttisnumber(o)		checktype((o), LUA_TNUMBER)

//判断o(类型为TValue*)是不是 float{float并不需要GC, collectable tag为0,直接取LUA_TNUMFLT进行对比就行了}
#define ttisfloat(o)		checktag((o), LUA_TNUMFLT)

//判断o(类型为TValue*)是不是 integer{integer并不需要GC, collectable tag为0,直接取LUA_TNUMINT进行对比就行了}
#define ttisinteger(o)		checktag((o), LUA_TNUMINT)

//判断o(类型为TValue*)是不是 nil
#define ttisnil(o)		checktag((o), LUA_TNIL)

//判断o(类型为TValue*)是不是 boolean
#define ttisboolean(o)		checktag((o), LUA_TBOOLEAN)

//判断o(类型为TValue*)是不是 lightUserData{lightUserData并不需要GC, collectable tag为0,直接取LUA_TLIGHTUSERDATA进行对比就行了}
#define ttislightuserdata(o)	checktag((o), LUA_TLIGHTUSERDATA)

//判断o(类型为TValue*)是不是string
#define ttisstring(o)		checktype((o), LUA_TSTRING)

#define ttisshrstring(o)	checktag((o), ctb(LUA_TSHRSTR))
#define ttislngstring(o)	checktag((o), ctb(LUA_TLNGSTR))

//判断o(类型为TValue*)是不是Table
#define ttistable(o)		checktag((o), ctb(LUA_TTABLE))


#define ttisfunction(o)		checktype(o, LUA_TFUNCTION)
#define ttisclosure(o)		((rttype(o) & 0x1F) == LUA_TFUNCTION)

//判断o(类型为TValue*)是不是 C Closure(collectable tag为1)
#define ttisCclosure(o)		checktag((o), ctb(LUA_TCCL))

//判断o(类型为TValue*)是不是 Lua Closure(collectable tag为1)
#define ttisLclosure(o)		checktag((o), ctb(LUA_TLCL))

//判断o(类型为TValue*)是不是 light C function{light c function并不需要GC, collectable tag为0,直接取LUA_TLCF进行对比就行了}
#define ttislcf(o)		checktag((o), LUA_TLCF)

//判断o(类型为TValue*)是不是UserData(collectable tag为1)
#define ttisfulluserdata(o)	checktag((o), ctb(LUA_TUSERDATA))

//判断o(类型为TValue*)是不是Thread(lua_State, collectable tag为1)
#define ttisthread(o)		checktag((o), ctb(LUA_TTHREAD))


#define ttisdeadkey(o)		checktag((o), LUA_TDEADKEY)


/* Macros to access values */

//取o(类型为TValue*)中保存的integer整数值,
#define ivalue(o)	check_exp(ttisinteger(o), val_(o).i)

//取o(类型为TValue*)中保存的float浮点数值,
#define fltvalue(o)	check_exp(ttisfloat(o), val_(o).n)

//取o(类型为TValue*)中保存的数值(integer/float),将其转为double并返回,
#define nvalue(o)	check_exp(ttisnumber(o), \
	(ttisinteger(o) ? cast_num(ivalue(o)) : fltvalue(o)))

//取o(类型为TValue*)中保存的"GCObject*"变量gc, 会判断该TValue.tt_的collectable tag是否为1
#define gcvalue(o)	check_exp(iscollectable(o), val_(o).gc)

//取o(类型为TValue*)中保存的"void *"变量p,即 light userdata
#define pvalue(o)	check_exp(ttislightuserdata(o), val_(o).p)

//取o(类型为TValue*)中保存的字符串TString*
#define tsvalue(o)	check_exp(ttisstring(o), gco2ts(val_(o).gc))

//取o(类型为TValue*)中保存的UData* (userdata)
#define uvalue(o)	check_exp(ttisfulluserdata(o), gco2u(val_(o).gc))


#define clvalue(o)	check_exp(ttisclosure(o), gco2cl(val_(o).gc))

//取o(类型为TValue*)中的LClosure(lua closure)地址,
#define clLvalue(o)	check_exp(ttisLclosure(o), gco2lcl(val_(o).gc))

//取o(类型为TValue*)中的CClosure(c closure)地址,
#define clCvalue(o)	check_exp(ttisCclosure(o), gco2ccl(val_(o).gc))

//取o(类型为TValue*)中保存的lua_CFunction(light c function)
#define fvalue(o)	check_exp(ttislcf(o), val_(o).f)

//取o(类型为TValue*)中保存的Table*
#define hvalue(o)	check_exp(ttistable(o), gco2t(val_(o).gc))

//取o(类型为TValue*)中保存的boolean值,
#define bvalue(o)	check_exp(ttisboolean(o), val_(o).b)

//取o(类型为TValue*)中保存的Thread*(也就是lua_State)
#define thvalue(o)	check_exp(ttisthread(o), gco2th(val_(o).gc))

/* a dead value may get the 'gc' field, but cannot access its contents */
#define deadvalue(o)	check_exp(ttisdeadkey(o), cast(void *, val_(o).gc))

//判断o(类型为TValue*)中保存的值是否为false(或者为nil),注意,是false返回1,否则返回0
#define l_isfalse(o)	(ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0))

//判断o(类型为TValue*)的tag中的collectable tag是否为1
#define iscollectable(o)	(rttype(o) & BIT_ISCOLLECTABLE)


/* Macros for internal tests
 * 判断obj(类型为TValue*).tt_中的bits 0-5(variant tag + actual tag)和其保存的GCObject*gc中的GCObject.tt是否一致,
 */
#define righttt(obj)		(ttype(obj) == gcvalue(obj)->tt)

//obj(类型为TValue*),一些正常的检测操作,如果是gc变量,会检测一下GCObject.tt和TValue.tt_之间是否对应等,
#define checkliveness(L,obj) \
	lua_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj)))))


/* Macros to set values
 * 赋值 o->tt_ = t
 * o:TValue*类型
 * t:整数
 */
#define settt_(o,t)	((o)->tt_=(t))

//使obj(类型为TValue*)中保存浮点数值x,并将其TValue.tt_赋值为LUA_TNUMFLT{bits 0-6, 因为无需gc,所以collectable tag为0}
#define setfltvalue(obj,x) \
  { TValue *io=(obj); val_(io).n=(x); settt_(io, LUA_TNUMFLT); }

#define chgfltvalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisfloat(io)); val_(io).n=(x); }

//使obj(类型为TValue*)中保存整型值x,并将其TValue.tt_赋值为LUA_TNUMINT{bits 0-6, 因为无需gc,所以collectable tag为0}
#define setivalue(obj,x) \
  { TValue *io=(obj); val_(io).i=(x); settt_(io, LUA_TNUMINT); }

#define chgivalue(obj,x) \
  { TValue *io=(obj); lua_assert(ttisinteger(io)); val_(io).i=(x); }

//设置obj(类型为TValue*)的标记为nil
#define setnilvalue(obj) settt_(obj, LUA_TNIL)

#define setfvalue(obj,x) \
  { TValue *io=(obj); val_(io).f=(x); settt_(io, LUA_TLCF); }

//设置obj(类型为TValue*)的标记为light userdata,并保存值x(void*)
#define setpvalue(obj,x) \
  { TValue *io=(obj); val_(io).p=(x); settt_(io, LUA_TLIGHTUSERDATA); }

//设置obj(类型为TValue*)中保存bool值x
#define setbvalue(obj,x) \
  { TValue *io=(obj); val_(io).b=(x); settt_(io, LUA_TBOOLEAN); }

/*
 * 将x(GCObject*类型)的值copy给obj.gc,且设置obj的tag为(1<<6 | x->tt),即添加collectable tag, 表示需回收
 * obj: TValue*类型
 * x: GCObject*类型
 */
#define setgcovalue(L,obj,x) \
  { TValue *io = (obj); GCObject *i_g=(x); \
    val_(io).gc = i_g; settt_(io, ctb(i_g->tt)); }

/* 将x(TString*类型)转成GCObject*,赋值给obj.gc,且设置obj的tag为(1<<6 | x->tt),即添加collectable tag, 表示需回收
 * obj:TValue*类型
 * x:TString*类型
 */
#define setsvalue(L,obj,x) \
  { TValue *io = (obj); TString *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(x_->tt)); \
    checkliveness(L,io); }

#define setuvalue(L,obj,x) \
  { TValue *io = (obj); Udata *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TUSERDATA)); \
    checkliveness(L,io); }

/*
 *将x(Thread*类型,即lua_State*类型)转成GCObject*,赋值给obj.gc, 且设置obj.tag为(1<<6 | LUA_TTHREAD),即添加collectable tag,表示需回收
 *obj: TValue*类型
 *x:Thread*类型,即lua_State*类型,
 */
#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTHREAD)); \
    checkliveness(L,io); }

#define setclLvalue(L,obj,x) \
  { TValue *io = (obj); LClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TLCL)); \
    checkliveness(L,io); }

/*
 * 将x的值copy给obj,且设置obj的tag为(1<<6 | LUA_TCCL),即添加collectable tag, 表示需回收
 * obj: TValue*类型
 * x: CClosure*类型(C函数)
 */
#define setclCvalue(L,obj,x) \
  { TValue *io = (obj); CClosure *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TCCL)); \
    checkliveness(L,io); }

#define sethvalue(L,obj,x) \
  { TValue *io = (obj); Table *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_TTABLE)); \
    checkliveness(L,io); }

#define setdeadvalue(obj)	settt_(obj, LUA_TDEADKEY)


/*
 * 赋值 obj1 = obj2
 * obj1: TValue*类型
 * obj2: TValue*类型
 */
#define setobj(L,obj1,obj2) \
	{ TValue *io1=(obj1); *io1 = *(obj2); \
	  (void)L; checkliveness(L,io1); }


/*
** different types of assignments, according to destination
*/

/* from stack to (same) stack */
#define setobjs2s	setobj
/* to stack (not from same stack) */
#define setobj2s	setobj

#define setsvalue2s	setsvalue

#define sethvalue2s	sethvalue
#define setptvalue2s	setptvalue
/* from table to same table */
#define setobjt2t	setobj
/* to new object */
#define setobj2n	setobj
#define setsvalue2n	setsvalue

/* to table (define it as an expression to be used in macros) */
#define setobj2t(L,o1,o2)  ((void)L, *(o1)=*(o2), checkliveness(L,(o1)))




/*
** {======================================================
** types and prototypes
** =======================================================
*/


typedef TValue *StkId;  /* index to stack elements */




/*
** Header for string value; string bytes follow the end of this structure
** (aligned according to 'UTString'; see next).
* 这个只是string的头部,TString中所保存的字符串内容的具体地址为 (TString*) + sizeof(UTString)
*/
typedef struct TString {
  CommonHeader;
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs, 短字符串时 >0表示对应的保留字index, ==0表示一般的短字符串;长字符串时:值为1表示hash值已计算,为0表示还未计算 */
  lu_byte shrlen;  /* length for short strings */
  unsigned int hash;//长字符串时,该值的初始值为G->seed{参见luaS_newlstr()},此时extra=0表示还未计算hash值;短字符串时,如果不是保留字,则在创建的时候就会赋值hash
  union {
    size_t lnglen;  /* length for long strings */
    struct TString *hnext;  /* linked list for hash table,短字符串时,TString会存在g->stringtable中, hnext指向下一个TString, */
  } u;
} TString;


/*
** Ensures that address after this type is always fully aligned.
* TString中所保存的字符串内容的具体地址为 (TString*) + sizeof(UTString)
*/
typedef union UTString {
  L_Umaxalign dummy;  /* ensures maximum alignment for strings */
  TString tsv;
} UTString;


/*
** Get the actual string (array of bytes) from a 'TString'.
** (Access to 'extra' ensures that value is really a 'TString'. 这里访问ts->extra只是为了编译通过,用来确保ts的类型为TString*)
* 返回ts(类型为TString*)中存储字符串的首字符地址(类型为char*),
*/
#define getstr(ts)  \
  check_exp(sizeof((ts)->extra), cast(char *, (ts)) + sizeof(UTString))


/* get the actual string (array of bytes) from a Lua value
 * 取o(类型为TValue*)中保存的字符串的首字符地址(类型为char*)
 */
#define svalue(o)       getstr(tsvalue(o))

/* get string length from 'TString *s'
 * 返回s(类型为TString*)中保存的短/长字符串的长度,
 */
#define tsslen(s)	((s)->tt == LUA_TSHRSTR ? (s)->shrlen : (s)->u.lnglen)

/* get string length from 'TValue *o'
 * 返回o(类型为TValue*)中保存的短/长字符串的长度,
 */
#define vslen(o)	tsslen(tsvalue(o))


/*
** Header for userdata; memory area follows the end of this structure
** (aligned according to 'UUdata'; see next).
* 这个只是Udata的头部, 通过宏getudatamem可知, Udata u实际数据保存的地址为 (char*)u + sizeof(UUdata),
*/
typedef struct Udata {
  CommonHeader;
  lu_byte ttuv_;  /* user value's tag */
  struct Table *metatable;//Udata和table都有属于自己的metatable
  size_t len;  /* number of bytes, 保存数据的大小,不包含头部 */
  union Value user_;  /* user value, 目前还不知道这个Value是用来存储什么的,因为数据是保存在getudatamem地址的 */
} Udata;


/*
** Ensures that address after this type is always fully aligned.
*/
typedef union UUdata {
  L_Umaxalign dummy;  /* ensures maximum alignment for 'local' udata */
  Udata uv;
} UUdata;


/*
**  Get the address of memory block inside 'Udata'.
** (Access to 'ttuv_' ensures that value is really a 'Udata'. 调用 u->ttuv_只是为了确定u的类型是Udata*)
* 取U(类型为Udata*, userdata)中保存的数据, 看样子保存的地址是 (char*)u + sizeof(UUdata)
*/
#define getudatamem(u)  \
  check_exp(sizeof((u)->ttuv_), (cast(char*, (u)) + sizeof(UUdata)))

/*
 * 将o(类型为TValue*)的Value值赋给u(类型为Udata*),并赋值tag
 */
#define setuservalue(L,u,o) \
	{ const TValue *io=(o); Udata *iu = (u); \
	  iu->user_ = io->value_; iu->ttuv_ = rttype(io); \
	  checkliveness(L,io); }

/*
 * 将U(类型为Udata*)的Value值赋给O(类型为TValue*),并赋值tag
 */
#define getuservalue(L,u,o) \
	{ TValue *io=(o); const Udata *iu = (u); \
	  io->value_ = iu->user_; settt_(io, iu->ttuv_); \
	  checkliveness(L,io); }


/*
** Description of an upvalue for function prototypes,仅在编译的时候使用到,
*/
typedef struct Upvaldesc {
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
} Upvaldesc;


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
typedef struct LocVar {
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */
} LocVar;


/*
** Function Prototypes
*/
typedef struct Proto {
  CommonHeader;
  lu_byte numparams;  /* number of fixed parameters */
  lu_byte is_vararg;
  lu_byte maxstacksize;  /* number of registers needed by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of 'p' */
  int sizelocvars;
  int linedefined;  /* debug information  */
  int lastlinedefined;  /* debug information  */
  TValue *k;  /* constants used by the function */
  Instruction *code;  /* opcodes */
  struct Proto **p;  /* functions defined inside the function */
  int *lineinfo;  /* map from opcodes to source lines (debug information) */
  LocVar *locvars;  /* information about local variables (debug information) */
  Upvaldesc *upvalues;  /* upvalue information */
  struct LClosure *cache;  /* last-created closure with this prototype */
  TString  *source;  /* used for debug information */
  GCObject *gclist;
} Proto;



/*
** Lua Upvalues
*/
typedef struct UpVal UpVal;


/*
** Closures
*/

#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist//nupvalues表示upvalue的个数,

//c closure
typedef struct CClosure {
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues, 其实是数组,保存Upvalue */
} CClosure;

//lua closure
typedef struct LClosure {
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */
} LClosure;

//包含 c closure 和 lua closure
typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;

//判断o(类型为TValue*)是不是 Lua Closure(collectable tag为1)
#define isLfunction(o)	ttisLclosure(o)

//取o(类型为TValue*)中的LClosure.Proto* p
#define getproto(o)	(clLvalue(o)->p)


/*
** Tables
*/

typedef union TKey {
  struct {
    TValuefields;//TValue
    int next;  /* for chaining (offset for next node) */
  } nk;
  TValue tvk;
} TKey;


/* copy a value into a key without messing up field 'next'
 * 将obj(类型为TValue*)的值赋给key(类型为TKey)
 */
#define setnodekey(L,key,obj) \
	{ TKey *k_=(key); const TValue *io_=(obj); \
	  k_->nk.value_ = io_->value_; k_->nk.tt_ = io_->tt_; \
	  (void)L; checkliveness(L,io_); }


typedef struct Node {
  TValue i_val;
  TKey i_key;
} Node;


typedef struct Table {
  CommonHeader;
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;  /* log2 of size of 'node' array */
  unsigned int sizearray;  /* size of 'array' array */
  TValue *array;  /* array part */
  Node *node;
  Node *lastfree;  /* any free position is before this position */
  struct Table *metatable;
  GCObject *gclist;
} Table;



/*
** 'module' operation for hashing (size is always a power of 2)
* 按位与操作, size一般为2的n次幂的值(size - 1)后bit全是1,
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast(int, (s) & ((size)-1)))))


#define twoto(x)	(1<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


/*
** (address of) a fixed nil value
* 一个为nil的TValue变量的地址, 类型为 TValue*
*/
#define luaO_nilobject		(&luaO_nilobject_)


LUAI_DDEC const TValue luaO_nilobject_;

/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8

LUAI_FUNC int luaO_int2fb (unsigned int x);
LUAI_FUNC int luaO_fb2int (int x);
LUAI_FUNC int luaO_utf8esc (char *buff, unsigned long x);
LUAI_FUNC int luaO_ceillog2 (unsigned int x);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, TValue *res);
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC int luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, StkId obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t len);


#endif

