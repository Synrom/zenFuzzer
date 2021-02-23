// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uint256.h"

#include "utilstrencodings.h"

#include <stdio.h>
#include <string.h>
#include <zendoo/zendoo_mc.h>

template <unsigned int BITS>
base_blob<BITS>::base_blob(const std::vector<unsigned char>& vch)
{
    assert(vch.size() == sizeof(data));
    memcpy(data, &vch[0], sizeof(data));
}

template <unsigned int BITS>
std::string base_blob<BITS>::GetHex() const
{
    char psz[sizeof(data) * 2 + 1];
    for (unsigned int i = 0; i < sizeof(data); i++)
        sprintf(psz + i * 2, "%02x", data[sizeof(data) - i - 1]);
    return std::string(psz, psz + sizeof(data) * 2);
}

template <unsigned int BITS>
void base_blob<BITS>::SetHex(const char* psz)
{
    memset(data, 0, sizeof(data));

    // skip leading spaces
    while (isspace(*psz))
        psz++;

    // skip 0x
    if (psz[0] == '0' && tolower(psz[1]) == 'x')
        psz += 2;

    // hex string to uint
    const char* pbegin = psz;
    while (::HexDigit(*psz) != -1)
        psz++;
    psz--;
    unsigned char* p1 = (unsigned char*)data;
    unsigned char* pend = p1 + WIDTH;
    while (psz >= pbegin && p1 < pend) {
        *p1 = ::HexDigit(*psz--);
        if (psz >= pbegin) {
            *p1 |= ((unsigned char)::HexDigit(*psz--) << 4);
            p1++;
        }
    }
}

template <unsigned int BITS>
void base_blob<BITS>::SetHex(const std::string& str)
{
    SetHex(str.c_str());
}

template <unsigned int BITS>
std::string base_blob<BITS>::ToString() const
{
    return (GetHex());
}

// Explicit instantiations for base_blob<160>
template base_blob<160>::base_blob(const std::vector<unsigned char>&);
template std::string base_blob<160>::GetHex() const;
template std::string base_blob<160>::ToString() const;
template void base_blob<160>::SetHex(const char*);
template void base_blob<160>::SetHex(const std::string&);

// Explicit instantiations for base_blob<256>
template base_blob<256>::base_blob(const std::vector<unsigned char>&);
template std::string base_blob<256>::GetHex() const;
template std::string base_blob<256>::ToString() const;
template void base_blob<256>::SetHex(const char*);
template void base_blob<256>::SetHex(const std::string&);

// Explicit instantiations for sidechain-related stuff
template             base_blob<SC_FIELD_SIZE * 8>::base_blob(const std::vector<unsigned char>&);
template std::string base_blob<SC_FIELD_SIZE * 8>::GetHex() const;
template void        base_blob<SC_FIELD_SIZE * 8>::SetHex(const char*);
template void        base_blob<SC_FIELD_SIZE * 8>::SetHex(const std::string&);
template std::string base_blob<SC_FIELD_SIZE * 8>::ToString() const;

template             base_blob<SC_VK_SIZE * 8>::base_blob(const std::vector<unsigned char>&);
template void        base_blob<SC_VK_SIZE * 8>::SetHex(const std::string&);
template std::string base_blob<SC_VK_SIZE * 8>::ToString() const;

template             base_blob<SC_PROOF_SIZE * 8>::base_blob(const std::vector<unsigned char>&);
template void        base_blob<SC_PROOF_SIZE * 8>::SetHex(const std::string&);


static void inline HashMix(uint32_t& a, uint32_t& b, uint32_t& c)
{
    // Taken from lookup3, by Bob Jenkins.
    a -= c;
    a ^= ((c << 4) | (c >> 28));
    c += b;
    b -= a;
    b ^= ((a << 6) | (a >> 26));
    a += c;
    c -= b;
    c ^= ((b << 8) | (b >> 24));
    b += a;
    a -= c;
    a ^= ((c << 16) | (c >> 16));
    c += b;
    b -= a;
    b ^= ((a << 19) | (a >> 13));
    a += c;
    c -= b;
    c ^= ((b << 4) | (b >> 28));
    b += a;
}

