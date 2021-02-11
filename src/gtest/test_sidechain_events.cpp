#include <gtest/gtest.h>
#include <chainparams.h>
#include <coins.h>
#include "tx_creation_utils.h"
#include <main.h>
#include <undo.h>
#include <consensus/validation.h>

class SidechainsEventsTestSuite: public ::testing::Test {

public:
    SidechainsEventsTestSuite():
        dummyBackingView(nullptr)
        , view(nullptr) {};

    ~SidechainsEventsTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        dummyBackingView = new CCoinsView();
        view = new CCoinsViewCache(dummyBackingView);
    };

    void TearDown() override {
        delete view;
        view = nullptr;

        delete dummyBackingView;
        dummyBackingView = nullptr;
    };

protected:
    CCoinsView        *dummyBackingView;
    CCoinsViewCache   *view;
};


///////////////////////////////////////////////////////////////////////////////
/////////////////////////// isSidechainCeased /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, UnknownSidechainIsNeitherAliveNorCeased) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1912;
    ASSERT_FALSE(view->HaveSidechain(scId));

    CSidechain::State state = view->isCeasedAtHeight(scId, creationHeight);
    EXPECT_TRUE(state == CSidechain::State::NOT_APPLICABLE)
        <<"sc is in state "<<int(state);
}

TEST_F(SidechainsEventsTestSuite, SidechainInItsFirstEpochIsNotCeased) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1912;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), /*height*/10);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);

    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int endEpochHeight = sidechain.StartHeightForEpoch(currentEpoch+1)-1;

    for(int height = creationHeight; height <= endEpochHeight; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, SidechainIsNotCeasedBeforeNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1945;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10), /*epochLength*/11);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);

    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int nextEpochStart = sidechain.StartHeightForEpoch(currentEpoch+1);

    for(int height = nextEpochStart; height <= nextEpochStart + sidechain.SafeguardMargin(); ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, SidechainIsCeasedAftereNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10),/*epochLength*/100);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);

    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int nextEpochStart = sidechain.StartHeightForEpoch(currentEpoch+1);
    int nextEpochEnd = sidechain.StartHeightForEpoch(currentEpoch+2)-1;

    for(int height = nextEpochStart + sidechain.SafeguardMargin()+1; height <= nextEpochEnd; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::CEASED)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, FullCertMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int nextEpochStart = sidechain.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + sidechain.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +sidechain.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, PureBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int nextEpochStart = sidechain.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + sidechain.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(),
        /*changeTotalAmount*/CAmount(10),/*numChangeOut*/10, /*bwtAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +sidechain.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite, NoBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int nextEpochStart = sidechain.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + sidechain.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(),
            /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +sidechain.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(SidechainsEventsTestSuite,EmptyCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int nextEpochStart = sidechain.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + sidechain.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(),
            /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +sidechain.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}
///////////////////////////////////////////////////////////////////////////////
/////////////////////// Ceasing Sidechain updates /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForScCreation) {
    int scCreationHeight = 1492;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    ASSERT_TRUE(view->UpdateSidechain(scCreationTx, creationBlock, scCreationHeight));

    //test
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->ScheduleSidechainEvent(scCreationOut, scCreationHeight));

    //Checks
    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int ceasingHeight = sidechain.StartHeightForEpoch(1)+sidechain.SafeguardMargin()+1;
    CSidechainEvents ceasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);
}


TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForFullCert) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);

    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2);

    CBlockUndo blockUndo;
    EXPECT_TRUE(view->UpdateSidechain(cert, blockUndo));

    //test
    view->ScheduleSidechainEvent(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));
}

TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForPureBwtCert) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);


    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,
        /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/4);

    CBlockUndo blockUndo;
    EXPECT_TRUE(view->UpdateSidechain(cert, blockUndo));

    //test
    view->ScheduleSidechainEvent(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));
}

TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForNoBwtCert) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);


    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,
        /*changeTotalAmount*/CAmount(3), /*numChangeOut*/3,/*bwtAmount*/CAmount(0), /*numBwt*/0);

    CBlockUndo blockUndo;
    EXPECT_TRUE(view->UpdateSidechain(cert, blockUndo));

    //test
    view->ScheduleSidechainEvent(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));
}

