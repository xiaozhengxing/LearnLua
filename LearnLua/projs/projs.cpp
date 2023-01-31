#include <iostream>
#include <cassert>
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

int main(int argc, char* argv[])
{
    lua_State* L = luaL_newstate();
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
