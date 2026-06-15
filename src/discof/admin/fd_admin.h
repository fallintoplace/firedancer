#ifndef HEADER_fd_src_discof_admin_fd_admin_h
#define HEADER_fd_src_discof_admin_fd_admin_h

#include "../../tango/cnc/fd_cnc.h"

#define FD_CNC_ADMIN_TYPE (0xF17EDA2C37AD0000UL) /* FIREDANCER ADMIN CNC VER 0 */

/* IMPORTANT: The CNC signal enum ordering must be preserved!  If it is
   not, the interface between commands and the admin tile will break. */
#define FD_CNC_SIGNAL_ADD_AUTH_VOTER (FD_CNC_SIGNAL_HALT+1UL)

struct fd_admin_cnc_add_auth_voter {
  int   result;
  uchar keypair[ 64UL ];
};
typedef struct fd_admin_cnc_add_auth_voter fd_admin_cnc_add_auth_voter_t;

struct fd_admin_cnc {
  union {
    fd_admin_cnc_add_auth_voter_t add_auth_voter;
  };
};

typedef struct fd_admin_cnc fd_admin_cnc_t;

#endif /* HEADER_fd_src_discof_admin_fd_admin_h */
