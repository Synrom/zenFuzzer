#include <gtest/gtest.h>
#include "tx_creation_utils.h"
#include <sc/sidechain.h>
#include <boost/filesystem.hpp>
#include <txdb.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <txmempool.h>
#include <undo.h>
#include <main.h>

class CInMemorySidechainDb final: public CCoinsView {
public:
    CInMemorySidechainDb()  = default;
    virtual ~CInMemorySidechainDb() = default;

    bool HaveSidechain(const uint256& scId) const { return inMemoryMap.count(scId); }
    bool GetSidechain(const uint256& scId, CSidechain& info) const {
        if(!inMemoryMap.count(scId))
            return false;
        info = inMemoryMap[scId];
        return true;
    }

    virtual void queryScIds(std::set<uint256>& scIdsList) const {
        for (auto& entry : inMemoryMap)
            scIdsList.insert(entry.first);
        return;
    }

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap)
    {
        for (auto& entry : sidechainMap)
            switch (entry.second.flag) {
                case CSidechainsCacheEntry::Flags::FRESH:
                case CSidechainsCacheEntry::Flags::DIRTY:
                    inMemoryMap[entry.first] = entry.second.scInfo;
                    break;
                case CSidechainsCacheEntry::Flags::ERASED:
                    inMemoryMap.erase(entry.first);
                    break;
                case CSidechainsCacheEntry::Flags::DEFAULT:
                    break;
                default:
                    return false;
            }
        sidechainMap.clear();
        return true;
    }

private:
    mutable boost::unordered_map<uint256, CSidechain, ObjectHasher> inMemoryMap;
};

class SidechainTestSuite: public ::testing::Test {

public:
    SidechainTestSuite():
          fakeChainStateDb(nullptr)
        , sidechainsView(nullptr) {};

    ~SidechainTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        fakeChainStateDb   = new CInMemorySidechainDb();
        sidechainsView     = new CCoinsViewCache(fakeChainStateDb);
    };

    void TearDown() override {
        delete sidechainsView;
        sidechainsView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;
    };

protected:
    CInMemorySidechainDb            *fakeChainStateDb;
    CCoinsViewCache                 *sidechainsView;

    //Helpers
    CBlockUndo   createBlockUndoWith(const uint256 & scId, int height, CAmount amount);
};

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// checkTxSemanticValidity ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, TransparentCcNullTxsAreSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createTransparentTx(/*ccIsNull = */true);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, TransparentNonCcNullTxsAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createTransparentTx(/*ccIsNull = */false);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SproutCcNullTxsAreCurrentlySupported) {
    CTransaction aTransaction = txCreationUtils::createSproutTx(/*ccIsNull = */true);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SproutNonCcNullTxsAreCurrentlySupported) {
    CTransaction aTransaction = txCreationUtils::createSproutTx(/*ccIsNull = */false);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithoutForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWithNoFwdTransfer(uint256S("1492"));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithPositiveForwardTransferAreSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith( uint256S("1492"), CAmount(1000));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(txState.IsValid());
}

TEST_F(SidechainTestSuite, SidechainCreationsWithTooLargePositiveForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(MAX_MONEY +1));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithZeroForwardTransferAreNotSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(0));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, SidechainCreationsWithNegativeForwardTransferNotAreSemanticallyValid) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(-1));
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

