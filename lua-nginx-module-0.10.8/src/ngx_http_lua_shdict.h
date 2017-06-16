
/*
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef _NGX_HTTP_LUA_SHDICT_H_INCLUDED_
#define _NGX_HTTP_LUA_SHDICT_H_INCLUDED_


#include "ngx_http_lua_common.h"

//用于存放数据的每一个节点的数据结构
typedef struct {
    u_char                       color;//红黑树 用于标识节点颜色
    uint8_t                      value_type;//值类型，如TNIL\TBOOLEAN\TNUMBER\TSTRING
    u_short                      key_len;//键的长度
    uint32_t                     value_len;//值的长度
    uint64_t                     expires;//过期时间
    ngx_queue_t                  queue;//LRU队列节点
    uint32_t                     user_flags;//用户设置的标志
    u_char                       data[1];//键字符串的第一个字符
} ngx_http_lua_shdict_node_t;

//共享字典列表节点数据结构
typedef struct {
    ngx_queue_t                  queue;
    uint32_t                     value_len;
    uint8_t                      value_type;
    u_char                       data[1];
} ngx_http_lua_shdict_list_node_t;

//共享字典在共享内存中的上下文数据结构
typedef struct {
    ngx_rbtree_t                  rbtree;//红黑树根节点root
    ngx_rbtree_node_t             sentinel;//哨兵节点
    ngx_queue_t                   lru_queue;//LRU队列的哨兵节点
} ngx_http_lua_shdict_shctx_t;

//共享字典上下文数据结构
typedef struct {
    ngx_http_lua_shdict_shctx_t  *sh;
    ngx_slab_pool_t              *shpool;
    ngx_str_t                     name;
    ngx_http_lua_main_conf_t     *main_conf;
    ngx_log_t                    *log;
} ngx_http_lua_shdict_ctx_t;

//共享内存上下文数据结构
typedef struct {
    ngx_log_t                   *log;
    ngx_http_lua_main_conf_t    *lmcf;
    ngx_cycle_t                 *cycle;
    ngx_shm_zone_t               zone;//指向共享内存对象
} ngx_http_lua_shm_zone_ctx_t;


ngx_int_t ngx_http_lua_shdict_init_zone(ngx_shm_zone_t *shm_zone, void *data);
void ngx_http_lua_shdict_rbtree_insert_value(ngx_rbtree_node_t *temp,
    ngx_rbtree_node_t *node, ngx_rbtree_node_t *sentinel);
void ngx_http_lua_inject_shdict_api(ngx_http_lua_main_conf_t *lmcf,
    lua_State *L);


#endif /* _NGX_HTTP_LUA_SHDICT_H_INCLUDED_ */

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
