/**
  ******************************************************************************
  * File Name          : main.c
  * Description        : Main program body
  ******************************************************************************
  *
  * COPYRIGHT(c) 2016 STMicroelectronics
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "stm32f0xx_hal.h"
#include "usb_device.h"

/* USER CODE BEGIN Includes */
#include "usbd_hid.h"
#include "eeprom.h"
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim14;

ADC_HandleTypeDef hadc;
DMA_HandleTypeDef hdma_adc;
SPI_HandleTypeDef hspi1;

#define X_AXIS_LOW_TH	1400
#define X_AXIS_HIGH_TH	2500
#define Y_AXIS_LOW_TH	500
#define Y_AXIS_HIGH_TH	2600

#define X_AXIS			(axis[0] & 0xffffu)
#define Y_AXIS			(axis[1] & 0xffffu)
#define T_AXIS			(axis[2] & 0xffffu)
#define B_AXIS			(axis[3] & 0xffffu)
#define C_AXIS			(axis[4] & 0xffffu)

__ALIGN_BEGIN static volatile uint32_t axis[5] __ALIGN_END = { 0, 0, 0, 0, 0 };    // Y, X, T, B, C
__ALIGN_BEGIN static uint8_t rx_buffer[2] __ALIGN_END;

/* Virtual address defined by the user: 0xFFFF value is prohibited */
#define X_AXIS_LOW_VADDR	0x1001
#define X_AXIS_HIGH_VADDR	0x1002
#define Y_AXIS_LOW_VADDR	0x1003
#define Y_AXIS_HIGH_VADDR	0x1004

__ALIGN_BEGIN uint16_t VirtAddVarTab[NB_OF_VAR] __ALIGN_END =
{
    X_AXIS_LOW_VADDR, X_AXIS_HIGH_VADDR, Y_AXIS_LOW_VADDR, Y_AXIS_HIGH_VADDR
};

struct __packed
{
    uint8_t		id;			// 0x01
    uint8_t		buttons[3];
    uint16_t	axis[5];
} report = { 1, { 0, 0, 0 }, { 0, 0, 0, 0, 0 } };

unsigned short x_low_th  = X_AXIS_LOW_TH;
unsigned short y_low_th  = Y_AXIS_LOW_TH;
unsigned short x_high_th = X_AXIS_HIGH_TH;
unsigned short y_high_th = Y_AXIS_HIGH_TH;

unsigned short report2send = 0;
static unsigned short x_axis = 2048;
static unsigned short y_axis = 2048;

static volatile uint32_t u100ticks = 0;

void SystemClock_Config(void);
void Error_Handler(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ADC_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM14_Init(void);

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef * hadc);

