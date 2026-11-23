#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

// Line-based debug console on the programming cable (38400 8N1).
// Numbers are hex unless noted. Commands (one per line):
//
//   r RR          read BK4819 register        -> "R RR VVVV"
//   w RR VVVV     write BK4819 register       -> "W RR VVVV"
//   d N           read N words from the FSK FIFO (reg 5F) now -> "D wwww ..."
//   p N           words to read automatically on each FIFO-almost-full
//                 interrupt (default 4, 0 = don't read)            -> "P N"
//   t FREQ        tune; FREQ is DECIMAL in 10 Hz units             -> "T FREQ"
//   y             keypad diagnostics: GPIOA DIR, raw input per scan row, key, I2C bus recoveries
//   v             version                                          -> "V ..."
//   c             beacon configuration (64-byte BeaconConfig)      -> "C HEX" (or "C -" if unset)
//   c HEX         validate and store a new beacon configuration    -> "C HEX"
//   u             beacon type + status text (72-byte BeaconStatus)    -> "U HEX"
//   u HEX         validate and store a new one                        -> "U HEX"
//   o             station options (16-byte StationConfig, station.h)   -> "O HEX"
//   o HEX         validate, store and apply new station options       -> "O HEX"
//   k [FREQ]      switch the serial line to KISS (see kiss.h), optionally
//                 retuning first (DECIMAL, 10 Hz units)  -> "K FREQ", then KISS.
//                 A complete, valid KISS frame (see kiss.h) also switches to KISS.
//
// With ENABLE_TX (see tx.h for the safety limits):
//   x HEX...      transmit one AX.25 frame (addresses..info, no FCS) as
//                 Bell 202 AFSK on the current frequency  -> "X <on-air ms>"
//   g NN          AFSK tone level, REG_70 tone-1 gain 00..7F (deviation) -> "G NN"
//   b NN          PA bias; capped at the calibrated low-power value     -> "B NN CC" (CC = cap)
//   f NN          preamble flags before the frame (TXDELAY, 6.67 ms each) -> "F NN"
//   s             last transmission: "S <count> <result> <on-air ms>" (decimal;
//                 result 0 = sent). Lets the host confirm a frame after reopening
//                 the port, since RF during TX can wedge the cable's receive side.
//
// Asynchronous events:
//   I SSSS        BK4819 interrupt status (reg 02) for every interrupt
//   D wwww ...    FIFO words read automatically after FIFO-almost-full
//
// Errors are reported as "E <reason>".

void CONSOLE_Poll(void);                      // call from the main loop
void CONSOLE_OnRadioInterrupt(uint16_t status);

#endif
