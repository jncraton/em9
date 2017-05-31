#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <termios.h>
#define O_BINARY 0
int linux_console = 0;

#define MINEXTEND      32768
#define LINEBUF_EXTRA  32

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

enum key_codes {KEY_BACKSPACE = 0x108, KEY_ESC,KEY_INS, KEY_DEL, KEY_LEFT, 
  KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_HOME, KEY_END, KEY_ENTER, KEY_TAB,
  KEY_PGUP, KEY_PGDN, KEY_CTRL_UP,
  KEY_CTRL_DOWN, KEY_CTRL_HOME, KEY_CTRL_END, KEY_CTRL_TAB,
  KEY_SHIFT_LEFT, KEY_SHIFT_RIGHT, KEY_SHIFT_UP, KEY_SHIFT_DOWN,
  KEY_SHIFT_PGUP, KEY_SHIFT_PGDN, KEY_SHIFT_HOME, KEY_SHIFT_END, 
  KEY_SHIFT_TAB, KEY_SHIFT_CTRL_LEFT, KEY_SHIFT_CTRL_RIGHT, 
  KEY_SHIFT_CTRL_UP, KEY_SHIFT_CTRL_DOWN, KEY_SHIFT_CTRL_HOME,
  KEY_SHIFT_CTRL_END, KEY_F3, KEY_UNKNOWN};

#define ctrl(c) ((c) - 0x60)

//
// Editor data block
//
// Structure of split buffer:
//
//    +------------------+------------------+------------------+
//    | text before gap  |        gap       |  text after gap  |
//    +------------------+------------------+------------------+
//    ^                  ^                  ^                  ^     
//    |                  |                  |                  |
//  start               gap                rest               end
//

struct env;

struct undo {
  int pos;                 // Editor position
  int erased;              // Size of erased contents
  int inserted;            // Size of inserted contents
  char *undobuf;  // Erased contents for undo
  char *redobuf;  // Inserted contents for redo
  struct undo *next;       // Next undo buffer
  struct undo *prev;       // Previous undo buffer
};

struct editor {
  char *start;      // Start of text buffer
  char *gap;        // Start of gap
  char *rest;       // End of gap
  char *end;        // End of text buffer

  int toppos;                // Text position for current top screen line
  int topline;               // Line number for top of screen
  int margin;                // Position for leftmost column on screen

  int linepos;               // Text position for current line
  int line;                  // Current document line
  int col;                   // Current document column
  int lastcol;               // Remembered column from last horizontal navigation
  int anchor;                // Anchor position for selection
  
  struct undo *undohead;     // Start of undo buffer list
  struct undo *undotail;     // End of undo buffer list
  struct undo *undo;         // Undo/redo boundary

  int refresh;               // Flag to trigger screen redraw
  int lineupdate;            // Flag to trigger redraw of current line
  int dirty;                 // Dirty flag is set when the editor buffer has been changed

  int newfile;               // File is a new file
  int permissions;           // File permissions

  struct env *env;           // Reference to global editor environment

  char filename[FILENAME_MAX];
};

struct env {
  struct editor *ed; // Current editor

  char *clipboard;
  int clipsize;

  char *linebuf;     // Scratch buffer

  int cols;          // Console columns
  int lines;         // Console lines
};

//
// Editor buffer functions
//

void clear_undo(struct editor *ed) {
  struct undo *undo = ed->undohead;
  while (undo) {
    struct undo *next = undo->next;
    free(undo->undobuf);
    free(undo->redobuf);
    free(undo);
    undo = next;
  }
  ed->undohead = ed->undotail = ed->undo = NULL;
}

void reset_undo(struct editor *ed) {
  while (ed->undotail != ed->undo) {
    struct undo *undo = ed->undotail;
    if (!undo) {
      ed->undohead = NULL;
      ed->undotail = NULL;
      break;
    }
    ed->undotail = undo->prev;
    if (undo->prev) undo->prev->next = NULL;
    free(undo->undobuf);
    free(undo->redobuf);
    free(undo);
  }
  ed->undo = ed->undotail;
}

