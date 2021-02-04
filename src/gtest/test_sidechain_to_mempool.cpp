#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <chainparams.h>
#include <util.h>

#include <txdb.h>
#include <main.h>
#include <zen/forks/fork7_sidechainfork.h>

#include <key.h>
#include <keystore.h>
#include <script/sign.h>

#include "tx_creation_utils.h"
#include <consensus/validation.h>

#include <sc/sidechain.h>
#include <txmempool.h>
#include <init.h>
#include <undo.h>
#include <gtest/libzendoo_test_files.h>

class CCoinsOnlyViewDB : public CCoinsViewDB
{
public:
    CCoinsOnlyViewDB(size_t nCacheSize, bool fWipe = false)
        : CCoinsViewDB(nCacheSize, false, fWipe) {}

    bool BatchWrite(CCoinsMap &mapCoins)
    {
        const uint256 hashBlock;
        const uint256 hashAnchor;
        CAnchorsMap mapAnchors;
        CNullifiersMap mapNullifiers;
        CSidechainsMap mapSidechains;
        CSidechainEventsMap mapSidechainEvents;
        CCswNullifiersMap cswNullifiers;
        CCertDataHashMap certDataHashes;

        return CCoinsViewDB::BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains, mapSidechainEvents, cswNullifiers, certDataHashes);
    }
};

class SidechainsInMempoolTestSuite: public ::testing::Test {
public:
    SidechainsInMempoolTestSuite():
        aMempool(::minRelayTxFee),
        pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()),
        chainStateDbSize(2 * 1024 * 1024),
        pChainStateDb(nullptr),
        minimalHeightForSidechains(SidechainFork().getHeight(CBaseChainParams::REGTEST)),
        csMainLock(cs_main, "cs_main", __FILE__, __LINE__),
        csAMempool(aMempool.cs, "cs_AMempool", __FILE__, __LINE__)
    {
        SelectParams(CBaseChainParams::REGTEST);

        boost::filesystem::create_directories(pathTemp);
        mapArgs["-datadir"] = pathTemp.string();

        pChainStateDb = new CCoinsOnlyViewDB(chainStateDbSize,/*fWipe*/true);
        pcoinsTip     = new CCoinsViewCache(pChainStateDb);
    }

    void SetUp() override {
        chainSettingUtils::ExtendChainActiveToHeight(minimalHeightForSidechains);
        pcoinsTip->SetBestBlock(chainActive.Tip()->GetBlockHash());
        pindexBestHeader = chainActive.Tip();

        InitCoinGeneration();
    }

    void TearDown() override {
        mempool.clear();
        chainActive.SetTip(nullptr);
        mapBlockIndex.clear();
    }

    ~SidechainsInMempoolTestSuite() {
        delete pcoinsTip;
        pcoinsTip = nullptr;

        delete pChainStateDb;
        pChainStateDb = nullptr;

        ClearDatadirCache();
        boost::system::error_code ec;
        boost::filesystem::remove_all(pathTemp.string(), ec);
    }

protected:
    CTxMemPool aMempool;
    CTransaction GenerateScTx(const CAmount & creationTxAmount, int epochLenght = -1, bool ceasedVkDefined = true);
    CTransaction GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount);
    CTxCeasedSidechainWithdrawalInput GenerateCSWInput(const uint256& scId, int32_t epoch, const std::string& nullifierHex, CAmount amount);
    CTransaction GenerateCSWTx(const std::vector<CTxCeasedSidechainWithdrawalInput>& csws);
    CTransaction GenerateCSWTx(const CTxCeasedSidechainWithdrawalInput& csw);
    CScCertificate GenerateCertificate(const uint256 & scId, int epochNum, const uint256 & endEpochBlockHash,
                                 CAmount inputAmount, CAmount changeTotalAmount/* = 0*/, unsigned int numChangeOut/* = 0*/,
                                 CAmount bwtTotalAmount/* = 1*/, unsigned int numBwt/* = 1*/, int64_t quality,
                                 const CTransactionBase* inputTxBase = nullptr);

private:
    boost::filesystem::path  pathTemp;
    const unsigned int       chainStateDbSize;
    CCoinsOnlyViewDB*        pChainStateDb;

    const unsigned int       minimalHeightForSidechains;

    CKey                     coinsKey;
    CBasicKeyStore           keystore;
    CScript                  coinsScript;

    void InitCoinGeneration();
    std::pair<uint256, CCoinsCacheEntry> GenerateCoinsAmount(const CAmount & amountToGenerate);
    bool StoreCoins(const std::pair<uint256, CCoinsCacheEntry>& entryToStore);

    //Critical sections below needed when compiled with --enable-debug, which activates ASSERT_HELD
    CCriticalBlock csMainLock;
    CCriticalBlock csAMempool;
};

TEST_F(SidechainsInMempoolTestSuite, NewSidechainsAreAcceptedToMempool) {
    CTransaction scTx = GenerateScTx(CAmount(1));
    CValidationState txState;
    bool missingInputs = false;

    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, txState, scTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToConfirmedSideChainsAreAllowed) {
    int creationHeight = 1789;
    chainSettingUtils::ExtendChainActiveToHeight(creationHeight);

    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);

    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, creationHeight);
    sidechainsView.SetBestBlock(pcoinsTip->GetBestBlock()); //do not alter BestBlock, as set in test fixture
    sidechainsView.Flush();

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    bool missingInputs = false;

    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

