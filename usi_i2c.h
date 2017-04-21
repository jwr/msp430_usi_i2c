/*
  usi_i2c.h

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

#ifndef USI_I2C_H
#define USI_I2C_H

#include <stdint.h>

#define I2C_RESTART 	1<<8    /* repeated start */
#define I2C_READ		2<<8    /* read a byte */

// Call this with one of the USIDIV_* constants as a usi_clock_divider parameter, which will set the clock divider used
// for USI I2C communications. The usi_clock_source parameter should be set to one of the USISSEL* constants. Example:
// i2c_init(USIDIV_5, USISSEL_2) uses SMCLK/16.
void i2c_init(uint16_t usi_clock_divider, uint16_t usi_clock_source);

// Sends a command/data sequence that can include restarts, writes and reads. Every transmission begins with a START,
// and ends with a STOP so you do not have to specify that. Will busy-spin if another transmission is in progress. Note
// that this is interrupt-driven asynchronous code: you can't just call i2c_send_sequence from an interrupt handler and
// expect it to work: you risk a deadlock if another transaction is in progress and nothing will happen before the
// interrupt handler is done anyway. So the proper way to use this is in normal code. This should not be a problem, as
// performing such lengthy tasks as I2C communication inside an interrupt handler is a bad idea anyway.  wakeup_sr_bits
// should be a bit mask of bits to clear in the SR register when the transmission is completed (to exit LPM0: LPM0_BITS
// (CPUOFF), for LPM3: LPM3_bits (SCG1+SCG0+CPUOFF))
void i2c_send_sequence(uint16_t const * sequence, uint16_t sequence_length, uint8_t *received_data, uint16_t wakeup_sr_bits);

typedef enum i2c_state_enum {
  I2C_IDLE = 0,
  I2C_START = 2,
  I2C_PREPARE_ACKNACK = 4,
  I2C_HANDLE_RXTX = 6,
  I2C_RECEIVED_DATA = 8,
  I2C_PREPARE_STOP = 10,
  I2C_STOP = 12} i2c_state_type;

extern i2c_state_type i2c_state;

// Use this to check whether a previously scheduled I2C sequence has been fully processed.
inline unsigned int i2c_done() {
  return(i2c_state == I2C_IDLE);
}

#endif
