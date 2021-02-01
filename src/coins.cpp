// Copyright (c) 2012-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"

#include "memusage.h"
#include "random.h"
#include "version.h"
#include "policy/fees.h"

#include <assert.h>
#include "utilmoneystr.h"
#include <undo.h>
#include <chainparams.h>

std::string CCoins::ToString() const
{
    std::string ret;
    ret += strprintf("\n version           (%d)", nVersion);
    ret += strprintf("\n fCoinBase         (%d)", fCoinBase);
    ret += strprintf("\n height            (%d)", nHeight);
    ret += strprintf("\n nFirstBwtPos      (%d)", nFirstBwtPos);
    ret += strprintf("\n nBwtMaturityHeight(%d)", nBwtMaturityHeight);
    for (const CTxOut& out : vout)
    {
        ret += "\n    " + out.ToString();
    }
    return ret;
}

CCoins::CCoins() : fCoinBase(false), vout(0), nHeight(0), nVersion(0), nFirstBwtPos(BWT_POS_UNSET), nBwtMaturityHeight(0) { }

CCoins::CCoins(const CTransaction &tx, int nHeightIn) { From(tx, nHeightIn); }

CCoins::CCoins(const CScCertificate &cert, int nHeightIn, int bwtMaturityHeight, bool isBlockTopQualityCert)
{
    From(cert, nHeightIn, bwtMaturityHeight, isBlockTopQualityCert);
}

void CCoins::From(const CTransaction &tx, int nHeightIn) {
    fCoinBase          = tx.IsCoinBase();
    vout               = tx.GetVout();
    nHeight            = nHeightIn;
    nVersion           = tx.nVersion;
    nFirstBwtPos       = BWT_POS_UNSET;
    nBwtMaturityHeight = 0;
    ClearUnspendable();
}

void CCoins::From(const CScCertificate &cert, int nHeightIn, int bwtMaturityHeight, bool isBlockTopQualityCert) {
    fCoinBase          = cert.IsCoinBase();
    vout               = cert.GetVout();
    nHeight            = nHeightIn;
    nVersion           = cert.nVersion;
    nFirstBwtPos       = cert.nFirstBwtPos;
    nBwtMaturityHeight = bwtMaturityHeight;

    if (!isBlockTopQualityCert) //drop bwts of low q certs
    {
        for(unsigned int bwtPos = nFirstBwtPos; bwtPos < vout.size(); ++bwtPos)
            Spend(bwtPos);
    }

    ClearUnspendable();
}

void CCoins::Clear() {
    fCoinBase = false;
    std::vector<CTxOut>().swap(vout);
    nHeight = 0;
    nVersion = 0;
    nFirstBwtPos = BWT_POS_UNSET;
    nBwtMaturityHeight = 0;
}

void CCoins::Cleanup() {
    while (vout.size() > 0 && vout.back().IsNull())
        vout.pop_back();

    if (vout.empty())
        std::vector<CTxOut>().swap(vout);
}

void CCoins::ClearUnspendable() {
    BOOST_FOREACH(CTxOut &txout, vout) {
        if (txout.scriptPubKey.IsUnspendable())
            txout.SetNull();
    }
    Cleanup();
}

void CCoins::swap(CCoins &to) {
    std::swap(to.fCoinBase, fCoinBase);
    to.vout.swap(vout);
    std::swap(to.nHeight, nHeight);
    std::swap(to.nVersion, nVersion);
    std::swap(to.nFirstBwtPos, nFirstBwtPos);
    std::swap(to.nBwtMaturityHeight, nBwtMaturityHeight);
}

bool operator==(const CCoins &a, const CCoins &b) {
     // Empty CCoins objects are always equal.
     if (a.IsPruned() && b.IsPruned())
         return true;
     return a.fCoinBase          == b.fCoinBase          &&
            a.nHeight            == b.nHeight            &&
            a.nVersion           == b.nVersion           &&
            a.vout               == b.vout               &&
            a.nFirstBwtPos       == b.nFirstBwtPos       &&
            a.nBwtMaturityHeight == b.nBwtMaturityHeight;
}

bool operator!=(const CCoins &a, const CCoins &b) {
    return !(a == b);
}

bool CCoins::IsCoinBase() const {
    return fCoinBase;
}

bool CCoins::IsFromCert() const {
    // when restored from serialization, nVersion, if negative, is populated only with latest 7 bits of the original value!
    // we enforced that no tx/cert can have a version other than a list of well known ones
    // therefore no other 4-bytes signed version will have this 7-bits ending
    return (nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f);
}

bool CCoins::isOutputMature(unsigned int outPos, int nSpendingHeigh) const
{
    if (!IsCoinBase() && !IsFromCert())
        return true;

    if (IsCoinBase())
        return nSpendingHeigh >= (nHeight + COINBASE_MATURITY);

    //Hereinafter a cert
    if (outPos >= nFirstBwtPos)
        return nSpendingHeigh >= nBwtMaturityHeight;
    else
        return true;
}

bool CCoins::Spend(uint32_t nPos)
{
    if (nPos >= vout.size() || vout[nPos].IsNull())
        return false;

    vout[nPos].SetNull();
    Cleanup();
    return true;
}

bool CCoins::IsAvailable(unsigned int nPos) const {
    return (nPos < vout.size() && !vout[nPos].IsNull());
}

bool CCoins::IsPruned() const {
    for(const CTxOut &out: vout)
        if (!out.IsNull())
            return false;
    return true;
}

size_t CCoins::DynamicMemoryUsage() const {
    size_t ret = memusage::DynamicUsage(vout);
    for(const CTxOut &out: vout) {
        ret += RecursiveDynamicUsage(out.scriptPubKey);
    }
    return ret;
}

void CCoins::CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const {
    unsigned int nLastUsedByte = 0;
    for (unsigned int b = 0; 2+b*8 < vout.size(); b++) {
        bool fZero = true;
        for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++) {
            if (!vout[2+b*8+i].IsNull()) {
                fZero = false;
                continue;
            }
        }
        if (!fZero) {
            nLastUsedByte = b + 1;
            nNonzeroBytes++;
        }
    }
    nBytes += nLastUsedByte;
}

bool CCoinsView::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree)  const { return false; }
bool CCoinsView::GetNullifier(const uint256 &nullifier)                         const { return false; }
bool CCoinsView::GetCoins(const uint256 &txid, CCoins &coins)                   const { return false; }
bool CCoinsView::HaveCoins(const uint256 &txid)                                 const { return false; }
bool CCoinsView::HaveSidechain(const uint256& scId)                             const { return false; }
bool CCoinsView::GetSidechain(const uint256& scId, CSidechain& info)            const { return false; }
bool CCoinsView::HaveSidechainEvents(int height)                                const { return false; }
bool CCoinsView::GetSidechainEvents(int height, CSidechainEvents& scEvent)      const { return false; }
void CCoinsView::GetScIds(std::set<uint256>& scIdsList)                         const { scIdsList.clear(); return; }
bool CCoinsView::CheckQuality(const CScCertificate& cert)                       const { return false; }
uint256 CCoinsView::GetBestBlock()                                              const { return uint256(); }
uint256 CCoinsView::GetBestAnchor()                                             const { return uint256(); };
bool CCoinsView::HaveCswNullifier(const uint256& scId,
                                 const libzendoomc::ScFieldElement &nullifier)  const { return false; }
bool CCoinsView::HaveCertDataHashes(const uint256& scId, const int epoch)       const { return false; }
bool CCoinsView::GetCertDataHashes(const uint256& scId, const int epoch,
                                  std::pair<libzendoomc::ScFieldElement,
                                  libzendoomc::ScFieldElement>& certDataHashes) const { return false; }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                            const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                            CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                            CSidechainEventsMap& mapSidechainEvents,
                            CCswNullifiersMap& cswNullifiers,
                            CCertDataHashMap& certDataHashes)                         { return false; }
bool CCoinsView::GetStats(CCoinsStats &stats)                                   const { return false; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }

bool CCoinsViewBacked::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree)   const { return base->GetAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetNullifier(const uint256 &nullifier)                          const { return base->GetNullifier(nullifier); }
bool CCoinsViewBacked::GetCoins(const uint256 &txid, CCoins &coins)                    const { return base->GetCoins(txid, coins); }
bool CCoinsViewBacked::HaveCoins(const uint256 &txid)                                  const { return base->HaveCoins(txid); }
bool CCoinsViewBacked::HaveSidechain(const uint256& scId)                              const { return base->HaveSidechain(scId); }
bool CCoinsViewBacked::GetSidechain(const uint256& scId, CSidechain& info)             const { return base->GetSidechain(scId,info); }
bool CCoinsViewBacked::HaveSidechainEvents(int height)                                 const { return base->HaveSidechainEvents(height); }
bool CCoinsViewBacked::GetSidechainEvents(int height, CSidechainEvents& scEvents)      const { return base->GetSidechainEvents(height, scEvents); }
void CCoinsViewBacked::GetScIds(std::set<uint256>& scIdsList)                          const { return base->GetScIds(scIdsList); }
bool CCoinsViewBacked::CheckQuality(const CScCertificate& cert)                        const { return base->CheckQuality(cert); }
uint256 CCoinsViewBacked::GetBestBlock()                                               const { return base->GetBestBlock(); }
uint256 CCoinsViewBacked::GetBestAnchor()                                              const { return base->GetBestAnchor(); }

bool CCoinsViewBacked::HaveCswNullifier(const uint256& scId,
                                        const libzendoomc::ScFieldElement &nullifier)  const { return base->HaveCswNullifier(scId,nullifier); }

bool CCoinsViewBacked::HaveCertDataHashes(const uint256& scId, const int epoch)        const { return base->HaveCertDataHashes(scId, epoch); }
bool CCoinsViewBacked::GetCertDataHashes(const uint256& scId, const int epoch,
                                         std::pair<libzendoomc::ScFieldElement,
                                         libzendoomc::ScFieldElement>& certDataHashes) const { return base->GetCertDataHashes(scId, epoch, certDataHashes); }

void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                                  const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                                  CNullifiersMap &mapNullifiers, CSidechainsMap& mapSidechains,
                                  CSidechainEventsMap& mapSidechainEvents,
                                  CCswNullifiersMap& cswNullifiers,
                                  CCertDataHashMap& certDataHashes) { return base->BatchWrite(mapCoins, hashBlock, hashAnchor,
                                                                                          mapAnchors, mapNullifiers, mapSidechains,
                                                                                            mapSidechainEvents, cswNullifiers,
                                                                                            certDataHashes); }
bool CCoinsViewBacked::GetStats(CCoinsStats &stats)                                  const { return base->GetStats(stats); }

CCoinsKeyHasher::CCoinsKeyHasher() : salt(GetRandHash()) {}
CCswNullifiersKeyHasher::CCswNullifiersKeyHasher() : salt() {GetRandBytes(reinterpret_cast<unsigned char*>(salt), BUF_LEN);}

size_t CCswNullifiersKeyHasher::operator()(const std::pair<uint256, libzendoomc::ScFieldElement>& key) const {
    uint32_t buf[BUF_LEN/4];
    memcpy(buf, key.first.begin(), 32);
    memcpy((buf + 8), key.second.begin(), SC_FIELD_SIZE);
    return CalculateHash(buf, BUF_LEN/4, salt);
}

CCertDataKeyHasher::CCertDataKeyHasher() : salt() {GetRandBytes(reinterpret_cast<unsigned char*>(salt), BUF_LEN);}

size_t CCertDataKeyHasher::operator()(const std::pair<uint256, int>& key) const {
        uint32_t buf[BUF_LEN/4];
        memcpy(buf, key.first.begin(), 32);
        memcpy((buf + 8), &key.second, sizeof(int));
        return CalculateHash(buf, BUF_LEN/4, salt);
}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), hasModifier(false), cachedCoinsUsage(0) { }

CCoinsViewCache::~CCoinsViewCache()
{
    assert(!hasModifier);
}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) +
           memusage::DynamicUsage(cacheAnchors) +
           memusage::DynamicUsage(cacheNullifiers) +
           memusage::DynamicUsage(cacheSidechains) +
           memusage::DynamicUsage(cacheSidechainEvents) +
           memusage::DynamicUsage(cacheCswNullifiers) +
           memusage::DynamicUsage(cacheCertDataHashes) +
           cachedCoinsUsage;
}

