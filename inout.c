/*
** inout.c
** Provide function to realise the input/output function and debugger 
** facilities.
** Also provides some predefined lua functions.
*/

char *rcs_inout="$Id: inout.c,v 2.50 1997/03/31 20:59:09 roberto Exp roberto $";

#include <stdio.h>
#include <string.h>

#include "auxlib.h"
#include "lex.h"
#include "opcode.h"
#include "inout.h"
#include "table.h"
#include "tree.h"
#include "lua.h"
#include "hash.h"
#include "luamem.h"
#include "fallback.h"


/* Exported variables */
Word lua_linenumber;
char *lua_parsedfile;


static FILE *fp;
static char *st;

/*
** Function to get the next character from the input file
*/
static int fileinput (void)
{
  int c = fgetc(fp);
  return (c == EOF) ? 0 : c;
}

/*
** Function to get the next character from the input string
*/
static int stringinput (void)
{
 return *st++;
}

/*
** Function to open a file to be input unit. 
** Return the file.
*/
FILE *lua_openfile (char *fn)
{
 lua_setinput (fileinput);
 if (fn == NULL)
 {
   fp = stdin;
   fn = "(stdin)";
 }
 else
   fp = fopen (fn, "r");
 if (fp == NULL)
   return NULL;
 lua_parsedfile = luaI_createfixedstring(fn)->str;
 return fp;
}

/*
** Function to close an opened file
*/
void lua_closefile (void)
{
 if (fp != NULL && fp != stdin)
 {
  fclose (fp);
  fp = NULL;
 }
}

/*
** Function to open a string to be input unit
*/
#define SIZE_PREF 20  /* size of string prefix to appear in error messages */
void lua_openstring (char *s)
{
  char buff[SIZE_PREF+25];
  lua_setinput(stringinput);
  st = s;
  strcpy(buff, "(dostring) >> ");
  strncat(buff, s, SIZE_PREF);
  if (strlen(s) > SIZE_PREF) strcat(buff, "...");
  lua_parsedfile = luaI_createfixedstring(buff)->str;
}

/*
** Function to close an opened string
*/
void lua_closestring (void)
{
}


static int passresults (void)
{
  int arg = 0;
  lua_Object obj;
  while ((obj = lua_getresult(++arg)) != LUA_NOOBJECT)
    lua_pushobject(obj);
  return arg-1;
}
 
/*
** Internal function: do a string
*/
static void lua_internaldostring (void)
{
  if (lua_dostring(luaL_check_string(1, "dostring")) == 0)
    if (passresults() == 0)
      lua_pushuserdata(NULL);  /* at least one result to signal no errors */
}

/*
** Internal function: do a file
*/
static void lua_internaldofile (void)
{
 char *fname = luaL_opt_string(1, NULL, "dofile");
 if (lua_dofile(fname) == 0)
    if (passresults() == 0)
      lua_pushuserdata(NULL);  /* at least one result to signal no errors */
}
 

static char *tostring (lua_Object obj)
{
  if (lua_isstring(obj))   /* get strings and numbers */
    return lua_getstring(obj);
  else if (lua_istable(obj))
    return "<table>";
  else if (lua_isfunction(obj))
    return "<function>";
  else if (lua_isnil(obj))
    return "nil";
  else /* if (lua_isuserdata(obj)) */
    return "<userdata>";
}

static void luaI_tostring (void)
{
  lua_pushstring(tostring(lua_getparam(1)));
}

static void luaI_print (void)
{
  int i = 1;
  lua_Object obj;
  while ((obj = lua_getparam(i++)) != LUA_NOOBJECT)
    printf("%s\n", tostring(obj));
}

/*
** Internal function: convert an object to a number
*/
static void lua_obj2number (void)
{
  lua_Object o = lua_getparam(1);
  if (lua_isnumber(o))
    lua_pushnumber(lua_getnumber(o));
}


static void luaI_error (void)
{
  char *s = lua_getstring(lua_getparam(1));
  if (s == NULL) s = "(no message)";
  lua_error(s);
}

static void luaI_assert (void)
{
  lua_Object p = lua_getparam(1);
  if (p == LUA_NOOBJECT || lua_isnil(p))
    lua_error("assertion failed!");
}

static void luaI_setglobal (void)
{
  lua_Object value = lua_getparam(2);
  luaL_arg_check(value != LUA_NOOBJECT, "setglobal", 2, NULL);
  lua_pushobject(value);
  lua_storeglobal(luaL_check_string(1, "setglobal"));
  lua_pushobject(value);  /* return given value */
}

static void luaI_basicsetglobal (void)
{
  lua_Object value = lua_getparam(2);
  luaL_arg_check(value != LUA_NOOBJECT, "basicsetglobal", 2, NULL);
  lua_pushobject(value);
  lua_basicstoreglobal(luaL_check_string(1, "basicsetglobal"));
  lua_pushobject(value);  /* return given value */
}

