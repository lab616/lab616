#include <iostream>
#include <map>
#include <vector>
#include <stdio.h>

#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/thread.hpp>

#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sigc++/sigc++.h>

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_queue.h>
#include <tbb/pipeline.h>
#include <tbb/tick_count.h>
#include <tbb/task_scheduler_init.h>
#include <tbb/tbb_allocator.h>

#include "common.hpp"
#include "tbb_config.hpp"


using namespace std;


/* !!!!  See AllTests.cpp for initialization of TBB scheduler.  !!! */

// Various attempts. Not all of them work so use a macro to point to
// the working version / attempt.
#define IMPL case4

static int NThread = tbb::task_scheduler_init::automatic;

static const string aapl("AAPL");
static const string pcln("PCLN");
static const string nflx("NFLX");

//////////////////////////////////////////////////////////////////////
struct Message
{
  enum Type { BID = 0, ASK = 1 };
  Message(Type t) : tc(t) {}
  ~Message() {}
  Type tc;
};

struct Bid : Message
{
  Bid() : Message(BID){}

  int t;
  const string* symbol;
  double price;
  int volume;
};

struct Ask : Message
{
  Ask() : Message(ASK){}

  int t;
  const string* symbol;
  double price;
  int volume;
};

template <typename M>
M* NewInstance(int i, const string* symbol, double price, int vol)
{
  M* m = (TbbPrototype::GetConfig()->tbb_alloc) ?
      static_cast<M*>(tbb::tbb_allocator<M>().allocate(sizeof(M))) :
      new M();

  m->t = i;
  m->symbol = symbol;
  m->price = price;
  m->volume = vol;
  return m;
}

template <typename M> void Delete(M* p)
{
  if (TbbPrototype::GetConfig()->tbb_alloc) {
    tbb::tbb_allocator<M>().deallocate(p, sizeof(M));
  } else {
    delete p;
  }
}

static boost::mutex cout_mutex;

template<typename M> void Print(const string& t, M* m, int verbose = 0)
{
  boost::mutex::scoped_lock lock(cout_mutex);
  if (!verbose && !TbbPrototype::GetConfig()->verbose) {
    return;
  }
  cout << t << "[t=" << m-> t
       << ",symbol=" << *m->symbol
       << ",price=" << m->price
       << ",vol=" << m->volume
       << "]" << endl;
}

//////////////////////////////////////////////////////////////////////
template <typename M> struct Task {
  virtual void operator()(const M& m) = 0;
};

typedef Task<Bid> BidTask;
typedef Task<Ask> AskTask;

class Strategy;
typedef map<string, Strategy*> StrategyMap;


//////////////////////////////////////////////////////////////////////
struct Strategy : public BidTask, public AskTask
{
  Strategy(const string& symbol) : symbol(symbol) {}

  string symbol;

  virtual void operator()(const Bid& bid)
  {
    EXPECT_EQ(symbol, *bid.symbol);
    if (!TbbPrototype::GetConfig()->sleep) {
      sleep(TbbPrototype::GetConfig()->sleep);
    }
    boost::mutex::scoped_lock lock(cout_mutex);
    cout << "Strategy[" << symbol << "]"
         << "@ t=" << bid.t
         << ",s=" << *bid.symbol
         << ",bid=" << bid.price
         << ",v=" << static_cast<float>(bid.volume)
         << endl;
   }

  virtual void operator()(const Ask& ask) {
    EXPECT_EQ(symbol, *ask.symbol);
    if (!TbbPrototype::GetConfig()->sleep) {
      sleep(TbbPrototype::GetConfig()->sleep);
      return;
    }
    boost::mutex::scoped_lock lock(cout_mutex);
    cout << "Strategy[" << symbol << "]"
         << "@ t=" << ask.t
         << ",s=" << *ask.symbol
         << ",ask=" << ask.price
         << ",v=" << static_cast<float>(ask.volume)
         << endl;
   }
};

TEST(TbbPrototype, StrategyTest)
{
  Strategy s(aapl);
  Bid* bid = NewInstance<Bid>(1, &aapl, 100., 20);
  s(*bid);

  Strategy* p = new Strategy(pcln);
  Ask* ask = NewInstance<Ask>(1, &pcln, 200., 30);
  (*p)(*ask);
  delete p;
};

