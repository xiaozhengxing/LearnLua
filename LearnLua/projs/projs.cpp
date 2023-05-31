#include <iostream>
#include <cassert>
#include <csetjmp>
#include <windows.h>

using namespace  std;
extern "C"//跟着<自己动手实现lua虚拟机>,使用5.3.4版本
{
#include "LuaSrc/lua.h"
#include "LuaSrc/lauxlib.h"
#include "LuaSrc/lualib.h"
#include "LuaSrc/ltable.h"
#include "LuaSrc/lstate.h"
#include "LuaSrc/lobject.h"
#include "LuaSrc/lapi.h"
#include "LuaSrc/lauxlib.h"
#include "LuaSrc/lgc.h"
}

#define xzxTest

#ifdef xzxTest
int main(int argc, char* argv[])
{
    lua_State* L = luaL_newstate();//xzxtodo0
    assert(L != NULL);    

    luaopen_base(L);
    luaL_openlibs(L);

    int iRet = luaL_dofile(L, "test.lua");
    if (iRet)
    {
        cout << "load file test.lua error " << iRet << endl;
        const char* pErrorMsg = lua_tostring(L, -1);
        cout << pErrorMsg << endl;
        lua_close(L);
        
        return 0;
    }
    return 0;
}

#else

jmp_buf env;

int my_test(int a, int b)
{
    if(b == 0)
    {
        printf("test 0\n");
        longjmp(env, 2);
    }
    return a / b;
}

int main(int argc, char* argv[])
{
    int res = setjmp(env);
    if(res == 0)//执行my_test方法
    {
        cout<< "return from setjmp" <<endl;
        my_test(10, 0);
    }
    else if(res == 1)//遇到longjmp,则进入该分支
    {
        cout << "1return from longjmp: " << res << endl;
    }
    else if(res == 2)
    {
        cout << "2return from longjmp: " << res << endl;
    }
    
    return 0;
}
#endif



