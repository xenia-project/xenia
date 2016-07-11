#ifndef DES_H
#define DES_H

#include <cstdint>

typedef uint64_t ui64;
typedef uint32_t ui32;
typedef uint8_t ui8;

class DES
{
public:
    DES(ui64 key);
    DES(ui64 *sub_key);
    ui64 des(ui64 block, bool mode);

    ui64 encrypt(ui64 block);
    ui64 decrypt(ui64 block);

    static ui64 encrypt(ui64 block, ui64 key);
    static ui64 decrypt(ui64 block, ui64 key);

    static void set_parity(ui8 *key, int length, ui8 *out) {
      for (int i = 0; i < length; i++) {
        ui8 parity = key[i];

        parity ^= parity >> 4;
        parity ^= parity >> 2;
        parity ^= parity >> 1;

        out[i] = (key[i] & 0xFE) | (~parity & 1);
      }
    }

    const ui64 *get_sub_key() const { return sub_key; }
    void set_sub_key(ui64 *key);

protected:
    void keygen(ui64 key);

    ui64 ip(ui64 block);
    ui64 fp(ui64 block);

    void feistel(ui32 &L, ui32 &R, ui32 F);
    ui32 f(ui32 R, ui64 k);

private:
    ui64 sub_key[16]; // 48 bits each
};

#endif // DES_H
