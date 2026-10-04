# K230 Yahboom 适配状态与镜像构建

本文记录当前 SDK 中 Yahboom K230 开发板的适配情况。判定为“已验证”的项目，依据是此前开发板实机启动、串口/SSH 检查或用户确认；仅在配置中出现但未实际测试的项目单独列出。本文中的源码路径均相对于仓库根目录。

## 当前适配状态

### 已配置并实机验证

| 项目 | 当前实现 | 验证情况 |
| --- | --- | --- |
| LCD 与启动显示 | 2-lane MIPI-DSI，ST7701S，480x640；启动 Logo 与内核启动日志可显示。Linux 桌面逆时针旋转 90 度。 | 实机显示正常 |
| 触摸 | FT5x06，I2C 触摸设备；包含触点计数兼容修正。X11 使用旋转后的绝对坐标映射。短按左键；按住至少约 0.65 秒且移动小于 20 像素为右键；按住超过约 0.50 秒并移动至少 20 像素开始拖动；双指纵向滑动模拟滚轮。 | 桌面触摸和相机画面操作已确认 |
| KEY1 | 原理图 GPIO61，对应 GPIO1 port 29，低电平有效；内核用轮询式 gpio-keys 注册 KEY_PROG1。桌面启动时默认 DPMS 关屏，KEY1 在关屏与桌面显示间切换。 | 已配置；开关动作以当前板上验证为准 |
| GC2093 相机 | VVCAM/ISP 使用 GC2093 mode 0。实机识别传感器，VVCAM 注册 `/dev/video2` 至 `/dev/video4`；`/dev/video2` 支持 NV12，1920x1080@30，V4L2 采集测试正常。桌面应用菜单有 “Camera Check” 实时预览入口。 | 用户确认能显示相机图像 |
| Wi-Fi | RTL8189FS 模块随系统加载；NetworkManager 管理 Wi-Fi。启动时优先尝试 `/root/wifi-config.sh` 中配置的上游网络，失败后以 hostapd/dnsmasq 启动备用热点。`wlan1` 设为不受 NetworkManager 管理，避免与板载 Wi-Fi 接口冲突。 | 热点和联网功能曾实机确认 |
| USB 文件访问 | USB gadget 组合 MTP + ADB，MTP 可用于电脑与开发板间传文件、编辑根目录 Wi-Fi 配置。 | 曾确认 USB 枚举和文件访问正常 |
| Debian 桌面 | Debian 13 + LXDE，LightDM 自动以 root 登录；Firefox ESR、NetworkManager 图形组件已加入镜像。桌面壁纸使用 Debian 默认背景。 | 桌面与浏览器曾实机运行 |
| 调试串口与 SSH | U-Boot/Linux console 配置到 115200 串口；默认 root 密码为 `root`，SSH 密码认证开启。此密码是公开默认值，部署前应修改。 | 串口和 SSH 曾用于实机调试 |
| Openbox 配置 | 镜像构建时检查 root 用户 LXDE Openbox XML；缺失、空白或 XML 错误时，从系统默认配置生成有效文件。 | 已配置构建期检查与修复 |

### 尚未据此认定为已适配

SDK 配置中还包含音频、蓝牙、以太网、AI/非 AI 2D、UVC、RTSP/WebRTC 等驱动或软件包选项；“编译进 SDK”不等于已按这块板的原理图完成引脚、电源、设备树和实机功能验证。扬声器/麦克风、蓝牙、额外 GPIO/外接模块及其他摄像头接口仍应按实际接线逐项测试。

LCD 以外的外接显示、所有原理图引出的 UART/I2C/SPI、蜂鸣器和其他 GPIO，也没有在本轮逐一做电气与功能验收。

当前 KEY1 显示控制是“关屏 / LXDE 桌面”两态；串口终端可通过 UART 或 SSH 登录，但当前代码没有把串口终端作为 KEY1 的第三种 LCD 显示模式。

## 主要源码位置

- Buildroot 板级配置：`k230_linux_sdk/buildroot-overlay/configs/k230_canmv_yahboom_defconfig`
- Linux 设备树：`k230_linux_sdk/buildroot-overlay/linux/k230-canmv-yahboom-lcd.dts`
- LCD 初始化与时序：`k230_linux_sdk/buildroot-overlay/linux/display-d240c2501v1.dtsi`
- Linux 配置片段：`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/fragment/`
- Yahboom DTS、触摸和显示相关补丁：`k230_linux_sdk/buildroot-overlay/linux/0030-add-yahboom-dts.patch` 至 `0034-ft5x06-generic-touch-count.patch`
- Debian 镜像集成逻辑：`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/distribution/distribution.sh`
- 桌面、触摸和相机入口：`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-*.py`、`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-camera-preview`、`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-configure-desktop.sh`
- Wi-Fi 和 USB MTP/ADB：`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-wifi-start.sh`、`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/distribution/yahboom-wifi.service`、`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/rootfs_overlay/etc/init.d/S41adb_mtp`
- 串口启动参数：`k230_linux_sdk/buildroot-overlay/board/canaan/k230-soc/yahboom-console.env`

