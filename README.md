# 電子仙女棒（Electronic Sparkler）— STM32G474RE

> 以 NUCLEO-G474RE 開發板驅動 7 顆 LED，模擬「電子仙女棒」的點燃、燃燒蔓延、漸弱熄滅與碳化恢復效果；透過 UART 輸入 `1`～`7` 即可點燃對應位置。
>
> An STM32G474RE firmware that simulates a handheld sparkler with 7 LEDs — ignition, chain-spreading flame, fade-out, carbonization and a 5-second auto-recovery — controlled over UART with keys `1`–`7`.

---

## 目錄

- [專案簡介](#專案簡介)
- [功能特色](#功能特色)
- [硬體需求與接腳](#硬體需求與接腳)
- [運作原理](#運作原理)
- [目錄結構](#目錄結構)
- [建置與燒錄](#建置與燒錄)
- [使用方式](#使用方式)
- [關鍵參數](#關鍵參數)
- [開發環境](#開發環境)
- [備註](#備註)

---

## 專案簡介

本專案是一個 STM32CubeIDE 產生的嵌入式韌體（MCU：STM32G474RET6，封裝 LQFP64，開發板 NUCLEO-G474RE）。核心概念是把 7 顆 LED 當成一根「仙女棒」上的火藥段：

- 使用者從序列埠輸入 `1`～`7`，點燃對應的那一段；
- 火焰會先**漸亮**，亮度到達一定程度後**向左右鄰居連鎖蔓延**；
- 每段燒到頂峰後**漸弱熄滅**，燒完的段落標記為**碳化**（無法再次點燃）；
- 當 7 段全部碳化後，**靜置 5 秒**自動恢復，可重新開始下一輪。

所有 LED 以「軟體 PWM」方式調光（利用 TIM6 定時器中斷），不需額外的硬體 PWM 通道。

---

## 功能特色

- ✨ **7 顆 LED 獨立控制**：每顆都有各自的亮度與目標亮度。
- 🌗 **軟體 PWM 調光**：TIM6 以 10 kHz 中斷掃描，提供 0～100 共 100 級亮度，畫面平順不閃爍。
- 🔥 **連鎖燃燒蔓延**：某顆 LED 亮度 ≥ 60 時，會自動點燃左右兩側尚未燃燒的鄰居，模擬真實仙女棒的火流。
- 🕯️ **碳化機制**：燒盡（亮度歸零）的段落進入碳化狀態，之後再輸入對應編號也不會點燃。
- ⏱️ **5 秒自動恢復**：全部碳化後倒數 5000 ms，自動解除碳化、恢復可點燃狀態。
- 🎮 **UART 指令控制**：115200 8N1，輸入單一字元 `'1'`～`'7'` 即點燃對應位置。
- 🧩 **中斷驅動**：PWM 由 TIM6 中斷產生、指令由 USART2 接收中斷處理，主迴圈僅負責每 20 ms 更新一次動畫狀態。

---

## 硬體需求與接腳

| 項目 | 內容 |
|---|---|
| 開發板 | NUCLEO-G474RE（STM32G474RET6，LQFP64） |
| 系統時脈 | 170 MHz（HSI 16 MHz → PLL） |
| LED 數量 | 7 顆（建議串接限流電阻） |
| 序列埠 | USART2，115200 bps、8 資料位元、1 停止位元、無同位檢查 |

### LED 接腳對應

| LED 編號 | 指令鍵 | GPIO |
|:---:|:---:|:---|
| LED 1 | `1` | PA0 |
| LED 2 | `2` | PA1 |
| LED 3 | `3` | PB0 |
| LED 4 | `4` | PB1 |
| LED 5 | `5` | PA4 |
| LED 6 | `6` | PA5 |
| LED 7 | `7` | PA6 |

> 註：LED 3、LED 4 特意使用 PB0、PB1，以避開 UART 使用的 PA2（TX）與 PA3（RX）。

### UART 接腳

| 訊號 | GPIO |
|---|---|
| USART2_TX | PA2 |
| USART2_RX | PA3 |

其餘為除錯／時鐘用腳：SWD（PA13/PA14）、SWO（PB3）、外部振盪（PF0/PF1）、RTC 32.768 kHz（PC14/PC15）。

---

## 運作原理

1. **系統初始化**：`HAL_Init()` → `SystemClock_Config()`（PLL 至 170 MHz）→ 初始化 GPIO / TIM6 / USART2。
2. **軟體 PWM（TIM6）**：TIM6 預分頻 169（170 MHz → 1 MHz），自動重載 99（每 100 µs 中斷一次，即 10 kHz）。中斷回調 `HAL_TIM_PeriodElapsedCallback()` 內以計數器 `pwm_counter`（0～99）與各 LED 亮度比較，決定該瞬間 GPIO 為高或低，形成 100 級 PWM。
3. **UART 指令**：`HAL_UART_RxCpltCallback()` 收到字元後，若為 `'1'`～`'7'` 且該位置尚未碳化，就把對應的目標亮度設為 100（點燃）。
4. **動畫更新**：主迴圈每 20 ms 呼叫 `Sparkler_Update()`，分四階段處理：
   - **亮度漸變**：亮度 < 目標值時 +1 漸亮；亮度 > 目標值時快速 -3 漸暗；歸零後標記碳化。
   - **連鎖蔓延**：亮度 ≥ 60 且目標為 100 的 LED，會點燃左右未碳化、未點燃的鄰居。
   - **熄滅引導**：亮度達 100 後把目標降為 0；目標值在消散階段每次 -5，加快熄滅。
   - **碳化恢復**：全部碳化後開始計時，經過 5000 ms 清除所有碳化旗標，恢復仙女棒。

---

## 目錄結構

```
20261103test/
├── .gitattributes
├── .gitignore
└── B11413106test/                 # STM32CubeIDE 專案本體
    ├── B11413106test.ioc          # STM32CubeMX 設定檔（腳位／時鐘／周邊）
    ├── B11413106test Debug.launch # STM32CubeIDE 除錯啟動設定
    ├── .cproject / .project       # Eclipse / CubeIDE 專案設定
    ├── .mxproject                 # CubeMX 產生的專案中繼資料
    ├── .settings/                 # CubeIDE 專案偏好設定
    ├── STM32G474RETX_FLASH.ld     # Flash 連結腳本
    ├── STM32G474RETX_RAM.ld       # RAM 連結腳本
    ├── Core/
    │   ├── Inc/                   # main.h、HAL 設定、中斷標頭
    │   ├── Src/                   # main.c（主要邏輯）、HAL MSP、中斷、syscalls 等
    │   └── Startup/               # startup_stm32g474retx.s（啟動組語）
    ├── Drivers/                  # CMSIS 與 STM32G4xx HAL 驅動
    └── Debug/                    # 建置產物（makefile、物件清單、map/list 等）
```

> 主要使用者邏輯集中在 `B11413106test/Core/Src/main.c`。

---

## 建置與燒錄

### 方式一：STM32CubeIDE（建議）

1. 開啟 STM32CubeIDE，選擇 **File → Import → Existing Projects into Workspace**。
2. 選擇本專案資料夾 `B11413106test`（含 `.project` / `.cproject`）。
3. 點 **Build（Ctrl+B）** 編譯（預設 Debug 組態）。
4. 接上 ST-LINK，點 **Run / Debug** 燒錄並執行。
   - Debug 啟動設定已內建於 `B11413106test Debug.launch`。

### 方式二：命令列（GCC）

若環境已安裝 `arm-none-eabi-gcc` 與 `make`，可直接使用專案內已產生的 `Debug/makefile`：

```bash
cd B11413106test/Debug
make all
```

產生的 `.elf` / `.hex` 可再以 ST-LINK 工具（STM32CubeProgrammer 或 `openocd`）燒錄。

> 若需要以 CubeMX 重新產生程式碼，請開啟 `B11413106test.ioc`；本專案使用 STM32Cube FW_G4 V1.6.3 韌體套件。

---

## 使用方式

1. 燒錄完成後，使用序列埠終端機（如 PuTTY、Tera Term、Arduino Serial Monitor 等）連線到開發板的 USART2：
   - 鮑率 **115200**、資料位元 **8**、停止位元 **1**、同位檢查 **None**。
2. 輸入 `1`～`7` 其中一個字元，即可點燃對應編號的 LED。
3. 觀察火焰由點燃處向兩側連鎖蔓延、漸亮、熄滅、碳化。
4. 待 7 段全部碳化後約 5 秒，系統自動恢復，可再輸入指令重新開始。

> 小提示：若某段已碳化，輸入對應編號不會有反應，屬正常行為。

---

## 關鍵參數

| 參數 | 值 | 說明 |
|---|---|---|
| `LED_COUNT` | 7 | 仙女棒 LED 總數 |
| `PWM_MAX` | 100 | 軟體 PWM 亮度上限（決議度） |
| TIM6 Prescaler | 169 | 170 MHz → 1 MHz 計數 |
| TIM6 Period | 99 | 每 100 µs 中斷（10 kHz） |
| 動畫更新週期 | 20 ms | 主迴圈 `Sparkler_Update()` 呼叫間隔 |
| 蔓延門檻亮度 | 60 | 達此亮度才向鄰居蔓延 |
| 碳化恢復時間 | 5000 ms | 全部碳化後自動恢復的等待時間 |
| 系統時脈 | 170 MHz | HSI 16 MHz + PLL（M=4, N=85, R=2） |
| UART 鮑率 | 115200 | USART2，8N1 |

---

## 開發環境

| 項目 | 版本／內容 |
|---|---|
| 開發板 | NUCLEO-G474RE |
| MCU | STM32G474RET6（STM32G4 系列） |
| IDE | STM32CubeIDE |
| 設定工具 | STM32CubeMX 6.18.1（DB 6.0.181） |
| 韌體套件 | STM32Cube FW_G4 V1.6.3 |
| 工具鏈 | GNU Arm Embedded（GCC） |
| HAL 驅動 | STM32G4xx HAL Driver + CMSIS |

---

## 備註

- 本專案為課程／個人練習用途的韌體範例，主要邏輯與註解集中於 `B11413106test/Core/Src/main.c`。
- `Debug/` 目錄為建置產物（`.list`、`makefile`、物件清單等），一般不需納入版本控制，可視情況加入 `.gitignore`。
- LED 請務必串接適當限流電阻，避免 GPIO 過電流損壞。

---

**作者**：B11413106（YunTech）
