#ifndef DES3_H
#define DES3_H

#include "des.h"

class DES3
{
public:
    DES3(ui64 k1, ui64 k2, ui64 k3) : des{ DES(k1), DES(k2), DES(k3) } {}
    DES3(ui64* sk1, ui64* sk2, ui64* sk3) : des{ DES(sk1), DES(sk2), DES(sk3) } {}

    inline ui64 encrypt(ui64 block)
    {
      return des[2].encrypt(des[1].decrypt(des[0].encrypt(block)));
    }

    inline ui64 decrypt(ui64 block)
    {
      return des[0].decrypt(des[1].encrypt(des[2].decrypt(block)));
    }

    inline ui64 crypt(ui64 block, bool bEncrypt)
    {
      if (bEncrypt)
      {
        return encrypt(block);
      }
      else
      {
        return decrypt(block);
      }
    }

    inline DES* getDES() { return des; }

private:
    DES des[3];
};

#endif // DES3_H