## 构建流程

SDK 当前选择配置记录在 `.last_conf`，应为：

```text
CONF=k230_canmv_yahboom_defconfig
```

### 1. 构建 Buildroot 基础系统

当修改了 Linux、U-Boot、设备树、内核配置或 Buildroot 软件包时，先在 SDK 根目录运行：

```bash
cd /path/to/k230/k230_linux_sdk
make CONF=k230_canmv_yahboom_defconfig
```

该步骤生成启动分区和 Buildroot 根文件系统，主要产物为：

```text
output/k230_canmv_yahboom_defconfig/images/sysimage-sdcard.img.gz
```

### 2. 组装 Debian + LXDE 镜像

`make debian` 会把上述基础镜像与 Debian 13 根文件系统、LXDE、驱动、设备树相关文件和 Yahboom 定制脚本组装为最终镜像。该阶段需要管理员权限：

```bash
pkexec make debian
```

若需保留自定义上游 Wi-Fi 凭据，先将开发板的 `/root/wifi-config.sh` 安全复制到电脑，再将路径传给构建脚本：

```bash
pkexec env YAHBOOM_WIFI_CONFIG_SOURCE=/绝对路径/wifi-config.sh \
  make debian
```

不传该变量时，镜像使用演示网络值：上游网络名 `K230-Demo-WiFi`、示例密码 `k230demo`；备用热点名 `Elaina`、密码 `22332233`。这些不是个人网络凭据。烧录到真实网络前，请通过 MTP 修改开发板上的 `/root/wifi-config.sh`，或用外部配置文件构建。WPA2 热点密码至少需要 8 个字符；不要把个人 Wi-Fi 或热点密码提交到 Git。

默认镜像使用通用密码 `root`。它会进入公开镜像，不应用于生产环境；建议构建时通过环境变量传入自己的 SHA-512 密码哈希。哈希只传给本次构建，不要提交到 Git：

```bash
ROOT_HASH="$(openssl passwd -6)"
pkexec env YAHBOOM_ROOT_PASSWORD_HASH="$ROOT_HASH" make debian
unset ROOT_HASH
```

不要把密码哈希或任何明文凭据提交到 Git。

最终镜像位于：

```text
output/k230_canmv_yahboom_defconfig/images/k230_canmv_yahboom_debian_*.img.gz
```

构建脚本同时更新 `debian.img.gz` 和版本名符号链接。重建会覆盖这些当前产物；SDK 下载缓存 `debian13.tar.gz`、`debian13_lxde.tar.gz` 和 DEB 包不是最终镜像，删除它们会使后续构建重新下载或安装，耗时明显增加。

### 3. 检查镜像

```bash
gzip -t output/k230_canmv_yahboom_defconfig/images/debian.img.gz
sha256sum output/k230_canmv_yahboom_defconfig/images/debian.img.gz
```

记录 SHA-256，烧录后可以对卡上实际写入的镜像字节做回读校验。

### 4. 写入 SD 卡

先用 `lsblk` 确认 SD 卡设备。目标必须是整张卡（例如 `/dev/sdX`），不是分区（`/dev/sdX1`）。下面命令中的设备名必须按当前机器实际情况替换：

```bash
lsblk -o NAME,PATH,SIZE,MODEL,TRAN,MOUNTPOINT
sudo umount /dev/sdX1 /dev/sdX2   # 若这些分区已挂载
gzip -t output/k230_canmv_yahboom_defconfig/images/debian.img.gz
gzip -dc output/k230_canmv_yahboom_defconfig/images/debian.img.gz \
  | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
sync
```

`dd` 会覆盖目标卡上的分区表和数据。务必再次核对 `/dev/sdX`；设备名可能随插拔变化。写完后安全弹出，再将卡插回开发板。首次启动会扩展 rootfs 分区以使用 SD 卡剩余空间。

### 空间管理

先检查 `df -h` 和 `du -sh output/k230_canmv_yahboom_defconfig/images/*`。优先删除确认过期的旧 `.img`/`.img.gz` 备份；保留当前可用镜像以及 SDK 的 Debian/LXDE 下载缓存。不要对整个 `output/` 执行 `rm -rf`，其中含有工具链构建结果、基础系统和增量构建所需文件。
