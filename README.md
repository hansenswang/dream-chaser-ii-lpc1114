# 逐梦贰号 — 多外设智能小车 / Multi-Peripheral Smart Car

**LPC1114 (Cortex-M0) · 八套子系统 · 自研 PCB · 7.4 V 双锂电**

第一代自制智能小车：在一块 70×100 mm 的自制 PCB 上集成八套独立子系统 —— 四驱 PWM、一键启动、蜂鸣器音乐、电池电压监测、转向灯、超声波避障、五路红外循迹、串口控制台。

A first-generation self-built smart car: eight independent subsystems on one 70×100 mm custom PCB — four-wheel PWM drive, one-touch start, buzzer melody, battery supervision, turn indicators, ultrasonic obstacle avoidance, five-channel infrared line following, and a UART console.

> 这是**系列项目的第一次迭代**。八个月后的第二代（接力小车）放弃了"每个外设各来一遍"的广度路线，转而在单一线循迹能力上做得更深 —— 引入 RTOS、状态机与闭环 PID。两者的对比本身就是一份工程成长记录。
>
> *This is the **first iteration** of a series. Eight months later the second generation abandoned breadth for depth — dropping the "one demo per peripheral" approach in favour of an RTOS, a state machine and closed-loop PID on line following alone. The contrast between the two is itself a record of engineering growth.*

---

## 📌 项目亮点 / Highlights

| # | 亮点 | 具体实现 |
|---|------|---------|
| 1 | **八套子系统一次集成** | 每套子系统对应一个单片机知识点：GPIO 输出（灯）、GPIO 输入/中断（按键）、定时器（蜂鸣器）、ADC（电池）、PWM（电机）、外设传感器（超声、红外）、串口（调试扩展）。目标不是深度而是**覆盖面** |
| 2 | **自研 PCB，立创免费打样** | 70×100 mm 双层板，除 RZ7899（SOP-8）外全部为插件封装，适合新手焊接。含完整的电源、驱动、传感、指示分区 |
| 3 | **无源蜂鸣器硬件 PWM 演奏** | 用 TMR32B0 的 MAT2 通道直接输出音频，`MR3 = SystemCoreClock/2/(f-1)` 设定音高，主循环无需参与 —— 这是"硬件产生音调"而非软件翻转 |
| 4 | **ADC 电池监测 + 低压呼吸灯** | 三颗 10 kΩ 串联取 1/3 分压点接 ADC7（P1_11），8 次采样取平均抑制电机启动压降造成的抖动 |
| 5 | **超声波避障带超时兜底** | CT16B0 预分频 47 得 1 µs 计时，回波高电平直接就是往返微秒数。等待回波有看门狗计数，避免无回波时死锁 |
| 6 | **中断驱动的按键多模式** | P2_10 外部中断，按键循环切换"行驶 / 音乐 / 静音"三态；5 µs 后二次读引脚实现去抖 |
| 7 | **5 路循迹显式状态表** | 32 种位模式全部显式映射到 7 个语义状态，无 if-else 链，每个模式都有确定行为 |
| 8 | **中断回显的串口链路** | UART0 中断回显本身就是"无线扩展通路已验证"的证明 —— 换成蓝牙/WiFi 模块无需改动上层 |

---

## 1. 系统概览 / System Overview

```
                    ┌─────────┐   ┌──────────────┐
                    │ 电源供电 │   │ PWM 电机驱动  │
                    └────┬────┘   └──────┬───────┘
                         │ ADC            │
   ┌──────────────┐      │                │      ┌──────────────┐
   │ 无线控制模块  ├──────┤                ├──────┤ 蜂鸣器音乐播放 │
   └──────────────┘      │   ┌────────┐   │      └──────────────┘
   ┌──────────────┐      └──►│LPC1114 │◄──┘
   │ 按键控制模块  ├─────────►│ 核心   │   ┌──────────────┐
   └──────────────┘          │ 控制单元│──►│  LED 指示灯   │
   ┌──────────────┐          └───┬────┘   └──────────────┘
   │ 红外循迹传感器 ├─────────────┤        ┌──────────────┐
   └──────────────┘              └───────►│  OLED 显示屏  │
   ┌──────────────┐                       └──────────────┘
   │ 超声波避障模块 ├───────────────────────┘
   └──────────────┘
```

见 `docs/figures/21_system_block_diagram.png`。

### 1.1 功能需求 / Functional requirements