TEST_F(SidechainsEventsTestSuite, CeasingHeightUpdateForEmptyCertificate) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);


    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,
        /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/0);

    CBlockUndo blockUndo;
    EXPECT_TRUE(view->UpdateSidechain(cert, blockUndo));

    //test
    view->ScheduleSidechainEvent(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));
}
///////////////////////////////////////////////////////////////////////////////
////////////////////////////// HandleCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, FullCertCoinsHaveBwtStrippedOutWhenSidechainCeases) {
    //Create sidechain
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain preCertSidechain;
    view->GetSidechain(scId, preCertSidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4), /*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight,/*isBlockTopQualityCert*/true);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = preCertSidechain.StartHeightForEpoch(cert.epochNumber+2)+preCertSidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy));

    //Checks
    CCoins updatedCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    updatedCoin.ClearUnspendable();
    EXPECT_TRUE(updatedCoin.vout.size() == cert.nFirstBwtPos);
    EXPECT_TRUE(updatedCoin.nFirstBwtPos == cert.nFirstBwtPos);
    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, PureBwtCoinsAreRemovedWhenSidechainCeases) {
    //Create sidechain
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain preCertSidechain;
    view->GetSidechain(scId, preCertSidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight,/*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //test
    int minimalCeaseHeight = preCertSidechain.StartHeightForEpoch(cert.epochNumber+2)+preCertSidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    unsigned int bwtCounter = 0;
    ASSERT_TRUE(coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.size() == 1);
    for(int pos =  cert.nFirstBwtPos; pos < cert.GetVout().size(); ++pos)
    {
        const CTxOut& out = cert.GetVout()[pos];
        EXPECT_TRUE( (coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))
                     <<coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).nVersion;
        EXPECT_TRUE(coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).nBwtMaturityHeight == coinFromCert.nBwtMaturityHeight);
        EXPECT_TRUE(out == coinsBlockUndo.scUndoDatabyScId.at(scId).ceasedBwts.at(bwtCounter).txout);
        ++bwtCounter;
    }

    EXPECT_TRUE(cert.GetVout().size() == bwtCounter); //all cert outputs are handled
    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, NoBwtCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    //Create sidechain
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain preCertSidechain;
    view->GetSidechain(scId, preCertSidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4), /*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight,/*isBlockTopQualityCert*/true);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = preCertSidechain.StartHeightForEpoch(cert.epochNumber+2)+preCertSidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy));

    //Checks
    CCoins updatedCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    updatedCoin.ClearUnspendable();
    EXPECT_TRUE(updatedCoin.vout.size() == cert.GetVout().size());
    EXPECT_TRUE(updatedCoin.nFirstBwtPos == cert.nFirstBwtPos);
    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, EmptyCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    //Create sidechain
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain preCertSidechain;
    view->GetSidechain(scId, preCertSidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight, /*isBlockTopQualityCert*/true);
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = preCertSidechain.StartHeightForEpoch(cert.epochNumber+2)+preCertSidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    EXPECT_FALSE(view->HaveSidechainEvents(minimalCeaseHeight));
}
///////////////////////////////////////////////////////////////////////////////
////////////////////////////// RevertCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, RestoreFullCertCeasedCoins) {
    //Create sidechain
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock scCreationBlock;
    int scCreationHeight = 1789;
    view->UpdateSidechain(scCreationTx, scCreationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    int certReferencedEpoch = 0;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
            /*changeTotalAmount*/CAmount(4), /*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, sidechain.StartHeightForEpoch(1), /*isBlockTopQualityCert*/true);
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = sidechain.StartHeightForEpoch(certReferencedEpoch+2)+sidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy);

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummy);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout.at(pos) == originalCoins.vout.at(pos));
    }
    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, RestorePureBwtCeasedCoins) {
    //Create sidechain
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock scCreationBlock;
    int scCreationHeight = 1789;
    view->UpdateSidechain(scCreationTx, scCreationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    int certReferencedEpoch = 0;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
            /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, sidechain.StartHeightForEpoch(1), /*isBlockTopQualityCert*/true);
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = sidechain.StartHeightForEpoch(certReferencedEpoch+2)+sidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy);
    ASSERT_FALSE(view->HaveCoins(cert.GetHash()));

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummy);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, RestoreNoBwtCeasedCoins) {
    //Create sidechain
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock scCreationBlock;
    int scCreationHeight = 1789;
    view->UpdateSidechain(scCreationTx, scCreationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    int certReferencedEpoch = 0;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
            /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, sidechain.StartHeightForEpoch(1), /*isBlockTopQualityCert*/true);
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = sidechain.StartHeightForEpoch(certReferencedEpoch+2)+sidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy);

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummy);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}

