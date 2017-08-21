# reading-lua-nginx-module-zh
lua-nginx-module-0.10.8 代码理解及详细注释

阅读思路
=======
0. ngx_http_lua_module.c: ngx_http_lua_init --> ngx_http_lua_init_vm
1. ngx_http_lua_util.c: ngx_http_lua_init_vm --> ngx_http_lua_new_state --> ngx_http_lua_init_registry --> ngx_http_lua_init_globals
2. ngx_http_lua_util.c: ngx_http_lua_init_globals --> ngx_http_lua_inject_ndk_api --> ngx_http_lua_inject_ngx_api
3. ngx_http_lua_inject_ngx_api
- ngx_http_lua_inject_arg_api
- ngx_http_lua_inject_http_consts
- ngx_http_lua_inject_core_consts
- ngx_http_lua_inject_log_api
- ngx_http_lua_inject_output_api
- ngx_http_lua_inject_time_api
- ngx_http_lua_inject_string_api
- ngx_http_lua_inject_control_api
- ngx_http_lua_inject_subrequest_api
- ngx_http_lua_inject_sleep_api
- ngx_http_lua_inject_phase_api
- ngx_http_lua_inject_regex_api
- ngx_http_lua_inject_req_api
- ngx_http_lua_inject_resp_header_api
- ngx_http_lua_create_headers_metatable
- ngx_http_lua_inject_variable_api
- ngx_http_lua_inject_shdict_api
- ngx_http_lua_inject_socket_tcp_api
- ngx_http_lua_inject_socket_udp_api
- ngx_http_lua_inject_uthread_api
- ngx_http_lua_inject_timer_api
- ngx_http_lua_inject_config_api
- ngx_http_lua_inject_worker_api
- ngx_http_lua_inject_misc_api

阅读过程
=======
17.06.21
- http_lua_module 初始化
- Lua VM 虚拟机创建以及创建时相关栈变化情况
- 注入获取 ngx request context phase API
- 注入 ngx.arg API
- 注入 http consts API
- 注入 core consts API
- 注入 ngx.log API

17.06.20
- lua-nginx-module 模块定义
- 配置定义
- 常量定义
- 共享字典定义、操作API注入逻辑
- nginx 内部变量获取、更新逻辑