#include "fd_vm.h"
#include "../../ballet/sbpf/fd_sbpf_instr.h"
#include "../../ballet/sbpf/fd_sbpf_opcodes.h"

ulong
fd_vm_trace_align( void ) {
  return 8UL;
}

ulong
fd_vm_trace_footprint( ulong event_max ) {
  if( FD_UNLIKELY( event_max>(1UL<<60) ) ) return 0UL; /* FIXME: tune these bounds */
  return fd_ulong_align_up( sizeof(fd_vm_trace_t) + event_max, 8UL );
}

void *
fd_vm_trace_new( void * shmem,
                 ulong  event_max ) {
  fd_vm_trace_t * trace = (fd_vm_trace_t *)shmem;

  if( FD_UNLIKELY( !trace ) ) {
    FD_LOG_WARNING(( "NULL shmem" ));
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)shmem, fd_vm_trace_align() ) ) ) {
    FD_LOG_WARNING(( "misaligned shmem" ));
    return NULL;
  }

  ulong footprint = fd_vm_trace_footprint( event_max );
  if( FD_UNLIKELY( !footprint ) ) {
    FD_LOG_WARNING(( "bad event_max" ));
    return NULL;
  }

  memset( trace, 0, footprint );

  trace->event_max = event_max;
  trace->event_sz  = 0UL;

  FD_COMPILER_MFENCE();
  FD_VOLATILE( trace->magic ) = FD_VM_TRACE_MAGIC;
  FD_COMPILER_MFENCE();

  return trace;
}

fd_vm_trace_t *
fd_vm_trace_join( void * _trace ) {
  fd_vm_trace_t * trace = (fd_vm_trace_t *)_trace;

  if( FD_UNLIKELY( !trace ) ) {
    FD_LOG_WARNING(( "NULL _trace" ));
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)_trace, fd_vm_trace_align() ) ) ) {
    FD_LOG_WARNING(( "misaligned _trace" ));
    return NULL;
  }

  if( FD_UNLIKELY( trace->magic!=FD_VM_TRACE_MAGIC ) ) {
    FD_LOG_WARNING(( "bad magic" ));
    return NULL;
  }

  return trace;
}

void *
fd_vm_trace_leave( fd_vm_trace_t * trace ) {

  if( FD_UNLIKELY( !trace ) ) {
    FD_LOG_WARNING(( "NULL trace" ));
    return NULL;
  }

  return (void *)trace;
}

void *
fd_vm_trace_delete( void * _trace ) {
  fd_vm_trace_t * trace = (fd_vm_trace_t *)_trace;

  if( FD_UNLIKELY( !trace ) ) {
    FD_LOG_WARNING(( "NULL _trace" ));
    return NULL;
  }

  if( FD_UNLIKELY( !fd_ulong_is_aligned( (ulong)_trace, fd_vm_trace_align() ) ) ) {
    FD_LOG_WARNING(( "misaligned _trace" ));
    return NULL;
  }

  if( FD_UNLIKELY( trace->magic!=FD_VM_TRACE_MAGIC ) ) {
    FD_LOG_WARNING(( "bad magic" ));
    return NULL;
  }

  FD_COMPILER_MFENCE();
  FD_VOLATILE( trace->magic ) = 0UL;
  FD_COMPILER_MFENCE();

  return (void *)trace;
}

int
fd_vm_trace_event_exe( fd_vm_trace_t * trace,
                       ulong           pc,
                       ulong           ic,
                       ulong           cu,
                       ulong           reg[ FD_VM_REG_CNT ],
                       ulong const *   text,
                       ulong           text_cnt,
                       ulong           ic_correction,
                       ulong           frame_cnt ) {

  /* Acquire event storage */

  if( FD_UNLIKELY( (!trace) | (!reg) | (!text) | (!text_cnt) ) ) return FD_VM_ERR_INVAL;

  ulong text0     = text[0];
  int   multiword = (text_cnt>1UL) & (fd_sbpf_instr( text0 ).opcode.any.op_class==FD_SBPF_OPCODE_CLASS_LD);

  ulong event_footprint = sizeof(fd_vm_trace_event_exe_t) - fd_ulong_if( !multiword, 8UL, 0UL );

  ulong event_sz  = trace->event_sz;
  ulong event_rem = trace->event_max - event_sz;
  if( FD_UNLIKELY( event_footprint > event_rem ) ) return FD_VM_ERR_FULL;

  fd_vm_trace_event_exe_t * event = (fd_vm_trace_event_exe_t *)((ulong)(trace+1) + event_sz);

  trace->event_sz = event_sz + event_footprint;

  /* Record the event */

  event->multiword = (ulong)multiword;
  event->pc        = pc;
  event->ic        = ic;
  event->cu        = cu;
  memcpy( event->reg, reg, FD_VM_REG_CNT*sizeof(ulong) );
  event->text[0] = text0;
  event->ic_correction = ic_correction;
  event->frame_cnt = frame_cnt;
  if( FD_UNLIKELY( multiword ) ) event->text[1] = text[1];

  return FD_VM_SUCCESS;
}



