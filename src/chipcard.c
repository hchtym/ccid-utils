/*
 * This file is part of cci-utils
 * Copyright (c) 2008 Gianni Tedesco <gianni@scaramanga.co.uk>
 * Released under the terms of the GNU GPL version 3
 *
 * Interface to a chipcard interface slot.
*/

#include <ccid.h>

#include "ccid-internal.h"

/** Retrieve cached chip card status.
 * \ingroup g_chipcard
 *
 * @param cc \ref chipcard_t to query.
 *
 * Retrieve chip card status as of last transaction. Generates no traffic
 * accross physical bus to CCID.
 *
 * @return one of CHIPCARD_(ACTIVE|PRESENT|NOT_PRESENT).
 */
unsigned int chipcard_slot_status(chipcard_t cc)
{
	return cc->cc_status;
}

/** Retrieve chip card status.
 * \ingroup g_chipcard
 *
 * @param cc \ref chipcard_t to query.
 *
 * Query CCID for status of clock in relevant chip card slot.
 *
 * @return one of CHIPCARD_CLOCK_(START|STOP|STOP_L|STOP_H).
 */
unsigned int chipcard_clock_status(chipcard_t cc)
{
	struct _cci *cci = cc->cc_parent;

	if ( !_PC_to_RDR_GetSlotStatus(cci, cc->cc_idx, cci->cci_xfr) )
		return CHIPCARD_CLOCK_ERR;

	if ( !_RDR_to_PC(cci, cc->cc_idx, cci->cci_xfr) )
		return CHIPCARD_CLOCK_ERR;

	return _RDR_to_PC_SlotStatus(cci, cci->cci_xfr);
}

/** Power on a chip card slot.
 * \ingroup g_chipcard
 *
 * @param cc \ref chipcard_t to power on.
 * @param voltage Voltage selector.
 * @param atr_len Pointer to size_t to retrieve length of ATR message.
 *
 * @return NULL for failure, pointer to ATR message otherwise.
 */
const uint8_t *chipcard_slot_on(chipcard_t cc, unsigned int voltage,
				size_t *atr_len)
{
	struct _cci *cci = cc->cc_parent;

	if ( !_PC_to_RDR_IccPowerOn(cci, cc->cc_idx, cci->cci_xfr, voltage) )
		return 0;

	if ( !_RDR_to_PC(cci, cc->cc_idx, cci->cci_xfr) )
		return 0;
	
	_RDR_to_PC_DataBlock(cci, cci->cci_xfr);
	if ( atr_len )
		*atr_len = cci->cci_xfr->x_rxlen;
	return cci->cci_xfr->x_rxbuf;
}

/** Perform a chip card transaction.
 * \ingroup g_chipcard
 *
 * @param cc \ref chipcard_t for this transaction.
 * @param xfr \ref xfr_t representing the transfer buffer.
 *
 * Transactions consist of a transmit followed by a recieve.
 *
 * @return zero on failure.
 */
int chipcard_transact(chipcard_t cc, xfr_t xfr)
{
	struct _cci *cci = cc->cc_parent;

	if ( !_PC_to_RDR_XfrBlock(cci, cc->cc_idx, xfr) )
		return 0;

	if ( !_RDR_to_PC(cci, cc->cc_idx, xfr) )
		return 0;

	_RDR_to_PC_DataBlock(cci, xfr);
	return 1;
}

/** Power off a chip card slot.
 * \ingroup g_chipcard
 *
 * @param cc \ref chipcard_t to power off.
 *
 * @return zero on failure.
 */
int chipcard_slot_off(chipcard_t cc)
{
	struct _cci *cci = cc->cc_parent;

	if ( !_PC_to_RDR_IccPowerOff(cci, cc->cc_idx, cci->cci_xfr) )
		return 0;

	if ( !_RDR_to_PC(cci, cc->cc_idx, cci->cci_xfr) )
		return 0;
	
	return _RDR_to_PC_SlotStatus(cci, cci->cci_xfr);
}

/** Wait for insertion of a chip card in to the slot.
 * \ingroup g_chipcard
 *
 * @param cc \ref chipcard_t to wait on.
 *
 * @return Always succeeds and returns 1.
 */
int chipcard_wait_for_card(chipcard_t cc)
{
	struct _cci *cci = cc->cc_parent;

	do {
		_PC_to_RDR_GetSlotStatus(cci, cc->cc_idx, cci->cci_xfr);
		_RDR_to_PC(cci, cc->cc_idx, cci->cci_xfr);
		if ( cc->cc_status != CHIPCARD_NOT_PRESENT )
			break;
		_cci_wait_for_interrupt(cci);
	} while( cc->cc_status == CHIPCARD_NOT_PRESENT );
	return 1;
}

/** Return pointer to CCID to which a chip card slot belongs.
 * \ingroup g_chipcard
 *
 * @param cc \ref chipcard_t to query.
 *
 * @return \ref cci_t representing the CCID which contains the slot cc.
 */
cci_t chipcard_cci(chipcard_t cc)
{
	return cc->cc_parent;
}