CCoinsMap::const_iterator CCoinsViewCache::FetchCoins(const uint256 &txid) const {
    CCoinsMap::iterator it = cacheCoins.find(txid);
    if (it != cacheCoins.end())
        return it;
    CCoins tmp;
    if (!base->GetCoins(txid, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry())).first;
    tmp.swap(ret->second.coins);
    if (ret->second.coins.IsPruned()) {
        // The parent only has an empty entry for this txid; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coins.DynamicMemoryUsage();
    return ret;
}

CSidechainsMap::const_iterator CCoinsViewCache::FetchSidechains(const uint256& scId) const {
    CSidechainsMap::iterator candidateIt = cacheSidechains.find(scId);
    if (candidateIt != cacheSidechains.end())
        return candidateIt;

    CSidechain tmp;
    if (!base->GetSidechain(scId, tmp))
        return cacheSidechains.end();

    //Fill cache and return iterator. The insert in cache below looks cumbersome. However
    //it allows to insert CSidechain and keep iterator to inserted member without extra searches
    CSidechainsMap::iterator ret =
            cacheSidechains.insert(std::make_pair(scId, CSidechainsCacheEntry(tmp, CSidechainsCacheEntry::Flags::DEFAULT ))).first;

    cachedCoinsUsage += ret->second.scInfo.DynamicMemoryUsage();
    return ret;
}

CSidechainsMap::iterator CCoinsViewCache::ModifySidechain(const uint256& scId) {
    CSidechainsMap::iterator candidateIt = cacheSidechains.find(scId);
    if (candidateIt != cacheSidechains.end())
        return candidateIt;

    CSidechainsMap::iterator ret = cacheSidechains.end();
    CSidechain tmp;
    if (base->GetSidechain(scId, tmp))
        ret = cacheSidechains.insert(std::make_pair(scId, CSidechainsCacheEntry(tmp, CSidechainsCacheEntry::Flags::DEFAULT ))).first;
    else
        ret = cacheSidechains.insert(std::make_pair(scId, CSidechainsCacheEntry(tmp, CSidechainsCacheEntry::Flags::FRESH ))).first;

    cachedCoinsUsage += ret->second.scInfo.DynamicMemoryUsage();
    return ret;
}

const CSidechain* const CCoinsViewCache::AccessSidechain(const uint256& scId) {
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    if (it == cacheSidechains.end())
        return nullptr;
    else
        return &it->second.scInfo;
}

CSidechainEventsMap::const_iterator CCoinsViewCache::FetchSidechainEvents(int height) const {
    CSidechainEventsMap::iterator candidateIt = cacheSidechainEvents.find(height);
    if (candidateIt != cacheSidechainEvents.end())
        return candidateIt;

    CSidechainEvents tmp;
    if (!base->GetSidechainEvents(height, tmp))
        return cacheSidechainEvents.end();

    //Fill cache and return iterator. The insert in cache below looks cumbersome. However
    //it allows to insert CCeasingSidechains and keep iterator to inserted member without extra searches
    CSidechainEventsMap::iterator ret =
            cacheSidechainEvents.insert(std::make_pair(height, CSidechainEventsCacheEntry(tmp, CSidechainEventsCacheEntry::Flags::DEFAULT ))).first;

    cachedCoinsUsage += ret->second.scEvents.DynamicMemoryUsage();
    return ret;
}

CSidechainEventsMap::iterator CCoinsViewCache::ModifySidechainEvents(int height)
{
    CSidechainEventsMap::iterator candidateIt = cacheSidechainEvents.find(height);
    if (candidateIt != cacheSidechainEvents.end())
        return candidateIt;

    CSidechainEventsMap::iterator ret = cacheSidechainEvents.end();
    CSidechainEvents tmp;
    if (!base->GetSidechainEvents(height, tmp))
        ret = cacheSidechainEvents.insert(std::make_pair(height, CSidechainEventsCacheEntry(tmp, CSidechainEventsCacheEntry::Flags::FRESH ))).first;
    else
        ret = cacheSidechainEvents.insert(std::make_pair(height, CSidechainEventsCacheEntry(tmp, CSidechainEventsCacheEntry::Flags::DEFAULT ))).first;

    cachedCoinsUsage += ret->second.scEvents.DynamicMemoryUsage();
    return ret;
}

CCertDataHashMap::const_iterator CCoinsViewCache::FetchCertDataEntry(const uint256& scId, const int epoch) const {
    auto key = std::make_pair(scId, epoch);
    CCertDataHashMap::iterator candidateIt = cacheCertDataHashes.find(key);
    if (candidateIt != cacheCertDataHashes.end())
        return candidateIt;

    std::pair<libzendoomc::ScFieldElement, libzendoomc::ScFieldElement> baseData;
    if (!base->GetCertDataHashes(scId, epoch, baseData))
        return cacheCertDataHashes.end();

    //Fill cache and return iterator. The insert in cache below looks cumbersome. However
    //it allows to insert CCeasingSidechains and keep iterator to inserted member without extra searches
    CCertDataHashMap::iterator ret = cacheCertDataHashes.insert(std::make_pair(key,
        CCertDataHashCacheEntry(baseData, CCertDataHashCacheEntry::Flags::DEFAULT))).first;

    //cachedCoinsUsage += ret->second.scEvents.DynamicMemoryUsage(); //TODO
    return ret;
}

bool CCoinsViewCache::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const {
    CAnchorsMap::const_iterator it = cacheAnchors.find(rt);
    if (it != cacheAnchors.end()) {
        if (it->second.entered) {
            tree = it->second.tree;
            return true;
        } else {
            return false;
        }
    }

    if (!base->GetAnchorAt(rt, tree)) {
        return false;
    }

    CAnchorsMap::iterator ret = cacheAnchors.insert(std::make_pair(rt, CAnchorsCacheEntry())).first;
    ret->second.entered = true;
    ret->second.tree = tree;
    cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();

    return true;
}

bool CCoinsViewCache::GetNullifier(const uint256 &nullifier) const {
    CNullifiersMap::iterator it = cacheNullifiers.find(nullifier);
    if (it != cacheNullifiers.end())
        return it->second.entered;

    CNullifiersCacheEntry entry;
    bool tmp = base->GetNullifier(nullifier);
    entry.entered = tmp;

    cacheNullifiers.insert(std::make_pair(nullifier, entry));

    return tmp;
}

void CCoinsViewCache::PushAnchor(const ZCIncrementalMerkleTree &tree) {
    uint256 newrt = tree.root();

    auto currentRoot = GetBestAnchor();

    // We don't want to overwrite an anchor we already have.
    // This occurs when a block doesn't modify mapAnchors at all,
    // because there are no joinsplits. We could get around this a
    // different way (make all blocks modify mapAnchors somehow)
    // but this is simpler to reason about.
    if (currentRoot != newrt) {
        auto insertRet = cacheAnchors.insert(std::make_pair(newrt, CAnchorsCacheEntry()));
        CAnchorsMap::iterator ret = insertRet.first;

        ret->second.entered = true;
        ret->second.tree = tree;
        ret->second.flags = CAnchorsCacheEntry::DIRTY;

        if (insertRet.second) {
            // An insert took place
            cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();
        }

        hashAnchor = newrt;
    }
}

void CCoinsViewCache::PopAnchor(const uint256 &newrt) {
    auto currentRoot = GetBestAnchor();

    // Blocks might not change the commitment tree, in which
    // case restoring the "old" anchor during a reorg must
    // have no effect.
    if (currentRoot != newrt) {
        // Bring the current best anchor into our local cache
        // so that its tree exists in memory.
        {
            ZCIncrementalMerkleTree tree;
            assert(GetAnchorAt(currentRoot, tree));
        }

        // Mark the anchor as unentered, removing it from view
        cacheAnchors[currentRoot].entered = false;

        // Mark the cache entry as dirty so it's propagated
        cacheAnchors[currentRoot].flags = CAnchorsCacheEntry::DIRTY;

        // Mark the new root as the best anchor
        hashAnchor = newrt;
    }
}

void CCoinsViewCache::SetNullifier(const uint256 &nullifier, bool spent) {
    std::pair<CNullifiersMap::iterator, bool> ret = cacheNullifiers.insert(std::make_pair(nullifier, CNullifiersCacheEntry()));
    ret.first->second.entered = spent;
    ret.first->second.flags |= CNullifiersCacheEntry::DIRTY;
}

bool CCoinsViewCache::GetCoins(const uint256 &txid, CCoins &coins) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it != cacheCoins.end()) {
        coins = it->second.coins;
        return true;
    }
    return false;
}

CCoinsModifier CCoinsViewCache::ModifyCoins(const uint256 &txid) {
    assert(!hasModifier);
    std::pair<CCoinsMap::iterator, bool> ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    size_t cachedCoinUsage = 0;
    if (ret.second) {
        if (!base->GetCoins(txid, ret.first->second.coins)) {
            // The parent view does not have this entry; mark it as fresh.
            ret.first->second.coins.Clear();
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        } else if (ret.first->second.coins.IsPruned()) {
            // The parent view only has a pruned entry for this; mark it as fresh.
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        }
    } else {
        cachedCoinUsage = ret.first->second.coins.DynamicMemoryUsage();
    }
    // Assume that whenever ModifyCoins is called, the entry will be modified.
    ret.first->second.flags |= CCoinsCacheEntry::DIRTY;
    return CCoinsModifier(*this, ret.first, cachedCoinUsage);
}

const CCoins* CCoinsViewCache::AccessCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it == cacheCoins.end()) {
        return NULL;
    } else {
        return &it->second.coins;
    }
}

bool CCoinsViewCache::HaveCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    // We're using vtx.empty() instead of IsPruned here for performance reasons,
    // as we only care about the case where a transaction was replaced entirely
    // in a reorganization (which wipes vout entirely, as opposed to spending
    // which just cleans individual outputs).
    return (it != cacheCoins.end() && !it->second.coins.vout.empty());
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
        hashBlock = base->GetBestBlock();
    return hashBlock;
}