//////////////////////////////////////////////////////////////////////
class TaskFilter : public tbb::filter, NoCopyAndAssign {
 public:
  TaskFilter(const string& stage) :
      tbb::filter(parallel),
      stage_(stage)
  {}

  virtual void* operator()(void* task)
  {
    return IMPL(task);
  }

 private:

  // Various attemtps to implement the operator()
  void* case1(void* task);
  void* case2(void* task);
  void* case3(void* task);
  void* case4(void* task);

  string stage_;
};


struct Callable
{
  ~Callable() {}
  virtual void call() = 0;
};

template <typename M>
class SClosure : public Callable
{
 public:

  SClosure(Strategy* strategy, M* m):
      strategy_(strategy), message_(m) {}
  ~SClosure()
  {
    //    delete message_;
  }

  virtual void call()
  {
    (*strategy_)(*message_);
  }

 private:
  Strategy* strategy_;
  M* message_;
};


#define USE_MUTEX 1

class InputFilter : public tbb::filter, NoCopyAndAssign {
 public:
  InputFilter(const string& id, int events, const StrategyMap& sm,
              tbb::concurrent_queue<Message*>* queue) :
      tbb::filter(serial_in_order),
      id_(id),
      messages_(events), sent_(0),
      strategy_map_(sm),
      queue_(queue), run_(true)
  {}

  InputFilter(const string& id, int events, const StrategyMap& sm) :
      tbb::filter(serial_in_order),
      id_(id),
      messages_(events), sent_(0),
      strategy_map_(sm),
      queue_(NULL), run_(true)
  {}

  ~InputFilter() {}

  void Stop()
  {
    boost::mutex::scoped_lock lock(run_mutex_);
    cout << "********************************** Stopping input filter." << endl;
    run_ = false;
  }

  virtual void* operator()(void* task)
  {
    bool run = run_;

    if (queue_ == NULL) {
      return IMPL(task);
    }
    Message* m = NULL;
    while (!queue_->try_pop(m) && run) {
      // Just loop until popped something or explicitly stopped.
      if (m) break;
    }

    if (!m && !run) return NULL; // No work left and told to stop
    cout << ".";
    switch (m->tc) {
      case Message::BID : {
        Bid* bid = static_cast<Bid*>(m);
        Print<Bid>("  Bid = ", bid);
        Strategy* s = strategy_map_.find(*(bid->symbol))->second;
        CHECK(s);
        SClosure<Bid>* sc = new SClosure<Bid>(s, bid);
        return sc;
      }
      case Message::ASK : {
        Ask* ask = static_cast<Ask*>(m);
        Print<Ask>("  Ask = ", ask);
        Strategy* s = strategy_map_.find(*(ask->symbol))->second;
        CHECK(s);
        SClosure<Ask>* sc = new SClosure<Ask>(s, ask);
        return sc;
      }
    }
    return NULL;
  }

 private:

  // Various attemtps to implement the operator()
  void* case1(void* task);
  void* case2(void* task);
  void* case3(void* task);
  void* case4(void* task);

  string id_;
  int messages_;
  int sent_;
  const StrategyMap& strategy_map_;
  tbb::concurrent_queue<Message*>* queue_;
  bool run_;
  boost::mutex run_mutex_;
};

void CleanUp(map<string, Strategy*>* m)
{
  map<string, Strategy*>::iterator itr;
  for (itr = m->begin(); itr != m->end(); ++itr) {
    delete itr->second;
  }
}

//////////////////////////////////////////////////////////////////////
TEST(TbbPrototype, DISABLED_TestConcurrentQueue)
{
  bool tbb_alloc = TbbPrototype::GetConfig()->tbb_alloc;
  tbb::concurrent_queue<Message*> q;
  for (int i = 0; i < 5; ++i) {
    Bid* bid = NewInstance<Bid>(i, &pcln, 100 + i, i * 10);
    // if we use C-style allocator, then we need to set the field:
    if (tbb_alloc) bid->tc = Message::BID;
    Print<Bid>("Pushing BID", bid, 1);
    q.push(static_cast<Message*>(bid));
    Ask* ask = NewInstance<Ask>(i, &pcln, 100 + i, i * 10);
    if (tbb_alloc) ask->tc = Message::ASK;
    Print<Ask>("Pushing ASK", ask, 1);
    q.push(static_cast<Message*>(ask));
  }

  // Poping
  Message* m = NULL;
  while (true) {
    bool ok = q.try_pop(m);
    if (!ok) { break; }
    switch (m->tc) {
      case Message::BID : {
        Bid* bid = static_cast<Bid*>(m);
        Print<Bid>("  Bid = ", bid);
        Delete<Bid>(bid);
        break;
      }
      case Message::ASK : {
        Ask* ask = static_cast<Ask*>(m);
        Print<Ask>("  Ask = ", ask);
        Delete<Ask>(ask);
        break;
      }
    }
  }
}



