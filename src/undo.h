// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UNDO_H
#define BITCOIN_UNDO_H

#include "compressor.h" 
#include "primitives/transaction.h"
#include "serialize.h"

/** Undo information for a CTxIn
 *
 *  Contains the prevout's CTxOut being spent, and if this was the
 *  last output of the affected transaction, its metadata as well
 *  (coinbase or not, height, transaction version)
 */
class CTxInUndo
{
public:
    CTxOut txout;         // the txout data before being spent
    bool fCoinBase;       // if the outpoint was the last unspent: whether it belonged to a coinbase
    unsigned int nHeight; // if the outpoint was the last unspent: its height
    int nVersion;         // if the outpoint was the last unspent: its version

    CTxInUndo() : txout(), fCoinBase(false), nHeight(0), nVersion(0) {}
    CTxInUndo(const CTxOut &txoutIn, bool fCoinBaseIn = false, unsigned int nHeightIn = 0, int nVersionIn = 0) : txout(txoutIn), fCoinBase(fCoinBaseIn), nHeight(nHeightIn), nVersion(nVersionIn) { }

    unsigned int GetSerializeSize(int nType, int nVersion) const {
        return ::GetSerializeSize(VARINT(nHeight*2+(fCoinBase ? 1 : 0)), nType, nVersion) +
               (nHeight > 0 ? ::GetSerializeSize(VARINT(this->nVersion), nType, nVersion) : 0) +
               ::GetSerializeSize(CTxOutCompressor(REF(txout)), nType, nVersion);
    }

    template<typename Stream>
    void Serialize(Stream &s, int nType, int nVersion) const {
        ::Serialize(s, VARINT(nHeight*2+(fCoinBase ? 1 : 0)), nType, nVersion);
        if (nHeight > 0)
            ::Serialize(s, VARINT(this->nVersion), nType, nVersion);
        ::Serialize(s, CTxOutCompressor(REF(txout)), nType, nVersion);
    }

    template<typename Stream>
    void Unserialize(Stream &s, int nType, int nVersion) {
        unsigned int nCode = 0;
        ::Unserialize(s, VARINT(nCode), nType, nVersion);
        nHeight = nCode / 2;
        fCoinBase = nCode & 1;
        if (nHeight > 0)
            ::Unserialize(s, VARINT(this->nVersion), nType, nVersion);
        ::Unserialize(s, REF(CTxOutCompressor(REF(txout))), nType, nVersion);
    }
};

/** Undo information for a CTransaction */
class CTxUndo
{
public:
    // undo information for all txins
    std::vector<CTxInUndo> vprevout;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vprevout);
    }
};

struct ScUndoData
{
    CAmount immAmount;
    int certEpoch;
    
    ScUndoData(): immAmount(0), certEpoch(CScCertificate::EPOCH_NOT_INITIALIZED) {}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(immAmount);
        READWRITE(certEpoch);
    }
};

/** Undo information for a CBlock */
class CBlockUndo
{
    /** Magic number read from the value expressing the size of vtxundo vector.
     *  It is used for distinguish new version of CBlockUndo instance from old ones.
     *  The maximum number of tx in a block is roughly MAX_BLOCK_SIZE / MIN_TX_SIZE, which is:
     *   2M / 61bytes =~ 33K = 0x8012
     * Therefore the magic number must be a number greater than this limit. */
    static const uint16_t _marker = 0xfec1;

    /** memory only */
    bool isNewVersion;

public:
    std::vector<CTxUndo> vtxundo; // for all but the coinbase
    uint256 old_tree_root;
    std::map<uint256, ScUndoData> msc_iaundo; // key=scid, value=amount matured at block height

    /** create as new */
    CBlockUndo() : isNewVersion(true) {}

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        CSizeComputer s(nType, nVersion);
        NCONST_PTR(this)->Serialize(s, nType, nVersion);
        return s.size();
    }   

    template<typename Stream> void Serialize(Stream& s, int nType, int nVersion) const
    {
        if (isNewVersion)
        {
            WriteCompactSize(s, _marker);
            ::Serialize(s, (vtxundo), nType, nVersion);
            ::Serialize(s, (old_tree_root), nType, nVersion);
            ::Serialize(s, (msc_iaundo), nType, nVersion);
        }
        else
        {
            ::Serialize(s, (vtxundo), nType, nVersion);
            ::Serialize(s, (old_tree_root), nType, nVersion);
        }
    }   

    template<typename Stream> void Unserialize(Stream& s, int nType, int nVersion)
    {
        // reading from data stream to memory
        vtxundo.clear();
        isNewVersion = false;

        unsigned int nSize = ReadCompactSize(s);
        if (nSize == _marker)
        {
            // this is a new version of blockundo
            ::Unserialize(s, (vtxundo), nType, nVersion);
            ::Unserialize(s, (old_tree_root), nType, nVersion);
            ::Unserialize(s, (msc_iaundo), nType, nVersion);
            isNewVersion = true;
        }
        else
        {
#if 1
            s.Rewind(GetSizeOfCompactSize(nSize));
            ::Unserialize(s, (vtxundo), nType, nVersion);
#else
            ::AddEntriesInVector(s, vtxundo, nType, nVersion, nSize);
#endif
            ::Unserialize(s, (old_tree_root), nType, nVersion);
        }
    };

    std::string ToString() const
    {
        std::string str;
        str += strprintf("\nisNewVersion=%u\n", isNewVersion);
        str += strprintf("vtxundo.size %u\n", vtxundo.size());
        str += strprintf("old_tree_root %s\n", old_tree_root.ToString().substr(0,10));
        str += strprintf("msc_iaundo.size %u\n", msc_iaundo.size());
        str += strprintf(" ===> tot size %u\n", GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION));
        return str;
    }

    /** for tresting */
    bool IsNewVersion() const  { return isNewVersion; }

};
#endif // BITCOIN_UNDO_H
