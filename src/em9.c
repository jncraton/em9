#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <termios.h>
#define O_BINARY 0
int linux_console = 0;

#define MAXSIZE      32768
#define LINEBUF      512

#define TABSIZE        2
#define PAGESIZE       20
#define INDENT         "  "

#define CLRSCR         "\033[0J"
#define CLREOL         "\033[K"
#define GOTO_LINE_COL  "\033[%d;%dH"
#define RESET_COLOR    "\033[0m"

#define TEXT_COLOR     "\033[0m"
#define SELECT_COLOR   "\033[7m\033[1m"
#define STATUS_COLOR   "\033[1m\033[7m"

enum key_codes {KEY_BACKSPACE = 0x1008, KEY_ESC, KEY_INS, KEY_DEL, KEY_LEFT, 
  KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_HOME, KEY_END, KEY_ENTER, KEY_TAB,
  KEY_PGUP, KEY_PGDN, KEY_F3, KEY_UNKNOWN};

#define ctrl(c) ((c) - 0x60)
#define shift(c) ((c) + 0x1000)

struct editor {
  int clipsize;

  int cols;          // Console columns
  int lines;         // Console lines

  int toppos;        // Text position for current top screen line
  int topline;       // Line number for top of screen
  int margin;        // Position for leftmost column on screen

  int linepos;       // Text position for current line
  int line;          // Current document line
  int col;           // Current document column
  int lastcol;       // Remembered column from last horizontal navigation
  int anchor;        // Anchor position for selection
  
  int refresh;       // Flag to trigger screen redraw
  int lineupdate;    // Flag to trigger redraw of current line
  int dirty;         // Dirty flag is set when the editor buffer has been changed

  char filename[FILENAME_MAX];

  char linebuf[LINEBUF];  

  char content[MAXSIZE];
  char tmpbuf[MAXSIZE];
  char clipboard[MAXSIZE];
};

int load_file(struct editor *ed, char *filename) {
  int f;

  if (!realpath(filename, ed->filename)) return -1;
  f = open(ed->filename, O_RDONLY | O_BINARY);
  if (f < 0) return -1;

  int res = read(f, ed->content, MAXSIZE);
  
  if (res == MAXSIZE || res < 0) {
    close(f);
    return -1;
  }

  ed->anchor = -1;

  close(f);
  return 0;
}

int save_file(struct editor *ed) {
  int f;

  f = open(ed->filename, O_TRUNC | O_WRONLY);
  if (f < 0) return -1;

  if (write(f, ed->content, strlen(ed->content)) != (int) strlen(ed->content)) goto err;

  close(f);
  ed->dirty = 0;
  return 0;

err:
  close(f);
  return -1;
}

void insert(struct editor *ed, int pos, char *buf, int bufsize) {
  // Slide the following text over
  memmove(ed->content + pos + bufsize, ed->content + pos, strlen(ed->content + pos)+1);
  // Overwrite the gap with new text
  memcpy(ed->content + pos, buf, bufsize);
  ed->dirty=1;
}

void erase(struct editor *ed, int pos, int len) {
  memmove(ed->content + pos, ed->content + pos + len, strlen(ed->content + pos + len) + 1);
  ed->dirty=1;
}

void replace(struct editor *ed, int pos, int len, char *buf, int bufsize) {
  erase(ed, pos, len);
  insert(ed, pos, buf, bufsize);
}

char get(struct editor *ed, int pos) {
  if (pos >= MAXSIZE) return 0;
  return ed->content[pos];
}

//
// Navigation functions
//

int line_length(struct editor *ed, int linepos) {
	int pos;
	int len = 0;
  for (pos = linepos;;pos++) {
    int ch = get(ed, pos);
    if (!(ch & 0b10000000)) { len++; } // Increment once per code point
    if (ch == 0 || ch == '\n' || ch == '\r') return len-1;
  }
}

int line_bytes(struct editor *ed, int linepos) {
	int pos;
  for (pos = linepos;;pos++) {
    char ch = get(ed, pos);
    if (ch == 0 || ch == '\n' || ch == '\r') return pos - linepos;
  }
}