//A proof that https://github.com/HorizenOfficial/zen/issues/215 is solved
TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToSideChainsInMempoolAreAllowed) {
    CTransaction scTx = GenerateScTx(CAmount(1));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CValidationState scTxState;
    bool missingInputs = false;
    AcceptTxToMemoryPool(mempool, scTxState, scTx, false, &missingInputs);
    ASSERT_TRUE(mempool.hasSidechainCreationTx(scId));

    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    EXPECT_TRUE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, FwdTransfersToUnknownSideChainAreNotAllowed) {
    uint256 scId = uint256S("dddd");
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CValidationState fwdTxState;
    bool missingInputs = false;

    EXPECT_FALSE(AcceptTxToMemoryPool(mempool, fwdTxState, fwdTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, hasSidechainCreationTxTest) {
    uint256 scId = uint256S("1492");

    //Case 1: no sidechain related tx in mempool
    bool res = aMempool.hasSidechainCreationTx(scId);
    EXPECT_FALSE(res);

    bool loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().GetVscCcOut())
            if(sc.GetScId() == scId) {
                loopRes = true;
                break;
        }
    EXPECT_TRUE(loopRes == res);

    //Case 2: fwd transfer tx only in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdPoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdPoolEntry.GetTx().GetHash(), fwdPoolEntry);
    res = aMempool.hasSidechainCreationTx(scId);
    EXPECT_FALSE(res);

    loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().GetVscCcOut())
            if(sc.GetScId() == scId) {
                loopRes = true;
                break;
        }
    EXPECT_TRUE(loopRes == res);

    //Case 3: sc creation tx in mempool
    CTransaction scTx  = GenerateScTx(CAmount(10));
    const uint256& scIdOk = scTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scPoolEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scPoolEntry.GetTx().GetHash(), scPoolEntry);
    res = aMempool.hasSidechainCreationTx(scIdOk);
    EXPECT_TRUE(res);

    loopRes = false;
    for(const auto& tx : aMempool.mapTx)
        for(const auto& sc: tx.second.GetTx().GetVscCcOut())
            if(sc.GetScId() == scIdOk) {
                loopRes = true;
                break;
        }
    EXPECT_TRUE(loopRes == res);
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_ScNonRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds are in mempool.
    // Sc Creation is confirmed, hence it has to be removed from mempool, while fwds stay.

    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.hasSidechainCreationTx(scId));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsOnlyInMempool_FwdNonRecursiveRemoval) {
    // Associated scenario: fws are in mempool, hence scCreation must be already confirmed
    // A fwd is confirmed hence it, and only it, is removed from mempool
    uint256 scId = uint256S("ababab");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(fwdTx1, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_ScRecursiveRemoval) {
    // Associated scenario: Sidechain creation and some fwds are in mempool, e.g. as a result of previous blocks disconnections
    // One of the new blocks about to me mounted double spends the original scTx, hence scCreation is marked for recursive removal by removeForConflicts
    // both scCreation and fwds must be cleared from mempool

    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scEntry(scTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(scTx.GetHash(), scEntry);
    ASSERT_TRUE(aMempool.hasSidechainCreationTx(scId));

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), scTx));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsOnlyInMempool_ScRecursiveRemoval) {
    // Associated scenario: upon block disconnections fwds have entered into mempool.
    // While unmounting block containing scCreation, scCreation cannot make to mempool. fwds must me purged
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(scTx, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, ScAndFwdsInMempool_FwdRecursiveRemoval) {
    // Associated scenario: upon block disconnections a fwd cannot make to mempool.
    // Recursive removal for refused fwd is called, but other fwds are unaffected

    uint256 scId = uint256S("1492");

    CTransaction fwdTx1 = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwdEntry1(fwdTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx1.GetHash(), fwdEntry1);

    CTransaction fwdTx2 = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry2(fwdTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdTx2.GetHash(), fwdEntry2);

    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    aMempool.remove(fwdTx2, removedTxs, removedCerts, /*fRecursive*/true);

    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx1));
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx2));
}

TEST_F(SidechainsInMempoolTestSuite, SimpleCertRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(6), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert.GetHash(), certEntry);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(cert, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(std::count(removedCerts.begin(), removedCerts.end(), cert));
    EXPECT_FALSE(mempool.existsCert(cert.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, ConflictingCertRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a certificate in mempool
    CScCertificate cert1 = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(6), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry1(cert1, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert1.GetHash(), certEntry1);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    CScCertificate cert2 = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2);
    mempool.removeConflicts(cert2, removedTxs, removedCerts);

    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(std::count(removedCerts.begin(), removedCerts.end(), cert1));
    EXPECT_FALSE(mempool.existsCert(cert1.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndCertInMempool_CertRemovalDoesNotAffectFwt) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a fwt in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(fwdTx.GetHash(), fwdEntry);

    //load a certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry1(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert.GetHash(), certEntry1);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(cert, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedCerts.begin(), removedCerts.end(), cert));
    EXPECT_FALSE(mempool.existsCert(cert.GetHash()));
    EXPECT_FALSE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx));
    EXPECT_TRUE(mempool.existsTx(fwdTx.GetHash()));
    ASSERT_TRUE(mempool.mapSidechains.count(scId));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).fwdTransfersSet.count(fwdTx.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).mBackwardCertificates.empty());
}

TEST_F(SidechainsInMempoolTestSuite, FwdsAndCertInMempool_FwtRemovalDoesNotAffectCert) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load a fwd in mempool
    CTransaction fwdTx = GenerateFwdTransferTx(scId, CAmount(20));
    CTxMemPoolEntry fwdEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(fwdTx.GetHash(), fwdEntry);

    //load a certificate in mempool
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNum*/0, /*endEpochBlockHash*/ uint256(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(2), /*numBwt*/2);
    CCertificateMemPoolEntry certEntry1(cert, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cert.GetHash(), certEntry1);

    //Remove the certificate
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(fwdTx, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), fwdTx));
    EXPECT_FALSE(mempool.existsTx(fwdTx.GetHash()));
    EXPECT_FALSE(std::count(removedCerts.begin(), removedCerts.end(), cert));
    EXPECT_TRUE(mempool.existsCert(cert.GetHash()));
    ASSERT_TRUE(mempool.mapSidechains.count(scId));
    EXPECT_FALSE(mempool.mapSidechains.at(scId).fwdTransfersSet.count(fwdTx.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).HasCert(cert.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, CertsCannotSpendHigherQualityCerts) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10000), /*epochLenght*/5);
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(401));
    sidechainsView.Flush();

    CBlockUndo dummyBlockUndo;
    for(const CTxScCreationOut& scCreationOut: scTx.GetVscCcOut())
        ASSERT_TRUE(sidechainsView.ScheduleSidechainEvent(scCreationOut, 401));

    std::vector<CScCertificateStatusUpdateInfo> dummy;
    ASSERT_TRUE(sidechainsView.HandleSidechainEvents(401 + Params().ScCoinsMaturity(), dummyBlockUndo, &dummy));
    sidechainsView.Flush();

    chainSettingUtils::ExtendChainActiveToHeight(/*startHeight*/406);

    const uint256& endEpochBlockHash = ArithToUint256(405);
    CValidationState state;
    bool missingInputs = false;

    //load a certificate in mempool (q=3, fee=600)
    CScCertificate cert1 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(1000),
        /*changeTotalAmount*/CAmount(400),/*numChangeOut*/1, /*bwtAmount*/CAmount(2000), /*numBwt*/2, /*quality*/3);

    ASSERT_TRUE(AcceptCertificateToMemoryPool(mempool, state, cert1, false, &missingInputs, false, false, false ));
    int64_t topQuality = mempool.mapSidechains.at(scId).GetTopQualityCert()->first;

    // create a certificate with same quality than top but depending on top-quality cert in mempool
    // and verify it is not accepted to mempool
    CScCertificate cert2 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(0),
        /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtAmount*/CAmount(90), /*numBwt*/2, /*quality*/topQuality,
        &cert1);

    EXPECT_FALSE(AcceptCertificateToMemoryPool(mempool, state, cert2, false, &missingInputs, false, false, false ));

    // create a certificate with lower quality than top but depending on top-quality cert in mempool
    // and verify that it is not accepted to mempool since a clash in consensus rules would be achieved
    // q1 < q2 but q1 depends on q2
    CScCertificate cert3 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(0),
        /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtAmount*/CAmount(90), /*numBwt*/2, /*quality*/topQuality-1,
        &cert1);

    EXPECT_FALSE(AcceptCertificateToMemoryPool(mempool, state, cert3, false, &missingInputs, false, false, false ));

    EXPECT_TRUE(mempool.mapSidechains.at(scId).GetTopQualityCert()->second == cert1.GetHash());
}

