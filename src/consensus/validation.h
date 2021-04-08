// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_VALIDATION_H
#define BITCOIN_CONSENSUS_VALIDATION_H

#include <string>

/** "reject" message codes */
static const unsigned char VALIDATION_OK = 0x00;

static const unsigned char REJECT_MALFORMED = 0x01;
static const unsigned char REJECT_INVALID = 0x10;
static const unsigned char REJECT_OBSOLETE = 0x11;
static const unsigned char REJECT_DUPLICATE = 0x12;
static const unsigned char REJECT_NONSTANDARD = 0x40;
static const unsigned char REJECT_DUST = 0x41;
static const unsigned char REJECT_INSUFFICIENTFEE = 0x42;
static const unsigned char REJECT_CHECKPOINT = 0x43;
static const unsigned char REJECT_CHECKBLOCKATHEIGHT_NOT_FOUND = 0x44;
static const unsigned char REJECT_SCID_NOT_FOUND = 0x45;
static const unsigned char REJECT_INSUFFICIENT_SCID_FUNDS = 0x46;
static const unsigned char REJECT_ABSURDLY_HIGH_FEE = 0x47;
static const unsigned char REJECT_HAS_CONFLICTS = 0x48;
static const unsigned char REJECT_NO_COINS_FOR_INPUT = 0x49;
static const unsigned char REJECT_PROOF_VER_FAILED = 0x4a;
static const unsigned char REJECT_SC_CUM_COMM_TREE = 0x4b;

/** Capture information about block/transaction validation */
class CValidationState {
private:
    enum mode_state {
        MODE_VALID,   //! everything ok
        MODE_INVALID, //! network rule violation (DoS value may be set)
        MODE_ERROR,   //! run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    unsigned char chRejectCode;
    bool corruptionPossible;
public:
    CValidationState() : mode(MODE_VALID), nDoS(0), chRejectCode(VALIDATION_OK), corruptionPossible(false) {}
    virtual bool DoS(int level, bool ret = false,
             unsigned char chRejectCodeIn=0, std::string strRejectReasonIn="",
             bool corruptionIn=false) {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        corruptionPossible = corruptionIn;
        if (mode == MODE_ERROR)
            return ret;
        nDoS += level;
        mode = MODE_INVALID;
        return ret;
    }
    virtual bool Invalid(bool ret = false,
                 unsigned char _chRejectCode=VALIDATION_OK, std::string _strRejectReason="") {
        return DoS(0, ret, _chRejectCode, _strRejectReason);
    }
    virtual bool Error(const std::string& strRejectReasonIn) {
        if (mode == MODE_VALID)
            strRejectReason = strRejectReasonIn;
        mode = MODE_ERROR;
        return false;
    }
    virtual bool IsValid() const {
        return mode == MODE_VALID;
    }
    virtual bool IsInvalid() const {
        return mode == MODE_INVALID;
    }
    virtual bool IsError() const {
        return mode == MODE_ERROR;
    }
    virtual bool IsInvalid(int &nDoSOut) const {
        if (IsInvalid()) {
            nDoSOut = nDoS;
            return true;
        }
        return false;
    }
    virtual bool CorruptionPossible() const {
        return corruptionPossible;
    }
    virtual unsigned char GetRejectCode() const { return chRejectCode; }
    virtual std::string GetRejectReason() const { return strRejectReason; }
};

#endif // BITCOIN_CONSENSUS_VALIDATION_H