//////////////////////////////////////////////////////////////////////
////  Case 1: Using Simple Data Object
void* InputFilter::case1(void* task)
{
  if (++sent_ <= messages_) {
    Bid* bid = NewInstance<Bid>(sent_, &pcln, 1.0, sent_);
    Print<Bid>("--> BID", bid);
    return bid;
  }
  return NULL;
}

void* TaskFilter::case1(void* task)
{
  Bid* bid = static_cast<Bid*>(task);
  Print<Bid>("  1111>>>> BID", bid);
  return task;
}

//////////////////////////////////////////////////////////////////////
////  Case 2: Using Container Message Object with void*
class Event
{
 public:
  enum Type { BID = 0, ASK = 1 };

  ~Event() { /* TODO: leak here. */ }
  Event(Bid* bid) : type_(BID), event_(bid) {}
  Event(Ask* ask) : type_(ASK), event_(ask) {}

 private:
  Type type_;
  void* event_;

 public:
  Type Type() const { return type_; }
  void* Get() const { return event_; }
};

TEST(TbbPrototype, EventClass)
{
  Bid* bid = NewInstance<Bid>(1, &pcln, 100., 10);
  Event event(bid);
  cout << "Event type = " << event.Type() << ", pt = " << event.Get() << endl;
  switch (event.Type()) {
    case Event::BID :
      Print<Bid>(">>>BID", static_cast<Bid*>(event.Get())); break;
    case Event::ASK :
      Print<Ask>(">>>ASK", static_cast<Ask*>(event.Get())); break;
  }
};

void* InputFilter::case2(void* task)
{
  if (++sent_ <= messages_) {
    Bid* bid = NewInstance<Bid>(sent_, &pcln, 1.0, sent_);
    Print<Bid>("--> BID", bid);
    Event* event = new Event(bid);
    return event;
  }
  return NULL;
}

void* TaskFilter::case2(void* task)
{
  Event& event = * static_cast<Event*>(task);
  cout << "Event type = " << event.Type() << ", pt = " << event.Get() << endl;
  switch (event.Type()) {
    case Event::BID :
      Print<Bid>(" 2222>>>>BID", static_cast<Bid*>(event.Get())); break;
    case Event::ASK :
      Print<Ask>( "2222>>>ASK", static_cast<Ask*>(event.Get())); break;
  }
  return task;
}


//////////////////////////////////////////////////////////////////////
////  Case 3: Using StrategyClosure

class StrategyClosure
{
 public:
  enum Type { BID = 0, ASK = 1 };

  StrategyClosure(Strategy* strategy, Type t, void* m):
      strategy_(strategy), t_(t), message_(m) {}
  ~StrategyClosure()
  {
    //    delete message_;
  }

  void call()
  {
    switch (t_) {
      case BID:
        (*strategy_)(* static_cast<Bid*>(message_));
        break;
      case ASK:
        (*strategy_)(* static_cast<Ask*>(message_));
        break;
    }
  }

 private:
  Strategy* strategy_;
  Type t_;
  void* message_;
};

TEST(TbbPrototype, StrategyClosureTest)
{
  Strategy s(aapl);
  Bid* bid = NewInstance<Bid>(1, &aapl, 100., 20);
  s(*bid);

  StrategyClosure sc(&s, StrategyClosure::BID, bid);
  sc.call();

  Strategy* p = new Strategy(pcln);
  Ask* ask = NewInstance<Ask>(1, &pcln, 200., 30);
  (*p)(*ask);

  StrategyClosure* pc = new StrategyClosure(p, StrategyClosure::ASK, ask);
  pc->call();
  delete p;
  delete pc;
};