TEST_F(SidechainsInMempoolTestSuite, CertInMempool_QualityOfCerts) {

    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10000), /*epochLenght*/5);
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(401));
    sidechainsView.Flush();

    CBlockUndo dummyBlockUndo;
    for(const CTxScCreationOut& scCreationOut: scTx.GetVscCcOut())
        ASSERT_TRUE(sidechainsView.ScheduleSidechainEvent(scCreationOut, 401));

    std::vector<CScCertificateStatusUpdateInfo> dummy;
    ASSERT_TRUE(sidechainsView.HandleSidechainEvents(401 + Params().ScCoinsMaturity(), dummyBlockUndo, &dummy));
    sidechainsView.Flush();

    chainSettingUtils::ExtendChainActiveToHeight(/*startHeight*/406);

    const uint256& endEpochBlockHash = ArithToUint256(405);
    CValidationState state;
    bool missingInputs = false;

    //load a certificate in mempool (q=3, fee=600)
    CScCertificate cert1 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(1000),
        /*changeTotalAmount*/CAmount(400),/*numChangeOut*/1, /*bwtAmount*/CAmount(2000), /*numBwt*/2, /*quality*/3);

    EXPECT_TRUE(AcceptCertificateToMemoryPool(mempool, state, cert1, false, &missingInputs, false, false, false ));

    //load a certificate in mempool (q=2, fee=150)
    CScCertificate cert2 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(300),
        /*changeTotalAmount*/CAmount(150),/*numChangeOut*/1, /*bwtAmount*/CAmount(30), /*numBwt*/2, /*quality*/2);

    EXPECT_TRUE(AcceptCertificateToMemoryPool(mempool, state, cert2, false, &missingInputs, false, false, false ));

    //load a certificate in mempool (q=2, fee=150) ---> dropped because this fee is the same
    CScCertificate cert3 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(400),
        /*changeTotalAmount*/CAmount(250),/*numChangeOut*/1, /*bwtAmount*/CAmount(40), /*numBwt*/2, /*quality*/2);

    EXPECT_FALSE(AcceptCertificateToMemoryPool(mempool, state, cert3, false, &missingInputs, false, false, false ));

    //load a certificate in mempool (q=2, fee=100) ---> dropped because this fee is lower
    CScCertificate cert3b = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(390),
        /*changeTotalAmount*/CAmount(290),/*numChangeOut*/1, /*bwtAmount*/CAmount(40), /*numBwt*/2, /*quality*/2);

    EXPECT_FALSE(AcceptCertificateToMemoryPool(mempool, state, cert3b, false, &missingInputs, false, false, false ));

    //load a certificate in mempool (q=4, fee=100)
    CScCertificate cert4 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(1500),
        /*changeTotalAmount*/CAmount(1400),/*numChangeOut*/2, /*bwtAmount*/CAmount(60), /*numBwt*/2, /*quality*/4);

    EXPECT_TRUE(AcceptCertificateToMemoryPool(mempool, state, cert4, false, &missingInputs, false, false, false ));

    EXPECT_TRUE(mempool.mapSidechains.at(scId).HasCert(cert1.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).HasCert(cert2.GetHash()));
    EXPECT_FALSE(mempool.mapSidechains.at(scId).HasCert(cert3.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).HasCert(cert4.GetHash()));

    EXPECT_TRUE(mempool.mapSidechains.at(scId).GetTopQualityCert()->second == cert4.GetHash());
    
    // erase a cert from mempool
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(cert4, removedTxs, removedCerts, /*fRecursive*/false);

    EXPECT_FALSE(mempool.mapSidechains.at(scId).HasCert(cert4.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).GetTopQualityCert()->second == cert1.GetHash());

    int64_t tq = mempool.mapSidechains.at(scId).GetTopQualityCert()->first;
    
    //create a certificate (not loading it in mempool) with quality same as top-quality and remove any conflict in mempool with that 
    // verify that former top-quality has been removed
    CScCertificate cert5 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(0),
        /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtAmount*/CAmount(90), /*numBwt*/2, /*quality*/tq);

    EXPECT_TRUE(mempool.RemoveCertAndSync(mempool.FindCertWithQuality(scId, cert5.quality).first));
    
    EXPECT_FALSE(mempool.mapSidechains.at(scId).HasCert(cert1.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).GetTopQualityCert()->second == cert2.GetHash());
    tq = mempool.mapSidechains.at(scId).GetTopQualityCert()->first;

    //load a certificate in mempool (q=top=2, fee=200) --> former is removed since this fee is higher
    CScCertificate cert6 = GenerateCertificate(scId, /*epochNum*/0, endEpochBlockHash, /*inputAmount*/CAmount(600),
        /*changeTotalAmount*/CAmount(400),/*numChangeOut*/1, /*bwtAmount*/CAmount(30), /*numBwt*/2, /*quality*/tq);

    EXPECT_TRUE(AcceptCertificateToMemoryPool(mempool, state, cert6, false, &missingInputs, false, false, false ));
    EXPECT_FALSE(mempool.mapSidechains.at(scId).HasCert(cert2.GetHash()));
    EXPECT_TRUE(mempool.mapSidechains.at(scId).GetTopQualityCert()->second == cert6.GetHash());
}