int line_start(struct editor *ed, int pos) {
  for (; pos > 0 && get(ed, pos - 1) != '\n'; pos--);
  return pos;
}

int next_line(struct editor *ed, int pos, int dir) {
  pos = line_start(ed, pos);
	
  if (dir > 0) pos += line_bytes(ed, pos);
  
  pos += dir;

  if (pos < 0) return -1;
  char ch = get(ed, pos);
  if (!ch) return -1;
  
  return line_start(ed, pos);
}

int column(struct editor *ed, int linepos, int col) {
  char *p = ed->content + linepos;
  int c = 0;
  while (col > 0) {
    if (p == ed->content + MAXSIZE) break;
    if (*p == '\t') {
      int spaces = TABSIZE - c % TABSIZE;
      c += spaces;
    } else {
      c++;
    }
    col--;
  }
  return c;
}

void moveto(struct editor *ed, int pos, int center) {
  int scroll = 0;
  for (;;) {
    int cur = ed->linepos + ed->col;
    if (pos < cur) {
      if (pos >= ed->linepos) {
        ed->col = pos - ed->linepos;
      } else {
        ed->col = 0;
        ed->linepos = next_line(ed, ed->linepos, -1);
        ed->line--;

        if (ed->topline > ed->line) {
          ed->toppos = ed->linepos;
          ed->topline--;
          ed->refresh = 1;
          scroll = 1;
        }
      }
    } else if (pos > cur) {
      int next = next_line(ed, ed->linepos, 1);
      if (next == -1) {
        ed->col = line_length(ed, ed->linepos);
        break;
      } else if (pos < next) {
        ed->col = pos - ed->linepos;
      } else {
        ed->col = 0;
        ed->linepos = next;
        ed->line++;

        if (ed->line >= ed->topline + ed->lines) {
          ed->toppos = next_line(ed, ed->toppos, 1);
          ed->topline++;
          ed->refresh = 1;
          scroll = 1;
        }
      }
    } else {
      break;
    }
  }

  if (scroll && center) {
    int tl = ed->line - ed->lines / 2;
    if (tl < 0) tl = 0;
    for (;;) {
      if (ed->topline > tl) {
        ed->toppos = next_line(ed, ed->toppos, -1);
        ed->topline--;
      } else if (ed->topline < tl) {
        ed->toppos = next_line(ed, ed->toppos, 1);
        ed->topline++;
      } else {
        break;
      }
    }
  }
}

//
// Text selection
//

int get_selection(struct editor *ed, int *start, int *end) {
  if (ed->anchor == -1) {
    *start = *end = -1;
    return 0;
  } else {
    int pos = ed->linepos + ed->col;
    if (pos == ed->anchor) {
      *start = *end = -1;
      return 0;
    } else if (pos < ed->anchor) {
      *start = pos;
      *end = ed->anchor;
    } else {
      *start = ed->anchor;
      *end = pos;
    }
  }
  return 1;
}

void get_selection_or_line(struct editor *ed, int *start, int *end) {
  if (!get_selection(ed, start, end)) {
    *start = ed->linepos;
    *end = next_line(ed, ed->linepos, 1);
  }
}

int get_selected_text(struct editor *ed, char *buffer, int size) {
  int selstart, selend, len;

  if (!get_selection(ed, &selstart, &selend)) return 0;
  len = selend - selstart;
  if (len >= size) return 0;
  strncpy(buffer, ed->content + selstart, len);
  buffer[len] = 0;
  return len;
}

void update_selection(struct editor *ed, int select) {
  if (select) {
    if (ed->anchor == -1) ed->anchor = ed->linepos + ed->col;
    ed->refresh = 1;
  } else {
    if (ed->anchor != -1) ed->refresh = 1;
    ed->anchor = -1;
  }
}

int erase_selection(struct editor *ed) {
  int selstart, selend;
  
  if (!get_selection(ed, &selstart, &selend)) return 0;
  moveto(ed, selstart, 0);
  erase(ed, selstart, selend - selstart);
  ed->anchor = -1;
  ed->refresh = 1;
  return 1;
}

