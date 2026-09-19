# Linux Runtime

## 默认路径

`aster run --config runtime.yaml` 启动通用 aster_runtime，不要求自写启动器、
Application 文件或 Deployment Lock。配置中的 Package、实例、执行器、日志和参数
在启动时解析。配置改变后重新运行即可；当前不提供热重载。

具名串行执行器和线程池落实线程数与队列容量。Local Channel/RPC 使用实例的
namespace/remap，并在绑定执行器上异步交付已取得所有权的消息。
当前只支持配置 `backends: [local]`；CAN/USB 和 CorePlugin 尚未接入这个启动器。

`aster validate runtime.yaml` 只校验配置，不加载 Package。
`aster run --config runtime.yaml --check` 则实际创建、初始化 Module 并封闭注册，
因此会执行应用的 Initialize/Shutdown。它不是纯静态检查。
两者都支持 `--runtime PATH` 指定启动程序。

## 嵌入与生命周期

包含 aster_runtime/platform/linux/runtime.hpp 并链接 `aster::linux`；
构造 NodeRuntime，先 RegisterModule 注册借用的静态对象，再 Initialize、Start、
Shutdown。动态和静态实例可混合；静态对象必须比 Runtime 活得更久。
业务 Package 则只链接公共 Interface，不链接 Runtime。

SIGINT/SIGTERM 触发停止。Runtime 先关闭通信入口，停止执行器并等待在途回调，
取消未完成 RPC，再逆序停止 Module、销毁对象和卸载动态库。
Shutdown 必须由控制线程调用，回调不得等待自身所属 Runtime 关闭。

目前可查询状态、诊断和 Module 实例列表；没有完整通信图查询、分布式健康监控、
配置化 Clock 替换或 CorePlugin 生命周期。不要将底层加载器测试视为这些功能的验收。

## 服务部署

Linux 安装提供 aster-node@.service 模板，直接启动 aster_runtime，读取
/opt/aster/<实例>/current/runtime.yaml。模板的可执行路径来自 CMake 配置时的
CMAKE_INSTALL_PREFIX；使用不同前缀安装时应重新配置或调整 unit。
账户、权限、设备授权和服务启用由部署者准备。

现有 deploy apply 只校验、暂存并切换产物目录，**不会自动执行 systemctl 或刷写 MCU**。
它仍消费旧部署输入；新 v1alpha3 产物描述尚待接入。
