// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAILSERVER_H
#define BITCOIN_MAILSERVER_H

/** Start mail server subsystem.
 */
bool StartMailServer();

/** Interrupt mail server subsystem.
 */
void InterruptMailServer();

/** Stop mail server subsystem.
 */
void StopMailServer();

#endif//BITCOIN_MAILSERVER_H