void erase_selection_or_line(struct editor *ed) {
  if (!erase_selection(ed)) {
    moveto(ed, ed->linepos, 0);
    erase(ed, ed->linepos, next_line(ed, ed->linepos, 1) - ed->linepos);
    ed->anchor = -1;
    ed->refresh = 1;
  }
}

void select_all(struct editor *ed) {
  ed->anchor = 0;
  ed->refresh = 1;
  moveto(ed, strlen(ed->content), 0);
}

//
// Screen functions
//

void get_console_size(struct editor *ed) {
  struct winsize ws;

  ioctl(0, TIOCGWINSZ, &ws);
  ed->cols = ws.ws_col;
  ed->lines = ws.ws_row - 1;
}

//
// Keyboard functions
//

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

//
// Display functions
//

void display_message(struct editor *ed, char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  printf(GOTO_LINE_COL, ed->lines + 1, 1);
  fputs(STATUS_COLOR, stdout);
  vprintf(fmt, args);
  fputs(CLREOL TEXT_COLOR, stdout);
  fflush(stdout);
  va_end(args);
}

int prompt(struct editor *ed, char *msg, int selection) {
  int maxlen, len, ch;
  char *buf = ed->linebuf;

  display_message(ed, msg);

  len = 0;
  maxlen = ed->cols - strlen(msg) - 1;
  if (selection) {
    len = get_selected_text(ed, buf, maxlen);
    fwrite(buf, 1, len, stdout);
  }

  for (;;) {
    fflush(stdout);
    ch = get_key();
    if (ch == KEY_ESC) {
      return 0;
    } else if (ch == KEY_ENTER) {
      buf[len] = 0;
      return len > 0;
    } else if (ch == KEY_BACKSPACE) {
      if (len > 0) {
        fputs("\b \b", stdout);
        len--;
      }
    } else if (ch >= ' ' && ch < 0x100 && len < maxlen) {
      putchar(ch);
      buf[len++] = ch;
    }
  }
}

int ask() {
  int ch = getchar();
  return ch == 'y' || ch == 'Y';
}

void draw_full_statusline(struct editor *ed) {
  int namewidth = ed->cols - 19;
  printf(GOTO_LINE_COL, ed->lines + 1, 1);
  sprintf(ed->linebuf, STATUS_COLOR "%*.*s%c Ln %-6dCol %-4d" CLREOL TEXT_COLOR, -namewidth, namewidth, ed->filename, ed->dirty ? '*' : ' ', ed->line + 1, column(ed, ed->linepos, ed->col) + 1);
  fputs(ed->linebuf, stdout);
}

void draw_statusline(struct editor *ed) {
  printf(GOTO_LINE_COL, ed->lines + 1, ed->cols - 18);
  sprintf(ed->linebuf, STATUS_COLOR "%c Ln %-6dCol %-4d" CLREOL TEXT_COLOR, ed->dirty ? '*' : ' ', ed->line + 1, column(ed, ed->linepos, ed->col) + 1);
  fputs(ed->linebuf, stdout);
}

