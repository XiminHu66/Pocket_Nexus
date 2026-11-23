# StickS3 BLE 翻页器

适用 **M5Stack StickS3 K150**。通过蓝牙连接 BOOX、电脑或手机，五种模式都可以在 StickS3 上切换。

## 网页刷写

用桌面版 Chrome 或 Edge 打开 [BLE 翻页器安装页](https://ximinhu66.github.io/Pocket_Nexus/#page-turner)。用 USB-C 数据线连接 K150，按住电源键至绿灯闪烁后松开，点击「连接并安装 BLE 翻页器」，选择设备串口。安装时保持 `Erase device` 未勾选，以保留设备设置。要切换回多功能固件，在同一网页选择「Pocket Nexus」。

## 模式与键位

同时按住正面 **A** 和侧面 **B** 约 1 秒后松开。**A** 在菜单中循环选择，**B** 确认。MANGA、VIDEO 和 IPHONE 打开后各有两种输入方式，同样用 A 和 B 操作。再按一次双键组合可以退出，15 秒无操作也会退出。确认后立即生效，重启会记住上次选择。

| 模式 | 正面 A | 侧面 B | 使用方式 |
| --- | --- | --- | --- |
| BOOX | 音量减 | 音量加 | 在 Leaf 5C 的 Legado 或其他阅读 App 中打开音量键翻页 |
| PPT | Page Down | Page Up | 在电脑上先进入 PowerPoint 放映，再用两键切页 |
| MANGA → Page arrows | 右方向键 | 左方向键 | 在支持左右键翻页的分页漫画阅读器中使用 |
| MANGA → Page Up / Down | Page Down | Page Up | 在长条漫画网页先点一下内容区，再按一屏上下滚动 |
| VIDEO → PC arrow keys | 下方向键 | 上方向键 | 在电脑短视频页点击内容区，适用于支持上下键切换的页面 |
| VIDEO → Mouse wheel | 滚轮向下 | 滚轮向上 | 适用于接收滚轮的应用 |
| IPHONE → Vertical swipe | 手指向上划 | 手指向下划 | iPhone 竖屏的 YouTube Shorts 和长条漫画 |
| IPHONE → Horizontal swipe | 手指向左划 | 手指向右划 | iPhone 竖屏的横向分页漫画 |

普通视频播放器可能把上下方向键用作音量。请在短视频流里测试。若分页漫画从右往左读，可通过 USB 串口设 `invert on`，再试两个方向。此设置会反转当前设备的 A/B 映射。

首次安装和 v0.1 从未保存键位的设备默认进入 **PPT**。v0.1 已保存过的 Volume、Page、Arrow 键位分别延续为 BOOX、PPT、MANGA。日常短按在松手时发送一次，长按单键也只发一次。设备通过蓝牙发送按键，USB-C 用于供电和刷机。

## 配对和多设备切换

- **已有配对**从 v0.2 升级时不勾选 `Erase device`。现有设备会迁入第一个位置，保留模式和方向。可在 `Manage devices → 选择设备 → Name` 给它加上标签。
- **切换设备**同时按住 A+B 约 3 秒，看到 `DEVICES` 后松开。A 浏览，B 连接。菜单优先高亮上一次使用的另一台设备，适合两台设备来回切换。当前连接在列表中有星号。
- **首次配对**在目标设备的蓝牙设置选择 **StickS3 Page**。首个配对后进入 `DEVICES → Add device` 添加其他设备，选择 PC、BOOX、IPHONE 或 DEVICE 标签。配对窗口为 90 秒，最多保存 4 台。PC 初始用 PPT，BOOX 初始用音量键，IPHONE 初始用纵向滑动。首次自动配对使用通用标签和默认键位。
- **等待超时**显式切换等待约 12 秒，已开始的蓝牙配对握手另有超时。目标不可用时，A 重试，B 返回上一台设备。重启会尝试连接上次成功连接的设备。选定设备、加密连接和输入通道都就绪后才发送按键，切换过程的按键不会补发。
- **移除设备**进入 `DEVICES → Manage devices → 选择设备 → Forget`，再按 A 确认或 B 取消。如果设备已经清除了 StickS3 的配对，应在 StickS3 也清除对应的一项后重新添加。列表满时可以先移除一个旧绑定。

同一台设备通过 A+B 约 1 秒继续打开模式菜单。每台设备分别记住键位和方向。

## iPhone 漫画与 YouTube Shorts

1. 打开「设置 → 辅助功能 → 触控 → 辅助触控」。在该页面的「设备」里确认 `StickS3 Page` 被识别为指针设备。
2. 竖屏打开内容。Shorts 和长条漫画选 `IPHONE → Vertical swipe`。横向分页漫画选 `IPHONE → Horizontal swipe`。
3. 按 A/B 时，指针会先移动到内容区，再按下拖动并松开。按一次等动作完成再按下一次。v0.3.0 中标签为 IPHONE 的旧漫画、短视频模式会迁移到对应滑动模式，标签为 DEVICE 的旧绑定可手动切换。

如果 StickS3 显示 `NO POINTER`，先检查辅助触控里的「设备」。若蓝牙显示已连接但「设备」里没有指针，再只删除 iPhone 这一对绑定，在 StickS3 的 `Add device` 选 IPHONE 标签，再通过辅助触控里的「设备 → 蓝牙设备」重新配对。若指针会动但拖动落在边缘，在辅助触控中适当调低跟踪灵敏度。若启用了停留控制或吸附到项目，可关闭后再试。不同机型和跟踪灵敏度下应实测手势距离和 App 反应。

## USB 串口设置

用数据 USB-C 连接串口终端（115200），每行一条命令。可用 `help` 或 `status` 查看。

| 命令 | 效果 |
| --- | --- |
| `mode boox` / `mode ppt` | BOOX 或 PPT |
| `mode manga` / `mode manga-scroll` | 漫画分页或整屏滚动 |
| `mode video-pc` / `mode video-phone` | 短视频方向键或蓝牙滚轮 |
| `mode iphone-vertical` / `mode iphone-horizontal` | iPhone 纵向或横向滑动 |
| `invert on` / `invert off` | 反转或还原前后方向 |

屏幕在状态变化或操作菜单时亮起，普通状态 10 秒后熄屏。BLE 在熄屏后可继续使用。

## 构建、回滚与验收

使用 ESP-IDF **v5.5.2**，M5Unified 与 M5GFX 锁定为 **0.3.0**。

```sh
idf.py build
idf.py merge-bin -o sticks3-page-turner-0.3.1.bin
cc -std=c11 -Wall -Wextra -Werror -I main main/controls.c main/swipe.c tests/test_controls.c -o /tmp/sticks3-page-test
/tmp/sticks3-page-test
cc -std=c11 -Wall -Wextra -Werror -I main main/hosts.c tests/test_hosts.c -o /tmp/sticks3-hosts-test
/tmp/sticks3-hosts-test
```

手动刷写优先用 `idf.py -p COMx flash`。单一合并镜像写入 `0x0` 会覆盖配置所在区间。完整 Flash 备份可能含 Wi-Fi 或蓝牙凭据，恢复时在同一台 K150 上写回 `0x0`。网页刷写默认使用分区镜像。

实机验收建议先在 Windows PowerPoint 放映中前后各翻一页，接着测试电脑漫画页面的分页和滚动、短视频的上一条和下一条。测试 Leaf 5C 翻页，再在 iPhone 原生 YouTube Shorts、长条漫画和分页漫画分别检查 A/B。检查升级保留旧配对、三台设备同时开蓝牙时只向选中的设备发按键、超时后退回原设备、恢复上次设备及其键位，以及移除一个设备后其他绑定仍能重连。