uint256 CCoinsViewCache::GetBestAnchor() const {
    if (hashAnchor.IsNull())
        hashAnchor = base->GetBestAnchor();
    return hashAnchor;
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

void CCoinsViewCache::AddCswNullifier(const uint256& scId,
                         const libzendoomc::ScFieldElement &nullifier) {
    std::pair<uint256, libzendoomc::ScFieldElement> position = std::make_pair(scId, nullifier);
    std::pair<CCswNullifiersMap::iterator, bool> ret = cacheCswNullifiers.insert(std::make_pair(position, CCswNullifiersCacheEntry()));
    ret.first->second.flag = CCswNullifiersCacheEntry::FRESH;
}

void CCoinsViewCache::RemoveCswNullifier(const uint256& scId,
                         const libzendoomc::ScFieldElement &nullifier) {
    std::pair<uint256, libzendoomc::ScFieldElement> position = std::make_pair(scId, nullifier);
    std::pair<CCswNullifiersMap::iterator, bool> ret = cacheCswNullifiers.insert(std::make_pair(position, CCswNullifiersCacheEntry()));
    ret.first->second.flag = CCswNullifiersCacheEntry::ERASED;
}

bool CCoinsViewCache::HaveCswNullifier(const uint256& scId,
                         const libzendoomc::ScFieldElement &nullifier) const {
    std::pair<uint256, libzendoomc::ScFieldElement> position = std::make_pair(scId, nullifier);
    
    CCswNullifiersMap::iterator it = cacheCswNullifiers.find(position);
    if (it != cacheCswNullifiers.end())
        return (it->second.flag == CCswNullifiersCacheEntry::ERASED);

    bool tmp = base->HaveCswNullifier(scId, nullifier);

    if (tmp) {
        CCswNullifiersCacheEntry entry;
        entry.flag = CCswNullifiersCacheEntry::DEFAULT;
        cacheCswNullifiers.insert(std::make_pair(position, entry));
    }

    return tmp;
}

void CCoinsViewCache::UpdateCertDataHash(const uint256& scId, const int epoch, const libzendoomc::ScFieldElement &certDataHash, CBlockUndo& blockUndo) {
	assert(HaveSidechain(scId));
    if (HaveCertDataHashes(scId, epoch))
    {
    	//pick or create scId entry in scUndoDatabyScId
    	blockUndo.scUndoDatabyScId[scId].prevTopCommittedCertDataHash = this->cacheCertDataHashes.at(std::make_pair(scId, epoch)).certDataHash;
    	blockUndo.scUndoDatabyScId[scId].contentBitMask |= CSidechainUndoData::AvailableSections::CERT_DATA_HASH;

        this->cacheCertDataHashes.at(std::make_pair(scId, epoch)).certDataHash = certDataHash;
        this->cacheCertDataHashes.at(std::make_pair(scId, epoch)).flag = CCertDataHashCacheEntry::Flags::DIRTY;
    } else
    {
        libzendoomc::ScFieldElement updatedprevEpochCumulativeCertDataHash;

        if (HaveCertDataHashes(scId, epoch-1))
        {
            // Update prevEpochCumulativeCertDataHash
            const auto & prevEpochCertDataHash = this->cacheCertDataHashes.at(std::make_pair(scId, epoch-1)).certDataHash;
            const auto & prevEpochCumulativeCertDataHash = this->cacheCertDataHashes.at(std::make_pair(scId, epoch-1)).prevEpochCumulativeCertDataHash;
            libzendoomc::CalculateCumulativeCertDataHash(prevEpochCumulativeCertDataHash, prevEpochCertDataHash, updatedprevEpochCumulativeCertDataHash);
        } else {
            // use default value prevEpochCumulativeCertDataHash
            updatedprevEpochCumulativeCertDataHash = libzendoomc::ScFieldElement();
        }

        CCertDataHashCacheEntry newEntry{std::make_pair(certDataHash, updatedprevEpochCumulativeCertDataHash),CCertDataHashCacheEntry::Flags::FRESH};
        this->cacheCertDataHashes[std::make_pair(scId, epoch)] = newEntry;
    }
}

void CCoinsViewCache::RestoreCertDataHash(const uint256& scId, const int epoch, const CBlockUndo& blockUndo) {
	assert(HaveCertDataHashes(scId, epoch));
	if(blockUndo.scUndoDatabyScId.count(scId) &&
       (blockUndo.scUndoDatabyScId.at(scId).contentBitMask & CSidechainUndoData::AvailableSections::CERT_DATA_HASH))
	{
        this->cacheCertDataHashes.at(std::make_pair(scId, epoch)).certDataHash = blockUndo.scUndoDatabyScId.at(scId).prevTopCommittedCertDataHash;
        this->cacheCertDataHashes.at(std::make_pair(scId, epoch)).flag = CCertDataHashCacheEntry::Flags::DIRTY;
	} else
	{
	    this->cacheCertDataHashes.at(std::make_pair(scId, epoch)).flag = CCertDataHashCacheEntry::Flags::ERASED;
	}
}

bool CCoinsViewCache::HaveCertDataHashes(const uint256& scId, const int epoch) const {
    CCertDataHashMap::const_iterator it = FetchCertDataEntry(scId, epoch);
    if (it != cacheCertDataHashes.end() && it->second.flag != CCertDataHashCacheEntry::Flags::ERASED)
        return true;

    return false;
}

bool CCoinsViewCache::GetCertDataHashes(const uint256& scId, const int epoch,
                       std::pair<libzendoomc::ScFieldElement, libzendoomc::ScFieldElement>& certDataHashes) const
{
    CCertDataHashMap::const_iterator it = FetchCertDataEntry(scId, epoch);
    if (it != cacheCertDataHashes.end() && it->second.flag != CCertDataHashCacheEntry::Flags::ERASED) {
        certDataHashes.first = it->second.certDataHash;
        certDataHashes.second = it->second.prevEpochCumulativeCertDataHash;
        return true;
    }

    return false;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins,
                                 const uint256 &hashBlockIn,
                                 const uint256 &hashAnchorIn,
                                 CAnchorsMap &mapAnchors,
                                 CNullifiersMap &mapNullifiers,
                                 CSidechainsMap& mapSidechains,
                                 CSidechainEventsMap& mapSidechainEvents,
                                 CCswNullifiersMap& cswNullifiers,
                                 CCertDataHashMap& certDataHashes) {
    assert(!hasModifier);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                if (!it->second.coins.IsPruned()) {
                    // The parent cache does not have an entry, while the child
                    // cache does have (a non-pruned) one. Move the data up, and
                    // mark it as fresh (if the grandparent did have it, we
                    // would have pulled it in at first GetCoins).
                    assert(it->second.flags & CCoinsCacheEntry::FRESH);
                    CCoinsCacheEntry& entry = cacheCoins[it->first];
                    entry.coins.swap(it->second.coins);
                    cachedCoinsUsage += entry.coins.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY | CCoinsCacheEntry::FRESH;
                }
            } else {
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.coins.swap(it->second.coins);
                    cachedCoinsUsage += itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                }
            }
        }
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }

    for (CAnchorsMap::iterator child_it = mapAnchors.begin(); child_it != mapAnchors.end();)
    {
        if (child_it->second.flags & CAnchorsCacheEntry::DIRTY) {
            CAnchorsMap::iterator parent_it = cacheAnchors.find(child_it->first);

            if (parent_it == cacheAnchors.end()) {
                CAnchorsCacheEntry& entry = cacheAnchors[child_it->first];
                entry.entered = child_it->second.entered;
                entry.tree = child_it->second.tree;
                entry.flags = CAnchorsCacheEntry::DIRTY;

                cachedCoinsUsage += entry.tree.DynamicMemoryUsage();
            } else {
                if (parent_it->second.entered != child_it->second.entered) {
                    // The parent may have removed the entry.
                    parent_it->second.entered = child_it->second.entered;
                    parent_it->second.flags |= CAnchorsCacheEntry::DIRTY;
                }
            }
        }

        CAnchorsMap::iterator itOld = child_it++;
        mapAnchors.erase(itOld);
    }

    for (CNullifiersMap::iterator child_it = mapNullifiers.begin(); child_it != mapNullifiers.end();)
    {
        if (child_it->second.flags & CNullifiersCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CNullifiersMap::iterator parent_it = cacheNullifiers.find(child_it->first);

            if (parent_it == cacheNullifiers.end()) {
                CNullifiersCacheEntry& entry = cacheNullifiers[child_it->first];
                entry.entered = child_it->second.entered;
                entry.flags = CNullifiersCacheEntry::DIRTY;
            } else {
                if (parent_it->second.entered != child_it->second.entered) {
                    parent_it->second.entered = child_it->second.entered;
                    parent_it->second.flags |= CNullifiersCacheEntry::DIRTY;
                }
            }
        }
        CNullifiersMap::iterator itOld = child_it++;
        mapNullifiers.erase(itOld);
    }

    for (auto& entryToWrite : mapSidechains) {
        CSidechainsMap::iterator itLocalCacheEntry = cacheSidechains.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CSidechainsCacheEntry::Flags::FRESH:
                assert(
                    itLocalCacheEntry == cacheSidechains.end() ||
                    itLocalCacheEntry->second.flag == CSidechainsCacheEntry::Flags::ERASED
                ); //A fresh entry should not exist in localCache or be already erased
                cacheSidechains[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainsCacheEntry::Flags::DIRTY: //A dirty entry may or may not exist in localCache
                cacheSidechains[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainsCacheEntry::Flags::ERASED:
                if (itLocalCacheEntry != cacheSidechains.end())
                    itLocalCacheEntry->second.flag = CSidechainsCacheEntry::Flags::ERASED;
                break;
            case CSidechainsCacheEntry::Flags::DEFAULT:
                assert(itLocalCacheEntry != cacheSidechains.end());
                assert(itLocalCacheEntry->second.scInfo == entryToWrite.second.scInfo); //entry declared default is indeed different from backed value
                break; //nothing to do. entry is already persisted and has not been modified
            default:
                assert(false);
        }
    }
    mapSidechains.clear();

    for (auto& entryToWrite : mapSidechainEvents) {
        CSidechainEventsMap::iterator itLocalCacheEntry = cacheSidechainEvents.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CSidechainEventsCacheEntry::Flags::FRESH:
                assert(
                    itLocalCacheEntry == cacheSidechainEvents.end() ||
                    itLocalCacheEntry->second.flag == CSidechainEventsCacheEntry::Flags::ERASED
                ); //A fresh entry should not exist in localCache or be already erased
                cacheSidechainEvents[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainEventsCacheEntry::Flags::DIRTY: //A dirty entry may or may not exist in localCache
                cacheSidechainEvents[entryToWrite.first] = entryToWrite.second;
                break;
            case CSidechainEventsCacheEntry::Flags::ERASED:
                if (itLocalCacheEntry != cacheSidechainEvents.end())
                    itLocalCacheEntry->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;
                break;
            case CSidechainEventsCacheEntry::Flags::DEFAULT:
                assert(itLocalCacheEntry != cacheSidechainEvents.end());
                assert(itLocalCacheEntry->second.scEvents == entryToWrite.second.scEvents); //entry declared default is indeed different from backed value
                break; //nothing to do. entry is already persisted and has not been modified
            default:
                assert(false);
        }
    }
    mapSidechainEvents.clear();

    for (auto& entryToWrite : cswNullifiers) {
        CCswNullifiersMap::iterator itLocalCacheEntry = cacheCswNullifiers.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CCswNullifiersCacheEntry::FRESH:
                assert(
                    itLocalCacheEntry == cacheCswNullifiers.end() ||
                    itLocalCacheEntry->second.flag == CCswNullifiersCacheEntry::ERASED
                ); //A fresh entry should not exist in localCache or be already erased
                cacheCswNullifiers[entryToWrite.first] = entryToWrite.second;
                break;
            case CCswNullifiersCacheEntry::ERASED:
                if (itLocalCacheEntry != cacheCswNullifiers.end())
                    itLocalCacheEntry->second.flag = CCswNullifiersCacheEntry::ERASED;
                break;
            case CCswNullifiersCacheEntry::DEFAULT:
                assert(itLocalCacheEntry != cacheCswNullifiers.end());
                break;
            default:
                assert(false);
        }
    }
    cswNullifiers.clear();

    for (auto& entryToWrite : certDataHashes) {
        CCertDataHashMap::iterator itLocalCacheEntry = cacheCertDataHashes.find(entryToWrite.first);

        switch (entryToWrite.second.flag) {
            case CCertDataHashCacheEntry::Flags::FRESH:
                assert(
                    itLocalCacheEntry == cacheCertDataHashes.end() ||
                    itLocalCacheEntry->second.flag == CCertDataHashCacheEntry::Flags::ERASED
                ); //A fresh entry should not exist in localCache or be already erased
                cacheCertDataHashes[entryToWrite.first] = entryToWrite.second;
                break;
            case CCertDataHashCacheEntry::Flags::DIRTY: //A dirty entry may or may not exist in localCache
                cacheCertDataHashes[entryToWrite.first] = entryToWrite.second;
                break;
            case CCertDataHashCacheEntry::Flags::ERASED:
                if (itLocalCacheEntry != cacheCertDataHashes.end())
                    itLocalCacheEntry->second.flag = CCertDataHashCacheEntry::Flags::ERASED;
                break;
            case CCertDataHashCacheEntry::Flags::DEFAULT:
                assert(itLocalCacheEntry != cacheCertDataHashes.end());
                assert(itLocalCacheEntry->second.certDataHash == entryToWrite.second.certDataHash); //entry declared default is indeed different from backed value
                break; //nothing to do. entry is already persisted and has not been modified
            default:
                assert(false);
        }
    }
    certDataHashes.clear();

    hashAnchor = hashAnchorIn;
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::HaveSidechain(const uint256& scId) const
{
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    return (it != cacheSidechains.end()) && (it->second.flag != CSidechainsCacheEntry::Flags::ERASED);
}

bool CCoinsViewCache::GetSidechain(const uint256 & scId, CSidechain& targetScInfo) const
{
    CSidechainsMap::const_iterator it = FetchSidechains(scId);
    if (it != cacheSidechains.end())
        LogPrint("sc", "%s():%d - FetchedSidechain: scId[%s]\n", __func__, __LINE__, scId.ToString());

    if (it != cacheSidechains.end() && it->second.flag != CSidechainsCacheEntry::Flags::ERASED) {
        targetScInfo = it->second.scInfo;
        return true;
    }
    return false;
}