void display_line(struct editor *ed, int pos, int fullline) {
  int hilite = 0;
  int col = 0;
  int margin = ed->margin;
  int maxcol = ed->cols + margin;
  char *bufptr = ed->linebuf;
  char *p = ed->content + pos;
  int selstart, selend, ch;
  char *s;
  
  get_selection(ed, &selstart, &selend);
  while (col < maxcol) {
    if (margin == 0) {
      if (!hilite && pos >= selstart && pos < selend) {
        for (s = SELECT_COLOR; *s; s++) *bufptr++ = *s;
        hilite = 1;
      } else if (hilite && pos >= selend) {
        for (s = TEXT_COLOR; *s; s++) *bufptr++ = *s;
        hilite = 0;
      }
    }

    if (p == ed->content + MAXSIZE) break;
    ch = *p;
    if (ch == '\r' || ch == '\n' || ch == 0) break;

    if (ch == '\t') {
      int spaces = TABSIZE - col % TABSIZE;
      while (spaces > 0 && col < maxcol) {
        if (margin > 0) {
          margin--;
        } else {
          *bufptr++ = ' ';
        }
        col++;
        spaces--;
      }
    } else {
      if (margin > 0) {
        margin--;
      } else {
        *bufptr++ = ch;
      }
      if (ch < 0b10000000) {
        col++;
      }
    }

    p++;
    pos++;
  }

  if (hilite) {
    while (col < maxcol) {
      *bufptr++ = ' ';
      col++;
    }
  } else {
    if (col == margin) *bufptr++ = ' ';
  }

  if (col < maxcol) {
    for (s = CLREOL; *s; s++) *bufptr++ = *s;
  }

  if (fullline) {
    memcpy(bufptr, "\r\n", 2);
    bufptr += 2;
  }

  if (hilite) {
    for (s = TEXT_COLOR; *s; s++) *bufptr++ = *s;
  }

  fwrite(ed->linebuf, 1, bufptr - ed->linebuf, stdout);
}

void update_line(struct editor *ed) {
  printf(GOTO_LINE_COL, ed->line - ed->topline + 1, 1);
  display_line(ed, ed->linepos, 0);
}

void draw_screen(struct editor *ed) {
  int pos;
  int i;

  printf(GOTO_LINE_COL, 1, 1);
  fputs(TEXT_COLOR, stdout);
  pos = ed->toppos;
  for (i = 0; i < ed->lines; i++) {
    if (pos < 0) {
      fputs(CLREOL "\r\n", stdout);
    } else {
      display_line(ed, pos, 1);
      pos = next_line(ed, pos, 1);
    }
  }
}

void position_cursor(struct editor *ed) {
  int col = column(ed, ed->linepos, ed->col);
  if (ed->anchor != -1 && ed->anchor < ed->linepos + ed->col) {
    col = col - 1;
  }
  printf(GOTO_LINE_COL, ed->line - ed->topline + 1, col - ed->margin + 1);
}

//
// Cursor movement
//

void adjust(struct editor *ed) {
  int col, ll;

  if (ed->line < ed->topline) {
    ed->toppos = ed->linepos;
    ed->topline = ed->line;
    ed->refresh = 1;
  }

  while (ed->line >= ed->topline + ed->lines) {
    ed->toppos = next_line(ed, ed->toppos, 1);
    ed->topline++;
    ed->refresh = 1;
  }

  ll = line_length(ed, ed->linepos);
  ed->col = ed->lastcol;
  if (ed->col > ll) ed->col = ll;

  col = column(ed, ed->linepos, ed->col);
  while (col < ed->margin) {
    ed->margin -= 4;
    if (ed->margin < 0) ed->margin = 0;
    ed->refresh = 1;
  }

  while (col - ed->margin >= ed->cols) {
    ed->margin += 4;
    ed->refresh = 1;
  }
}

int sign(int x) {
  return (x > 0) - (x < 0);
}

void down(struct editor *ed, int select, int lines) {
  int newpos, i;
  int dir = sign(lines);

  update_selection(ed, select);

  for (i = 0; i < lines * dir; i++) {
    newpos = next_line(ed, ed->linepos, dir);
    if (newpos < 0) break;
  
    ed->linepos = newpos;
    ed->line += dir;
  }

  adjust(ed);
}

void left(struct editor *ed, int select) {
  update_selection(ed, select);
  if (ed->col > 0) {
    ed->col--;
  } else {
    int newpos = next_line(ed, ed->linepos, -1);
    if (newpos < 0) return;

    ed->col = line_length(ed, newpos);
    ed->linepos = newpos;
    ed->line--;
  }

  ed->lastcol = ed->col;
  adjust(ed);
}

void right(struct editor *ed, int select) {
  update_selection(ed, select);
  if (ed->col < line_length(ed, ed->linepos)) {
    ed->col++;
  } else {
    int newpos = next_line(ed, ed->linepos, 1);
    if (newpos < 0) return;

    ed->col = 0;
    ed->linepos = newpos;
    ed->line++;
  }

  ed->lastcol = ed->col;
  adjust(ed);
}

