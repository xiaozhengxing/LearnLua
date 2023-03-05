/*
** $Id: lstring.c,v 2.56 2015/11/23 11:32:51 roberto Exp $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


#define MEMERRMSG       "not enough memory"


/*
** Lua will use at most ~(2^LUAI_HASHLIMIT) bytes from a string to
** compute its hash
*/
#if !defined(LUAI_HASHLIMIT)
#define LUAI_HASHLIMIT		5
#endif


/*
** equality for long strings
* a,b均为长字符串,对比其是否相等,
*/
int luaS_eqlngstr (TString *a, TString *b) {
  size_t len = a->u.lnglen;
  lua_assert(a->tt == LUA_TLNGSTR && b->tt == LUA_TLNGSTR);
  return (a == b) ||  /* same instance or... */
    ((len == b->u.lnglen) &&  /* equal length and ... */
     (memcmp(getstr(a), getstr(b), len) == 0));  /* equal contents */
}

//计算字符串的hash值,
unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ cast(unsigned int, l);
  size_t step = (l >> LUAI_HASHLIMIT) + 1;//注意这里有个步长,并不会每个字符都去会计算,不同字符串也可能会相同的hash值,
  for (; l >= step; l -= step)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}

//返回长字符串的hash值{如果还未计算,就先计算一次}
unsigned int luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_TLNGSTR);
  if (ts->extra == 0) {  /* no hash? */
    ts->hash = luaS_hash(getstr(ts), ts->u.lnglen, ts->hash);
    ts->extra = 1;  /* now it has its hash */
  }
  return ts->hash;
}


/*
** resizes the string table
* 给g->stringTable(保存的是短字符串)重新设置大小(扩容或者缩容),并更新hash数组各自对应的链表,
*/
void luaS_resize (lua_State *L, int newsize) {
  int i;
  stringtable *tb = &G(L)->strt;
  if (newsize > tb->size) {  /* grow table if needed */
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);//对hash数组进行重新分配,注意这个realloc会将原内存中的值拷贝到新的内存中,
    for (i = tb->size; i < newsize; i++)//对新增的数组元素(注意i的起始大小)直接置为null
      tb->hash[i] = NULL;
  }

  //重新hash,将hash表格中每个元素对应的链表中的TString重新计算位置并移动过去,
  for (i = 0; i < tb->size; i++) {  /* rehash */
    TString *p = tb->hash[i];
    tb->hash[i] = NULL;
    while (p) {  /* for each node in the list */
      TString *hnext = p->u.hnext;  /* save next */
      unsigned int h = lmod(p->hash, newsize);  /* new position */
      p->u.hnext = tb->hash[h];  /* chain it */
      tb->hash[h] = p;//放到链首,
      p = hnext;
    }
  }

  //?
  if (newsize < tb->size) {  /* shrink table if needed */
    /* vanishing slice should be empty */
    lua_assert(tb->hash[newsize] == NULL && tb->hash[tb->size - 1] == NULL);
    //有点看不懂这里了,感觉因为要缩容,直接将[tb->size, newSize]之间的hash表中的元素不管了,
    //不过因为这个只是stringtable,即使不管了,丢弃了,也不会引起什么问题,因为可以再次构建newTString就可以了,
    //丢弃了的hash表中的元素再之后也会被GC掉,???但还是有问题,对字符串对比的时候是直接对比的指针(eqshrstr)
    luaM_reallocvector(L, tb->hash, tb->size, newsize, TString *);
  }
  tb->size = newsize;
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*清除G->strcache中保存的字符串(长或短),需要是白色的(包含两种白),
*/
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
    if (iswhite(g->strcache[i][j]))  /* will entry be collected? */
      g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed,虽然没有区分是哪种白色,但只是擦除并重新赋值,并没有处理保存的TString,所以不会影响gc */
    }
}


/*
** Initialize the string table and the string cache
*初始化G->strt(string table)和G->strcache
*/
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  //1 初始化G->strt(string table,保存短字符串)的大小为128
  luaS_resize(L, MINSTRTABSIZE);  /* initial size of string table */

  //2 初始化 G->strcache(保存长或短字符串,仅仅作为一个快速的缓存)
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);
  luaC_fix(L, obj2gco(g->memerrmsg));  /* it should never be collected */
  for (i = 0; i < STRCACHE_N; i++)  /* fill cache with valid strings */
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}



