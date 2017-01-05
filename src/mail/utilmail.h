// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
//               2016-2017 Duzy Chan
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIL_UTIL_H
#define BITCOIN_MAIL_UTIL_H 1
#include "util.h"
#include <cstdlib>
#include <cstdio>
#include <memory>

struct UniqueCStrDeleter
{
  void operator()(char *s) { free(s); }
};

struct UniqueFileDeleter
{
  void operator()(FILE *f) { fclose(f); }
};

using UniqueCStr = std::unique_ptr<char, UniqueCStrDeleter>;
using UniqueFile = std::unique_ptr<FILE, UniqueFileDeleter>;

#endif//BITCOIN_MAIL_UTIL_H
