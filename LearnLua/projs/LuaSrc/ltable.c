/*
** $Id: ltable.c,v 2.118 2016/11/07 12:38:35 roberto Exp $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#define ltable_c
#define LUA_CORE

#include "lprefix.h"


/*
** Implementation of tables (aka arrays, objects, or hash tables).
** Tables keep its elements in two parts: an array part and a hash part.
** Non-negative integer keys are all candidates to be kept in the array
** part. The actual size of the array is the largest 'n' such that
** more than half the slots between 1 and n are in use.
** Hash uses a mix of chained scatter table with Brent's variation.
** A main invariant of these tables is that, if an element is not
** in its main position (i.e. the 'original' position that its hash gives
** to it), then the colliding element is in its own main position.
** Hence even when the load factor reaches 100%, performance remains good.
*/

#include <math.h>
#include <limits.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"


/*
** Maximum size of array part (MAXASIZE) is 2^MAXABITS. MAXABITS is
** the largest integer such that MAXASIZE fits in an unsigned int.
*/
#define MAXABITS	cast_int(sizeof(int) * CHAR_BIT - 1)
#define MAXASIZE	(1u << MAXABITS)

/*
** Maximum size of hash part is 2^MAXHBITS. MAXHBITS is the largest
** integer such that 2^MAXHBITS fits in a signed int. (Note that the
** maximum number of elements in a table, 2^MAXABITS + 2^MAXHBITS, still
** fits comfortably in an unsigned int.)
*/
#define MAXHBITS	(MAXABITS - 1)


#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

//求t[str]对应的node地址(使用了str->hash)
#define hashstr(t,str)		hashpow2(t, (str)->hash)

//求bool值p对应的node地址,
#define hashboolean(t,p)	hashpow2(t, p)

//求t[i]对应的node地址,i为int值,
#define hashint(t,i)		hashpow2(t, i)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
* 求t[n]对应的node的地址,n为int值,
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))

//将指针p(light userdata或light c function或 GCObject*)转为uint,求其对应的node的地址,
#define hashpointer(t,p)	hashmod(t, point2uint(p))


#define dummynode		(&dummynode_)

static const Node dummynode_ = {
  {NILCONSTANT},  /* value */
  {{NILCONSTANT, 0}}  /* key */
};


/*
** Hash for floating-point numbers.
** The main computation should be just
**     n = frexp(n, &i); return (n * INT_MAX) + i
** but there are some numerical subtleties.
** In a two-complement representation, INT_MAX does not has an exact
** representation as a float, but INT_MIN does; because the absolute
** value of 'frexp' is smaller than 1 (unless 'n' is inf/NaN), the
** absolute value of the product 'frexp * -INT_MIN' is smaller or equal
** to INT_MAX. Next, the use of 'unsigned int' avoids overflows when
** adding 'i'; the use of '~u' (instead of '-u') avoids problems with
** INT_MIN.
*/
#if !defined(l_hashfloat)
static int l_hashfloat (lua_Number n) {
  int i;
  lua_Integer ni;
  n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
  if (!lua_numbertointeger(n, &ni)) {  /* is 'n' inf/-inf/NaN? */
    lua_assert(luai_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  /* normal case */
    unsigned int u = cast(unsigned int, i) + cast(unsigned int, ni);
    return cast_int(u <= cast(unsigned int, INT_MAX) ? u : ~u);
  }
}
#endif


/*
** returns the 'main' position of an element in a table (that is, the index
** of its hash value)
*  根据key(TValue*)的类型,计算其对应的Table.node(hash部分)数组元素的地址,
*/
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMINT:
      return hashint(t, ivalue(key));
    case LUA_TNUMFLT:
      return hashmod(t, l_hashfloat(fltvalue(key)));
    case LUA_TSHRSTR://短或长字符串都是取TString.hash 和 table.sizenode(求幂)来计算对应node的地址,
      return hashstr(t, tsvalue(key));
    case LUA_TLNGSTR://短或长字符串都是取TString.hash 和 table.sizenode(求幂)来计算对应node的地址,
      return hashpow2(t, luaS_hashlongstr(tsvalue(key)));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    case LUA_TLCF:
      return hashpointer(t, fvalue(key));
    default:
      lua_assert(!ttisdeadkey(key));
      return hashpointer(t, gcvalue(key));
  }
}


