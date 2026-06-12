#include "../../disco/topo/fd_topo.h"

#include "generated/fd_admin_tile_seccomp.h"

struct fd_admin_tile_ctx {
  ulong unused;
};

typedef struct fd_admin_tile_ctx fd_admin_tile_ctx_t;

FD_FN_CONST static inline ulong
scratch_align( void ) {
  return alignof(fd_admin_tile_ctx_t);
}

FD_FN_PURE static inline ulong
scratch_footprint( fd_topo_tile_t const * tile ) {
  (void)tile;
  return sizeof(fd_admin_tile_ctx_t);
}

static void
privileged_init( fd_topo_t const *      topo,
                 fd_topo_tile_t const * tile ) {
  (void)topo;
  (void)tile;
}

static void
unprivileged_init( fd_topo_t const *      topo,
                   fd_topo_tile_t const * tile ) {
  (void)topo;
  (void)tile;
}

static inline void
after_credit( fd_admin_tile_ctx_t * ctx,
              fd_stem_context_t *   stem,
              int *                 opt_poll_in,
              int *                 charge_busy ) {
  (void)ctx;
  (void)stem;
  (void)opt_poll_in;
  (void)charge_busy;
}

static ulong
populate_allowed_seccomp( fd_topo_t const *      topo,
                          fd_topo_tile_t const * tile,
                          ulong                  out_cnt,
                          struct sock_filter *   out ) {
  (void)topo;
  (void)tile;

  populate_sock_filter_policy_fd_admin_tile( out_cnt, out, (uint)fd_log_private_logfile_fd() );
  return sock_filter_policy_fd_admin_tile_instr_cnt;
}

static ulong
populate_allowed_fds( fd_topo_t const *      topo,
                      fd_topo_tile_t const * tile,
                      ulong                  out_fds_cnt,
                      int *                  out_fds ) {
  (void)topo;
  (void)tile;

  if( FD_UNLIKELY( out_fds_cnt<2UL ) ) FD_LOG_ERR(( "out_fds_cnt %lu", out_fds_cnt ));

  ulong out_cnt = 0UL;
  out_fds[ out_cnt++ ] = 2; /* stderr */
  if( FD_LIKELY( -1!=fd_log_private_logfile_fd() ) )
    out_fds[ out_cnt++ ] = fd_log_private_logfile_fd(); /* logfile */
  return out_cnt;
}

#define STEM_BURST (1UL)
#define STEM_LAZY  ((long)1e6) /* 1ms */

#define STEM_CALLBACK_CONTEXT_TYPE  fd_admin_tile_ctx_t
#define STEM_CALLBACK_CONTEXT_ALIGN alignof(fd_admin_tile_ctx_t)

#define STEM_CALLBACK_AFTER_CREDIT after_credit

#include "../../disco/stem/fd_stem.c"

fd_topo_run_tile_t fd_tile_admin = {
  .name                     = "admin",
  .populate_allowed_seccomp = populate_allowed_seccomp,
  .populate_allowed_fds     = populate_allowed_fds,
  .scratch_align            = scratch_align,
  .scratch_footprint        = scratch_footprint,
  .privileged_init          = privileged_init,
  .unprivileged_init        = unprivileged_init,
  .run                      = stem_run,
};
