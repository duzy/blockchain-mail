// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
//               2016-2017 Duzy Chan
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mail/delivery.h"

void mail::deliver(const std::string &sender, const std::string &recpt)
{
  LogPrint("mail", "deliver %s -> %s", sender.c_str(), recpt.c_str());

  // TODO: machnism for picking the right message (message identification)
  // TODO: encapsulate the message for security

  
}