| # | 功能 | 知识点 | 实现状态 |
|---|------|--------|---------|
| 1 | 四路电机独立 PWM 控制 | 定时器 / PWM 输出 | ✅ 已实现 |
| 2 | 独立按键一键启动 | GPIO 输入 / 外部中断 | ✅ 已实现 |
| 3 | 红外光电对管循迹 | 外设传感器 + 状态判别 | ✅ 已实现 |
| 4 | 超声波避障 | 外设传感器 + 计时 | ✅ 已实现 |
| 5 | 车头双 LED 灯光 | GPIO 输出 | ✅ 已实现 |
| 6 | 无源蜂鸣器播放音乐 | 定时器 / PWM 音频 | ✅ 已实现 |
| 7 | ADC 检测电池电压 + 低压呼吸灯 | ADC 采集 | ✅ 已实现 |
| 8 | 外接串口扩展无线模块 | UART | ✅ 通路已验证（回显） |

### 1.2 性能指标 / Design targets

| 指标 | 取值 |
|------|------|
| 核心板 | LPC1114 最小系统板（3.3 V，注意与 5 V 器件兼容） |
| 供电 | 2× 14500 锂电 7.4 V → AMS1117-3.3 → 3.3 V |
| 电机 | N20 直流减速电机，减速比 1:10，1400 rpm |
| 轮径 | 43 mm |
| 电路板尺寸 | 3900 mil × 2755.9 mil（约 99 × 70 mm） |
| 排针规格 | 2.54 mm / 20 针，间距 600 mil |
| 辅助功能时延 | 音乐/灯光 PWM 切换 ≤ 10 µs；与主功能联动 ≤ 50 ms；蓝牙调试指令响应 ≤ 200 ms |

---

## 2. 硬件设计 / Hardware

完整原理图见 `docs/figures/01_full_schematic_all_subcircuits.png`（立创 EDA 单页出图，含标题栏）。

### 2.1 分电路 / Sub-circuits

| 图 | 电路 | 要点 |
|----|------|------|
| `16` | 电源输入 | 双电源输入（外部 +3V3 / 电池），D1 防反接，SW1 路径切换，AMS1117-3.3 输出 3.3 V；C1 100 µF + C3/C4 10 µF 多级滤波；R2 10 kΩ + LED1 电源指示 |
| `02` | 主控电路 | LPC1114 MAS 核心板，引脚逐一标注功能 |
| — | 蜂鸣器 | SS8050 NPN 开关，R10 10 kΩ 基极电阻（约 0.23 mA 基极电流保证饱和），D2 续流二极管 |
| — | 按键电路 | 一端接主控、一端接地，按下读到低电平 |
| — | 无线通信 | E01-ML01D，SPI 接口（CSN/MOSI/MISO/SCK/IRQ），C10 + C15 双 100 nF 构成 π 型滤波，CE 双引出 |
| `19` | 超声波接口 | HC-SR04，+3V3 供电，Trig/Echo 直连主控 |
| — | 循迹电路 | ITR9909 ×5 + 10 kΩ 电阻网络 + 滤波电容，测距 1–15 mm 可调 |
| — | 电机驱动 | RZ7899 ×4，每路独立 VCC/GND，模块化布局降低串扰 |
| `03` | 串口 / OLED | HC0506 蓝牙排针 + HS96L03W2C03 OLED（I²C） |
| `05` | ADC 电压检测 | R14/R15/R16 三颗 10 kΩ 串联分压 |
| `04` | 指示灯 | 前后各两个 LED，限流电阻驱动 |

### 2.2 引脚分配 / Pin assignment

```
电机（8 个方向引脚 + 4 路 PWM）
  左前  正转 P1_2   反转 P1_4
  左后  正转 P0_11  反转 P1_1
  右前  正转 P0_8   反转 P1_9
  右后  正转 P0_9   反转 P1_10

传感器与指示
  循迹 ×5        P2_7 … P2_11
  循迹激励       P0_2
  超声波 Trig    P2_5
  超声波 Echo    P2_4
  按键 K1        P2_10（EINT2，下降沿）
  电池检测       P1_11（ADC7）
  蜂鸣器         P0_1（TMR32B0 MAT2）

灯光（4 前 + 2 后）
  左前灯 P0_7   右前灯 P0_6
  左后灯 P0_3   右后灯 P2_8

显示与通信
  OLED SCL P0_4   OLED SDA P0_5
  串口 TXD P1_7   RXD P1_6
```

### 2.3 供电 / Power

```
2×14500 串联 7.4V（满电 8.4V）
        │
        ▼
   ┌────┴────┐
   │ D1 防反 │
   │ SW1 开关 │
   └────┬────┘
        │
        ▼
   AMS1117-3.3  ──► 3.3V  ──► 核心板 / 传感器 / OLED
   (压差典型 1.1V，故电池需 ≥ 4.4V)
        │
        └──► 电机驱动直接由电池 7.4V 供电
```