/*
** creates a new string object
* 创建字符串TString(字符长度为l, 这里还没有将str copy进去), 设置tag, hash
*/
static TString *createstrobj (lua_State *L, size_t l, int tag, unsigned int h) {
  TString *ts;
  GCObject *o;
  size_t totalsize;  /* total size of TString object */
  totalsize = sizelstring(l);//这里 sizeof(UTString) + l + 1, 最后一位用来存入'\0'
  o = luaC_newobj(L, tag, totalsize);
  ts = gco2ts(o);
  ts->hash = h;
  ts->extra = 0;
  getstr(ts)[l] = '\0';  /* ending 0 */
  return ts;
}

/*
 * 创建长字符串,默认不会计算hash值,初始hash值为G->seed
 */
TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  TString *ts = createstrobj(L, l, LUA_TLNGSTR, G(L)->seed);
  ts->u.lnglen = l;
  return ts;
}


void luaS_remove (lua_State *L, TString *ts) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->hash[lmod(ts->hash, tb->size)];
  while (*p != ts)  /* find previous element */
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list */
  tb->nuse--;
}


/*
** checks whether short string exists and reuses it or creates a new one
* 创建短字符串TString
* (1)先在G->strt(类型为stringtable)中查找,如果找到了则直接返回,
* (2)没找到则先检测G->strt是否要扩容,并将新建的TString插入到G->strt的hash链表(通过hash值计算在哪个链表中)头部,
*/
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);

  //1、先在G->strt(类型为stringtable)中查找,如果找到了则直接返回,
  unsigned int h = luaS_hash(str, l, g->seed);
  TString **list = &g->strt.hash[lmod(h, g->strt.size)];
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined */
  for (ts = *list; ts != NULL; ts = ts->u.hnext) {
    if (l == ts->shrlen &&
        (memcmp(str, getstr(ts), l * sizeof(char)) == 0)) {
      /* found! */
      if (isdead(g, ts))  /* dead (but not collected yet)? */
        changewhite(ts);  /* resurrect it */
      return ts;
    }
  }

  //2、在G->strt(类型为stringtable)中查找失败, 因为要新创建一个字符串, 所以check一下stringtable是不是要扩容并更新,
  if (g->strt.nuse >= g->strt.size && g->strt.size <= MAX_INT/2) {
    luaS_resize(L, g->strt.size * 2);
    list = &g->strt.hash[lmod(h, g->strt.size)];  /* recompute with new size, 重新计算一下在哪个hash链表中 */
  }

  //3、创建新的TString,复制字符串进去,设置tag,hash等值,
  ts = createstrobj(L, l, LUA_TSHRSTR, h);
  memcpy(getstr(ts), str, l * sizeof(char));
  ts->shrlen = cast_byte(l);
  ts->u.hnext = *list;//将新建的TString放到g->stringtable的hash链表(前面计算了在哪个hash链表中)的第一个节点,
  *list = ts;
  g->strt.nuse++;
  return ts;
}


/*
** new string (with explicit length)
* 创建字符串(短或长)
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? 短字符串 */
    return internshrstr(L, str, l);
  else {//长字符串,
    TString *ts;
    if (l >= (MAX_SIZE - sizeof(TString))/sizeof(char))
      luaM_toobig(L);
    ts = luaS_createlngstrobj(L, l);
    memcpy(getstr(ts), str, l * sizeof(char));
    return ts;
  }
}


/*
** Create or reuse a zero-terminated string, first checking in the
** cache (using the string address as a key). The cache can contain
** only zero-terminated strings, so it is safe to use 'strcmp' to
** check hits.
* 使用str(char*)来构建TString,先在strcache里面找,找不到则调用 luaS_newlstr新建字符串,并且将新建的字符串更新到strcache中,
*/
TString *luaS_new (lua_State *L, const char *str) {
  unsigned int i = point2uint(str) % STRCACHE_N;  /* hash */
  int j;

  //先在strcache里面查找是否已经有了同样的字符串,
  TString **p = G(L)->strcache[i];
  for (j = 0; j < STRCACHE_M; j++) {
    if (strcmp(str, getstr(p[j])) == 0)  /* hit? */
      return p[j];  /* that is it */
  }
  
  /* normal route */
  for (j = STRCACHE_M - 1; j > 0; j--)
    p[j] = p[j - 1];  /* move out last element */
  /* new element is first in the list, 更新strCache */
  p[0] = luaS_newlstr(L, str, strlen(str));
  return p[0];
}


Udata *luaS_newudata (lua_State *L, size_t s) {
  Udata *u;
  GCObject *o;
  if (s > MAX_SIZE - sizeof(Udata))
    luaM_toobig(L);
  o = luaC_newobj(L, LUA_TUSERDATA, sizeludata(s));
  u = gco2u(o);
  u->len = s;
  u->metatable = NULL;
  setuservalue(L, u, luaO_nilobject);
  return u;
}

