#include "asyncproofverifier.h"

#include "coins.h"
#include "init.h"
#include "main.h"
#include "util.h"
#include "primitives/certificate.h"

const uint32_t CScAsyncProofVerifier::BATCH_VERIFICATION_MAX_DELAY = 5000;   /**< The maximum delay in milliseconds between batch verification requests */
const uint32_t CScAsyncProofVerifier::BATCH_VERIFICATION_MAX_SIZE = 10;      /**< The threshold size of the proof queue that triggers a call to the batch verification. */


#ifndef BITCOIN_TX
void CScAsyncProofVerifier::LoadDataForCertVerification(const CCoinsViewCache& view, const CScCertificate& scCert, CNode* pfrom)
{
    LOCK(cs_asyncQueue);
    CScProofVerifier::LoadDataForCertVerification(view, scCert, pfrom);
}

void CScAsyncProofVerifier::LoadDataForCswVerification(const CCoinsViewCache& view, const CTransaction& scTx, CNode* pfrom)
{
    LOCK(cs_asyncQueue);
    CScProofVerifier::LoadDataForCswVerification(view, scTx, pfrom);
}
#endif

uint32_t CScAsyncProofVerifier::GetCustomMaxBatchVerifyDelay()
{
    int32_t delay = GetArg("-scproofverificationdelay", BATCH_VERIFICATION_MAX_DELAY);
    if (delay < 0)
    {
        LogPrintf("%s():%d - ERROR: scproofverificationdelay=%d, must be non negative, setting to default value = %d\n",
            __func__, __LINE__, delay, BATCH_VERIFICATION_MAX_DELAY);
        delay = BATCH_VERIFICATION_MAX_DELAY;
    }
    return static_cast<uint32_t>(delay);
}

uint32_t CScAsyncProofVerifier::GetCustomMaxBatchVerifyMaxSize()
{
    int32_t size = GetArg("-scproofqueuesize", BATCH_VERIFICATION_MAX_SIZE);
    if (size < 0)
    {
        LogPrintf("%s():%d - ERROR: scproofqueuesize=%d, must be non negative, setting to default value = %d\n",
            __func__, __LINE__, size, BATCH_VERIFICATION_MAX_SIZE);
        size = BATCH_VERIFICATION_MAX_SIZE;
    }
    return static_cast<uint32_t>(size);
}

void CScAsyncProofVerifier::RunPeriodicVerification()
{
    /**
     * The age of the queue in milliseconds.
     * This value represents the time spent in the queue by the oldest proof in the queue.
     */
    uint32_t queueAge = 0;

    uint32_t batchVerificationMaxDelay = GetCustomMaxBatchVerifyDelay();
    uint32_t batchVerificationMaxSize  = GetCustomMaxBatchVerifyMaxSize();


    while (!ShutdownRequested())
    {
        size_t currentQueueSize = certEnqueuedData.size() + cswEnqueuedData.size();

        if (currentQueueSize > 0)
        {
            queueAge += THREAD_WAKE_UP_PERIOD;

            /**
             * The batch verification can be triggered by two events:
             * 
             * 1. The queue has grown up beyond the threshold size;
             * 2. The oldest proof in the queue has waited for too long.
             */
            if (queueAge > batchVerificationMaxDelay || currentQueueSize > batchVerificationMaxSize)
            {
                queueAge = 0;
                std::map</*scTxHash*/uint256, std::vector<CCswProofVerifierItem>> tempCswData;
                std::map</*certHash*/uint256, std::vector<CCertProofVerifierItem>> tempCertData;

                {
                    LOCK(cs_asyncQueue);

                    size_t cswQueueSize = cswEnqueuedData.size();
                    size_t certQueueSize = certEnqueuedData.size();

                    LogPrint("cert", "%s():%d - Async verification triggered, %d certificates and %d CSW inputs to verify \n",
                             __func__, __LINE__, certQueueSize, cswQueueSize);

                    // Move the queued proofs into local maps, so that we can release the lock
                    tempCswData = std::move(cswEnqueuedData);
                    tempCertData = std::move(certEnqueuedData);

                    assert(cswEnqueuedData.size() == 0);
                    assert(certEnqueuedData.size() == 0);
                    assert(tempCswData.size() == cswQueueSize);
                    assert(tempCertData.size() == certQueueSize);
                }

                std::pair<bool, std::map<uint256, ProofVerifierOutput>> batchResult = BatchVerifyInternal(tempCswData, tempCertData);
                std::map<uint256, ProofVerifierOutput> outputs = batchResult.second;

                if (!batchResult.first)
                {
                    LogPrint("cert", "%s():%d - Batch verification failed, proceeding one by one... \n",
                             __func__, __LINE__);

                    // If the batch verification fails, check the proofs one by one
                    //outputs = NormalVerify(tempCswData, tempCertData);
                }

                // Post processing of proofs
                for (auto entry : outputs)
                {
                    ProofVerifierOutput output = entry.second;

                    // At the very end of the batch verification (after any eventual retry) the result of each proof must be
                    // FAILED or PASSED, UNKNOWN is not admitted.
                    assert(output.proofResult == ProofVerificationResult::Failed || output.proofResult == ProofVerificationResult::Passed);

                    LogPrint("cert", "%s():%d - Post processing certificate or transaction [%s] from node [%d], result [%d] \n",
                             __func__, __LINE__, output.tx->GetHash().ToString(), output.node->GetId(), output.proofResult);

                    // CODE USED FOR UNIT TEST ONLY [Start]
                    if (BOOST_UNLIKELY(Params().NetworkIDString() == "regtest"))
                    {
                        UpdateStatistics(output); // Update the statistics

                        // Check if the AcceptToMemoryPool has to be skipped.
                        if (skipAcceptToMemoryPool)
                        {
                            continue;
                        }
                    }
                    // CODE USED FOR UNIT TEST ONLY [End]

                    CValidationState dummyState;
                    ProcessTxBaseAcceptToMemoryPool(*output.tx.get(), output.node,
                                                    output.proofResult == ProofVerificationResult::Passed ? BatchVerificationStateFlag::VERIFIED : BatchVerificationStateFlag::FAILED,
                                                    dummyState);
                }
            }
        }

        MilliSleep(THREAD_WAKE_UP_PERIOD);
    }
}