TEST_F(SidechainsEventsTestSuite, RestoreEmptyCertCeasedCoins) {
    //Create sidechain
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock scCreationBlock;
    int scCreationHeight = 1789;
    view->UpdateSidechain(scCreationTx, scCreationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, scCreationHeight);
    CSidechain sidechain;
    view->GetSidechain(scId, sidechain);

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 500);
    CBlock dummyBlock;
    int certReferencedEpoch = 0;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
            /*changeTotalAmount*/CAmount(0), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, sidechain.StartHeightForEpoch(1), /*isBlockTopQualityCert*/true);
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    //Make the sidechain cease
    int minimalCeaseHeight = sidechain.StartHeightForEpoch(certReferencedEpoch+2)+sidechain.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    view->HandleSidechainEvents(minimalCeaseHeight, coinsBlockUndo, &dummy);

    //test
    view->RevertSidechainEvents(coinsBlockUndo, minimalCeaseHeight, &dummy);

    //checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    EXPECT_TRUE(view->HaveSidechainEvents(minimalCeaseHeight));
}
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// UndoCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, CancelSidechainEvent) {
    int scCreationHeight = 1492;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    ASSERT_TRUE(view->UpdateSidechain(scCreationTx, creationBlock, scCreationHeight));

    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->ScheduleSidechainEvent(scCreationOut, scCreationHeight));

    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int ceasingHeight = sidechain.StartHeightForEpoch(1)+sidechain.SafeguardMargin()+1;
    CSidechainEvents ceasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);

    //test
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->CancelSidechainEvent(scCreationOut, scCreationHeight));

    //checks
    CSidechainEvents restoredCeasingScIds;
    EXPECT_FALSE(view->HaveSidechainEvents(ceasingHeight));
}

TEST_F(SidechainsEventsTestSuite, UndoFullCertUpdatesToCeasingScs) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);


    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"),
        /*changeTotalAmount*/CAmount(4), /*numChangeOut*/4, /*bwtTotalAmount*/CAmount(0), /*numBwt*/3);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));

    //test
    view->RestoreSidechain(cert, blockUndo.scUndoDatabyScId.at(scId));
    view->CancelSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);
    EXPECT_FALSE(view->HaveSidechainEvents(newCeasingHeight));
    CSidechainEvents restoredCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);

    //check idempotency
    EXPECT_TRUE(view->CancelSidechainEvent(cert));
}

TEST_F(SidechainsEventsTestSuite, UndoPureBwtCertUpdatesToCeasingScs) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);


    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"),
        /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/3);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));

    //test
    view->RestoreSidechain(cert, blockUndo.scUndoDatabyScId.at(scId));
    view->CancelSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);

    EXPECT_FALSE(view->HaveSidechainEvents(newCeasingHeight));
    CSidechainEvents restoredCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);

    //check idempotency
    EXPECT_TRUE(view->CancelSidechainEvent(cert));
}

TEST_F(SidechainsEventsTestSuite, UndoNoBwtCertUpdatesToCeasingScs) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);


    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"),
        /*changeTotalAmount*/CAmount(4),/*numChangeOut*/4, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));

    //test
    view->RestoreSidechain(cert, blockUndo.scUndoDatabyScId.at(scId));
    view->CancelSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);

    EXPECT_FALSE(view->HaveSidechainEvents(newCeasingHeight));
    CSidechainEvents restoredCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);

    //check idempotency
    EXPECT_TRUE(view->CancelSidechainEvent(cert));
}