TEST_F(SidechainsInMempoolTestSuite, CSWsToCeasedSidechainIsAccepted) {
    uint256 scId = uint256S("aaa");
    CSidechain sidechain;
    sidechain.balance = CAmount(100);
    sidechain.creationData.wCeasedVk = libzendoomc::ScVk(ParseHex(SAMPLE_VK));
    sidechain.prevBlockTopQualityCertReferencedEpoch = 10;
    sidechain.currentState = (uint8_t)CSidechain::State::CEASED;

    CSidechainEventsMap dummyCeasingMap;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);
    txCreationUtils::storeCertDataHash(*pcoinsTip, scId, 0);
    txCreationUtils::storeCertDataHash(*pcoinsTip, scId, sidechain.prevBlockTopQualityCertReferencedEpoch-1);

    CAmount cswTxCoins = sidechain.balance/2;
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId,/*epoch*/0, "aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CValidationState dummyState;
    libzendoomc::CScProofVerifier verifier = libzendoomc::CScProofVerifier::Disabled();
    EXPECT_TRUE(pcoinsTip->IsTxCswApplicableToState(cswTx, dummyState, verifier));
}

TEST_F(SidechainsInMempoolTestSuite, ExcessiveAmountOfCSWsToCeasedSidechainIsRejected) {
    uint256 scId = uint256S("aaa");
    CSidechain sidechain;
    sidechain.balance = CAmount(100);
    sidechain.creationData.wCeasedVk = libzendoomc::ScVk(ParseHex(SAMPLE_VK));
    sidechain.currentState = (uint8_t)CSidechain::State::CEASED;

    CSidechainEventsMap dummyCeasingMap;
    txCreationUtils::storeSidechain(*pcoinsTip,scId, sidechain, dummyCeasingMap);
    txCreationUtils::storeCertDataHash(*pcoinsTip, scId, 0);

    CAmount cswTxCoins = sidechain.balance*2;
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId,/*epoch*/0, "aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CValidationState dummyState;
    libzendoomc::CScProofVerifier verifier = libzendoomc::CScProofVerifier::Disabled();
    EXPECT_FALSE(pcoinsTip->IsTxCswApplicableToState(cswTx, dummyState, verifier));
}

TEST_F(SidechainsInMempoolTestSuite, CSWsToUnknownSidechainIsRefused) {
    uint256 unknownScId = uint256S("aaa");
    ASSERT_FALSE(pcoinsTip->HaveSidechain(unknownScId));
    CAmount cswTxCoins = 10;
    ASSERT_TRUE(cswTxCoins > 0);
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(unknownScId,/*epoch*/0, "aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CValidationState dummyState;
    libzendoomc::CScProofVerifier verifier = libzendoomc::CScProofVerifier::Disabled();
    EXPECT_FALSE(pcoinsTip->IsTxCswApplicableToState(cswTx, dummyState, verifier));
}

TEST_F(SidechainsInMempoolTestSuite, CSWsToActiveSidechainIsRefused) {
    uint256 scId = uint256S("aaa");
    CSidechain sidechain;
    sidechain.balance = CAmount(100);
    sidechain.currentState = (uint8_t)CSidechain::State::ALIVE;

    CSidechainEventsMap dummyCeasingMap;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    CAmount cswTxCoins = 10;
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId,/*epoch*/0, "aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CValidationState dummyState;
    libzendoomc::CScProofVerifier verifier = libzendoomc::CScProofVerifier::Disabled();
    EXPECT_FALSE(pcoinsTip->IsTxCswApplicableToState(cswTx, dummyState, verifier));
}

TEST_F(SidechainsInMempoolTestSuite, DuplicatedCSWsToCeasedSidechainAreRejected) {
    uint256 scId = uint256S("aaa");
    CAmount cswTxCoins = 10;
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId,/*epoch*/0, "aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx.GetHash(), cswEntry));

    CMutableTransaction duplicatedCswTx = cswTx;
    duplicatedCswTx.addOut(CTxOut(CAmount(5), CScript()));
    ASSERT_TRUE(cswTx.GetHash() != duplicatedCswTx.GetHash());

    CValidationState dummyState;
    libzendoomc::CScProofVerifier verifier = libzendoomc::CScProofVerifier::Disabled();
    EXPECT_FALSE(mempool.checkIncomingTxConflicts(duplicatedCswTx));
}

TEST_F(SidechainsInMempoolTestSuite, onConnectRemoveOutdatedCrosschainData_FT) {
    uint256 scId = uint256S("aaa");
    CSidechain sidechain;
    sidechain.balance = CAmount(1000);
    sidechain.currentState = (uint8_t)CSidechain::State::ALIVE;

    CSidechainEventsMap dummyCeasingMap;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    CTransaction fwtTx = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwtEntry(fwtTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(fwtTx.GetHash(), fwtEntry));

    // Sidechain State is Active. No removed Txs and Certs expected.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.onConnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(removedCerts.size() == 0);

    // Cease sidechains
    sidechain.currentState = (uint8_t)CSidechain::State::CEASED;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    // Sidechain State is Ceased. FT expected to be removed.
    removedTxs.clear();
    removedCerts.clear();
    mempool.onConnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 1);
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), fwtTx) != removedTxs.end());
    EXPECT_TRUE(removedCerts.size() == 0);
}