int main(void)
{
    /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
    HAL_Init();

    /* Configure the system clock */
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC_Init();
    MX_SPI1_Init();
    MX_USB_DEVICE_Init();
    MX_TIM14_Init();

    HAL_FLASH_Unlock();
    EE_Init();

    EE_ReadVariable(VirtAddVarTab[0], &x_low_th);
    EE_ReadVariable(VirtAddVarTab[1], &x_high_th);
    EE_ReadVariable(VirtAddVarTab[2], &y_low_th);
    EE_ReadVariable(VirtAddVarTab[3], &y_high_th);

    HAL_FLASH_Lock();

    HAL_ADC_Start_DMA(&hadc, (uint32_t*)axis, 5);

    while (1)
    {
        HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_SET);

        HAL_TIM_Base_Start_IT(&htim14);
        while (!u100ticks) /* do nothing for 100 us */;
        HAL_TIM_Base_Stop_IT(&htim14);
        u100ticks = 0;

        HAL_StatusTypeDef status =
            HAL_SPI_Receive(&hspi1, rx_buffer, sizeof(rx_buffer), 3000);

        switch(status)
        {
        case HAL_OK:
            report.buttons[0] = 0x00;
            report.buttons[1] = 0x00;
            report.buttons[2] = 0x00;

            if ((rx_buffer[0] & 0xff) != 0xff)   // if all bits of rx_buffer[0] is 1 assume shifter is disconnected
            {
                if (rx_buffer[0] & 4)   report.buttons[0] |= 1;         else report.buttons[0] &= ~1;
                if (rx_buffer[0] & 1)   report.buttons[0] |= (1 << 1);  else report.buttons[0] &= ~(1 << 1);
                if (rx_buffer[0] & 2)   report.buttons[0] |= (1 << 2);  else report.buttons[0] &= ~(1 << 2);
                if (rx_buffer[0] & 8)   report.buttons[0] |= (1 << 3);  else report.buttons[0] &= ~(1 << 3);

                if (rx_buffer[1] & 1)   report.buttons[2] |= 1;         else report.buttons[2] &= ~1;
                if (rx_buffer[1] & 2)   report.buttons[2] |= (1 << 1);  else report.buttons[2] &= ~(1 << 1);
                if (rx_buffer[1] & 4)   report.buttons[2] |= (1 << 2);  else report.buttons[2] &= ~(1 << 2);
                if (rx_buffer[1] & 8)   report.buttons[2] |= (1 << 3);  else report.buttons[2] &= ~(1 << 3);

                if (rx_buffer[1] & 32)  report.buttons[0] |= (1 << 4);  else report.buttons[0] &= ~(1 << 4);
                if (rx_buffer[1] & 128) report.buttons[0] |= (1 << 5);  else report.buttons[0] &= ~(1 << 5);
                if (rx_buffer[1] & 64)  report.buttons[0] |= (1 << 6);  else report.buttons[0] &= ~(1 << 6);
                if (rx_buffer[1] & 16)  report.buttons[0] |= (1 << 7);  else report.buttons[0] &= ~(1 << 7);
            }
            break;

        case HAL_TIMEOUT:
        case HAL_BUSY:
        case HAL_ERROR:
            Error_Handler();
        default:
            report.buttons[0] = 0xff;
            break;
        }

        HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_RESET);
        report.id = 0x01;

        x_axis = ((X_AXIS << 2) + x_axis * 96) / 100;
        y_axis = ((Y_AXIS << 2) + y_axis * 96) / 100;

        if (rx_buffer[0] & 16)
        {
            if (y_axis < y_low_th)   // stick towards player
            {
                report.buttons[1] = 128;
                report.buttons[2] &= ~(1 << 4);
            }
            else if (y_axis > y_high_th)   // stick opposite to player
            {
                report.buttons[1] = 0; // neutral
                report.buttons[2] |= (1 << 4);
            }
            else
            {
                report.buttons[1] = 0; // neutral
                report.buttons[2] &= ~(1 << 4);
            }
        }
        else
        {
            report.buttons[1] &= ~(1 << 7);
            report.buttons[2] &= ~(1 << 4);

            if (y_axis < y_low_th)   // stick towards player
            {
                if (x_axis < x_low_th)
                {
                    if (!report.buttons[1]) report.buttons[1] = 2; // 2nd gear
                }
                else if (!report.buttons[1])
                {
                    report.buttons[1] = (x_axis > x_high_th) ? ((rx_buffer[0] & 64) ? 64 : 32) : 8;
                }
            }
            else
            {
                if (y_axis > y_high_th)   // stick opposite to player
                {
                    if (x_axis < x_low_th)
                    {
                        if (!report.buttons[1]) report.buttons[1] = 1; // 1st gear
                    }
                    else if (!report.buttons[1])
                    {
                        report.buttons[1] = (x_axis > x_high_th) ? 16 : 4;
                    }
                }
                else
                {
                    report.buttons[1] = 0; // neutral
                }
            }
        }

        report.axis[0] = x_axis;
        report.axis[1] = y_axis;

        if (report2send == 2)
        {
            HAL_FLASH_Unlock();

            EE_WriteVariable(VirtAddVarTab[0], x_low_th);
            EE_WriteVariable(VirtAddVarTab[1], x_high_th);
            EE_WriteVariable(VirtAddVarTab[2], y_low_th);
            EE_WriteVariable(VirtAddVarTab[3], y_high_th);

            HAL_FLASH_Lock();
            report2send = 0;
        }

        if (hUsbDeviceFS.pClassData
                && ((USBD_HID_HandleTypeDef *)hUsbDeviceFS.pClassData)->state == HID_IDLE)
        {
            USBD_HID_SendReport(&hUsbDeviceFS, (uint8_t *)&report, sizeof(report));
        }
    }
}