TEST_F(SidechainsEventsTestSuite, UndoEmptyCertUpdatesToCeasingScs) {
    //Create and register sidechain
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock creationBlock;
    view->UpdateSidechain(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->ScheduleSidechainEvent(scCreationOut, creationHeight);


    CSidechain sidechain;
    ASSERT_TRUE(view->GetSidechain(scId, sidechain));
    int currentEpoch = sidechain.EpochFor(creationHeight);
    int initialCeasingHeight = sidechain.StartHeightForEpoch(currentEpoch+1)+sidechain.SafeguardMargin() +1;
    CSidechainEvents initialCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"),
        /*changeTotalAmount*/CAmount(0),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateSidechain(cert, blockUndo);
    view->ScheduleSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);
    int newCeasingHeight = sidechain.StartHeightForEpoch(cert.epochNumber+2)+sidechain.SafeguardMargin() +1;
    CSidechainEvents updatedCeasingScIds;
    ASSERT_TRUE(view->GetSidechainEvents(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveSidechainEvents(initialCeasingHeight));

    //test
    view->RestoreSidechain(cert, blockUndo.scUndoDatabyScId.at(scId));
    view->CancelSidechainEvent(cert);

    //Checks
    view->GetSidechain(scId, sidechain);

    EXPECT_FALSE(view->HaveSidechainEvents(newCeasingHeight));
    CSidechainEvents restoredCeasingScIds;
    EXPECT_TRUE(view->GetSidechainEvents(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);

    //check idempotency
    EXPECT_TRUE(view->CancelSidechainEvent(cert));
}
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// ApplyTxInUndo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendChangeOutput)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the change output from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendBwtOutput)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending only the bwt from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 1;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendFullCoinsByChangeOutput)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4), /*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the change output (the only output) from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert)<<coinFromCert.ToString() + reconstructedCoinFromCert.ToString();
}

TEST_F(SidechainsEventsTestSuite, Cert_CoinReconstructionFromBlockUndo_SpendFullCoinsByBwt)
{
    //Create sidechain
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256& scId = scCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    uint256 inputCoinHash = txCreationUtils::CreateSpendableCoinAtHeight(*view, 100);
    CBlock dummyBlock;
    CMutableScCertificate mutCert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, dummyBlock.GetHash(),
        /*changeTotalAmount*/CAmount(4), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    mutCert.vin.clear();
    mutCert.vin.push_back(CTxIn(inputCoinHash, 0));
    CScCertificate cert(mutCert);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight, /*isBlockTopQualityCert*/true);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the bwt (the only output) from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(SidechainsEventsTestSuite, TransparentTx_CoinReconstructionFromBlockUndo_SpendNonFinalOutput)
{
    //Generate transparent transaction and the associated coin to be spent
    CMutableTransaction txToBeSpent;
    txToBeSpent.addOut(CTxOut(CAmount(10),CScript(OP_TRUE))); //a dummy code to ensures the correct serialization
    txToBeSpent.addOut(CTxOut(CAmount(20),CScript(OP_TRUE))); //a dummy code to ensures the correct serialization

    CTxUndo dummyTxUndo;
    int coinHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(txToBeSpent.GetHash()));
    UpdateCoins(txToBeSpent, *view, dummyTxUndo, coinHeight);
    CCoins coinFromTx;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),coinFromTx));

    //Create Tx spending the and output from txToBeSpent. Not a final one
    CMutableTransaction spendingTx;
    spendingTx.vin.resize(1);
    spendingTx.vin.at(0).prevout.hash = txToBeSpent.GetHash();
    spendingTx.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild spent coin output
    CTxUndo txUndo;
    int spendTxHeight = 2020;
    UpdateCoins(spendingTx, *view, txUndo, spendTxHeight);

    //Simulate serialization and deserialization
    CDataStream ssBlockUndo(SER_DISK, 111 /*AVersion*/);

    ssBlockUndo << txUndo;
    CTxUndo retrievedtxUndo;
    ssBlockUndo >> retrievedtxUndo;

    //Test
    for (unsigned int inPos = spendingTx.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = spendingTx.vin[inPos].prevout;
        const CTxInUndo &undo = retrievedtxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoin;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),reconstructedCoin));
    EXPECT_TRUE(coinFromTx == reconstructedCoin)
        <<"\n coinFromTx        "<<coinFromTx.ToString()
        <<"\n reconstructedCoin "<<reconstructedCoin.ToString();
}

