Menuconfig 配置系统使用指南
=============================

配置优先级（从高到低）
--------------------

1. ``.config``（用户通过 menuconfig 修改后的配置）
2. ``defconfig`` / ``proj.conf``（工程基础配置）

配置如何生效
------------

配置先由 Make 汇总为 ``build/generated/defconfig.cmake``，CMake 配置时先读取该
文件，再读取 menuconfig 生成的 ``.config.cmake`` 覆盖同名变量。最后
``build/generated/autoconf.h`` 会根据最终的 CMake 变量自动生成，并通过
``-include`` 注入到所有编译单元。

由于 Make 使用 ``y/n`` 表示布尔开关，``.config.cmake`` 同样使用 ``y/n``，
而不是 ``1/0``。``n`` 会生成 ``#undef``，避免 C 代码中的 ``#ifdef`` 误判。

快速开始
--------

无需预先运行 menuconfig，直接 make 即可：

.. code-block:: bash

    make

自定义配置
----------

.. code-block:: bash

    make menuconfig        # 命令行配置界面
    make guiconfig         # 图形化配置界面
    make savedefconfig     # 将当前配置另存为 boards/defconfig（备份）
    make load-defconfig    # 丢弃当前配置并重新加载默认配置
    make diffconfig        # 对比 .config.old 和 .config

每次执行 ``make menuconfig`` 时，系统都会把当前有效的 Make 配置（defconfig /
``proj.conf``）合并进 ``.config`` 作为起点，因此界面中的默认值即当前工程的真实
配置；已存在的用户修改会保留。保存后同步生成 ``.config.cmake``，下次 ``make``
自动重新配置并生效。

组件菜单
--------

menuconfig 的菜单结构与 ``components/CMakeLists.txt`` 的组件分级一致，包括
AI、Crypto、Debug、File System、Graphics、Memory Management、C Library、
Network、Operating System、Shell、USB、Utilities、Multimedia、IPC、FOTA 等。
每个组件的启用开关控制对应 CMake 子目录是否参与编译。

无线相关配置（Wi-Fi、Bluetooth/BLE、Thread、Zigbee、共存调度等）以预编译
库形式发布，由库侧定义，**不在 menuconfig 中开放**，只能通过工程的
``defconfig`` 指定。

注意事项
--------

- ``.config`` 由 Kconfig 工具维护，不要手动编辑。
- 只有 Kconfig 树中已定义的选项才受 menuconfig 管理；其余选项仍由工程的
  ``defconfig`` / ``proj.conf`` 控制。
- ``savedefconfig`` 生成的文件仅作为配置备份，不会自动参与编译；工程默认配置
  仍以各 example 的 ``defconfig`` / ``proj.conf`` 为准。
