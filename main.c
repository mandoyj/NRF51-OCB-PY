/*
 * main.c
 *
 *  Created on: Mar 2, 2015
 *      Author: lauril
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "boards.h"
#include <stdio.h>
#include "nrf.h"
#include "nrf_temp.h"
#include "app_uart.h"
#include "app_error.h"
#include "app_gpiote.h"
#include "bsp.h"
#include "nrf_ecb.h"
#include "ocb.h"

#define UART_TX_BUF_SIZE 256                                                          /**< UART TX buffer size. */
#define UART_RX_BUF_SIZE 1
#define AES_ECB_BUFF_SIZE 16

const uint8_t leds_list[LEDS_NUMBER] = LEDS_LIST;
uint8_t keyArray[16] ={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

void uart_error_handle(app_uart_evt_t * p_event)
{
    if (p_event->evt_type == APP_UART_COMMUNICATION_ERROR)
    {
        APP_ERROR_HANDLER(p_event->data.error_communication);
    }
    else if (p_event->evt_type == APP_UART_FIFO_ERROR)
    {
        APP_ERROR_HANDLER(p_event->data.error_code);
    }
}

void print_hex_memory(void *mem) {
  int i;
  uint8_t *p = (uint8_t *)mem;
  for (i=0;i<AES_ECB_BUFF_SIZE;i++) {
    printf("0x%02x ", p[i]);
    if (i%16==0)
      printf("\n");
  }
  printf("\n");
}


int main(void)
{

    int32_t volatile temp;
    uint32_t err_code;
    // Configure LED-pins as outputs.
    LEDS_CONFIGURE(LEDS_MASK);
    nrf_temp_init();


    APP_GPIOTE_INIT(1);
    const app_uart_comm_params_t comm_params =
     {
         RX_PIN_NUMBER,
         TX_PIN_NUMBER,
         RTS_PIN_NUMBER,
         CTS_PIN_NUMBER,
         APP_UART_FLOW_CONTROL_ENABLED,
         false,
         UART_BAUDRATE_BAUDRATE_Baud38400
     };

    APP_UART_FIFO_INIT(&comm_params,
                    UART_RX_BUF_SIZE,
                    UART_TX_BUF_SIZE,
                    uart_error_handle,
                    APP_IRQ_PRIORITY_LOW,
                    err_code);

    APP_ERROR_CHECK(err_code);

    uint8_t *aesKey = keyArray;
    ocb_init(aesKey);
//    uint8_t src[AES_ECB_BUFF_SIZE]={1};
//    uint8_t dst[AES_ECB_BUFF_SIZE]={0};
//    nrf_ecb_set_key(aesKey);
    while (true)
    {
        for (int i = 0; i < LEDS_NUMBER; i++)
        {
            LEDS_INVERT(1 << leds_list[i]);

        }
        NRF_TEMP->TASKS_START = 1; /** Start the temperature measurement. */

        /* Busy wait while temperature measurement is not finished, you can skip waiting if you enable interrupt for DATARDY event and read the result in the interrupt. */
        /*lint -e{845} // A zero has been given as right argument to operator '|'" */
        while (NRF_TEMP->EVENTS_DATARDY == 0)
        {
            // Do nothing.
        }
        NRF_TEMP->EVENTS_DATARDY = 0;

        /**@note Workaround for PAN_028 rev2.0A anomaly 29 - TEMP: Stop task clears the TEMP register. */
        temp = (nrf_temp_read() / 4);

        /**@note Workaround for PAN_028 rev2.0A anomaly 30 - TEMP: Temp module analog front end does not power down when DATARDY event occurs. */
        NRF_TEMP->TASKS_STOP = 1; /** Stop the temperature measurement. */

        printf("Actual temperature: %d\n\r", (int)temp);
        uint8_t zeroes[128] = {0,};
        uint8_t nonce[12] = {1,};
        uint8_t p[128] = {0,};
        uint8_t final[16];
        uint8_t *c;
        unsigned i, next;
        int result;

        /* Encrypt and output RFC vector */
        c = (uint8_t *) malloc((uint8_t )20*64);
        next = 0;
        for (i=0; i<128; i++) {
            nonce[11] = i;
            ocb_encrypt(c+next, zeroes, nonce, zeroes, i, zeroes, i);
            next = next + i + TAGBYTES;
            ocb_encrypt(c+next, zeroes, nonce, zeroes, 0, zeroes, i);
            next = next + i + TAGBYTES;
            ocb_encrypt(c+next, zeroes, nonce, zeroes, i, zeroes, 0);
            next = next + TAGBYTES;
        }
        nonce[11] = 0;
        ocb_encrypt(final, zeroes, nonce, c, next, zeroes, 0);
        if (NONCEBYTES == 12) {
            printf("AEAD_AES_%d_OCB_TAGLEN %d Output: ", KEYBYTES*8, TAGBYTES*8);
            for (i=0; i<TAGBYTES; i++) printf("%02X", final[i]); printf("\n");
        }

        /* Decrypt and test for all zeros and authenticity */
        result = ocb_decrypt(p, zeroes, nonce, c, next, final, TAGBYTES);
        if (result)  printf("FAIL\n");
        next = 0;
        for (i=0; i<128; i++) {
            nonce[11] = i;
            result = ocb_decrypt(p, zeroes, nonce, zeroes, i, c+next, i+TAGBYTES);
            if (result || memcmp(p,zeroes,i)) { printf("FAIL\n"); return 0; }
            next = next + i + TAGBYTES;
            result = ocb_decrypt(p, zeroes, nonce, zeroes, 0, c+next, i+TAGBYTES);
            if (result || memcmp(p,zeroes,i)) { printf("FAIL\n"); return 0; }
            next = next + i + TAGBYTES;
            result = ocb_decrypt(p, zeroes, nonce, zeroes, i, c+next, TAGBYTES);
            if (result || memcmp(p,zeroes,i)) { printf("FAIL\n"); return 0; }
            next = next + TAGBYTES;
        }
        nrf_delay_ms(2000);

    }
}

