/* clang-format off */
/*****************************************************************************
  The MIT License

  Copyright © 2013 Pavel Karelin (hkarel), <hkarel@yandex.ru>
  Author of idea: Denis Fedorin

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
  ---

  The module implemented the program breakpoint.

*****************************************************************************/

#pragma once

/*** Original example ***
void sigtrap_handler(int signo)
{}

int debug_init()
{
    return signal(SIGTRAP, sigtrap_handler) == SIG_ERR ? -1 : 0;
}

#define DebugBreak() {asm volatile("int $3");}

int main ()
{
    int a, b, c, max;
    debug_init();
    scanf("%d %d %d", &a, &b, &c);
    if (a > b)
        max = a;
    else
        max = b;
    DebugBreak();
    if (c > max)
        max = c;
    printf("%d\n", max);
    return 0;
}
*/

#ifndef NDEBUG
  #if defined(_MSC_VER) || defined(__MINGW32__)
    #include <intrin.h>
    #define break_point  __debugbreak();
  #elif defined(__arm__)
    #define break_point
  #else
    #include <stdio.h>
    #include <signal.h>
    #include <stdlib.h>
    #include <assert.h>

    static void sigtrap_handler(int /*signo*/)
    {}
    struct InitBreakPoint
    {
        InitBreakPoint() {
            assert(signal(SIGTRAP, sigtrap_handler) != SIG_ERR);
        }
    } static init_break_point;
    //#define break_point  {asm volatile("int $3");}
    #define break_point  {asm volatile("int $3" ::: "cc", "memory");}
  #endif
#else
    #define break_point
#endif