void CCoinsViewCache::GetScIds(std::set<uint256>& scIdsList) const
{
    base->GetScIds(scIdsList);

    // Note that some of the values above may have been erased in current cache.
    // Also new id may be in current cache but not in persisted
    for (const auto& entry: cacheSidechains)
    {
      if (entry.second.flag == CSidechainsCacheEntry::Flags::ERASED)
          scIdsList.erase(entry.first);
      else
          scIdsList.insert(entry.first);
    }

    return;
}

bool CCoinsViewCache::CheckQuality(const CScCertificate& cert) const
{
    // check in blockchain if a better cert is already there for this epoch
    CSidechain info;
    if (GetSidechain(cert.GetScId(), info))
    {
        if (info.prevBlockTopQualityCertHash != cert.GetHash() &&
            info.prevBlockTopQualityCertReferencedEpoch == cert.epochNumber &&
            info.prevBlockTopQualityCertQuality >= cert.quality)
        {
            LogPrint("cert", "%s.%s():%d - NOK, cert %s q=%d : a cert q=%d for same sc/epoch is already in blockchain\n",
                __FILE__, __func__, __LINE__, cert.GetHash().ToString(), cert.quality, info.prevBlockTopQualityCertQuality);
            return false;
        }
    }
    else
    {
        LogPrint("cert", "%s.%s():%d - cert %s has no scid in blockchain\n",
            __FILE__, __func__, __LINE__, cert.GetHash().ToString());
    }

    LogPrint("cert", "%s.%s():%d - cert %s q=%d : OK, no better quality certs for same sc/epoch are in blockchain\n",
        __FILE__, __func__, __LINE__, cert.GetHash().ToString(), cert.quality);
    return true;
}


int CCoinsViewCache::getInitScCoinsMaturity()
{
    if ( (Params().NetworkIDString() == "regtest") )
    {
        int val = (int)(GetArg("-sccoinsmaturity", Params().ScCoinsMaturity() ));
        LogPrint("sc", "%s():%d - %s: using val %d \n", __func__, __LINE__, Params().NetworkIDString(), val);
        return val;
    }
    return Params().ScCoinsMaturity();
}

int CCoinsViewCache::getScCoinsMaturity()
{
    // gets constructed just one time
    static int retVal( getInitScCoinsMaturity() );
    return retVal;
}