void* InputFilter::case3(void* task)
{
  if (++sent_ <= messages_) {
    const string* sym = (sent_ % 4 < 2) ? &aapl : &nflx;
    if (sent_ % 2) {
      Bid* bid = NewInstance<Bid>(sent_, sym, 1.0, 10);
      Print<Bid>("--> BID", bid);
      Strategy* s = strategy_map_.find(*sym)->second;
      CHECK(s);
      StrategyClosure* sc = new StrategyClosure(s, StrategyClosure::BID, bid);
      return sc;
    } else {
      Ask* ask = NewInstance<Ask>(sent_, sym, 2.0, 20);
      Print<Ask>("--> ASK", ask);
      Strategy* s = strategy_map_.find(*sym)->second;
      CHECK(s);
      StrategyClosure* sc = new StrategyClosure(s, StrategyClosure::ASK, ask);
      return sc;
    }
  }
  return NULL;
}

void* TaskFilter::case3(void* task)
{
  if (task) {
    cout << task << ".... Stage[" << stage_ << "]:\t\t\t";
    // Simply invoke the task closure.
    StrategyClosure& c = * static_cast<StrategyClosure*>(task);
    c.call();
    return task;
  }
  return NULL;
}


//////////////////////////////////////////////////////////////////////
////  Case 4: Using Generic Closure
TEST(TbbPrototype, CallableTest)
{
  Strategy s(aapl);
  Bid* bid = NewInstance<Bid>(1, &aapl, 100., 20);
  s(*bid);

  SClosure<Bid> sc(&s, bid);
  sc.call();
}

void* InputFilter::case4(void* task)
{
  if (++sent_ <= messages_) {
    const string* sym = (sent_ % 4 < 2) ? &aapl : &nflx;
    if (sent_ % 2) {
      Bid* bid = NewInstance<Bid>(
          sent_, sym, 1.0, TbbPrototype::GetConfig()->ticks - sent_);

      Print<Bid>("--> BID", bid);

      Strategy* s = strategy_map_.find(*sym)->second;
      CHECK(s);
      SClosure<Bid>* sc = new SClosure<Bid>(s, bid);
      return sc;
    } else {
      Ask* ask = NewInstance<Ask>(
          sent_, sym, 2.0, TbbPrototype::GetConfig()->ticks - sent_);

      Print<Ask>("--> ASK", ask);

      Strategy* s = strategy_map_.find(*sym)->second;
      CHECK(s);
      SClosure<Ask>* sc = new SClosure<Ask>(s, ask);
      return sc;
    }
  }
  return NULL;
}

void* TaskFilter::case4(void* task)
{
  if (task) {
    // Simply invoke the task closure.
    Callable& c = * static_cast<Callable*>(task);
    c.call();
    return task;
  }
  return NULL;
}



//////////////////////////////////////////////////////////////////////
//                             TEST                                 //
//////////////////////////////////////////////////////////////////////
TEST(TbbPrototype, DISABLED_Pipeline)
{
  // Set up the strategies
  map<string, Strategy*> strategies;
  strategies["AAPL"] = new Strategy("AAPL");
  strategies["PCLN"] = new Strategy("PCLN");
  strategies["NFLX"] = new Strategy("NFLX");

  InputFilter input("TickSource", TbbPrototype::GetConfig()->ticks, strategies);
  TaskFilter strategy("Strategy");

  tbb::pipeline pipeline;
  pipeline.add_filter(input);
  pipeline.add_filter(strategy);

  cout << "Start..." << endl;
  tbb::tick_count t0 = tbb::tick_count::now();

  pipeline.run(TbbPrototype::GetConfig()->tokens);

  tbb::tick_count t1 = tbb::tick_count::now();
  cout << "Run time = " << (t1 - t0).seconds() << endl;

  CleanUp(&strategies);
}

/////////////////////////////////////////////////////////////////
struct TickGenerator
{
  typedef boost::function<void()> DoneCallback;

  tbb::concurrent_queue<Message*>* queue;
  DoneCallback callback;
  TickGenerator(tbb::concurrent_queue<Message*>* q, DoneCallback cb) :
      queue(q), callback(cb) {}

