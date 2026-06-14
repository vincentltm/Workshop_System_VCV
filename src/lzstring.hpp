#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cmath>

namespace lzstring {

const std::string uri_alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-$";

// Helper to convert UTF-8 std::string to UTF-16 std::u16string
inline std::u16string utf8_to_utf16(const std::string& str) {
    std::u16string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size();) {
        uint32_t cp = 0;
        uint8_t c = str[i];
        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= str.size()) break;
            cp = ((c & 0x1F) << 6) | (str[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= str.size()) break;
            cp = ((c & 0x0F) << 12) | ((str[i + 1] & 0x3F) << 6) | (str[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= str.size()) break;
            cp = ((c & 0x07) << 18) | ((str[i + 1] & 0x3F) << 12) | ((str[i + 2] & 0x3F) << 6) | (str[i + 3] & 0x3F);
            i += 4;
        } else {
            i += 1;
            continue;
        }
        
        if (cp < 0x10000) {
            out.push_back((char16_t)cp);
        } else {
            cp -= 0x10000;
            out.push_back((char16_t)(0xD800 | ((cp >> 10) & 0x3FF)));
            out.push_back((char16_t)(0xDC00 | (cp & 0x3FF)));
        }
    }
    return out;
}

// Helper to convert UTF-16 std::u16string to UTF-8 std::string
inline std::string utf16_to_utf8(const std::u16string& str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size();) {
        uint32_t cp = str[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < str.size()) {
            uint32_t trail = str[i + 1];
            if (trail >= 0xDC00 && trail <= 0xDFFF) {
                cp = 0x10000 + (((cp & 0x3FF) << 10) | (trail & 0x3FF));
                i += 2;
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }
        
        if (cp < 0x80) {
            out.push_back(cp);
        } else if (cp < 0x800) {
            out.push_back(0xC0 | ((cp >> 6) & 0x1F));
            out.push_back(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out.push_back(0xE0 | ((cp >> 12) & 0x0F));
            out.push_back(0x80 | ((cp >> 6) & 0x3F));
            out.push_back(0x80 | (cp & 0x3F));
        } else {
            out.push_back(0xF0 | ((cp >> 18) & 0x07));
            out.push_back(0x80 | ((cp >> 12) & 0x3F));
            out.push_back(0x80 | ((cp >> 6) & 0x3F));
            out.push_back(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

inline std::string _compress(const std::u16string& uncompressed, int bitsPerChar, const std::function<char(int)>& getCharFromVal) {
    if (uncompressed.empty()) return "";
    
    std::unordered_map<std::u16string, int> s;
    std::unordered_map<std::u16string, bool> u;
    
    std::u16string c = u"";
    char16_t a;
    std::u16string p = u"";
    std::u16string a_str = u"";
    
    int l = 2;
    int f = 3;
    int h = 2;
    std::string d = "";
    int m = 0;
    int v = 0;
    
    auto writeBit = [&](int bit) {
        m = (m << 1) | bit;
        if (v == bitsPerChar - 1) {
            v = 0;
            d.push_back(getCharFromVal(m));
            m = 0;
        } else {
            v++;
        }
    };
    
    for (size_t i = 0; i < uncompressed.length(); i++) {
        a = uncompressed[i];
        a_str = std::u16string(1, a);
        if (s.find(a_str) == s.end()) {
            s[a_str] = f++;
            u[a_str] = true;
        }
        p = c + a_str;
        if (s.find(p) != s.end()) {
            c = p;
        } else {
            if (u.find(c) != u.end()) {
                if ((uint16_t)c[0] < 256) {
                    for (int e = 0; e < h; e++) {
                        writeBit(0);
                    }
                    int t = (uint16_t)c[0];
                    for (int e = 0; e < 8; e++) {
                        writeBit(t & 1);
                        t >>= 1;
                    }
                } else {
                    int t = 1;
                    for (int e = 0; e < h; e++) {
                        writeBit(t);
                        t = 0;
                    }
                    t = (uint16_t)c[0];
                    for (int e = 0; e < 16; e++) {
                        writeBit(t & 1);
                        t >>= 1;
                    }
                }
                l--;
                if (l == 0) {
                    l = std::pow(2, h);
                    h++;
                }
                u.erase(c);
            } else {
                int t = s[c];
                for (int e = 0; e < h; e++) {
                    writeBit(t & 1);
                    t >>= 1;
                }
            }
            l--;
            if (l == 0) {
                l = std::pow(2, h);
                h++;
            }
            s[p] = f++;
            c = a_str;
        }
    }
    
    if (!c.empty()) {
        if (u.find(c) != u.end()) {
            if ((uint16_t)c[0] < 256) {
                for (int e = 0; e < h; e++) {
                    writeBit(0);
                }
                int t = (uint16_t)c[0];
                for (int e = 0; e < 8; e++) {
                    writeBit(t & 1);
                    t >>= 1;
                }
            } else {
                int t = 1;
                for (int e = 0; e < h; e++) {
                    writeBit(t);
                    t = 0;
                }
                t = (uint16_t)c[0];
                for (int e = 0; e < 16; e++) {
                    writeBit(t & 1);
                    t >>= 1;
                }
            }
            l--;
            if (l == 0) {
                l = std::pow(2, h);
                h++;
            }
            u.erase(c);
        } else {
            int t = s[c];
            for (int e = 0; e < h; e++) {
                writeBit(t & 1);
                t >>= 1;
            }
        }
        l--;
        if (l == 0) {
            l = std::pow(2, h);
            h++;
        }
    }
    
    // Output end mark (code 2)
    int t = 2;
    for (int e = 0; e < h; e++) {
        writeBit(t & 1);
        t >>= 1;
    }
    
    // Flush
    while (true) {
        m <<= 1;
        if (v == bitsPerChar - 1) {
            d.push_back(getCharFromVal(m));
            break;
        }
        v++;
    }
    
    return d;
}

inline std::u16string _decompress(int length, int resetValue, const std::function<int(int)>& getValFromChar) {
    std::vector<std::u16string> l;
    l.resize(3);
    for (int i = 0; i < 3; i++) {
        l[i] = std::u16string(1, (char16_t)i);
    }
    
    struct State {
        int val;
        int position;
        int index;
        std::function<int(int)> getVal;
        
        int readBit() {
            int u = val & position;
            position >>= 1;
            if (position == 0) {
                position = 32; // Always 32 for encoded URI component
                val = getVal(index++);
            }
            return u > 0 ? 1 : 0;
        }
        
        int readBits(int count) {
            int s = 0;
            int p = 1;
            for (int i = 0; i < count; i++) {
                s |= readBit() * p;
                p <<= 1;
            }
            return s;
        }
    };
    
    State g = { getValFromChar(0), resetValue, 1, getValFromChar };
    
    int s = g.readBits(2);
    char16_t c_char = 0;
    
    switch (s) {
        case 0:
            c_char = (char16_t)g.readBits(8);
            break;
        case 1:
            c_char = (char16_t)g.readBits(16);
            break;
        case 2:
            return u"";
    }
    
    l.push_back(std::u16string(1, c_char));
    std::u16string i_str = std::u16string(1, c_char);
    std::u16string c_str = i_str;
    std::vector<std::u16string> v;
    v.push_back(c_str);
    
    int f = 4;
    int h = 4;
    int d = 3;
    
    while (true) {
        if (g.index > length) {
            return u"";
        }
        
        int s = g.readBits(d);
        int c_code = s;
        
        switch (c_code) {
            case 0: {
                char16_t cc = (char16_t)g.readBits(8);
                l.push_back(std::u16string(1, cc));
                c_code = l.size() - 1;
                f--;
                break;
            }
            case 1: {
                char16_t cc = (char16_t)g.readBits(16);
                l.push_back(std::u16string(1, cc));
                c_code = l.size() - 1;
                f--;
                break;
            }
            case 2: {
                std::u16string res = u"";
                for (const auto& chunk : v) {
                    res += chunk;
                }
                return res;
            }
        }
        
        if (f == 0) {
            f = std::pow(2, d);
            d++;
        }
        
        std::u16string m;
        if (c_code < (int)l.size()) {
            m = l[c_code];
        } else {
            if (c_code != (int)l.size()) {
                return u"";
            }
            m = i_str + i_str[0];
        }
        v.push_back(m);
        l.push_back(i_str + m[0]);
        i_str = m;
        f--;
        if (f == 0) {
            f = std::pow(2, d);
            d++;
        }
    }
}

inline std::string compressToEncodedURIComponent(const std::string& str) {
    if (str.empty()) return "";
    std::u16string u16 = utf8_to_utf16(str);
    return _compress(u16, 6, [](int val) {
        return uri_alphabet[val];
    });
}

inline std::string decompressFromEncodedURIComponent(const std::string& str) {
    if (str.empty()) return "";
    
    // Normalize spaces to '+'
    std::string normalized = str;
    for (char& c : normalized) {
        if (c == ' ') c = '+';
    }
    
    // Map character to value
    auto getValFromChar = [&normalized](int index) -> int {
        if (index >= (int)normalized.length()) return 0;
        char c = normalized[index];
        size_t pos = uri_alphabet.find(c);
        return (pos != std::string::npos) ? (int)pos : 0;
    };
    
    std::u16string u16 = _decompress(normalized.length(), 32, getValFromChar);
    return utf16_to_utf8(u16);
}

} // namespace lzstring
