#ifndef _CERTIFICATE_H
#define _CERTIFICATE_H

#include "transaction.h"
#include "policy/fees.h"

struct CMutableScCertificate;
#if NO2
struct CTransactionNetworkObj2;
#endif

class CScCertificate : virtual public CTransactionBase
{
    /** Memory only. */
    void UpdateHash() const override;

public:
    static const int32_t EPOCH_NULL = -1;
    static const int32_t EPOCH_NOT_INITIALIZED = -2;

private:
    const uint256 scId;

public:
    const int32_t epochNumber;
    const uint256 endEpochBlockHash;
    const CAmount totalAmount;
    const CAmount fee;
    const uint256 nonce;

    /** Construct a CScCertificate that qualifies as IsNull() */
    CScCertificate();

    /** Convert a CMutableScCertificate into a CScCertificate.  */
    CScCertificate(const CMutableScCertificate &tx);

#if NO2
    CScCertificate(const CTransactionNetworkObj2 &tno);
#endif

    CScCertificate& operator=(const CScCertificate& tx);
    CScCertificate(const CScCertificate& tx);

    friend bool operator==(const CScCertificate& a, const CScCertificate& b)
    {
        return a.hash == b.hash;
    }

    friend bool operator!=(const CScCertificate& a, const CScCertificate& b)
    {
        return a.hash != b.hash;
    }

    const uint256& GetHash() const { return hash; }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*const_cast<int32_t*>(&this->nVersion));
        READWRITE(*const_cast<uint256*>(&scId));
        READWRITE(*const_cast<int32_t*>(&epochNumber));
        READWRITE(*const_cast<uint256*>(&endEpochBlockHash));
        READWRITE(*const_cast<CAmount*>(&totalAmount));
        READWRITE(*const_cast<CAmount*>(&fee));

        if (ser_action.ForRead())
        {
            std::vector<CBackwardTransferOut> vbt_ccout_ser;

            // reading from data stream to memory
            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout));
            READWRITE(*const_cast<std::vector<CBackwardTransferOut>*>(&vbt_ccout_ser));

            for (auto& btout : vbt_ccout_ser)
            {
                CTxOut out(btout);
                (*const_cast<std::vector<CTxOut>*>(&vout)).push_back(out);
            }
        }
        else
        {
            std::vector<CBackwardTransferOut> vbt_ccout_ser;
            // we must not modify vout
            std::vector<CTxOut> vout_ser;

            // reading from memory and writing to data stream
            for (auto it = vout.begin(); it != vout.end(); ++it)
            {
                if ((*it).isFromBackwardTransfer)
                {
                    CBackwardTransferOut btout((*it));
                    vbt_ccout_ser.push_back(btout);
                }
                else
                {
                    vout_ser.push_back(*it);
                }
            }
            READWRITE(*const_cast<std::vector<CTxOut>*>(&vout_ser));
            READWRITE(*const_cast<std::vector<CBackwardTransferOut>*>(&vbt_ccout_ser));
        }

        READWRITE(*const_cast<uint256*>(&nonce));
        if (ser_action.ForRead())
            UpdateHash();
    }

    template <typename Stream>
    CScCertificate(deserialize_type, Stream& s) : CScCertificate(CMutableScCertificate(deserialize, s)) {}

    bool IsCertificateVersion() const override
    {
        // so far just one version
        return (nVersion == SC_CERT_VERSION);
    }

    //GETTERS
    const std::vector<CTxIn>&         GetVin()        const override {static const std::vector<CTxIn> noInputs; return noInputs;};
    const std::vector<CTxOut>&        GetVout()       const override {return vout;};
    const std::vector<JSDescription>& GetVjoinsplit() const override {static const std::vector<JSDescription> noJs; return noJs;};
    const uint256&                    GetScId()       const override {return scId;};
    //END OF GETTERS

    //CHECK FUNCTIONS
    bool CheckVersionBasic        (CValidationState &state) const override;
    bool CheckVersionIsStandard   (std::string& reason, int nHeight) const override;
    bool CheckInputsAvailability  (CValidationState &state) const override;
    bool CheckOutputsAvailability (CValidationState &state) const override;
    bool CheckSerializedSize      (CValidationState &state) const override;
    bool CheckFeeAmount(const CAmount& totalVinAmount, CValidationState& state) const override;
    //END OF CHECK FUNCTIONS


    bool IsNull() const override {
        return (
            scId.IsNull() &&
            epochNumber == EPOCH_NULL &&
            endEpochBlockHash.IsNull() &&
            totalAmount == 0 &&
            fee == 0 &&
            vout.empty() &&
            nonce.IsNull() );
    }

    CAmount GetFeeAmount(CAmount valueIn) const override;

    unsigned int CalculateSize() const override;
    unsigned int CalculateModifiedSize(unsigned int /* unused nTxSize*/) const override;

    std::string EncodeHex() const override;
    std::string ToString() const override;

    void addToScCommitment(std::map<uint256, uint256>& mLeaves, std::set<uint256>& sScIds) const;
    CAmount GetValueOfBackwardTransfers() const;
    int GetNumbOfBackwardTransfers() const;

    void AddToBlock(CBlock* pblock) const override; 
    void AddToBlockTemplate(CBlockTemplate* pblocktemplate, CAmount fee, unsigned int /* not used sigops */) const override;

    bool ContextualCheck(CValidationState& state, int nHeight, int dosLevel) const override;
    bool CheckFinal(int flags) const override;
    bool IsApplicableToState(CValidationState& state, int nHeight = -1) const override;

    bool TryPushToMempool(bool fLimitFree, bool fRejectAbsurdFee) override final;

    double GetPriority(const CCoinsViewCache &view, int nHeight) const override;

    bool IsCertificate() const override { return true; }
};

/** A mutable version of CScCertificate. */
struct CMutableScCertificate : public CMutableTransactionBase
{
    uint256 scId;
    int32_t epochNumber;
    uint256 endEpochBlockHash;
    CAmount totalAmount;
    CAmount fee;
    uint256 nonce;

    CMutableScCertificate();
    CMutableScCertificate(const CScCertificate& tx);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(this->nVersion);
        READWRITE(scId);
        READWRITE(epochNumber);
        READWRITE(endEpochBlockHash);
        READWRITE(totalAmount);
        READWRITE(fee);
        READWRITE(vout);
        READWRITE(nonce);
    }

    template <typename Stream>
    CMutableScCertificate(deserialize_type, Stream& s) :
    scId(), epochNumber(CScCertificate::EPOCH_NULL), endEpochBlockHash(), totalAmount(), fee(), nonce() {
        Unserialize(s);
    }

    /** Compute the hash of this CMutableScCertificate. This is computed on the
     * fly, as opposed to GetHash() in CScCertificate, which uses a cached result.
     */
    uint256 GetHash() const override;

    bool add(const CTxOut& out)
    { 
        vout.push_back(out);
        return true;
    }

};

#endif // _CERTIFICATE_H
