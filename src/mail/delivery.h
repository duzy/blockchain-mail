// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
//               2016-2017 Duzy Chan
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIL_DELIVERY_H
#define BITCOIN_MAIL_DELIVERY_H 1
#include "mail/utilmail.h"
#include <string>

namespace mail
{
  
  void deliver(const std::string &sender, const std::string &recpt);

} // namespace mail

#endif//BITCOIN_MAIL_DELIVERY_H
