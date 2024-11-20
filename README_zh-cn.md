# Healthd 模块

\[ [English](README.md) | 简体中文 \]

`openvela healthd` 通过 `uORB` 发布电池充电状态，充电电压电流，电池温度等健康检测的模块。Healthd 模块主要是通过 `poll` 系统下 `/dev/charge/` 目录下的所有设备节点，感知 `mask` 的变化，获取变化参数通过 uORB 发布出去。

## 电池芯片驱动
针对不同的电池芯片驱动，需要对框架有如下要求：

1. 电池芯片注册的设备节点需要在 `/dev/charge/` 目录下。
2. 电池芯片驱动必须实现参数变化感知通知，流程上支持上层通过 `poll` 访问。

## Healthd 系统框架
Healthd 的系统框架如下图所示：

![healthd系统框架](./chart/healthd_sys.png)

主要分3个部分：

1. Healthd 会打开所有已注册的 `charger` 设备节点，并将设备节点加入到监控列表中。
2. Healthd 通过 `poll` 监控 `charger` 设备节点的 `POLLIN` 事件，如果有事件发生，会读取对应的 `mask` 值，通过 `mask` 调用 `ioctl` 接口获取 `charger` 相应变化参数的值。
3. Healthd 调用 uORB 的 API orb_publish_auto 发布电池状态的 topic 信息。

## Healthd 监测框架
Healthd 的 `poll` 框架如下图所示：

![healthd 检测框架](./chart/healthd_poll.png)

电池芯片主要注册两类设备节点：
1. `charger` 设备节点：主要监控电池充电在位，充电状态，充电电压，充电电流，电池温度等变化。
2. `gauge` 设备节点：主要监控电池的在位，电池电量，电池温度等变化。

## Healthd 代码路径
路径：frameworks/system/healthd
