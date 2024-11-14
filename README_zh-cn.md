# Healthd 概述

\[ [English](README.md) | 简体中文 \]

Vela healthd 是通过uORB发布电池充电状态，充电电压电流，电池温度等健康检测的模块。Healthd模块主要是通过poll系统下/dev/charge/目录下的所有设备节点，感知mask的变化，获取变化参数通过uORB发布出去。

## 电池芯片驱动
针对不同的电池芯片驱动，需要对电池芯片驱动框架有如下要求：

1. 电池芯片注册的设备节点需要在/dev/charge/目录下。
2. 电池芯片驱动必须实现参数变化感知通知，流程上支持上层通过poll访问。

## Healthd 系统框架
Healthd 的系统框架如下图所示：

![healthd系统框架](./chart/healthd_sys.png)

主要分3个部分完成：

1. Healthd 会打开所有已注册的charger设备节点，并将设备节点加入到监控列表中。
2. Healthd 通过poll监控charger设备节点的POLLIN事件，如果有事件发生，会读取对应的mask值，通过mask调用ioctl接口获取charger相应变化参数的值。
3. Healthd 调用uORB的API orb_publish_auto发布电池状态的topic信息。

## Healthd 监测框架
Healthd 的poll框架如下图所示：

![healthd 检测框架](./chart/healthd_poll.png)

电池芯片主要注册2类设备节点：
1. charger设备节点，主要监控电池充电在位，充电状态，充电电压，充电电流，电池温度等变化；
2. gauge设备节点，主要监控电池的在位，电池电量，电池温度等变化。

## Healthd 代码路径
路径：frameworks/system/healthd
