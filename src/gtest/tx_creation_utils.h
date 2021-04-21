#ifndef TX_CREATION_UTILS_H
#define TX_CREATION_UTILS_H

#include <amount.h>
#include <coins.h>

class CTransaction;
class CMutableTransaction;
class CScCertificate;
class CMutableScCertificate;
class uint256;
class CBlock;

namespace txCreationUtils
{
CMutableTransaction populateTx(int txVersion,
                               const CAmount & creationTxAmount = CAmount(0),
                               int epochLength = 5,
                               const CAmount& ftScFee = CAmount(0),
                               const CAmount& mbtrScFee = CAmount(0),
                               int mbtrDataLength = 0);
void signTx(CMutableTransaction& mtx);
void signTx(CMutableScCertificate& mcert);

CTransaction createNewSidechainTxWith(const CAmount & creationTxAmount, int epochLength = 15);
CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);
CTxCeasedSidechainWithdrawalInput CreateCSWInput(const uint256& scId, const std::string& nullifierHex, CAmount amount, int32_t idx);
CTransaction createCSWTxWith(const CTxCeasedSidechainWithdrawalInput& csw);

CTransaction createCoinBase(const CAmount& amount);
CTransaction createTransparentTx(bool ccIsNull = true); //ccIsNull = false allows generation of faulty tx with non-empty cross chain output
CTransaction createSproutTx(bool ccIsNull = true); //ccIsNull = false allows generation of faulty tx with non-empty cross chain output

void addNewScCreationToTx(CTransaction & tx, const CAmount & scAmount);

CScCertificate createCertificate(const uint256 & scId, int epochNum, const uint256 & endEpochBlockHash,
                                 const CFieldElement& endEpochCumScTxCommTreeRoot,
                                 CAmount changeTotalAmount, unsigned int numChangeOut,
                                 CAmount bwtTotalAmount, unsigned int numBwt,
                                 CAmount ftScFee, CAmount mbtrScFee, const int quality = 3);

uint256 CreateSpendableCoinAtHeight(CCoinsViewCache& targetView, unsigned int coinHeight);

void storeSidechain(CSidechainsMap& mapToWriteInto, const uint256& scId, const CSidechain& sidechain);
void storeSidechainEvent(CSidechainEventsMap& mapToWriteInto, int eventHeight, const CSidechainEvents& scEvent);
} // end of namespace

namespace chainSettingUtils
{
    void ExtendChainActiveToHeight(int targetHeight);
    void ExtendChainActiveWithBlock(const CBlock& block);
} // end of namespace

#endif