TEST_F(SidechainsInMempoolTestSuite, onConnectRemoveOutdatedCrosschainData_CSW) {
    uint256 scId = uint256S("aaa");
    CSidechain sidechain;
    sidechain.balance = CAmount(1000);
    sidechain.currentState = (uint8_t)CSidechain::State::CEASED;

    CSidechainEventsMap dummyCeasingMap;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    // Create and add CSW Tx
    CAmount cswTxCoins = sidechain.balance; // csw coins = total sc mature coins
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId,/*epoch*/0, "aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx.GetHash(), cswEntry));

    // Sidechain State is Ceased and there is no Sidechain balance conflicts in the mempool. No removed Txs and Certs expected.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.onConnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(removedCerts.size() == 0);

    // Add without checks another CSW tx to the same sidechain
    CAmount cswTxCoins2 = 1;
    CTxCeasedSidechainWithdrawalInput cswInput2 = GenerateCSWInput(scId,/*epoch*/0, "ddcc", cswTxCoins2);
    CTransaction cswTx2 = GenerateCSWTx(cswInput2);
    CTxMemPoolEntry cswEntry2(cswTx2, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx2.GetHash(), cswEntry2));

    // Mempool CSW Txs total withdrawal amount is greater than Sidechain mature balance -> both Txs expected to be removed.
    removedTxs.clear();
    removedCerts.clear();
    mempool.onConnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 2);
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), cswTx) != removedTxs.end());
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), cswTx2) != removedTxs.end());
    EXPECT_TRUE(removedCerts.size() == 0);
}

TEST_F(SidechainsInMempoolTestSuite, onDisconnectRemoveOutdatedCrosschainData_FT) {
    uint256 scId = uint256S("aaa");
    CSidechain sidechain;
    sidechain.balance = CAmount(1000);
    sidechain.currentState = (uint8_t)CSidechain::State::ALIVE;

    CSidechainEventsMap dummyCeasingMap;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    CTransaction fwtTx = GenerateFwdTransferTx(scId, CAmount(10));
    CTxMemPoolEntry fwtEntry(fwtTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(fwtTx.GetHash(), fwtEntry));

    // Sidechain State is Active. No removed Txs and Certs expected.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.onDisconnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(removedCerts.size() == 0);

    // revert sidechain state to NOT_APPLICABLE
    sidechain.currentState = (uint8_t)CSidechain::State::NOT_APPLICABLE;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    // Sidechain State is NOT_APPLICABLE. FT expected to be removed.
    removedTxs.clear();
    removedCerts.clear();
    mempool.onDisconnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 1);
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), fwtTx) != removedTxs.end());
    EXPECT_TRUE(removedCerts.size() == 0);
}

TEST_F(SidechainsInMempoolTestSuite, onDisconnectRemoveOutdatedCrosschainData_CSW) {
    uint256 scId = uint256S("aaa");
    CSidechain sidechain;
    sidechain.balance = CAmount(1000);
    sidechain.currentState = (uint8_t)CSidechain::State::CEASED;

    CSidechainEventsMap dummyCeasingMap;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    // Create and add CSW Tx
    CAmount cswTxCoins = sidechain.balance; // csw coins = total sc mature coins
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId,/*epoch*/0, "aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    EXPECT_TRUE(mempool.addUnchecked(cswTx.GetHash(), cswEntry));

    // Sidechain State is Ceased and there is no Sidechain balance conflicts in the mempool. No removed Txs and Certs expected.
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.onConnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 0);
    EXPECT_TRUE(removedCerts.size() == 0);

    // revert sidechain state to ACTIVE
    sidechain.currentState = (uint8_t)CSidechain::State::ALIVE;
    txCreationUtils::storeSidechain(*pcoinsTip, scId, sidechain, dummyCeasingMap);

    // Mempool CSW Txs total withdrawal amount is greater than Sidechain mature balance -> both Txs expected to be removed.
    removedTxs.clear();
    removedCerts.clear();
    mempool.onDisconnectRemoveOutdatedCrosschainData(pcoinsTip, removedTxs, removedCerts);
    EXPECT_TRUE(removedTxs.size() == 1);
    EXPECT_TRUE(std::find(removedTxs.begin(), removedTxs.end(), cswTx) != removedTxs.end());
    EXPECT_TRUE(removedCerts.size() == 0);
}

TEST_F(SidechainsInMempoolTestSuite, SimpleCswRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load csw tx to mempool
    CAmount dummyAmount(1);
    int32_t dummyEpoch(19);
    uint160 dummyPubKeyHash;
    libzendoomc::ScProof dummyScProof;
    CScript dummyRedeemScript;

    CMutableTransaction mutTx;
    libzendoomc::ScFieldElement nullfier_1{std::vector<unsigned char>(size_t(SC_FIELD_SIZE), 'a')};
    libzendoomc::ScFieldElement nullfier_2{std::vector<unsigned char>(size_t(SC_FIELD_SIZE), 'b')};
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(dummyAmount, scId, dummyEpoch, nullfier_1, dummyPubKeyHash, dummyScProof, dummyRedeemScript));
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(dummyAmount, scId, dummyEpoch, nullfier_2, dummyPubKeyHash, dummyScProof, dummyRedeemScript));

    CTransaction cswTx(mutTx);
    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cswTx.GetHash(), cswEntry);

    //Remove the csw tx
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;
    mempool.remove(cswTx, removedTxs, removedCerts, /*fRecursive*/false);

    //checks
    EXPECT_TRUE(removedCerts.size() == 0);
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), cswTx));
    EXPECT_FALSE(mempool.existsTx(cswTx.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, CSWsToCeasedSidechainWithoutVK) {
    // Create and persist sidechain
    int creationHeight = 1789;
    int epochLength = 10;
    CAmount scCoins = 1000;
    chainSettingUtils::ExtendChainActiveToHeight(creationHeight);
    // NOTE: no Ceased VK in SC creation output
    CTransaction scTx = GenerateScTx(scCoins, epochLength, /*ceasedVkDefined*/false);
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, creationHeight);
    sidechainsView.Flush();
    for(const CTxScCreationOut& scCreationOut: scTx.GetVscCcOut())
        ASSERT_TRUE(sidechainsView.ScheduleSidechainEvent(scCreationOut, creationHeight));
    sidechainsView.Flush();

    // Make coins mature
    CBlockUndo dummyBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    int coinsMatureHeight = creationHeight + Params().ScCoinsMaturity();
    ASSERT_TRUE(sidechainsView.HandleSidechainEvents(coinsMatureHeight, dummyBlockUndo, &dummy));


    // Cease sidechain
    int safeguardMargin = epochLength/5;
    int ceasingHeight = creationHeight + epochLength + safeguardMargin;
    ASSERT_TRUE(sidechainsView.HandleSidechainEvents(ceasingHeight, dummyBlockUndo, &dummy));
    sidechainsView.Flush();

    chainSettingUtils::ExtendChainActiveToHeight(ceasingHeight);

    // Create and add CSW Tx
    CAmount cswTxCoins = scCoins / 4;
    assert(cswTxCoins > 0);
    CTxCeasedSidechainWithdrawalInput cswInput = GenerateCSWInput(scId, /*epoch*/0,"aabb", cswTxCoins);
    CTransaction cswTx = GenerateCSWTx(cswInput);

    CValidationState cswTxState;
    bool missingInputs = false;

    EXPECT_FALSE(AcceptTxToMemoryPool(mempool, cswTxState, cswTx, false, &missingInputs));
}