#include <stdio.h>

#define FD_VM_TRACE_OUT_BUF_SZ (4096UL)

typedef struct {
  char  buf[ FD_VM_TRACE_OUT_BUF_SZ ];
  ulong buf_sz;
} fd_vm_trace_out_t;

static int
fd_vm_trace_out_flush( fd_vm_trace_out_t * out ) {
  ulong buf_sz = out->buf_sz;
  if( FD_UNLIKELY( !buf_sz ) ) return FD_VM_SUCCESS;

  if( FD_UNLIKELY( fwrite( out->buf, 1UL, buf_sz, stdout )!=buf_sz ) ) return FD_VM_ERR_IO;

  out->buf_sz = 0UL;
  return FD_VM_SUCCESS;
}

static int
fd_vm_trace_out_write( fd_vm_trace_out_t * out,
                       void const *        _data,
                       ulong               data_sz ) {
  char const * data = (char const *)_data;

  while( data_sz ) {
    ulong rem = FD_VM_TRACE_OUT_BUF_SZ - out->buf_sz;
    if( FD_UNLIKELY( !rem ) ) {
      int err = fd_vm_trace_out_flush( out );
      if( FD_UNLIKELY( err ) ) return err;
      rem = FD_VM_TRACE_OUT_BUF_SZ;
    }

    ulong chunk_sz = fd_ulong_min( data_sz, rem );
    memcpy( out->buf + out->buf_sz, data, chunk_sz );
    out->buf_sz += chunk_sz;
    data        += chunk_sz;
    data_sz     -= chunk_sz;
  }

  return FD_VM_SUCCESS;
}

static int
fd_vm_trace_out_cstr( fd_vm_trace_out_t * out,
                      char const *        cstr ) {
  return fd_vm_trace_out_write( out, cstr, strlen( cstr ) );
}

static int
fd_vm_trace_out_char( fd_vm_trace_out_t * out,
                      char                c ) {
  return fd_vm_trace_out_write( out, &c, 1UL );
}

static int
fd_vm_trace_out_repeat( fd_vm_trace_out_t * out,
                        char                c,
                        ulong               cnt ) {
  char buf[ 64 ];
  memset( buf, c, sizeof(buf) );

  while( cnt ) {
    ulong chunk_sz = fd_ulong_min( cnt, sizeof(buf) );
    int err = fd_vm_trace_out_write( out, buf, chunk_sz );
    if( FD_UNLIKELY( err ) ) return err;
    cnt -= chunk_sz;
  }

  return FD_VM_SUCCESS;
}

static int
fd_vm_trace_out_ulong_dec( fd_vm_trace_out_t * out,
                           ulong               x,
                           ulong               width ) {
  char  buf[ 32 ];
  char * end = buf + sizeof(buf);
  char * p   = end;

  do {
    ulong d = x % 10UL;
    x /= 10UL;
    *(--p) = (char)( d + (ulong)'0' );
  } while( x );

  ulong digit_cnt = (ulong)( end - p );
  if( FD_UNLIKELY( digit_cnt<width ) ) {
    int err = fd_vm_trace_out_repeat( out, ' ', width - digit_cnt );
    if( FD_UNLIKELY( err ) ) return err;
  }

  return fd_vm_trace_out_write( out, p, digit_cnt );
}

static int
fd_vm_trace_out_int_dec( fd_vm_trace_out_t * out,
                         int                 x ) {
  ulong ux;
  if( FD_UNLIKELY( x<0 ) ) {
    int err = fd_vm_trace_out_char( out, '-' );
    if( FD_UNLIKELY( err ) ) return err;
    ux = (ulong)(-(long)x);
  } else {
    ux = (ulong)x;
  }

  return fd_vm_trace_out_ulong_dec( out, ux, 0UL );
}