struct editor *create_editor(struct env *env) {
  struct editor *ed = (struct editor *) malloc(sizeof(struct editor));
  memset(ed, 0, sizeof(struct editor));
  ed->env = env;
  env->ed = ed;
  return ed;
}

void delete_editor(struct editor *ed) {
  ed->env->ed = NULL;
  if (ed->start) free(ed->start);
  clear_undo(ed);
  free(ed);
}

int load_file(struct editor *ed, char *filename) {
  struct stat statbuf;
  int length;
  int f;

  if (!realpath(filename, ed->filename)) return -1;
  f = open(ed->filename, O_RDONLY | O_BINARY);
  if (f < 0) return -1;

  if (fstat(f, &statbuf) < 0) goto err;
  length = statbuf.st_size;
  ed->permissions = statbuf.st_mode & 0777;

  ed->start = (char *) malloc(length + MINEXTEND);
  if (!ed->start) goto err;
  if (read(f, ed->start, length) != length) goto err;

  ed->gap = ed->start + length;
  ed->rest = ed->end = ed->gap + MINEXTEND;
  ed->anchor = -1;

  close(f);
  return 0;

err:
  close(f);
  if (ed->start) {
    free(ed->start);
    ed->start = NULL;
  }
  return -1;
}

int save_file(struct editor *ed) {
  int f;

  f = open(ed->filename, O_CREAT | O_TRUNC | O_WRONLY, ed->permissions);
  if (f < 0) return -1;

  if (write(f, ed->start, ed->gap - ed->start) != ed->gap - ed->start) goto err;
  if (write(f, ed->rest, ed->end - ed->rest) != ed->end - ed->rest) goto err;

  close(f);
  ed->dirty = 0;
  clear_undo(ed);
  return 0;

err:
  close(f);
  return -1;
}

int text_length(struct editor *ed) {
  return (ed->gap - ed->start) + (ed->end - ed->rest);
}

char *text_ptr(struct editor *ed, int pos) {
  char *p = ed->start + pos;
  if (p >= ed->gap) p += (ed->rest - ed->gap);
  return p;
}

void move_gap(struct editor *ed, int pos, int minsize) {
  int gapsize = ed->rest - ed->gap;
  char *p = text_ptr(ed, pos);
  if (minsize < 0) minsize = 0;

  if (minsize <= gapsize) {
    if (p != ed->rest) {
      if (p < ed->gap) {
        memmove(p + gapsize, p, ed->gap - p);
      } else {
        memmove(ed->gap, ed->rest, p - ed->rest);
      }
      ed->gap = ed->start + pos;
      ed->rest = ed->gap + gapsize;
    }
  } else {
    int newsize;
    char *start;
    char *gap;
    char *rest;
    char *end;

    if (gapsize + MINEXTEND > minsize) minsize = gapsize + MINEXTEND;
    newsize = (ed->end - ed->start) - gapsize + minsize;
    start = (char *) malloc(newsize); // TODO check for out of memory
    gap = start + pos;
    rest = gap + minsize;
    end = start + newsize;

    if (p < ed->gap) {
      memcpy(start, ed->start, pos);
      memcpy(rest, p, ed->gap - p);
      memcpy(end - (ed->end - ed->rest), ed->rest, ed->end - ed->rest);
    } else {
      memcpy(start, ed->start, ed->gap - ed->start);
      memcpy(start + (ed->gap - ed->start), ed->rest, p - ed->rest);
      memcpy(rest, p, ed->end - p);
    }

    free(ed->start);
    ed->start = start;
    ed->gap = gap;
    ed->rest = rest;
    ed->end = end;
  }
}

void close_gap(struct editor *ed) {
  int len = text_length(ed);
  move_gap(ed, len, 1);
  ed->start[len] = 0;
}

int get(struct editor *ed, int pos) {
  char *p = text_ptr(ed, pos);
  if (p >= ed->end) return -1;
  return *p;
}

int compare(struct editor *ed, char *buf, int pos, int len) {
  char *bufptr = buf;
  char *p = ed->start + pos;
  if (p >= ed->gap) p += (ed->rest - ed->gap);

  while (len > 0) {
    if (p == ed->end) return 0;
    if (*bufptr++ != *p) return 0;
    len--;
    if (++p == ed->gap) p = ed->rest;
  }

  return 1;
}

