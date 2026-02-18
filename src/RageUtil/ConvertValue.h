#ifndef RAGEUTIL_CONVERTVALUE_H
#define RAGEUTIL_CONVERTVALUE_H

/* Helper for ConvertValue(). */
template <typename TO, typename FROM>
struct ConvertValueHelper {
  explicit ConvertValueHelper(FROM* pVal) : m_pFromValue(pVal) {
    m_ToValue = static_cast<TO>(*m_pFromValue);
  }

  ~ConvertValueHelper() { *m_pFromValue = static_cast<FROM>(m_ToValue); }

  TO& operator*() { return m_ToValue; }
  operator TO*() { return &m_ToValue; }

 private:
  FROM* m_pFromValue;
  TO m_ToValue;
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