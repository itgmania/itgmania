#ifndef RAGEUTIL_CONVERTVALUE_H
#define RAGEUTIL_CONVERTVALUE_H

/* Helper for ConvertValue(). */
template <typename TO, typename FROM>
struct ConvertValueHelper {
  explicit ConvertValueHelper(FROM* pVal) : from_value_(pVal) {
    to_value_ = static_cast<TO>(*from_value_);
  }

  ~ConvertValueHelper() { *from_value_ = static_cast<FROM>(to_value_); }

  TO& operator*() { return to_value_; }
  operator TO*() { return &to_value_; }

 private:
  FROM* from_value_;
  TO to_value_;
};

/* Safely temporarily convert between types.  For example,
 *
 * float f = 10.5;
 * *ConvertValue<int>(&f) = 12;
 */
template <typename TO, typename FROM>
ConvertValueHelper<TO, FROM> ConvertValue(FROM* pValue) {
  return ConvertValueHelper<TO, FROM>(pValue);
}

#endif