static void inline HashFinal(uint32_t& a, uint32_t& b, uint32_t& c)
{
    // Taken from lookup3, by Bob Jenkins.
    c ^= b;
    c -= ((b << 14) | (b >> 18));
    a ^= c;
    a -= ((c << 11) | (c >> 21));
    b ^= a;
    b -= ((a << 25) | (a >> 7));
    c ^= b;
    c -= ((b << 16) | (b >> 16));
    a ^= c;
    a -= ((c << 4) | (c >> 28));
    b ^= a;
    b -= ((a << 14) | (a >> 18));
    c ^= b;
    c -= ((b << 24) | (b >> 8));
}

uint64_t uint256::GetHash(const uint256& salt) const
{
    uint32_t a, b, c;
    const uint32_t *pn = (const uint32_t*)data;
    const uint32_t *salt_pn = (const uint32_t*)salt.data;
    a = b = c = 0xdeadbeef + WIDTH;

    a += pn[0] ^ salt_pn[0];
    b += pn[1] ^ salt_pn[1];
    c += pn[2] ^ salt_pn[2];
    HashMix(a, b, c);
    a += pn[3] ^ salt_pn[3];
    b += pn[4] ^ salt_pn[4];
    c += pn[5] ^ salt_pn[5];
    HashMix(a, b, c);
    a += pn[6] ^ salt_pn[6];
    b += pn[7] ^ salt_pn[7];
    HashFinal(a, b, c);

    return ((((uint64_t)b) << 32) | c);
}

template <>
uint64_t base_blob<768>::GetHash(const base_blob<768>& salt) const
{
    uint32_t a, b, c;
    const uint32_t *pn = (const uint32_t*)data;
    const uint32_t *salt_pn = (const uint32_t*)salt.data;
    a = b = c = 0xdeadbeef + WIDTH;

    a += pn[0] ^ salt_pn[0];
    b += pn[1] ^ salt_pn[1];
    c += pn[2] ^ salt_pn[2];
    HashMix(a, b, c);
    a += pn[3] ^ salt_pn[3];
    b += pn[4] ^ salt_pn[4];
    c += pn[5] ^ salt_pn[5];
    HashMix(a, b, c);
    a += pn[6] ^ salt_pn[6];
    b += pn[7] ^ salt_pn[7];
    c += pn[8] ^ salt_pn[8];
    HashMix(a, b, c);
    a += pn[9] ^ salt_pn[9];
    b += pn[10] ^ salt_pn[10];
    c += pn[11] ^ salt_pn[11];
    HashMix(a, b, c);
    a += pn[12] ^ salt_pn[12];
    b += pn[13] ^ salt_pn[13];
    c += pn[14] ^ salt_pn[14];
    HashMix(a, b, c);
    a += pn[15] ^ salt_pn[15];
    b += pn[16] ^ salt_pn[16];
    c += pn[17] ^ salt_pn[17];
    HashMix(a, b, c);
    a += pn[18] ^ salt_pn[18];
    b += pn[19] ^ salt_pn[19];
    c += pn[20] ^ salt_pn[20];
    HashMix(a, b, c);
    a += pn[21] ^ salt_pn[21];
    b += pn[22] ^ salt_pn[22];
    c += pn[23] ^ salt_pn[23];
    
    HashFinal(a, b, c);

    return ((((uint64_t)b) << 32) | c);
}

uint64_t CalculateHash(const uint32_t* const src, size_t length, const uint32_t* const salt)
{
    uint32_t a, b, c;
    const uint32_t *pn = (const uint32_t*)src;
    const uint32_t *salt_pn = (const uint32_t*)salt;
    a = b = c = 0xdeadbeef + length;

    while(length > 3 ) {
        a += pn[0] ^ salt_pn[0];
        b += pn[1] ^ salt_pn[1];
        c += pn[2] ^ salt_pn[2];
        HashMix(a, b, c);

        length -= 3;
        pn += 3;
        salt_pn += 3;
    }

    switch(length) {
        case 3 : c += pn[2] ^ salt_pn[2];
        case 2 : b += pn[1] ^ salt_pn[1];
        case 1 : a += pn[0] ^ salt_pn[0];
            HashFinal(a, b, c);
        case 0:
            break;
    }

    HashFinal(a, b, c);

    return ((((uint64_t)b) << 32) | c);
}