// /**
//  * @brief Run the verification for CSW inputs and certificates one by one (not batched).
//  * 
//  * @param cswInputs The map of CSW inputs data to be verified.
//  * @param certInputs The map of certificates data to be verified.
//  * @return std::vector<AsyncProofVerifierOutput> The result of all processed proofs.
//  */
// std::vector<AsyncProofVerifierOutput> CScAsyncProofVerifier::NormalVerify(const std::map</*scTxHash*/uint256,
//                                                                           std::map</*outputPos*/unsigned int, CCswProofVerifierInput>>& cswInputs,
//                                                                           const std::map</*certHash*/uint256, CCertProofVerifierInput>& certInputs) const
// {
//     std::vector<AsyncProofVerifierOutput> outputs;

//     for (const auto& verifierInput : cswInputs)
//     {
//         bool res = NormalVerifyCsw(verifierInput.first, verifierInput.second);

//         outputs.push_back(AsyncProofVerifierOutput{ .tx = verifierInput.second.begin()->second.transactionPtr,
//                                                     .node = verifierInput.second.begin()->second.node,
//                                                     .proofVerified = res });
//     }

//     for (const auto& verifierInput : certInputs)
//     {
//         bool res = NormalVerifyCertificate(verifierInput.second);

//         outputs.push_back(AsyncProofVerifierOutput{ .tx = verifierInput.second.certificatePtr,
//                                                     .node = verifierInput.second.node,
//                                                     .proofVerified = res });
//     }

//     return outputs;
// }

// /**
//  * @brief Run the normal verification for a certificate.
//  * This is equivalent to running the batch verification with a single input.
//  * 
//  * @param input Data of the certificate to be verified.
//  * @return true If the certificate proof is correctly verified
//  * @return false If the certificate proof is rejected
//  */
// bool CScAsyncProofVerifier::NormalVerifyCertificate(CCertProofVerifierInput input) const
// {
//     CctpErrorCode code;

//     int custom_fields_len = input.vCustomFields.size(); 

//     std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
//     int i = 0;
//     std::vector<wrappedFieldPtr> vSptr;
//     for (auto entry: input.vCustomFields)
//     {
//         wrappedFieldPtr sptrFe = entry.GetFieldElement();
//         custom_fields[i] = sptrFe.get();
//         vSptr.push_back(sptrFe);
//         i++;
//     }

//     const backward_transfer_t* bt_list_ptr = input.bt_list.data();
//     int bt_list_len = input.bt_list.size();