TEST_F(SidechainTestSuite, FwdTransferCumulatedAmountDoesNotOverFlow) {
    uint256 scId = uint256S("1492");
    CAmount initialFwdTrasfer(1);
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialFwdTrasfer);
    txCreationUtils::extendTransaction(aTransaction, scId, MAX_MONEY);
    CValidationState txState;

    //test
    bool res = Sidechain::checkTxSemanticValidity(aTransaction, txState);

    //checks
    EXPECT_FALSE(res);
    EXPECT_FALSE(txState.IsValid());
    EXPECT_TRUE(txState.GetRejectCode() == REJECT_INVALID)
        <<"wrong reject code. Value returned: "<<txState.GetRejectCode();
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////// HaveScRequirements /////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewScCreationsHaveTheRightDependencies) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("1492"), CAmount(1953));

    //test
    bool res = sidechainsView->HaveScRequirements(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, DuplicatedScCreationsHaveNotTheRightDependencies) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1815));

    //test
    bool res = sidechainsView->HaveScRequirements(duplicatedTx);

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistingSCsHaveTheRightDependencies) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1953));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(5));

    //test
    bool res = sidechainsView->HaveScRequirements(aTransaction);

    //checks
    EXPECT_TRUE(res);
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistingSCsHaveNotTheRightDependencies) {
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(uint256S("1492"), CAmount(1815));

    //test
    bool res = sidechainsView->HaveScRequirements(aTransaction);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////// ApplyMatureBalances /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, InitialCoinsTransferDoesNotModifyScBalanceBeforeCoinsMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 5;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight - 1;
    CBlockUndo anEmptyBlockUndo;

    //test
    bool res = sidechainsView->ApplyMatureBalances(lookupBlockHeight, anEmptyBlockUndo);

    //check
    EXPECT_TRUE(res);

    sidechainsView->Flush();
    CSidechain mgrInfos;
    ASSERT_TRUE(fakeChainStateDb->GetSidechain(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance < initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" while initial amount is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferModifiesScBalanceAtCoinMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 7;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight;
    CBlockUndo anEmptyBlockUndo;

    //test
    bool res = sidechainsView->ApplyMatureBalances(lookupBlockHeight, anEmptyBlockUndo);

    //checks
    EXPECT_TRUE(res);

    sidechainsView->Flush();
    CSidechain mgrInfos;
    ASSERT_TRUE(fakeChainStateDb->GetSidechain(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance == initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" expected one is "<<initialAmount;
}

TEST_F(SidechainTestSuite, InitialCoinsTransferDoesNotModifyScBalanceAfterCoinsMaturity) {
    uint256 scId = uint256S("a1b2");
    CAmount initialAmount = 1000;
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, initialAmount);
    int scCreationHeight = 11;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int coinMaturityHeight = /*height*/int(1789) + Params().ScCoinsMaturity();
    int lookupBlockHeight = coinMaturityHeight + 1;
    CBlockUndo anEmptyBlockUndo;

    //test
    EXPECT_DEATH(sidechainsView->ApplyMatureBalances(lookupBlockHeight, anEmptyBlockUndo), "maturityHeight >= blockHeight");

    sidechainsView->Flush();
    CSidechain mgrInfos;
    ASSERT_TRUE(fakeChainStateDb->GetSidechain(scId, mgrInfos));
    EXPECT_TRUE(mgrInfos.balance < initialAmount)
        <<"resulting balance is "<<mgrInfos.balance <<" while initial amount is "<<initialAmount;
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////// RestoreImmatureBalances ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RestoreImmatureBalancesAffectsScBalance) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 71;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo;

    sidechainsView->ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);

    CSidechain viewInfos;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    CAmount amountToUndo = 17;
    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,amountToUndo);

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_TRUE(res);
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance - amountToUndo)
        <<"balance after restore is "<<viewInfos.balance
        <<" instead of "<< scBalance - amountToUndo;
}

TEST_F(SidechainTestSuite, YouCannotRestoreMoreCoinsThanAvailableBalance) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 1991;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo;

    sidechainsView->ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity(), aBlockUndo);
    CSidechain viewInfos;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    CAmount scBalance = viewInfos.balance;

    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,CAmount(50));

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == scBalance)
        <<"balance after restore is "<<viewInfos.balance <<" instead of"<< scBalance;
}