bool CCoinsViewCache::UpdateScInfo(const CTransaction& tx, const CBlock& block, int blockHeight)
{
    const uint256& txHash = tx.GetHash();
    LogPrint("sc", "%s():%d - enter tx=%s\n", __func__, __LINE__, txHash.ToString() );

    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = blockHeight + SC_COIN_MATURITY;

    // ceased sidechain withdrawal ccin
    for (const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        if (!HaveSidechain(csw.scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, csw.scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(csw.scId);

        // decrease SC balance
        scIt->second.scInfo.balance -= csw.nValue;
        assert(scIt->second.scInfo.balance >= 0);
        scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - sidechain balance decreased by CSW in scView csw_amount=%s scId=%s\n",
            __func__, __LINE__, FormatMoney(csw.nValue), csw.scId.ToString());
    }

    // creation ccout
    for (const auto& cr: tx.GetVscCcOut())
    {
        const uint256& scId = cr.GetScId();
        if (HaveSidechain(scId)) {
            LogPrintf("ERROR: %s():%d - CR: scId=%s already in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        scIt->second.scInfo.creationBlockHash = block.GetHash();
        scIt->second.scInfo.creationBlockHeight = blockHeight;
        scIt->second.scInfo.creationTxHash = txHash;
        scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch = CScCertificate::EPOCH_NULL;
        scIt->second.scInfo.prevBlockTopQualityCertHash.SetNull();
        scIt->second.scInfo.prevBlockTopQualityCertQuality = CScCertificate::QUALITY_NULL;
        scIt->second.scInfo.prevBlockTopQualityCertBwtAmount = 0;
        scIt->second.scInfo.creationData.withdrawalEpochLength = cr.withdrawalEpochLength;
        scIt->second.scInfo.creationData.customData = cr.customData;
        scIt->second.scInfo.creationData.constant = cr.constant;
        scIt->second.scInfo.creationData.wCertVk = cr.wCertVk;
        scIt->second.scInfo.creationData.wCeasedVk = cr.wCeasedVk;
        scIt->second.scInfo.mImmatureAmounts[maturityHeight] = cr.nValue;
        scIt->second.scInfo.currentState = (uint8_t)CSidechain::State::ALIVE;
        scIt->second.flag = CSidechainsCacheEntry::Flags::FRESH;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(cr.nValue), scId.ToString());

        LogPrint("sc", "%s():%d - scId[%s] added in scView\n", __func__, __LINE__, scId.ToString() );
    }

    // forward transfer ccout
    for(auto& ft: tx.GetVftCcOut())
    {
        if (!HaveSidechain(ft.scId))
        {
            // should not happen
            LogPrintf("%s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, ft.scId.ToString() );
            return false;
        }
        CSidechainsMap::iterator scIt = ModifySidechain(ft.scId);

        // add a new immature balance entry in sc info or increment it if already there
        scIt->second.scInfo.mImmatureAmounts[maturityHeight] += ft.nValue;
        if (scIt->second.flag != CSidechainsCacheEntry::Flags::FRESH)
            scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - immature balance added in scView (h=%d, amount=%s) %s\n",
            __func__, __LINE__, maturityHeight, FormatMoney(ft.nValue), ft.scId.ToString());
    }

    return true;
}

bool CCoinsViewCache::RevertTxOutputs(const CTransaction& tx, int nHeight)
{
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = nHeight + SC_COIN_MATURITY;

    // revert forward transfers
    for(const auto& entry: tx.GetVftCcOut())
    {
        const uint256& scId = entry.scId;
        LogPrint("sc", "%s():%d - removing fwt for scId=%s\n", __func__, __LINE__, scId.ToString());

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        if (!DecrementImmatureAmount(scId, scIt, entry.nValue, maturityHeight) )
        {
            // should not happen
            LogPrintf("ERROR %s():%d - scId=%s could not handle immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }
    }

    // remove sidechain if the case
    for(const auto& entry: tx.GetVscCcOut())
    {
        const uint256& scId = entry.GetScId();
        LogPrint("sc", "%s():%d - removing scId=%s\n", __func__, __LINE__, scId.ToString());

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        if (!DecrementImmatureAmount(scId, scIt, entry.nValue, maturityHeight) )
        {
            // should not happen
            LogPrintf("ERROR %s():%d - scId=%s could not handle immature balance at height%d\n",
                __func__, __LINE__, scId.ToString(), maturityHeight);
            return false;
        }

        if (scIt->second.scInfo.balance > 0)
        {
            // should not happen either
            LogPrintf("ERROR %s():%d - scId=%s balance not null: %s\n",
                __func__, __LINE__, scId.ToString(), FormatMoney(scIt->second.scInfo.balance));
            return false;
        }

        scIt->second.flag = CSidechainsCacheEntry::Flags::ERASED;
        LogPrint("sc", "%s():%d - scId=%s removed from scView\n", __func__, __LINE__, scId.ToString() );
    }

    // revert sidechain balances for CSWs
    for (const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        LogPrint("sc", "%s():%d - removing CSW for scId=%s\n", __func__, __LINE__, csw.scId.ToString());

        if (!HaveSidechain(csw.scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - Can not update balance, could not find scId=%s\n",
                __func__, __LINE__, csw.scId.ToString() );
            return false;
        }

        CSidechainsMap::iterator scIt = ModifySidechain(csw.scId);

        // increase SC balance
        scIt->second.scInfo.balance += csw.nValue;
        scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

        LogPrint("sc", "%s():%d - sidechain balance increased by CSW in scView csw_amount=%s scId=%s\n",
            __func__, __LINE__, FormatMoney(csw.nValue), csw.scId.ToString());
    }

    return true;
}

#ifdef BITCOIN_TX
int CSidechain::EpochFor(int targetHeight) const { return CScCertificate::EPOCH_NULL; }
int CSidechain::StartHeightForEpoch(int targetEpoch) const { return -1; }
int CSidechain::SafeguardMargin() const { return -1; }
size_t CSidechain::DynamicMemoryUsage() const { return 0; }
std::string CSidechain::stateToString(State s) { return "";}
bool CCoinsViewCache::isEpochDataValid(const CSidechain& info, int epochNumber, const uint256& endEpochBlockHash) const {return true;}
bool CCoinsViewCache::IsTxCswApplicableToState(const CTransaction& tx, CValidationState& state, libzendoomc::CScProofVerifier& scVerifier) const {return true;}
bool libzendoomc::CScProofVerifier::verifyCTxCeasedSidechainWithdrawalInput(
        const ScFieldElement& prevCumulativeCertDataHash,
        const ScFieldElement& currentCertDataHash,
        const ScFieldElement& lastCumulativeCertDataHash,
        const ScVk& wCeasedVk,
        const CTxCeasedSidechainWithdrawalInput& csw
    ) const { return true; }
bool libzendoomc::CalculateCumulativeCertDataHash(const ScFieldElement& prevCumulativeHash, const ScFieldElement& prevCertHash, ScFieldElement& CumulativeHashOutput) {return true;};
bool CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state, libzendoomc::CScProofVerifier& scVerifier) const {return true;}
bool libzendoomc::CScProofVerifier::verifyCScCertificate(              
    const libzendoomc::ScConstant& constant,
    const libzendoomc::ScVk& wCertVk,
    const uint256& prev_end_epoch_block_hash,
    const CScCertificate& scCert
) const { return true; }
bool CCoinsViewCache::HaveScRequirements(const CTransaction& tx) { return true; }
size_t CSidechainEvents::DynamicMemoryUsage() const { return 0;}

#else

#include "consensus/validation.h"
#include "main.h"

bool CCoinsViewCache::IsTxCswApplicableToState(const CTransaction& tx, CValidationState& state, libzendoomc::CScProofVerifier& scVerifier) const
{
    // Key is Sc id, value - total amount of coins to be withdrawn by Tx CSWs for given sidechain
    std::map<uint256, CAmount> cswTotalBalances;

    for(const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        CSidechain scInfo;
        if (!GetSidechain(csw.scId, scInfo))
        {
            LogPrintf("%s():%d - tx[%s] CSW input [%s] refers to scId not yet created\n",
                __func__, __LINE__, tx.ToString(), csw.ToString() );
            return state.Invalid(error("scid does not exist"),
                 REJECT_INVALID, "tx-csw-input-scid-missed");
        }

        if ((CSidechain::State)scInfo.currentState != CSidechain::State::CEASED) {
            LogPrintf("ERROR: Tx[%s] CSW input [%s] cannot be accepted, sidechain not ceased at height = %d\n",
                tx.ToString(), csw.ToString(), chainActive.Height());
            return state.Invalid(error("received a csw for not ceased sidechain"),
                         REJECT_INVALID, "tx-csw-input-scid-not-ceased");
        }

        if(!scInfo.creationData.wCeasedVk.is_initialized())
        {
            LogPrintf("%s():%d - tx[%s] CSW input [%s] refers to SC without CSW support\n",
                __func__, __LINE__, tx.ToString(), csw.ToString() );
            return state.Invalid(error("received a csw for sc without CSW support"),
                 REJECT_INVALID, "tx-csw-input-sc-without-csw-support");
        }

        // add a new balance entry in the map or increment it if already there
        cswTotalBalances[csw.scId] += csw.nValue;

        for(const CTxCeasedSidechainWithdrawalInput& cswIn : tx.GetVcswCcIn()) {
            if (pcoinsTip->HaveCswNullifier(csw.scId, cswIn.nullifier)) {
                return state.Invalid(error("csw input had been already used"),
                     REJECT_INVALID, "tx-csw-input-used-nullifier");                
            }
        }

        std::pair<libzendoomc::ScFieldElement, libzendoomc::ScFieldElement> certDataHashes;
        this->GetCertDataHashes(csw.scId, csw.nEpoch, certDataHashes);
        const libzendoomc::ScFieldElement& prevCumulativeCertDataHash = certDataHashes.second;
        const libzendoomc::ScFieldElement& currentCertDataHash = certDataHashes.first;
        libzendoomc::ScFieldElement lastCumulativeCertDataHash = libzendoomc::ScFieldElement();
        libzendoomc::CalculateCumulativeCertDataHash(prevCumulativeCertDataHash, currentCertDataHash, lastCumulativeCertDataHash);
        //TODO: check that field elements are not null/empty.

        // Verify CSW proof
        if (!scVerifier.verifyCTxCeasedSidechainWithdrawalInput(prevCumulativeCertDataHash, currentCertDataHash,
                                                                lastCumulativeCertDataHash, scInfo.creationData.wCeasedVk.get(), csw))
        {
            LogPrintf("ERROR: tx[%s] CSW input [%s] cannot be accepted: proof verification failed\n",
                tx.ToString(), csw.ToString());
            return state.Invalid(error("proof not verified"),
                         REJECT_INVALID, "tx-csw-input-proof-not-verified");
        }
    }

    // Check that CSW balances don't exceed the SC balance
    for (auto const& totalBalance: cswTotalBalances)
    {
        CSidechain scInfo;
        GetSidechain(totalBalance.first, scInfo);
        if(totalBalance.second > scInfo.balance)
        {
            LogPrintf("ERROR: tx[%s] CSW inputs total amount is greater than sidechain [%s] total amount\n",
                tx.ToString(), totalBalance.first.ToString());
            return state.Invalid(error("CSW inputs total amount is greater than sidechain total amount"),
                         REJECT_INVALID, "tx-csw-inputs-amount-greater-than-sc-balance");
        }
    }

    return true;
}

bool CCoinsViewCache::IsCertApplicableToState(const CScCertificate& cert, int nHeight, CValidationState& state, libzendoomc::CScProofVerifier& scVerifier) const
{
    const uint256& certHash = cert.GetHash();

    LogPrint("cert", "%s():%d - called: cert[%s], scId[%s], height[%d]\n",
        __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString(), nHeight );

    CSidechain scInfo;
    if (!GetSidechain(cert.GetScId(), scInfo))
    {
        LogPrintf("%s():%d - cert[%s] refers to scId[%s] not yet created\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString() );
        return state.Invalid(error("scid does not exist"),
             REJECT_INVALID, "sidechain-certificate-scid");
    }

    // check that epoch data are consistent
    if (!isEpochDataValid(scInfo, cert.epochNumber, cert.endEpochBlockHash) )
    {
        LogPrintf("%s():%d - invalid cert[%s], scId[%s] invalid epoch data\n",
            __func__, __LINE__, certHash.ToString(), cert.GetScId().ToString() );
        return state.Invalid(error("certificate with invalid epoch"),
             REJECT_INVALID, "sidechain-certificate-epoch");
    }

    if (nHeight < scInfo.StartHeightForEpoch(cert.epochNumber+1))
    {
        LogPrintf("%s():%d - invalid cert[%s] received too early (cert epoch %d, current height %d, current epoch %d)\n",
            __func__, __LINE__, certHash.ToString(), cert.epochNumber, nHeight, scInfo.prevBlockTopQualityCertReferencedEpoch+1);
        return state.Invalid(error("cert epoch not acceptable at this height"),
             REJECT_INVALID, "sidechain-certificate-epoch");
    }

    if (GetSidechainState(cert.GetScId()) != CSidechain::State::ALIVE) {
        LogPrintf("ERROR: certificate[%s] cannot be accepted, sidechain [%s] already ceased at height = %d (chain.h = %d)\n",
            certHash.ToString(), cert.GetScId().ToString(), nHeight, chainActive.Height());
        return state.Invalid(error("received a delayed cert"),
                     REJECT_INVALID, "sidechain-certificate-delayed");
    }

    if (!CheckQuality(cert))
    {
        LogPrintf("%s():%d - Dropping cert %s : invalid quality\n", __func__, __LINE__, certHash.ToString());
        return state.Invalid(error("invalid quality"),
                             REJECT_INVALID, "sidechain-invalid-quality-certificate");
    }

    CAmount bwtTotalAmount = cert.GetValueOfBackwardTransfers(); 
    CAmount scBalance = scInfo.balance;
    if (cert.epochNumber == scInfo.prevBlockTopQualityCertReferencedEpoch) 
    {
        // if we are targeting the same epoch of an existing certificate, add
        // to the scInfo.balance the amount of the former top-quality cert if any
        scBalance += scInfo.prevBlockTopQualityCertBwtAmount;
    }

    if (bwtTotalAmount > scBalance)
    {
        LogPrintf("%s():%d - insufficent balance in scId[%s]: balance[%s], cert amount[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString(), FormatMoney(scBalance), FormatMoney(bwtTotalAmount) );
        return state.Invalid(error("insufficient balance"),
                     REJECT_INVALID, "sidechain-insufficient-balance");
    }
    LogPrint("sc", "%s():%d - ok, balance in scId[%s]: balance[%s], cert amount[%s]\n",
        __func__, __LINE__, cert.GetScId().ToString(), FormatMoney(scBalance), FormatMoney(bwtTotalAmount) );

    // Retrieve previous end epoch block hash for certificate proof verification
    int targetHeight = scInfo.StartHeightForEpoch(cert.epochNumber) - 1;
    uint256 prev_end_epoch_block_hash = chainActive[targetHeight] -> GetBlockHash();

    // Verify certificate proof
    if (!scVerifier.verifyCScCertificate(scInfo.creationData.constant, scInfo.creationData.wCertVk, prev_end_epoch_block_hash, cert)){
        LogPrintf("ERROR: certificate[%s] cannot be accepted for sidechain [%s]: proof verification failed\n",
            certHash.ToString(), cert.GetScId().ToString());
        return state.Invalid(error("proof not verified"),
                     REJECT_INVALID, "sidechain-certificate-proof-not-verified");
    }

    return true;
}

bool CCoinsViewCache::isEpochDataValid(const CSidechain& scInfo, int epochNumber, const uint256& endEpochBlockHash) const
{
    if (epochNumber < 0 || endEpochBlockHash.IsNull())
    {
        LogPrint("sc", "%s():%d - invalid epoch data %d/%s\n", __func__, __LINE__, epochNumber, endEpochBlockHash.ToString() );
        return false;
    }

    // Adding handling of quality, we can have also certificates for the same epoch of the last certificate
    // 1. the epoch number must be consistent with the sc certificate history (no old epoch allowed)
    if ( epochNumber != scInfo.prevBlockTopQualityCertReferencedEpoch &&
         epochNumber != scInfo.prevBlockTopQualityCertReferencedEpoch + 1)

    {
        LogPrint("sc", "%s():%d - can not receive a certificate for epoch %d (expected: %d or %d)\n",
            __func__, __LINE__, epochNumber,
            scInfo.prevBlockTopQualityCertReferencedEpoch, scInfo.prevBlockTopQualityCertReferencedEpoch+1);
        return false;
    }

    // 2. the referenced end-epoch block must be in active chain
    LOCK(cs_main);
    if (mapBlockIndex.count(endEpochBlockHash) == 0)
    {
        LogPrint("sc", "%s():%d - endEpochBlockHash %s is not in block index map\n",
            __func__, __LINE__, endEpochBlockHash.ToString() );
        return false;
    }

    CBlockIndex* pblockindex = mapBlockIndex[endEpochBlockHash];
    if (!chainActive.Contains(pblockindex))
    {
        LogPrint("sc", "%s():%d - endEpochBlockHash %s refers to a valid block but is not in active chain\n",
            __func__, __LINE__, endEpochBlockHash.ToString() );
        return false;
    }

    // 3. combination of epoch number and epoch length, specified in sc info, must point to that end-epoch block
    int endEpochHeight = scInfo.StartHeightForEpoch(epochNumber+1) -1;
    pblockindex = chainActive[endEpochHeight];

    if (!pblockindex)
    {
        LogPrint("sc", "%s():%d - calculated height %d (createHeight=%d/epochNum=%d/epochLen=%d) is out of active chain\n",
            __func__, __LINE__, endEpochHeight, scInfo.creationBlockHeight, epochNumber, scInfo.creationData.withdrawalEpochLength);
        return false;
    }

    const uint256& hash = pblockindex->GetBlockHash();
    if (hash != endEpochBlockHash)
    {
        LogPrint("sc", "%s():%d - bock hash mismatch: endEpochBlockHash[%s] / calculated[%s]\n",
            __func__, __LINE__, endEpochBlockHash.ToString(), hash.ToString());
        return false;
    }

    return true;
}

bool CCoinsViewCache::HaveScRequirements(const CTransaction& tx)
{
    if (tx.IsCoinBase())
        return true;

    const uint256& txHash = tx.GetHash();

    // check creation
    for (const auto& sc: tx.GetVscCcOut())
    {
        const uint256& scId = sc.GetScId();
        if (HaveSidechain(scId))
        {
            LogPrintf("%s():%d - ERROR: Invalid tx[%s] : scid[%s] already created\n",
                __func__, __LINE__, txHash.ToString(), scId.ToString());
            return false;
        }
        LogPrint("sc", "%s():%d - OK: tx[%s] is creating scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), scId.ToString());
    }

    // check fw tx
    for (const auto& ft: tx.GetVftCcOut())
    {
        const uint256& scId = ft.scId;
        if (HaveSidechain(scId))
        {
            auto s = GetSidechainState(scId);
            if (s != CSidechain::State::ALIVE && s != CSidechain::State::UNCONFIRMED)
            {
                LogPrintf("ERROR: tx[%s] tries to send funds to scId[%s] with state(%s)\n",
                            txHash.ToString(), scId.ToString(), CSidechain::stateToString(s));
                return false;
            }
        } else {
            if (!Sidechain::hasScCreationOutput(tx, scId)) {
                LogPrintf("%s():%d - ERROR: tx [%s] tries to send funds to scId[%s] not yet created\n",
                        __func__, __LINE__, txHash.ToString(), scId.ToString() );
                return false;
            }
        }

        LogPrint("sc", "%s():%d - OK: tx[%s] is sending [%s] to scId[%s]\n",
            __func__, __LINE__, txHash.ToString(), FormatMoney(ft.nValue), scId.ToString());
    }

    // check CSW inputs
    for (const CTxCeasedSidechainWithdrawalInput& csw: tx.GetVcswCcIn())
    {
        auto s = GetSidechainState(csw.scId);
        if (s != CSidechain::State::CEASED)
        {
            LogPrintf("ERROR: tx[%s] tries to send make ceased sidechain withdrawal to not yet ceased sidechain scId[%s] with state(%s)\n",
                        txHash.ToString(), csw.scId.ToString(), CSidechain::stateToString(s));
            return false;
        }
    }

    return true;
}

#endif

bool CCoinsViewCache::UpdateScInfo(const CScCertificate& cert, CBlockUndo& blockUndo)
{
    const uint256& certHash       = cert.GetHash();
    const uint256& scId           = cert.GetScId();
    const CAmount& bwtTotalAmount = cert.GetValueOfBackwardTransfers();

    LogPrint("cert", "%s():%d - cert=%s\n", __func__, __LINE__, certHash.ToString() );

    if (!HaveSidechain(scId))
    {
        // should not happen
        LogPrintf("%s():%d - Can not update balance, could not find scId=%s\n",
            __func__, __LINE__, scId.ToString() );
        return false;
    }

    CSidechainsMap::iterator scIt = ModifySidechain(scId);

    //UpdateScInfo should be called only once per block and scId, with top qualiy cert only
    assert(blockUndo.scUndoDatabyScId[scId].prevTopCommittedCertHash.IsNull());
    blockUndo.scUndoDatabyScId[scId].prevTopCommittedCertReferencedEpoch = scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch;
    blockUndo.scUndoDatabyScId[scId].prevTopCommittedCertHash            = scIt->second.scInfo.prevBlockTopQualityCertHash;
    blockUndo.scUndoDatabyScId[scId].prevTopCommittedCertQuality         = scIt->second.scInfo.prevBlockTopQualityCertQuality;
    blockUndo.scUndoDatabyScId[scId].prevTopCommittedCertBwtAmount       = scIt->second.scInfo.prevBlockTopQualityCertBwtAmount;
    blockUndo.scUndoDatabyScId[scId].contentBitMask |= CSidechainUndoData::AvailableSections::SIDECHAIN_STATE;

    if (scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch != cert.epochNumber)
    {
        // we are changing epoch, this is the first certificate we got
        if (cert.epochNumber != scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch+1)
        {
            LogPrint("cert", "%s():%d - bad epoch value: %d (should be %d)\n",
                __func__, __LINE__, cert.epochNumber, scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch+1);
            return false;
        }

        if (scIt->second.scInfo.balance < bwtTotalAmount)
        {
            LogPrint("cert", "%s():%d - Can not update balance %s with amount[%s] for scId=%s, would be negative\n",
                __func__, __LINE__, FormatMoney(scIt->second.scInfo.balance), FormatMoney(bwtTotalAmount), scId.ToString() );
            return false;
        }

        scIt->second.scInfo.balance -= bwtTotalAmount;
        LogPrint("cert", "%s():%d - amount removed from scView (amount=%s, resulting bal=%s) %s\n",
          __func__, __LINE__, FormatMoney(bwtTotalAmount), FormatMoney(scIt->second.scInfo.balance), scId.ToString());

        scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch = cert.epochNumber;
        scIt->second.scInfo.prevBlockTopQualityCertHash            = certHash;
        scIt->second.scInfo.prevBlockTopQualityCertQuality         = cert.quality;
        scIt->second.scInfo.prevBlockTopQualityCertBwtAmount       = bwtTotalAmount;

        LogPrint("cert", "%s():%d - cert quality set in scView (best cert=%s, best q=%d)\n", __func__, __LINE__,
        scIt->second.scInfo.prevBlockTopQualityCertHash.ToString(), scIt->second.scInfo.prevBlockTopQualityCertQuality);
    }
    else
    {
        // another cert for the same epoch in this scid
        assert(cert.quality != scIt->second.scInfo.prevBlockTopQualityCertQuality);

        if (cert.quality > scIt->second.scInfo.prevBlockTopQualityCertQuality)
        {
            scIt->second.scInfo.balance += scIt->second.scInfo.prevBlockTopQualityCertBwtAmount;
            LogPrint("cert", "%s():%d - amount restored into scView (amount=%s, resulting bal=%s) %s\n", __func__, __LINE__,
                FormatMoney(scIt->second.scInfo.prevBlockTopQualityCertBwtAmount), FormatMoney(scIt->second.scInfo.balance), scId.ToString());

            if (scIt->second.scInfo.balance < bwtTotalAmount)
            {
                LogPrint("cert", "%s():%d - Can not update balance %s with amount[%s] for scId=%s, would be negative\n",
                    __func__, __LINE__, FormatMoney(scIt->second.scInfo.balance), FormatMoney(bwtTotalAmount), scId.ToString() );
                return false;
            }

            // update top-quality certificate data
            scIt->second.scInfo.prevBlockTopQualityCertHash      = certHash;
            scIt->second.scInfo.prevBlockTopQualityCertQuality   = cert.quality;
            scIt->second.scInfo.prevBlockTopQualityCertBwtAmount = bwtTotalAmount;
            LogPrint("cert", "%s():%d - cert quality updated in scView (best cert=%s, best q=%d)\n", __func__, __LINE__,
            scIt->second.scInfo.prevBlockTopQualityCertHash.ToString(), scIt->second.scInfo.prevBlockTopQualityCertQuality);
            scIt->second.scInfo.balance -= bwtTotalAmount;
            LogPrint("cert", "%s():%d - amount removed from scView (amount=%s, resulting bal=%s) %s\n",
              __func__, __LINE__, FormatMoney(bwtTotalAmount), FormatMoney(scIt->second.scInfo.balance), scId.ToString());

        }
        else
        {
            // should never happen if certs are ordered by quality in a block
            LogPrintf("%s():%d - cert quality %d not greater than last seen %d", 
                __func__, __LINE__, cert.quality, scIt->second.scInfo.prevBlockTopQualityCertQuality);
            return false;
        }
    }

    scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
    return true;
}

void CCoinsViewCache::NullifyBackwardTransfers(const uint256& certHash, std::vector<CTxInUndo>& nullifiedOuts)
{
    LogPrint("cert", "%s():%d - called for cert %s\n", __func__, __LINE__, certHash.ToString());
    if (certHash.IsNull())
        return;
 
    if (!this->HaveCoins(certHash))
    {
        //in case the cert had not bwt nor change, there won't be any coin generated by cert. Nothing to handle
        LogPrint("cert", "%s():%d - cert has no bwt nor change", __func__, __LINE__);
        return;
    }

    CCoinsModifier coins = this->ModifyCoins(certHash);
    assert(coins->nBwtMaturityHeight != 0);

    //null all bwt outputs and add related txundo in block
    for(int pos = coins->nFirstBwtPos; pos < coins->vout.size(); ++pos)
    {
        nullifiedOuts.push_back(CTxInUndo(coins->vout.at(pos)));
        LogPrint("cert", "%s():%d - nullifying %s amount, pos=%d, cert %s\n", __func__, __LINE__,
            FormatMoney(coins->vout.at(pos).nValue), pos, certHash.ToString());
        coins->Spend(pos);
        if (coins->vout.size() == 0)
        {
            CTxInUndo& undo         = nullifiedOuts.back();
            undo.nHeight            = coins->nHeight;
            undo.fCoinBase          = coins->fCoinBase;
            undo.nVersion           = coins->nVersion;
            undo.nFirstBwtPos       = coins->nFirstBwtPos;
            undo.nBwtMaturityHeight = coins->nBwtMaturityHeight;
        }
    }
}

bool CCoinsViewCache::RestoreBackwardTransfers(const uint256& certHash, const std::vector<CTxInUndo>& outsToRestore)
{
    bool fClean = true;
    LogPrint("cert", "%s():%d - called for cert %s\n", __func__, __LINE__, certHash.ToString());

    CCoinsModifier coins = this->ModifyCoins(certHash);

    for (size_t idx = outsToRestore.size(); idx-- > 0;)
    {
        if (outsToRestore.at(idx).nHeight != 0)
        {
            coins->fCoinBase          = outsToRestore.at(idx).fCoinBase;
            coins->nHeight            = outsToRestore.at(idx).nHeight;
            coins->nVersion           = outsToRestore.at(idx).nVersion;
            coins->nFirstBwtPos       = outsToRestore.at(idx).nFirstBwtPos;
            coins->nBwtMaturityHeight = outsToRestore.at(idx).nBwtMaturityHeight;
        }
        else
        {
            if (coins->IsPruned())
            {
                LogPrint("cert", "%s():%d - idx=%d coin is pruned\n", __func__, __LINE__, idx);
                fClean = fClean && error("%s: undo data idx=%d adding output to missing transaction", __func__, idx);
            }
        }
 
        if (coins->IsAvailable(coins->nFirstBwtPos + idx))
        {
            LogPrint("cert", "%s():%d - idx=%d coin is available\n", __func__, __LINE__, idx);
            fClean = fClean && error("%s: undo data idx=%d overwriting existing output", __func__, idx);
        }

        if (coins->vout.size() < (coins->nFirstBwtPos + idx+1))
        {
            coins->vout.resize(coins->nFirstBwtPos + idx+1);
        }
        coins->vout.at(coins->nFirstBwtPos + idx) = outsToRestore.at(idx).txout;
    }
 
    return fClean;
}

bool CCoinsViewCache::RestoreScInfo(const CScCertificate& certToRevert, const CSidechainUndoData& sidechainUndo)
{
    const uint256& scId           = certToRevert.GetScId();
    const CAmount& bwtTotalAmount = certToRevert.GetValueOfBackwardTransfers();

    LogPrint("cert", "%s():%d - removing cert for scId=%s\n", __func__, __LINE__, scId.ToString());

    if (!HaveSidechain(scId))
    {
        // should not happen
        LogPrintf("ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
        return false;
    }

    CSidechainsMap::iterator scIt = ModifySidechain(scId);

    // restore only if this is the top quality cert for this epoch
    LogPrint("cert", "%s():%d - cert %s, last cert %s, undo cert %s\n", __func__, __LINE__,
        certToRevert.GetHash().ToString(), scIt->second.scInfo.prevBlockTopQualityCertHash.ToString(), sidechainUndo.prevTopCommittedCertHash.ToString());
    LogPrint("cert", "%s():%d - cert epoch %d, last epoch %d, undo epoch %d\n", __func__, __LINE__,
        certToRevert.epochNumber, scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch, sidechainUndo.prevTopCommittedCertReferencedEpoch);

    // RestoreScInfo should be called only once per block and scId, with top qualiy cert only    
    assert(certToRevert.GetHash() == scIt->second.scInfo.prevBlockTopQualityCertHash);

    scIt->second.scInfo.balance += bwtTotalAmount;
    LogPrint("cert", "%s():%d - amount restored to scView (amount=%s, resulting bal=%s) scid=%s\n",
        __func__, __LINE__, FormatMoney(bwtTotalAmount), FormatMoney(scIt->second.scInfo.balance), scId.ToString());

    //if a lower quality certificate is restored, we have to subtract the relevant amount
    // from the balance
    if (certToRevert.epochNumber == sidechainUndo.prevTopCommittedCertReferencedEpoch)
    {
        // if we are restoring a cert for the same epoch it must have a lower quality than us
        assert(certToRevert.quality > sidechainUndo.prevTopCommittedCertQuality);

        // in this case we have to update the sc balance with undo amount
        scIt->second.scInfo.balance -= sidechainUndo.prevTopCommittedCertBwtAmount;
    }

    assert(sidechainUndo.contentBitMask & CSidechainUndoData::AvailableSections::SIDECHAIN_STATE);
    scIt->second.scInfo.prevBlockTopQualityCertReferencedEpoch = sidechainUndo.prevTopCommittedCertReferencedEpoch;
    scIt->second.scInfo.prevBlockTopQualityCertHash            = sidechainUndo.prevTopCommittedCertHash;
    scIt->second.scInfo.prevBlockTopQualityCertQuality         = sidechainUndo.prevTopCommittedCertQuality;
    scIt->second.scInfo.prevBlockTopQualityCertBwtAmount       = sidechainUndo.prevTopCommittedCertBwtAmount;

    scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

    return true;
}

bool CCoinsViewCache::HaveSidechainEvents(int height) const
{
    CSidechainEventsMap::const_iterator it = FetchSidechainEvents(height);
    return (it != cacheSidechainEvents.end()) && (it->second.flag != CSidechainEventsCacheEntry::Flags::ERASED);
}

bool CCoinsViewCache::GetSidechainEvents(int height, CSidechainEvents& scEvents) const
{
    CSidechainEventsMap::const_iterator it = FetchSidechainEvents(height);
    if (it != cacheSidechainEvents.end() && it->second.flag != CSidechainEventsCacheEntry::Flags::ERASED) {
        scEvents = it->second.scEvents;
        return true;
    }
    return false;
}

bool CCoinsViewCache::ScheduleSidechainEvent(const CTxScCreationOut& scCreationOut, int creationHeight)
{
    CSidechain scInfo;
    if (!this->GetSidechain(scCreationOut.GetScId(), scInfo)) {
        LogPrintf("%s():%d - SIDECHAIN-EVENT: attempt schedule maturing scCreation for unknown scId[%s]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString());
        return false;
    }

    // Schedule maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = creationHeight + SC_COIN_MATURITY;

    CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
    if (scMaturingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
        scMaturingEventIt->second.scEvents.maturingScs.insert(scCreationOut.GetScId());
    } else {
        scMaturingEventIt->second.scEvents.maturingScs.insert(scCreationOut.GetScId());
        scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: scCreation next maturing height [%d]\n",
             __func__, __LINE__, scCreationOut.GetScId().ToString(), maturityHeight);

    // Schedule Ceasing Sidechains
    int nextCeasingHeight = scInfo.StartHeightForEpoch(1) + scInfo.SafeguardMargin();

    CSidechainEventsMap::iterator scCeasingEventIt = ModifySidechainEvents(nextCeasingHeight);
    if (scCeasingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
        scCeasingEventIt->second.scEvents.ceasingScs.insert(scCreationOut.GetScId());
    } else {
        scCeasingEventIt->second.scEvents.ceasingScs.insert(scCreationOut.GetScId());
        scCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: scCreation next ceasing height [%d]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), nextCeasingHeight);

    return true;
}

bool CCoinsViewCache::ScheduleSidechainEvent(const CTxForwardTransferOut& forwardOut, int fwdHeight)
{
    CSidechain scInfo;
    if (!this->GetSidechain(forwardOut.GetScId(), scInfo)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: attempt to undo schedule maturing fwd for unknown scId[%s]\n",
            __func__, __LINE__, forwardOut.scId.ToString());
        return false;
    }

    // Schedule maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = fwdHeight + SC_COIN_MATURITY;

    CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
    if (scMaturingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
        scMaturingEventIt->second.scEvents.maturingScs.insert(forwardOut.GetScId());
    } else {
        scMaturingEventIt->second.scEvents.maturingScs.insert(forwardOut.GetScId());
        scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: fwd Transfer next maturing height [%d]\n",
             __func__, __LINE__, forwardOut.scId.ToString(), maturityHeight);

    return true;
}

bool CCoinsViewCache::ScheduleSidechainEvent(const CScCertificate& cert)
{
    CSidechain sidechain;
    if (!this->GetSidechain(cert.GetScId(), sidechain)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: attempt to update ceasing sidechain map with cert to unknown scId[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    int curCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+1) + sidechain.SafeguardMargin();
    int nextCeasingHeight = curCeasingHeight + sidechain.creationData.withdrawalEpochLength;

    //clear up current ceasing height, if any
    if (HaveSidechainEvents(curCeasingHeight))
    {
        CSidechainEventsMap::iterator scCurCeasingEventIt = ModifySidechainEvents(curCeasingHeight);
        scCurCeasingEventIt->second.scEvents.ceasingScs.erase(cert.GetScId());
        if (!scCurCeasingEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds maturing
            scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
            scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] removes prevCeasingHeight [%d] (certEp=%d, currentEp=%d)\n",
                __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), curCeasingHeight, cert.epochNumber,
                sidechain.prevBlockTopQualityCertReferencedEpoch);
    } else
    {
        if (!HaveSidechainEvents(nextCeasingHeight) )
        {
            LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: Could not find scheduling for current ceasing height [%d] nor next ceasing height [%d]\n",
                __func__, __LINE__, cert.GetScId().ToString(), curCeasingHeight, nextCeasingHeight);
            return false;
        }
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] misses prevCeasingHeight [%d] to remove\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), curCeasingHeight);
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: nextCeasingHeight already scheduled at[%d].\n",
            __func__, __LINE__, cert.GetScId().ToString(), nextCeasingHeight);
        return true;
    }

    //add next ceasing Height
    CSidechainEventsMap::iterator scNextCeasingEventIt = ModifySidechainEvents(nextCeasingHeight);
    if (scNextCeasingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
        scNextCeasingEventIt->second.scEvents.ceasingScs.insert(cert.GetScId());
    } else {
        scNextCeasingEventIt->second.scEvents.ceasingScs.insert(cert.GetScId());
        scNextCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: cert [%s] sets nextCeasingHeight to [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), nextCeasingHeight);

    return true;
}