TEST_F(SidechainsEventsTestSuite, TransparentTx_CoinReconstructionFromBlockUndo_FullySpendOutput)
{
    //Generate transparent transaction and the associated coin to be spent
    CMutableTransaction txToBeSpent;
    txToBeSpent.addOut(CTxOut(CAmount(10),CScript(OP_TRUE)));  //a dummy code to ensures the correct serialization

    CTxUndo dummyTxUndo;
    int coinHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(txToBeSpent.GetHash()));
    UpdateCoins(txToBeSpent, *view, dummyTxUndo, coinHeight);
    CCoins coinFromTx;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),coinFromTx));

    //Create Tx spending the last output from txToBeSpent
    CMutableTransaction spendingTx;
    spendingTx.vin.resize(1);
    spendingTx.vin.at(0).prevout.hash = txToBeSpent.GetHash();
    spendingTx.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild spent coin output
    CTxUndo txUndo;
    int spendTxHeight = 2020;
    UpdateCoins(spendingTx, *view, txUndo, spendTxHeight);

    //Simulate serialization and deserialization
    CDataStream ssBlockUndo(SER_DISK, 111 /*AVersion*/);

    ssBlockUndo << txUndo;
    CTxUndo retrievedtxUndo;
    ssBlockUndo >> retrievedtxUndo;

    //Test
    for (unsigned int inPos = spendingTx.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = spendingTx.vin[inPos].prevout;
        const CTxInUndo &undo = retrievedtxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoin;
    EXPECT_TRUE(view->GetCoins(txToBeSpent.GetHash(),reconstructedCoin));
    EXPECT_TRUE(coinFromTx == reconstructedCoin)
        <<"\n coinFromTx        "<<coinFromTx.ToString()
        <<"\n reconstructedCoin "<<reconstructedCoin.ToString();
}
///////////////////////////////////////////////////////////////////////////////
/////////////////////////// Mature sidechain balance //////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsEventsTestSuite, UponScCreationMaturingEventForCreationAmountIsScheduled) {
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //test
    EXPECT_TRUE(view->ScheduleSidechainEvent(scCreationTx.GetVscCcOut()[0], scCreationHeight));

    //Checks
    int creationMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(creationMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[creationMaturityHeight] == scCreationTx.GetVscCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, UponFwdMaturingEventForFwdAmountIsScheduled) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    CAmount fwdAmount = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    int fwdHeight = 20;

    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));

    //test
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[0], fwdHeight));

    //Checks
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(fwdMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[fwdMaturityHeight] == fwdTx.GetVftCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, UponMbtrMaturingEventForScFeeIsScheduled) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    CAmount scFee = 2;

    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = scId;
    CMutableTransaction mutTx;
    mutTx.nVersion = SC_TX_VERSION;
    mutTx.vmbtr_out.push_back(mcBwtReq);

    int nHeight = 20;

    EXPECT_TRUE(view->UpdateSidechain(mutTx, dummyBlock, nHeight));

    //test
    EXPECT_TRUE(view->ScheduleSidechainEvent(mutTx.vmbtr_out[0], nHeight));

    //Checks
    int scFeeMaturityHeight = nHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(scFeeMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[scFeeMaturityHeight] == mutTx.vmbtr_out[0].scFee);
}

TEST_F(SidechainsEventsTestSuite, DoubleFwdSchedulingIsDoneCorrectly) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //Create transaction with double fwd Tx
    CAmount fwdAmount1 = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount1);
    CMutableTransaction mutFwdTx(fwdTx);
    CAmount fwdAmount2 = 300;
    mutFwdTx.vft_ccout.push_back(CTxForwardTransferOut(scId, fwdAmount2, uint256S("add")));
    fwdTx = mutFwdTx;

    int fwdHeight = 20;

    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[0], fwdHeight));

    //Test: schedule a second fwd at the same height
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[1], fwdHeight));

    //Checks
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CSidechainEvents scheduledEvent;
    EXPECT_TRUE(view->GetSidechainEvents(fwdMaturityHeight, scheduledEvent));
    EXPECT_TRUE(scheduledEvent.maturingScs.count(scId));

    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts[fwdMaturityHeight] == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
}

TEST_F(SidechainsEventsTestSuite, ScCreationAmountMaturesAtHeight) {
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(scCreationTx.GetVscCcOut()[0], scCreationHeight));

    //test
    int creationMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(creationMaturityHeight, blockUndo, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == scCreationTx.GetVscCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(creationMaturityHeight) == 0);

    EXPECT_TRUE(blockUndo.scUndoDatabyScId.at(scId).appliedMaturedAmount == scCreationTx.GetVscCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, FwdAmountMaturesAtHeight) {
    //Create a sidechain
    CTransaction dummyScCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = dummyScCreationTx.GetScIdFromScCcOut(0);
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(dummyScCreationTx, dummyBlock, /*creationHeight*/5));

    // create a fwd
    CAmount fwdAmount = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[0], fwdHeight));

    //test
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == fwdTx.GetVftCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) == 0);

    EXPECT_TRUE(blockUndo.scUndoDatabyScId.at(scId).appliedMaturedAmount == fwdTx.GetVftCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, DoubleFwdsMatureAtHeight) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));

    //Create transaction with double fwd Tx
    CAmount fwdAmount1 = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount1);
    CMutableTransaction mutFwdTx(fwdTx);
    CAmount fwdAmount2 = 300;
    mutFwdTx.vft_ccout.push_back(CTxForwardTransferOut(scId, fwdAmount2, uint256S("add")));
    fwdTx = mutFwdTx;

    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[0], fwdHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[1], fwdHeight));

    //test
    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) == 0);

    EXPECT_TRUE(blockUndo.scUndoDatabyScId.at(scId).appliedMaturedAmount == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
}