int wordchar(int ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

void wordleft(struct editor *ed, int select) {
  int pos, phase;
  
  update_selection(ed, select);
  pos = ed->linepos + ed->col;
  phase = 0;
  while (pos > 0) {
    int ch = get(ed, pos - 1);
    if (phase == 0) {
      if (wordchar(ch)) phase = 1;
    } else {
      if (!wordchar(ch)) break;
    }

    pos--;
    if (pos < ed->linepos) {
      ed->linepos = next_line(ed, ed->linepos, -1);
      ed->line--;
      ed->refresh = 1;
    }
  }
  ed->col = pos - ed->linepos;
  ed->lastcol = ed->col;
  adjust(ed);
}

void wordright(struct editor *ed, int select) {
  int pos, end, next, phase;
  
  update_selection(ed, select);
  pos = ed->linepos + ed->col;
  end = (int64_t) ed->content + MAXSIZE;
  next = next_line(ed, ed->linepos, 1);
  phase = 0;
  while (pos < end) {
    int ch = get(ed, pos);
    if (phase == 0) {
      if (wordchar(ch)) phase = 1;
    } else {
      if (!wordchar(ch)) break;
    }

    pos++;
    if (pos == next) {
      ed->linepos = next;
      next = next_line(ed, ed->linepos, 1);
      ed->line++;
      ed->refresh = 1;
    }
  }
  ed->col = pos - ed->linepos;
  ed->lastcol = ed->col;
  adjust(ed);
}

void home(struct editor *ed, int select) {
  update_selection(ed, select);
  ed->col = ed->lastcol = 0;
  adjust(ed);
}

void end(struct editor *ed, int select) {
  update_selection(ed, select);
  ed->col = ed->lastcol = line_length(ed, ed->linepos);
  adjust(ed);
}

//
// Text editing
//

void insert_char(struct editor *ed, char ch) {
  erase_selection(ed);
  insert(ed, ed->linepos + ed->col, &ch, 1);
  ed->col++;
  ed->lastcol = ed->col;
  adjust(ed);
  if (!ed->refresh) ed->lineupdate = 1;
}

void newline(struct editor *ed) {
  int p;
  char ch;

  erase_selection(ed);
  insert(ed, ed->linepos + ed->col, "\n", 1);
  ed->col = ed->lastcol = 0;
  ed->line++;
  p = ed->linepos;
  ed->linepos = next_line(ed, ed->linepos, 1);
  for (;;) {
    ch = get(ed, p++);
    if (ch == ' ' || ch == '\t') {
      insert(ed, ed->linepos + ed->col, &ch, 1);
      ed->col++;
    } else {
      break;
    }
  }
  ed->lastcol = ed->col;
  
  ed->refresh = 1;

  if (ed->line >= ed->topline + ed->lines) {
    ed->toppos = next_line(ed, ed->toppos, 1);
    ed->topline++;
    ed->refresh = 1;
  }

  adjust(ed);
}

void del(struct editor *ed) {
  int pos, ch;
  
  if (erase_selection(ed)) return;
  pos = ed->linepos + ed->col;
  ch = get(ed, pos);
  if (ch < 0) return;

  erase(ed, pos, 1);
  if (ch == '\r') {
    ch = get(ed, pos);
    if (ch == '\n') erase(ed, pos, 1);
  }

  if (ch == '\n') {
    ed->refresh = 1;
  } else {
    ed->lineupdate = 1;
  }
}

void backspace(struct editor *ed) {
  if (erase_selection(ed)) return;
  if (ed->linepos + ed->col == 0) return;
  left(ed, 0);
  del(ed);
}

void indent(struct editor *ed, char *indentation) {
  int start, end, i, lines, toplines, newline, ch;
  char *p;
  int buflen;
  int width = strlen(indentation);
  int pos = ed->linepos + ed->col;

  get_selection_or_line(ed, &start, &end);

  lines = 0;
  toplines = 0;
  newline = 1;
  for (i = start; i < end; i++) {
    if (i == ed->toppos) toplines = lines;
    if (newline) {
      lines++;
      newline = 0;
    }
    if (get(ed, i) == '\n') newline = 1;
  }
  buflen = end - start + lines * width;

  newline = 1;
  p = ed->tmpbuf;
  for (i = start; i < end; i++) {
    if (newline) {
      memcpy(p, indentation, width);
      p += width;
      newline = 0;
    }
    ch = get(ed, i);
    *p++ = ch;
    if (ch == '\n') newline = 1;
  }

  replace(ed, start, end - start, ed->tmpbuf, buflen);

  if (ed->anchor < pos) {
    pos += width * lines;
  } else {
    ed->anchor += width * lines;
  }

  ed->toppos += width * toplines;
  ed->linepos = line_start(ed, pos);
  ed->col = ed->lastcol = pos - ed->linepos;

  adjust(ed);
  ed->refresh = 1;
}

void unindent(struct editor *ed, char *indentation) {
  int start, end, i, newline, ch, shrinkage, topofs;
  char *p;
  int width = strlen(indentation);
  int pos = ed->linepos + ed->col;
  if (!get_selection(ed, &start, &end)) {
    start = ed->linepos;
    end = ed->linepos + line_length(ed, ed->linepos);
  }

  newline = 1;
  p = ed->tmpbuf;
  i = start;
  shrinkage = 0;
  topofs = 0;
  while (i < end) {
    if (newline) {
      newline = 0;
      if (strncmp(ed->content + pos + i, indentation, width)) {
        i += width;
        shrinkage += width;
        if (i < ed->toppos) topofs -= width;
        continue;
      }
    }
    ch = get(ed, i++);
    *p++ = ch;
    if (ch == '\n') newline = 1;
  }

  if (!shrinkage) { return; }

  replace(ed, start, end - start, ed->tmpbuf, p - ed->tmpbuf);

  if (ed->anchor < pos) {
    pos -= shrinkage;
  } else {
    ed->anchor -= shrinkage;
  }

  ed->toppos += topofs;
  ed->linepos = line_start(ed, pos);
  ed->col = ed->lastcol = pos - ed->linepos;

  ed->refresh = 1;
  adjust(ed);
}

//
// Clipboard
//

void copy_selection_or_line(struct editor *ed) {
  FILE *f_pri, *f_sec, *f_clip;
  
  int selstart, selend, pos;
  
  char c;

  get_selection_or_line(ed, &selstart, &selend);
  
  f_pri = popen("xsel", "w");
  f_sec = popen("xsel --secondary", "w");
  f_clip = popen("xsel --clipboard", "w");
  if (f_pri) {
    for (pos = selstart; pos < selend; pos++) {
     	c = *(ed->content + pos); 
      fprintf(f_pri, "%c", c);
      if (f_sec) fprintf(f_sec, "%c", c);
      if (f_clip) fprintf(f_clip, "%c", c);
    }
    pclose(f_pri);
    if (f_sec) pclose(f_sec);
    if (f_clip) pclose(f_clip);
  } else {  
    strncpy(ed->clipboard, ed->content+selstart, ed->clipsize);
  }
}

void cut_selection_or_line(struct editor *ed) {
  copy_selection_or_line(ed);
  erase_selection_or_line(ed);
}

void paste_selection(struct editor *ed) {
  FILE * f;
  char buffer[512];
  int n;
  int pos;

  erase_selection(ed);

  f = popen("xsel", "r");
  if (f) {
    pos = ed->linepos + ed->col;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
      insert(ed, pos, buffer, n);
      pos += n;
    }
    moveto(ed, pos, 0);
    pclose(f);
  } else {
    insert(ed, ed->linepos + ed->col, ed->clipboard, ed->clipsize);
    moveto(ed, ed->linepos + ed->col + ed->clipsize, 0);
  }

  ed->refresh = 1;
}

