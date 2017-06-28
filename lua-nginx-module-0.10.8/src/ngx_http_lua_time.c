
/*
 * Copyright (C) Xiaozhe Wang (chaoslawful)
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_http_lua_time.h"
#include "ngx_http_lua_util.h"


static int ngx_http_lua_ngx_today(lua_State *L);
static int ngx_http_lua_ngx_time(lua_State *L);
static int ngx_http_lua_ngx_now(lua_State *L);
static int ngx_http_lua_ngx_localtime(lua_State *L);
static int ngx_http_lua_ngx_utctime(lua_State *L);
static int ngx_http_lua_ngx_cookie_time(lua_State *L);
static int ngx_http_lua_ngx_http_time(lua_State *L);
static int ngx_http_lua_ngx_parse_http_time(lua_State *L);
static int ngx_http_lua_ngx_update_time(lua_State *L);
static int ngx_http_lua_ngx_req_start_time(lua_State *L);


static int
ngx_http_lua_ngx_today(lua_State *L)
{
    time_t                   now;
    ngx_tm_t                 tm;
    u_char                   buf[sizeof("2010-11-19") - 1];

    now = ngx_time();
    ngx_gmtime(now + ngx_cached_time->gmtoff * 60, &tm); //时区转换

    ngx_sprintf(buf, "%04d-%02d-%02d", tm.ngx_tm_year, tm.ngx_tm_mon,
                tm.ngx_tm_mday); //当前日期

    lua_pushlstring(L, (char *) buf, sizeof(buf));

    return 1;
}


static int
ngx_http_lua_ngx_localtime(lua_State *L)
{
    ngx_tm_t                 tm;

    u_char buf[sizeof("2010-11-19 20:56:31") - 1];

    ngx_gmtime(ngx_time() + ngx_cached_time->gmtoff * 60, &tm); //以分钟为单位进行时区转换

    ngx_sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.ngx_tm_year,
                tm.ngx_tm_mon, tm.ngx_tm_mday, tm.ngx_tm_hour, tm.ngx_tm_min,
                tm.ngx_tm_sec);

    lua_pushlstring(L, (char *) buf, sizeof(buf));

    return 1;
}


static int
ngx_http_lua_ngx_time(lua_State *L)
{
    lua_pushnumber(L, (lua_Number) ngx_time()); //时间戳 到毫秒

    return 1;
}


static int
ngx_http_lua_ngx_now(lua_State *L)
{
    ngx_time_t              *tp;

    tp = ngx_timeofday();

    lua_pushnumber(L, (lua_Number) (tp->sec + tp->msec / 1000.0L)); //返回时间戳 到秒

    return 1;
}


static int
ngx_http_lua_ngx_update_time(lua_State *L)
{
    ngx_time_update();
    return 0;
}


static int
ngx_http_lua_ngx_utctime(lua_State *L)
{
    ngx_tm_t       tm;
    u_char         buf[sizeof("2010-11-19 20:56:31") - 1];

    ngx_gmtime(ngx_time(), &tm); //当前时间值对象赋值给tm

    ngx_sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.ngx_tm_year,
                tm.ngx_tm_mon, tm.ngx_tm_mday, tm.ngx_tm_hour, tm.ngx_tm_min,
                tm.ngx_tm_sec); //格式转换赋值给buf

    lua_pushlstring(L, (char *) buf, sizeof(buf)); //输出相应字符串

    return 1;
}


static int
ngx_http_lua_ngx_cookie_time(lua_State *L)
{
    time_t                               t;
    u_char                              *p;

    u_char   buf[sizeof("Mon, 28 Sep 1970 06:00:00 GMT") - 1];

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument");
    }

    t = (time_t) luaL_checknumber(L, 1); //时间戳

    p = buf;
    p = ngx_http_cookie_time(p, t);

    lua_pushlstring(L, (char *) buf, p - buf);

    return 1;
}


static int
ngx_http_lua_ngx_http_time(lua_State *L)
{
    time_t                               t;
    u_char                              *p;

    u_char   buf[sizeof("Mon, 28 Sep 1970 06:00:00 GMT") - 1];

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument");
    }

    t = (time_t) luaL_checknumber(L, 1);

    p = buf;
    p = ngx_http_time(p, t);

    lua_pushlstring(L, (char *) buf, p - buf);

    return 1;
}


static int
ngx_http_lua_ngx_parse_http_time(lua_State *L)
{
    u_char                              *p;
    size_t                               len;
    time_t                               time;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expecting one argument");
    }

    p = (u_char *) luaL_checklstring(L, 1, &len);

    time = ngx_http_parse_time(p, len); //http_time转换成时间戳
    if (time == NGX_ERROR) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushnumber(L, (lua_Number) time);

    return 1;
}


static int
ngx_http_lua_ngx_req_start_time(lua_State *L)
{
    ngx_http_request_t  *r;

    r = ngx_http_lua_get_req(L);
    if (r == NULL) {
        return luaL_error(L, "no request found");
    }

    lua_pushnumber(L, (lua_Number) (r->start_sec + r->start_msec / 1000.0L));
    return 1;
}


void
ngx_http_lua_inject_time_api(lua_State *L)
{
    lua_pushcfunction(L, ngx_http_lua_ngx_utctime);
    lua_setfield(L, -2, "utctime"); //ngx.utctime()

    lua_pushcfunction(L, ngx_http_lua_ngx_time);
    lua_setfield(L, -2, "get_now_ts"); /* deprecated */

    lua_pushcfunction(L, ngx_http_lua_ngx_localtime);
    lua_setfield(L, -2, "get_now"); /* deprecated */

    lua_pushcfunction(L, ngx_http_lua_ngx_localtime);
    lua_setfield(L, -2, "localtime"); //ngx.localtime()

    lua_pushcfunction(L, ngx_http_lua_ngx_time);
    lua_setfield(L, -2, "time"); //ngx.time() 毫秒时间戳

    lua_pushcfunction(L, ngx_http_lua_ngx_now);
    lua_setfield(L, -2, "now"); //ngx.now() 秒时间戳

    lua_pushcfunction(L, ngx_http_lua_ngx_update_time);
    lua_setfield(L, -2, "update_time"); //触发更新nginx缓存时间

    lua_pushcfunction(L, ngx_http_lua_ngx_today);
    lua_setfield(L, -2, "get_today"); /* deprecated */

    lua_pushcfunction(L, ngx_http_lua_ngx_today);
    lua_setfield(L, -2, "today");

    lua_pushcfunction(L, ngx_http_lua_ngx_cookie_time);
    lua_setfield(L, -2, "cookie_time");

    lua_pushcfunction(L, ngx_http_lua_ngx_http_time);
    lua_setfield(L, -2, "http_time");

    lua_pushcfunction(L, ngx_http_lua_ngx_parse_http_time);
    lua_setfield(L, -2, "parse_http_time");
}


void
ngx_http_lua_inject_req_time_api(lua_State *L)
{
    lua_pushcfunction(L, ngx_http_lua_ngx_req_start_time);
    lua_setfield(L, -2, "start_time");
}


#ifndef NGX_LUA_NO_FFI_API
double
ngx_http_lua_ffi_now(void)
{
    ngx_time_t              *tp;

    tp = ngx_timeofday();

    return tp->sec + tp->msec / 1000.0;
}


double
ngx_http_lua_ffi_req_start_time(ngx_http_request_t *r)
{
    return r->start_sec + r->start_msec / 1000.0;
}


long
ngx_http_lua_ffi_time(void)
{
    return (long) ngx_time();
}
#endif /* NGX_LUA_NO_FFI_API */


/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