/*
** returns the index for 'key' if 'key' is an appropriate key to live in
** the array part of the table, 0 otherwise.
* 计算key(TValue*类型,保存的是integer值)应该对应的数组下标,其实就是返回key中的integer值, 如不符合条件则返回0
*/
static unsigned int arrayindex (const TValue *key) {
  if (ttisinteger(key)) {
    lua_Integer k = ivalue(key);
    if (0 < k && (lua_Unsigned)k <= MAXASIZE)
      return cast(unsigned int, k);  /* 'key' is an appropriate array index */
  }
  return 0;  /* 'key' did not match some condition */
}


/*
** returns the index of a 'key' for table traversals. First goes all
** elements in the array part, then elements in the hash part. The
** beginning of a traversal is signaled by 0.
* 返回key在table中对应的位置下标:
* key为数字 且  0 < key <= table.sizearray，则返回对应的int值
* key不为数字则在table的hash node中查找,找到了返回 (table.sizearray + hash node下标 + 1)
*/
static unsigned int findindex (lua_State *L, Table *t, StkId key) {
  unsigned int i;
  if (ttisnil(key)) return 0;  /* first iteration, 第一次迭代一般都是会push一个nil进来,此时返回0,可查看lua_next的用法 */
  i = arrayindex(key);
  if (i != 0 && i <= t->sizearray)  /* is 'key' inside array part?, key是个整数,根据值大小判断是否在table的数组部分 */
    return i;  /* yes; that's the index */
  else {
    int nx;
    Node *n = mainposition(t, key);
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      /* key may be dead already, but it is ok to use it in 'next' */
      if (luaV_rawequalobj(gkey(n), key) ||
            (ttisdeadkey(gkey(n)) && iscollectable(key) &&
             deadvalue(gkey(n)) == gcvalue(key)))
      {
        i = cast_int(n - gnode(t, 0));  /* key index in hash table */
        /* hash elements are numbered after array ones */
        return (i + 1) + t->sizearray;//数组个数 + hash node下标 + 1
      }
      nx = gnext(n);
      if (nx == 0)
        luaG_runerror(L, "invalid key to 'next'");  /* key not found */
      else n += nx;
    }
  }
}

/*
 *查找key对应的table[key]和下一个key,如成功返回1,失败返回0,
 *执行前的栈: [key][top]
 *执行后的栈:[next(key)][table[key]][top],其中next[key]指的是根据key在table中的位置(array中或hash node中),查找下一个位置,
 */
int luaH_next (lua_State *L, Table *t, StkId key) {
  unsigned int i = findindex(L, t, key);  /* find original element */

  //需要注意当findindex返回的是sizearray的时候,会执行下面的第二个for循环,会找到table.node[0]
  //数组部分,
  for (; i < t->sizearray; i++) {  /* try first array part */
    if (!ttisnil(&t->array[i])) {  /* a non-nil value? */
      setivalue(key, i + 1);//将整型值(i+1,遍历过程中的下一个下标)赋给key(TValue*)
      setobj2s(L, key+1, &t->array[i]);
      return 1;
    }
  }

  //hash node部分,
  for (i -= t->sizearray; cast_int(i) < sizenode(t); i++) {  /* hash part */
    if (!ttisnil(gval(gnode(t, i)))) {  /* a non-nil value? */
      setobj2s(L, key, gkey(gnode(t, i)));//当前key所在位置对应的next hash node,将改node.key赋值到key(TValue*)中,
      setobj2s(L, key+1, gval(gnode(t, i)));
      return 1;
    }
  }
  return 0;  /* no more elements */
}


/*
** {=============================================================
** Rehash
** ==============================================================
*/

/*
** Compute the optimal size for the array part of table 't'. 'nums' is a
** "count array" where 'nums[i]' is the number of integers in the table
** between 2^(i - 1) + 1 and 2^i. 'pna' enters with the total number of
** integer keys in the table and leaves with the number of keys that
** will go to the array part; return the optimal size.
*/
static unsigned int computesizes (unsigned int nums[], unsigned int *pna) {
  int i;
  unsigned int twotoi;  /* 2^i (candidate for optimal size) */
  unsigned int a = 0;  /* number of elements smaller than 2^i */
  unsigned int na = 0;  /* number of elements to go to array part */
  unsigned int optimal = 0;  /* optimal size for array part */
  /* loop while keys can fill more than half of total size */
  for (i = 0, twotoi = 1; *pna > twotoi / 2; i++, twotoi *= 2) {
    if (nums[i] > 0) {
      a += nums[i];
      if (a > twotoi/2) {  /* more than half elements present? */
        optimal = twotoi;  /* optimal size (till now) */
        na = a;  /* all elements up to 'optimal' will go to array part */
      }
    }
  }
  lua_assert((optimal == 0 || optimal / 2 < na) && na <= optimal);
  *pna = na;
  return optimal;
}

