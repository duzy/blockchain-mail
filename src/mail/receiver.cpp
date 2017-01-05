// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
//               2016-2017 Duzy Chan
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mail/receiver.h"
#include <boost/filesystem.hpp>

bool mail::MailReceiver::decodeMailboxNotation(const std::string &s, std::string &name, std::string &host, std::string *params)
{
  auto posOpen = s.find('<');
  if (posOpen == std::string::npos) {
    return false;
  }
  auto posClose = s.find('>', posOpen);
  if (posClose == std::string::npos) {
    return false;
  }
  auto posSep = s.find(':', posOpen);
  if (posSep != std::string::npos) {
    // TODO: deal with at-domain, e.g. <@a.foo.org,@b.foo.org:xxx@foo.org>
    //auto t = s.substr(posOpen+1, posSep-posOpen-1);
  } else {
    posSep = posOpen;
  }
  auto posAt = s.find('@', posSep);
  if (posAt == std::string::npos) {
    name = s.substr(posOpen+1, posClose-posSep-1);
  } else {
    name = s.substr(posOpen+1, posAt-posSep-1);
    host = s.substr(posAt+1, posClose-posAt-1);
  }
  if (params) {
    // TODO: decode parameters
  }
  return true;
}

bool mail::MailReceiver::decodeSender()
{
  std::string name, host, params;
  if (decodeMailboxNotation(sender, name, host, &params)) {
    parameters = params;
    sender = name; // TODO: formal format of `sender'
    return true;
  } else {
    parameters.clear();
    sender.clear();
  }
  return false;
}

bool mail::MailReceiver::decodeRecpt()
{
  std::string name, host, params;
  if (decodeMailboxNotation(recpt, name, host, &params)) {
    // TODO: deal with parameters
    recpt = name; // TODO: formal format of `sender'
    return true;
  } else {
    recpt.clear();
  }
  return false;
}

bool mail::MailReceiver::startReading()
{
  namespace fs = boost::filesystem;
  
  if (MailState::CREATING != state || recpt.empty() || sender.empty()) {
    LogPrint("mail", "mail not ready to read\n"
             "Client: %s\nSender: %s\nRecipient: %s\n"
             , domain.c_str(), sender.c_str(), recpt.c_str());
    return false;
  }

  // TODO: check if sender address is in the wallet

  // TODO: specific better mail storage

  boost::filesystem::path path = GetDataDir() / "mail";
  path /= sender; // TODO: check sender address
  path /= recpt; // TODO: check recpt address
  fs::create_directories(path);
  
  path /= "message.txt"; // TODO: append message-id
  
  auto filename(path.string());
  
  LogPrint("mail", "Reading message %s\n"
           "Client: %s\nSender: %s\nRecipient: %s\n"
           , filename.c_str(), domain.c_str()
           , sender.c_str(), recpt.c_str());
  
  file.reset(fopen(filename.c_str(), "w+"));
  if (file) {
    state = MailState::READING;
  } else {
    LogPrint("mail", "cannot write to %s\n"
             "Client: %s\nSender: %s\nRecipient: %s\n"
             , filename.c_str(), domain.c_str(), sender.c_str(), recpt.c_str());
  }
  return MailState::READING == state;
}

std::size_t mail::MailReceiver::write(const char *s, std::size_t sz)
{
  return file ? fwrite(s, 1, sz, file.get()) : 0;
}
