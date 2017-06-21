# reading-lua-nginx-module-zh
lua-nginx-module-0.10.8 代码理解及详细注释

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