int copy(struct editor *ed, char *buf, int pos, int len) {
  char *bufptr = buf;
  char *p = ed->start + pos;
  if (p >= ed->gap) p += (ed->rest - ed->gap);

  while (len > 0) {
    if (p == ed->end) break;
    *bufptr++ = *p;
    len--;
    if (++p == ed->gap) p = ed->rest;
  }

  return bufptr - buf;
}

void replace(struct editor *ed, int pos, int len, char *buf, int bufsize, int doundo) {
  char *p;
  struct undo *undo;

  // Store undo information
  if (doundo) {
    reset_undo(ed);
    undo = ed->undotail;
    if (undo && len == 0 && bufsize == 1 && undo->erased == 0 && pos == undo->pos + undo->inserted) {
      // Insert character at end of current redo buffer
      undo->redobuf = realloc(undo->redobuf, undo->inserted + 1);
      undo->redobuf[undo->inserted] = *buf;
      undo->inserted++;
    } else if (undo && len == 1 && bufsize == 0 && undo->inserted == 0 && pos == undo->pos) {
      // Erase character at end of current undo buffer
      undo->undobuf = realloc(undo->undobuf, undo->erased + 1);
      undo->undobuf[undo->erased] = get(ed, pos);
      undo->erased++;
    } else if (undo && len == 1 && bufsize == 0 && undo->inserted == 0 && pos == undo->pos - 1) {
      // Erase character at beginning of current undo buffer
      undo->pos--;
      undo->undobuf = realloc(undo->undobuf, undo->erased + 1);
      memmove(undo->undobuf + 1, undo->undobuf, undo->erased);
      undo->undobuf[0] = get(ed, pos);
      undo->erased++;
    } else {
      // Create new undo buffer
      undo = (struct undo *) malloc(sizeof(struct undo));
      if (ed->undotail) ed->undotail->next = undo;
      undo->prev = ed->undotail;
      undo->next = NULL;
      ed->undotail = ed->undo = undo;
      if (!ed->undohead) ed->undohead = undo;

      undo->pos = pos;
      undo->erased = len;
      undo->inserted = bufsize;
      undo->undobuf = undo->redobuf = NULL;
      if (len > 0) {
        undo->undobuf = malloc(len);
        copy(ed, undo->undobuf, pos, len);      
      }
      if (bufsize > 0) {
        undo->redobuf = malloc(bufsize);
        memcpy(undo->redobuf, buf, bufsize);
      }
    }
  }

  p = ed->start + pos;
  if (bufsize == 0 && p <= ed->gap && p + len >= ed->gap) {
    // Handle deletions at the edges of the gap
    ed->rest += len - (ed->gap - p);
    ed->gap = p;
  } else {
    // Move the gap
    move_gap(ed, pos + len, bufsize - len);

    // Replace contents
    memcpy(ed->start + pos, buf, bufsize);
    ed->gap = ed->start + pos + bufsize;
  }

  // Mark buffer as dirty
  ed->dirty = 1;
}

void insert(struct editor *ed, int pos, char *buf, int bufsize) {
  replace(ed, pos, 0, buf, bufsize, 1);
}

void erase(struct editor *ed, int pos, int len) {
  replace(ed, pos, len, NULL, 0, 1);
}

//
// Navigation functions
//

int line_length(struct editor *ed, int linepos) {
  int pos = linepos;
  while (1) {
    int ch = get(ed, pos);
    if (ch < 0 || ch == '\n' || ch == '\r') break;
    pos++;
  }

  return pos - linepos;
}

int line_start(struct editor *ed, int pos) {
  while (1) {
    if (pos == 0) break;
    if (get(ed, pos - 1) == '\n') break;
    pos--;
  }

  return pos;
}

int next_line(struct editor *ed, int pos, int dir) {
  pos = line_start(ed, pos);
	
  if (dir > 0) pos += line_length(ed, pos);
  
  pos += dir;

  if (pos < 0) return -1;
  int ch = get(ed, pos);
  if (ch < 0) return -1;
  
  return line_start(ed, pos);
}