TEST_F(SidechainTestSuite, RestoringBeforeBalanceMaturesHasNoEffects) {
    uint256 scId = uint256S("ca1985");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(34));
    int scCreationHeight = 71;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    CBlockUndo aBlockUndo;

    sidechainsView->ApplyMatureBalances(scCreationHeight + Params().ScCoinsMaturity() -1, aBlockUndo);

    aBlockUndo = createBlockUndoWith(scId,scCreationHeight,CAmount(17));

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
    CSidechain viewInfos;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    EXPECT_TRUE(viewInfos.balance == 0)
        <<"balance after restore is "<<viewInfos.balance <<" instead of 0";
}

TEST_F(SidechainTestSuite, YouCannotRestoreCoinsFromInexistentSc) {
    uint256 inexistentScId = uint256S("ca1985");
    int scCreationHeight = 71;

    CAmount amountToUndo = 10;

    CBlockUndo aBlockUndo = createBlockUndoWith(inexistentScId,scCreationHeight,amountToUndo);

    //test
    bool res = sidechainsView->RestoreImmatureBalances(scCreationHeight, aBlockUndo);

    //checks
    EXPECT_FALSE(res);
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// RevertTxOutputs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, RevertingScCreationTxRemovesTheSc) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, scCreationHeight);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, RevertingFwdTransferRemovesCoinsFromImmatureBalance) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(7));
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, fwdTxHeight);

    //checks
    EXPECT_TRUE(res);
    CSidechain viewInfos;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity()) == 0)
        <<"resulting immature amount is "<< viewInfos.mImmatureAmounts.count(fwdTxHeight + Params().ScCoinsMaturity());
}

TEST_F(SidechainTestSuite, ScCreationTxCannotBeRevertedIfScIsNotPreviouslyCreated) {
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(uint256S("a1b2"),CAmount(15));

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, FwdTransferTxToUnexistingScCannotBeReverted) {
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(uint256S("a1b2"), CAmount(999));

    //test
    bool res = sidechainsView->RevertTxOutputs(aTransaction, /*height*/int(1789));

    //checks
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, RevertingAFwdTransferOnTheWrongHeightHasNoEffect) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);

    int fwdTxHeight = 5;
    CAmount fwdAmount = 7;
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeight);

    //test
    int faultyHeight = fwdTxHeight -1;
    bool res = sidechainsView->RevertTxOutputs(aTransaction, faultyHeight);

    //checks
    EXPECT_FALSE(res);
    CSidechain viewInfos;
    ASSERT_TRUE(sidechainsView->GetSidechain(scId, viewInfos));
    EXPECT_TRUE(viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity()) == fwdAmount)
        <<"Immature amount is "<<viewInfos.mImmatureAmounts.at(fwdTxHeight + Params().ScCoinsMaturity())
        <<"instead of "<<fwdAmount;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// UpdateScInfo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TEST_F(SidechainTestSuite, NewSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(newScId, CAmount(1));
    CBlock aBlock;

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(newScId));
}

TEST_F(SidechainTestSuite, DuplicatedSCsAreRejected) {
    uint256 scId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    CTransaction duplicatedTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(999));

    //test
    bool res = sidechainsView->UpdateScInfo(duplicatedTx, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
}

TEST_F(SidechainTestSuite, NoRollbackIsPerformedOnceInvalidTransactionIsEncountered) {
    uint256 firstScId = uint256S("1492");
    uint256 secondScId = uint256S("1912");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(firstScId, CAmount(10));
    CBlock aBlock;
    txCreationUtils::extendTransaction(aTransaction, firstScId, CAmount(20));
    txCreationUtils::extendTransaction(aTransaction, secondScId, CAmount(30));

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(firstScId));
    EXPECT_FALSE(sidechainsView->HaveSidechain(secondScId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToNonExistentSCsAreRejected) {
    uint256 nonExistentId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createFwdTransferTxWith(nonExistentId, CAmount(10));
    CBlock aBlock;

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_FALSE(res);
    EXPECT_FALSE(sidechainsView->HaveSidechain(nonExistentId));
}

TEST_F(SidechainTestSuite, ForwardTransfersToExistentSCsAreRegistered) {
    uint256 newScId = uint256S("1492");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(newScId, CAmount(5));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    aTransaction = txCreationUtils::createFwdTransferTxWith(newScId, CAmount(15));

    //test
    bool res = sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //check
    EXPECT_TRUE(res);
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// BatchWrite ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FRESHSidechainsGetWrittenInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = CSidechain();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    //write new sidechain when backing view doesn't know about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, FRESHSidechainsCanBeWrittenOnlyIfUnknownToBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    //Prefill backing cache with sidechain
    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    //attempt to write new sidechain when backing view already knows about it
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = CSidechain();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    ASSERT_DEATH(sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite),"");
}

