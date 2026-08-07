#ifndef INPUT_H
#define INPUT_H

typedef enum {
    INPUT_NONE = 0,
    INPUT_LEFT,
    INPUT_RIGHT,
    INPUT_UP,
    INPUT_DOWN,
    INPUT_SELECT,
    INPUT_QUIT
} input_event_t;

/* Puts the controlling terminal (stdin) into raw, non-blocking mode so
 * key presses typed into the SSH session running this program can be read
 * without waiting for Enter. Returns 0 on success. */
int input_init(void);

/* Restores the terminal to its original mode. Always call before exiting. */
void input_restore(void);

/* Non-blocking: returns INPUT_NONE if nothing has been typed since the
 * last call. Arrow keys are read as ANSI escape sequences (ESC [ A/B/C/D). */
input_event_t input_poll(void);

#endif