//     // mc crypto lib wants a null ptr if we have no elements
//     if (custom_fields_len == 0)
//         custom_fields.reset();

//     if (bt_list_len == 0)
//         bt_list_ptr = nullptr;

//     wrappedFieldPtr   sptrConst  = input.constant.GetFieldElement();
//     wrappedFieldPtr   sptrCum    = input.endEpochCumScTxCommTreeRoot.GetFieldElement();
//     wrappedScProofPtr sptrProof  = input.certProof.GetProofPtr();
//     wrappedScVkeyPtr  sptrCertVk = input.CertVk.GetVKeyPtr();

//     bool ret = zendoo_verify_certificate_proof(
//         sptrConst.get(),
//         input.epochNumber,
//         input.quality,
//         bt_list_ptr,
//         bt_list_len,
//         custom_fields.get(),
//         custom_fields_len,
//         sptrCum.get(),
//         input.mainchainBackwardTransferRequestScFee,
//         input.forwardTransferScFee,
//         sptrProof.get(),
//         sptrCertVk.get(),
//         &code
//     );

//     if (!ret || code != CctpErrorCode::OK)
//     {
//         LogPrintf("ERROR: %s():%d - cert [%s] has proof which does not verify: code [0x%x]\n",
//             __func__, __LINE__, input.certHash.ToString(), code);
//     }

//     return ret;
// }

// /**
//  * @brief Run the normal verification for the CSW inputs of a sidechain transaction.
//  * This is equivalent to running the batch verification with a single input.
//  * 
//  * @param txHash The sidechain transaction hash.
//  * @param inputMap The map of CSW input data to be verified.
//  * @return true If the CSW proof is correctly verified
//  * @return false If the CSW proof is rejected
//  */
// bool CScAsyncProofVerifier::NormalVerifyCsw(uint256 txHash, std::map</*outputPos*/unsigned int, CCswProofVerifierInput> inputMap) const
// {
//     for (const auto& entry : inputMap)
//     {
//         const CCswProofVerifierInput& input = entry.second;
    
//         wrappedFieldPtr sptrScId = CFieldElement(input.scId).GetFieldElement();
//         field_t* scid_fe = sptrScId.get();
 
//         const uint160& csw_pk_hash = input.pubKeyHash;
//         BufferWithSize bws_csw_pk_hash(csw_pk_hash.begin(), csw_pk_hash.size());
     
//         wrappedFieldPtr   sptrCdh       = input.certDataHash.GetFieldElement();
//         wrappedFieldPtr   sptrCum       = input.ceasingCumScTxCommTree.GetFieldElement();
//         wrappedFieldPtr   sptrNullifier = input.nullifier.GetFieldElement();
//         wrappedScProofPtr sptrProof     = input.cswProof.GetProofPtr();
//         wrappedScVkeyPtr  sptrCeasedVk  = input.ceasedVk.GetVKeyPtr();

//         CctpErrorCode code;
//         bool ret = zendoo_verify_csw_proof(
//                     input.nValue,
//                     scid_fe, 
//                     sptrNullifier.get(),
//                     &bws_csw_pk_hash,
//                     sptrCdh.get(),
//                     sptrCum.get(),
//                     sptrProof.get(),
//                     sptrCeasedVk.get(),
//                     &code);

//         if (!ret || code != CctpErrorCode::OK)
//         {
//             LogPrintf("ERROR: %s():%d - tx [%s] has csw proof which does not verify: ret[%d], code [0x%x]\n",
//                 __func__, __LINE__, input.transactionPtr->GetHash().ToString(), (int)ret, code);
//             return false;
//         }
//     }
//     return true;
// }

/**
 * @brief Update the statistics of the proof verifier.
 * It must be used in regression test mode only.
 * @param output The result of the proof verification that has been performed.
 */
void CScAsyncProofVerifier::UpdateStatistics(const ProofVerifierOutput& output)
{
    assert(Params().NetworkIDString() == "regtest");

    if (output.tx->IsCertificate())
    {
        if (output.proofResult == ProofVerificationResult::Passed)
        {
            stats.okCertCounter++;
        }
        else if (output.proofResult == ProofVerificationResult::Failed)
        {
            stats.failedCertCounter++;
        }
    }
    else
    {
        if (output.proofResult == ProofVerificationResult::Passed)
        {
            stats.okCswCounter++;
        }
        else if (output.proofResult == ProofVerificationResult::Failed)
        {
            stats.failedCswCounter++;
        }
    }
}