TEST_F(SidechainsInMempoolTestSuite, ConflictingCswRemovalFromMempool) {
    //Create and persist sidechain
    CTransaction scTx = GenerateScTx(CAmount(10));
    const uint256& scId = scTx.GetScIdFromScCcOut(0);
    CBlock aBlock;
    CCoinsViewCache sidechainsView(pcoinsTip);
    sidechainsView.UpdateScInfo(scTx, aBlock, /*height*/int(1789));
    sidechainsView.Flush();

    //load csw tx to mempool
    CAmount dummyAmount(1);
    int32_t dummyEpoch(19);
    uint160 dummyPubKeyHash;
    libzendoomc::ScProof dummyScProof;
    CScript dummyRedeemScript;

    CMutableTransaction mutTx;
    mutTx.nVersion = SC_TX_VERSION;
    libzendoomc::ScFieldElement nullfier_1{std::vector<unsigned char>(size_t(SC_FIELD_SIZE), 'a')};
    libzendoomc::ScFieldElement nullfier_2{std::vector<unsigned char>(size_t(SC_FIELD_SIZE), 'b')};
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(dummyAmount, scId, dummyEpoch, nullfier_1, dummyPubKeyHash, dummyScProof, dummyRedeemScript));
    mutTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(dummyAmount, scId, dummyEpoch, nullfier_2, dummyPubKeyHash, dummyScProof, dummyRedeemScript));

    CTransaction cswTx(mutTx);
    CTxMemPoolEntry cswEntry(cswTx, /*fee*/CAmount(5), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(cswTx.GetHash(), cswEntry);

    //Remove the csw tx due to nullifier conflict with cswConfirmedTx
    std::list<CTransaction> removedTxs;
    std::list<CScCertificate> removedCerts;

    CMutableTransaction mutConfirmedTx;
    mutConfirmedTx.vcsw_ccin.push_back(CTxCeasedSidechainWithdrawalInput(dummyAmount, scId, dummyEpoch, nullfier_1, dummyPubKeyHash, dummyScProof, dummyRedeemScript));
    CTransaction cswConfirmedTx(mutConfirmedTx);
    ASSERT_TRUE(cswTx.GetHash() != cswConfirmedTx.GetHash());
    mempool.removeConflicts(cswConfirmedTx, removedTxs, removedCerts);

    //checks
    EXPECT_TRUE(removedCerts.size() == 0);
    EXPECT_TRUE(std::count(removedTxs.begin(), removedTxs.end(), cswTx));
    EXPECT_FALSE(mempool.existsTx(cswTx.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, ImmatureExpenditureRemoval) {
    //Create a coinbase
    CMutableTransaction mutCoinBase;
    mutCoinBase.vin.push_back(CTxIn(uint256(), -1));
    mutCoinBase.addOut(CTxOut(10,CScript()));
    mutCoinBase.addOut(CTxOut(20,CScript()));
    CTransaction coinBase(mutCoinBase);
    CTxUndo dummyUndo;
    UpdateCoins(coinBase, *pcoinsTip, dummyUndo, chainActive.Height());

    EXPECT_FALSE(pcoinsTip->AccessCoins(coinBase.GetHash())->isOutputMature(0, chainActive.Height()));
    // mature the coinbase
    chainSettingUtils::ExtendChainActiveToHeight(chainActive.Height() + COINBASE_MATURITY);
    EXPECT_TRUE(pcoinsTip->AccessCoins(coinBase.GetHash())->isOutputMature(0, chainActive.Height()));

    //add to mempool txes spending coinbase
    CMutableTransaction mutTx;
    mutTx.vin.push_back(CTxIn(COutPoint(coinBase.GetHash(), 0), CScript(), -1));
    CTransaction mempoolTx1(mutTx);
    CTxMemPoolEntry mempoolEntry1(mempoolTx1, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(mempoolTx1.GetHash(), mempoolEntry1);
    EXPECT_TRUE(mempool.exists(mempoolTx1.GetHash()));

    mutTx.vin.clear();
    mutTx.vin.push_back(CTxIn(COutPoint(coinBase.GetHash(), 1), CScript(), -1));
    CTransaction mempoolTx2(mutTx);
    CTxMemPoolEntry mempoolEntry2(mempoolTx2, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    mempool.addUnchecked(mempoolTx2.GetHash(), mempoolEntry2);
    EXPECT_TRUE(mempool.exists(mempoolTx2.GetHash()));

    //Revert chain undoing coinbase maturity, and check mempool cleanup
    chainSettingUtils::ExtendChainActiveToHeight(chainActive.Height() -1);
    // check coinbase is not mature anymore
    EXPECT_FALSE(pcoinsTip->AccessCoins(coinBase.GetHash())->isOutputMature(0, chainActive.Height()));

    //test
    mempool.removeImmatureExpenditures(pcoinsTip, chainActive.Height());

    //Check
    EXPECT_FALSE(mempool.exists(mempoolTx1.GetHash()));
    EXPECT_FALSE(mempool.exists(mempoolTx2.GetHash()));
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesInEmptyMempool) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut(dummyAmount, dummyScript);

    CMutableTransaction tx_1;
    tx_1.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_1.addOut(dummyOut);

    //test and checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_1).empty());
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesOfSingleTransaction) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut(dummyAmount, dummyScript);

    CMutableTransaction tx_1;
    tx_1.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_1.addOut(dummyOut);
    CTxMemPoolEntry tx_1_entry(tx_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);

    //test
    aMempool.addUnchecked(tx_1.GetHash(), tx_1_entry);

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_1).empty());
}