TEST_F(SidechainTestSuite, DIRTYSidechainsAreStoredInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    entry.scInfo = CSidechain();
    entry.flag   = CSidechainsCacheEntry::Flags::FRESH;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view doesn't know about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, DIRTYSidechainsUpdatesDirtyOnesInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DIRTY;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view already knows about it
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    CSidechain cachedSc;
    EXPECT_TRUE(sidechainsView->GetSidechain(scId, cachedSc));
    EXPECT_TRUE(cachedSc.balance == CAmount(12) );
}

TEST_F(SidechainTestSuite, DIRTYSidechainsOverwriteErasedOnesInBackingCache) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    //Create sidechain...
    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    //...then revert it to have it erased
    sidechainsView->RevertTxOutputs(scTx, /*nHeight*/1000);
    ASSERT_FALSE(sidechainsView->HaveSidechain(scId));

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DIRTY;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    CSidechain cachedSc;
    EXPECT_TRUE(sidechainsView->GetSidechain(scId, cachedSc));
    EXPECT_TRUE(cachedSc.balance == CAmount(12) );
}

TEST_F(SidechainTestSuite, ERASEDSidechainsSetExistingOnesInBackingCacheasErased) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::ERASED;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    bool res = sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite);

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(sidechainsView->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, DEFAULTSidechainsCanBeWrittenInBackingCacheasOnlyIfUnchanged) {
    CCoinsMap mapCoins;
    const uint256 hashBlock;
    const uint256 hashAnchor;
    CAnchorsMap mapAnchors;
    CNullifiersMap mapNullifiers;


    uint256 scId = uint256S("aaaa");
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scTx, CBlock(), /*nHeight*/ 1000);

    CSidechainsMap mapToWrite;
    CSidechainsCacheEntry entry;
    CSidechain updatedScInfo;
    updatedScInfo.balance = CAmount(12);
    entry.scInfo = updatedScInfo;
    entry.flag   = CSidechainsCacheEntry::Flags::DEFAULT;

    mapToWrite[scId] = entry;

    //write dirty sidechain when backing view have it erased
    ASSERT_DEATH(sidechainsView->BatchWrite(mapCoins, hashBlock, hashAnchor, mapAnchors, mapNullifiers, mapToWrite),"");
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// Flush /////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, FlushPersistsNewSidechains) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1000));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(fakeChainStateDb->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, FlushPersistsForwardTransfers) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    int scCreationHeight = 1;
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, scCreationHeight);
    sidechainsView->Flush();

    CAmount fwdTxAmount = 1000;
    int fwdTxHeght = scCreationHeight + 10;
    int fwdTxMaturityHeight = fwdTxHeght + Params().ScCoinsMaturity();
    aTransaction = txCreationUtils::createFwdTransferTxWith(scId, CAmount(1000));
    sidechainsView->UpdateScInfo(aTransaction, aBlock, fwdTxHeght);

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);

    CSidechain persistedInfo;
    ASSERT_TRUE(fakeChainStateDb->GetSidechain(scId, persistedInfo));
    ASSERT_TRUE(persistedInfo.mImmatureAmounts.at(fwdTxMaturityHeight) == fwdTxAmount)
        <<"Following flush, persisted fwd amount should equal the one in view";
}