bool CCoinsViewCache::CancelSidechainEvent(const CTxScCreationOut& scCreationOut, int creationHeight)
{
    CSidechain sidechain;
    if (!this->GetSidechain(scCreationOut.GetScId(), sidechain)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: attempt to undo ScCreation amount maturing for unknown scId[%s]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString());
        return false;
    }

    // Cancel maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = creationHeight + SC_COIN_MATURITY;

    if (HaveSidechainEvents(maturityHeight))
    {
        CSidechainEventsMap::iterator scMaturityEventIt = ModifySidechainEvents(maturityHeight);
        scMaturityEventIt->second.scEvents.maturingScs.erase(scCreationOut.GetScId());
        if (!scMaturityEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds
            scMaturityEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
        else
            scMaturityEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] deleted maturing height [%d] for creation amount.\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), maturityHeight);
    } else
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] nothing to do for scCreation amount maturing canceling at height [%d].\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), maturityHeight);


    //remove current ceasing Height
    int currentCeasingHeight = sidechain.StartHeightForEpoch(1) + sidechain.SafeguardMargin();

    // Cancel Ceasing Sidechains
    if (!HaveSidechainEvents(currentCeasingHeight)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] misses current ceasing height; expected value was [%d]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), currentCeasingHeight);
        return false;
    }

    CSidechainEventsMap::iterator scCurCeasingEventIt = ModifySidechainEvents(currentCeasingHeight);
    scCurCeasingEventIt->second.scEvents.ceasingScs.erase(scCreationOut.GetScId());
    if (!scCurCeasingEventIt->second.scEvents.IsNull())
        scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    else
        scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: undo of creation removes currentCeasingHeight [%d]\n",
            __func__, __LINE__, scCreationOut.GetScId().ToString(), currentCeasingHeight);

    return true;
}

