#define _GNU_SOURCE
#include "../../shared/fd_config.h"
#include "../../shared/fd_action.h"

#include "../../../disco/topo/fd_topo.h"
#include "../../../disco/keyguard/fd_keyload.h"
#include "../../../discof/admin/fd_admin.h"
#include "../../../ballet/ed25519/fd_ed25519.h"

#include <limits.h>
#include <strings.h>
#include <unistd.h>

void
add_authorized_voter_cmd_args( int *    pargc,
                               char *** pargv,
                               args_t * args ) {

  if( FD_UNLIKELY( *pargc<1 ) ) {
    FD_LOG_ERR(( "Usage: %s add-authorized-voter <keypair>", FD_BINARY_NAME ));
  }

  char const * path = *pargv[0];
  (*pargc)--;
  (*pargv)++;

  if( FD_UNLIKELY( !strcmp( path, "-" ) ) ) {
    uchar * keypair_wr = fd_keyload_alloc_protected_pages( 1UL, 2UL );
    FD_LOG_STDOUT(( "Reading authorized voter keypair from stdin.  Press Ctrl-D when done.\n" ));
    fd_keyload_read( STDIN_FILENO, "stdin", keypair_wr );
    args->add_authorized_voter.keypair = fd_keyload_mprotect_ro( keypair_wr, 0 );
  } else {
    args->add_authorized_voter.keypair = fd_keyload_load( path, 0 );
  }
}

static void FD_FN_SENSITIVE
add_authorized_voter( args_t *   args,
                      config_t * config ) {

  /* Sanity check on the input. */
  uchar       public_key[ 32 ];
  fd_sha512_t sha512[ 1 ];
  FD_TEST( fd_sha512_join( fd_sha512_new( sha512 ) ) );

  fd_ed25519_public_from_private( public_key, args->add_authorized_voter.keypair, sha512 );
  if( FD_UNLIKELY( memcmp( public_key, args->add_authorized_voter.keypair+32UL, 32UL ) ) ) {
    fd_memzero_explicit( public_key, sizeof(public_key) );
    FD_LOG_ERR(( "The public key in the key file does not match the public key derived from the private key."
                 "Firedancer will not use the key pair to sign as it might leak the private key." ));
  }

  /* Join the CNC object. Once it's open, we can send a request to the
     admin tile.  If another caller has an active command session, we'll
     wait for it to complete.  Otherwise, fail because something is
     wrong. */
  fd_topo_obj_t const * admin_cnc_obj = fd_topo_find_obj( &config->topo, "cnc", "admin", 0UL );
  if( FD_UNLIKELY( !admin_cnc_obj ) ) FD_LOG_ERR(( "admin tile command endpoint not found" ));

  fd_topo_join_workspace( &config->topo, &config->topo.workspaces[ admin_cnc_obj->wksp_id ], FD_SHMEM_JOIN_MODE_READ_WRITE, FD_TOPO_CORE_DUMP_LEVEL_DISABLED );

  fd_cnc_t * cnc = fd_cnc_join( fd_topo_obj_laddr( &config->topo, admin_cnc_obj->id ) );
  if( FD_UNLIKELY( !cnc ) ) FD_LOG_ERR(( "fd_cnc_join failed" ));
  if( FD_UNLIKELY( fd_cnc_type( cnc )!=FD_CNC_ADMIN_TYPE ) ) FD_LOG_ERR(( "unexpected admin cnc type %lu", fd_cnc_type( cnc ) ));

  int err;
  err = fd_cnc_open( cnc );
  while( FD_UNLIKELY( err==FD_CNC_ERR_AGAIN ) ) {
    FD_SPIN_PAUSE();
    err = fd_cnc_open( cnc );
  }
  if( FD_UNLIKELY( err!=FD_CNC_SUCCESS ) ) FD_LOG_ERR(( "add-authorized-voter failed because the command-and-control failed (%s)", fd_cnc_strerror( err ) ));

  /* Copy keypair to the admin cnc and send signal to the admin tile. */
  fd_admin_cnc_t * req = fd_cnc_app_laddr( cnc );
  req->add_auth_voter.result = FD_CNC_ADMIN_ADD_AUTH_VOTER_RESULT_PENDING;
  memcpy( req->add_auth_voter.keypair, args->add_authorized_voter.keypair, 64UL );

  uchar * keypair_wr = fd_keyload_mprotect_wr( args->add_authorized_voter.keypair, 0 );
  fd_memzero_explicit( keypair_wr, 64UL );
  fd_keyload_mprotect_ro( keypair_wr, 0 );

  fd_cnc_signal( cnc, FD_CNC_SIGNAL_ADD_AUTH_VOTER );
  ulong signal = fd_cnc_wait( cnc, FD_CNC_SIGNAL_ADD_AUTH_VOTER, LONG_MAX, NULL );

  int result = req->add_auth_voter.result;
  fd_memzero_explicit( req->add_auth_voter.keypair, 64UL );
  fd_cnc_close( cnc );

  if( FD_UNLIKELY( signal!=FD_CNC_SIGNAL_RUN ) ) FD_LOG_ERR(( "admin tile failed while adding authorized voter, signal %lu", signal ));

  if( FD_UNLIKELY( result!=FD_CNC_ADMIN_ADD_AUTH_VOTER_RESULT_SUCCESS ) ) {
    FD_LOG_WARNING(( "Failed to add authorized voter key, check validator logs for more details" ));
  } else {
    FD_LOG_NOTICE(( "Authorized voter key added" ));
  }
}

void
add_authorized_voter_cmd_fn( args_t *   args,
                             config_t * config ) {
  add_authorized_voter( args, config );
}

static void
add_authorized_voter_args_help( fd_action_help_t * help ) {
  fd_action_help_arg( help, "<keypair>", NULL, "Path to the authorized voter keypair to add, in the standard Solana\n"
                                               "keypair file format (the 64-byte JSON array).  The full keypair is\n"
                                               "required, not just the public key, because the validator must sign\n"
                                               "votes with it.  Pass `-` to read the same JSON array from stdin\n"
                                               "instead of from a file" );
}

action_t fd_action_add_authorized_voter = {
  .name           = "add-authorized-voter",
  .args           = add_authorized_voter_cmd_args,
  .fn             = add_authorized_voter_cmd_fn,
  .require_config = 1,
  .perm           = NULL,
  .description    = "Add an authorized voter to the validator",
  .detail         = "Registers an additional authorized voter key with an already running\n"
                    "validator so it can sign votes with that key, in addition to the identity\n"
                    "key and any voters already configured.  On success it prints `Authorized\n"
                    "voter key added <pubkey>` and exits 0.  It fails (non-zero, with no change)\n"
                    "if the key is already an authorized voter, or if the validator already has\n"
                    "the maximum of 16 authorized voters.\n"
                    "\n"
                    "This command does not start a validator; it attaches to one that is already\n"
                    "running.  It finds the running validator from the shared memory described by\n"
                    "the configuration file, so you must point --config at the SAME config file the\n"
                    "validator was started with, and run it from a binary built from the SAME git\n"
                    "commit (compare this binary's `--version` against the running validator's).  If\n"
                    "the config or binary differ, the layout will not match and the command fails\n"
                    "without changing anything.\n"
                    "\n"
                    "The change is live only: it is not written back to the config file, so the\n"
                    "voter is dropped on the validator's next restart.  To keep it across restarts,\n"
                    "also add the keypair path to [paths.authorized_voter_paths] in the config.",
  .usage          = "add-authorized-voter <keypair>",
  .args_help      = add_authorized_voter_args_help,
};