int column(struct editor *ed, int linepos, int col) {
  char *p = text_ptr(ed, linepos);
  int c = 0;
  while (col > 0) {
    if (p == ed->end) break;
    if (*p == '\t') {
      int spaces = TABSIZE - c % TABSIZE;
      c += spaces;
    } else {
      c++;
    }
    col--;
    if (++p == ed->gap) p = ed->rest;
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

        if (ed->line >= ed->topline + ed->env->lines) {
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
    int tl = ed->line - ed->env->lines / 2;
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
  copy(ed, buffer, selstart, len);
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
  moveto(ed, text_length(ed), 0);
}

//
// Screen functions
//

void get_console_size(struct env *env) {
  struct winsize ws;

  ioctl(0, TIOCGWINSZ, &ws);
  env->cols = ws.ws_col;
  env->lines = ws.ws_row - 1;
  env->linebuf = realloc(env->linebuf, env->cols + LINEBUF_EXTRA);
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
enum key_codes get_key() {
  int ch, shift, ctrl;

  ch = getchar();
  if (ch < 0) return ch;

  switch (ch) {
    case 0x08: return KEY_BACKSPACE;
    case 0x09:
      get_modifier_keys(&shift, &ctrl);
      if (shift) return KEY_SHIFT_TAB;
      if (ctrl) return KEY_CTRL_TAB;
      return KEY_TAB;
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
                if (shift && ctrl) return KEY_SHIFT_CTRL_HOME;
                if (shift) return KEY_SHIFT_HOME;
                if (ctrl) return KEY_CTRL_HOME;
                return KEY_HOME;
              default:
                return KEY_UNKNOWN;
            }
          }

          switch (ch) {
            case 0x31: 
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              if (shift && ctrl) return KEY_SHIFT_CTRL_HOME;
              if (shift) return KEY_SHIFT_HOME;
              if (ctrl) return KEY_CTRL_HOME;
              return KEY_HOME;
            case 0x32: return getchar() == 0x7E ? KEY_INS : KEY_UNKNOWN;
            case 0x33: return getchar() == 0x7E ? KEY_DEL : KEY_UNKNOWN;
            case 0x34:
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              if (shift && ctrl) return KEY_SHIFT_CTRL_END;
              if (shift) return KEY_SHIFT_END;
              if (ctrl) return KEY_CTRL_END;
              return KEY_END;
            case 0x35:
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              if (shift) return KEY_SHIFT_PGUP;
              return KEY_PGUP;
            case 0x36:
              if (getchar() != 0x7E) return KEY_UNKNOWN;
              if (shift) return KEY_SHIFT_PGDN;
              return KEY_PGDN;
            case 0x41: 
              if (shift && ctrl) return KEY_SHIFT_CTRL_UP;
              if (shift) return KEY_SHIFT_UP;
              if (ctrl) return KEY_CTRL_UP;
              return KEY_UP;
            case 0x42: 
              if (shift && ctrl) return KEY_SHIFT_CTRL_DOWN;
              if (shift) return KEY_SHIFT_DOWN;
              if (ctrl) return KEY_CTRL_DOWN;
              return KEY_DOWN;
            case 0x43: 
              if (shift && ctrl) return KEY_SHIFT_CTRL_RIGHT;
              if (shift) return KEY_SHIFT_RIGHT;
              if (ctrl) return ctrl(KEY_RIGHT);
              return KEY_RIGHT;
            case 0x44:
              if (shift && ctrl) return KEY_SHIFT_CTRL_LEFT;
              if (shift) return KEY_SHIFT_LEFT;
              if (ctrl) return ctrl(KEY_LEFT);
              return KEY_LEFT;
            case 0x46:
              if (shift && ctrl) return KEY_SHIFT_CTRL_END;
              if (shift) return KEY_SHIFT_END;
              if (ctrl) return KEY_CTRL_END;
              return KEY_END;
            case 0x48:
              if (shift && ctrl) return KEY_SHIFT_CTRL_HOME;
              if (shift) return KEY_SHIFT_HOME;
              if (ctrl) return KEY_CTRL_HOME;
              return KEY_HOME;
            case 0x5A: 
              return KEY_SHIFT_TAB;
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
        case 0x0F: return KEY_SHIFT_TAB;
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
        case 0x75: return KEY_CTRL_END;
        case 0x77: return KEY_CTRL_HOME;
        case 0x8D: return KEY_CTRL_UP;
        case 0x91: return KEY_CTRL_DOWN;
        case 0x94: return KEY_CTRL_TAB;
        case 0xB8: return KEY_SHIFT_UP;
        case 0xB7: return KEY_SHIFT_HOME;
        case 0xBF: return KEY_SHIFT_END;
        case 0xB9: return KEY_SHIFT_PGUP;
        case 0xBB: return KEY_SHIFT_LEFT;
        case 0xBD: return KEY_SHIFT_RIGHT;
        case 0xC0: return KEY_SHIFT_DOWN;
        case 0xC1: return KEY_SHIFT_PGDN;
        case 0xDB: return KEY_SHIFT_CTRL_LEFT;
        case 0xDD: return KEY_SHIFT_CTRL_RIGHT;
        case 0xD8: return KEY_SHIFT_CTRL_UP;
        case 0xE0: return KEY_SHIFT_CTRL_DOWN;
        case 0xD7: return KEY_SHIFT_CTRL_HOME;
        case 0xDF: return KEY_SHIFT_CTRL_END;

        default: return KEY_UNKNOWN;
      }
      break;

    case 0x7F: return KEY_BACKSPACE;

    default: return ch;
  }
}

//
// Display functions
//

void display_message(struct editor *ed, char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  printf(GOTO_LINE_COL, ed->env->lines + 1, 1);
  fputs(STATUS_COLOR, stdout);
  vprintf(fmt, args);
  fputs(CLREOL TEXT_COLOR, stdout);
  fflush(stdout);
  va_end(args);
}

int prompt(struct editor *ed, char *msg, int selection) {
  int maxlen, len, ch;
  char *buf = ed->env->linebuf;

  display_message(ed, msg);

  len = 0;
  maxlen = ed->env->cols - strlen(msg) - 1;
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
  struct env *env = ed->env;
  int namewidth = env->cols - 19;
  printf(GOTO_LINE_COL, env->lines + 1, 1);
  sprintf(env->linebuf, STATUS_COLOR "%*.*s%c Ln %-6dCol %-4d" CLREOL TEXT_COLOR, -namewidth, namewidth, ed->filename, ed->dirty ? '*' : ' ', ed->line + 1, column(ed, ed->linepos, ed->col) + 1);
  fputs(env->linebuf, stdout);
}

void draw_statusline(struct editor *ed) {
  printf(GOTO_LINE_COL, ed->env->lines + 1, ed->env->cols - 18);
  sprintf(ed->env->linebuf, STATUS_COLOR "%c Ln %-6dCol %-4d" CLREOL TEXT_COLOR, ed->dirty ? '*' : ' ', ed->line + 1, column(ed, ed->linepos, ed->col) + 1);
  fputs(ed->env->linebuf, stdout);
}

void display_line(struct editor *ed, int pos, int fullline) {
  int hilite = 0;
  int col = 0;
  int margin = ed->margin;
  int maxcol = ed->env->cols + margin;
  char *bufptr = ed->env->linebuf;
  char *p = text_ptr(ed, pos);
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

    if (p == ed->end) break;
    ch = *p;
    if (ch == '\r' || ch == '\n') break;

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
      col++;
    }

    if (++p == ed->gap) p = ed->rest;
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

  fwrite(ed->env->linebuf, 1, bufptr - ed->env->linebuf, stdout);
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
  for (i = 0; i < ed->env->lines; i++) {
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

  while (ed->line >= ed->topline + ed->env->lines) {
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

  while (col - ed->margin >= ed->env->cols) {
    ed->margin += 4;
    ed->refresh = 1;
  }
}

void up(struct editor *ed, int select) {
  int newpos;
  
  update_selection(ed, select);

  newpos = next_line(ed, ed->linepos, -1);
  if (newpos < 0) return;

  ed->linepos = newpos;
  ed->line--;

  adjust(ed);
}

void down(struct editor *ed, int select) {
  int newpos;
  
  update_selection(ed, select);

  newpos = next_line(ed, ed->linepos, 1);
  if (newpos < 0) return;

  ed->linepos = newpos;
  ed->line++;

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
  int pos, end, phase, next;
  
  update_selection(ed, select);
  pos = ed->linepos + ed->col;
  end = text_length(ed);
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

void pageup(struct editor *ed, int select) {
  int i;

  for (i = 0; i < PAGESIZE; i++) up(ed, select);
}

void pagedown(struct editor *ed, int select) {
  int i;

  for (i = 0; i < PAGESIZE; i++) down(ed, select);
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

  if (ed->line >= ed->topline + ed->env->lines) {
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
  char *buffer, *p;
  int buflen;
  int width = strlen(indentation);
  int pos = ed->linepos + ed->col;

  if (!get_selection(ed, &start, &end)) {
    insert_char(ed, '\t');
    return;
  }

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
  buffer = malloc(buflen);
  if (!buffer) return;

  newline = 1;
  p = buffer;
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

  replace(ed, start, end - start, buffer, buflen, 1);
  free(buffer);  

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
  char *buffer, *p;
  int width = strlen(indentation);
  int pos = ed->linepos + ed->col;

  if (!get_selection(ed, &start, &end)) return;

  buffer = malloc(end - start);
  if (!buffer) return;

  newline = 1;
  p = buffer;
  i = start;
  shrinkage = 0;
  topofs = 0;
  while (i < end) {
    if (newline) {
      newline = 0;
      if (compare(ed, indentation, i, width)) {
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

  if (!shrinkage) {
    free(buffer);
    return;
  }

  replace(ed, start, end - start, buffer, p - buffer, 1);
  free(buffer);

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

void undo(struct editor *ed) {
  if (!ed->undo) return;
  moveto(ed, ed->undo->pos, 0);
  replace(ed, ed->undo->pos, ed->undo->inserted, ed->undo->undobuf, ed->undo->erased, 0);
  ed->undo = ed->undo->prev;
  if (!ed->undo) ed->dirty = 0;
  ed->anchor = -1;
  ed->lastcol = ed->col;
  ed->refresh = 1;
}

void redo(struct editor *ed) {
  if (ed->undo) {
    if (!ed->undo->next) return;
    ed->undo = ed->undo->next;
  } else {
    if (!ed->undohead) return;
    ed->undo = ed->undohead;
  }
  replace(ed, ed->undo->pos, ed->undo->erased, ed->undo->redobuf, ed->undo->inserted, 0);
  moveto(ed, ed->undo->pos, 0);
  ed->dirty = 1;
  ed->anchor = -1;
  ed->lastcol = ed->col;
  ed->refresh = 1;
}

//
// Clipboard
//

void copy_selection_or_line(struct editor *ed) {
  FILE *f_pri, *f_sec, *f_clip;
  
  int selstart, selend, pos;

  get_selection_or_line(ed, &selstart, &selend);
  
  f_pri = popen("xsel", "w");
  f_sec = popen("xsel --secondary", "w");
  f_clip = popen("xsel --clipboard", "w");
  if (f_pri) {
    for (pos = selstart; pos < selend; pos++) {
      fprintf(f_pri, "%c", *text_ptr(ed, pos));
      if (f_sec) fprintf(f_sec, "%c", *text_ptr(ed, pos));
      if (f_clip) fprintf(f_clip, "%c", *text_ptr(ed, pos));
    }
    pclose(f_pri);
    if (f_sec) pclose(f_sec);
    if (f_clip) pclose(f_clip);
  } else {  
    ed->env->clipsize = selend - selstart;
    ed->env->clipboard = (char *) realloc(ed->env->clipboard, ed->env->clipsize);
    if (!ed->env->clipboard) return;
    copy(ed, ed->env->clipboard, selstart, ed->env->clipsize);
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
    insert(ed, ed->linepos + ed->col, ed->env->clipboard, ed->env->clipsize);
    moveto(ed, ed->linepos + ed->col + ed->env->clipsize, 0);
  }

  ed->refresh = 1;
}

void duplicate_selection_or_line(struct editor *ed) {
  int selstart, selend, sellen;
  char* buf;
  
  get_selection_or_line(ed, &selstart, &selend);

  sellen = selend - selstart;

  buf = malloc(sellen);
  if (!buf) return;
  
  copy(ed, buf, selstart, sellen);

  insert(ed, ed->linepos + ed->col, buf, sellen);
  ed->refresh = 1;

  free(buf);
}

//
// Editor Commands
//

void read_from_stdin(struct editor *ed) {
  char buffer[512];
  int n, pos;

  pos = 0;
  while ((n = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
    insert(ed, pos, buffer, n);
    pos += n;
  }
  strcpy(ed->filename, "<stdin>");  
  ed->newfile = 1;
  ed->dirty = 0;
}

void save_editor(struct editor *ed) {
  int rc;
  
  if (!ed->dirty && !ed->newfile) return;
  
  if (ed->newfile) {
    if (!prompt(ed, "Save as: ", 1)) {
      ed->refresh = 1;
      return;
    }

    if (access(ed->env->linebuf, F_OK) == 0) {
      display_message(ed, "Overwrite %s (y/n)? ", ed->env->linebuf);
      if (!ask()) {
        ed->refresh = 1;
        return;
      }
    }
    strcpy(ed->filename, ed->env->linebuf);
    ed->newfile = 0;
  }

  rc = save_file(ed);
  if (rc < 0) {
    display_message(ed, "Error %d saving document (%s)", errno, strerror(errno));
    sleep(5);
  }

  ed->refresh = 1;
}

void find_text(struct editor *ed, char* search) {
  int slen, selstart, selend;
  int own_search = 0; //tracks ownership of search string to determine if it should be freed

  if (!search) {
    if (!get_selection(ed, &selstart, &selend)) {
      if (!prompt(ed, "Find: ", 1)) {
        ed->refresh = 1;
        return;
      }    
      search = strdup(ed->env->linebuf);
    } else {
      search = malloc(selend - selstart);
      copy(ed, search, selstart, selend - selstart);
      own_search = 1;
    }
    if (!search) return;
  }
  
  slen = strlen(search);

  if (slen > 0) {
    char *match;
    
    close_gap(ed);
    match = strstr(ed->start + ed->linepos + ed->col, search);
    if (match != NULL) {
      int pos = match - ed->start;
      ed->anchor = pos;
      moveto(ed, pos + slen, 1);
    } else {
      putchar('\007');
    }
  }
  ed->refresh = 1;
  if (own_search) free(search);
}

void goto_line(struct editor *ed, int lineno) {
  int l, pos, new_pos;

  ed->anchor = -1;
  if (!lineno && prompt(ed, "Goto line: ", 1)) {
    lineno = atoi(ed->env->linebuf);
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
    query = ed->env->linebuf;
  }

  if (query[0] == ':') { goto_line(ed, atoi(query + 1)); }
  if (query[0] == '#') { find_text(ed, query + 1); }
  if (query[0] == '@') { find_text(ed, query + 1); }
}

void redraw_screen(struct editor *ed) {
  get_console_size(ed->env);
  draw_screen(ed);
}

int quit(struct env *env) {
  if (env->ed->dirty) {
    display_message(env->ed, "Close %s without saving changes (y/n)? ", env->ed->filename);
    if (!ask()) return 0;
  }

  return 1;
}

//
// Editor
//

void edit(struct editor *ed) {
  int done = 0;
  int key;

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
#ifdef LESS
      switch (key) {
        case 'q': done = 1; break;
        case '/': find_text(ed, 0); break;
      }
#else
      insert_char(ed, (char) key);
#endif
    } else {
      switch (key) {
        case ctrl('t'): goto_line(ed, 1); break;
        case ctrl('b'): goto_line(ed, -1); break;

        case KEY_UP: up(ed, 0); break;
        case KEY_DOWN: down(ed, 0); break;
        case KEY_LEFT: left(ed, 0); break;
        case KEY_RIGHT: right(ed, 0); break;
        case KEY_HOME: home(ed, 0); break;
        case KEY_END: end(ed, 0); break;
        case KEY_PGUP: pageup(ed, 0); break;
        case KEY_PGDN: pagedown(ed, 0); break;

        case ctrl(KEY_RIGHT): wordright(ed, 0); break;
        case ctrl(KEY_LEFT): wordleft(ed, 0); break;
        case KEY_CTRL_HOME: goto_line(ed, 1); break;
        case KEY_CTRL_END: goto_line(ed, -1); break;

        case KEY_SHIFT_UP: up(ed, 1); break;
        case KEY_SHIFT_DOWN: down(ed, 1); break;
        case KEY_SHIFT_LEFT: left(ed, 1); break;
        case KEY_SHIFT_RIGHT: right(ed, 1); break;
        case KEY_SHIFT_PGUP: pageup(ed, 1); break;
        case KEY_SHIFT_PGDN: pagedown(ed, 1); break;
        case KEY_SHIFT_HOME: home(ed, 1); break;
        case KEY_SHIFT_END: end(ed, 1); break;

        case KEY_SHIFT_CTRL_RIGHT: wordright(ed, 1); break;
        case KEY_SHIFT_CTRL_LEFT: wordleft(ed, 1); break;
        case KEY_SHIFT_CTRL_HOME: goto_line(ed, 1); break;
        case KEY_SHIFT_CTRL_END: goto_line(ed, -1); break;

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
#ifdef LESS
        case KEY_ESC: done = 1; break;
#else
        case KEY_TAB: indent(ed, INDENT); break;
        case KEY_SHIFT_TAB: unindent(ed, INDENT); break;

        case KEY_ENTER: newline(ed); break;
        case KEY_BACKSPACE: backspace(ed); break;
        case KEY_DEL: del(ed); break;
        case ctrl('x'): cut_selection_or_line(ed); break;
        case ctrl('z'): undo(ed); break;
        case ctrl('r'): redo(ed); break;
        case ctrl('v'): paste_selection(ed); break;
        case ctrl('s'): save_editor(ed); break;
#endif
      }
    }
  }
}

//
// main
//

int main(int argc, char *argv[]) {
  struct env env;
  sigset_t blocked_sigmask, orig_sigmask;
  struct termios tio;
  struct termios orig_tio;

  memset(&env, 0, sizeof(env));
	
  env.ed = create_editor(&env);

  if (argc >= 2) {
    if (load_file(env.ed, argv[1]) < 0) {
      perror(argv[1]);
      return 0;
    }
  } else {
    if (isatty(fileno(stdin))) {
      return 0;
    } else {
      read_from_stdin(env.ed);
    }    
  }

  if (!isatty(fileno(stdin))) {
    if (!freopen("/dev/tty", "r", stdin)) perror("/dev/tty");
  }

  if (argc >= 3) goto_anything(env.ed, argv[2]);

  setvbuf(stdout, NULL, 0, 8192);

  tcgetattr(0, &orig_tio);
  cfmakeraw(&tio);  
  tcsetattr(0, TCSANOW, &tio);
  if (getenv("TERM") && strcmp(getenv("TERM"), "linux") == 0) {
    linux_console = 1;
  } else {
    fputs("\033[3 q", stdout);  // xterm
    fputs("\033]50;CursorShape=2\a", stdout);  // KDE
  }

  get_console_size(&env);
  sigemptyset(&blocked_sigmask);
  sigaddset(&blocked_sigmask, SIGINT);
  sigaddset(&blocked_sigmask, SIGTSTP);
  sigaddset(&blocked_sigmask, SIGABRT);
  sigprocmask(SIG_BLOCK, &blocked_sigmask, &orig_sigmask);

  for (;;) {
    if (!env.ed) break;
    edit(env.ed);
    if (quit(&env)) break;
  }

  printf(GOTO_LINE_COL, env.lines + 2, 1);
  fputs(RESET_COLOR CLREOL, stdout);
  tcsetattr(0, TCSANOW, &orig_tio);   

  delete_editor(env.ed);

  if (env.clipboard) free(env.clipboard);
  if (env.linebuf) free(env.linebuf);

  setbuf(stdout, NULL);
  sigprocmask(SIG_SETMASK, &orig_sigmask, NULL);
  return 0;
}
