# StickS3 BLE 翻页器

适用 **M5Stack StickS3 K150**。独立 ESP32-S3 固件，适合 BOOX Leaf 5C 等可接收蓝牙 HID 按键的阅读器。翻页只使用蓝牙，离线可用。

## 网页刷写

用桌面版 Chrome 或 Edge 打开 [BLE 翻页器安装页](https://ximinhu66.github.io/Pocket_Nexus/#page-turner)。用 USB-C 数据线连接 K150，按住电源键至绿灯闪烁后松开，点击「连接并安装 BLE 翻页器」，选择设备串口。安装时保持 `Erase device` 未勾选，以保留设备设置。要切换回来，在同一网页选择「Pocket Nexus」。

## 配对与翻页

1. StickS3 开机后，首次配对窗口为 90 秒。在 BOOX 的蓝牙设置里连接 **StickS3 Page**。
2. 打开你常用的阅读 App。如果该 App 有「音量键翻页」，先打开它。默认配置用音量减键翻下一页、音量加键翻上一页。
3. 正面 **A / KEY1** 翻下一页，侧面 **B / KEY2** 翻上一页。松开按键时翻页。按住单键只翻一次，电源键保留原本的硬件功能。
4. 如果 App 调整了音量或没有翻页，从 USB 串口试 `profile page`，然后试 `profile arrow`。如果方向反了，输入 `invert on`。

蓝牙短暂断线时会尝试重连已绑定的阅读器，最多广播 60 秒。显示 `DISCONNECTED` 后短按 A 或 B 可再试。连接前按下的键只用于恢复连接，连接成功后重新按一次翻页。第一次配对超时后，同时长按 A+B 约 5 秒可重新打开配对窗口。

要换阅读器，在旧阅读器上忘记 **StickS3 Page**，然后在 StickS3 同时长按 A+B 约 5 秒。看到 `FORGET READER?` 后释放两键，再短按 A 确认，或按 B 取消。设备只清除蓝牙绑定，保留键位配置。配对使用加密的 BLE Just Works，请在你能确认目标阅读器的场合配对。

## USB 串口设置

连接数据 USB-C，打开能发送行结束符的串口终端（115200），每行一条命令。

| 命令 | 效果 |
| --- | --- |
| `status` | 查看当前键位、方向、绑定、连接和电量 |
| `profile volume` | 使用音量减 / 音量加。默认 |
| `profile page` | 使用 Page Down / Page Up |
| `profile arrow` | 使用右 / 左方向键 |
| `invert on` / `invert off` | 反转 / 恢复方向 |

设置立即保存到设备。屏幕只在切换状态、设置和低电量提醒时短暂亮起，10 秒后关闭背光并让 LCD 休眠。第一版采用熄屏 BLE 常连，协商低功耗连接参数。空闲时仍可直接翻页，实际续航需测量。

## 构建与刷写

在此目录下使用 ESP-IDF **v5.5.2**。M5Unified 与 M5GFX 锁定在 **0.3.0**。

```sh
idf.py build
idf.py merge-bin -o sticks3-page-turner-0.1.0.bin
```

完整镜像烧写地址为 `0x0`。优先使用分区烧写 `idf.py -p COMx flash`，或者发行包中的三段镜像分别写入 `0x0`、`0x8000`、`0x10000`。确认串口和板型，烧写前备份旧固件和设备配置。完整镜像会覆盖分区间隙，其中包含 NVS。分区烧写保留 NVS 所在的地址范围。需要下载模式时，通过数据 USB-C 接入电脑，按住电源键，绿灯开始闪烁后松开。[M5Stack 官方下载模式说明](https://docs.m5stack.com/en/core/StickS3#download-mode)

### 回滚

如已做完整 Flash 备份，在同一台 K150 上用 esptool 将备份写回 `0x0`。备份可能含 Wi-Fi 凭据和蓝牙绑定信息，请保存在可信位置。也可以用 [Pocket Nexus 原网页刷机入口](https://ximinhu66.github.io/Pocket_Nexus/) 重新安装原固件，再按需恢复其配置。

## 验收

本机可运行按键和键值测试。

```sh
cc -std=c11 -Wall -Wextra -Werror -I main main/controls.c tests/test_controls.c -o /tmp/sticks3-page-test
/tmp/sticks3-page-test
```

以下项目留待 StickS3 K150 和 BOOX Leaf 5C 实机验收。

| 项目 | 操作与通过标准 |
| --- | --- |
| 首次配对、重启和重连 | 首次配对后重启，能重新连接。对第二台主机保持单主机绑定。超时后通过实体键恢复 |
| 阅读 App | 记录 BOOX 固件和 App 版本。NeoReader 和常用第三方 App 分别确认哪种键位可用、方向正确、系统音量无变化 |
| 200 次翻页 | 下一页、上一页各 100 次。长按单键只翻一次，双键配对手势期间不翻页 |
| 锁屏和断开 | 熄屏后真实翻页。关闭再开启 BOOX 蓝牙或锁屏再唤醒，检查恢复时间及断线期间的按键是否被丢弃 |
| 配对更新 | 长按双键，分别验证取消和确认；确认后可换主机，三种键位和方向设置在重启后能保留 |
| 离线和功耗 | 关闭 Wi-Fi。测配对广播、熄屏常连、每分钟翻两页、断线重连、广播停止后待机。低电提醒在电池约 15% 时检查 |

参考 [ESP-IDF BLE HID Device 示例](https://github.com/espressif/esp-idf/tree/v5.5.2/examples/bluetooth/esp_hid_device)、[BOOX 按键映射说明](https://shop.boox.com/blogs/news/new-features-at-a-glance)。
