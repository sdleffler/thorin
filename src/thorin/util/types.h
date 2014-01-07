#ifndef THORIN_UTIL_TYPES_H
#define THORIN_UTIL_TYPES_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <ostream>
#include <type_traits>

#include "thorin/util/cast.h"

namespace thorin {

// This code assumes two-complement arithmetic for unsigned operations.
// This is *implementation-defined* but *NOT* *undefined behavior*.

class IntError {};

template<class ST, bool wrap>
class SInt {
public:
    typedef typename std::make_unsigned<ST>::type UT;
    static_assert(std::is_signed<ST>::value, "ST must be a signed type");

    SInt(ST data)
        : data_(data)
    {}

    SInt operator - () const {
        if (data_ == std::numeric_limits<ST>::min())
            throw IntError();
        return SInt(-data_); 
    }

    SInt operator + (SInt other) const {
        SInt res(UT(this->data_) + UT(other.data_));
        if (!wrap && (this->is_neg() == other.is_neg()) && (this->is_neg() != res.is_neg()))
            throw IntError();
        return res;
    }

    SInt operator - (SInt other) const { return *this + SInt(other.minus()); }

    SInt operator * (SInt other) const {
        ST a = this->data_, b = other.data_;

        if (!wrap) {
            if (a > ST(0)) {
                if (b > ST(0)) {
                    if (a > (std::numeric_limits<ST>::max() / b))
                        throw IntError();
                } else {
                    if (b < (std::numeric_limits<ST>::min() / a))
                        throw IntError();
                }
            } else {
                if (b > ST(0)) {
                    if (a < (std::numeric_limits<ST>::min() / b))
                        throw IntError();
                } else {
                    if ( (a != ST(0)) && (b < (std::numeric_limits<ST>::max() / a)))
                        throw IntError();
                }
            }
        }

        return UT(a) * UT(b);
    }

    void div_check(SInt other) const {
        if (other.data_ == ST(0) || (this->data_ == std::numeric_limits<ST>::min() && other.data_ == ST(-1)))
            throw IntError();
    }

    SInt operator / (SInt other) const { div_check(other); return SInt(this->data_ / other.data_); }
    SInt operator % (SInt other) const { div_check(other); return SInt(this->data_ % other.data_); }

    SInt operator << (SInt other) const {
        if (this->is_neg() || other.is_neg() 
                || other.data_ >= std::numeric_limits<ST>::digits+1 
                || this->data_ >= std::numeric_limits<ST>::max() >> other.data_) {
            throw IntError();
        } 

        return this->data_ << other.data_;
    }

    SInt operator & (SInt other) const { return this->data_ & other.data_; }
    SInt operator | (SInt other) const { return this->data_ | other.data_; }
    SInt operator ^ (SInt other) const { return this->data_ ^ other.data_; }
    SInt operator >>(SInt other) const { return this->data_ >>other.data_; }
    bool operator < (SInt other) const { return this->data_ < other.data_; }
    bool operator <=(SInt other) const { return this->data_ <=other.data_; }
    bool operator > (SInt other) const { return this->data_ > other.data_; }
    bool operator >=(SInt other) const { return this->data_ >=other.data_; }
    bool operator ==(SInt other) const { return this->data_ ==other.data_; }
    bool operator !=(SInt other) const { return this->data_ !=other.data_; }
    bool is_neg() const { return data_ < ST(0); }
    operator ST() const { return data_; }
    ST data() const { return data_; }

private:
    SInt minus() const { return SInt(~UT(data_)+UT(1u)); }
    SInt abs() const { return is_neg() ? minus() : *this; }

    ST data_;
};

template<class UT, bool wrap>
class UInt {
public:
    static_assert(std::is_unsigned<UT>::value, "UT must be an unsigned type");

    UInt(UT data)
        : data_(data)
    {}

    UInt operator - () const {
        assert(false && "TODO");
        return UInt(0);
    }

    UInt operator + (UInt other) const {
        UInt res(UT(this->data_) + UT(other.data_));
        if (!wrap && res.data_ < this->data_)
            throw IntError();
        return res;
    }

    UInt operator - (UInt other) const { 
        UInt res(UT(this->data_) - UT(other.data_));
        if (!wrap && res.data_ > this->data_)
            throw IntError();
        return res;
    }

