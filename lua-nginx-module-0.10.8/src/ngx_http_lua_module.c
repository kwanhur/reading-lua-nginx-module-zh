
/*
 * Copyright (C) Xiaozhe Wang (chaoslawful)
 * Copyright (C) Yichun Zhang (agentzh)
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include "ngx_http_lua_directive.h"
#include "ngx_http_lua_capturefilter.h"
#include "ngx_http_lua_contentby.h"
#include "ngx_http_lua_rewriteby.h"
#include "ngx_http_lua_accessby.h"
#include "ngx_http_lua_logby.h"
#include "ngx_http_lua_util.h"
#include "ngx_http_lua_headerfilterby.h"
#include "ngx_http_lua_bodyfilterby.h"
#include "ngx_http_lua_initby.h"
#include "ngx_http_lua_initworkerby.h"
#include "ngx_http_lua_probe.h"
#include "ngx_http_lua_semaphore.h"
#include "ngx_http_lua_balancer.h"
#include "ngx_http_lua_ssl_certby.h"
#include "ngx_http_lua_ssl_session_storeby.h"
#include "ngx_http_lua_ssl_session_fetchby.h"


static void *ngx_http_lua_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_lua_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_lua_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_lua_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static void *ngx_http_lua_create_loc_conf(ngx_conf_t *cf);

static char *ngx_http_lua_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_lua_init(ngx_conf_t *cf);
static char *ngx_http_lua_lowat_check(ngx_conf_t *cf, void *post, void *data);
#if (NGX_HTTP_SSL)
static ngx_int_t ngx_http_lua_set_ssl(ngx_conf_t *cf,
    ngx_http_lua_loc_conf_t *llcf);
#endif
static char *ngx_http_lua_malloc_trim(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_conf_post_t  ngx_http_lua_lowat_post =
    { ngx_http_lua_lowat_check };


static volatile ngx_cycle_t  *ngx_http_lua_prev_cycle = NULL;


#if (NGX_HTTP_SSL) && defined(nginx_version) && nginx_version >= 1001013

static ngx_conf_bitmask_t  ngx_http_lua_ssl_protocols[] = {
    { ngx_string("SSLv2"), NGX_SSL_SSLv2 },
    { ngx_string("SSLv3"), NGX_SSL_SSLv3 },
    { ngx_string("TLSv1"), NGX_SSL_TLSv1 },
    { ngx_string("TLSv1.1"), NGX_SSL_TLSv1_1 },
    { ngx_string("TLSv1.2"), NGX_SSL_TLSv1_2 },
    { ngx_null_string, 0 }
};

#endif


static ngx_command_t ngx_http_lua_cmds[] = {

    { ngx_string("lua_max_running_timers"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_main_conf_t, max_running_timers),
      NULL },

    { ngx_string("lua_max_pending_timers"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_main_conf_t, max_pending_timers),
      NULL },

    { ngx_string("lua_shared_dict"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_lua_shared_dict,
      0,
      0,
      NULL },

#if (NGX_PCRE)
    { ngx_string("lua_regex_cache_max_entries"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_main_conf_t, regex_cache_max_entries),
      NULL },

    { ngx_string("lua_regex_match_limit"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_main_conf_t, regex_match_limit),
      NULL },
#endif

    { ngx_string("lua_package_cpath"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_package_cpath,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("lua_package_path"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_package_path,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("lua_code_cache"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_http_lua_code_cache,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, enable_code_cache),
      NULL },

    { ngx_string("lua_need_request_body"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, force_read_body),
      NULL },

    { ngx_string("lua_transform_underscores_in_response_headers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, transform_underscores_in_resp_headers),
      NULL },

     { ngx_string("lua_socket_log_errors"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, log_socket_errors),
      NULL },

    { ngx_string("init_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_init_by_lua_block,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_init_by_inline },

    { ngx_string("init_by_lua"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_init_by_lua,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_init_by_inline },

    { ngx_string("init_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_init_by_lua,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_init_by_file },

    { ngx_string("init_worker_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_init_worker_by_lua_block,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_init_worker_by_inline },

    { ngx_string("init_worker_by_lua"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_init_worker_by_lua,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_init_worker_by_inline },

    { ngx_string("init_worker_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_init_worker_by_lua,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_init_worker_by_file },

#if defined(NDK) && NDK
    /* set_by_lua $res { inline Lua code } [$arg1 [$arg2 [...]]] */
    { ngx_string("set_by_lua_block"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_1MORE|NGX_CONF_BLOCK,
      ngx_http_lua_set_by_lua_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_filter_set_by_lua_inline },

    /* set_by_lua $res <inline script> [$arg1 [$arg2 [...]]] */
    { ngx_string("set_by_lua"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_2MORE,
      ngx_http_lua_set_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_filter_set_by_lua_inline },

    /* set_by_lua_file $res rel/or/abs/path/to/script [$arg1 [$arg2 [..]]] */
    { ngx_string("set_by_lua_file"),
      NGX_HTTP_SRV_CONF|NGX_HTTP_SIF_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                       |NGX_CONF_2MORE,
      ngx_http_lua_set_by_lua_file,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_filter_set_by_lua_file },
#endif

    /* rewrite_by_lua "<inline script>" */
    { ngx_string("rewrite_by_lua"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_rewrite_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_rewrite_handler_inline },

    /* rewrite_by_lua_block { <inline script> } */
    { ngx_string("rewrite_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_rewrite_by_lua_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_rewrite_handler_inline },

    /* access_by_lua "<inline script>" */
    { ngx_string("access_by_lua"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_access_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_access_handler_inline },

    /* access_by_lua_block { <inline script> } */
    { ngx_string("access_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_access_by_lua_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_access_handler_inline },

    /* content_by_lua "<inline script>" */
    { ngx_string("content_by_lua"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_content_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_content_handler_inline },

    /* content_by_lua_block { <inline script> } */
    { ngx_string("content_by_lua_block"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_content_by_lua_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_content_handler_inline },

    /* log_by_lua <inline script> */
    { ngx_string("log_by_lua"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_log_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_log_handler_inline },

    /* log_by_lua_block { <inline script> } */
    { ngx_string("log_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_log_by_lua_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_log_handler_inline },

    { ngx_string("rewrite_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_rewrite_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_rewrite_handler_file },

    { ngx_string("rewrite_by_lua_no_postpone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_main_conf_t, postponed_to_rewrite_phase_end),
      NULL },

    { ngx_string("access_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_access_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_access_handler_file },

    { ngx_string("access_by_lua_no_postpone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_lua_main_conf_t, postponed_to_access_phase_end),
      NULL },

    /* content_by_lua_file rel/or/abs/path/to/script */
    { ngx_string("content_by_lua_file"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_content_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_content_handler_file },

    { ngx_string("log_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_log_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_log_handler_file },

    /* header_filter_by_lua <inline script> */
    { ngx_string("header_filter_by_lua"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_header_filter_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_header_filter_inline },

    /* header_filter_by_lua_block { <inline script> } */
    { ngx_string("header_filter_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_header_filter_by_lua_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_header_filter_inline },

    { ngx_string("header_filter_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_header_filter_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_header_filter_file },

    { ngx_string("body_filter_by_lua"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_body_filter_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_body_filter_inline },

    /* body_filter_by_lua_block { <inline script> } */
    { ngx_string("body_filter_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_body_filter_by_lua_block,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_body_filter_inline },

    { ngx_string("body_filter_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_TAKE1,
      ngx_http_lua_body_filter_by_lua,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_body_filter_file },

    { ngx_string("balancer_by_lua_block"),
      NGX_HTTP_UPS_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_balancer_by_lua_block,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_balancer_handler_inline },

    { ngx_string("balancer_by_lua_file"),
      NGX_HTTP_UPS_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_balancer_by_lua,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_balancer_handler_file },

    { ngx_string("lua_socket_keepalive_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
          |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, keepalive_timeout),
      NULL },

    { ngx_string("lua_socket_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
          |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, connect_timeout),
      NULL },

    { ngx_string("lua_socket_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
          |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, send_timeout),
      NULL },
    //cosocket send buffer low water value to control,only work on FreeBSD
    { ngx_string("lua_socket_send_lowat"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
          |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, send_lowat),
      &ngx_http_lua_lowat_post },

    { ngx_string("lua_socket_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
          |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, buffer_size),
      NULL },

    { ngx_string("lua_socket_pool_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
                        |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, pool_size),
      NULL },

    { ngx_string("lua_socket_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF
          |NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, read_timeout),
      NULL },

    { ngx_string("lua_http10_buffering"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, http10_buffering),
      NULL },

    { ngx_string("lua_check_client_abort"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, check_client_abort),
      NULL },

    { ngx_string("lua_use_default_type"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, use_default_type),
      NULL },

#if (NGX_HTTP_SSL)

#   if defined(nginx_version) && nginx_version >= 1001013

    { ngx_string("lua_ssl_protocols"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, ssl_protocols),
      &ngx_http_lua_ssl_protocols },

#   endif

    { ngx_string("lua_ssl_ciphers"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, ssl_ciphers),
      NULL },

    { ngx_string("ssl_certificate_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_ssl_cert_by_lua_block,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_ssl_cert_handler_inline },

    { ngx_string("ssl_certificate_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_ssl_cert_by_lua,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_ssl_cert_handler_file },

    { ngx_string("ssl_session_store_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_ssl_sess_store_by_lua_block,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_ssl_sess_store_handler_inline },

    { ngx_string("ssl_session_store_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_ssl_sess_store_by_lua,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_ssl_sess_store_handler_file },

    { ngx_string("ssl_session_fetch_by_lua_block"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_http_lua_ssl_sess_fetch_by_lua_block,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_ssl_sess_fetch_handler_inline },

    { ngx_string("ssl_session_fetch_by_lua_file"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_ssl_sess_fetch_by_lua,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      (void *) ngx_http_lua_ssl_sess_fetch_handler_file },

    { ngx_string("lua_ssl_verify_depth"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, ssl_verify_depth),
      NULL },

    { ngx_string("lua_ssl_trusted_certificate"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, ssl_trusted_certificate),
      NULL },

    { ngx_string("lua_ssl_crl"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_lua_loc_conf_t, ssl_crl),
      NULL },

#endif  /* NGX_HTTP_SSL */

     { ngx_string("lua_malloc_trim"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_lua_malloc_trim,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    ngx_null_command
};


ngx_http_module_t ngx_http_lua_module_ctx = {
    NULL,                             /*  preconfiguration */
    ngx_http_lua_init,                /*  postconfiguration */ //解析完nginx.conf配置文件后 进行调用

    ngx_http_lua_create_main_conf,    /*  create main configuration */
    ngx_http_lua_init_main_conf,      /*  init main configuration */

    ngx_http_lua_create_srv_conf,     /*  create server configuration */
    ngx_http_lua_merge_srv_conf,      /*  merge server configuration */

    ngx_http_lua_create_loc_conf,     /*  create location configuration */
    ngx_http_lua_merge_loc_conf       /*  merge location configuration */
};

//模块定义结构体
ngx_module_t ngx_http_lua_module = {
    NGX_MODULE_V1, //#define NGX_MODULE_V1    0, 0, 0, 0, 0, 0, 1         //该宏用来初始化前7个字段
    &ngx_http_lua_module_ctx,   /*  module context */ //该模块的上下文，每个种类的模块有不同的上下文
    ngx_http_lua_cmds,          /*  module directives */ //该模块的命令集，指向一个ngx_command_t结构数组
    NGX_HTTP_MODULE,            /*  module type */ //该模块的种类，为core/event/http/mail中的一种
    NULL,                       /*  init master */
    NULL,                       /*  init module */
    ngx_http_lua_init_worker,   /*  init process */
    NULL,                       /*  init thread */
    NULL,                       /*  exit thread */
    NULL,                       /*  exit process */
    NULL,                       /*  exit master */
    NGX_MODULE_V1_PADDING //#define NGX_MODULE_V1_PADDING 0, 0, 0, 0, 0, 0, 0, 0 //该宏用来初始化最后8个字段
};
/*
typedef enum {
    //读取请求phase
    NGX_HTTP_POST_READ_PHASE = 0,                       //第一个

    //开始处理
    //这个阶段主要是处理全局的(server block)的rewrite
    NGX_HTTP_SERVER_REWRITE_PHASE,

    //这个阶段主要是通过uri来查找对应的location。然后将uri和location的数据关联起来
    NGX_HTTP_FIND_CONFIG_PHASE,

    //这个主要处理location的rewrite。
    NGX_HTTP_REWRITE_PHASE,
    //post rewrite，这个主要是进行一些校验以及收尾工作，以便于交给后面的模块。
    NGX_HTTP_POST_REWRITE_PHASE,

    //比如流控这种类型的access就放在这个phase，也就是说它主要是进行一些比较粗粒度的access。
    NGX_HTTP_PREACCESS_PHASE,
    //这个比如存取控制，权限验证就放在这个phase，一般来说处理动作是交给下面的模块做的.这个主要是做一些细粒度的access。
    NGX_HTTP_ACCESS_PHASE,
    //当上面的access模块得到access_code之后就会由这个模块根据access_code来进行操作
    NGX_HTTP_POST_ACCESS_PHASE,
    //try_file模块，也就是对应配置文件中的try_files指令。
    NGX_HTTP_TRY_FILES_PHASE,
    //内容处理模块，我们一般的handle都是处于这个模块,比如说模块扩展也将是该phase, 默认调用ngx_http_autoindex_module.c中的ngx_http_autoindex_handler handler
    NGX_HTTP_CONTENT_PHASE,
    //log模块
    NGX_HTTP_LOG_PHASE
} ngx_http_phases;
*/

//配置项API文档 https://www.nginx.com/resources/wiki/extending/api/configuration/
static ngx_int_t
ngx_http_lua_init(ngx_conf_t *cf)
{
    int                         multi_http_blocks;//标识是否有多个http配置块
    ngx_int_t                   rc;
    ngx_array_t                *arr;
    ngx_http_handler_pt        *h;
    volatile ngx_cycle_t       *saved_cycle;
    ngx_http_core_main_conf_t  *cmcf;
    ngx_http_lua_main_conf_t   *lmcf;
#ifndef NGX_LUA_NO_FFI_API
    ngx_pool_cleanup_t         *cln;
#endif

    lmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_lua_module);//lua_module_conf 从configuration配置对象获取http_lua模块的配置项

    if (ngx_http_lua_prev_cycle != ngx_cycle) {
        ngx_http_lua_prev_cycle = ngx_cycle;
        multi_http_blocks = 0;

    } else {
        multi_http_blocks = 1;
    }

    if (multi_http_blocks || lmcf->requires_capture_filter) {
        rc = ngx_http_lua_capture_filter_init(cf);
        if (rc != NGX_OK) {
            return rc;
        }
    }

    if (lmcf->postponed_to_rewrite_phase_end == NGX_CONF_UNSET) {
        lmcf->postponed_to_rewrite_phase_end = 0;//默认关闭rewrite_by_lua_no_postpone
    }

    if (lmcf->postponed_to_access_phase_end == NGX_CONF_UNSET) {
        lmcf->postponed_to_access_phase_end = 0;//默认关闭access_by_lua_no_postpone
    }

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);//core_module_conf

    if (lmcf->requires_rewrite) {
        //返回数组中可添加元素的指针位置
        h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);//添加lua_rewrite_by_xxx
        if (h == NULL) {
            return NGX_ERROR;
        }

        *h = ngx_http_lua_rewrite_handler;
    }

    if (lmcf->requires_access) {
        h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);//添加lua_access_by_xxx
        if (h == NULL) {
            return NGX_ERROR;
        }

        *h = ngx_http_lua_access_handler;
    }

    dd("requires log: %d", (int) lmcf->requires_log);

    if (lmcf->requires_log) {
        arr = &cmcf->phases[NGX_HTTP_LOG_PHASE].handlers;//添加lua_log_by_xxx
        h = ngx_array_push(arr);
        if (h == NULL) {
            return NGX_ERROR;
        }

        if (arr->nelts > 1) {
            h = arr->elts;
            ngx_memmove(&h[1], h,
                        (arr->nelts - 1) * sizeof(ngx_http_handler_pt));
        }

        *h = ngx_http_lua_log_handler;
    }

    if (multi_http_blocks || lmcf->requires_header_filter) {
        rc = ngx_http_lua_header_filter_init();//初始化lua_header_filter_xxx
        if (rc != NGX_OK) {
            return rc;
        }
    }

    if (multi_http_blocks || lmcf->requires_body_filter) {
        rc = ngx_http_lua_body_filter_init();//初始化lua_body_filter_xxx
        if (rc != NGX_OK) {
            return rc;
        }
    }

#ifndef NGX_LUA_NO_FFI_API
    /* add the cleanup of semaphores after the lua_close */
    cln = ngx_pool_cleanup_add(cf->pool, 0); //添加多一种清理器
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->data = lmcf;
    cln->handler = ngx_http_lua_sema_mm_cleanup; //内存池对应的清理结构中对应的处理器
#endif

    if (lmcf->lua == NULL) {
        dd("initializing lua vm");

        ngx_http_lua_content_length_hash =
                                  ngx_http_lua_hash_literal("content-length");
        ngx_http_lua_location_hash = ngx_http_lua_hash_literal("location");

        lmcf->lua = ngx_http_lua_init_vm(NULL, cf->cycle, cf->pool, lmcf,
                                         cf->log, NULL);//初始化lua_vm
        if (lmcf->lua == NULL) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0,
                               "failed to initialize Lua VM");
            return NGX_ERROR;
        }

        if (!lmcf->requires_shm && lmcf->init_handler) {
            saved_cycle = ngx_cycle;
            ngx_cycle = cf->cycle;

            rc = lmcf->init_handler(cf->log, lmcf, lmcf->lua);

            ngx_cycle = saved_cycle;

            if (rc != NGX_OK) {
                /* an error happened */
                return NGX_ERROR;
            }
        }

        dd("Lua VM initialized!");
    }

    return NGX_OK;
}


static char *
ngx_http_lua_lowat_check(ngx_conf_t *cf, void *post, void *data)
{
#if (NGX_FREEBSD)
    ssize_t *np = data;
    //freebsd系统下 待发送TCP数据缓冲区空间
    if ((u_long) *np >= ngx_freebsd_net_inet_tcp_sendspace) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"lua_send_lowat\" must be less than %d "
                           "(sysctl net.inet.tcp.sendspace)",
                           ngx_freebsd_net_inet_tcp_sendspace);

        return NGX_CONF_ERROR;
    }

#elif !(NGX_HAVE_SO_SNDLOWAT)
    ssize_t *np = data;

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"lua_send_lowat\" is not supported, ignored");

    *np = 0;

#endif

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_create_main_conf(ngx_conf_t *cf)
{
#ifndef NGX_LUA_NO_FFI_API
    ngx_int_t       rc;
#endif

    ngx_http_lua_main_conf_t    *lmcf;

    lmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_main_conf_t));
    if (lmcf == NULL) {
        return NULL;
    }
    //lua_main_conf初始化值表
    /* set by ngx_pcalloc:
     *      lmcf->lua = NULL;
     *      lmcf->lua_path = { 0, NULL };
     *      lmcf->lua_cpath = { 0, NULL };
     *      lmcf->pending_timers = 0;
     *      lmcf->running_timers = 0;
     *      lmcf->watcher = NULL;
     *      lmcf->regex_cache_entries = 0;
     *      lmcf->shm_zones = NULL;
     *      lmcf->init_handler = NULL;
     *      lmcf->init_src = { 0, NULL };
     *      lmcf->shm_zones_inited = 0;
     *      lmcf->shdict_zones = NULL;
     *      lmcf->preload_hooks = NULL;
     *      lmcf->requires_header_filter = 0;
     *      lmcf->requires_body_filter = 0;
     *      lmcf->requires_capture_filter = 0;
     *      lmcf->requires_rewrite = 0;
     *      lmcf->requires_access = 0;
     *      lmcf->requires_log = 0;
     *      lmcf->requires_shm = 0;
     */

    lmcf->pool = cf->pool;
    lmcf->max_pending_timers = NGX_CONF_UNSET;
    lmcf->max_running_timers = NGX_CONF_UNSET;
#if (NGX_PCRE)
    lmcf->regex_cache_max_entries = NGX_CONF_UNSET;
    lmcf->regex_match_limit = NGX_CONF_UNSET;
#endif
    lmcf->postponed_to_rewrite_phase_end = NGX_CONF_UNSET;
    lmcf->postponed_to_access_phase_end = NGX_CONF_UNSET;

#if (NGX_HTTP_LUA_HAVE_MALLOC_TRIM)
    lmcf->malloc_trim_cycle = NGX_CONF_UNSET_UINT;
#endif

#ifndef NGX_LUA_NO_FFI_API
    rc = ngx_http_lua_sema_mm_init(cf, lmcf);
    if (rc != NGX_OK) {
        return NULL;
    }

    dd("nginx Lua module main config structure initialized!");
#endif

    return lmcf;
}


static char *
ngx_http_lua_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_lua_main_conf_t *lmcf = conf;

#if (NGX_PCRE)
    if (lmcf->regex_cache_max_entries == NGX_CONF_UNSET) {
        lmcf->regex_cache_max_entries = 1024;
    }

    if (lmcf->regex_match_limit == NGX_CONF_UNSET) {
        lmcf->regex_match_limit = 0;
    }
#endif

    if (lmcf->max_pending_timers == NGX_CONF_UNSET) {
        lmcf->max_pending_timers = 1024;
    }

    if (lmcf->max_running_timers == NGX_CONF_UNSET) {
        lmcf->max_running_timers = 256;
    }

#if (NGX_HTTP_LUA_HAVE_MALLOC_TRIM)
    if (lmcf->malloc_trim_cycle == NGX_CONF_UNSET_UINT) {
        lmcf->malloc_trim_cycle = 1000;  /* number of reqs */
    }
#endif

    lmcf->cycle = cf->cycle;

    return NGX_CONF_OK;
}


static void *
ngx_http_lua_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_lua_srv_conf_t     *lscf;

    lscf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_srv_conf_t));
    if (lscf == NULL) {
        return NULL;
    }

    /* set by ngx_pcalloc:
     *      lscf->srv.ssl_cert_handler = NULL;
     *      lscf->srv.ssl_cert_src = { 0, NULL };
     *      lscf->srv.ssl_cert_src_key = NULL;
     *
     *      lscf->srv.ssl_session_store_handler = NULL;
     *      lscf->srv.ssl_session_store_src = { 0, NULL };
     *      lscf->srv.ssl_session_store_src_key = NULL;
     *
     *      lscf->srv.ssl_session_fetch_handler = NULL;
     *      lscf->srv.ssl_session_fetch_src = { 0, NULL };
     *      lscf->srv.ssl_session_fetch_src_key = NULL;
     *
     *      lscf->balancer.handler = NULL;
     *      lscf->balancer.src = { 0, NULL };
     *      lscf->balancer.src_key = NULL;
     */

    return lscf;
}


static char *
ngx_http_lua_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
#if (NGX_HTTP_SSL)

    ngx_http_lua_srv_conf_t *prev = parent;//可继承的server conf配置
    ngx_http_lua_srv_conf_t *conf = child;//当前的server conf配置
    ngx_http_ssl_srv_conf_t *sscf;

    dd("merge srv conf");
    //根据情况判断是否可进行合并继承
    if (conf->srv.ssl_cert_src.len == 0) {
        conf->srv.ssl_cert_src = prev->srv.ssl_cert_src;
        conf->srv.ssl_cert_src_key = prev->srv.ssl_cert_src_key;
        conf->srv.ssl_cert_handler = prev->srv.ssl_cert_handler;
    }

    if (conf->srv.ssl_cert_src.len) {
        sscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_ssl_module);
        if (sscf == NULL || sscf->ssl.ctx == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "no ssl configured for the server");

            return NGX_CONF_ERROR;
        }

#ifdef LIBRESSL_VERSION_NUMBER

        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "LibreSSL does not support ssl_ceritificate_by_lua*");
        return NGX_CONF_ERROR;

#else

#   if OPENSSL_VERSION_NUMBER >= 0x1000205fL

        SSL_CTX_set_cert_cb(sscf->ssl.ctx, ngx_http_lua_ssl_cert_handler, NULL);

#   else

        ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                      "OpenSSL too old to support ssl_ceritificate_by_lua*");
        return NGX_CONF_ERROR;

#   endif

#endif
    }

    if (conf->srv.ssl_sess_store_src.len == 0) {
        conf->srv.ssl_sess_store_src = prev->srv.ssl_sess_store_src;
        conf->srv.ssl_sess_store_src_key = prev->srv.ssl_sess_store_src_key;
        conf->srv.ssl_sess_store_handler = prev->srv.ssl_sess_store_handler;
    }

    if (conf->srv.ssl_sess_store_src.len) {
        sscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_ssl_module);
        if (sscf && sscf->ssl.ctx) {
#ifdef LIBRESSL_VERSION_NUMBER
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "LibreSSL does not support "
                          "ssl_session_store_by_lua*");

            return NGX_CONF_ERROR;
#else
            SSL_CTX_sess_set_new_cb(sscf->ssl.ctx,
                                    ngx_http_lua_ssl_sess_store_handler);
#endif
        }
    }

    if (conf->srv.ssl_sess_fetch_src.len == 0) {
        conf->srv.ssl_sess_fetch_src = prev->srv.ssl_sess_fetch_src;
        conf->srv.ssl_sess_fetch_src_key = prev->srv.ssl_sess_fetch_src_key;
        conf->srv.ssl_sess_fetch_handler = prev->srv.ssl_sess_fetch_handler;
    }

    if (conf->srv.ssl_sess_fetch_src.len) {
        sscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_ssl_module);
        if (sscf && sscf->ssl.ctx) {
#ifdef LIBRESSL_VERSION_NUMBER
            ngx_log_error(NGX_LOG_EMERG, cf->log, 0,
                          "LibreSSL does not support "
                          "ssl_session_fetch_by_lua*");

            return NGX_CONF_ERROR;
#else
            SSL_CTX_sess_set_get_cb(sscf->ssl.ctx,
                                    ngx_http_lua_ssl_sess_fetch_handler);
#endif
        }
    }

#endif  /* NGX_HTTP_SSL */
    return NGX_CONF_OK;
}


static void *
ngx_http_lua_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_lua_loc_conf_t *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_lua_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /* set by ngx_pcalloc:
     *      conf->access_src  = {{ 0, NULL }, NULL, NULL, NULL};
     *      conf->access_src_key = NULL
     *      conf->rewrite_src = {{ 0, NULL }, NULL, NULL, NULL};
     *      conf->rewrite_src_key = NULL
     *      conf->rewrite_handler = NULL;
     *
     *      conf->content_src = {{ 0, NULL }, NULL, NULL, NULL};
     *      conf->content_src_key = NULL
     *      conf->content_handler = NULL;
     *
     *      conf->log_src = {{ 0, NULL }, NULL, NULL, NULL};
     *      conf->log_src_key = NULL
     *      conf->log_handler = NULL;
     *
     *      conf->header_filter_src = {{ 0, NULL }, NULL, NULL, NULL};
     *      conf->header_filter_src_key = NULL
     *      conf->header_filter_handler = NULL;
     *
     *      conf->body_filter_src = {{ 0, NULL }, NULL, NULL, NULL};
     *      conf->body_filter_src_key = NULL
     *      conf->body_filter_handler = NULL;
     *
     *      conf->ssl = 0;
     *      conf->ssl_protocols = 0;
     *      conf->ssl_ciphers = { 0, NULL };
     *      conf->ssl_trusted_certificate = { 0, NULL };
     *      conf->ssl_crl = { 0, NULL };
     */

    conf->force_read_body    = NGX_CONF_UNSET;//lua_need_request_body 默认
    conf->enable_code_cache  = NGX_CONF_UNSET;//lua_code_cache 默认 on
    conf->http10_buffering   = NGX_CONF_UNSET;//lua_http10_buffering 默认 on
    conf->check_client_abort = NGX_CONF_UNSET;//lua_check_client_abort 默认 off
    conf->use_default_type   = NGX_CONF_UNSET;//lua_use_default_type 默认 on

    conf->keepalive_timeout = NGX_CONF_UNSET_MSEC;//lua_socket_keepalive_timeout default:60s
    conf->connect_timeout = NGX_CONF_UNSET_MSEC;//lua_socket_connect_timeout default:60s
    conf->send_timeout = NGX_CONF_UNSET_MSEC;//lua_socket_send_timeout default:60s
    conf->read_timeout = NGX_CONF_UNSET_MSEC;//lua_socket_read_timeout default:60s
    conf->send_lowat = NGX_CONF_UNSET_SIZE;//lua_socket_send_lowat default:0
    conf->buffer_size = NGX_CONF_UNSET_SIZE;//lua_socket_buffer_size
    conf->pool_size = NGX_CONF_UNSET_UINT;//lua_socket_pool_size default:30

    conf->transform_underscores_in_resp_headers = NGX_CONF_UNSET;//lua_transform_underscores_in_response_headers default:on
    conf->log_socket_errors = NGX_CONF_UNSET;//lua_socket_log_errors deafult:on

#if (NGX_HTTP_SSL)
    conf->ssl_verify_depth = NGX_CONF_UNSET_UINT;
#endif

    return conf;
}


static char *
ngx_http_lua_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_lua_loc_conf_t *prev = parent;//可继承的location配置
    ngx_http_lua_loc_conf_t *conf = child;//当前的location配置
    //根据情况判断是否可进行继承
    if (conf->rewrite_src.value.len == 0) {
        conf->rewrite_src = prev->rewrite_src;
        conf->rewrite_handler = prev->rewrite_handler;
        conf->rewrite_src_key = prev->rewrite_src_key;
        conf->rewrite_chunkname = prev->rewrite_chunkname;
    }

    if (conf->access_src.value.len == 0) {
        conf->access_src = prev->access_src;
        conf->access_handler = prev->access_handler;
        conf->access_src_key = prev->access_src_key;
        conf->access_chunkname = prev->access_chunkname;
    }

    if (conf->content_src.value.len == 0) {
        conf->content_src = prev->content_src;
        conf->content_handler = prev->content_handler;
        conf->content_src_key = prev->content_src_key;
        conf->content_chunkname = prev->content_chunkname;
    }

    if (conf->log_src.value.len == 0) {
        conf->log_src = prev->log_src;
        conf->log_handler = prev->log_handler;
        conf->log_src_key = prev->log_src_key;
        conf->log_chunkname = prev->log_chunkname;
    }

    if (conf->header_filter_src.value.len == 0) {
        conf->header_filter_src = prev->header_filter_src;
        conf->header_filter_handler = prev->header_filter_handler;
        conf->header_filter_src_key = prev->header_filter_src_key;
    }

    if (conf->body_filter_src.value.len == 0) {
        conf->body_filter_src = prev->body_filter_src;
        conf->body_filter_handler = prev->body_filter_handler;
        conf->body_filter_src_key = prev->body_filter_src_key;
    }

#if (NGX_HTTP_SSL)

#   if defined(nginx_version) && nginx_version >= 1001013

    ngx_conf_merge_bitmask_value(conf->ssl_protocols, prev->ssl_protocols,
                                 (NGX_CONF_BITMASK_SET|NGX_SSL_SSLv3
                                  |NGX_SSL_TLSv1|NGX_SSL_TLSv1_1
                                  |NGX_SSL_TLSv1_2));

#   endif

    ngx_conf_merge_str_value(conf->ssl_ciphers, prev->ssl_ciphers,
                             "DEFAULT");

    ngx_conf_merge_uint_value(conf->ssl_verify_depth,
                              prev->ssl_verify_depth, 1);
    ngx_conf_merge_str_value(conf->ssl_trusted_certificate,
                             prev->ssl_trusted_certificate, "");
    ngx_conf_merge_str_value(conf->ssl_crl, prev->ssl_crl, "");

    if (ngx_http_lua_set_ssl(cf, conf) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

#endif

    ngx_conf_merge_value(conf->force_read_body, prev->force_read_body, 0);
    ngx_conf_merge_value(conf->enable_code_cache, prev->enable_code_cache, 1);
    ngx_conf_merge_value(conf->http10_buffering, prev->http10_buffering, 1);
    ngx_conf_merge_value(conf->check_client_abort, prev->check_client_abort, 0);
    ngx_conf_merge_value(conf->use_default_type, prev->use_default_type, 1);

    ngx_conf_merge_msec_value(conf->keepalive_timeout,
                              prev->keepalive_timeout, 60000);

    ngx_conf_merge_msec_value(conf->connect_timeout,
                              prev->connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->send_timeout,
                              prev->send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->read_timeout,
                              prev->read_timeout, 60000);

    ngx_conf_merge_size_value(conf->send_lowat,
                              prev->send_lowat, 0);

    ngx_conf_merge_size_value(conf->buffer_size,
                              prev->buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_uint_value(conf->pool_size, prev->pool_size, 30);

    ngx_conf_merge_value(conf->transform_underscores_in_resp_headers,
                         prev->transform_underscores_in_resp_headers, 1);

    ngx_conf_merge_value(conf->log_socket_errors, prev->log_socket_errors, 1);

    return NGX_CONF_OK;
}


#if (NGX_HTTP_SSL)

static ngx_int_t
ngx_http_lua_set_ssl(ngx_conf_t *cf, ngx_http_lua_loc_conf_t *llcf)
{
    ngx_pool_cleanup_t  *cln;

    llcf->ssl = ngx_pcalloc(cf->pool, sizeof(ngx_ssl_t));
    if (llcf->ssl == NULL) {
        return NGX_ERROR;
    }

    llcf->ssl->log = cf->log;

    if (ngx_ssl_create(llcf->ssl, llcf->ssl_protocols, NULL) != NGX_OK) {
        return NGX_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_ssl_cleanup_ctx;
    cln->data = llcf->ssl;

    if (SSL_CTX_set_cipher_list(llcf->ssl->ctx,
                                (const char *) llcf->ssl_ciphers.data)
        == 0)
    {
        ngx_ssl_error(NGX_LOG_EMERG, cf->log, 0,
                      "SSL_CTX_set_cipher_list(\"%V\") failed",
                      &llcf->ssl_ciphers);
        return NGX_ERROR;
    }

    if (llcf->ssl_trusted_certificate.len) {

#if defined(nginx_version) && nginx_version >= 1003007

        if (ngx_ssl_trusted_certificate(cf, llcf->ssl,
                                        &llcf->ssl_trusted_certificate,
                                        llcf->ssl_verify_depth)
            != NGX_OK)
        {
            return NGX_ERROR;
        }

#else

        ngx_log_error(NGX_LOG_CRIT, cf->log, 0, "at least nginx 1.3.7 is "
                      "required for the \"lua_ssl_trusted_certificate\" "
                      "directive");
        return NGX_ERROR;

#endif
    }

    dd("ssl crl: %.*s", (int) llcf->ssl_crl.len, llcf->ssl_crl.data);

    if (ngx_ssl_crl(cf, llcf->ssl, &llcf->ssl_crl) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

#endif  /* NGX_HTTP_SSL */


static char *
ngx_http_lua_malloc_trim(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_HTTP_LUA_HAVE_MALLOC_TRIM)

    ngx_int_t       nreqs;
    ngx_str_t      *value;

    ngx_http_lua_main_conf_t    *lmcf = conf;

    value = cf->args->elts;

    nreqs = ngx_atoi(value[1].data, value[1].len);
    if (nreqs == NGX_ERROR) {
        return "invalid number in the 1st argument";
    }

    lmcf->malloc_trim_cycle = (ngx_uint_t) nreqs;

    if (nreqs == 0) {
        return NGX_CONF_OK;
    }

    lmcf->requires_log = 1;

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0, "lua_malloc_trim is not supported "
                       "on this platform, ignored");

#endif
    return NGX_CONF_OK;
}

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
