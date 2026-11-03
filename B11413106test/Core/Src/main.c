/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : NUCLEO-G474RE 電子仙女棒控制程式 (加快燈光熄滅速度、平滑 PWM 及 5 秒碳化恢復)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>          // 引入字串與記憶體操作函式庫，用於初始化與歸零全域陣列，原因為方便管理記憶體
#include <stdbool.h>         // 引入布林值型態支援，用於碳化狀態與等待邏輯判斷，原因便於狀態管理
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LED_COUNT 7          // 定義仙女棒總共的 LED 數量為 7 顆，原因為方便後續迴圈與陣列操作
#define PWM_MAX 100          // 定義軟體 PWM 的最大亮度極限值，數值越大階調越細緻平順
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim6;     // 宣告 TIM6 定時器句柄結構體變數，原因用於產生軟體 PWM 與動畫時基

UART_HandleTypeDef huart2;   // 宣告 UART2 句柄結構體變數，原因用於接收外部控制指令

/* USER CODE BEGIN PV */
uint8_t rx_byte;                             // 用於存放從 UART 接收到的單一字元變數
volatile uint8_t led_brightness[LED_COUNT];  // 儲存 7 顆 LED 當前的實際亮度 (0 到 100)，供中斷服務函式使用
volatile uint8_t led_target[LED_COUNT];      // 儲存 7 顆 LED 的目標亮度，原因用於實現漸變過渡
bool led_carbonized[LED_COUNT];              // 模擬碳化狀態：若為 true 表示該處已燃燒完畢碳化，無法再次點燃
uint32_t anim_tick = 0;                      // 動畫計時器變數，原因用來控制仙女棒燃燒擴散與漸弱的更新頻率
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM6_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
void Sparkler_Update(void);                  // 宣告仙女棒燃燒動態邏輯更新函式
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// 定時器 TIM6 中斷回調函式：負責產生軟體 PWM 脈波與掃描 LED 亮度
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6)                  // 檢查是否為 TIM6 定時器觸發的中斷
  {
    static uint8_t pwm_counter = 0;            // 宣告靜態變數作為 PWM 週期計數器 (0 到 99)
    pwm_counter++;                             // 每次中斷計數器遞增 1，形成高頻掃描
    if (pwm_counter >= PWM_MAX)                // 當計數器達到最大值時歸零，完成一個 PWM 週期
    {
      pwm_counter = 0;                         // 歸零計數器以重新開始下一個 PWM 週期
    }

    // 依序掃描 7 顆 LED，利用比較 pwm_counter 與當前亮度來決定開關狀態，達到平順調光
    // LED 1 控制 (位於 PA0)
    if (pwm_counter < led_brightness[0]) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET); }
    else { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET); }

    // LED 2 控制 (位於 PA1)
    if (pwm_counter < led_brightness[1]) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET); }
    else { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET); }

    // LED 3 控制 (位於 PB0，避開 UART 的 PA2)
    if (pwm_counter < led_brightness[2]) { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); }
    else { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET); }

    // LED 4 控制 (位於 PB1，避開 UART 的 PA3)
    if (pwm_counter < led_brightness[3]) { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); }
    else { HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET); }

    // LED 5 控制 (位於 PA4)
    if (pwm_counter < led_brightness[4]) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET); }
    else { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET); }

    // LED 6 控制 (位於 PA5)
    if (pwm_counter < led_brightness[5]) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET); }
    else { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); }

    // LED 7 控制 (位於 PA6)
    if (pwm_counter < led_brightness[6]) { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET); }
    else { HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET); }
  }
}

// UART 接收完成回調函式：當透過序列埠輸入字元時觸發
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2)               // 確認是否為 USART2 觸發的接收事件
  {
    if (rx_byte >= '1' && rx_byte <= '7')      // 檢查輸入字元是否介於 '1' 到 '7' 之間
    {
      int index = rx_byte - '1';               // 將字元轉換為對應的陣列索引值 (0 到 6)

      if (!led_carbonized[index])              // 檢查該位置是否已經碳化，若未碳化才允許點燃
      {
        led_target[index] = 100;               // 設定輸入點擊位置為最高亮度目標 (點燃起點)
      }
    }
    HAL_UART_Receive_IT(&huart2, &rx_byte, 1); // 重新啟動 UART 接收中斷以持續監聽下一個指令
  }
}