/*
 * nums[i]用来保存 key(注意key中保存的值为整数)的个数, key符合要求 {2^(i-1) < key <= 2^i 且table[key]不为nil} 
 * key中保存的值为整数,则往nums数组中相应的值+1,并返回1
 * 不是整数则返回0
 */
static int countint (const TValue *key, unsigned int *nums) {
  unsigned int k = arrayindex(key);
  if (k != 0) {  /* is 'key' an appropriate array index? */
    nums[luaO_ceillog2(k)]++;  /* count as such */
    return 1;
  }
  else
    return 0;
}


/*
** Count keys in array part of table 't': Fill 'nums[i]' with
** number of keys that will go into corresponding slice and return
** total number of non-nil keys.
* 只查找table.array部分, nums[i]用来保存 key(注意key为整数)的个数, 其中key符合{ 2^(i-1) < key <= 2^i 且table[key]不为nil} 
* 返回table.array中总的“table[key]不为nil”的key的个数,
*/
static unsigned int numusearray (const Table *t, unsigned int *nums) {
  int lg;
  unsigned int ttlg;  /* 2^lg */
  unsigned int ause = 0;  /* summation of 'nums' */
  unsigned int i = 1;  /* count to traverse all array keys */
  /* traverse each slice */
  for (lg = 0, ttlg = 1; lg <= MAXABITS; lg++, ttlg *= 2) {
    unsigned int lc = 0;  /* counter */
    unsigned int lim = ttlg;
    if (lim > t->sizearray) {
      lim = t->sizearray;  /* adjust upper limit */
      if (i > lim)
        break;  /* no more elements to count */
    }
    /* count elements in range (2^(lg - 1), 2^lg] */
    for (; i <= lim; i++) {
      if (!ttisnil(&t->array[i-1]))
        lc++;
    }
    nums[lg] += lc;
    ause += lc;
  }
  return ause;
}

/*
* nums[i]用来保存 符合要求的key(注意key为整数)的个数, 其中key符合{2^(i-1) < key <= 2^i 且table[key]不为nil} 
 * pna用来保存node中key(TValue)中保存的类型为整数，这样的key的个数,
 * 返回table的node中value(TValue)不为nil的个数,
 */
static int numusehash (const Table *t, unsigned int *nums, unsigned int *pna) {
  int totaluse = 0;  /* total number of elements */
  int ause = 0;  /* elements added to 'nums' (can go to array part), 保存 hash node中,"key(TValue)中保存的类型为整数",这样的key的个数 */
  int i = sizenode(t);
  while (i--) {
    Node *n = &t->node[i];
    if (!ttisnil(gval(n))) {
      ause += countint(gkey(n), nums);
      totaluse++;
    }
  }
  *pna += ause;
  return totaluse;
}

//设置table.array的大小为size,并更新array中的元素值,
static void setarrayvector (lua_State *L, Table *t, unsigned int size) {
  unsigned int i;
  //注意这里的realloc会做copy操作,old array中的数据会copy到new array中,
  luaM_reallocvector(L, t->array, t->sizearray, size, TValue);
  for (i=t->sizearray; i<size; i++)//将new array中新增的内存元素全部赋值为nil
     setnilvalue(&t->array[i]);
  t->sizearray = size;
}

