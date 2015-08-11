/*
  usi_i2c.c

  Copyright (C) 2013 Jan Rychter
  
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <msp430g2412.h>
#include <stdint.h>
#include "usi_i2c.h"

// Internal state
static uint16_t *i2c_sequence;
static uint16_t i2c_sequence_length;
static uint8_t *i2c_receive_buffer;
static uint16_t i2c_wakeup_sr_bits;
i2c_state_type i2c_state = I2C_IDLE;

static inline void i2c_prepare_stop();
static inline void i2c_prepare_data_xmit_recv();

void i2c_send_sequence(uint16_t *sequence, uint16_t sequence_length, uint8_t *received_data, uint16_t wakeup_sr_bits) {
  while(i2c_state != I2C_IDLE); // we can't start another sequence until the current one is done
  i2c_sequence = sequence;
  i2c_sequence_length = sequence_length;
  i2c_receive_buffer = received_data;
  i2c_wakeup_sr_bits = wakeup_sr_bits;
  i2c_state = I2C_START;
  USICTL1 |= USIIFG;            // actually start communication
}

static inline void i2c_prepare_stop() {
  USICTL0 |= USIOE;             // SDA = output
  USISRL = 0x00;
  USICNT |=  0x01;              // Bit counter= 1, SCL high, SDA low
  i2c_state = I2C_STOP;
}

static inline void i2c_prepare_data_xmit_recv() {
  if(i2c_sequence_length == 0) {
    i2c_prepare_stop();         // nothing more to do, prepare to send STOP
  } else {
    if(*i2c_sequence == I2C_RESTART) {
      USICTL0 |= USIOE;         // SDA = output
      USISRL = 0xff;            // prepare and send a dummy bit, so that SDA is high
      USICNT = (USICNT & 0xE0) | 1;
      i2c_state = I2C_START;
    }
    else if(*i2c_sequence == I2C_READ) {
      USICTL0 &= ~USIOE;               // SDA = input
      USICNT = (USICNT & 0xE0) | 8;    // Bit counter = 8, RX data
      i2c_state = I2C_RECEIVED_DATA;   // next state: Test data and ACK/NACK
    } else {                           // a write
      // at this point we should have a pure data byte, not a command, so (*i2c_sequence >> 8) == 0
      USICTL0 |= USIOE;                // SDA = output
      USISRL = (char)(*i2c_sequence);  // Load data byte
      USICNT = (USICNT & 0xE0) | 8;    // Bit counter = 8, start TX
      i2c_state = I2C_PREPARE_ACKNACK; // next state: prepare to receive data ACK/NACK
    }
    i2c_sequence++;
    i2c_sequence_length--;
  }
}

#pragma vector = USI_VECTOR
__interrupt void USI_TXRX(void)
{
  switch(__even_in_range(i2c_state,12)) {
  case I2C_IDLE:
    break;

  case I2C_START:               // generate start condition
    USISRL = 0x00;
    USICTL0 |= (USIGE|USIOE);
    USICTL0 &= ~USIGE;
    i2c_prepare_data_xmit_recv();
    break;

  case I2C_PREPARE_ACKNACK:      // prepare to receive ACK/NACK
    USICTL0 &= ~USIOE;           // SDA = input
    USICNT |= 0x01;              // Bit counter=1, receive (N)Ack bit
    i2c_state = I2C_HANDLE_RXTX; // Go to next state: check ACK/NACK and continue xmitting/receiving if necessary
    break;

  case I2C_HANDLE_RXTX:         // Process Address Ack/Nack & handle data TX
    if((USISRL & BIT0) != 0) {  // did we get a NACK?
      i2c_prepare_stop();
    } else {
      i2c_prepare_data_xmit_recv();
    }
    break;

  case I2C_RECEIVED_DATA:       // received data, send ACK/NACK
    *i2c_receive_buffer = USISRL;
    i2c_receive_buffer++;
    USICTL0 |= USIOE;           // SDA = output
    if(i2c_sequence_length > 0) {
      // If this is not the last byte
      USISRL = 0x00;                // ACK
      i2c_state = I2C_HANDLE_RXTX;  // Go to next state: data/rcv again
    } else {                        // last byte: send NACK
      USISRL = 0xff;                // NACK
      i2c_state = I2C_PREPARE_STOP; // stop condition is next
    }
    USICNT |= 0x01;             // Bit counter = 1, send ACK/NACK bit
    break;

  case I2C_PREPARE_STOP:        // prepare stop condition
    i2c_prepare_stop();         // prepare stop, go to state 14 next
    break;

  case I2C_STOP:                // Generate Stop Condition
    USISRL = 0x0FF;             // USISRL = 1 to release SDA
    USICTL0 |= USIGE;           // Transparent latch enabled
    USICTL0 &= ~(USIGE+USIOE);  // Latch/SDA output disabled
    i2c_state = I2C_IDLE;       // Reset state machine for next xmt
    if(i2c_wakeup_sr_bits) {
      _bic_SR_register_on_exit(i2c_wakeup_sr_bits); // exit active if prompted to
    }
    break;
  }
  USICTL1 &= ~USIIFG;           // Clear pending flag
}

void i2c_init(uint16_t usi_clock_divider, uint16_t usi_clock_source) {
  _disable_interrupts();
  USICTL0 = USIPE6+USIPE7+USIMST+USISWRST;  // Port & USI mode setup
  USICTL1 = USII2C+USIIE;                   // Enable I2C mode & USI interrupt
  USICKCTL = usi_clock_divider + usi_clock_source + USICKPL;
  USICNT |= USIIFGCC;                       // Disable automatic clear control
  USICTL0 &= ~USISWRST;                     // Enable USI
  USICTL1 &= ~USIIFG;                       // Clear pending flag
  _enable_interrupts();
}
