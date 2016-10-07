#ifndef DESCBC_H
#define DESCBC_H

#include "des.h"

class DESCBC
{
public:
    DESCBC(ui64 key, ui64 iv) : des(key), iv(iv), last_block(iv) {}
    inline ui64 encrypt(ui64 block)
    {
      last_block = des.encrypt(block ^ last_block);
      return last_block;
    }

    inline ui64 decrypt(ui64 block)
    {
      ui64 result = des.decrypt(block) ^ last_block;
      last_block = block;
      return result;
    }

    inline void reset()
    {
      last_block = iv;
    }

private:
    DES des;
    ui64 iv;
    ui64 last_block;
};

#endif // DESCBC_H