/** System Clock Configuration
*/
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct;
    RCC_ClkInitTypeDef RCC_ClkInitStruct;
    RCC_PeriphCLKInitTypeDef PeriphClkInit;

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI14|RCC_OSCILLATORTYPE_HSI48;
    RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;

    RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
    RCC_OscInitStruct.HSI14CalibrationValue = RCC_HSI14CALIBRATION_DEFAULT;

    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                  |RCC_CLOCKTYPE_PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI48;

    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
    {
        Error_Handler();
    }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
    PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq()/1000);
    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK);

    /* SysTick_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(SysTick_IRQn, 0, 0);
}

/* TIM14 init function */
static void MX_TIM14_Init(void)
{

    htim14.Instance = TIM14;
    htim14.Init.Prescaler = 48;
    htim14.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim14.Init.Period = 100; /* generate IRQ 48 MHz / 48 / 100 = 10,000 times per second */
    htim14.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_Base_Init(&htim14) != HAL_OK)
    {
        Error_Handler();
    }

}

/* ADC init function */
static void MX_ADC_Init(void)
{

    ADC_ChannelConfTypeDef sConfig;

    /**Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
    */
    hadc.Instance = ADC1;
    hadc.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
    hadc.Init.Resolution = ADC_RESOLUTION_12B;
    hadc.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
    hadc.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    hadc.Init.LowPowerAutoWait = DISABLE;
    hadc.Init.LowPowerAutoPowerOff = DISABLE;
    hadc.Init.ContinuousConvMode = ENABLE;
    hadc.Init.DiscontinuousConvMode = DISABLE;
    hadc.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc.Init.SamplingTimeCommon = ADC_SAMPLETIME_239CYCLES_5;
    hadc.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc.Init.DMAContinuousRequests = ENABLE;
    hadc.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    if (HAL_ADC_Init(&hadc) != HAL_OK)
    {
        Error_Handler();
    }

    /**Configure for the selected ADC regular channel to be converted.
    */
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /**Configure for the selected ADC regular channel to be converted.
    */
    sConfig.Channel = ADC_CHANNEL_1;
    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /**Configure for the selected ADC regular channel to be converted.
    */
    sConfig.Channel = ADC_CHANNEL_2;
    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /**Configure for the selected ADC regular channel to be converted.
    */
    sConfig.Channel = ADC_CHANNEL_3;
    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /**Configure for the selected ADC regular channel to be converted.
    */
    sConfig.Channel = ADC_CHANNEL_4;
    if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }

}

// ADC DMA interrupt handler
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{

    report.axis[2] = ((T_AXIS << 1) + report.axis[2] * 98) / 100;
    report.axis[3] = ((B_AXIS << 1) + report.axis[3] * 98) / 100;
    report.axis[4] = ((C_AXIS << 1) + report.axis[4] * 98) / 100;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM14)
    {
        u100ticks++;
    }
}

/* SPI1 init function */
static void MX_SPI1_Init(void)
{

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{
    /* DMA controller clock enable */
    __HAL_RCC_DMA1_CLK_ENABLE();

    /* DMA interrupt init */
    /* DMA1_Channel1_IRQn interrupt configuration */
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);

}

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
static void MX_GPIO_Init(void)
{

    GPIO_InitTypeDef GPIO_InitStruct;

    /* GPIO Ports Clock Enable */
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(SPI1_nCS_GPIO_Port, SPI1_nCS_Pin, GPIO_PIN_RESET);

    /*Configure GPIO pin : SPI1_nCS_Pin */
    GPIO_InitStruct.Pin = SPI1_nCS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SPI1_nCS_GPIO_Port, &GPIO_InitStruct);

    /*Configure GPIO pin Output Level */
    GPIOF->BSRR = ((uint32_t) SHIFTER_SEL2_Pin << 16) | SHIFTER_SEL1_Pin;

    /*Configure GPIO pin : SPI1_nCS_Pin */
    GPIO_InitStruct.Pin = SHIFTER_SEL1_Pin | SHIFTER_SEL2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @param  None
  * @retval None
  */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler */
    /* User can add his own implementation to report the HAL error return state */
    while(1)
    {
    }
    /* USER CODE END Error_Handler */
}

#ifdef USE_FULL_ASSERT

/**
   * @brief Reports the name of the source file and the source line number
   * where the assert_param error has occurred.
   * @param file: pointer to the source file name
   * @param line: assert_param error line source number
   * @retval None
   */
void assert_failed(uint8_t* file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
      ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
    /* USER CODE END 6 */

}

#endif

/**
  * @}
  */

/**
  * @}
*/

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