bool CCoinsViewCache::CancelSidechainEvent(const CTxForwardTransferOut& forwardOut, int fwdHeight)
{
    // Cancel maturing amount
    static const int SC_COIN_MATURITY = getScCoinsMaturity();
    const int maturityHeight = fwdHeight + SC_COIN_MATURITY;

    if (!HaveSidechainEvents(maturityHeight)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] maturing height [%d] already deleted. This may happen in case of concurrent fwd\n",
            __func__, __LINE__, forwardOut.scId.ToString(), maturityHeight);
        return true;
    }

    CSidechainEventsMap::iterator scMaturingEventIt = ModifySidechainEvents(maturityHeight);
    scMaturingEventIt->second.scEvents.maturingScs.erase(forwardOut.scId);
    if (!scMaturingEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds
        scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    else
        scMaturingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s] cancelled maturing height [%s] for fwd amount.\n",
        __func__, __LINE__, forwardOut.scId.ToString(), maturityHeight);

    return true;
}

bool CCoinsViewCache::CancelSidechainEvent(const CScCertificate& cert)
{
    CSidechain restoredSidechain;
    if (!this->GetSidechain(cert.GetScId(), restoredSidechain)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: attempt to undo ceasing sidechain map with cert to unknown scId[%s]\n",
            __func__, __LINE__, cert.GetScId().ToString());
        return false;
    }

    int currentCeasingHeight = restoredSidechain.StartHeightForEpoch(cert.epochNumber+2) + restoredSidechain.SafeguardMargin();
    int previousCeasingHeight = currentCeasingHeight - restoredSidechain.creationData.withdrawalEpochLength;

    //remove current ceasing Height
    if (!HaveSidechainEvents(currentCeasingHeight))
    {
        if (!HaveSidechainEvents(previousCeasingHeight) )
        {
            LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: Could not find scheduling for current ceasing height [%d] nor previous ceasing height [%d]\n",
                __func__, __LINE__, cert.GetScId().ToString(), currentCeasingHeight, previousCeasingHeight);
            return false;
        }
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: misses current ceasing height [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), currentCeasingHeight);
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: previousCeasingHeight already restored at[%d].\n",
            __func__, __LINE__, cert.GetScId().ToString(), previousCeasingHeight);

        return true;
    }

    CSidechainEventsMap::iterator scCurCeasingEventIt = ModifySidechainEvents(currentCeasingHeight);
    scCurCeasingEventIt->second.scEvents.ceasingScs.erase(cert.GetScId());
    if (!scCurCeasingEventIt->second.scEvents.IsNull()) //still other sc ceasing at that height or fwds
        scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    else
        scCurCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: scId[%s]: undo of cert [%s] removes currentCeasingHeight [%d]\n",
            __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), currentCeasingHeight);

    //restore previous ceasing Height
    CSidechainEventsMap::iterator scRestoredCeasingEventIt = ModifySidechainEvents(previousCeasingHeight);
    if (scRestoredCeasingEventIt->second.flag == CSidechainEventsCacheEntry::Flags::FRESH) {
        scRestoredCeasingEventIt->second.scEvents.ceasingScs.insert(cert.GetScId());
    } else {
        scRestoredCeasingEventIt->second.scEvents.ceasingScs.insert(cert.GetScId());
        scRestoredCeasingEventIt->second.flag = CSidechainEventsCacheEntry::Flags::DIRTY;
    }

    LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId[%s]: undo of cert [%s] set nextCeasingHeight to [%d]\n",
        __func__, __LINE__, cert.GetScId().ToString(), cert.GetHash().ToString(), previousCeasingHeight);

    return true;
}

bool CCoinsViewCache::HandleSidechainEvents(int height, CBlockUndo& blockUndo, std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo)
{
    if (!HaveSidechainEvents(height))
        return true;

    CSidechainEvents scEvents;
    GetSidechainEvents(height, scEvents);

    //Handle Maturing amounts
    for (const uint256& maturingScId : scEvents.maturingScs)
    {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: about to mature scId[%s] amount at height [%d]\n",
                __func__, __LINE__, maturingScId.ToString(), height);

        assert(HaveSidechain(maturingScId));
        CSidechainsMap::iterator scMaturingIt = ModifySidechain(maturingScId);
        assert(scMaturingIt->second.scInfo.mImmatureAmounts.count(height));

        scMaturingIt->second.scInfo.balance += scMaturingIt->second.scInfo.mImmatureAmounts.at(height);
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: scId=%s balance updated to: %s\n",
            __func__, __LINE__, maturingScId.ToString(), FormatMoney(scMaturingIt->second.scInfo.balance));

        blockUndo.scUndoDatabyScId[maturingScId].appliedMaturedAmount = scMaturingIt->second.scInfo.mImmatureAmounts[height];
        blockUndo.scUndoDatabyScId[maturingScId].contentBitMask |= CSidechainUndoData::AvailableSections::MATURED_AMOUNTS;
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: adding immature amount %s for scId=%s in blockundo\n",
            __func__, __LINE__, FormatMoney(scMaturingIt->second.scInfo.mImmatureAmounts[height]), maturingScId.ToString());

        scMaturingIt->second.scInfo.mImmatureAmounts.erase(height);
        scMaturingIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
    }

    //Handle Ceasing Sidechain
    for (const uint256& ceasingScId : scEvents.ceasingScs)
    {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: about to handle scId[%s] and ceasingHeight [%d]\n",
                __func__, __LINE__, ceasingScId.ToString(), height);

        CSidechain scInfo;
        assert(GetSidechain(ceasingScId, scInfo));

        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT: lastCertEpoch [%d], lastCertHash [%s]\n",
                __func__, __LINE__, scInfo.prevBlockTopQualityCertReferencedEpoch, scInfo.prevBlockTopQualityCertHash.ToString());

        LogPrint("sc", "%s():%d - set voidedCertHash[%s], ceasingScId = %s\n",
            __func__, __LINE__, scInfo.prevBlockTopQualityCertHash.ToString(), ceasingScId.ToString());

        CSidechainsMap::iterator scIt = ModifySidechain(ceasingScId);
        scIt->second.scInfo.currentState = (uint8_t)CSidechain::State::CEASED;
        scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
        
        blockUndo.scUndoDatabyScId[ceasingScId].contentBitMask |= CSidechainUndoData::AvailableSections::CEASED_CERTIFICATE_DATA;
        if (scInfo.prevBlockTopQualityCertReferencedEpoch == CScCertificate::EPOCH_NULL) {
            assert(scInfo.prevBlockTopQualityCertHash.IsNull());
            continue;
        }

        NullifyBackwardTransfers(scInfo.prevBlockTopQualityCertHash, blockUndo.scUndoDatabyScId[ceasingScId].ceasedBwts);
        if (pCertsStateInfo != nullptr)
            pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(ceasingScId, scInfo.prevBlockTopQualityCertHash,
                                       scInfo.prevBlockTopQualityCertReferencedEpoch,
                                       scInfo.prevBlockTopQualityCertQuality,
                                       CScCertificateStatusUpdateInfo::BwtState::BWT_OFF));
    }

    CSidechainEventsMap::iterator scCeasingIt = ModifySidechainEvents(height);
    scCeasingIt->second.flag = CSidechainEventsCacheEntry::Flags::ERASED;
    return true;
}