TEST_F(SidechainTestSuite, FlushPersistsScErasureToo) {
    uint256 scId = uint256S("a1b2");
    CTransaction aTransaction = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock aBlock;
    sidechainsView->UpdateScInfo(aTransaction, aBlock, /*height*/int(1789));
    sidechainsView->Flush();

    sidechainsView->RevertTxOutputs(aTransaction, /*height*/int(1789));

    //test
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_FALSE(fakeChainStateDb->HaveSidechain(scId));
}

TEST_F(SidechainTestSuite, FlushPersistsNewScsOnTopOfErasedOnes) {
    uint256 scId = uint256S("a1b2");
    CBlock aBlock;

    //Create new sidechain and flush it
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    sidechainsView->UpdateScInfo(scCreationTx, aBlock, /*height*/int(1789));
    sidechainsView->Flush();
    ASSERT_TRUE(fakeChainStateDb->HaveSidechain(scId));

    //Remove it and flush again
    sidechainsView->RevertTxOutputs(scCreationTx, /*height*/int(1789));
    sidechainsView->Flush();
    ASSERT_FALSE(fakeChainStateDb->HaveSidechain(scId));

    //re-create sc with same scId as erased one
    CTransaction scReCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(20));
    sidechainsView->UpdateScInfo(scReCreationTx, aBlock, /*height*/int(1815));
    bool res = sidechainsView->Flush();

    //checks
    EXPECT_TRUE(res);
    EXPECT_TRUE(fakeChainStateDb->HaveSidechain(scId));
}
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// queryScIds //////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, queryScIdsReturnsNonErasedSidechains) {
    CBlock aBlock;

    uint256 scId1 = uint256S("aaaa");
    int sc1CreationHeight(11);
    CTransaction scTx1 = txCreationUtils::createNewSidechainTxWith(scId1, CAmount(1));
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx1, aBlock, sc1CreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId1, CAmount(3));
    int fwdTxHeight(22);
    sidechainsView->UpdateScInfo(fwdTx, aBlock, fwdTxHeight);

    uint256 scId2 = uint256S("bbbb");
    int sc2CreationHeight(33);
    CTransaction scTx2 = txCreationUtils::createNewSidechainTxWith(scId2, CAmount(2));
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx2, aBlock,sc2CreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    ASSERT_TRUE(sidechainsView->RevertTxOutputs(scTx2, sc2CreationHeight));

    //test
    std::set<uint256> knownScIdsSet;
    sidechainsView->queryScIds(knownScIdsSet);

    //check
    EXPECT_TRUE(knownScIdsSet.size() == 1)<<"Instead knowScIdSet size is "<<knownScIdsSet.size();
    EXPECT_TRUE(knownScIdsSet.count(scId1) == 1)<<"Actual count is "<<knownScIdsSet.count(scId1);
    EXPECT_TRUE(knownScIdsSet.count(scId2) == 0)<<"Actual count is "<<knownScIdsSet.count(scId2);
}