//设置table.node的大小为size,并将node中的所有元素信息清除,
//table.lastfree指针指向最后一个元素node[size](注意这个是一个越界值,插入新的(key,val)时,会从lastfree--开始计算,见luaH_newkey),
static void setnodevector (lua_State *L, Table *t, unsigned int size) {
  if (size == 0) {  /* no elements to hash part? */
    t->node = cast(Node *, dummynode);  /* use common 'dummynode' */
    t->lsizenode = 0;
    t->lastfree = NULL;  /* signal that it is using dummy node */
  }
  else {
    int i;
    int lsize = luaO_ceillog2(size);
    if (lsize > MAXHBITS)
      luaG_runerror(L, "table overflow");
    size = twoto(lsize);//求间隔最近的2^n值,
    t->node = luaM_newvector(L, size, Node);
    
    for (i = 0; i < (int)size; i++) {
      Node *n = gnode(t, i);
      gnext(n) = 0;
      setnilvalue(wgkey(n));
      setnilvalue(gval(n));
    }
    t->lsizenode = cast_byte(lsize);
    t->lastfree = gnode(t, size);  /* all positions are free */
  }
}

/* 更新table的大小(array和node hash), 并将原array和node hash的值插入到新的array和node hash中,
 * nasize: new array size
 * nhsize: new hash node size
 */
void luaH_resize (lua_State *L, Table *t, unsigned int nasize,
                                          unsigned int nhsize) {
  unsigned int i;
  int j;
  unsigned int oldasize = t->sizearray;
  int oldhsize = allocsizenode(t);
  Node *nold = t->node;  /* save old hash ... 注意这里保存了老node hash的首地址 */
  
  if (nasize > oldasize)  /* array part must grow? 数组扩容 */
    setarrayvector(L, t, nasize);//设置table.array的大小为size,并更新array中的元素值,

  /* create new hash part with appropriate size */
  setnodevector(L, t, nhsize);//设置table.node的大小为size,并将node中的所有元素信息清除,
  
  if (nasize < oldasize) {  /* array part must shrink?数组缩容 */
    t->sizearray = nasize;
    /* re-insert elements from vanishing slice */
    for (i=nasize; i<oldasize; i++) {
      if (!ttisnil(&t->array[i]))
        luaH_setint(L, t, i + 1, &t->array[i]);//插入到node hash中,
    }
    /* shrink array, 注意这里的realloc会做copy操作,old array中的数据会copy到new array中, */
    luaM_reallocvector(L, t->array, oldasize, nasize, TValue);
  }
  
  /* re-insert elements from hash part */
  for (j = oldhsize - 1; j >= 0; j--) {//将old node hash中的key-value插入到新的table中(插入到新的array或新的node hash中)
    Node *old = nold + j;
    if (!ttisnil(gval(old))) {
      /* doesn't need barrier/invalidate cache, as entry was
         already present in the table */
      setobjt2t(L, luaH_set(L, t, gkey(old)), gval(old));
    }
  }
  if (oldhsize > 0)  /* not the dummy node? 释放旧的node hash */
    luaM_freearray(L, nold, cast(size_t, oldhsize)); /* free old hash */
}

//更新table的数组大小(nasize:new array size), node hash的大小不变,
//注意,table中的数组和node内容不变,key-value放的位置可能需要更新,
void luaH_resizearray (lua_State *L, Table *t, unsigned int nasize) {
  int nsize = allocsizenode(t);
  luaH_resize(L, t, nasize, nsize);
}

/*
** nums[i] = number of keys 'k' where 2^(i - 1) < k <= 2^i
* ek:extral key, 等待新插入的key
*  考虑即将要新插入的key(extral key), 计算table中的array和node hash的最适当的大小, 并resize(这里面会调整array和node hash元素的位置),
*/
static void rehash (lua_State *L, Table *t, const TValue *ek) {
  unsigned int asize;  /* optimal size for array part */
  unsigned int na;  /* number of keys in the array part,计算有效的key的个数: table.array[key]不为 nil(其中key为整数) 或 table.node[i].key(类型为TValue,保存的值为整数) */
  unsigned int nums[MAXABITS + 1];
  int i;
  int totaluse;
  for (i = 0; i <= MAXABITS; i++) nums[i] = 0;  /* reset counts */
  na = numusearray(t, nums);  /* count keys in array part */
  totaluse = na;  /* all those keys are integer keys */
  totaluse += numusehash(t, nums, &na);  /* count keys in hash part */
  /* count extra key ,判断extral key中保存的是不是整数 */
  na += countint(ek, nums);
  totaluse++;
  
  /* compute new size for array part */
  asize = computesizes(nums, &na);
  /* resize the table to new computed sizes */
  luaH_resize(L, t, asize, totaluse - na);
}



/*
** }=============================================================
*/

/*
 * 新建table, array和node hash大小都为0
 */
