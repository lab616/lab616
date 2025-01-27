#include <string.h>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ib/adapters.hpp>

using namespace ib::adapter;
using namespace std;

namespace {

enum State {
  OPEN,
  CONNECTED,
  DISCONNECTED
};

class TestLoggingEWrapper : public LoggingEWrapper {
 public :
  TestLoggingEWrapper(int id) :
      LoggingEWrapper::LoggingEWrapper("", 4001, id),
      state_(OPEN)
  {
  }
  ~TestLoggingEWrapper() {}

 public:
  State GetState() { return state_; }
  void nextValidId(OrderId orderId) {
    state_ = CONNECTED;
  }

 private:
  State state_;
};


TEST(AdapterTest, CreateAdapterTest) {
  VLOG(1) << "Starting test.";

  TestLoggingEWrapper wrapper(1U);

  EXPECT_EQ(1U, wrapper.get_connection_id());

  int state = wrapper.GetState();
  EXPECT_EQ(OPEN, state);

  OrderId nextId(10);
  wrapper.nextValidId(nextId);
  EXPECT_EQ(CONNECTED, wrapper.GetState());
}

} // namespace