**注意 LDO 压差：** AMS1117 的典型压差为 1.1 V，因此电池电压低于 4.4 V 时 3.3 V 输出将无法维持。两节 14500 满电 8.4 V，即使放到 5 V 附近仍有余量，但这也是低压监测阈值设为 6.6 V 的原因之一 —— 留出充分的余量而非等到输出开始塌陷。

---

## 3. 软件结构 / Software Structure

### 3.1 主流程

```
main()
 ├─ Led_Init()            灯先亮，确认供电
 ├─ SysTick_Config(/10)   100 ms 节拍（转向灯）
 ├─ Buzzer_Init() + 启动提示音
 ├─ Oled_Init()           SSD1306 初始化序列
 ├─ Serial_Init()         UART0 115200
 ├─ Track_Init()          五路输入
 ├─ Key_Init()            EINT2
 ├─ Adc_Init()            ADC7
 └─ HcSr04_Init()         CT16B0 1 µs 计时

 while(1)
   ├─ 读电池电压 → 低压则呼吸灯
   ├─ 触发超声波 → 测距
   │    ├─ ≤ 40 cm：刹车 300 ms → 左转规避
   │    └─ 否则：读五路循迹 → 状态表解码 → 差速控制
   └─ 按键模式为"音乐"则演奏
```

### 3.2 循迹状态表 / Line-sensor state table

| 模式 | 含义 | 动作 |
|------|------|------|
| `00000` | 丢线 | 保持上一次判定的方向 |
| `00100` `01100` `00110` `01110` `01000` `00010` | 线在中心 | 直行 `Motor_SetSpeed(20, 20)` |
| `11100` `11000` `10000` `11010` | 线明显偏左 | 强左修正 `(5, 20)` 或 `(10, 20)` |
| `00111` `00011` `00001` `00101` | 线明显偏右 | 强右修正 `(20, 5)` 或 `(20, 10)` |
| `11111` | 全黑 | 十字或停止线 |
| 其他 | 边缘过渡 | 按仍能看到线的一侧做轻微修正 |

**为什么用显式表而不是 if-else 链：** 32 种模式全部有确定行为。用 if-else 时容易漏掉某种组合，而漏掉的那一种会走到 `default` 分支 —— 在高速行驶时一个未定义分支就等于一次失控。写成表之后，编译器能直接检查覆盖完整性，后续维护也不会漏。

### 3.3 文件结构

```
src/
├── main.c         应用入口：外设初始化 + 主循环
├── motor.c/h      RZ7899 四驱 + 软件 PWM + 差速/原地转向
├── track.c/h      ITR9909 ×5：激励、读取、状态表解码
├── ultrasonic.c/h HC-SR04 测距（CT16B0 1 µs 计时 + 超时兜底）
├── adc.c/h        电池电压检测（8 次平均 + 分压还原）
├── buzzer.c/h     无源蜂鸣器 PWM 演奏 + 转向灯
├── system.c/h     延时、SysTick、按键中断、UART、OLED
```

---

## 4. 调试记录 / Debug Log

| 环节 | 现象 | 结论 |
|------|------|------|
| 环境搭建 | DAPLink-LPC 与系统板引脚对应连接，Keil 中选择 CMSIS-DAP Debugger | 硬件链路正常 |
| ADC 电压检测 | 未开电池时读到 2.58 V（板内供电） | 分压与转换逻辑正确 |
| ADC 电压检测 | 开电池后读到 7.13 V（两节 7.4 V，已用一段时间） | ✅ 满足测量要求 |
| 音乐播放 | 听见播放，调出 GPIO0 与 TMR32B0 参数界面可见规律变化 | ✅ 满足要求 |
| 电机 / 循迹 / 指示 | 编译无错，烧写后左偏左转灯闪、右偏右转灯闪 | ✅ 满足要求 |

**Keil 实测输出（UART 初始化验证）：**
```
Build target 'Target 1'
compiling UART.c...
Program Size: Code=8724 RO-data=520 RW-data=52 ZI-data=1240
0 Error(s)
```

---

## 5. 局限与反思 / Limitations and Reflection

诚实地记录当时的边界，比把每一格都打勾更有价值：

| 项目 | 状态 | 说明 |
|------|------|------|
| 四路电机独立 PWM | ⚠️ 部分 | 代码保留了逐轮控制接口，但实际整车运行只用到左右两侧差速 —— 四轮独立的真正价值（如原地旋转）未被充分利用 |
| 无线控制 | ➖ 未实现 | 电路与串口通路均已就绪，回显测试证明链路可用，但未接入蓝牙/WiFi 模块做实际遥控 |
| 避障精度 | ⚠️ 有限 | 40 cm 阈值固定，未按车速动态调整；且超声波对斜面反射不敏感 |
| 循迹鲁棒性 | ⚠️ 有限 | 硬编码状态表在低速稳定，但速度提高后需要更细的中间档位；这一局限正是第二代改用 PID 的直接动因 |