Table *luaH_new (lua_State *L) {
  GCObject *o = luaC_newobj(L, LUA_TTABLE, sizeof(Table));
  Table *t = gco2t(o);
  t->metatable = NULL;
  t->flags = cast_byte(~0);//默认bits全为1, 表示node中不含"__index"等键值对,
  t->array = NULL;
  t->sizearray = 0;
  setnodevector(L, t, 0);
  return t;
}

/*
 * free table{包含table中的array和 node hash}
 */
void luaH_free (lua_State *L, Table *t) {
  if (!isdummy(t))
    luaM_freearray(L, t->node, cast(size_t, sizenode(t)));
  luaM_freearray(L, t->array, t->sizearray);
  luaM_free(L, t);
}

//从(lastfree--)开始,查找到第一个空闲node(key为nil,并更新 lastfree )并返回,
static Node *getfreepos (Table *t) {
  if (!isdummy(t)) {
    while (t->lastfree > t->node) {
      t->lastfree--;
      if (ttisnil(gkey(t->lastfree)))
        return t->lastfree;
    }
  }
  return NULL;  /* could not find a free place */
}



/*
** inserts a new key into a hash table; first, check whether key's main
** position is free. If not, check whether colliding node is in its main
** position or not: if it is not, move colliding node to an empty place and
** put new key in its main position; otherwise (colliding node is in its main
** position), new key goes to an empty position.
*
* 将新的key(TValue*)插入到table的hash node(检测是否需要扩容,并检测是否冲突)中,并返回node.i_val(类型为TValue*,注意该node已填充好了key)
* 
*/
TValue *luaH_newkey (lua_State *L, Table *t, const TValue *key) {
  Node *mp;
  TValue aux;
  if (ttisnil(key)) luaG_runerror(L, "table index is nil");
  else if (ttisfloat(key)) {//如果是float则需要转成整数,保存到aux中,最终将整数保存回key中,
    lua_Integer k;
    if (luaV_tointeger(key, &k, 0)) {  /* does index fit in an integer? */
      setivalue(&aux, k);
      key = &aux;  /* insert it as an integer */
    }
    else if (luai_numisnan(fltvalue(key)))
      luaG_runerror(L, "table index is NaN");
  }
  
  mp = mainposition(t, key);//先求出位置:对应的Table.node(hash部分)数组元素的地址,
  
  if (!ttisnil(gval(mp)) || isdummy(t)) {  /* 该node中已被占用, main position is taken? */
    Node *othern;
    Node *f = getfreepos(t);  /* get a free place */
    if (f == NULL) {  /* cannot find a free place? 没有空间了,则扩容 */
      rehash(L, t, key);  /* grow table, 注意这里面会考虑等待新插入的key */
      /* whatever called 'newkey' takes care of TM cache */
      return luaH_set(L, t, key);  /* insert key into grown table */
    }
    
    lua_assert(!isdummy(t));
    othern = mainposition(t, gkey(mp));//冲突元素实际的位置,
    if (othern != mp) {  /* 冲突元素的实际位置并不是mp(新插入key的实际位置), is colliding node out of its main position? */
      /* yes; move colliding node into free position */
      while (othern + gnext(othern) != mp)  /* find previous,找到othern(冲突元素)的前一个node, 不同的Tkey计算出来的hash值相同,使用TKey.nk.next串起来 */
        othern += gnext(othern);//

      //挪动 mainPosition中的冲突元素node(node.key值做hash后 != key值(函数参数)做hash)
      //1、修改冲突node前一个元素的next偏移值,另起指向freepos
      //2、将冲突node移动到freePos
      //3、更新node.next偏移值,
      
      //此刻othern冲突元素为mainpositoin(被占用的node)的前一个node,
      //修改othern冲突元素的next偏移值,使它指向f(freePos)
      gnext(othern) = cast_int(f - othern);  /* rechain to point to 'f',1.修改冲突node前一个元素的next偏移值 */
      *f = *mp;  /* copy colliding node into free pos. (mp->next also goes),2.将冲突node移动到freePos */
      if (gnext(mp) != 0) {
        gnext(f) += cast_int(mp - f);  /* correct 'next' ,3.更新冲突node.next偏移值,注意这里是"+="而不是"="*/
        gnext(mp) = 0;  /* now 'mp' is free, mainPosition位置的node*/
      }
      setnilvalue(gval(mp));//最终腾出来的空余node
    }
    else {  /* 冲突元素的实际位置和mp(新插入key的实际位置)一样,colliding node is in its own main position */
      /* new node will go into free position, 将新key插入到freePos, 调整next值，使得
       * 原链: mp->mpNext
       * 现在: mp->newKey->mpNext, 感觉这里是直接插入到链首的下一个节点,就是链的第二个节点,而不是链尾,
       */
      if (gnext(mp) != 0)
        gnext(f) = cast_int((mp + gnext(mp)) - f);  /* chain new position */
      else
        lua_assert(gnext(f) == 0);
      
      gnext(mp) = cast_int(f - mp);
      mp = f;
    }
  }
  
  setnodekey(L, &mp->i_key, key);//将key赋值给Node.i_key
  luaC_barrierback(L, t, key);
  lua_assert(ttisnil(gval(mp)));
  return gval(mp);//返回node.i_val(类型为TValue*), 注意该node已填充好了key 
}