void duplicate_selection_or_line(struct editor *ed) {
  int selstart, selend, sellen;
  
  get_selection_or_line(ed, &selstart, &selend);

  sellen = selend - selstart;

  strncpy(ed->tmpbuf, ed->content + selstart, sellen);

  insert(ed, ed->linepos + ed->col, ed->tmpbuf, sellen);
  ed->refresh = 1;
}

//
// Editor Commands
//

void save_editor(struct editor *ed) {
  int rc;
  
  if (!ed->dirty) return;
  
  rc = save_file(ed);
  if (rc < 0) {
    display_message(ed, "Error %d saving document (%s)", errno, strerror(errno));
    sleep(5);
  }

  ed->refresh = 1;
}

void find_text(struct editor *ed, char* search) {
  int slen, selstart, selend;

  if (!search) {
    if (!get_selection(ed, &selstart, &selend)) {
      if (!prompt(ed, "Find: ", 1)) {
        ed->refresh = 1;
        return;
      }
      search = ed->linebuf;
    } else {
      strncpy(ed->tmpbuf, ed->content + selstart, selend - selstart);
    	search = ed->tmpbuf;
    }
  }
  
  slen = strlen(search);

  if (slen > 0) {
    char *match;
    
    match = strstr(ed->content + ed->linepos + ed->col, search);
    if (match) {
      int pos = match - ed->content;
      ed->anchor = pos;
      moveto(ed, pos + slen, 1);
    } else {
      putchar('\007');
    }
  }
  ed->refresh = 1;
}

