# 控制协议与接口

## UART

Daisy USART1：D13/PB6 TX、D14/PB7 RX。XIAO GPIO21 TX、GPIO20 RX，经各 1kΩ 电阻交叉连接。3.3V、230400 baud、8N1、无流控。

帧原文为 `version:u8, type:u8, sequence:u16LE, payload_size:u16LE, payload, crc32:u32LE`。版本 1；最大 payload 192 字节。CRC32/IEEE（`123456789` → `0xcbf43926`）覆盖头和 payload。原文 COBS 编码，帧末尾为 `00`。浮点为有限 IEEE754 float32、小端序。错误长度、CRC、COBS、范围直接拒绝；超长流丢弃至下个分隔符。

| 类型 | 数值 | Payload |
|---|---:|---|
| GET_STATE | 1 | 空 |
| SET_PARAM | 2 | 参数 ID u8 + float32；范围来自 parameters.hpp/schema.json |
| LOAD_PRESET / SAVE_PRESET | 3 / 4 | 槽位 u8，0–15；保存 ACK 在闪存读回校验后返回 |
| SET_KNOBS | 5 | 六个 ID u8，仅连续参数，不含 model/cab/效果开关 |
| BYPASS / TUNER | 6 / 7 | bool u8，0/1 |
| TAP | 8 | 空；相邻点击间隔设置 delay_time |
| HELLO | 9 | ESP 启动随机 nonce u32；每秒发送，nonce 改变后清除序号缓存 |
| STATE | 128 | 138 字节，见下表 |
| ACK | 129 | Result u8；sequence 对应原请求 |

Result：0 OK、1 INVALID、2 BUSY、3 STORAGE_ERROR、4 LINK_TIMEOUT。Daisy 缓存最近序号/结果，重复请求不重复执行；保存中等待最终 ACK。ESP 每 100ms 请求状态，只接受匹配 nonce 的遥测；失联超过 1.5s 报告离线。

| STATE offset | 内容 |
|---:|---|
| 0 | 26 个 float32 参数，共 104 字节 |
| 104 | 六个旋钮映射 ID |
| 110 / 111 / 112 | 当前槽位 / bypass / tuning，u8 |
| 113 | flags：bit0 dirty、bit1 busy、bit2 clipped、bit3 storageError |
| 114 / 118 / 122 | pitch Hz / CPU average / CPU maximum，float32 |
| 126 / 130 | inputPeak / outputPeak，float32 |
| 134 | 会话 nonce u32LE |

调参 OK 表示主控已接受；结构更新先淡出再由主循环配置，STATE.busy 表示未完成。ACK 不能代替实机换模耗时测量。

## 本地 HTTP

AP 名 `SeedFX-XXXX`，默认 WPA2 密码 `seedfx123`，至多两个客户端。地址 `192.168.4.1`，页面完全从固件加载。`GET /api/state` 返回遥测与 connected；`POST /api/command` 为 JSON，最多 512 字节。

```json
{"op":"set","id":0,"value":-6}
{"op":"load","preset":2}
{"op":"save","preset":2}
{"op":"knobs","knobs":[0,5,6,7,16,11]}
{"op":"bypass","enabled":true}
{"op":"tuner","enabled":true}
{"op":"tap"}
```

HTTP 200 返回 `ok:true,result:0,sequence`；非法请求 400、Daisy 离线 503、2 秒无确认 504、主控拒绝 409。接口适用于本地 AP 控制；不提供互联网账户或 OTA。

## 存储

8MB QSPI 最后 64KiB 为保留区；两个 4KiB 银行偏移 `0x7f0000` / `0x7f1000`。银行含 magic SFX1、版本、generation、active_slot、16 个 Settings 与 CRC32。Settings 含 26 个 float32、六个旋钮 ID，自然按 4 字节对齐。读校验通过后使用最新银行；保存擦写另一银行，清除旧缓存后逐字节读回确认。过程停止音频，结束后淡入。

修改存储结构时需升级银行版本并实现迁移或显式恢复工厂配置，不能重新解释旧数据。更新应用只写 `0x90040000` 起的 BIN，不做 QSPI 全片擦除。
