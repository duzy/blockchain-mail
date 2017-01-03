// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mail/server.h"
#include "util.h"
#include <future>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/thread.h>

//
// This implementation compies to https://tools.ietf.org/html/rfc5321.
// 

struct UniqueCStrDeleter
{
  void operator()(char *s) { free(s); }
};

using UniqueCStr = std::unique_ptr<char, UniqueCStrDeleter>;

enum class MailState
{
  CREATING,
  READING,
  DONE,
};

// Maintains a conversation with a client.
struct MailConversation
{
  MailState state;
  std::string domain;
  std::string sender;
  std::string recpt;

  // TODO: encapsulate a mail conversation

  MailConversation()
    : state(MailState::CREATING)
    , sender()
    , recpt()
  {}

  bool isCreating() const { return state == MailState::CREATING; }
  bool isReading() const { return state == MailState::READING; }
  bool isDone() const { return state == MailState::DONE; }
};

static struct event_base *mailEventBase = nullptr;
static struct evconnlistener *mailListener = nullptr;
static std::mutex mailCS;
static std::condition_variable mailCond;
static bool mailRunning = true;

static bool MailEventThread(struct event_base *base)
{
  RenameThread("mail-event");
  LogPrint("mail", "Entering mail event loop\n");
  event_base_dispatch(base);
  LogPrint("mail", "Exited mail event loop\n");
  return event_base_got_break(base) == 0;
}