bool CCoinsViewCache::RevertSidechainEvents(const CBlockUndo& blockUndo, int height, std::vector<CScCertificateStatusUpdateInfo>* pCertsStateInfo)
{
    if (HaveSidechainEvents(height)) {
        LogPrint("sc", "%s():%d - SIDECHAIN-EVENT:: attempt to recreate sidechain event at height [%d], but there is one already\n",
            __func__, __LINE__, height);
        return false;
    }

    CSidechainEvents recreatedScEvent;

    // Reverting amount maturing
    for (auto it = blockUndo.scUndoDatabyScId.begin(); it != blockUndo.scUndoDatabyScId.end(); ++it)
    {
        if ((it->second.contentBitMask & CSidechainUndoData::AvailableSections::MATURED_AMOUNTS) == 0)
            continue;

        const uint256& scId = it->first;
        const std::string& scIdString = scId.ToString();

        if (!HaveSidechain(scId))
        {
            // should not happen
            LogPrintf("ERROR: %s():%d - scId=%s not in scView\n", __func__, __LINE__, scId.ToString() );
            return false;
        }

        CAmount amountToRestore = it->second.appliedMaturedAmount;
        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        if (amountToRestore > 0)
        {
            LogPrint("sc", "%s():%d - adding immature amount %s into sc view for scId=%s\n",
                __func__, __LINE__, FormatMoney(amountToRestore), scIdString);

            if (scIt->second.scInfo.balance < amountToRestore)
            {
                LogPrint("sc", "%s():%d - Can not update balance with amount[%s] for scId=%s, would be negative\n",
                    __func__, __LINE__, FormatMoney(amountToRestore), scId.ToString() );
                return false;
            }

            scIt->second.scInfo.mImmatureAmounts[height] += amountToRestore;

            LogPrint("sc", "%s():%d - scId=%s balance before: %s\n", __func__, __LINE__, scIdString, FormatMoney(scIt->second.scInfo.balance));
            scIt->second.scInfo.balance -= amountToRestore;
            LogPrint("sc", "%s():%d - scId=%s balance after: %s\n", __func__, __LINE__, scIdString, FormatMoney(scIt->second.scInfo.balance));

            scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
        }

        recreatedScEvent.maturingScs.insert(scId);
    }

    // Reverting ceasing sidechains
    for (auto it = blockUndo.scUndoDatabyScId.begin(); it != blockUndo.scUndoDatabyScId.end(); ++it)
    {
        if ((it->second.contentBitMask & CSidechainUndoData::AvailableSections::CEASED_CERTIFICATE_DATA) == 0)
            continue;

        const uint256& scId = it->first;
        const CSidechain* const pSidechain = AccessSidechain(scId);

        if (pSidechain->prevBlockTopQualityCertReferencedEpoch != CScCertificate::EPOCH_NULL)
        {
            if (!RestoreBackwardTransfers(pSidechain->prevBlockTopQualityCertHash, blockUndo.scUndoDatabyScId.at(scId).ceasedBwts))
                return false;
 
            if (pCertsStateInfo != nullptr)
                pCertsStateInfo->push_back(CScCertificateStatusUpdateInfo(scId, pSidechain->prevBlockTopQualityCertHash,
                                           pSidechain->prevBlockTopQualityCertReferencedEpoch,
                                           pSidechain->prevBlockTopQualityCertQuality,
                                           CScCertificateStatusUpdateInfo::BwtState::BWT_ON));
        }

        recreatedScEvent.ceasingScs.insert(scId);
        CSidechainsMap::iterator scIt = ModifySidechain(scId);
        scIt->second.scInfo.currentState = (uint8_t)CSidechain::State::ALIVE;
        scIt->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
    }

    if (!recreatedScEvent.IsNull())
    {
        CSidechainEventsMap::iterator scEventIt = ModifySidechainEvents(height);
        scEventIt->second.scEvents = recreatedScEvent;
        scEventIt->second.flag = CSidechainEventsCacheEntry::Flags::FRESH;
    }

    return true;
}

CSidechain::State CCoinsViewCache::GetSidechainState(const uint256& scId) const
{
    if (!HaveSidechain(scId))
        return CSidechain::State::NOT_APPLICABLE;

    CSidechain scInfo;
    GetSidechain(scId, scInfo);

    LogPrint("cert", "%s.%s():%d sc %s state is %s\n", __FILE__, __func__, __LINE__, scId.ToString(),
        CSidechain::stateToString((CSidechain::State)scInfo.currentState));
    return (CSidechain::State)scInfo.currentState;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock, hashAnchor, cacheAnchors, cacheNullifiers, cacheSidechains, cacheSidechainEvents, cacheCswNullifiers, cacheCertDataHashes);
    cacheCoins.clear();
    cacheSidechains.clear();
    cacheSidechainEvents.clear();
    cacheAnchors.clear();
    cacheNullifiers.clear();
    cachedCoinsUsage = 0;
    cacheCswNullifiers.clear();
    return fOk;
}

bool CCoinsViewCache::DecrementImmatureAmount(const uint256& scId, const CSidechainsMap::iterator& targetEntry, CAmount nValue, int maturityHeight)
{
    // get the map of immature amounts, they are indexed by height
    auto& iaMap = targetEntry->second.scInfo.mImmatureAmounts;

    if (!iaMap.count(maturityHeight) )
    {
        // should not happen
        LogPrintf("ERROR %s():%d - could not find immature balance at height%d\n",
            __func__, __LINE__, maturityHeight);
        return false;
    }

    LogPrint("sc", "%s():%d - immature amount before: %s\n",
        __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

    if (iaMap[maturityHeight] < nValue)
    {
        // should not happen either
        LogPrintf("ERROR %s():%d - negative balance at height=%d\n",
            __func__, __LINE__, maturityHeight);
        return false;
    }

    iaMap[maturityHeight] -= nValue;
    targetEntry->second.flag = CSidechainsCacheEntry::Flags::DIRTY;

    LogPrint("sc", "%s():%d - immature amount after: %s\n",
        __func__, __LINE__, FormatMoney(iaMap[maturityHeight]));

    if (iaMap[maturityHeight] == 0)
    {
        iaMap.erase(maturityHeight);
        targetEntry->second.flag = CSidechainsCacheEntry::Flags::DIRTY;
        LogPrint("sc", "%s():%d - removed entry height=%d from immature amounts in memory\n",
            __func__, __LINE__, maturityHeight );
    }
    return true;
}

void CCoinsViewCache::Dump_info() const
{
    std::set<uint256> scIdsList;
    GetScIds(scIdsList);
    LogPrint("sc", "-- number of side chains found [%d] ------------------------\n", scIdsList.size());
    for(const auto& scId: scIdsList)
    {
        LogPrint("sc", "-- side chain [%s] ------------------------\n", scId.ToString());
        CSidechain info;
        if (!GetSidechain(scId, info))
        {
            LogPrint("sc", "===> No such side chain\n");
            return;
        }

        LogPrint("sc", "  created in block[%s] (h=%d)\n", info.creationBlockHash.ToString(), info.creationBlockHeight );
        LogPrint("sc", "  creationTx[%s]\n", info.creationTxHash.ToString());
        LogPrint("sc", "  prevBlockTopQualityCertReferencedEpoch[%d]\n", info.prevBlockTopQualityCertReferencedEpoch);
        LogPrint("sc", "  prevBlockTopQualityCertHash[%s]\n",            info.prevBlockTopQualityCertHash.ToString());
        LogPrint("sc", "  prevBlockTopQualityCertQuality[%d]\n",         info.prevBlockTopQualityCertQuality);
        LogPrint("sc", "  prevBlockTopQualityCertBwtAmount[%s]\n",       FormatMoney(info.prevBlockTopQualityCertBwtAmount));
        LogPrint("sc", "  balance[%s]\n", FormatMoney(info.balance));
        LogPrint("sc", "  ----- creation data:\n");
        LogPrint("sc", "      withdrawalEpochLength[%d]\n", info.creationData.withdrawalEpochLength);
        LogPrint("sc", "      customData[%s]\n", HexStr(info.creationData.customData));
        LogPrint("sc", "      constant[%s]\n", HexStr(info.creationData.constant));
        LogPrint("sc", "      wCertVk[%s]\n", HexStr(info.creationData.wCertVk));
        LogPrint("sc", "      wCeasedVk[%s]\n", info.creationData.wCeasedVk.is_initialized() ? HexStr(info.creationData.wCeasedVk.get()) : "none");
        LogPrint("sc", "  immature amounts size[%d]\n", info.mImmatureAmounts.size());
    }

    return;
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

const CTxOut &CCoinsViewCache::GetOutputFor(const CTxIn& input) const
{
    const CCoins* coins = AccessCoins(input.prevout.hash);
    assert(coins && coins->IsAvailable(input.prevout.n));
    return coins->vout[input.prevout.n];
}

CAmount CCoinsViewCache::GetValueIn(const CTransactionBase& txBase) const
{
    if (txBase.IsCoinBase())
        return 0;

    CAmount nResult = 0;
    for (const CTxIn& in : txBase.GetVin())
        nResult += GetOutputFor(in).nValue;

    nResult += txBase.GetJoinSplitValueIn() + txBase.GetCSWValueIn();

    return nResult;
}

bool CCoinsViewCache::HaveJoinSplitRequirements(const CTransactionBase& txBase) const
{
    boost::unordered_map<uint256, ZCIncrementalMerkleTree, CCoinsKeyHasher> intermediates;

    for(const JSDescription &joinsplit: txBase.GetVjoinsplit()) {
        for(const uint256& nullifier: joinsplit.nullifiers) {
            if (GetNullifier(nullifier)) {
                // If the nullifier is set, this transaction
                // double-spends!
                return false;
            }
        }

        ZCIncrementalMerkleTree tree;
        auto it = intermediates.find(joinsplit.anchor);
        if (it != intermediates.end()) {
            tree = it->second;
        } else if (!GetAnchorAt(joinsplit.anchor, tree)) {
            return false;
        }

        for(const uint256& commitment: joinsplit.commitments) {
            tree.append(commitment);
        }

        intermediates.insert(std::make_pair(tree.root(), tree));
    }

    return true;
}

bool CCoinsViewCache::HaveInputs(const CTransactionBase& txBase) const
{
    if (!txBase.IsCoinBase()) {
        for(const CTxIn & in: txBase.GetVin()) {
            const CCoins* coins = AccessCoins(in.prevout.hash);
            if (!coins || !coins->IsAvailable(in.prevout.n)) {
                return false;
            }
        }
    }
    return true;
}

double CCoinsViewCache::GetPriority(const CTransactionBase &tx, int nHeight) const
{
    if (tx.IsCoinBase())
        return 0.0;

    // Joinsplits do not reveal any information about the value or age of a note, so we
    // cannot apply the priority algorithm used for transparent utxos.  Instead, we just
    // use the maximum priority whenever a transaction contains any JoinSplits.
    // (Note that coinbase transactions cannot contain JoinSplits.)
    // FIXME: this logic is partially duplicated between here and CreateNewBlock in miner.cpp.

    if (tx.GetVjoinsplit().size() > 0) {
        return MAXIMUM_PRIORITY;
    }

    if (tx.IsCertificate() ) {
        return MAXIMUM_PRIORITY;
    }

    double dResult = 0.0;
    BOOST_FOREACH(const CTxIn& txin, tx.GetVin())
    {
        const CCoins* coins = AccessCoins(txin.prevout.hash);
        assert(coins);
        if (!coins->IsAvailable(txin.prevout.n)) continue;
        if (coins->nHeight < nHeight) {
            dResult += coins->vout[txin.prevout.n].nValue * (nHeight-coins->nHeight);
        }
    }

    // As per csw inputs, we assign them depth zero (i.e. their creation height matches containing tx height)
    // They won't contribute to initial priority, but they will to priority in mempool

    return tx.ComputePriority(dResult);
}

CCoinsModifier::CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_, size_t usage):
        cache(cache_), it(it_), cachedCoinUsage(usage) {
    assert(!cache.hasModifier);
    cache.hasModifier = true;
}

CCoinsModifier::~CCoinsModifier()
{
    assert(cache.hasModifier);
    cache.hasModifier = false;
    it->second.coins.Cleanup();
    cache.cachedCoinsUsage -= cachedCoinUsage; // Subtract the old usage
    if ((it->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
        cache.cacheCoins.erase(it);
    } else {
        // If the coin still exists after the modification, add the new usage
        cache.cachedCoinsUsage += it->second.coins.DynamicMemoryUsage();
    }
}
