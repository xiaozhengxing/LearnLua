/*
** $Id: lstring.h,v 1.61 2015/11/03 15:36:01 roberto Exp $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include "lgc.h"
#include "lobject.h"
#include "lstate.h"


#define sizelstring(l)  (sizeof(union UTString) + ((l) + 1) * sizeof(char))

#define sizeludata(l)	(sizeof(union UUdata) + (l))
#define sizeudata(u)	sizeludata((u)->len)

#define luaS_newliteral(L, s)	(luaS_newlstr(L, "" s, \
                                 (sizeof(s)/sizeof(char))-1))


/*
** test whether a string is a reserved word
*/
#define isreserved(s)	((s)->tt == LUA_TSHRSTR && (s)->extra > 0)


/*
** equality for short strings, which are always internalized
* 对比短字符串a,b是否相等,
*/
#define eqshrstr(a,b)	check_exp((a)->tt == LUA_TSHRSTR, (a) == (b))

//计算字符串的hash值,
LUAI_FUNC unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);

//返回长字符串的hash值{如果还未计算,就先计算一次}
LUAI_FUNC unsigned int luaS_hashlongstr (TString *ts);

//a,b均为长字符串,对比其是否相等,
LUAI_FUNC int luaS_eqlngstr (TString *a, TString *b);

//给g->stringTable(保存的是短字符串)重新设置大小(扩容或者缩容),并更新hash数组各自对应的链表,
LUAI_FUNC void luaS_resize (lua_State *L, int newsize);

//清除G->strcache中保存的字符串(长或短),需要是白色的(包含两种白),
LUAI_FUNC void luaS_clearcache (global_State *g);

//初始化G->strt(string table)和G->strcache
LUAI_FUNC void luaS_init (lua_State *L);

//将字符串ts(短字符串)从G->strt(stringtable)中移除,
LUAI_FUNC void luaS_remove (lua_State *L, TString *ts);

//新建Udata, s为需要保存的数据大小(不包含udata头部所占空间)
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s);

//创建字符串(短或长)
LUAI_FUNC TString *luaS_newlstr (lua_State *L, const char *str, size_t l);

//使用str(char*)来构建TString,先在strcache里面找,找不到则调用 luaS_newlstr新建字符串,并且将新建的字符串更新到strcache中,
LUAI_FUNC TString *luaS_new (lua_State *L, const char *str);

//创建长字符串,默认不会计算hash值,初始hash值为G->seed
LUAI_FUNC TString *luaS_createlngstrobj (lua_State *L, size_t l);


#endif
