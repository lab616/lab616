// 2009 lab616.com, All Rights Reserved.

package com.lab616.ib.api;

/**
 * Ticker symbol.  This class encapsulates the ticker symbol for stocks and
 * option contracts. This class also supports generation of a 32 bit unsigned
 * int that can be used as a unique ticker id identifying the underlying.
 * 
 * BINARY FORMAT from MSB to LSB.
 * For an unsigned 32 bit int, we support up to 4 letter symbol (e.g.
 * AAPL or those on NASDAQ, 2 bits delimiter, 1 expiration month code, and
 * a 1 char strike code, per standard option symbology.
 * Writing: start with first letter of symbol, write from LSB toward MSB.
 * Reading: start with LSB and read toward MSB and append to buffer.
 * 
 * [strike] [expiration] [delimiter] [symbol(N)][symbol(N-1)]...[symbol[0]]
 * where
 * [strike] = 1 char for the strike price, per standard option symbol rules
 * [expiration] = 1 char for the expiration month, per option symbol rules.
 * [delimiter] = 2 bytes. 0x3 indicates option code following. 0x0 for none.
 * [symbol(N)] = Nth char of the ticker symbol.
 * [symbol(0)] = First char of the ticker symbol.
 *
 * @author david
 */
public final class TickerId {

  
  static char[][] SPECIAL_CHARS = new char[][] {
    { 27, '/' },
    { 28, '.' },
    { 29, '$' },
  };
  
  /**
   * Max number of characters for the symbol.  Up to 4.
   */
  static int MAX_CHARS = 4;
  
  // 5 bits for 0-31 values.
  static int FRAME_SIZE = 5;  // 0-31 for 26 alphabets so 5 bits are required.

  // Maximum of 6 frames per 32 bit unsigned int.
  static int FRAMES = 6;
  
  // Mask for the delimiter.
  static int DELIMITER_MASK = 0x3 << 30;
  
  /**
   * Computes a unique id for the given symbol.
   * @param symbol The symbol.
   * @return The ticker id.
   */
  public static int toTickerId(String sym) {
    String symbol = sym.toUpperCase();
    int size = symbol.length();
    int tickerId = 0;
    for (int i = 0; i < size; i++) {
      char c = symbol.charAt(i);
      int v = c - 'A' + 1;
      if (v < 1 || v > 26) {
        for (int j = 0; j < SPECIAL_CHARS.length; j++) {
          if (c == SPECIAL_CHARS[j][1]) {
            v = SPECIAL_CHARS[j][0];
            break;
          }
        }
      }
      tickerId += Math.max(0, v) << (FRAME_SIZE * i);
    }
    return tickerId;
  }
  
  /**
   * From a ticker id generated by {@link #toTickerId(String)}, return the
   * string representation of the symbol.
   * @param tickerId The ticker id.
   * @return The symbol string.
   */
  public static String fromTickerId(int tickerId) {
    StringBuffer symbol = new StringBuffer();
    for (int i = 0; i < FRAMES; i++) {
      int mask = ((1 << FRAME_SIZE) - 1) << (FRAME_SIZE * i);
      int v = (tickerId & mask) >> (FRAME_SIZE * i);
      char c = ' ';
      if (v > 0 && v < 27) {
        c = (char)('A' + (v - 1));
        symbol.append(c);
      } else {
        for (int j = 0; j < SPECIAL_CHARS.length; j++) {
          if (v == SPECIAL_CHARS[j][0]) {
            c = SPECIAL_CHARS[j][1];
            symbol.append(c);
            break;
          }
        }
      }
    }
    return symbol.toString();
  }
}