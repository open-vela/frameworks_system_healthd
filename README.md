# Overview of Healthd

\[ English | [简体中文](README_zh-cn.md) \]

Vela healthd is a module that publishes battery charging status, charging voltage and current, battery temperature, and other health monitoring data via uORB. The Healthd module primarily senses changes by polling all device nodes under the /dev/charge/ directory, detects changes in the mask, and publishes the changed parameters through uORB.

## Battery Chip Driver
Different battery chip drivers need to meet the following requirements for the battery chip driver framework:：

1. The device nodes registered for the battery chip must be located under the /dev/charge/ directory.
2. The battery chip driver must implement change notification for parameter sensing and support access through polling at the upper layer.

## System Framework of Healthd
The system framework of Healthd is shown in the diagram below:：

![healthd system framework](./chart/healthd_sys.png)

It is mainly divided into three parts:：
1. Healthd will open all registered charger device nodes and add them to the monitoring list.
2. Healthd monitors the POLLIN events of the charger device nodes through polling. If an event occurs, it reads the corresponding mask value and calls the ioctl interface to obtain the values of the corresponding changing parameters of the charger.
3. Healthd calls the uORB API orb_publish_auto to publish the battery status topic information.

## Healthd Poll Framework
The poll framework of Healthd is illustrated in the diagram below：

![healthd poll framework](./chart/healthd_poll.png)

The battery chip mainly registers two types of device nodes:：
1. Charger device nodes, which primarily monitor changes in battery charging presence, charging status, charging voltage, charging current, battery temperature, etc.
2. Gauge device nodes, which mainly monitor changes in battery presence, battery level, battery temperature, etc.

## Healthd Code Path
Path: frameworks/system/healthd