static void MailTalkIn(struct bufferevent *be, void *pdata)
{
  MailConversation *mailConv = reinterpret_cast<MailConversation*>(pdata);

  // TODO: deal with multiple clients
  (void) mailConv;
  
  auto input = bufferevent_get_input(be);
  assert(input != nullptr); // Should always be valid!

  auto output = bufferevent_get_output(be);
  assert(output != nullptr); // Should always be valid!

  auto len = evbuffer_get_length(input);
  if (len == 0) {
    LogPrint("mail", "zero\n");
    return;
  }

  // TODO: improve mail conversation processing

  if (mailConv->isReading()) {
    std::size_t ll = 0;
    while (true) {
      UniqueCStr s(evbuffer_readln(input, &ll, EVBUFFER_EOL_CRLF));
      LogPrint("mail", "%s\n", s.get());
      if (!s) break;
      if (ll == 1 && strncmp(s.get(), ".", 1) == 0) {
        evbuffer_add_printf(output, "250 OK\r\n");
        mailConv->state = MailState::DONE;
        break;
      }
    }
    return;
  }
  
  evbuffer_ptr ptrbeg, ptrw;
  evbuffer_ptr_set(input, &ptrbeg, 0, EVBUFFER_PTR_SET);

  if (mailConv->isDone()) {
    ptrw = evbuffer_search(input, "QUIT", 4, &ptrbeg);
    if (ptrw.pos == ptrbeg.pos) {
      evbuffer_ptr_set(input, &ptrw, 4, EVBUFFER_PTR_ADD);
      auto ptrend = evbuffer_search_eol(input, &ptrw, nullptr, EVBUFFER_EOL_CRLF);
      if (ptrend.pos == -1) {
        evbuffer_add_printf(output, "500 Bad request\r\n");
        // TODO: error processing, terminate conversation
        if (evbuffer_drain(input, ptrend.pos+2) == -1) {
          LogPrint("mail", "fatal: mail buffer drain failed");
        }
        return;
      }
      if (evbuffer_drain(input, ptrend.pos+2) == -1) {
        LogPrint("mail", "fatal: mail buffer drain failed");
      }
      evbuffer_add_printf(output, "221 %s Service closing transmission channel\r\n", "node-xxx"); // TODO: node name
      // TODO: terminate and cleanup conversation
      LogPrint("mail", "QUIT [%d, %d]\n", ptrw.pos, ptrend.pos);
      return;
    }
  } else if (!mailConv->isCreating()) {
    LogPrint("mail", "invalid state (%d)\n", int(mailConv->state));
    return;
  }
  
  ptrw = evbuffer_search(input, "DATA", 4, &ptrbeg);
  if (ptrw.pos == ptrbeg.pos) {
    evbuffer_ptr_set(input, &ptrw, 4, EVBUFFER_PTR_ADD);
    auto ptrend = evbuffer_search_eol(input, &ptrw, nullptr, EVBUFFER_EOL_CRLF);
    if (ptrend.pos == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      // TODO: error processing, terminate conversation
      if (evbuffer_drain(input, ptrend.pos+2) == -1) {
        LogPrint("mail", "fatal: mail buffer drain failed");
      }
      return;
    }
    if (evbuffer_drain(input, ptrend.pos+2) == -1) {
      LogPrint("mail", "fatal: mail buffer drain failed");
    }
    evbuffer_add_printf(output, "354 Start mail input; end with <CRLF>.<CRLF>\r\n");
    // TODO: switch to data transfer mode
    mailConv->state = MailState::READING;
    LogPrint("mail", "DATA [%d, %d]\n", ptrw.pos, ptrend.pos);
    return;
  }

  ptrw = evbuffer_search(input, "EHLO ", 5, &ptrbeg);
  if (ptrw.pos < 0) {
    ptrw = evbuffer_search(input, "HELO ", 5, &ptrbeg);
  }
  if (ptrw.pos == ptrbeg.pos) {
    evbuffer_ptr_set(input, &ptrw, 5, EVBUFFER_PTR_ADD);
    auto ptrend = evbuffer_search_eol(input, &ptrw, nullptr, EVBUFFER_EOL_CRLF);
    if (ptrend.pos <= ptrw.pos) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      // TODO: error processing, terminate conversation
      if (evbuffer_drain(input, ptrw.pos - ptrbeg.pos) == -1) {
        LogPrint("mail", "fatal: mail buffer drain failed");
      }
      return;
    }
    if (evbuffer_drain(input, ptrw.pos) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    mailConv->domain.resize(ptrend.pos-ptrw.pos);
    if (evbuffer_remove(input, &mailConv->domain[0], ptrend.pos-ptrw.pos) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    if (evbuffer_drain(input, 2) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }

    auto nodeName = "node-xxx";  // TODO: node name
    auto greetString = "hi there"; // TODO: get greet streag from bitcoind.conf
    evbuffer_add_printf(output, "250-%s %s\r\n", nodeName, greetString);
    evbuffer_add_printf(output, "250-%s\r\n", "8BITMIME");
    //evbuffer_add_printf(output, "250-%s\r\n", "SIZE");
    //evbuffer_add_printf(output, "250-%s\r\n", "DSN");
    evbuffer_add_printf(output, "250 %s\r\n", "HELP");
    LogPrint("mail", "EHLO %s\n", mailConv->domain.c_str());
    return;
  }

  ptrw = evbuffer_search(input, "RCPT TO:", 8, &ptrbeg);
  if (ptrw.pos == ptrbeg.pos) {
    evbuffer_ptr_set(input, &ptrw, 8, EVBUFFER_PTR_ADD);
    auto ptrend = evbuffer_search_eol(input, &ptrw, nullptr, EVBUFFER_EOL_CRLF);
    if (ptrend.pos == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      // TODO: error processing, terminate conversation
      if (evbuffer_drain(input, ptrend.pos+2) == -1) {
        LogPrint("mail", "fatal: insufficient mail buffer");
      }
      return;
    }
    if (evbuffer_drain(input, ptrw.pos) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    mailConv->recpt.resize(ptrend.pos-ptrw.pos);
    if (evbuffer_remove(input, &mailConv->recpt[0], ptrend.pos-ptrw.pos) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    if (evbuffer_drain(input, 2) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    
    evbuffer_add_printf(output, "250 %s\r\n", "OK");
    LogPrint("mail", "RCPT TO: %s\n", mailConv->recpt.c_str());
    return;
  }

  ptrw = evbuffer_search(input, "MAIL FROM:", 10, &ptrbeg);
  if (ptrw.pos == ptrbeg.pos) {
    evbuffer_ptr_set(input, &ptrw, 10, EVBUFFER_PTR_ADD);
    auto ptrend = evbuffer_search_eol(input, &ptrw, nullptr, EVBUFFER_EOL_CRLF);
    if (ptrend.pos == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      // TODO: enhanced error process
      if (evbuffer_drain(input, ptrend.pos+2) == -1) {
        LogPrint("mail", "fatal: insufficient mail buffer");
      }
      return;
    }
    if (evbuffer_drain(input, ptrw.pos) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    mailConv->sender.resize(ptrend.pos-ptrw.pos);
    if (evbuffer_remove(input, &mailConv->sender[0], ptrend.pos-ptrw.pos) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    if (evbuffer_drain(input, 2) == -1) {
      evbuffer_add_printf(output, "500 Bad request\r\n");
      LogPrint("mail", "fatal: insufficient mail buffer");
      return;
    }
    evbuffer_add_printf(output, "250 %s\r\n", "OK");
    LogPrint("mail", "MAIL FROM: %s\n", mailConv->sender.c_str());
    return;
  }
}

static void MailTalkEvent(struct bufferevent *be, short what, void *pdata)
{
  MailConversation *mailConv = reinterpret_cast<MailConversation*>(pdata);
  bool finished = false;
  if (what & BEV_EVENT_ERROR) {
    LogPrint("mail", "error\n");
    finished = true;
  }
  if (what & BEV_EVENT_READING) {
    LogPrint("mail", "event: READING\n");
  }
  if (what & BEV_EVENT_WRITING) {
    LogPrint("mail", "event: WRITING\n");
  }
  if (what & BEV_EVENT_TIMEOUT) {
    LogPrint("mail", "event: TIMEOUT\n");
  }
  if (what & BEV_EVENT_EOF) {
    LogPrint("mail", "event: EOF\n");
    finished = true;
  }
  if (finished) {
    bufferevent_free(be);
    delete mailConv;
    return;
  }
}

static void MailDeal(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *addr, int socklen, void *pdata)
{
  // TODO: deal with many conversations

  std::unique_ptr<MailConversation> mailConv(new MailConversation());

  struct event_base *base = evconnlistener_get_base(listener);
  struct bufferevent *conn = bufferevent_socket_new(base, fd, BEV_OPT_CLOSE_ON_FREE/*|BEV_OPT_THREADSAFE*/);
  //bufferevent_setwatermark(conn, EV_READ, 256, 1024);
  //bufferevent_set_timeouts(conn, 10 /*READ*/, 10 /*WRITE*/);
  bufferevent_setcb(conn, MailTalkIn, nullptr, MailTalkEvent, (void*) mailConv.release());
  if (bufferevent_enable(conn, EV_READ|EV_WRITE) != 0) {
    LogPrint("mail", "failed to enable talk buffer\n");
    return;
  }

  char str[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, addr, str, socklen /*INET_ADDRSTRLEN*/);
  LogPrint("mail", "deal %s\n", str);

  auto nodeName = "node-xxx"; // TODO: get the valid node name
  auto caps = ""; // TODO: capabilities

  // TODO: move it to the work queue to initiate the conversation
  auto output = bufferevent_get_output(conn);
  evbuffer_add_printf(output, "220 %s %s\r\n", nodeName, caps);
}

static void MailError(struct evconnlistener *listener, void *pdata)
{
  int err = EVUTIL_SOCKET_ERROR();
  LogPrint("mail", "error: %s (%d)\n", evutil_socket_error_to_string(err), err);
}

static void MailParseThread()
{
  while (mailRunning) {
    {
      std::unique_lock<std::mutex> lock(mailCS);
      if (true) mailCond.wait(lock);
    }
    // TODO: process mails in a loop until interrupted
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

static std::thread mailEventThread;
static std::future<bool> mailThreadResult;

bool StartMailServer()
{
  LogPrintf("Starting mail server\n");
  assert(mailEventBase == nullptr);
  assert(mailListener == nullptr);
#ifdef WIN32
  evthread_use_windows_threads();
#else
  evthread_use_pthreads();
#endif
  mailEventBase = event_base_new();
  if (mailEventBase == nullptr) {
    LogPrintf("mail: unable to create event_base\n");
    return false;
  }

  // TODO: using -mailbind and -mailport (25)
  // TODO: allow multiple -mailbind for multiple interfaces
  // TODO: IPv6 any interface "[::]:8125"
  const std::string bindaddr("0.0.0.0:8125"); // bind to any

  struct sockaddr_storage addr;
  int socklen = sizeof(addr);
  // TODO: using -mailbind and -mailport (25)
  if (evutil_parse_sockaddr_port(bindaddr.c_str(), (struct sockaddr*)&addr, &socklen) != 0) {
    LogPrintf("mail: unable to parse mail address (%s)\n", bindaddr.c_str());
    return false;
  }
  mailListener = evconnlistener_new_bind(mailEventBase, &MailDeal, nullptr, 
    LEV_OPT_REUSEABLE/*|LEV_OPT_THREADSAFE*/|LEV_OPT_CLOSE_ON_FREE,
    -1, (struct sockaddr*)&addr, socklen);
  if (mailListener == nullptr) {
    LogPrintf("mail: unable to create mail listener\n");
    return false;
  }
  evconnlistener_set_error_cb(mailListener, MailError);

  std::packaged_task<bool(struct event_base*)> task(MailEventThread);
  mailThreadResult = task.get_future();
  mailEventThread = std::thread(std::move(task), mailEventBase);
  // TODO: multiple mail worker threads
  std::thread mailParseThread(&MailParseThread);
  mailParseThread.detach();
  return true;
}

void InterruptMailServer()
{
  LogPrintf("Interrupting mail server\n");
  std::unique_lock<std::mutex> lock(mailCS);
  mailRunning = false;
  mailCond.notify_all();
  // TODO: more interruption work
}

void StopMailServer()
{
  LogPrintf("Stopping mail server\n");
  // TODO: emit quit signal to all mail workers
  if (mailEventBase) {
    LogPrint("mail", "Waiting for mail event thread to exit\n");
    if (mailThreadResult.valid() && mailThreadResult.wait_for(std::chrono::milliseconds(2000)) == std::future_status::timeout) {
      LogPrintf("Mail event loop did not exit within allotted time, sending loopbreak\n");
      event_base_loopbreak(mailEventBase);
    }
    mailEventThread.join();
  }
  if (mailListener) {
    evconnlistener_free(mailListener);
    mailListener = nullptr;
  }
  if (mailEventBase) {
    event_base_free(mailEventBase);
    mailEventBase = nullptr;
  }
}