TEST_F(SidechainTestSuite, queryScIdsOnChainstateDbSelectOnlySidechains) {

    //init a tmp chainstateDb
    boost::filesystem::path pathTemp(boost::filesystem::temp_directory_path() / boost::filesystem::unique_path());
    const unsigned int      chainStateDbSize(2 * 1024 * 1024);
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    CCoinsViewDB chainStateDb(chainStateDbSize,/*fWipe*/true);
    sidechainsView->SetBackend(chainStateDb);

    //prepare a sidechain
    CBlock aBlock;
    uint256 scId = uint256S("aaaa");
    int sc1CreationHeight(11);
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(1));
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx, aBlock, sc1CreationHeight));

    //prepare a coin
    CCoinsCacheEntry aCoin;
    aCoin.flags = CCoinsCacheEntry::FRESH | CCoinsCacheEntry::DIRTY;
    aCoin.coins.fCoinBase = false;
    aCoin.coins.nVersion = TRANSPARENT_TX_VERSION;
    aCoin.coins.vout.resize(1);
    aCoin.coins.vout[0].nValue = CAmount(10);

    CCoinsMap mapCoins;
    mapCoins[uint256S("aaaa")] = aCoin;
    CAnchorsMap    emptyAnchorsMap;
    CNullifiersMap emptyNullifiersMap;
    CSidechainsMap emptySidechainsMap;

    sidechainsView->BatchWrite(mapCoins, uint256(), uint256(), emptyAnchorsMap, emptyNullifiersMap, emptySidechainsMap);

    //flush both the coin and the sidechain to the tmp chainstatedb
    ASSERT_TRUE(sidechainsView->Flush());

    //test
    std::set<uint256> knownScIdsSet;
    sidechainsView->queryScIds(knownScIdsSet);

    //check
    EXPECT_TRUE(knownScIdsSet.size() == 1)<<"Instead knowScIdSet size is "<<knownScIdsSet.size();
    EXPECT_TRUE(knownScIdsSet.count(scId) == 1)<<"Actual count is "<<knownScIdsSet.count(scId);

    ClearDatadirCache();
    boost::system::error_code ec;
    boost::filesystem::remove_all(pathTemp.string(), ec);
}
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// GetSidechain /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainTestSuite, CSidechainFromMempoolRetrievesUnconfirmedInformation) {
    CTxMemPool aMempool(CFeeRate(1));

    //Confirm a Sidechain
    uint256 scId = uint256S("dead");
    CAmount creationAmount = 10;
    CTransaction scTx = txCreationUtils::createNewSidechainTxWith(scId, creationAmount);
    int scCreationHeight(11);
    CBlock aBlock;
    ASSERT_TRUE(sidechainsView->UpdateScInfo(scTx, aBlock, scCreationHeight));
    ASSERT_TRUE(sidechainsView->Flush());

    //Fully mature initial Sc balance
    int coinMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CBlockUndo anEmptyBlockUndo;
    ASSERT_TRUE(sidechainsView->ApplyMatureBalances(coinMaturityHeight, anEmptyBlockUndo));

    //a fwd is accepted in mempool
    CAmount fwdAmount = 20;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    CTxMemPoolEntry fwdPoolEntry(fwdTx, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(fwdPoolEntry.GetTx().GetHash(), fwdPoolEntry);

    //a bwt cert is accepted in mempool too
    CAmount certAmount = 4;
    CMutableScCertificate cert = CScCertificate();
    cert.scId = scId;
    cert.totalAmount = certAmount;
    CCertificateMemPoolEntry bwtPoolEntry(cert, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/1987);
    aMempool.addUnchecked(bwtPoolEntry.GetCertificate().GetHash(), bwtPoolEntry);

    //test
    CCoinsViewMemPool viewMemPool(sidechainsView, aMempool);
    CSidechain retrievedInfo;
    viewMemPool.GetSidechain(scId, retrievedInfo);

    //check
    EXPECT_TRUE(retrievedInfo.creationBlockHeight == scCreationHeight);
    EXPECT_TRUE(retrievedInfo.balance == creationAmount - certAmount);
    EXPECT_TRUE(retrievedInfo.lastEpochReferencedByCertificate == -1); //certs in mempool do not affect lastEpochReferencedByCertificate
    EXPECT_TRUE(retrievedInfo.mImmatureAmounts.at(-1) == fwdAmount);
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////// Test Fixture definitions ///////////////////////////
///////////////////////////////////////////////////////////////////////////////
CBlockUndo SidechainTestSuite::createBlockUndoWith(const uint256 & scId, int height, CAmount amount)
{
    CBlockUndo retVal;
    CAmount AmountPerHeight = amount;
    ScUndoData data;
    data.immAmount = AmountPerHeight;
    data.certEpoch = CScCertificate::EPOCH_NULL;
    retVal.msc_iaundo[scId] = data;

    return retVal;
}
