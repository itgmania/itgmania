#ifndef STDSTRING_H
#define STDSTRING_H

#include <cstddef>
#include <string>
#include <string_view>

void MakeUpper(char * p, size_t iLen);
void MakeLower(char * p, size_t iLen);
void MakeUpper(wchar_t * p, size_t iLen);
void MakeLower(wchar_t * p, size_t iLen);

#if 0
typedef RString std::string;
#else
class RString {
    std::string s;
    static int tolower(char c) {
        // chars must be converted to unsigned chars before passing to std::tolower
        return std::tolower(static_cast<unsigned char>(c));
    }
    static int toupper(char c) {
        // chars must be converted to unsigned chars before passing to std::tolower
        return std::toupper(static_cast<unsigned char>(c));
    }

  public:
    static const size_t npos = std::string::npos;
    using size_type = std::string::size_type;

    RString() {}
    RString(const char * c) : s(c) {}
    RString(const char * c, size_t len) : s(c, len) {}
    RString(const std::string & s) : s(s) {}
    RString(const std::string_view & s) : s(s) {}
    RString(std::string::const_iterator begin, std::string::const_iterator end) : s(begin, end) {}
    RString(size_t count, char c) : s(count, c) {}
    operator const char *() const { return s.c_str(); }
    operator const std::string &() const { return s; }
    const char * c_str() const { return s.c_str(); }
    const char * data() const { return s.data(); }
    char & front() { return s.front(); }
    bool empty() const { return s.empty(); }
    size_t size() const { return s.size(); }
    size_t length() const { return s.length(); }
    void clear() { s.clear(); }
    char & operator[](size_t i) { return s[i]; }
    bool operator==(const RString & other) const { return s == other.s; }
    bool operator!=(const RString & other) const { return s != other.s; }
    bool operator==(const char * other) const { return s == other; }
    bool operator!=(const char * other) const { return s != other; }
    RString operator+(const RString & other) const { return RString(s + other.s); }
    RString & operator+=(const RString & other) {
        s += other.s;
        return *this;
    }
    RString & operator+=(char c) {
        s += c;
        return *this;
    }
    bool operator<(const RString & other) const { return s < other.s; }
    bool operator<=(const RString & other) const { return s <= other.s; }
    bool operator>(const RString & other) const { return s > other.s; }
    bool operator>=(const RString & other) const { return s >= other.s; }
    std::string::iterator begin() { return s.begin(); }
    std::string::iterator end() { return s.end(); }
    std::string::const_iterator begin() const { return s.begin(); }
    std::string::const_iterator end() const { return s.end(); }
    void erase(size_t pos = 0, size_t len = npos) { s.erase(pos, len); }
    void erase(std::string::iterator p) { s.erase(p); }
    void erase(std::string::iterator first, std::string::iterator last) { s.erase(first, last); }
    void reserve(size_t size) { s.reserve(size); }
    int compare(const RString & other) const {
        return s.compare(other.s);
    }
    int compare(size_t pos, size_t len, const RString & other) const {
        return s.compare(pos, len, other.s);
    }
    RString & append(std::string::const_iterator first, std::string::const_iterator last) {
        s.append(first, last);
        return *this;
    }
    RString & append(const RString & other) {
        s.append(other.s);
        return *this;
    }
    RString & append(const char * first, const char * last) {
        s.append(first, last);
        return *this;
    }
    RString & append(const char * c, size_t n) {
        s.append(c, n);
        return *this;
    }
    RString & append(size_t n, char c) {
        s.append(n, c);
        return *this;
    }
    RString & append(const RString & other, size_t subpos, size_t sublen) {
        s.append(other, subpos, sublen);
        return *this;
    }
    RString & MakeLower() {
        for (size_t i = 0; i < s.size(); ++i) {
            s[i] = tolower(s[i]);
        }
        return *this;
    }
    RString & MakeUpper() {
        for (size_t i = 0; i < s.size(); ++i) {
            s[i] = toupper(s[i]);
        }
        return *this;
    }
    bool EqualsNoCase(const RString & other) const {
        if (s.size() != other.s.size()) {
            return false;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            if (tolower(s[i]) != tolower(other.s[i])) {
                return false;
            }
        }
        return true;
    }
    int CompareNoCase(const RString & other) const {
        size_t len = length();
        size_t other_len = other.length();
        for (size_t i = 0; i < std::min(len, other_len); ++i) {
            int a = tolower((*this)[i]);
            int b = tolower(other[i]);
            if (a - b != 0) {
                return a - b;
            }
        }
        return len - other_len;
    }
    RString substr(size_t start, size_t end = std::string::npos) const {
        return RString(s.substr(start, end));
    }
    void insert(size_t pos, const RString & other) {
        s.insert(pos, other.s);
    }
    void insert(std::string::const_iterator pos, char c) {
        s.insert(pos, c);
    }
    void insert(std::string::const_iterator pos, size_t count, char c) {
        s.insert(pos, count, c);
    }
    size_t find_first_of(const RString & other, size_t pos = 0) const {
        return s.find_first_of(other.s, pos);
    }
    size_t find_last_of(const RString & other, size_t pos = npos) const {
        return s.find_last_of(other.s, pos);
    }
    size_t find_first_not_of(const RString & other, size_t pos = 0) const {
        return s.find_first_not_of(other.s, pos);
    }
    size_t find_last_not_of(const RString & other, size_t pos = npos) const {
        return s.find_last_not_of(other.s, pos);
    }
    size_t find_first_of(char c, size_t pos = 0) const {
        return s.find_first_of(c, pos);
    }
    size_t find_last_of(char c, size_t pos = npos) const {
        return s.find_last_of(c, pos);
    }
    void replace(size_t pos, size_t len, const RString & other) {
        s.replace(pos, len, other.s);
    }
    void replace(size_t pos, size_t len, const char * other, size_t other_len) {
        s.replace(pos, len, other, other_len);
    }

    RString Left(int n) const {
        n = std::max(n, 0);
        n = std::min(n, static_cast<int>(s.size()));
        return substr(0, n);
    }
    RString Right(int n) const {
        n = std::max(n, 0);
        n = std::min(n, static_cast<int>(s.size()));
        return substr(static_cast<int>(s.size()) - n);
    }
    size_t find(const RString & other, size_t pos = 0) const {
        return s.find(other.s, pos);
    }
    size_t find(char c, size_t pos = 0) const {
        return s.find(c, pos);
    }
    size_t rfind(const RString & other, size_t pos = npos) const {
        return s.rfind(other.s, pos);
    }
    size_t rfind(char c, size_t pos = npos) const {
        return s.rfind(c, pos);
    }
    void Replace(const RString & a, const RString & b) {
        size_t idx = 0;
        size_t a_len = a.length();
        size_t b_len = b.length();
        while (idx = find(a, idx), idx != npos) {
            replace(idx, a_len, b);
            idx += b_len;
        }
    }
    void Replace(char a, char b) {
        size_t len = length();
        for (size_t i = 0; i < len; ++i) {
            if (s[i] == a) {
                s[i] = b;
            }
        }
    }
    void assign(const RString & other) {
        s.assign(other.s);
    }
    void assign(const RString & other, size_t pos, size_t count) {
        s.assign(other.s, pos, count);
    }
    void assign(const char * other, size_t count) {
        s.assign(other, count);
    }
    void swap(RString & other) {
        s.swap(other.s);
    }
    void resize(size_t n) {
        s.resize(n);
    }
};

inline RString operator+(const char * s1, const RString & s2) {
    return RString(std::string(s1) + s2.c_str());
}
#endif

#endif // #ifndef STDSTRING_H