static void luaI_getglobal (void)
{
  lua_pushobject(lua_getglobal(luaL_check_string(1, "getglobal")));
}

static void luaI_basicgetglobal (void)
{
  lua_pushobject(lua_basicgetglobal(luaL_check_string(1, "basicgetglobal")));
}

static void luatag (void)
{
  lua_pushnumber(lua_tag(lua_getparam(1)));
}

#define MAXPARAMS	256
static void luaI_call (void)
{
  lua_Object f = lua_getparam(1);
  lua_Object arg = lua_getparam(2);
  lua_Object temp, params[MAXPARAMS];
  int narg, i;
  luaL_arg_check(lua_isfunction(f), "call", 1, "function expected");
  luaL_arg_check(lua_istable(arg), "call", 2, "table expected");
  /* narg = arg.n */
  lua_pushobject(arg);
  lua_pushstring("n");
  temp = lua_getsubscript();
  narg = lua_isnumber(temp) ? lua_getnumber(temp) : MAXPARAMS+1;
  /* read arg[1...n] */
  for (i=0; i<narg; i++) {
    if (i>=MAXPARAMS)
      lua_error("argument list too long in function `call'");
    lua_pushobject(arg);
    lua_pushnumber(i+1);
    params[i] = lua_getsubscript();
    if (narg == MAXPARAMS+1 && lua_isnil(params[i])) {
      narg = i;
      break;
    }
  }
  /* push parameters and do the call */
  for (i=0; i<narg; i++)
    lua_pushobject(params[i]);
  if (lua_callfunction(f))
    lua_error(NULL);
  else
    passresults();
}

static void luaIl_settag (void)
{
  lua_Object o = lua_getparam(1);
  luaL_arg_check(o != LUA_NOOBJECT, "settag", 1, NULL);
  lua_pushobject(o);
  lua_settag(luaL_check_number(2, "settag"));
}

static void luaIl_newtag (void)
{
  lua_pushnumber(lua_newtag(luaL_check_string(1, "newtag")));
}

static void basicindex (void)
{
  lua_Object t = lua_getparam(1);
  lua_Object i = lua_getparam(2);
  luaL_arg_check(t != LUA_NOOBJECT, "basicindex", 1, NULL);
  luaL_arg_check(i != LUA_NOOBJECT, "basicindex", 2, NULL);
  lua_pushobject(t);
  lua_pushobject(i);
  lua_pushobject(lua_basicindex());
}

static void basicstoreindex (void)
{
  lua_Object t = lua_getparam(1);
  lua_Object i = lua_getparam(2);
  lua_Object v = lua_getparam(3);
  luaL_arg_check(t != LUA_NOOBJECT && i != LUA_NOOBJECT && v != LUA_NOOBJECT,
            "basicindex", 0, NULL);
  lua_pushobject(t);
  lua_pushobject(i);
  lua_pushobject(v);
  lua_basicstoreindex();
}



/*
** Internal functions
*/
static struct {
  char *name;
  lua_CFunction func;
} int_funcs[] = {
  {"assert", luaI_assert},
  {"basicgetglobal", luaI_basicgetglobal},
  {"basicindex", basicindex},
  {"basicsetglobal", luaI_basicsetglobal},
  {"basicstoreindex", basicstoreindex},
  {"call", luaI_call},
  {"dofile", lua_internaldofile},
  {"dostring", lua_internaldostring},
  {"error", luaI_error},
  {"getglobal", luaI_getglobal},
  {"newtag", luaIl_newtag},
  {"next", lua_next},
  {"nextvar", luaI_nextvar},
  {"print", luaI_print},
  {"seterrormethod", luaI_seterrormethod},
  {"setfallback", luaI_setfallback},
  {"setglobal", luaI_setglobal},
  {"setintmethod", luaI_setintmethod},
  {"settag", luaIl_settag},
  {"tonumber", lua_obj2number},
  {"tostring", luaI_tostring},
  {"tag", luatag},
  {"type", luaI_type}
};

#define INTFUNCSIZE (sizeof(int_funcs)/sizeof(int_funcs[0]))


void luaI_predefine (void)
{
  int i;
  Word n;
  for (i=0; i<INTFUNCSIZE; i++) {
    n = luaI_findsymbolbyname(int_funcs[i].name);
    s_ttype(n) = LUA_T_CFUNCTION; s_fvalue(n) = int_funcs[i].func;
  }
  n = luaI_findsymbolbyname("_VERSION_");
  s_ttype(n) = LUA_T_STRING; s_tsvalue(n) = lua_createstring(LUA_VERSION);
}


