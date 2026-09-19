# C 与 C++ API 参考

本页由 Doxygen 从已安装的公共接口头文件提取，再由 Breathe 直接渲染在 Sphinx 文档
中，不是独立的 Doxygen 站点。要改进某个 API 的说明，请编辑拥有该符号的组件头文件，
不要修改本页。

`src/interface` 下的组件头文件是主接口；聚合头文件只提供便捷导入，不定义第二套
接口。

```{toctree}
:maxdepth: 2

module
core
communication
services
packages
```