TEST_F(SidechainsEventsTestSuite, CreationAmountDoesNotMatureUponRevertSidechainEvents) {
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(scCreationTx.GetVscCcOut()[0], scCreationHeight));

    int creationMaturityHeight = scCreationHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(creationMaturityHeight, blockUndo, &dummy));

    //test
    EXPECT_TRUE(view->RevertSidechainEvents(blockUndo, creationMaturityHeight, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(creationMaturityHeight) != 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.at(creationMaturityHeight) == scCreationTx.GetVscCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, FwdAmountsDoNotMatureUponRevertSidechainEvents) {
    //Create and mature fwd amount
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(10));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    int scCreationHeight = 5;
    CBlock dummyBlock;
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(scCreationTx.GetVscCcOut()[0], scCreationHeight));

    CBlockUndo dummyBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(scCreationHeight + Params().ScCoinsMaturity(), dummyBlockUndo, &dummy));

    // create and mature a fwd
    CAmount fwdAmount = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount);
    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[0], fwdHeight));

    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo;
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //test
    EXPECT_TRUE(view->RevertSidechainEvents(blockUndo, fwdMaturityHeight, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == scCreationTx.GetVscCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) != 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.at(fwdMaturityHeight) == fwdTx.GetVftCcOut()[0].nValue);
}

TEST_F(SidechainsEventsTestSuite, DoubleFwdsDoNotMatureUponRevertSidechainEvents) {
    int scCreationHeight = 5;
    CBlock dummyBlock;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(CAmount(1));
    const uint256 & scId = scCreationTx.GetScIdFromScCcOut(0);
    EXPECT_TRUE(view->UpdateSidechain(scCreationTx, dummyBlock, scCreationHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(scCreationTx.GetVscCcOut()[0], scCreationHeight));

    CBlockUndo dummyBlockUndo;
    std::vector<CScCertificateStatusUpdateInfo> dummy;
    EXPECT_TRUE(view->HandleSidechainEvents(scCreationHeight + Params().ScCoinsMaturity(), dummyBlockUndo, &dummy));

    //Create transaction with double fwd Tx
    CAmount fwdAmount1 = 200;
    CTransaction fwdTx = txCreationUtils::createFwdTransferTxWith(scId, fwdAmount1);
    CMutableTransaction mutFwdTx(fwdTx);
    CAmount fwdAmount2 = 300;
    mutFwdTx.vft_ccout.push_back(CTxForwardTransferOut(scId, fwdAmount2, uint256S("add")));
    fwdTx = mutFwdTx;

    int fwdHeight = 20;
    EXPECT_TRUE(view->UpdateSidechain(fwdTx, dummyBlock, fwdHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[0], fwdHeight));
    EXPECT_TRUE(view->ScheduleSidechainEvent(fwdTx.GetVftCcOut()[1], fwdHeight));

    int fwdMaturityHeight = fwdHeight + Params().ScCoinsMaturity();
    CBlockUndo blockUndo;
    EXPECT_TRUE(view->HandleSidechainEvents(fwdMaturityHeight, blockUndo, &dummy));

    //test
    EXPECT_TRUE(view->RevertSidechainEvents(blockUndo, fwdMaturityHeight, &dummy));

    //Checks
    CSidechain sidechain;
    EXPECT_TRUE(view->GetSidechain(scId, sidechain));
    EXPECT_TRUE(sidechain.balance == scCreationTx.GetVscCcOut()[0].nValue);
    EXPECT_TRUE(sidechain.mImmatureAmounts.count(fwdMaturityHeight) != 0);
    EXPECT_TRUE(sidechain.mImmatureAmounts.at(fwdMaturityHeight) == fwdTx.GetVftCcOut()[0].nValue + fwdTx.GetVftCcOut()[1].nValue);
}
