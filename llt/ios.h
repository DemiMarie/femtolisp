#ifndef __IOS_H_
#define __IOS_H_

#include <stdarg.h>

// this flag controls when data actually moves out to the underlying I/O
// channel. memory streams are a special case of this where the data
// never moves out.
typedef enum { bm_none, bm_line, bm_block, bm_mem } bufmode_t;

typedef enum { bst_none, bst_rd, bst_wr } bufstate_t;

#define IOS_INLSIZE 54
#define IOS_BUFSIZE 131072

typedef struct {
    bufmode_t bm;

    // the state only indicates where the underlying file position is relative
    // to the buffer. reading: at the end. writing: at the beginning.
    // in general, you can do any operation in any state.
    bufstate_t state;

    int errcode;

    char *buf;        // start of buffer
    size_t maxsize;   // space allocated to buffer
    size_t size;      // length of valid data in buf, >=ndirty
    size_t bpos;      // current position in buffer
    size_t ndirty;    // # bytes at &buf[0] that need to be written

    off_t fpos;       // cached file pos
    size_t lineno;    // current line number

    // pointer-size integer to support platforms where it might have
    // to be a pointer
    long fd;

    unsigned char readonly:1;
    unsigned char ownbuf:1;
    unsigned char ownfd:1;
    unsigned char _eof:1;

    // this means you can read, seek back, then read the same data
    // again any number of times. usually only true for files and strings.
    unsigned char rereadable:1;

    // this enables "stenciled writes". you can alternately write and
    // seek without flushing in between. this performs read-before-write
    // to populate the buffer, so "rereadable" capability is required.
    // this is off by default.
    //unsigned char stenciled:1;

    // request durable writes (fsync)
    // unsigned char durable:1;

    // todo: mutex
    char local[IOS_INLSIZE];
} ios_t;

/* low-level interface functions */
size_t ios_read(ios_t *s, char *dest, size_t n);
size_t ios_readall(ios_t *s, char *dest, size_t n);
size_t ios_write(ios_t *s, const char *data, size_t n);
off_t ios_seek(ios_t *s, off_t pos);   // absolute seek
off_t ios_seek_end(ios_t *s);
off_t ios_skip(ios_t *s, off_t offs);  // relative seek
off_t ios_pos(ios_t *s);  // get current position
size_t ios_trunc(ios_t *s, size_t size);
int ios_eof(ios_t *s);
int ios_flush(ios_t *s);
void ios_close(ios_t *s);
char *ios_takebuf(ios_t *s, size_t *psize);  // release buffer to caller
// set buffer space to use
int ios_setbuf(ios_t *s, char *buf, size_t size, int own);
int ios_bufmode(ios_t *s, bufmode_t mode);
void ios_set_readonly(ios_t *s);
size_t ios_copy(ios_t *to, ios_t *from, size_t nbytes);
size_t ios_copyall(ios_t *to, ios_t *from);
size_t ios_copyuntil(ios_t *to, ios_t *from, char delim);
// ensure at least n bytes are buffered if possible. returns # available.
size_t ios_readprep(ios_t *from, size_t n);
//void ios_lock(ios_t *s);
//int ios_trylock(ios_t *s);
//int ios_unlock(ios_t *s);

/* stream creation */
ios_t *ios_file(ios_t *s, char *fname, int rd, int wr, int create, int trunc);
ios_t *ios_mem(ios_t *s, size_t initsize);
ios_t *ios_str(ios_t *s, char *str);
ios_t *ios_static_buffer(ios_t *s, char *buf, size_t sz);
ios_t *ios_fd(ios_t *s, long fd, int isfile, int own);
// todo: ios_socket
extern ios_t *ios_stdin;
extern ios_t *ios_stdout;
extern ios_t *ios_stderr;
void ios_init_stdstreams();

/* high-level functions - output */
int ios_putnum(ios_t *s, char *data, uint32_t type);
int ios_putint(ios_t *s, int n);
int ios_pututf8(ios_t *s, uint32_t wc);
int ios_putstringz(ios_t *s, char *str, bool_t do_write_nulterm);
int ios_printf(ios_t *s, const char *format, ...);
int ios_vprintf(ios_t *s, const char *format, va_list args);