/*
** search function for integers
* 返回table[key],key是个整数, 可能是在数组array中,也可能是在hash node中,
* 找到返回对应的value,找不到返回luaO_nilobject
* 不会触发元方法
*/
const TValue *luaH_getint (Table *t, lua_Integer key) {
  /* (1 <= key && key <= t->sizearray) */
  if (l_castS2U(key) - 1 < t->sizearray)//这里其实有个问题,在表格resize的时候,原来在hash表中的node.key>srcSize但node.key<dstSize的节点,会不会搬到新table的array中,
    return &t->array[key - 1];
  else {//到 hash表中查询,
    Node *n = hashint(t, key);
    for (;;) {  /* check whether 'key' is somewhere in the chain */
      if (ttisinteger(gkey(n)) && ivalue(gkey(n)) == key)
        return gval(n);  /* that's it */
      else {
        int nx = gnext(n);//取链表中的下一个node,查找key在不在对应的node中(这里面维持了一个类似链表后指针的结构,)
        if (nx == 0) break;
        n += nx;
      }
    }
    return luaO_nilobject;
  }
}


/* 
** search function for short strings
* 查找t[key], key为短字符串, 找到了返回node.val(类型为TValue*);没找到返回luaO_nilobject
* 不会调用元方法,
*/
const TValue *luaH_getshortstr (Table *t, TString *key) {
  Node *n = hashstr(t, key);
  lua_assert(key->tt == LUA_TSHRSTR);

  //根据node和node.next,一直查找到最后一个元素, 查找key是否已存在,
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    const TValue *k = gkey(n);
    if (ttisshrstring(k) && eqshrstr(tsvalue(k), key))//直接对比短字符串的指针是否相等(短字符串只会创建一次),
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return luaO_nilobject;  /* not found */
      n += nx;
    }
  }
}


/*
** "Generic" get version. (Not that generic: not valid for integers,
** which may be in array part, nor for floats with integral values.)
*  返回t[key],是在table.node中查找(没有触发元方法), 所以key不能为整数或可转成整数的浮点数,
*/
static const TValue *getgeneric (Table *t, const TValue *key) {
  Node *n = mainposition(t, key);
  for (;;) {  /* check whether 'key' is somewhere in the chain */
    if (luaV_rawequalobj(gkey(n), key))//简单对比key(指针),并不会触发元方法,
      return gval(n);  /* that's it */
    else {
      int nx = gnext(n);
      if (nx == 0)
        return luaO_nilobject;  /* not found */
      n += nx;
    }
  }
}

/*简单查找 t[key], key为string(不会触发元方法)
查不到则返回nilobj(有值不为null,但类型是nil)
*/
const TValue *luaH_getstr (Table *t, TString *key) {
  if (key->tt == LUA_TSHRSTR)//短字符串,只需要对比指针即可(短字符串只会创建一次)
    return luaH_getshortstr(t, key);
  else {  /* for long strings, use generic case,长字符串 */
    TValue ko;
    setsvalue(cast(lua_State *, NULL), &ko, key);//key值赋给ko
    return getgeneric(t, &ko);//长字符串时对比指针,不相等的话,就对比保存的字符内容(不会触发元方法)
  }
}