// 仙女棒燃燒、連鎖蔓延與碳化動畫邏輯核心函式
void Sparkler_Update(void)
{
  // 1. 亮度漸變與碳化狀態更新
  for (int i = 0; i < LED_COUNT; i++)          // 迴圈遍歷每一顆 LED 以平滑更新其亮度
  {
    if (led_brightness[i] < led_target[i])     // 如果當前亮度小於目標亮度
    {
      led_brightness[i]++;                     // 漸進式調亮，使點亮過程平順不突兀
    }
    else if (led_brightness[i] > led_target[i]) // 如果當前亮度大於目標亮度（模擬燃燒後漸弱與碳化）
    {
      // 讓亮度遞減速度加快（每次扣 3），使熄滅過程更快速俐落
      if (led_brightness[i] >= 3) {
        led_brightness[i] -= 3;
      } else {
        led_brightness[i] = 0;
      }

      if (led_brightness[i] == 0 && led_target[i] == 0) // 當亮度歸零且目標也為零時
      {
        led_carbonized[i] = true;              // 標記該 LED 已經碳化失效，無法再次點亮
      }
    }
  }

  // 2. 模擬真實仙女棒流動蔓延特性：當某處燃燒亮度達到 60 時，平順向左右未點燃的鄰居傳遞燃燒
  for (int i = 0; i < LED_COUNT; i++)
  {
    if (led_brightness[i] >= 60 && led_target[i] == 100) // 當該 LED 正在強烈燃燒時
    {
      // 往左側流動蔓延（若左側有空間且未碳化且尚未點燃）
      if (i > 0 && !led_carbonized[i - 1] && led_target[i - 1] == 0 && led_brightness[i - 1] == 0)
      {
        led_target[i - 1] = 100;               // 點燃左側鄰近 LED，實現連鎖蔓延
      }
      // 往右側流動蔓延（若右側有空間且未碳化且尚未點燃）
      if (i < LED_COUNT - 1 && !led_carbonized[i + 1] && led_target[i + 1] == 0 && led_brightness[i + 1] == 0)
      {
        led_target[i + 1] = 100;               // 點燃右側鄰近 LED，實現連鎖蔓延
      }
    }
  }

  // 3. 燃燒頂峰後的熄滅引導：當 LED 達到最亮 (100) 後，將目標降為 0，並加快目標遞減速度以加速熄滅
  for (int i = 0; i < LED_COUNT; i++)
  {
    if (led_brightness[i] >= 100 && led_target[i] == 100)
    {
      led_target[i] = 0;                       // 達到頂峰後設定目標為 0，引導進入熄滅階段
    }

    // 如果目標值大於 0 且已經在消散中，讓目標值快速遞減（每次減 5），使熄滅時間大幅縮短
    if (led_target[i] > 0 && led_target[i] < 100)
    {
      if (led_target[i] > 5) {
        led_target[i] -= 5;                    // 加快目標值下降速度，使燈光熄滅速度變快
      } else {
        led_target[i] = 0;
      }
    }
  }

  // 4. 5 秒碳化恢復計時邏輯
  static uint32_t carbonize_timer = 0;         // 記錄完全碳化開始的系統時間戳記
  static bool waiting_for_reset = false;       // 記錄目前是否正在進行 5 秒倒數計時

  // 檢查是否所有 LED 都已經碳化燃燒完畢
  bool all_carbonized = true;                  // 假設全部已經碳化
  for (int i = 0; i < LED_COUNT; i++)
  {
    if (!led_carbonized[i]) { all_carbonized = false; break; }  // 只要有一顆還沒碳化就設為 false
  }

  if (all_carbonized)                          // 如果全部燃燒完畢進入完全碳化狀態
  {
    if (!waiting_for_reset)                    // 若尚未開始計時
    {
      waiting_for_reset = true;                // 標記進入等待狀態
      carbonize_timer = HAL_GetTick();         // 取得當前系統時間作為 5 秒計時起點
    }
    else                                       // 若已經在等待中
    {
      if (HAL_GetTick() - carbonize_timer >= 5000) // 檢查是否已經經過了 5 秒 (5000 毫秒)
      {
        for (int i = 0; i < LED_COUNT; i++)
        {
          led_carbonized[i] = false;           // 5 秒冷卻時間到，解除碳化限制，恢復仙女棒狀態
        }
        waiting_for_reset = false;             // 重置等待旗標，準備迎接下一輪燃燒
      }
    }
  }
  else
  {
    waiting_for_reset = false;                 // 若尚未全數碳化，則重置計時等待狀態
  }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();                                  // 初始化 HAL 庫與系統滴答計時器，確保基礎時脈正常

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();                        // 設定系統時脈至 170 MHz

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();                              // 初始化所有 LED 控制的 GPIO 腳位
  MX_TIM6_Init();                              // 初始化 TIM6 定時器（用於軟體 PWM 脈波基準）
  MX_USART2_UART_Init();                       // 初始化 USART2 序列埠（用於接收外部指令）

  /* USER CODE BEGIN 2 */
  memset((void*)led_brightness, 0, sizeof(led_brightness)); // 將所有 LED 亮度歸零初始化
  memset((void*)led_target, 0, sizeof(led_target));         // 將所有目標亮度歸零初始化
  memset(led_carbonized, 0, sizeof(led_carbonized));        // 清除碳化標記，預設均未碳化

  HAL_TIM_Base_Start_IT(&htim6);                            // 啟動 TIM6 定時器中斷，開始高頻軟體 PWM 掃描
  HAL_UART_Receive_IT(&huart2, &rx_byte, 1);                // 啟動 UART2 接收中斷，準備接收使用者指令
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uint32_t current_tick = HAL_GetTick();     // 取得當前的系統時間戳記
    if (current_tick - anim_tick >= 20)        // 每隔 20 毫秒執行一次更新，確保動畫平順
    {
      anim_tick = current_tick;                // 更新計時基準點
      Sparkler_Update();                       // 呼叫仙女棒動態更新函式
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST); // 設定內部穩壓器為 Boost 模式以支援高效能

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI; // 使用內部高速振盪器 (HSI)
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;                  // 開啟 HSI
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT; // 預設校準值
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;              // 開啟 PLL 鎖相迴路
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;      // PLL 來源設為 HSI
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;               // PLL M 分頻係數
  RCC_OscInitStruct.PLL.PLLN = 85;                          // PLL N 倍頻係數
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;               // PLL P 分頻
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;               // PLL Q 分頻
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;               // PLL R 分頻
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();                                        // 若時脈設定失敗則進入錯誤處理
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; // 系統時脈來源設為 PLL
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;         // AHB 匯流排不分頻
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;          // APB1 匯流排不分頻
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;          // APB2 匯流排不分頻

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();                                        // 若匯流排設定失敗則進入錯誤處理
  }
}