  void operator()()
  {
    sleep(1);
    Bid* bid; Ask* ask;
    int ticks = TbbPrototype::GetConfig()->ticks;
    for (int i = 0; i < ticks; ++i) {
      for (int j = 0; j < 1000; ++j) {}

      bid = NewInstance<Bid>(i, &aapl, 100 + i, ticks-i);
      queue->push(static_cast<Message*>(bid));

      ask = NewInstance<Ask>(i, &aapl, 100 + i, ticks-i);
      queue->push(static_cast<Message*>(ask));

      bid = NewInstance<Bid>(i, &pcln, 100 + i, ticks-i);
      queue->push(static_cast<Message*>(bid));

      ask = NewInstance<Ask>(i, &pcln, 100 + i, ticks-i);
      queue->push(static_cast<Message*>(ask));
    }
    (callback)();
  }
};

struct DoneCallback{
  InputFilter* input;
  DoneCallback(InputFilter* input) : input(input) {}
  void operator()(void) { input->Stop(); }
};


TEST(TbbPrototype, PipelineWithQueue)
{
  // Set up the strategies
  map<string, Strategy*> strategies;
  strategies["AAPL"] = new Strategy("AAPL");
  strategies["PCLN"] = new Strategy("PCLN");
  strategies["NFLX"] = new Strategy("NFLX");

  tbb::concurrent_queue<Message*> q;
  InputFilter input("TickSource",
                    TbbPrototype::GetConfig()->ticks, strategies, &q);
  TaskFilter strategy("Strategy");

  tbb::pipeline pipeline;
  pipeline.add_filter(input);
  pipeline.add_filter(strategy);

  TickGenerator gen(&q, DoneCallback(&input));
  boost::thread gen_thread(gen);

  cout << "Start..." << endl;
  tbb::tick_count t0 = tbb::tick_count::now();

  pipeline.run(TbbPrototype::GetConfig()->tokens); // Blocks

  tbb::tick_count t1 = tbb::tick_count::now();
  cout << endl;
  cout << "Total = " << (t1 - t0).seconds() << endl;
  cout << "QPS   = "
       << static_cast<float>(
           TbbPrototype::GetConfig()->ticks) / (t1 - t0).seconds();
  cout << " / ms = "
       << (t1 - t0).seconds() / static_cast<float>(
           TbbPrototype::GetConfig()->ticks) * 1000.
       << endl;

  gen_thread.join();
  CleanUp(&strategies);
}

// Test structure
struct Tick
{
  enum TickType { BID = 0, ASK = 1 };
  TickType type;
  int64_t timestamp;
  int32_t id;
  double price;
  const string* symbol;
};

TEST(TbbPrototype, AllocatorTest)
{
  Tick* tick = static_cast<Tick*>(
      tbb::tbb_allocator<Tick>().allocate(sizeof(Tick)));
  EXPECT_TRUE(tick != NULL);

  tick->type = Tick::BID;
  tick->id = 1000;
  tick->timestamp = 11111111;
  tick->price = 99.;
  tick->symbol = &aapl;

  EXPECT_EQ(aapl, *tick->symbol);
  tbb::tbb_allocator<Tick>().deallocate(tick, sizeof(Tick));

  Bid* bid = static_cast<Bid*>(
      tbb::tbb_allocator<Bid>().allocate(sizeof(Bid)));
  bid->symbol = &aapl;
  bid->volume = 0;
  bid->price = 0.;
  bid->t = 1;
  Print<Bid>("==> Found bid:", bid);
  tbb::tbb_allocator<Bid>().deallocate(bid, sizeof(Bid));
}

///////////////////////////////////////////////
// Stream iterator
// See http://software.intel.com/en-us/forums/showthread.php?t=56256
// For use with parallel_do
template<typename Stream>
class tbb_stream_iterator: public std::iterator<std::input_iterator_tag,
                                                typename Stream::value_type>
{
  Stream* my_stream;
  typedef typename Stream::value_type value_type;
  value_type my_item;

 public:

  // Construct input iterator representing end of stream.
  tbb_stream_iterator() : my_stream(NULL) {}

  // Construct input iterator representing front of stream.
  tbb_stream_iterator(Stream& stream) : my_stream(&stream)
  {
    operator++();
  }

  bool operator==(const tbb_stream_iterator& other) const
  {
    return my_stream==other.my_stream;
  }

  const value_type& operator*() const {return my_item;}

  const tbb_stream_iterator& operator++()
  {
    if( !my_stream->pop_if_present(my_item) )
      my_stream = NULL;
    return *this;
  }
};