/*
** main search function
* 查找t[key],(不会触发元方法)
*/
const TValue *luaH_get (Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TSHRSTR: return luaH_getshortstr(t, tsvalue(key));//短字符串仅比较指针(因为只会创建一次),不会触发元方法,
    case LUA_TNUMINT: return luaH_getint(t, ivalue(key));//简单查找t[key],key为int,不会触发元方法,
    case LUA_TNIL: return luaO_nilobject;
    case LUA_TNUMFLT: {
      lua_Integer k;
      if (luaV_tointeger(key, &k, 0)) /* index is int? */
        return luaH_getint(t, k);  /* use specialized version */
      /* else... */
    }  /* FALLTHROUGH */
    default:
      return getgeneric(t, key);//在table.node hash中对比key来查找table[key],不会触发元方法,
  }
}


/*
** beware: when using this function you probably need to check a GC
** barrier and invalidate the TM cache.
* 返回 table[key](类型转为TValue*),如果找不到table[key]则将key 新插入到table中(数组或node hash)
*/
TValue *luaH_set (lua_State *L, Table *t, const TValue *key) {
  const TValue *p = luaH_get(t, key);
  if (p != luaO_nilobject)
    return cast(TValue *, p);
  else return luaH_newkey(L, t, key);
}

/*
 * key为整数, 设置t[key] = value,
1 如果t[key]存在(包含情况:key<arraysize,但 t[key]=nil,也就是找得到对应的TValue就行),则更新即可,
2 如果t[key]不存在,则需要在table中新初始化node,将key和value都赋值到node中,
*/
void luaH_setint (lua_State *L, Table *t, lua_Integer key, TValue *value) {
  const TValue *p = luaH_getint(t, key);//查找t[key],找不到返回luaO_nilobject
  TValue *cell;
  if (p != luaO_nilobject)
    cell = cast(TValue *, p);
  else {
    TValue k;
    setivalue(&k, key);
    cell = luaH_newkey(L, t, &k);
  }
  setobj2t(L, cell, value);
}

/*
 * 根据上层调用,此时j已经是 table.node部分了, 因为 j = table.arraySize
 * 查找table.node中, 返回i值, 符合{table[i]!=nil and table[i+1]==nil}
 */
static int unbound_search (Table *t, unsigned int j) {
  unsigned int i = j;  /* i is zero or a present index */
  j++;
  /* find 'i' and 'j' such that i is present and j is not
   * 先大致找到一个范围,使得 {t[i] != nil and t[j]==nil , 其中 j = i * 2}
   */
  while (!ttisnil(luaH_getint(t, j))) {
    i = j;
    if (j > cast(unsigned int, MAX_INT)/2) {  /* overflow? */
      /* table was built with bad purposes: resort to linear search */
      i = 1;
      while (!ttisnil(luaH_getint(t, i))) i++;
      return i - 1;
    }
    j *= 2;
  }
  
  /* now do a binary search between them */
  while (j - i > 1) {
    unsigned int m = (i+j)/2;
    if (ttisnil(luaH_getint(t, m))) j = m;
    else i = m;
  }
  return i;
}


/*
** Try to find a boundary in table 't'. A 'boundary' is an integer index
** such that t[i] is non-nil and t[i+1] is nil (and 0 if t[1] is nil).
* 返回i,符合 t[i]!=nil and t[i+1]==nil
* 先数组部分找,找不到则在node中找, 所返回的i值并不能认为就是table中有效值的个数,比如数组中 {1,2,3,nil,5,6,7,nil},返回的就是3,会把5,6,7忽略的,
*/
int luaH_getn (Table *t) {
  //检查数组部分,
  unsigned int j = t->sizearray;
  if (j > 0 && ttisnil(&t->array[j - 1])) {//数组里面最后一格元素是nil,
    /* there is a boundary in the array part: (binary) search for it, 二分法 */
    unsigned int i = 0;
    
    //二分查找,
    while (j - i > 1) { 
      unsigned int m = (i+j)/2;
      if (ttisnil(&t->array[m - 1])) j = m;
      else i = m;
    }
    return i;
  }
  /* else must find a boundary in hash part */
  else if (isdummy(t))  /* hash part is empty? hash表是空 */
    return j;  /* that is easy... */
  else return unbound_search(t, j);//hash表不为空,
}



#if defined(LUA_DEBUG)

Node *luaH_mainposition (const Table *t, const TValue *key) {
  return mainposition(t, key);
}

int luaH_isdummy (const Table *t) { return isdummy(t); }

#endif