/**
  * @brief TIM6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 170 - 1;              // 預分頻器設定：將 170MHz 主頻降頻為 1MHz (每 1us 計數一次)
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP; // 向上計數模式
  htim6.Init.Period = 100 - 1;                 // 自動重載值設定：計數 100 次觸發一次中斷 (100us，即 10kHz  PWM 頻率)
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE; // 停用自動重載預載入
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();                           // 初始化失敗則進錯誤處理
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();                           // 同步設定失敗則進錯誤處理
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;               // 波特率設為 115200
  huart2.Init.WordLength = UART_WORDLENGTH_8B; // 資料位元為 8 位元
  huart2.Init.StopBits = UART_STOPBITS_1;      // 停止位元為 1 位元
  huart2.Init.Parity = UART_PARITY_NONE;      // 無奇偶校驗
  huart2.Init.Mode = UART_MODE_TX_RX;         // 啟用收發雙向模式
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE; // 無硬體流控
  huart2.Init.OverSampling = UART_OVERSAMPLING_16; // 過取樣率 16 倍
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE; // 停用單位取樣
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1; // 時脈不分頻
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();                           // 初始化失敗則進錯誤處理
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();                // 啟用 GPIOC 時脈
  __HAL_RCC_GPIOF_CLK_ENABLE();                // 啟用 GPIOF 時脈
  __HAL_RCC_GPIOA_CLK_ENABLE();                // 啟用 GPIOA 時脈 (支援 PA0, PA1, PA4, PA5, PA6)
  __HAL_RCC_GPIOB_CLK_ENABLE();                // 啟用 GPIOB 時脈 (支援 PB0, PB1)

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6, GPIO_PIN_RESET); // 預設輸出低電位（LED 熄滅）

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET); // 預設輸出低電位（LED 熄滅）

  /*Configure GPIO pins : PA0 PA1 PA4 PA5 PA6 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 設定為推挽輸出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;          // 不上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; // 輸出速度設為低速
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);      // 初始化 GPIOA 腳位

  /*Configure GPIO pins : PB0 PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;  // 設定為推挽輸出模式
  GPIO_InitStruct.Pull = GPIO_NOPULL;          // 不上下拉
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; // 輸出速度設為低速
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);      // 初始化 GPIOB 腳位

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();                             // 關閉全域中斷，進入保護狀態
  while (1)
  {
    // 停留在無窮迴圈中等待排除
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
