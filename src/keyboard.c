#include <stdio.h>
#include <sys/ioctl.h>

#include "keyboard.h"

#if INTERFACE

enum key_codes {KEY_BACKSPACE = 0x1008, KEY_ESC, KEY_INS, KEY_DEL, KEY_LEFT, 
  KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_HOME, KEY_END, KEY_ENTER, KEY_TAB,
  KEY_PGUP, KEY_PGDN, KEY_F3, KEY_UNKNOWN};

#define ctrl(c) ((c) - 0x60)
#define shift(c) ((c) + 0x1000)

#endif

int linux_console = 0;

void get_modifier_keys(int *shift, int *ctrl) {
  *shift = *ctrl = 0;
  if (linux_console) {
    char modifiers = 6;
    if (ioctl(0, TIOCLINUX, &modifiers) >= 0) {
      if (modifiers & 1) *shift = 1;
      if (modifiers & 4) *ctrl = 1;
    }
  }
}

int add_modifiers(int key, int shift, int ctrl) {
  if (shift) key = shift(key);
  if (ctrl) key = ctrl(key);
  return key;
}

enum key_codes get_key() {
  int ch, shift, ctrl;

  ch = getchar();
  if (ch < 0) return ch;

  switch (ch) {
    case 0x08: return KEY_BACKSPACE;
    case 0x09:
      get_modifier_keys(&shift, &ctrl);
      return add_modifiers(KEY_TAB, shift, ctrl);
    case 0x0D: return KEY_ENTER;
    case 0x0A: return KEY_ENTER;
    case 0x1B:
      ch = getchar();
      switch (ch) {
        case 0x1B: return KEY_ESC;
        case 0x4F:
          ch = getchar();
          switch (ch) {
            case 0x46: return KEY_END;
            case 0x48: return KEY_HOME;
            case 0x52: return KEY_F3;
            default: return KEY_UNKNOWN;
          }
          break;

        case 0x5B:
          get_modifier_keys(&shift, &ctrl);
          ch = getchar();
          if (ch == 0x31) {
            ch = getchar();
            switch (ch) {
              case 0x3B:
                ch = getchar();
                if (ch == 0x32) shift = 1;
                if (ch == 0x35) ctrl = 1;
                if (ch == 0x36) shift = ctrl = 1;
                ch = getchar();
                break;
              case 0x7E:
                return add_modifiers(KEY_HOME, shift, ctrl);
              default:
                return KEY_UNKNOWN;
            }
          }

          switch (ch) {
            case 0x31: 
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              return add_modifiers(KEY_HOME, shift, ctrl);
            case 0x32: return getchar() == 0x7E ? KEY_INS : KEY_UNKNOWN;
            case 0x33: return getchar() == 0x7E ? KEY_DEL : KEY_UNKNOWN;
            case 0x34:
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              return add_modifiers(KEY_END, shift, ctrl);
            case 0x35:
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              return add_modifiers(KEY_PGUP, shift, 0);
            case 0x36:
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              return add_modifiers(KEY_PGDN, shift, 0);
            case 0x41: 
              return add_modifiers(KEY_UP, shift, ctrl);
            case 0x42: 
              return add_modifiers(KEY_DOWN, shift, ctrl);
            case 0x43: 
              return add_modifiers(KEY_RIGHT, shift, ctrl);
            case 0x44:
              return add_modifiers(KEY_LEFT, shift, ctrl);
            case 0x46:
              return add_modifiers(KEY_END, shift, ctrl);
            case 0x48:
              return add_modifiers(KEY_HOME, shift, ctrl);
            case 0x5A: 
              return shift(KEY_TAB);
            case 0x5B:
              ch = getchar();
              switch (ch) {
                case 0x43: return KEY_F3;
              }
              return KEY_UNKNOWN;

            default: return KEY_UNKNOWN;
          }
          break;

        default: return KEY_UNKNOWN;
      }
      break;

    case 0x00:
    case 0xE0:
      ch = getchar();
      switch (ch) {
        case 0x0F: return shift(KEY_TAB);
        case 0x3D: return KEY_F3;
        case 0x47: return KEY_HOME;
        case 0x48: return KEY_UP;
        case 0x49: return KEY_PGUP;
        case 0x4B: return KEY_LEFT;
        case 0x4D: return KEY_RIGHT;
        case 0x4F: return KEY_END;
        case 0x50: return KEY_DOWN;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INS;
        case 0x53: return KEY_DEL;
        case 0x73: return ctrl(KEY_LEFT);
        case 0x74: return ctrl(KEY_RIGHT);
        case 0x75: return ctrl(KEY_END);
        case 0x77: return ctrl(KEY_HOME);
        case 0x8D: return ctrl(KEY_UP);
        case 0x91: return ctrl(KEY_DOWN);
        case 0x94: return ctrl(KEY_TAB);
        case 0xB8: return shift(KEY_UP);
        case 0xB7: return shift(KEY_HOME);
        case 0xBF: return shift(KEY_END);
        case 0xB9: return shift(KEY_PGUP);
        case 0xBB: return shift(KEY_LEFT);
        case 0xBD: return shift(KEY_RIGHT);
        case 0xC0: return shift(KEY_DOWN);
        case 0xC1: return shift(KEY_PGDN);
        case 0xDB: return shift(ctrl(KEY_LEFT));
        case 0xDD: return shift(ctrl(KEY_RIGHT));
        case 0xD8: return shift(ctrl(KEY_UP));
        case 0xE0: return shift(ctrl(KEY_DOWN));
        case 0xD7: return shift(ctrl(KEY_HOME));
        case 0xDF: return shift(ctrl(KEY_END));

        default: return KEY_UNKNOWN;
      }
      break;

    case 0x7F: return KEY_BACKSPACE;

    default:
      if ((ch & 0b10000000) == 0b00000000) { return ch; }
      if ((ch & 0b11100000) == 0b11000000) { 
        return ((int)ch << 8) + getchar();
        //return (int)(ch & 0b00011111) << 6 | ((int)getchar() & 0b00111111);
      }
      return 0;
  }
}
