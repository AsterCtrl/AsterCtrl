# Package 与 Core Plugin ABI

Package 入口由声明式 Module 注册自动生成；Core Plugin 使用独立 ABI，不能作为业务
Module Package 加载。

v1alpha3 Manifest 当前生成 Linux Package 入口；对应的 Zephyr 静态入口尚待实现。
CorePlugin 已有加载与接口查询测试，但生产生命周期和后端注册尚未接入新启动器。
详见 [Package 概念](../concepts/packages.md)。

```{doxygenfile} pkg_main.h
:project: AsterCtrl
:sections: func
```

```{doxygenfile} core_plugin_main.h
:project: AsterCtrl
:sections: innerclass func
```
