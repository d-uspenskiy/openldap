/* $OpenLDAP$ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */
/*
 * Copyright (c) 1995 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#include "portable.h"

#include <stdio.h>
#include <ac/socket.h>

#include "ldap_pvt.h"
#include "slap.h"

static int compare_entry(
	Connection *conn,
	Operation *op,
	Entry *e,
	AttributeAssertion *ava );

int
do_compare(
    Connection	*conn,
    Operation	*op
)
{
	Entry *entry = NULL;
	char	*dn = NULL, *ndn=NULL;
	struct berval desc;
	struct berval value;
	struct berval *nvalue;
	AttributeAssertion ava;
	Backend	*be;
	int rc = LDAP_SUCCESS;
	const char *text = NULL;
	int manageDSAit;

	ava.aa_desc = NULL;
	desc.bv_val = NULL;
	value.bv_val = NULL;

#ifdef NEW_LOGGING
	LDAP_LOG(( "operation", LDAP_LEVEL_ENTRY,
		   "do_compare: conn %d\n", conn->c_connid ));
#else
	Debug( LDAP_DEBUG_TRACE, "do_compare\n", 0, 0, 0 );
#endif
	/*
	 * Parse the compare request.  It looks like this:
	 *
	 *	CompareRequest := [APPLICATION 14] SEQUENCE {
	 *		entry	DistinguishedName,
	 *		ava	SEQUENCE {
	 *			type	AttributeType,
	 *			value	AttributeValue
	 *		}
	 *	}
	 */

	if ( ber_scanf( op->o_ber, "{a" /*}*/, &dn ) == LBER_ERROR ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_ERR,
			   "do_compare: conn %d  ber_scanf failed\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "ber_scanf failed\n", 0, 0, 0 );
#endif
		send_ldap_disconnect( conn, op,
			LDAP_PROTOCOL_ERROR, "decoding error" );
		return SLAPD_DISCONNECT;
	}

	if ( ber_scanf( op->o_ber, "{oo}", &desc, &value ) == LBER_ERROR ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_ERR,
			   "do_compare: conn %d  get ava failed\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "do_compare: get ava failed\n", 0, 0, 0 );
#endif
		send_ldap_disconnect( conn, op,
			LDAP_PROTOCOL_ERROR, "decoding error" );
		rc = SLAPD_DISCONNECT;
		goto cleanup;
	}

	if ( ber_scanf( op->o_ber, /*{*/ "}" ) == LBER_ERROR ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_ERR,
			   "do_compare: conn %d  ber_scanf failed\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "ber_scanf failed\n", 0, 0, 0 );
#endif
		send_ldap_disconnect( conn, op,
			LDAP_PROTOCOL_ERROR, "decoding error" );
		rc = SLAPD_DISCONNECT;
		goto cleanup;
	}

	if( ( rc = get_ctrls( conn, op, 1 )) != LDAP_SUCCESS ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_INFO,
			   "do_compare: conn %d  get_ctrls failed\n", conn->c_connid ));
#else
		Debug( LDAP_DEBUG_ANY, "do_compare: get_ctrls failed\n", 0, 0, 0 );
#endif
		goto cleanup;
	} 

	ndn = ch_strdup( dn );

	if( dn_normalize( ndn ) == NULL ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_INFO,
			   "do_compare: conn %d  invalid dn (%s)\n",
			   conn->c_connid, dn ));
#else
		Debug( LDAP_DEBUG_ANY, "do_compare: invalid dn (%s)\n", dn, 0, 0 );
#endif
		send_ldap_result( conn, op, rc = LDAP_INVALID_DN_SYNTAX, NULL,
		    "invalid DN", NULL, NULL );
		goto cleanup;
	}

	rc = slap_bv2ad( &desc, &ava.aa_desc, &text );
	if( rc != LDAP_SUCCESS ) {
		send_ldap_result( conn, op, rc, NULL, text, NULL, NULL );
		goto cleanup;
	}

	rc = value_normalize( ava.aa_desc, SLAP_MR_EQUALITY, &value, &nvalue, &text );
	if( rc != LDAP_SUCCESS ) {
		send_ldap_result( conn, op, rc, NULL, text, NULL, NULL );
		goto cleanup;
	}

	ava.aa_value = nvalue;

	if( strcasecmp( ndn, LDAP_ROOT_DSE ) == 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_ARGS,
			"do_compare: conn %d  dn (%s) attr(%s) value (%s)\n",
			conn->c_connid, dn, ava.aa_desc->ad_cname.bv_val,
			ava.aa_value->bv_val ));
#else
		Debug( LDAP_DEBUG_ARGS, "do_compare: dn (%s) attr (%s) value (%s)\n",
			dn, ava.aa_desc->ad_cname.bv_val, ava.aa_value->bv_val );