TEST_F(SidechainsInMempoolTestSuite, DependenciesOfSimpleChain) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut(dummyAmount, dummyScript);

    // Create chain tx_1 -> tx_2 -> tx_3
    CMutableTransaction tx_1;
    tx_1.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_1.addOut(dummyOut);
    CTxMemPoolEntry tx_1_entry(tx_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_1.GetHash(), tx_1_entry));

    CMutableTransaction tx_2;
    tx_2.vin.push_back(CTxIn(tx_1.GetHash(), 0, dummyScript));
    tx_2.addOut(dummyOut);
    CTxMemPoolEntry tx_2_entry(tx_2, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_2.GetHash(), tx_2_entry));

    CMutableTransaction tx_3;
    tx_3.vin.push_back(CTxIn(tx_2.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_3_entry(tx_3, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_3.GetHash(), tx_3_entry));

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_2) == std::vector<uint256>({tx_1.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_3) == std::vector<uint256>({tx_2.GetHash(),tx_1.GetHash()}));

    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_1) == std::vector<uint256>({tx_2.GetHash(),tx_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_2) == std::vector<uint256>({tx_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_3).empty());
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesOfTree) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut_1(dummyAmount, dummyScript);
    CTxOut dummyOut_2(++dummyAmount, dummyScript);

    CMutableTransaction tx_root;
    tx_root.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_root.addOut(dummyOut_1);
    tx_root.addOut(dummyOut_2);
    CTxMemPoolEntry tx_root_entry(tx_root, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_root.GetHash(), tx_root_entry));

    CMutableTransaction tx_child_1;
    tx_child_1.vin.push_back(CTxIn(tx_root.GetHash(), 0, dummyScript));
    tx_child_1.addOut(dummyOut_1);
    tx_child_1.addOut(dummyOut_2);
    CTxMemPoolEntry tx_child_1_entry(tx_child_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_child_1.GetHash(), tx_child_1_entry));

    CMutableTransaction tx_child_2;
    tx_child_2.vin.push_back(CTxIn(tx_root.GetHash(), 1, dummyScript));
    tx_child_2.addOut(dummyOut_1);
    tx_child_2.addOut(dummyOut_2);
    CTxMemPoolEntry tx_child_2_entry(tx_child_2, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_child_2.GetHash(), tx_child_2_entry));

    CMutableTransaction tx_grandchild_1;
    tx_grandchild_1.vin.push_back(CTxIn(tx_child_1.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_grandchild_1_entry(tx_grandchild_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_1.GetHash(), tx_grandchild_1_entry));

    CMutableTransaction tx_grandchild_2;
    tx_grandchild_2.vin.push_back(CTxIn(tx_child_1.GetHash(), 1, dummyScript));
    CTxMemPoolEntry tx_grandchild_2_entry(tx_grandchild_2, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_2.GetHash(), tx_grandchild_2_entry));

    CMutableTransaction tx_grandchild_3;
    tx_grandchild_3.vin.push_back(CTxIn(tx_child_2.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_grandchild_3_entry(tx_grandchild_3, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_3.GetHash(), tx_grandchild_3_entry));

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_root).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_child_1) == std::vector<uint256>({tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_child_2) == std::vector<uint256>({tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_1) == std::vector<uint256>({tx_child_1.GetHash(), tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_2) == std::vector<uint256>({tx_child_1.GetHash(), tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_3) == std::vector<uint256>({tx_child_2.GetHash(), tx_root.GetHash()}));

    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_root)
        == std::vector<uint256>({tx_child_1.GetHash(), tx_grandchild_2.GetHash(), tx_grandchild_1.GetHash(),
                                 tx_child_2.GetHash(), tx_grandchild_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_child_1) == std::vector<uint256>({tx_grandchild_1.GetHash(), tx_grandchild_2.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_child_2) == std::vector<uint256>({tx_grandchild_3.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_1).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_2).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_3).empty());
}

TEST_F(SidechainsInMempoolTestSuite, DependenciesOfTDAG) {
    // prerequisites
    CAmount dummyAmount(10);
    CScript dummyScript;
    CTxOut dummyOut_1(dummyAmount, dummyScript);
    CTxOut dummyOut_2(++dummyAmount, dummyScript);

    CMutableTransaction tx_root;
    tx_root.vin.push_back(CTxIn(uint256(), 0, dummyScript));
    tx_root.addOut(dummyOut_1);
    tx_root.addOut(dummyOut_2);
    CTxMemPoolEntry tx_root_entry(tx_root, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_root.GetHash(), tx_root_entry));

    CMutableTransaction tx_child_1;
    tx_child_1.vin.push_back(CTxIn(tx_root.GetHash(), 0, dummyScript));
    tx_child_1.addOut(dummyOut_1);
    CTxMemPoolEntry tx_child_1_entry(tx_child_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_child_1.GetHash(), tx_child_1_entry));

    CMutableTransaction tx_grandchild_1;
    tx_grandchild_1.vin.push_back(CTxIn(tx_root.GetHash(), 1, dummyScript));
    tx_grandchild_1.vin.push_back(CTxIn(tx_child_1.GetHash(), 0, dummyScript));
    CTxMemPoolEntry tx_grandchild_1_entry(tx_grandchild_1, /*fee*/dummyAmount, /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    ASSERT_TRUE(aMempool.addUnchecked(tx_grandchild_1.GetHash(), tx_grandchild_1_entry));

    //checks
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_root).empty());
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_child_1) == std::vector<uint256>({tx_root.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesFrom(tx_grandchild_1) == std::vector<uint256>({tx_child_1.GetHash(),tx_root.GetHash()}));

    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_root) == std::vector<uint256>({tx_child_1.GetHash(), tx_grandchild_1.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_child_1) == std::vector<uint256>({tx_grandchild_1.GetHash()}));
    EXPECT_TRUE(aMempool.mempoolDependenciesOf(tx_grandchild_1).empty());
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainsInMempoolTestSuite::InitCoinGeneration() {
    coinsKey.MakeNewKey(true);
    keystore.AddKey(coinsKey);

    coinsScript << OP_DUP << OP_HASH160 << ToByteVector(coinsKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
}

std::pair<uint256, CCoinsCacheEntry> SidechainsInMempoolTestSuite::GenerateCoinsAmount(const CAmount & amountToGenerate) {
    static unsigned int hashSeed = 1987;
    CCoinsCacheEntry entry;
    entry.flags = CCoinsCacheEntry::FRESH | CCoinsCacheEntry::DIRTY;

    entry.coins.fCoinBase = false;
    entry.coins.nVersion = TRANSPARENT_TX_VERSION;
    entry.coins.nHeight = minimalHeightForSidechains;

    entry.coins.vout.resize(1);
    entry.coins.vout[0].nValue = amountToGenerate;
    entry.coins.vout[0].scriptPubKey = coinsScript;

    std::stringstream num;
    num << std::hex << ++hashSeed;

    return std::pair<uint256, CCoinsCacheEntry>(uint256S(num.str()), entry);
}

bool SidechainsInMempoolTestSuite::StoreCoins(const std::pair<uint256, CCoinsCacheEntry>& entryToStore) {
    CCoinsViewCache view(pcoinsTip);
    CCoinsMap tmpCoinsMap;
    tmpCoinsMap[entryToStore.first] = entryToStore.second;

    const uint256 hashBlock = pcoinsTip->GetBestBlock(); //keep same best block as set in Fixture setup
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;
    CSidechainsMap mapSidechains;
    CSidechainEventsMap mapCeasingScs;
    CCswNullifiersMap cswNullifiers;
    CCertDataHashMap certDataHashes;

    pcoinsTip->BatchWrite(tmpCoinsMap, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapSidechains, mapCeasingScs, cswNullifiers, certDataHashes);

    return view.HaveCoins(entryToStore.first) == true;
}

CTransaction SidechainsInMempoolTestSuite::GenerateScTx(const CAmount & creationTxAmount, int epochLenght, bool ceasedVkDefined) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vsc_ccout.resize(1);
    scTx.vsc_ccout[0].nValue = creationTxAmount;
    scTx.vsc_ccout[0].withdrawalEpochLength = (epochLenght < 0)?getScMinWithdrawalEpochLength(): epochLenght;
    if(ceasedVkDefined)
        scTx.vsc_ccout[0].wCeasedVk = libzendoomc::ScVk();

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}