    UInt operator * (UInt other) const {
        if (!wrap && other.data_ && this->data_ > std::numeric_limits<UT>::max() / other.data_)
            throw IntError();
        return UT(this->data_) * UT(other.data_);
    }

    void div_check(UInt other) const { if (other.data_ == UT(0)) throw IntError(); }
    UInt operator / (UInt other) const { div_check(other); return UInt(this->data_ / other.data_); }
    UInt operator % (UInt other) const { div_check(other); return UInt(this->data_ % other.data_); }

    UInt operator << (UInt other) const {
        assert(false && "TODO");
        return UInt(0);
        //if (this->is_neg() || other.is_neg() 
                //|| other >= std::numeric_limits<UT>::digits+1 
                //|| this->data_ >= std::numeric_limits<UT>::max() >> other.data_) {
            //throw IntError();
        //} 

        //return this->data_ << other->data_;
    }

    UInt operator & (UInt other) const { return this->data_ & other.data_; }
    UInt operator | (UInt other) const { return this->data_ | other.data_; }
    UInt operator ^ (UInt other) const { return this->data_ ^ other.data_; }
    UInt operator >>(UInt other) const { return this->data_ >>other.data_; }
    bool operator < (UInt other) const { return this->data_ < other.data_; }
    bool operator <=(UInt other) const { return this->data_ <=other.data_; }
    bool operator > (UInt other) const { return this->data_ > other.data_; }
    bool operator >=(UInt other) const { return this->data_ >=other.data_; }
    bool operator ==(UInt other) const { return this->data_ ==other.data_; }
    bool operator !=(UInt other) const { return this->data_ !=other.data_; }
    bool is_neg() const { return data_ < UT(0); }
    operator UT() const { return data_; }
    UT data() const { return data_; }

private:
    UInt minus() const { return UInt(~UT(data_)+UT(1u)); }
    UInt abs() const { return is_neg() ? minus() : *this; }

    UT data_;
};

template<class FT, bool precise>
class Float {
public:
    Float(FT data)
        : data_(data)
    {}

    Float operator - () const { return -data_; }
    Float operator + (Float other) const { return Float(this->data_ + other.data_); }
    Float operator - (Float other) const { return Float(this->data_ - other.data_); }
    Float operator * (Float other) const { return Float(this->data_ * other.data_); }
    Float operator / (Float other) const { return Float(this->data_ / other.data_); }
    Float operator % (Float other) const { return Float(std::fmod(this->data_, other.data_)); }
    bool  operator < (Float other) const { return Float(this->data_ < other.data_); }
    bool  operator <=(Float other) const { return Float(this->data_ <=other.data_); }
    bool  operator > (Float other) const { return Float(this->data_ > other.data_); }
    bool  operator >=(Float other) const { return Float(this->data_ >=other.data_); }
    bool  operator ==(Float other) const { return Float(this->data_ ==other.data_); }
    bool  operator !=(Float other) const { return Float(this->data_ !=other.data_); }
    operator FT() const { return data_; }
    FT data() const { return data_; }

private:
    FT data_;
};

typedef  int8_t  s8; typedef  uint8_t  u8; typedef SInt< s8, true>   ps8; typedef  UInt< u8, true>  pu8; typedef  SInt< s8, false>  qs8; typedef UInt< u8, false>  qu8;
typedef int16_t s16; typedef uint16_t u16; typedef SInt<s16, true>  ps16; typedef  UInt<u16, true> pu16; typedef  SInt<s16, false> qs16; typedef UInt<u16, false> qu16;
typedef int32_t s32; typedef uint32_t u32; typedef SInt<s32, true>  ps32; typedef  UInt<u32, true> pu32; typedef  SInt<s32, false> qs32; typedef UInt<u32, false> qu32;
typedef int64_t s64; typedef uint64_t u64; typedef SInt<s64, true>  ps64; typedef  UInt<u64, true> pu64; typedef  SInt<s64, false> qs64; typedef UInt<u64, false> qu64;

typedef float  f32; typedef Float<f32, true> pf32; typedef Float<f32, false>  qf32;
typedef double f64; typedef Float<f64, true> pf64; typedef Float<f64, false>  qf64;

}

#endif