static int
fd_vm_trace_out_ulong_hex( fd_vm_trace_out_t * out,
                           ulong               x,
                           ulong               width ) {
  static char const hex[ 16 ] = {
    '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
  };

  char  buf[ 16 ];
  char * end = buf + sizeof(buf);
  char * p   = end;

  do {
    ulong d = x & 0xFUL;
    x >>= 4;
    *(--p) = hex[ d ];
  } while( x );

  ulong digit_cnt = (ulong)( end - p );
  if( FD_UNLIKELY( digit_cnt<width ) ) {
    int err = fd_vm_trace_out_repeat( out, '0', width - digit_cnt );
    if( FD_UNLIKELY( err ) ) return err;
  }

  return fd_vm_trace_out_write( out, p, digit_cnt );
}

int
fd_vm_trace_printf( fd_vm_trace_t const *      trace,
                    fd_sbpf_syscalls_t const * syscalls ) {

  if( FD_UNLIKELY( !trace ) ) {
    FD_LOG_WARNING(( "bad input args" ));
    return FD_VM_ERR_INVAL;
  }

  fd_vm_trace_out_t out[1] = {{ .buf_sz = 0UL }};

#define OUT( expr )       do { int _err = (expr); if( FD_UNLIKELY( _err ) ) return _err; } while(0)
#define OUT_TEXT( text )  OUT( fd_vm_trace_out_write( out, (text), sizeof(text)-1UL ) )
#define OUT_CSTR( cstr )  OUT( fd_vm_trace_out_cstr( out, (cstr) ) )
#define OUT_CHAR( c )     OUT( fd_vm_trace_out_char( out, (c) ) )
#define OUT_DEC( x, w )   OUT( fd_vm_trace_out_ulong_dec( out, (x), (w) ) )
#define OUT_HEX( x, w )   OUT( fd_vm_trace_out_ulong_hex( out, (x), (w) ) )

  uchar const * ptr = fd_vm_trace_event   ( trace ); /* Note: this point is 8 byte aligned */
  ulong         rem = fd_vm_trace_event_sz( trace );
  while( rem ) {

    if( FD_UNLIKELY( rem < sizeof( fd_vm_trace_event_exe_t ) - 8UL ) ) {
      FD_LOG_WARNING(( "truncated event (exe)" ));
      return FD_VM_ERR_IO;
    }

    fd_vm_trace_event_exe_t const * event = (fd_vm_trace_event_exe_t const *)ptr;

    int   multiword       = (int)event->multiword;
    ulong event_footprint = sizeof( fd_vm_trace_event_exe_t ) - fd_ulong_if( !multiword, 8UL, 0UL );
    if( FD_UNLIKELY( rem < event_footprint ) ) {
      FD_LOG_WARNING(( "truncated event (exe)" ));
      return FD_VM_ERR_IO;
    }

    ulong event_pc = event->pc;

    /* Pretty print the architectural state before the instruction */

    OUT_DEC( event->ic, 5UL );
    OUT_TEXT( " [" );
    for( ulong reg_idx=0UL; reg_idx<FD_VM_REG_CNT; reg_idx++ ) {
      if( FD_LIKELY( reg_idx ) ) OUT_TEXT( ", " );
      OUT_HEX( event->reg[ reg_idx ], 16UL );
    }
    OUT_TEXT( "] " );
    OUT_DEC( event_pc, 5UL );
    OUT_TEXT( ": " );

    /* Print the instruction */

    ulong out_len = 0UL;
    char  instr[128];
    instr[0] = '\0';
    int err = fd_vm_disasm_instr( event->text, fd_ulong_if( !multiword, 1UL, 2UL ), event_pc, syscalls, instr, 128UL, &out_len );
    if( FD_UNLIKELY( err ) ) {
      OUT_TEXT( "disasm failed (" );
      OUT( fd_vm_trace_out_int_dec( out, err ) );
      OUT_CHAR( '-' );
      OUT_CSTR( fd_vm_strerror( err ) );
      OUT_CHAR( ')' );
    } else {
      OUT( fd_vm_trace_out_write( out, instr, out_len ) );
    }

    /* Print CUs  */

    OUT_CHAR( ' ' );
    OUT_DEC( event->cu, 0UL );
    OUT_CHAR( '\n' );
    OUT( fd_vm_trace_out_flush( out ) );
    if( FD_UNLIKELY( fflush( stdout ) ) ) return FD_VM_ERR_IO;

    ptr += event_footprint;
    rem -= event_footprint;
  }

  OUT( fd_vm_trace_out_flush( out ) );

#undef OUT_HEX
#undef OUT_DEC
#undef OUT_CHAR
#undef OUT_CSTR
#undef OUT_TEXT
#undef OUT

  return FD_VM_SUCCESS;
}
