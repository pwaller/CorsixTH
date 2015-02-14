/*
Copyright (c) 2009 Peter "Corsix" Cawley

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "config.h"
#include "../Src/main.h"
#include "../Src/bootstrap.h"
#include <stack>
#include <SDL.h>

// Template magic for checking type equality
template <typename T1, typename T2>
struct types_equal{ enum{
    result = -1,
}; };

template <typename T1>
struct types_equal<T1, T1>{ enum{
    result = 1,
}; };

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <syscall.h>
#include <execinfo.h>
#define UNW_LOCAL_ONLY
#include <libunwind.h>

lua_State *globL;


extern lua_State *mainloop_co;
int main(int argc, char** argv);

void show_backtrace() {
    unw_cursor_t c; unw_context_t uc;
    unw_word_t ip;
    unw_proc_info_t pi;

    int frames = 0;

    char cbuf[10240] = {0};
    char *bufstart = cbuf;

    char *argv[1024] = {"addr2line", "-Cfe", "CorsixTH/CorsixTH", NULL};
    int nArg = 3;

    unw_getcontext(&uc);
    unw_init_local(&c, &uc);
    while (unw_step(&c) > 0) {
        unw_get_proc_info(&c, &pi);

        unw_get_reg(&c, UNW_REG_IP, &ip);

        if (frames++ <= 2) continue; //skip first two

        static char buf[1024];
        unw_get_proc_name(&c, buf, sizeof(buf)/sizeof(buf[0]), NULL);
        if (buf[0] == '_' && buf[1] == 'i' && buf[2] == 'n' && buf[3] == 'i') {
            // Skip over the lua _init function
            continue;
        }

        printf("%s(%p) <- ", buf, reinterpret_cast<void*>(ip));

        int n = sprintf(bufstart, "%p", ip);
        bufstart[n+1] = 0;
        argv[nArg++] = bufstart;
        argv[nArg+1] = NULL;
        bufstart += n+1;

        // if (buf[0] == 'm' && buf[1] == 'a' && buf[2] == 'i' && buf[3] == 'n') {
        if (pi.start_ip == reinterpret_cast<unw_word_t>(&main)) {
            printf("program start\n");
            break;
        }
    }

    lua_State *L = mainloop_co;
    luaL_loadstring(L, "print(debug.traceback(\"\", 2))");
    lua_call(L, 0, 0);
    L = globL;
    luaL_loadstring(L, "print(debug.traceback(\"\", 2))");
    lua_call(L, 0, 0);

    printf("\n");

    execve("/usr/bin/addr2line", argv, NULL);
    fprintf(stderr, "addr2line failed. Run sudo apt-get install binutils\n");
    fflush(stderr);

    _exit(1);
}


void handler(int, siginfo_t *, void *) {
    const char buf[] = {"\n! SEGFAULT !\n\n"};
    write(1, buf, sizeof(buf)/sizeof(buf[0]));
    show_backtrace();
    _exit(1);
}

//! Program entry point
/*!
    Prepares a Lua state for, and catches errors from, CorsixTH_lua_main(). By
    executing in Lua mode as soon as possible, errors can be nicely caught
    sooner, hence this function does as little as possible and leaves the rest
    for CorsixTH_lua_main().
*/
int main(int argc, char** argv)
{
    struct compile_time_lua_check
    {
        // Lua 5.1, not 5.0, is required
        int lua_5_point_1_required[LUA_VERSION_NUM >= 501 ? 1 : -1];

        // Lua numbers must be doubles so that the mantissa has at least
        // 32 bits (floats only have 24 bits)
        int number_is_double[types_equal<lua_Number, double>::result];
    };

    bool bRun = true;


    struct sigaction a;

    sigemptyset (&a.sa_mask);
    a.sa_flags = SA_ONSTACK | SA_SIGINFO;
    a.sa_sigaction = handler;

    sigaction(SIGSEGV, &a, NULL);
    printf("SIGSEGV handler configured\n");

    while(bRun)
    {
        lua_State *L = NULL;

        L = luaL_newstate();
        globL = L;
        if(L == NULL)
        {
            fprintf(stderr, "Fatal error starting CorsixTH: "
                "Cannot open Lua state.\n");
            return 0;
        }

        lua_atpanic(L, CorsixTH_lua_panic);
        luaL_openlibs(L);
        lua_settop(L, 0);
        lua_pushcfunction(L, CorsixTH_lua_stacktrace);
        lua_pushcfunction(L, CorsixTH_lua_main);

        // Move command line parameters onto the Lua stack
        lua_checkstack(L, argc);
        for(int i = 0; i < argc; ++i)
        {
            lua_pushstring(L, argv[i]);
        }

        if(lua_pcall(L, argc, 0, 1) != 0)
        {

            CorsixTH_lua_stacktrace(L);

            // int level = 0;
            // lua_Debug ar;
            // // GCfunc *fn;
            // while (lua_getstack(L, level++, &ar)) {
            //     lua_getinfo(L, "Snlf", &ar);
            //     printf("Level %d\n", level);
            //     // fn = funcV(L->top - level);
            //     if (ar.currentline > 0)
            //         printf("line %d", ar.currentline);
            //     if (*ar.namewhat) {
            //         printf(" in function %s ", ar.name);
            //     } else {
            //         if (*ar.what == 'm') {
            //             printf(" in main chunk");
            //         } else if (*ar.what == 'C') {
            //             printf(" at %p", NULL);
            //         } else {
            //             printf(" in function <%s:%d>", ar.short_src, ar.linedefined);
            //         }
            //     }
            //     printf("\n");
            // }

            const char* err = lua_tostring(L, -1);
            if(err != NULL)
            {
                fprintf(stderr, "%s\n", err);
            }
            else
            {
                fprintf(stderr, "An error has occured in CorsixTH:\n"
                    "Uncaught non-string Lua error\n");
            }
            lua_pushcfunction(L, Bootstrap_lua_error_report);
            lua_insert(L, -2);
            if(lua_pcall(L, 1, 0, 0) != 0)
            {
                fprintf(stderr, "%s\n", lua_tostring(L, -1));
            }
        }

        lua_getfield(L, LUA_REGISTRYINDEX, "_RESTART");
        bRun = lua_toboolean(L, -1) != 0;

        // Get cleanup functions out of the Lua state (but don't run them yet)
        std::stack<void(*)(void)> stkCleanup;
        lua_getfield(L, LUA_REGISTRYINDEX, "_CLEANUP");
        if(lua_type(L, -1) == LUA_TTABLE)
        {
            for(unsigned int i = 1; i <= lua_objlen(L, -1); ++i)
            {
                lua_rawgeti(L, -1, (int)i);
                stkCleanup.push((void(*)(void))lua_touserdata(L, -1));
                lua_pop(L, 1);
            }
        }

        lua_close(L);

        // The cleanup functions are executed _after_ the Lua state is fully
        // closed, and in reserve order to that in which they were registered.
        while(!stkCleanup.empty())
        {
            if(stkCleanup.top() != NULL)
                stkCleanup.top()();
            stkCleanup.pop();
        }

        if(bRun)
        {
            printf("\n\nRestarting...\n\n\n");
        }
    }
    return 0;
}
