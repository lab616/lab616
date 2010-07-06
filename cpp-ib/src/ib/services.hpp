#ifndef IB_SERVICES_H_
#define IB_SERVICES_H_

#include <string>
#include <boost/scoped_ptr.hpp>

using namespace std;

namespace ib {
namespace services {


// Service interface for Requesting Market Data.
class IMarketData
{
 public:
  virtual ~IMarketData() {}

  // Requests tick data for the given symbol.
  // Returns the unique ticker id.
  virtual unsigned int requestTicks(const string& symbol) = 0;

  // Option data
  virtual unsigned int requestOptionData(const string& symbol,
                                         bool call,
                                         const double strike,
                                         const int year, const int month,
                                         const int day) = 0;
};


} // namespace services
} // namespace ib
#endif // IB_SERVICES_H_