CTransaction SidechainsInMempoolTestSuite::GenerateFwdTransferTx(const uint256 & newScId, const CAmount & fwdTxAmount) {
    std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(1000);
    StoreCoins(coinData);

    CMutableTransaction scTx;
    scTx.nVersion = SC_TX_VERSION;
    scTx.vin.resize(1);
    scTx.vin[0].prevout = COutPoint(coinData.first, 0);

    scTx.vft_ccout.resize(1);
    scTx.vft_ccout[0].scId   = newScId;
    scTx.vft_ccout[0].nValue = fwdTxAmount;

    //scTx.vft_ccout.resize(2); //testing double deletes 
    //scTx.vft_ccout[1].scId   = newScId;
    //scTx.vft_ccout[1].nValue = fwdTxAmount;

    SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, scTx, 0);

    return scTx;
}

CTxCeasedSidechainWithdrawalInput SidechainsInMempoolTestSuite::GenerateCSWInput(const uint256& scId, int32_t epoch, const std::string& nullifierHex, CAmount amount)
{
    libzendoomc::ScFieldElement nullifier;
    nullifier.SetHex(nullifierHex);

    uint160 dummyPubKeyHash = coinsKey.GetPubKey().GetID();
    libzendoomc::ScProof dummyScProof;
    CScript dummyRedeemScript;

    return CTxCeasedSidechainWithdrawalInput(amount, scId, epoch, nullifier, dummyPubKeyHash, dummyScProof, dummyRedeemScript);
}

CTransaction SidechainsInMempoolTestSuite::GenerateCSWTx(const std::vector<CTxCeasedSidechainWithdrawalInput>& csws)
{
    CMutableTransaction mutTx;
    mutTx.nVersion = SC_TX_VERSION;
    mutTx.vcsw_ccin.insert(mutTx.vcsw_ccin.end(), csws.begin(), csws.end());

    CScript dummyScriptPubKey =
            GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/true);

    CAmount totalValue = 0;
    for(const CTxCeasedSidechainWithdrawalInput& csw: csws)
        totalValue += csw.nValue;
    mutTx.addOut(CTxOut(totalValue - 1, dummyScriptPubKey));

    // Sign CSW input
    for(const CTxCeasedSidechainWithdrawalInput& csw: csws)
    {
        SignSignature(keystore, csw.scriptPubKey(), mutTx, 0);
    }

    return mutTx;
}

CTransaction SidechainsInMempoolTestSuite::GenerateCSWTx(const CTxCeasedSidechainWithdrawalInput& csw)
{
    std::vector<CTxCeasedSidechainWithdrawalInput> csws;
    csws.push_back(csw);
    return GenerateCSWTx(csws);
}

CScCertificate SidechainsInMempoolTestSuite::GenerateCertificate(const uint256 & scId, int epochNum, const uint256 & endEpochBlockHash,
                 CAmount inputAmount, CAmount changeTotalAmount, unsigned int numChangeOut,
                 CAmount bwtTotalAmount, unsigned int numBwt, int64_t quality, const CTransactionBase* inputTxBase)
{
    CMutableScCertificate res;
    res.nVersion = SC_CERT_VERSION;
    res.scId = scId;
    res.epochNumber = epochNum;
    res.endEpochBlockHash = endEpochBlockHash;
    res.quality = quality;

    CScript dummyScriptPubKey =
            GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/true);
    for(unsigned int idx = 0; idx < numChangeOut; ++idx)
        res.addOut(CTxOut(changeTotalAmount/numChangeOut,dummyScriptPubKey));

    for(unsigned int idx = 0; idx < numBwt; ++idx)
        res.addBwt(CTxOut(bwtTotalAmount/numBwt, dummyScriptPubKey));

    if (inputTxBase)
    {
        res.vin.push_back(CTxIn(COutPoint(inputTxBase->GetHash(), 0), CScript(), -1));
        SignSignature(keystore, inputTxBase->GetVout()[0].scriptPubKey, res, 0);
    }
    else
    if (inputAmount > 0)
    {
        std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(inputAmount);
        StoreCoins(coinData);
    
        res.vin.push_back(CTxIn(COutPoint(coinData.first, 0), CScript(), -1));
        SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, res, 0);
    }

    return res;
}

