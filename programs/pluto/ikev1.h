#ifndef _IKEV1_H
#define _IKEv1_H

#include "pluto_crypt.h"
#include "ikev1_continuations.h"
#include "dnskey.h"

/* ikev1.c */

extern void init_ikev1(void);

extern void complete_v1_state_transition(struct msg_digest **mdp,
					 stf_status result);
extern void process_v1_packet(struct msg_digest **mdp);

/*
 * IKEv1 functions: that ikev1_main.c provides and ikev1_aggr.c
 * needs.
 */

/* continue with encrypted packet */
extern void process_packet_tail(struct msg_digest **mdp);

extern bool justship_nonce(chunk_t *n,
			   pb_stream *outs, u_int8_t np,
			   const char *name);

/* calls previous two routines */
extern bool ship_nonce(chunk_t *n, struct pluto_crypto_req *r,
		       pb_stream *outs, u_int8_t np,
		       const char *name);

extern notification_t accept_v1_nonce(struct msg_digest *md, chunk_t *dest,
				      const char *name);

extern bool justship_KE(chunk_t *g,
			pb_stream *outs, u_int8_t np);

/* just calls previous two routines now */
extern bool ship_KE(struct state *st,
		    struct pluto_crypto_req *r,
		    chunk_t *g,
		    pb_stream *outs, u_int8_t np);

/* **MAIN MODE FUNCTIONS** in ikev1_main.c */
extern stf_status main_outI1(int whack_sock,
			     struct connection *c,
			     struct state *predecessor,
			     lset_t policy,
			     unsigned long try,
			     enum crypto_importance importance
#ifdef HAVE_LABELED_IPSEC
			     , struct xfrm_user_sec_ctx_ike * uctx
#endif
			     );

extern stf_status aggr_outI1(int whack_sock,
			     struct connection *c,
			     struct state *predecessor,
			     lset_t policy,
			     unsigned long try,
			     enum crypto_importance importance
#ifdef HAVE_LABELED_IPSEC
			     , struct xfrm_user_sec_ctx_ike * uctx
#endif
			     );

extern void ikev1_delete_out(struct state *st);

extern bool decode_peer_id(struct msg_digest *md, bool initiator,
			   bool aggrmode);

extern size_t RSA_sign_hash(struct connection *c,
			    u_char sig_val[RSA_MAX_OCTETS],
			    const u_char *hash_val, size_t hash_len);

extern size_t                           /* length of hash */
main_mode_hash(struct state *st,
	       u_char *hash_val,        /* resulting bytes */
	       bool hashi,              /* Initiator? */
	       const pb_stream *idpl);  /* ID payload, as PBS; cur must be at end */

enum key_oppo_step {
	kos_null,
	kos_his_txt
#ifdef USE_KEYRR
	, kos_his_key
#endif
};

struct key_continuation {
	struct adns_continuation ac;    /* common prefix */
	struct msg_digest   *md;
	enum   key_oppo_step step;
	bool failure_ok;
	err_t last_ugh;
};

typedef stf_status (key_tail_fn)(struct msg_digest *md,
				 struct key_continuation *kc);

extern void key_continue(struct adns_continuation *cr,
			 err_t ugh,
			 key_tail_fn *tail);

extern stf_status oakley_id_and_auth(struct msg_digest *md,
				     bool initiator,                    /* are we the Initiator? */
				     bool aggrmode,                     /* aggressive mode? */
				     cont_fn_t cont_fn,                 /* continuation function */
				     const struct key_continuation *kc  /* current state, can be NULL */
				     );

static inline stf_status aggr_id_and_auth(struct msg_digest *md,
					  bool initiator,               /* are we the Initiator? */
					  cont_fn_t cont_fn,            /* continuation function */
					  struct key_continuation *kc)  /* argument */
{
	return oakley_id_and_auth(md, initiator, TRUE, cont_fn, kc);
}

#endif