#endif

		Statslog( LDAP_DEBUG_STATS,
			"conn=%ld op=%d CMP dn=\"%s\" attr=\"%s\"\n",
			op->o_connid, op->o_opid, dn, ava.aa_desc->ad_cname.bv_val, 0 );

		rc = backend_check_restrictions( NULL, conn, op, NULL, &text ) ;
		if( rc != LDAP_SUCCESS ) {
			send_ldap_result( conn, op, rc, NULL, text, NULL, NULL );
			goto cleanup;
		}

		rc = root_dse_info( conn, &entry, &text );
		if( rc != LDAP_SUCCESS ) {
			send_ldap_result( conn, op, rc, NULL, text, NULL, NULL );
			goto cleanup;
		}

	} else if ( strcasecmp( ndn, SLAPD_SCHEMA_DN ) == 0 ) {
#ifdef NEW_LOGGING
		LDAP_LOG(( "operation", LDAP_LEVEL_ARGS,
			"do_compare: conn %d  dn (%s) attr(%s) value (%s)\n",
			conn->c_connid, dn, ava.aa_desc->ad_cname->bv_val,
			ava.aa_value->bv_val ));
#else
		Debug( LDAP_DEBUG_ARGS, "do_compare: dn (%s) attr (%s) value (%s)\n",
			dn, ava.aa_desc->ad_cname.bv_val, ava.aa_value->bv_val );
#endif

		Statslog( LDAP_DEBUG_STATS,
			"conn=%ld op=%d CMP dn=\"%s\" attr=\"%s\"\n",
			op->o_connid, op->o_opid, dn, ava.aa_desc->ad_cname.bv_val, 0 );

		rc = backend_check_restrictions( NULL, conn, op, NULL, &text ) ;
		if( rc != LDAP_SUCCESS ) {
			send_ldap_result( conn, op, rc, NULL, text, NULL, NULL );
			rc = 0;
			goto cleanup;
		}

		rc = schema_info( &entry, &text );
		if( rc != LDAP_SUCCESS ) {
			send_ldap_result( conn, op, rc, NULL, text, NULL, NULL );
			rc = 0;
			goto cleanup;
		}
	}

	if( entry ) {
		rc = compare_entry( conn, op, entry, &ava );
		entry_free( entry );

		send_ldap_result( conn, op, rc, NULL, text, NULL, NULL );

		if( rc == LDAP_COMPARE_TRUE || rc == LDAP_COMPARE_FALSE ) {
			rc = 0;
		}

		goto cleanup;
	}

	manageDSAit = get_manageDSAit( op );

	/*
	 * We could be serving multiple database backends.  Select the
	 * appropriate one, or send a referral to our "referral server"
	 * if we don't hold it.
	 */
	if ( (be = select_backend( ndn, manageDSAit )) == NULL ) {
		struct berval **ref = referral_rewrite( default_referral,
			NULL, dn, LDAP_SCOPE_DEFAULT );

		send_ldap_result( conn, op, rc = LDAP_REFERRAL,
			NULL, NULL, ref ? ref : default_referral, NULL );

		ber_bvecfree( ref );
		rc = 0;
		goto cleanup;
	}

	/* check restrictions */
	rc = backend_check_restrictions( be, conn, op, NULL, &text ) ;
	if( rc != LDAP_SUCCESS ) {
		send_ldap_result( conn, op, rc,
			NULL, text, NULL, NULL );
		goto cleanup;
	}

	/* check for referrals */
	rc = backend_check_referrals( be, conn, op, dn, ndn );
	if ( rc != LDAP_SUCCESS ) {
		goto cleanup;
	}

#ifdef NEW_LOGGING
	LDAP_LOG(( "operation", LDAP_LEVEL_ARGS,
		   "do_compare: conn %d	 dn (%s) attr(%s) value (%s)\n",
		   conn->c_connid, dn, ava.aa_desc->ad_cname.bv_val,
		   ava.aa_value->bv_val ));
#else
	Debug( LDAP_DEBUG_ARGS, "do_compare: dn (%s) attr (%s) value (%s)\n",
	    dn, ava.aa_desc->ad_cname.bv_val, ava.aa_value->bv_val );
#endif

	Statslog( LDAP_DEBUG_STATS, "conn=%ld op=%d CMP dn=\"%s\" attr=\"%s\"\n",
	    op->o_connid, op->o_opid, dn, ava.aa_desc->ad_cname.bv_val, 0 );


	/* deref suffix alias if appropriate */
	ndn = suffix_alias( be, ndn );

	if ( be->be_compare ) {
		(*be->be_compare)( be, conn, op, dn, ndn, &ava );
	} else {
		send_ldap_result( conn, op, rc = LDAP_UNWILLING_TO_PERFORM,
			NULL, "operation not supported within namingContext", NULL, NULL );
	}

cleanup:
	free( dn );
	free( ndn );
	free( desc.bv_val );
	free( value.bv_val );

	return rc;
}

static int compare_entry(
	Connection *conn,
	Operation *op,
	Entry *e,
	AttributeAssertion *ava )
{
	int rc = LDAP_NO_SUCH_ATTRIBUTE;
	Attribute *a;

	if ( ! access_allowed( NULL, conn, op, e,
		ava->aa_desc, ava->aa_value, ACL_COMPARE ) )
	{	
		return LDAP_INSUFFICIENT_ACCESS;
	}

	for(a = attrs_find( e->e_attrs, ava->aa_desc );
		a != NULL;
		a = attrs_find( a->a_next, ava->aa_desc ))
	{
		rc = LDAP_COMPARE_FALSE;

		if ( value_find_ex( ava->aa_desc,
			SLAP_MR_VALUE_IS_IN_MR_SYNTAX,
			a->a_vals, ava->aa_value ) == 0 )
		{
			rc = LDAP_COMPARE_TRUE;
			break;
		}
	}

	return rc;
}