void goto_line(struct editor *ed, int lineno) {
  int l, pos, new_pos;

  ed->anchor = -1;
  if (!lineno && prompt(ed, "Goto line: ", 1)) {
    lineno = atoi(ed->linebuf);
  }

  pos = 0;
  for (l = 0; l < lineno - 1 || lineno < 0; l++) {
    new_pos = next_line(ed, pos, 1);
    if (new_pos < 0) break;
    pos = new_pos;
  }

  if (pos >= 0) {
    moveto(ed, pos, 1);
  } else {
    putchar('\007');
  }
  ed->refresh = 1;
}

void goto_anything(struct editor *ed, char *query) {
  if (!query) {
    prompt(ed, "Goto Anything: ", 1);
    query = ed->linebuf;
  }

  if (query[0] == ':') { goto_line(ed, atoi(query + 1)); }
  if (query[0] == '#') { find_text(ed, query + 1); }
  if (query[0] == '@') { find_text(ed, query + 1); }
}

void redraw_screen(struct editor *ed) {
  get_console_size(ed);
  draw_screen(ed);
}

//
// Editor
//

void edit(struct editor *ed) {
  int done = 0;
  int key;
  int i;

  ed->refresh = 1;
  while (!done) {
    if (ed->refresh) {
      draw_screen(ed);
      draw_full_statusline(ed);
      ed->refresh = 0;
      ed->lineupdate = 0;
    } else if (ed->lineupdate) {
      update_line(ed);
      ed->lineupdate = 0;
      draw_statusline(ed);
    } else {
      draw_statusline(ed);
    }

    position_cursor(ed);
    fflush(stdout);
    key = get_key();

    if (key >= ' ' && key <= 0x7F) {
      insert_char(ed, (char) key);
    } else {
      switch (key) {
        case ctrl('t'): goto_line(ed, 1); break;
        case ctrl('b'): goto_line(ed, -1); break;

        case KEY_UP: down(ed, 0, -1); break;
        case KEY_DOWN: down(ed, 0, 1); break;
        case KEY_LEFT: left(ed, 0); break;
        case KEY_RIGHT: right(ed, 0); break;
        case KEY_HOME: home(ed, 0); break;
        case KEY_END: end(ed, 0); break;
        case KEY_PGUP: down(ed, 0, -PAGESIZE); break;
        case KEY_PGDN: down(ed, 0, PAGESIZE); break;
        case ctrl(KEY_UP): down(ed, 0, -PAGESIZE); break;
        case ctrl(KEY_DOWN): down(ed, 0, PAGESIZE); break;

        case ctrl(KEY_RIGHT): wordright(ed, 0); break;
        case ctrl(KEY_LEFT): wordleft(ed, 0); break;
        case ctrl(KEY_HOME): goto_line(ed, 1); break;
        case ctrl(KEY_END): goto_line(ed, -1); break;

        case shift(KEY_UP): down(ed, 1, -1); break;
        case shift(KEY_DOWN): down(ed, 1, 1); break;
        case shift(KEY_LEFT): left(ed, 1); break;
        case shift(KEY_RIGHT): right(ed, 1); break;
        case shift(KEY_PGUP): down(ed, 1, -PAGESIZE); break;
        case shift(KEY_PGDN): down(ed, 1, PAGESIZE); break;
        case shift(KEY_HOME): home(ed, 1); break;
        case shift(KEY_END): end(ed, 1); break;

        case shift(ctrl(KEY_RIGHT)): wordright(ed, 1); break;
        case shift(ctrl(KEY_LEFT)): wordleft(ed, 1); break;
        case shift(ctrl(KEY_HOME)): goto_line(ed, 1); break;
        case shift(ctrl(KEY_END)): goto_line(ed, -1); break;

        case ctrl('a'): select_all(ed); break;
        case ctrl('d'): duplicate_selection_or_line(ed); break;
        case ctrl('c'): copy_selection_or_line(ed); break;
        case KEY_F3: find_text(ed, 0); break;
        case ctrl('f'): find_text(ed, 0); break;
        case ctrl('l'): goto_line(ed, 0); break;
        case ctrl('g'): goto_anything(ed, 0); break;
        case ctrl('q'): done = 1; break;
        case ctrl('w'): done = 1; break;
        case ctrl('o'): done = 1; break;
        case ctrl('n'): done = 1; break;
        case KEY_TAB: indent(ed, INDENT); break;
        case shift(KEY_TAB): unindent(ed, INDENT); break;

        case KEY_ENTER: newline(ed); break;
        case KEY_BACKSPACE: backspace(ed); break;
        case KEY_DEL: del(ed); break;
        case ctrl('k'): erase_selection_or_line(ed); break;
        case ctrl('x'): cut_selection_or_line(ed); break;
        case ctrl('v'): paste_selection(ed); break;
        case ctrl('s'): save_editor(ed); break;
        default:
          erase_selection(ed);
          insert(ed, ed->linepos + ed->col, (char*)&key, 1);
          for (i = 1; *((char*)(&key) + i) & 0b10000000; i++) {
            insert(ed, ed->linepos + ed->col, (char*)(&key) + i, 1);
          }

          ed->col++;
          ed->lastcol = ed->col;
          adjust(ed);
          if (!ed->refresh) ed->lineupdate = 1;
          break;
      }
    }
  }
}