**成本评估中的说明：** 报告中的 255 元硬件成本表列出了 TB6612、ESP-01、带编码器电机等器件，这是**设计阶段的选型评估**（用于对比不同方案），与实际装配使用的 RZ7899 + N20 方案不完全一致。这一点在原始报告里没有区分清楚，此处予以澄清。

> **A note on the cost table.** The ¥255 hardware table in the original report lists parts (TB6612 drivers, an ESP-01 WiFi module, encoder motors) that were part of a **design-stage option study** rather than the configuration actually built, which used RZ7899 + N20. The two were not distinguished in the original document; this note corrects that.

**如果重做：** ① 先明确"要证明什么"再选器件，避免评估表与实际装配脱节；② 用硬件 PWM 替代软件 PWM 驱动电机，把主循环从延时中解放出来；③ 循迹引入连续量而非离散档位 —— 这正是第二代所做的事。

---

## 6. 成本 / Cost

参考原报告的评估口径（**选型评估值，非实际采购价**）：

| 配件 | 评估价（元） | 说明 |
|------|-------------|------|
| 核心板 LPC1114 | 30 | ARM Cortex-M0 |
| 直流减速电机（带编码器） | 55 | 运动驱动 |
| TB6612 电机驱动 | 18 | 评估选项 |
| 超声波 HC-SR04 | 16 | 障碍检测 |
| TCRT5000 循迹探头 | 14 | 路径追踪 |
| 有源蜂鸣器 | 6 | 音乐播放 |
| 按键/LED 组合模块 | 6 | 用户交互 |
| ESP-01 WiFi 模块 | 25 | 无线通讯（评估选项） |
| 18650 电池组 + 降压 | 25 | 动力供应 |
| 车架/连接件/PCB | 35 | 含定制 PCB |
| 备用损耗预算 | 25 | 元件损坏/焊接损耗 |
| **合计** | **255** | |

工时评估：软件 36 h、人工（组装焊接 + 校准 + 联调 + 验证 + 文档）28 h，合计 64 h（以小组为单位）。

---

## 7. 图集索引 / Figure Index

| # | 文件 | 内容 |
|---|------|------|
| 01 | `01_full_schematic_all_subcircuits.png` | 立创 EDA 完整原理图（含标题栏与署名） |
| 02 | `02_schematic_main_control_annotated.png` | 主控电路，逐脚功能标注 |
| 03 | `03_schematic_uart_oled.png` | 串口 + OLED 接口 |
| 04 | `04_schematic_indicator_leds.png` | 指示灯电路 |
| 05 | `05_schematic_battery_adc_detect.png` | 电池分压检测 |
| 06 | `06_software_flowchart_main.png` | 主程序流程图 |
| 07 | `07_flowchart_timer_pwm.png` | 定时器 PWM 初始化流程 |
| 08 | `08_flowchart_ultrasonic.png` | 超声波测距与避障流程 |
| 09 | `09_pcb_top_layer_silkscreen.png` | PCB 顶层布局与丝印 |
| 10 | `10_pcb_3d_render.png` | 3D 预览（俯视装配） |
| 11 | `11_car_assembled_oled_lit.jpg` | 实车装配，OLED 点亮 |
| 12 | `12_car_on_track_lamps_lit.jpg` | 跑道测试，尾灯点亮 |
| 13–15, 18 | `13…18_keil_*.jpg` | Keil 编译、主循环逻辑、调试监视窗口、外设寄存器视图 |
| 16 | `16_schematic_power_input_ams1117.png` | 电源输入 + AMS1117 稳压 |
| 17 | `17_pcb_3d_render_underside.png` | 3D 预览（底面，可见四电机与五探头） |
| 19 | `19_schematic_pinout_detail.png` | 引脚分配细节 |
| 20 | `20_cost_composition_tree.png` | 成本构成树 |
| 21 | `21_system_block_diagram.png` | 系统框图（八大模块） |

---

## 8. 作者 / Author

**王译 (Yiyi Wang)** — 原理图设计、主控软件、串口与 OLED
海南大学 信息与通信工程学院 · 电子信息工程 · 20223004416

同组：方清辉（原理图设计、代码调试）、李明泽（PCB 设计、原理图审核）、段皓元（PCB 设计、错误修改）

*项目性质：基于个人兴趣的嵌入式系统入门实践，非课程硬性作业。PCB 由小组自绘，立创 EDA 免费打样，元件手工焊接。*

---

## 9. 许可 / License

MIT License — 见 [`LICENSE`](LICENSE)。