void hexdump(ios_t *dest, const char *buffer, size_t len, size_t startoffs);

/* high-level stream functions - input */
int ios_getnum(ios_t *s, char *data, uint32_t type);
int ios_getutf8(ios_t *s, uint32_t *pwc);
int ios_peekutf8(ios_t *s, uint32_t *pwc);
int ios_ungetutf8(ios_t *s, uint32_t wc);
int ios_getstringz(ios_t *dest, ios_t *src);
int ios_getstringn(ios_t *dest, ios_t *src, size_t nchars);
int ios_getline(ios_t *s, char **pbuf, size_t *psz);
char *ios_readline(ios_t *s);

// discard data buffered for reading
void ios_purge(ios_t *s);

// seek by utf8 sequence increments
int ios_nextutf8(ios_t *s);
int ios_prevutf8(ios_t *s);

/* stdio-style functions */
#define IOS_EOF (-1)
int ios_putc(int c, ios_t *s);
//wint_t ios_putwc(ios_t *s, wchar_t wc);
int ios_getc(ios_t *s);
int ios_peekc(ios_t *s);
//wint_t ios_getwc(ios_t *s);
int ios_ungetc(int c, ios_t *s);
//wint_t ios_ungetwc(ios_t *s, wint_t wc);
#define ios_puts(str, s) ios_write(s, str, strlen(str))

/*
  With memory streams, mixed reads and writes are equivalent to performing
  sequences of *p++, as either an lvalue or rvalue. File streams behave
  similarly, but other streams might not support this. Using unbuffered
  mode makes this more predictable.

  Note on "unget" functions:
  There are two kinds of functions here: those that operate on sized
  blocks of bytes and those that operate on logical units like "character"
  or "integer". The "unget" functions only work on logical units. There
  is no "unget n bytes". You can only do an unget after a matching get.
  However, data pushed back by an unget is available to all read operations.
  The reason for this is that unget is defined in terms of its effect on
  the underlying buffer (namely, it rebuffers data as if it had been
  buffered but not read yet). IOS reserves the right to perform large block
  operations directly, bypassing the buffer. In such a case data was
  never buffered, so "rebuffering" has no meaning (i.e. there is no
  correspondence between the buffer and the physical stream).

  Single-bit I/O is able to write partial bytes ONLY IF the stream supports
  seeking. Also, line buffering is not well-defined in the context of
  single-bit I/O, so it might not do what you expect.

  implementation notes:
  in order to know where we are in a file, we must ensure the buffer
  is only populated from the underlying stream starting with p==buf.

  to switch from writing to reading: flush, set p=buf, cnt=0
  to switch from reading to writing: seek backwards cnt bytes, p=buf, cnt=0

  when writing: buf starts at curr. physical stream pos, p - buf is how
  many bytes we've written logically. cnt==0

  dirty == (bitpos>0 && state==iost_wr), EXCEPT right after switching from
  reading to writing, where we might be in the middle of a byte without
  having changed it.

  to write a bit: if !dirty, read up to maxsize-(p-buf) into buffer, then
  seek back by the same amount (undo it). write onto those bits. now set
  the dirty bit. in this state, we can bit-read up to the end of the byte,
  then formally switch to the read state using flush.

  design points:
  - data-source independence, including memory streams
  - expose buffer to user, allow user-owned buffers
  - allow direct I/O, don't always go through buffer
  - buffer-internal seeking. makes seeking back 1-2 bytes very fast,
    and makes it possible for sockets where it otherwise wouldn't be
  - tries to allow switching between reading and writing
  - support 64-bit and large files
  - efficient, low-latency buffering
  - special support for utf8
  - type-aware functions with byte-order swapping service
  - position counter for meaningful data offsets with sockets

  theory of operation:

  the buffer is a view of part of a file/stream. you can seek, read, and
  write around in it as much as you like, as if it were just a string.

  we keep track of the part of the buffer that's invalid (written to).
  we remember whether the position of the underlying stream is aligned
  with the end of the buffer (reading mode) or the beginning (writing mode).

  based on this info, we might have to seek back before doing a flush.

  as optimizations, we do no writing if the buffer isn't "dirty", and we
  do no reading if the data will only be overwritten.
*/

#endif