int main(int argc, char *argv[]) {
  sigset_t blocked_sigmask, orig_sigmask;
  struct termios tio;
  struct termios orig_tio;

  struct editor ed;

  if (argc < 2) return 0;

  if (load_file(&ed, argv[1]) < 0) {
    perror(argv[1]);
    return 0;
  }

  if (argc >= 3) goto_anything(&ed, argv[2]);

  setvbuf(stdout, NULL, 0, 8192);

  tcgetattr(0, &orig_tio);
  cfmakeraw(&tio);  
  tcsetattr(0, TCSANOW, &tio);
  linux_console = getenv("TERM") && !strcmp(getenv("TERM"), "linux");

  get_console_size(&ed);
  sigemptyset(&blocked_sigmask);
  sigaddset(&blocked_sigmask, SIGINT);
  sigaddset(&blocked_sigmask, SIGTSTP);
  sigaddset(&blocked_sigmask, SIGABRT);
  sigprocmask(SIG_BLOCK, &blocked_sigmask, &orig_sigmask);

  edit(&ed);

  printf(GOTO_LINE_COL, ed.lines + 2, 1);
  fputs(RESET_COLOR CLREOL, stdout);
  tcsetattr(0, TCSANOW, &orig_tio);   

  setbuf(stdout, NULL);
  sigprocmask(SIG_SETMASK, &orig_sigmask, NULL);
  return 0;
}
