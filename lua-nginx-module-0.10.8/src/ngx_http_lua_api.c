
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_http_lua_common.h"
#include "api/ngx_http_lua_api.h"
#include "ngx_http_lua_shdict.h"
#include "ngx_http_lua_util.h"


lua_State *
ngx_http_lua_get_global_state(ngx_conf_t *cf)
{
    ngx_http_lua_main_conf_t *lmcf;

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_module);

    return lmcf->lua;
}


ngx_http_request_t *
ngx_http_lua_get_request(lua_State *L)
{
    return ngx_http_lua_get_req(L);
}


static ngx_int_t ngx_http_lua_shared_memory_init(ngx_shm_zone_t *shm_zone,
    void *data);


ngx_int_t
ngx_http_lua_add_package_preload(ngx_conf_t *cf, const char *package,
    lua_CFunction func)
{
    lua_State                     *L;
    ngx_http_lua_main_conf_t      *lmcf;
    ngx_http_lua_preload_hook_t   *hook;

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_module);

    L = lmcf->lua;

    if (L) {
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "preload");
        lua_pushcfunction(L, func);
        lua_setfield(L, -2, package);
        lua_pop(L, 2);
    }

    /* we always register preload_hooks since we always create new Lua VMs
     * when lua code cache is off. */

    if (lmcf->preload_hooks == NULL) {
        lmcf->preload_hooks =
            ngx_array_create(cf->pool, 4,
                             sizeof(ngx_http_lua_preload_hook_t));

        if (lmcf->preload_hooks == NULL) {
            return NGX_ERROR;
        }
    }

    hook = ngx_array_push(lmcf->preload_hooks);
    if (hook == NULL) {
        return NGX_ERROR;
    }

    hook->package = (u_char *) package;
    hook->loader = func;

    return NGX_OK;
}


ngx_shm_zone_t *
ngx_http_lua_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name, size_t size,
    void *tag)
{
    ngx_http_lua_main_conf_t     *lmcf;
    ngx_shm_zone_t              **zp; //zone共享内存的数组指针
    ngx_shm_zone_t               *zone;
    ngx_http_lua_shm_zone_ctx_t  *ctx;
    ngx_int_t                     n;

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_module);
    if (lmcf == NULL) {
        return NULL;
    }

    if (lmcf->shm_zones == NULL) {
        lmcf->shm_zones = ngx_palloc(cf->pool, sizeof(ngx_array_t));
        if (lmcf->shm_zones == NULL) {
            return NULL;
        }

        if (ngx_array_init(lmcf->shm_zones, cf->pool, 2,
                           sizeof(ngx_shm_zone_t *))
            != NGX_OK)
        {
            return NULL;
        }
    }
    //此API由nginx自身提供的共享内存创建接口
    //name是共享字典定义的名称，如lua_shared_dict dog 1m; name是dog
    //tag标记是&ngx_http_lua_module，ngx_module_t模块定义结构体
    zone = ngx_shared_memory_add(cf, name, (size_t) size, tag);
    if (zone == NULL) {
        return NULL; //申请共享内存失败，直接返回
    }
    //该共享内存已经存在对应的数据，直接返回该共享内存对象
    if (zone->data) {
        ctx = (ngx_http_lua_shm_zone_ctx_t *) zone->data;
        return &ctx->zone;
    }
    //共享内存上下文结构体大小
    n = sizeof(ngx_http_lua_shm_zone_ctx_t);
    //从内存池里申请分配对应大小空间的结构体，初始化
    ctx = ngx_pcalloc(cf->pool, n);
    if (ctx == NULL) {
        return NULL;
    }
    //共享内存上下文 各属性赋值，注意 此时ctx->zone上下文的共享内存对象并未赋值
    ctx->lmcf = lmcf;
    ctx->log = &cf->cycle->new_log;
    ctx->cycle = cf->cycle;
    //对共享内存上下文的共享内存对象进行地址拷贝，指向已申请内存空间的zone
    ngx_memcpy(&ctx->zone, zone, sizeof(ngx_shm_zone_t));
    //将共享内存zone塞进共享内存数组进行管理
    zp = ngx_array_push(lmcf->shm_zones);
    if (zp == NULL) {
        return NULL;
    }

    *zp = zone;

    /* set zone init */
    zone->init = ngx_http_lua_shared_memory_init; //指定初始化函数 将在ngx_init_cycle里被调用
    zone->data = ctx; //共享内存的数据指向自定义的数据结构 共享内存上下文ctx--已初始化并赋值

    lmcf->requires_shm = 1; //标明ngx_lua需要使用共享内存
    //返回共享内存上下文里的共享内存对象
    return &ctx->zone;
}


static ngx_int_t
ngx_http_lua_shared_memory_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_lua_shm_zone_ctx_t *octx = data;
    ngx_shm_zone_t              *ozone;
    void                        *odata;

    ngx_int_t                    rc; //lmcf->init_handler 返回码
    volatile ngx_cycle_t        *saved_cycle;
    ngx_http_lua_main_conf_t    *lmcf;
    ngx_http_lua_shm_zone_ctx_t *ctx;
    ngx_shm_zone_t              *zone;
    //共享内存的数据指向自定义的数据结构，所以可进行类型转换
    ctx = (ngx_http_lua_shm_zone_ctx_t *) shm_zone->data;
    zone = &ctx->zone; //共享内存上下文的共享内存对象

    odata = NULL;
    if (octx) { //源的共享内存上下文 origin ctx 在nginx -s reload时出现该情况，这就是reload不会清空共享字典的真正原因
        ozone = &octx->zone;
        odata = ozone->data;
    }
    //真正的共享内存
    zone->shm = shm_zone->shm;
#if defined(nginx_version) && nginx_version >= 1009000
    zone->noreuse = shm_zone->noreuse;
#endif
    //此init就是 ngx_http_lua_shdict_init_zone 在ngx_http_lua_directive的ngx_http_lua_shared_dict被定义
    if (zone->init(zone, odata) != NGX_OK) {
        return NGX_ERROR;
    }

    dd("get lmcf");

    lmcf = ctx->lmcf;
    if (lmcf == NULL) {
        return NGX_ERROR;
    }

    dd("lmcf->lua: %p", lmcf->lua);

    lmcf->shm_zones_inited++; //每次共享内存初始化后累计
    //已初始化共享内存数量与登记的共享内存数组元素个数相同，且存在初始化处理器init_by_lua_xxx
    if (lmcf->shm_zones_inited == lmcf->shm_zones->nelts
        && lmcf->init_handler)
    {
        saved_cycle = ngx_cycle;
        ngx_cycle = ctx->cycle;

        rc = lmcf->init_handler(ctx->log, lmcf, lmcf->lua);

        ngx_cycle = saved_cycle;

        if (rc != NGX_OK) {
            /* an error happened */
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
