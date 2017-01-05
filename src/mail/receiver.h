// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
//               2016-2017 Duzy Chan
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIL_RECEIVER_H
#define BITCOIN_MAIL_RECEIVER_H 1
#include "utilmail.h"
#include <string>

namespace mail
{

  // Indicating the working states of mail subsystem.
  enum class MailState
  {
    CREATING,
    READING,
    DONE,
  };

  // Maintains a conversation with a client.
  struct MailReceiver
  {
    MailState state;
    std::string domain;
    std::string sender;
    std::string recpt;
    std::string parameters;

    UniqueFile file;

    // TODO: encapsulate a mail conversation

    MailReceiver()
      : state(MailState::CREATING)
      , domain()
      , sender()
      , recpt()
      , file()
    {}

    ~MailReceiver()
    {
      LogPrint("mail", "Mail done %s -> %s\n", sender.c_str(), recpt.c_str());
    }

    bool isCreating() const { return state == MailState::CREATING; }
    bool isReading() const { return state == MailState::READING; }
    bool isDone() const { return state == MailState::DONE; }

    static bool decodeMailboxNotation(const std::string &s, std::string &name, std::string &host, std::string *params = nullptr);
    
    bool decodeSender();
    bool decodeRecpt();

    bool startReading();
    std::size_t write(const char *s, std::size_t sz);
  };

} // namespace mail

#endif//BITCOIN_MAIL_RECEIVER_H

