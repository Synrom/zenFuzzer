// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "core_io.h"
#include "init.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include "walletdb.h"
#include "primitives/transaction.h"
#include "zcbenchmarks.h"
#include "script/interpreter.h"

#include "utiltime.h"
#include "asyncrpcoperation.h"
#include "asyncrpcqueue.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "wallet/asyncrpcoperation_shieldcoinbase.h"

#include "sodium.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

#include <numeric>

#include "sc/sidechainrpc.h"

using namespace std;

using namespace libzcash;
using namespace Sidechain;

extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

int64_t nWalletUnlockTime;
static CCriticalSection cs_nWalletUnlockTime;

// transaction.h comment: spending taddr output requires CTxIn >= 148 bytes and typical taddr txout is 34 bytes
#define CTXIN_SPEND_DUST_SIZE   148
#define CTXOUT_REGULAR_SIZE     34


// Private method:
UniValue z_getoperationstatus_IMPL(const UniValue&, bool);

std::string HelpRequiringPassphrase()
{
    return pwalletMain && pwalletMain->IsCrypted()
        ? "\nRequires wallet passphrase to be set with walletpassphrase call."
        : "";
}

bool EnsureWalletIsAvailable(bool avoidException)
{
    if (!pwalletMain)
    {
        if (!avoidException)
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
        else
            return false;
    }
    return true;
}

void EnsureWalletIsUnlocked()
{
    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
}

void TxExpandedToJSON(const CWalletTransactionBase& tx, const std::vector<CWalletTransactionBase*>& vtxIn, UniValue& entry)
{
    entry.push_back(Pair("txid", tx.getTxBase()->GetHash().GetHex()));
    entry.push_back(Pair("version", tx.getTxBase()->nVersion));

    tx.AddVinExpandedToJSON(entry, vtxIn);

    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.getTxBase()->GetVout().size(); i++) {
        const CTxOut& txout = tx.getTxBase()->GetVout()[i];
        UniValue out(UniValue::VOBJ);
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        out.push_back(Pair("valueZat", txout.nValue));
        out.push_back(Pair("n", (int64_t)i));
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.push_back(Pair("scriptPubKey", o));
        if (txout.isFromBackwardTransfer)
            out.push_back(Pair("backwardTransfer", true));
        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    tx.getTxBase()->AddSidechainOutsToJSON(entry);
    tx.getTxBase()->AddJoinSplitToJSON(entry);

    if (!tx.hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", tx.hashBlock.GetHex()));
        BlockMap::iterator mi = mapBlockIndex.find(tx.hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                entry.push_back(Pair("confirmations", 1 + chainActive.Height() - pindex->nHeight));
                entry.push_back(Pair("time", pindex->GetBlockTime()));
                entry.push_back(Pair("blocktime", pindex->GetBlockTime()));
            }
            else
                entry.push_back(Pair("confirmations", 0));
        }
    }
    else
    {
        entry.push_back(Pair("confirmations", 0));
        entry.push_back(Pair("time", tx.GetTxTime()));
    }

    if (tx.IsFromMe(ISMINE_ALL))
    {
        CAmount nDebit = tx.GetDebit(ISMINE_ALL);
        // with positive sign
        //CAmount nFee = nDebit - nOut;
        CAmount nFee = tx.getTxBase()->GetFeeAmount(nDebit);
        entry.push_back(Pair("fees", ValueFromAmount(nFee)));
    }
}

#if 0
void WalletTxToJSON(const CWalletTx& wtx, UniValue& entry, isminefilter filter)
#else
void WalletTxToJSON(const CWalletTransactionBase& wtx, UniValue& entry, isminefilter filter)
#endif
{
    int confirms = wtx.GetDepthInMainChain();
    entry.push_back(Pair("confirmations", confirms));
    if (wtx.getTxBase()->IsCoinBase())
        entry.push_back(Pair("generated", true));
    if (confirms > 0)
    {
        entry.push_back(Pair("blockhash", wtx.hashBlock.GetHex()));
        entry.push_back(Pair("blockindex", wtx.nIndex));
        entry.push_back(Pair("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime()));
    }
    uint256 hash = wtx.getTxBase()->GetHash();
    entry.push_back(Pair("txid", hash.GetHex()));
    UniValue conflicts(UniValue::VARR);
    BOOST_FOREACH(const uint256& conflict, wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.push_back(Pair("walletconflicts", conflicts));
    entry.push_back(Pair("time", wtx.GetTxTime()));
    entry.push_back(Pair("timereceived", (int64_t)wtx.nTimeReceived));
    BOOST_FOREACH(const PAIRTYPE(string,string)& item, wtx.mapValue)
        entry.push_back(Pair(item.first, item.second));

    // add the cross chain outputs if any
#if 0
    Sidechain::AddSidechainOutsToJSON(wtx, entry);
    entry.push_back(Pair("vjoinsplit", TxJoinSplitToJSON(wtx)));
#else
    wtx.getTxBase()->AddSidechainOutsToJSON(entry);
    wtx.getTxBase()->AddJoinSplitToJSON(entry);
#endif
}

string AccountFromValue(const UniValue& value)
{
    string strAccount = value.get_str();
    if (strAccount != "")
        throw JSONRPCError(RPC_WALLET_ACCOUNTS_UNSUPPORTED, "Accounts are unsupported");
    return strAccount;
}

UniValue getnewaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getnewaddress ( \"account\" , (retpubkeyhash))\n"
            "\nReturns a new Horizen address for receiving payments.\n"
            "\nArguments:\n"
            "1. \"account\"        (string, optional) DEPRECATED. If provided, it MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "2. retpubkeyhash    (boolean, optional) If provided the command will output the public key hash corresponding to the address.\n"
            "\nResult, one of these:\n"
            "\"horizenaddress\"    (string) The new Horizen address (default)\n"
            "\"public key hash\"   (string) If retpubkeyhash==true, the public key hash (20 Bytes) corresponding to a new Horizen address (not shown)\n"
            "\nExamples:\n"
            + HelpExampleCli("getnewaddress", "")
            + HelpExampleCli("getnewaddress \"\"", "true")
            + HelpExampleRpc("getnewaddress", "")
        );
    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    string strAccount;
    bool retPkh = false;

    if (params.size() > 0)
        strAccount = AccountFromValue(params[0]);

    if (params.size() == 2 )
        retPkh = params[1].get_bool();

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwalletMain->GetKeyFromPool(newKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();

    pwalletMain->SetAddressBook(keyID, strAccount, "receive");

    std::string ret;
    if (retPkh)
    {
        // return the public key hash string
        ret = keyID.ToString();
    }
    else
    {
        // return the taddr string
        ret = CBitcoinAddress(keyID).ToString();
    }
    return ret;
}


CBitcoinAddress GetAccountAddress(string strAccount, bool bForceNew=false)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);

    CAccount account;
    walletdb.ReadAccount(strAccount, account);

    bool bKeyUsed = false;

    // Check if the current key has been used
    if (account.vchPubKey.IsValid())
    {
        /* Get script for addr without OP_CHECKBLOCKATHEIGHT, cause we will use it only for searching */
        CScript scriptPubKey = GetScriptForDestination(account.vchPubKey.GetID(), false);
        for (auto it = pwalletMain->getMapWallet().begin();
             it != pwalletMain->getMapWallet().end() && account.vchPubKey.IsValid();
             ++it)
        {
#if 0
            const CWalletTx& wtx = (*it).second;
#else
            const CWalletTransactionBase& wtx = *((*it).second);
#endif
            BOOST_FOREACH(const CTxOut& txout, wtx.getTxBase()->GetVout())
            {
                /* Check that txout.scriptPubKey starts with scriptPubKey instead of full match,
                 * cause we cant compare OP_CHECKBLOCKATHEIGHT arguments, they are different all the time */
                auto res = std::search(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), scriptPubKey.begin(),
                                       scriptPubKey.end());
                if (res == txout.scriptPubKey.begin())
                    bKeyUsed = true;
            }
        }
    }

    // Generate a new key
    if (!account.vchPubKey.IsValid() || bForceNew || bKeyUsed)
    {
        if (!pwalletMain->GetKeyFromPool(account.vchPubKey))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

        pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, "receive");
        walletdb.WriteAccount(strAccount, account);
    }

    return CBitcoinAddress(account.vchPubKey.GetID());
}

UniValue getaccountaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaccountaddress \"account\"\n"
            "\nDEPRECATED. Returns the current Horizen address for receiving payments to this account.\n"
            "\nArguments:\n"
            "1. \"account\"       (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "\nResult:\n"
            "\"horizenaddress\"   (string) The account Horizen address\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccountaddress", "")
            + HelpExampleCli("getaccountaddress", "\"\"")
            + HelpExampleCli("getaccountaddress", "\"myaccount\"")
            + HelpExampleRpc("getaccountaddress", "\"myaccount\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    string strAccount = AccountFromValue(params[0]);

    UniValue ret(UniValue::VSTR);

    ret = GetAccountAddress(strAccount).ToString();
    return ret;
}


UniValue getrawchangeaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getrawchangeaddress\n"
            "\nReturns a new Horizen address, for receiving change.\n"
            "This is for use with raw transactions, NOT normal use.\n"
            "\nResult:\n"
            "\"address\"    (string) The address\n"
            "\nExamples:\n"
            + HelpExampleCli("getrawchangeaddress", "")
            + HelpExampleRpc("getrawchangeaddress", "")
       );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    CReserveKey reservekey(pwalletMain);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    reservekey.KeepKey();

    CKeyID keyID = vchPubKey.GetID();

    return CBitcoinAddress(keyID).ToString();
}


UniValue setaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "setaccount \"horizenaddress\" \"account\"\n"
            "\nDEPRECATED. Sets the account associated with the given address.\n"
            "\nArguments:\n"
            "1. \"horizenaddress\"  (string, required) The Horizen address to be associated with an account.\n"
            "2. \"account\"         (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "\nExamples:\n"
            + HelpExampleCli("setaccount", "\"t14oHp2v54vfmdgQ3v3SNuQga8JKHTNi2a1\" \"tabby\"")
            + HelpExampleRpc("setaccount", "\"t14oHp2v54vfmdgQ3v3SNuQga8JKHTNi2a1\", \"tabby\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");

    string strAccount;
    if (params.size() > 1)
        strAccount = AccountFromValue(params[1]);

    // Only add the account if the address is yours.
    if (IsMine(*pwalletMain, address.Get()))
    {
        // Detect when changing the account of an address that is the 'unused current key' of another account:
        if (pwalletMain->mapAddressBook.count(address.Get()))
        {
            string strOldAccount = pwalletMain->mapAddressBook[address.Get()].name;
            if (address == GetAccountAddress(strOldAccount))
                GetAccountAddress(strOldAccount, true);
        }
        pwalletMain->SetAddressBook(address.Get(), strAccount, "receive");
    }
    else
        throw JSONRPCError(RPC_MISC_ERROR, "setaccount can only be used with own address");

    return NullUniValue;
}


UniValue getaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaccount \"horizenaddress\"\n"
            "\nDEPRECATED. Returns the account associated with the given address.\n"
            "\nArguments:\n"
            "1. \"horizenaddress\"  (string, required) The horizen address for account lookup.\n"
            "\nResult:\n"
            "\"accountname\"        (string) the account address\n"
            "\nExamples:\n"
            + HelpExampleCli("getaccount", "\"t14oHp2v54vfmdgQ3v3SNuQga8JKHTNi2a1\"")
            + HelpExampleRpc("getaccount", "\"t14oHp2v54vfmdgQ3v3SNuQga8JKHTNi2a1\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");

    string strAccount;
    map<CTxDestination, CAddressBookData>::iterator mi = pwalletMain->mapAddressBook.find(address.Get());
    if (mi != pwalletMain->mapAddressBook.end() && !(*mi).second.name.empty())
        strAccount = (*mi).second.name;
    return strAccount;
}


UniValue getaddressesbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getaddressesbyaccount \"account\"\n"
            "\nDEPRECATED. Returns the list of addresses for the given account.\n"
            "\nArguments:\n"
            "1. \"account\"  (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "\nResult:\n"
            "[                     (json array of string)\n"
            "  \"horizenaddress\"  (string) a horizen address associated with the given account\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("getaddressesbyaccount", "\"tabby\"")
            + HelpExampleRpc("getaddressesbyaccount", "\"tabby\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount = AccountFromValue(params[0]);

    // Find all addresses that have the given account
    UniValue ret(UniValue::VARR);
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strName = item.second.name;
        if (strName == strAccount)
            ret.push_back(address.ToString());
    }
    return ret;
}

UniValue listaddresses(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
                "listaddresses\n"
                "Returns the list of transparent addresses.\n"
                "\nResult:\n"
                "[                     (json array of string)\n"
                "  \"horizenaddress\"  (string) a horizen address associated with the given account\n"
                "  ,...\n"
                "]\n"
                "\nExamples:\n"
                + HelpExampleCli("listaddresses", "")
                + HelpExampleRpc("listaddresses", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue ret(UniValue::VARR);
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strName = item.second.name;
        if (strName == "")
            ret.push_back(address.ToString());
    }
    return ret;
}

static void SendMoney(const CTxDestination &address, CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew)
{
    CAmount curBalance = pwalletMain->GetBalance();

    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    // Parse Zcash address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    vector<CRecipient> vecSend;
    vector< Sidechain::CcRecipientVariant > vecCcSend;
    int nChangePosRet = -1;
    CRecipient recipient = {scriptPubKey, nValue, fSubtractFeeFromAmount};
    vecSend.push_back(recipient);
    if (!pwalletMain->CreateTransaction(vecSend, vecCcSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strError)) {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
}

UniValue sendtoaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
            "sendtoaddress \"horizenaddress\" amount ( \"comment\" \"comment-to\" subtractfeefromamount )\n"
            "\nSend an amount to a given address. The amount is a real and is rounded to the nearest 0.00000001\n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1. \"horizenaddress\"  (string, required) The horizen address to send to.\n"
            "2. \"amount\"      (numeric, required) The amount in btc to send. eg 0.1\n"
            "3. \"comment\"     (string, optional) A comment used to store what the transaction is for. \n"
            "                             This is not part of the transaction, just kept in your wallet.\n"
            "4. \"comment-to\"  (string, optional) A comment to store the name of the person or organization \n"
            "                             to which you're sending the transaction. This is not part of the \n"
            "                             transaction, just kept in your wallet.\n"
            "5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.\n"
            "                             The recipient will receive less Horizen than you enter in the amount field.\n"
            "\nResult:\n"
            "\"transactionid\"  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sendtoaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 0.1")
            + HelpExampleCli("sendtoaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 0.1 \"donation\" \"ZenCash outpost\"")
            + HelpExampleCli("sendtoaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 0.1 \"\" \"\" true")
            + HelpExampleRpc("sendtoaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\", 0.1, \"donation\", \"ZenCash outpost\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CBitcoinAddress address(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && !params[2].isNull() && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["to"]      = params[3].get_str();

    bool fSubtractFeeFromAmount = false;
    if (params.size() > 4)
        fSubtractFeeFromAmount = params[4].get_bool();

    EnsureWalletIsUnlocked();

    SendMoney(address.Get(), nAmount, fSubtractFeeFromAmount, wtx);

    return wtx.GetHash().GetHex();
}

UniValue sc_send(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 3)
        throw runtime_error(
            "sc_send \"address\" amount \"scid\"\n"
            "\nSend a ZEN amount to an address of the given SC\n"
            + HelpRequiringPassphrase() +
            "\nArguments:\n"
            "1. \"address\"        (string, required) The uint256 hex representation of the PublicKey25519Proposition in the SC to send to.\n"
            "2. \"amount\"         (numeric, required) The amount in zen to send. eg 0.1\n"
            "3. \"side chain ID\"  (string, required) The uint256 side chain ID\n"
            "\nResult:\n"
            "\"transactionid\"  (string) The transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("sc_send", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\" 0.1 \"ea3e7ccbfd40c4e2304c4215f76d204e4de63c578ad835510f580d529516a874\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 sc_address;
    std::string inputString = params[0].get_str();
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address format: not an hex");

    sc_address.SetHex(inputString);

    if (sc_address.IsNull() )
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    // Amount
    CAmount nAmount = AmountFromValue(params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // side chain id
    inputString = params[2].get_str();
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");

    uint256 scId;
    scId.SetHex(inputString);

    {
        LOCK(mempool.cs);
        CCoinsViewMemPool scView(pcoinsTip, mempool);
        if (!scView.HaveSidechain(scId))
        {
            LogPrint("sc", "scid[%s] not yet created\n", scId.ToString() );
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not yet created: ") + scId.ToString());
        }
    }

    // Wallet comments
    CWalletTx wtx;

    EnsureWalletIsUnlocked();

    // rely on 'many' implementation
    UniValue input(UniValue::VARR);

    UniValue array(UniValue::VARR);
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("address", sc_address.GetHex()));
    entry.push_back(Pair("amount", ValueFromAmount(nAmount)));
    entry.push_back(Pair("scid", scId.GetHex()));
    array.push_back(entry);

    input.push_back(array);
    return sc_sendmany(input, false);
}

static void ScHandleTransaction(CWalletTx& wtx, std::vector<CcRecipientVariant>& vecCcSend, const CAmount& nTotalOut)
{
    CAmount curBalance = pwalletMain->GetBalance();
    if (nTotalOut > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    CReserveKey keyChange(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    string strFailReason;
    std::vector<CRecipient> vecSend;
    bool fCreated = pwalletMain->CreateTransaction(vecSend, vecCcSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason);
    if (!fCreated)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
    if (!pwalletMain->CommitTransaction(wtx, keyChange))
        throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");
}

static void addCustomData(const std::string& inputString, std::vector<unsigned char>& vBytes)
{ 
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid customData format: not an hex");
 
    const unsigned int cdLen = inputString.length();
    // we prefer to avoid padding with 0 any odd hex string
    if (cdLen%2)
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Invalid customData length %d, must be even (byte string)", cdLen));
 
    const unsigned int cdDataLen = cdLen/2;
 
    if (cdDataLen > MAX_CUSTOM_DATA_LEN)
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Invalid customData length %d, must be %d bytes at most",
            cdDataLen, MAX_CUSTOM_DATA_LEN));
    
    CScCustomData cdBlob;
    cdBlob.SetHex(inputString);
    cdBlob.fill(vBytes, cdDataLen);
}

UniValue sc_create(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp ||  params.size() < 4 ) 
        throw runtime_error(
            "sc_create \"scid\" withdrawalEpochLength [{\"address\":... ,\"amount\":...,...},...]\n"
            "\nCreate a Side chain with the given id staring from the given block. A fixed amount is charged to the creator\n"
            "\nIt also sends cross chain forward transfer of coins multiple times. Amounts are double-precision floating point numbers."
            "\nArguments:\n"
            "1. \"side chain ID\"          (string, required) The uint256 side chain ID\n"
            "2. withdrawalEpochLength:   (numeric, required) Length of the withdrawal epochs\n"
            "3. \"address\"                (string, required) The receiver PublicKey25519Proposition in the SC\n"
            "4. amount:                  (numeric, required) The numeric amount in ZEN is the value\n"
            "5. \"customData\"             (string, optional) It is an arbitrary byte string of even length expressed in\n"
            "                                   hexadecimal format. A max limit of 1024 bytes will be checked\n"
            "\nResult:\n"
            "\"transactionid\"    (string) The transaction id. Only 1 transaction is created regardless of \n"
            "                                    the number of addresses.\n"
            "\nExamples:\n"
            + HelpExampleCli("sc_create", "\"1a3e7ccbfd40c4e2304c3215f76d204e4de63c578ad835510f580d529516a874\" 123456 \"8aaddc9671dc5c8d33a3494df262883411935f4f54002fe283745fb394be508a\" 5.0 \"abcd..ef\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // side chain id
    string inputString = params[0].get_str();
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");

    uint256 scId;
    scId.SetHex(inputString);

    // sanity check of the side chain ID
    CCoinsViewCache scView(pcoinsTip);
    if (scView.HaveSidechain(scId))
    {
        LogPrint("sc", "scid[%s] already created\n", scId.ToString() );
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid already created: ") + scId.ToString());
    }

    int withdrawalEpochLength = params[1].get_int(); 
    if (withdrawalEpochLength < getScMinWithdrawalEpochLength())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid withdrawalEpochLength, less that minimum value allowed\n");

    uint256 address;
    inputString = params[2].get_str();
    if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address format: not an hex");
    address.SetHex(inputString);

    CAmount nAmount = AmountFromValue(params[3]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount must be positive");

    CRecipientScCreation sc;
    sc.scId = scId;
    sc.address = address;
    sc.nValue = nAmount;
    sc.creationData.withdrawalEpochLength = withdrawalEpochLength;

    if (params.size() > 4)
    {
        inputString = params[4].get_str();
        addCustomData(inputString, sc.creationData.customData);
    }

    CcRecipientVariant r(sc);

    vector<CcRecipientVariant> vecCcSend;
    vecCcSend.push_back(r);

    EnsureWalletIsUnlocked();

    CWalletTx wtx;
    ScHandleTransaction(wtx, vecCcSend, nAmount);

    return wtx.GetHash().GetHex();
}

UniValue create_sidechain(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp ||  params.size() != 1)
        throw runtime_error(
            "create_sidechain {\"scid\":... , \"withdrawalEpochLength\":... , \"fromaddress\":..., \"toaddress\":... ,\"amount\":... ,\"minconf\":..., \"fee\":...}\n"
            "\nCreate a Side chain.\n"
            "\nArguments:\n"
            "{\n"                     
            "   \"scid\": id                      (string, optional) The uint256 side chain ID, if omitted a random value is generated\n"
            "   \"withdrawalEpochLength\": epoch  (numeric, optional, default=100) length of the withdrawal epochs\n"
            "   \"fromaddress\":taddr             (string, optional) The taddr to send the funds from. If omitted funds are taken from all available UTXO\n"
            "   \"changeaddress\":taddr           (string, optional) The taddr to send the change to, if any. If not set, \"fromaddress\" is used. If the latter is not set too, a new generated address will be used\n"
            "   \"toaddress\":scaddr              (string, required) The receiver PublicKey25519Proposition in the SC\n"
            "   \"amount\":amount                 (numeric, required) Value expressed in " + CURRENCY_UNIT + "\n"
            "   \"minconf\":conf                  (numeric, optional, default=1) Only use funds confirmed at least this many times.\n"
            "   \"fee\":fee                       (numeric, optional, default=" +
                                                      strprintf("%s", FormatMoney(SC_RPC_OPERATION_DEFAULT_MINERS_FEE)) +
                                                      ") The fee amount to attach to this transaction.\n"
            "   \"customData\":data               (string, optional) It is an arbitrary byte string of even length expressed in\n"
            "                                          hexadecimal format. A max limit of 1024 bytes will be checked\n"
            "}\n"
            "\nResult:\n"
            "\"transactionid\"    (string) The resulting transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("create_sidechain", "'{\"toaddress\": \"8aaddc9671dc5c8d33a3494df262883411935f4f54002fe283745fb394be508a\" ,\"amount\": 5.0}'")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // valid input keywords
    static const std::set<std::string> validKeyArgs =
        {"scid", "withdrawalEpochLength", "fromaddress", "changeaddress",
         "toaddress", "amount", "minconf", "fee", "customData"};

    UniValue inputObject = params[0].get_obj();

    if (!inputObject.isObject())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");

    // keywords set in cmd
    std::set<std::string> setKeyArgs;

    // sanity check, report error if unknown/duplicate key-value pairs
    for (const string& s : inputObject.getKeys())
    {
        if (!validKeyArgs.count(s))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ") + s);

        if (!setKeyArgs.insert(s).second)
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Duplicate key in input: ") + s);
    }

    // ---------------------------------------------------------
    uint256 scId;
    if (setKeyArgs.count("scid"))
    {
        string inputString = find_value(inputObject, "scid").get_str();
        if (inputString.length() == 0 || inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");
        scId.SetHex(inputString);
    }
    else
    {
        scId = GetRandHash();
    }

    // ---------------------------------------------------------
    int withdrawalEpochLength = SC_RPC_OPERATION_DEFAULT_EPOCH_LENGTH;
    if (setKeyArgs.count("withdrawalEpochLength"))
    {
        withdrawalEpochLength = find_value(inputObject, "withdrawalEpochLength").get_int();
        if (withdrawalEpochLength < 1 )
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid withdrawalEpochLength: must be greater that 1");
    }

    ScCreationParameters creationData;
    creationData.withdrawalEpochLength = withdrawalEpochLength;

    // ---------------------------------------------------------
    CBitcoinAddress fromaddress;
    if (setKeyArgs.count("fromaddress"))
    {
        string inputString = find_value(inputObject, "fromaddress").get_str();
        fromaddress = CBitcoinAddress(inputString);
        if(!fromaddress.IsValid())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown fromaddress format: ")+inputString );
        }
    }

    // ---------------------------------------------------------
    CBitcoinAddress changeaddress;
    if (setKeyArgs.count("changeaddress"))
    {
        string inputString = find_value(inputObject, "changeaddress").get_str();
        changeaddress = CBitcoinAddress(inputString);
        if(!changeaddress.IsValid())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown changeaddress format: ")+inputString );
        }
        if (!IsMine(*pwalletMain, GetScriptForDestination(changeaddress.Get())))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, changeaddress is not mine: ")+inputString );
    }

    // ---------------------------------------------------------
    uint256 toaddress;
    if (setKeyArgs.count("toaddress"))
    {
        string inputString = find_value(inputObject, "toaddress").get_str();
        if (inputString.length() == 0 || inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid toaddress format: not an hex");

        toaddress.SetHex(inputString);
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing mandatory parameter in input: \"toaddress\"" );
    }

    // ---------------------------------------------------------
    CAmount nAmount = 0;
    if (setKeyArgs.count("amount"))
    {
        UniValue av = find_value(inputObject, "amount");
        nAmount = AmountFromValue( av );
        if (!MoneyRange(nAmount))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount out of range");
        if (nAmount == 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount can not be null");
    }
    else
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing mandatory parameter in input: \"amount\"" );
    }

    // ---------------------------------------------------------
    int nMinDepth = 1;
    if (setKeyArgs.count("minconf"))
    {
        nMinDepth = find_value(inputObject, "minconf").get_int();
        if (nMinDepth < 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid minconf: must be greater that 0");
    }

    // ---------------------------------------------------------
    CAmount nFee = SC_RPC_OPERATION_DEFAULT_MINERS_FEE;
    if (setKeyArgs.count("fee"))
    {
        UniValue val = find_value(inputObject, "fee");
        if (val.get_real() == 0.0)
        {
            nFee = 0;
        }
        else
        {
            nFee = AmountFromValue(val);
        }
    }
    if (!MoneyRange(nFee))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, fee out of range");
    if (nFee > nAmount)
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Fee %s is greater than output %s",
            FormatMoney(nFee), FormatMoney(nAmount)));

    // ---------------------------------------------------------
    if (setKeyArgs.count("customData"))
    {
        string inputString = find_value(inputObject, "customData").get_str();
        addCustomData(inputString, creationData.customData);
    }

    CMutableTransaction tx_create;
    tx_create.nVersion = SC_TX_VERSION;

    std::vector<ScRpcCmdTx::sOutParams> vOutputs;
    vOutputs.push_back(ScRpcCmdTx::sOutParams(scId, toaddress, nAmount));

    Sidechain::ScRpcCreationCmd cmd(tx_create, vOutputs, fromaddress, changeaddress, nMinDepth, nFee, creationData);

    cmd.addInputs();
    cmd.addChange();
    cmd.addCcOutputs();

    cmd.sign();
    cmd.send();
        
    return tx_create.GetHash().GetHex();
}

UniValue send_to_sidechain(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || (params.size() != 1 && params.size() != 2))
        throw runtime_error(
            "send_to_sidechain {...}\n"
            "\nArguments:\n"
            "1. \"outputs\"                       (string, required) A json array of json objects representing the amounts to send.\n"
            "[{\n"
            "   \"scid\": id                      (string, required) The uint256 side chain ID\n"
            "   \"toaddress\":scaddr              (string, required) The receiver PublicKey25519Proposition in the SC\n"
            "   \"amount\":amount                 (numeric, required) Value expressed in " + CURRENCY_UNIT + "\n"
            "},...,]\n"
            "2. \"params\"                        (string, optional) A json object with the command parameters\n"
            "{\n"                     
            "   \"fromaddress\":taddr             (string, optional) The taddr to send the funds from. If omitted funds are taken from all available UTXO\n"
            "   \"changeaddress\":taddr           (string, optional) The taddr to send the change to, if any. If not set, \"fromaddress\" is used. If the latter is not set too, a new generated address will be used\n"
            "   \"minconf\":conf                  (numeric, optional, default=1) Only use funds confirmed at least this many times.\n"
            "   \"fee\":fee                       (numeric, optional, default=" +
                                                      strprintf("%s", FormatMoney(SC_RPC_OPERATION_DEFAULT_MINERS_FEE)) +
                                                      ") The fee amount to attach to this transaction.\n"
            "}\n"
            "\nResult:\n"
            "\"transactionid\"    (string) The resulting transaction id.\n"
            "\nExamples:\n"
            + HelpExampleCli("send_to_sidechain", "'{TODO}]'")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);
    RPCTypeCheck(params, boost::assign::list_of (UniValue::VARR)(UniValue::VOBJ));

    // valid keywords in cmd
    static const std::set<std::string> validKeyArgs =
        {"fromaddress", "changeaddress", "minconf", "fee"};

    // valid keywords in output array
    static const std::set<std::string> validKeyOutputArray =
        {"scid", "toaddress", "amount"};

    UniValue outputsArr = params[0].get_array();

    // ---------------------------------------------------------
    std::vector<ScRpcCmdTx::sOutParams> vOutputs;
    CAmount totalAmount = 0;

    if (outputsArr.size()==0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output arrays is empty.");

    // keywords set in output array
    for (const UniValue& o : outputsArr.getValues())
    {
        if (!o.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");

        std::set<std::string> setKeyOutputArray;

        // sanity check, report error if unknown/duplicate key-value pairs
        for (const string& s : o.getKeys())
        {
            if (!validKeyOutputArray.count(s))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ") + s);
  
            if (!setKeyOutputArray.insert(s).second)
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Duplicate key in input: ") + s);
        }

        // ---------------------------------------------------------
        uint256 scId;
        if (setKeyOutputArray.count("scid"))
        {
            string inputString = find_value(o, "scid").get_str();
            if (inputString.length() == 0 || inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
                throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");
            scId.SetHex(inputString);
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing mandatory parameter in input: \"toaddress\"" );
        }

        // ---------------------------------------------------------
        uint256 toaddress;
        if (setKeyOutputArray.count("toaddress"))
        {
            string inputString = find_value(o, "toaddress").get_str();
            if (inputString.length() == 0 || inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
                throw JSONRPCError(RPC_TYPE_ERROR, "Invalid toaddress format: not an hex");
  
            toaddress.SetHex(inputString);
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing mandatory parameter in input: \"toaddress\"" );
        }
  
        // ---------------------------------------------------------
        CAmount nAmount = 0;
        if (setKeyOutputArray.count("amount"))
        {
            UniValue av = find_value(o, "amount");
            nAmount = AmountFromValue( av );
            if (!MoneyRange(nAmount))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount out of range");
            if (nAmount == 0)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount can not be null");
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Missing mandatory parameter in input: \"amount\"" );
        }

        {
            LOCK(mempool.cs);
            CCoinsViewMemPool scView(pcoinsTip, mempool);
            if (!scView.HaveSidechain(scId))
            {
                LogPrint("sc", "scid[%s] not yet created\n", scId.ToString() );
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not yet created: ") + scId.ToString());
            }
        }
 
        vOutputs.push_back(ScRpcCmdTx::sOutParams(scId, toaddress, nAmount));
        totalAmount += nAmount;
    }

    // optional parametes
    CBitcoinAddress fromaddress;
    CBitcoinAddress changeaddress;
    int nMinDepth = 1;
    CAmount nFee = SC_RPC_OPERATION_DEFAULT_MINERS_FEE;

    if (params.size() > 1 && !params[1].isNull())
    {
        UniValue cmdParams  = params[1].get_obj();

        if (!cmdParams.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
 
        // keywords set in cmd
        std::set<std::string> setKeyArgs;
 
        // sanity check, report error if unknown/duplicate key-value pairs
        for (const string& s : cmdParams.getKeys())
        {
            if (!validKeyArgs.count(s))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ") + s);
 
            if (!setKeyArgs.insert(s).second)
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Duplicate key in input: ") + s);
        }

        // ---------------------------------------------------------
        if (setKeyArgs.count("fromaddress"))
        {
            string inputString = find_value(cmdParams, "fromaddress").get_str();
            fromaddress = CBitcoinAddress(inputString);
            if(!fromaddress.IsValid())
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown fromaddress format: ")+inputString );
            }
        }
 
        // ---------------------------------------------------------
        if (setKeyArgs.count("changeaddress"))
        {
            string inputString = find_value(cmdParams, "changeaddress").get_str();
            changeaddress = CBitcoinAddress(inputString);
            if(!changeaddress.IsValid())
            {
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown changeaddress format: ")+inputString );
            }
            if (!IsMine(*pwalletMain, GetScriptForDestination(changeaddress.Get())))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, changeaddress is not mine: ")+inputString );
        }
 
        // ---------------------------------------------------------
        if (setKeyArgs.count("minconf"))
        {
            nMinDepth = find_value(cmdParams, "minconf").get_int();
            if (nMinDepth < 0)
                throw JSONRPCError(RPC_TYPE_ERROR, "Invalid minconf: must be greater that 0");
        }
 
        // ---------------------------------------------------------
        if (setKeyArgs.count("fee"))
        {
            UniValue val = find_value(cmdParams, "fee");
            if (val.get_real() == 0.0)
            {
                nFee = 0;
            }
            else
            {
                nFee = AmountFromValue(val);
            }
        }
        if (!MoneyRange(nFee))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, fee out of range");
        if (nFee > totalAmount)
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Fee %s is greater than output %s",
                FormatMoney(nFee), FormatMoney(totalAmount)));
    }

    CMutableTransaction tx_fwd;
    tx_fwd.nVersion = SC_TX_VERSION;

    Sidechain::ScRpcSendCmd cmd(tx_fwd, vOutputs, fromaddress, changeaddress, nMinDepth, nFee);

    cmd.addInputs();
    cmd.addChange();
    cmd.addCcOutputs();

    cmd.sign();
    cmd.send();
        
    return tx_fwd.GetHash().GetHex();
}

UniValue listaddressgroupings(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp)
        throw runtime_error(
            "listaddressgroupings\n"
            "\nLists groups of addresses which have had their common ownership\n"
            "made public by common use as inputs or as the resulting change\n"
            "in past transactions\n"
            "\nResult:\n"
            "[\n"
            "  [\n"
            "    [\n"
            "      \"horizenaddress\",     (string) The horizen address\n"
            "      amount,                 (numeric) The amount in btc\n"
            "      \"account\"             (string, optional) The account (DEPRECATED)\n"
            "    ]\n"
            "    ,...\n"
            "  ]\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("listaddressgroupings", "")
            + HelpExampleRpc("listaddressgroupings", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue jsonGroupings(UniValue::VARR);
    map<CTxDestination, CAmount> balances = pwalletMain->GetAddressBalances();
    BOOST_FOREACH(set<CTxDestination> grouping, pwalletMain->GetAddressGroupings())
    {
        UniValue jsonGrouping(UniValue::VARR);
        BOOST_FOREACH(CTxDestination address, grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(CBitcoinAddress(address).ToString());
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                if (pwalletMain->mapAddressBook.find(CBitcoinAddress(address).Get()) != pwalletMain->mapAddressBook.end())
                    addressInfo.push_back(pwalletMain->mapAddressBook.find(CBitcoinAddress(address).Get())->second.name);
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
}

UniValue signmessage(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
            "signmessage \"horizenaddress\" \"message\"\n"
            "\nSign a message with the private key of an address"
            + HelpRequiringPassphrase() + "\n"
            "\nArguments:\n"
            "1. \"horizenaddress\"  (string, required) The horizen address to use for the private key.\n"
            "2. \"message\"         (string, required) The message to create a signature of.\n"
            "\nResult:\n"
            "\"signature\"          (string) The signature of the message encoded in base 64\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("signmessage", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\", \"my message\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();
    string strMessage = params[1].get_str();

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    CKey key;
    if (!pwalletMain->GetKey(keyID, key))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

UniValue getreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getreceivedbyaddress \"horizenaddress\" ( minconf )\n"
            "\nReturns the total amount received by the given horizenaddress in transactions with at least minconf confirmations.\n"
            "\nArguments:\n"
            "1. \"horizenaddress\"  (string, required) The horizen address for transactions.\n"
            "2. minconf             (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\nResult:\n"
            "amount   (numeric) The total amount in " + CURRENCY_UNIT + " received at this address.\n"
            "\nExamples:\n"
            "\nThe amount from transactions with at least 1 confirmation\n"
            + HelpExampleCli("getreceivedbyaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\"") +
            "\nThe amount including unconfirmed transactions, zero confirmations\n"
            + HelpExampleCli("getreceivedbyaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 0") +
            "\nThe amount with at least 6 confirmation, very safe\n"
            + HelpExampleCli("getreceivedbyaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getreceivedbyaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\", 6")
       );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Bitcoin address
    CBitcoinAddress address = CBitcoinAddress(params[0].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");

    /* Get script for addr without OP_CHECKBLOCKATHEIGHT, cause we will use it only for searching */
    CScript scriptPubKey = GetScriptForDestination(address.Get(), false);
    if (!IsMine(*pwalletMain,scriptPubKey))
        return (double)0.0;

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Tally
    CAmount nAmount = 0;
#if 0
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
#else
    for (auto it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTransactionBase& wtx = *((*it).second);
        if (wtx.getTxBase()->IsCoinBase() || !wtx.getTxBase()->CheckFinal())
#endif
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.getTxBase()->GetVout())
        {
            /* Check that txout.scriptPubKey starts with scriptPubKey instead of full match,
             * cause we cant compare OP_CHECKBLOCKATHEIGHT arguments, they are different all the time */
            auto res = std::search(txout.scriptPubKey.begin(), txout.scriptPubKey.end(), scriptPubKey.begin(),
                                   scriptPubKey.end());
            if (res == txout.scriptPubKey.begin())
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return  ValueFromAmount(nAmount);
}


UniValue getreceivedbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getreceivedbyaccount \"account\" ( minconf )\n"
            "\nDEPRECATED. Returns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.\n"
            "\nArguments:\n"
            "1. \"account\"      (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\nResult:\n"
            "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this account.\n"
            "\nExamples:\n"
            "\nAmount received by the default account with at least 1 confirmation\n"
            + HelpExampleCli("getreceivedbyaccount", "\"\"") +
            "\nAmount received at the tabby account including unconfirmed amounts with zero confirmations\n"
            + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 0") +
            "\nThe amount with at least 6 confirmation, very safe\n"
            + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getreceivedbyaccount", "\"tabby\", 6")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Get the set of pub keys assigned to account
    string strAccount = AccountFromValue(params[0]);
    set<CTxDestination> setAddress = pwalletMain->GetAccountAddresses(strAccount);

    // Tally
    CAmount nAmount = 0;
#if 0
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
#else
    for (auto it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTransactionBase& wtx = *((*it).second);
        if (wtx.getTxBase()->IsCoinBase() || !wtx.getTxBase()->CheckFinal())
#endif
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.getTxBase()->GetVout())
        {
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*pwalletMain, address) && setAddress.count(address))
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return (double)nAmount / (double)COIN;
}


CAmount GetAccountBalance(CWalletDB& walletdb, const string& strAccount, int nMinDepth, const isminefilter& filter)
{
    CAmount nBalance = 0;

    // Tally wallet transactions
    for (auto it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTransactionBase& wtx = *((*it).second);
        if (!wtx.getTxBase()->CheckFinal() || (wtx.getTxBase()->IsCoinBase() && !wtx.HasMatureOutputs()))
            continue;

        CAmount nReceived, nSent, nFee;
        wtx.GetMatureAmountsForAccount(strAccount, nReceived, nSent, nFee, filter);

        if (nReceived != 0 && wtx.GetDepthInMainChain() >= nMinDepth)
            nBalance += nReceived;
        nBalance -= nSent + nFee;
    }

    // Tally internal accounting entries
    nBalance += walletdb.GetAccountCreditDebit(strAccount);

    return nBalance;
}

CAmount GetAccountBalance(const string& strAccount, int nMinDepth, const isminefilter& filter)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);
    return GetAccountBalance(walletdb, strAccount, nMinDepth, filter);
}


UniValue getbalance(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
            "getbalance ( \"account\" minconf includeWatchonly )\n"
            "\nReturns the server's total available balance.\n"
            "\nArguments:\n"
            "1. \"account\"      (string, optional) DEPRECATED. If provided, it MUST be set to the empty string \"\" or to the string \"*\", either of which will give the total available balance. Passing any other string will result in an error.\n"
            "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "3. includeWatchonly (bool, optional, default=false) Also include balance in watchonly addresses (see 'importaddress')\n"
            "\nResult:\n"
            "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this account.\n"
            "\nExamples:\n"
            "\nThe total amount in the wallet\n"
            + HelpExampleCli("getbalance", "") +
            "\nThe total amount in the wallet at least 5 blocks confirmed\n"
            + HelpExampleCli("getbalance", "\"*\" 6") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("getbalance", "\"*\", 6")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (params.size() == 0)
        return  ValueFromAmount(pwalletMain->GetBalance());

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();
    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 2)
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    if (params[0].get_str() == "*") {
        // Calculate total balance a different way from GetBalance()
        // (GetBalance() sums up all unspent TxOuts)
        // getbalance and "getbalance * 1 true" should return the same number
        CAmount nBalance = 0;
        for (auto it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
        {
            const CWalletTransactionBase& wtx = *((*it).second);
            if (!wtx.getTxBase()->CheckFinal() || !wtx.HasMatureOutputs())
                continue;

            CAmount allFee;
            string strSentAccount;
            list<COutputEntry> listReceived;
            list<COutputEntry> listSent;
            list<CScOutputEntry> listScSent;
            wtx.GetAmounts(listReceived, listSent, listScSent, allFee, strSentAccount, filter);
            if (wtx.GetDepthInMainChain() >= nMinDepth) {
                for(const COutputEntry& r: listReceived)
                    if (r.maturity == CCoins::outputMaturity::MATURE)
                        nBalance += r.amount;
            }

            for(const COutputEntry& s: listSent)
                nBalance -= s.amount;
            for(const CScOutputEntry& s: listScSent)
                nBalance -= s.amount;

            nBalance -= allFee;
        }
        return  ValueFromAmount(nBalance);
    }

    string strAccount = AccountFromValue(params[0]);

    CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, filter);

    return ValueFromAmount(nBalance);
}

UniValue getunconfirmedbalance(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw runtime_error(
                "getunconfirmedbalance\n"
                "Returns the server's total unconfirmed balance\n");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ValueFromAmount(pwalletMain->GetUnconfirmedBalance());
}


UniValue movecmd(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 5)
        throw runtime_error(
            "move \"fromaccount\" \"toaccount\" amount ( minconf \"comment\" )\n"
            "\nDEPRECATED. Move a specified amount from one account in your wallet to another.\n"
            "\nArguments:\n"
            "1. \"fromaccount\"   (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "2. \"toaccount\"     (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "3. amount            (numeric) Quantity of " + CURRENCY_UNIT + " to move between accounts.\n"
            "4. minconf           (numeric, optional, default=1) Only use funds with at least this many confirmations.\n"
            "5. \"comment\"       (string, optional) An optional comment, stored in the wallet only.\n"
            "\nResult:\n"
            "true|false           (boolean) true if successful.\n"
            "\nExamples:\n"
            "\nMove 0.01 " + CURRENCY_UNIT + " from the default account to the account named tabby\n"
            + HelpExampleCli("move", "\"\" \"tabby\" 0.01") +
            "\nMove 0.01 " + CURRENCY_UNIT + " timotei to akiko with a comment and funds have 6 confirmations\n"
            + HelpExampleCli("move", "\"timotei\" \"akiko\" 0.01 6 \"happy birthday!\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("move", "\"timotei\", \"akiko\", 0.01, 6, \"happy birthday!\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strFrom = AccountFromValue(params[0]);
    string strTo = AccountFromValue(params[1]);
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    if (params.size() > 3)
        // unused parameter, used to be nMinDepth, keep type-checking it though
        (void)params[3].get_int();
    string strComment;
    if (params.size() > 4)
        strComment = params[4].get_str();

    CWalletDB walletdb(pwalletMain->strWalletFile);
    if (!walletdb.TxnBegin())
        throw JSONRPCError(RPC_DATABASE_ERROR, "database error");

    int64_t nNow = GetAdjustedTime();

    // Debit
    CAccountingEntry debit;
    debit.nOrderPos = pwalletMain->IncOrderPosNext(&walletdb);
    debit.strAccount = strFrom;
    debit.nCreditDebit = -nAmount;
    debit.nTime = nNow;
    debit.strOtherAccount = strTo;
    debit.strComment = strComment;
    walletdb.WriteAccountingEntry(debit);

    // Credit
    CAccountingEntry credit;
    credit.nOrderPos = pwalletMain->IncOrderPosNext(&walletdb);
    credit.strAccount = strTo;
    credit.nCreditDebit = nAmount;
    credit.nTime = nNow;
    credit.strOtherAccount = strFrom;
    credit.strComment = strComment;
    walletdb.WriteAccountingEntry(credit);

    if (!walletdb.TxnCommit())
        throw JSONRPCError(RPC_DATABASE_ERROR, "database error");

    return true;
}


UniValue sendfrom(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 6)
        throw runtime_error(
            "sendfrom \"fromaccount\" \"tohorizenaddress\" amount ( minconf \"comment\" \"comment-to\" )\n"
            "\nDEPRECATED (use sendtoaddress). Sent an amount from an account to a Horizen address.\n"
            "The amount is a real and is rounded to the nearest 0.00000001."
            + HelpRequiringPassphrase() + "\n"
            "\nArguments:\n"
            "1. \"fromaccount\"       (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "2. \"tohorizenaddress\"  (string, required) The horizen address to send funds to.\n"
            "3. amount                (numeric, required) The amount in btc. (transaction fee is added on top).\n"
            "4. minconf               (numeric, optional, default=1) Only use funds with at least this many confirmations.\n"
            "5. \"comment\"           (string, optional) A comment used to store what the transaction is for. \n"
            "                                     This is not part of the transaction, just kept in your wallet.\n"
            "6. \"comment-to\"        (string, optional) An optional comment to store the name of the person or organization \n"
            "                                     to which you're sending the transaction. This is not part of the transaction, \n"
            "                                     it is just kept in your wallet.\n"
            "\nResult:\n"
            "\"transactionid\"        (string) The transaction id.\n"
            "\nExamples:\n"
            "\nSend 0.01 " + CURRENCY_UNIT + " from the default account to the address, must have at least 1 confirmation\n"
            + HelpExampleCli("sendfrom", "\"\" \"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 0.01") +
            "\nSend 0.01 from the tabby account to the given address, funds must have at least 6 confirmations\n"
            + HelpExampleCli("sendfrom", "\"tabby\" \"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 0.01 6 \"donation\" \"ZenCash outpost\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendfrom", "\"tabby\", \"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\", 0.01, 6, \"donation\", \"ZenCash outpost\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount = AccountFromValue(params[0]);
    CBitcoinAddress address(params[1].get_str());
    if (!address.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    int nMinDepth = 1;
    if (params.size() > 3)
        nMinDepth = params[3].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 4 && !params[4].isNull() && !params[4].get_str().empty())
        wtx.mapValue["comment"] = params[4].get_str();
    if (params.size() > 5 && !params[5].isNull() && !params[5].get_str().empty())
        wtx.mapValue["to"]      = params[5].get_str();

    EnsureWalletIsUnlocked();

    // Check funds
    CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, ISMINE_SPENDABLE);
    if (nAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    SendMoney(address.Get(), nAmount, false, wtx);

    return wtx.GetHash().GetHex();
}


UniValue sendmany(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
            "sendmany \"fromaccount\" {\"address\":amount,...} ( minconf \"comment\" [\"address\",...] )\n"
            "\nSend multiple times. Amounts are double-precision floating point numbers."
            + HelpRequiringPassphrase() + "\n"
            "\nArguments:\n"
            "1. \"fromaccount\"         (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"
            "2. \"amounts\"             (string, required) A json object with addresses and amounts\n"
            "    {\n"
            "      \"address\":amount   (numeric) The horizen address is the key, the numeric amount in btc is the value\n"
            "      ,...\n"
            "    }\n"
            "3. minconf                 (numeric, optional, default=1) Only use the balance confirmed at least this many times.\n"
            "4. \"comment\"             (string, optional) A comment\n"
            "5. subtractfeefromamount   (string, optional) A json array with addresses.\n"
            "                           The fee will be equally deducted from the amount of each selected address.\n"
            "                           Those recipients will receive less Zen than you enter in their corresponding amount field.\n"
            "                           If no addresses are specified here, the sender pays the fee.\n"
            "    [\n"
            "      \"address\"            (string) Subtract fee from this address\n"
            "      ,...\n"
            "    ]\n"
            "\nResult:\n"
            "\"transactionid\"          (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
            "                                    the number of addresses.\n"
            "\nExamples:\n"
            "\nSend two amounts to two different addresses:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\\\":0.01,\\\"znYHqyumkLY3zVwgaHq3sbtHXuP8GxsNws3\\\":0.02}\"") +
            "\nSend two amounts to two different addresses setting the confirmation and comment:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\\\":0.01,\\\"znYHqyumkLY3zVwgaHq3sbtHXuP8GxsNws3\\\":0.02}\" 6 \"testing\"") +
            "\nSend two amounts to two different addresses, subtract fee from amount:\n"
            + HelpExampleCli("sendmany", "\"\" \"{\\\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\\\":0.01,\\\"znYHqyumkLY3zVwgaHq3sbtHXuP8GxsNws3\\\":0.02}\" 1 \"\" \"[\\\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\\\",\\\"znYHqyumkLY3zVwgaHq3sbtHXuP8GxsNws3\\\"]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("sendmany", "\"\", \"{\\\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\\\":0.01,\\\"znYHqyumkLY3zVwgaHq3sbtHXuP8GxsNws3\\\":0.02}\", 6, \"testing\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount = AccountFromValue(params[0]);
    UniValue sendTo = params[1].get_obj();
    int nMinDepth = 1;
    if (params.size() > 2)
        nMinDepth = params[2].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["comment"] = params[3].get_str();

    UniValue subtractFeeFromAmount(UniValue::VARR);
    if (params.size() > 4)
        subtractFeeFromAmount = params[4].get_array();

    set<CBitcoinAddress> setAddress;
    vector<CRecipient> vecSend;

    CAmount totalAmount = 0;
    vector<string> keys = sendTo.getKeys();
    BOOST_FOREACH(const string& name_, keys)
    {
        CBitcoinAddress address(name_);
        if (!address.IsValid())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Zen address: ")+name_);

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+name_);
        setAddress.insert(address);

        CScript scriptPubKey = GetScriptForDestination(address.Get());
        CAmount nAmount = AmountFromValue(sendTo[name_]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
        totalAmount += nAmount;

        bool fSubtractFeeFromAmount = false;
        for (size_t idx = 0; idx < subtractFeeFromAmount.size(); idx++) {
            const UniValue& addr = subtractFeeFromAmount[idx];
            if (addr.get_str() == name_)
                fSubtractFeeFromAmount = true;
        }

        CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    EnsureWalletIsUnlocked();

    // Check funds
    CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, ISMINE_SPENDABLE);
    if (totalAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    // Send
    CReserveKey keyChange(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    string strFailReason;
    vector< Sidechain::CcRecipientVariant > vecCcSend;
    bool fCreated = pwalletMain->CreateTransaction(vecSend, vecCcSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason);
    if (!fCreated)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
    if (!pwalletMain->CommitTransaction(wtx, keyChange))
        throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");

    return wtx.GetHash().GetHex();
}

// Defined in rpcmisc.cpp
extern CScript _createmultisig_redeemScript(const UniValue& params);

UniValue addmultisigaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 3)
    {
        string msg = "addmultisigaddress nrequired [\"key\",...] ( \"account\" )\n"
            "\nAdd a nrequired-to-sign multisignature address to the wallet.\n"
            "Each key is a Zen address or hex-encoded public key.\n"
            "If 'account' is specified (DEPRECATED), assign address to that account.\n"

            "\nArguments:\n"
            "1. nrequired        (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. \"keysobject\"   (string, required) A json array of horizen addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"address\"  (string) horizen address or hex-encoded public key\n"
            "       ...,\n"
            "     ]\n"
            "3. \"account\"      (string, optional) DEPRECATED. If provided, MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.\n"

            "\nResult:\n"
            "\"horizenaddress\"  (string) A horizen address associated with the keys.\n"

            "\nExamples:\n"
            "\nAdd a multisig address from 2 addresses\n"
            + HelpExampleCli("addmultisigaddress", "2 \"[\\\"t16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"t171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"t16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"t171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"")
        ;
        throw runtime_error(msg);
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount;
    if (params.size() > 2)
        strAccount = AccountFromValue(params[2]);

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);
    pwalletMain->AddCScript(inner);

    pwalletMain->SetAddressBook(innerID, strAccount, "send");
    return CBitcoinAddress(innerID).ToString();
}


struct tallyitem
{
    CAmount nAmount;
    int nConf;
    vector<uint256> txids;
    bool fIsWatchonly;
    tallyitem()
    {
        nAmount = 0;
        nConf = std::numeric_limits<int>::max();
        fIsWatchonly = false;
    }
};

UniValue ListReceived(const UniValue& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get_bool();

    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 2)
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    // Tally
    map<CBitcoinAddress, tallyitem> mapTally;
#if 0
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
#else
    for (auto it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTransactionBase& wtx = *((*it).second);
        if (wtx.getTxBase()->IsCoinBase() || !wtx.getTxBase()->CheckFinal())
#endif
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        BOOST_FOREACH(const CTxOut& txout, wtx.getTxBase()->GetVout())
        {
            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address))
                continue;

            isminefilter mine = IsMine(*pwalletMain, address);
            if(!(mine & filter))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = min(item.nConf, nDepth);
            item.txids.push_back(wtx.getTxBase()->GetHash());
            if (mine & ISMINE_WATCH_ONLY)
                item.fIsWatchonly = true;
        }
    }

    // Reply
    UniValue ret(UniValue::VARR);
    map<string, tallyitem> mapAccountTally;
    BOOST_FOREACH(const PAIRTYPE(CBitcoinAddress, CAddressBookData)& item, pwalletMain->mapAddressBook)
    {
        const CBitcoinAddress& address = item.first;
        const string& strAccount = item.second.name;
        map<CBitcoinAddress, tallyitem>::iterator it = mapTally.find(address);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        CAmount nAmount = 0;
        int nConf = std::numeric_limits<int>::max();
        bool fIsWatchonly = false;
        if (it != mapTally.end())
        {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
            fIsWatchonly = (*it).second.fIsWatchonly;
        }

        if (fByAccounts)
        {
            tallyitem& item = mapAccountTally[strAccount];
            item.nAmount += nAmount;
            item.nConf = min(item.nConf, nConf);
            item.fIsWatchonly = fIsWatchonly;
        }
        else
        {
            UniValue obj(UniValue::VOBJ);
            if(fIsWatchonly)
                obj.push_back(Pair("involvesWatchonly", true));
            obj.push_back(Pair("address",       address.ToString()));
            obj.push_back(Pair("account",       strAccount));
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            UniValue transactions(UniValue::VARR);
            if (it != mapTally.end())
            {
                BOOST_FOREACH(const uint256& item, (*it).second.txids)
                {
                    transactions.push_back(item.GetHex());
                }
            }
            obj.push_back(Pair("txids", transactions));
            ret.push_back(obj);
        }
    }

    if (fByAccounts)
    {
        for (map<string, tallyitem>::iterator it = mapAccountTally.begin(); it != mapAccountTally.end(); ++it)
        {
            CAmount nAmount = (*it).second.nAmount;
            int nConf = (*it).second.nConf;
            UniValue obj(UniValue::VOBJ);
            if((*it).second.fIsWatchonly)
                obj.push_back(Pair("involvesWatchonly", true));
            obj.push_back(Pair("account",       (*it).first));
            obj.push_back(Pair("amount",        ValueFromAmount(nAmount)));
            obj.push_back(Pair("confirmations", (nConf == std::numeric_limits<int>::max() ? 0 : nConf)));
            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue listreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listreceivedbyaddress ( minconf includeempty includeWatchonly)\n"
            "\nList balances by receiving address.\n"
            "\nArguments:\n"
            "1. minconf       (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
            "2. includeempty  (numeric, optional, default=false) Whether to include addresses that haven't received any payments.\n"
            "3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"involvesWatchonly\" : true,        (bool) Only returned if imported addresses were involved in transaction\n"
            "    \"address\" : \"receivingaddress\",  (string) The receiving address\n"
            "    \"account\" : \"accountname\",       (string) DEPRECATED. The account of the receiving address. The default account is \"\".\n"
            "    \"amount\" : x.xxx,                  (numeric) The total amount in " + CURRENCY_UNIT + " received by the address\n"
            "    \"confirmations\" : n                (numeric) The number of confirmations of the most recent transaction included\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listreceivedbyaddress", "")
            + HelpExampleCli("listreceivedbyaddress", "6 true")
            + HelpExampleRpc("listreceivedbyaddress", "6, true, true")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ListReceived(params, false);
}

UniValue listreceivedbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listreceivedbyaccount ( minconf includeempty includeWatchonly)\n"
            "\nDEPRECATED. List balances by account.\n"
            "\nArguments:\n"
            "1. minconf      (numeric, optional, default=1) The minimum number of confirmations before payments are included.\n"
            "2. includeempty (boolean, optional, default=false) Whether to include accounts that haven't received any payments.\n"
            "3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"involvesWatchonly\" : true,   (bool) Only returned if imported addresses were involved in transaction\n"
            "    \"account\" : \"accountname\",  (string) The account name of the receiving account\n"
            "    \"amount\" : x.xxx,             (numeric) The total amount received by addresses with this account\n"
            "    \"confirmations\" : n           (numeric) The number of confirmations of the most recent transaction included\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listreceivedbyaccount", "")
            + HelpExampleCli("listreceivedbyaccount", "6 true")
            + HelpExampleRpc("listreceivedbyaccount", "6, true, true")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ListReceived(params, true);
}

static void MaybePushAddress(UniValue & entry, const CTxDestination &dest)
{
    CBitcoinAddress addr;
    if (addr.Set(dest))
        entry.push_back(Pair("address", addr.ToString()));
}

void ListTransactions(const CWalletTransactionBase& wtx, const string& strAccount, int nMinDepth, bool fLong, UniValue& ret, const isminefilter& filter)
{
    CAmount nFee;
    string strSentAccount;
    list<COutputEntry> listReceived;
    list<COutputEntry> listSent;
    list<CScOutputEntry> listScSent;

    wtx.GetAmounts(listReceived, listSent, listScSent, nFee, strSentAccount, filter);

    bool fAllAccounts = (strAccount == string("*"));
    bool involvesWatchonly = wtx.IsFromMe(ISMINE_WATCH_ONLY);

    // Sent
    if (( (!listSent.empty() || !listScSent.empty() ) || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        BOOST_FOREACH(const COutputEntry& s, listSent)
        {
            UniValue entry(UniValue::VOBJ);
            if(involvesWatchonly || (::IsMine(*pwalletMain, s.destination) & ISMINE_WATCH_ONLY))
                entry.push_back(Pair("involvesWatchonly", true));
            entry.push_back(Pair("account", strSentAccount));
            MaybePushAddress(entry, s.destination);
            entry.push_back(Pair("category", "send"));
            entry.push_back(Pair("amount", ValueFromAmount(-s.amount)));
            if (s.vout != -1)
               entry.push_back(Pair("vout", s.vout));
            entry.push_back(Pair("fee", ValueFromAmount(-nFee)));
            if (fLong)
                WalletTxToJSON(wtx, entry, filter);

            entry.push_back(Pair("size", (int)(wtx.getTxBase()->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)) ));
            ret.push_back(entry);
        }
        BOOST_FOREACH(const CScOutputEntry& s, listScSent)
        {
            UniValue entry(UniValue::VOBJ);
            entry.push_back(Pair("sc address", s.address.GetHex()));
            entry.push_back(Pair("category", "crosschain"));
            entry.push_back(Pair("amount", ValueFromAmount(-s.amount)));
            entry.push_back(Pair("fee", ValueFromAmount(-nFee)));
            if (fLong)
                WalletTxToJSON(wtx, entry, filter);

            entry.push_back(Pair("size", (int)(wtx.getTxBase()->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)) ));
            ret.push_back(entry);
        }
    }

    // Received
    if (listReceived.size() > 0 && wtx.GetDepthInMainChain() >= nMinDepth) {
        for(const COutputEntry& r: listReceived) {
            string account;
            if (pwalletMain->mapAddressBook.count(r.destination))
                account = pwalletMain->mapAddressBook[r.destination].name;
            if (fAllAccounts || (account == strAccount))
            {
                UniValue entry(UniValue::VOBJ);
                if(involvesWatchonly || (::IsMine(*pwalletMain, r.destination) & ISMINE_WATCH_ONLY))
                    entry.push_back(Pair("involvesWatchonly", true));
                entry.push_back(Pair("account", account));
                MaybePushAddress(entry, r.destination);
                if (wtx.getTxBase()->IsCoinBase())
                {
                    if (wtx.GetDepthInMainChain() < 1)
                        entry.push_back(Pair("category", "orphan"));
                    else if (!wtx.HasMatureOutputs())
                        entry.push_back(Pair("category", "immature"));
                    else
                        entry.push_back(Pair("category", "generate"));
                }
                else
                {
                    if (r.maturity == CCoins::outputMaturity::MATURE)
                        entry.push_back(Pair("category", "receive"));
                    else
                        entry.push_back(Pair("category", "immature"));
                }
                entry.push_back(Pair("amount", ValueFromAmount(r.amount)));
                if (r.vout != -1)
                   entry.push_back(Pair("vout", r.vout));
                if (fLong)
                    WalletTxToJSON(wtx, entry, filter);

                entry.push_back(Pair("size", (int)(wtx.getTxBase()->GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION)) ));
                ret.push_back(entry);
            }
        }
    }
}

void AcentryToJSON(const CAccountingEntry& acentry, const string& strAccount, UniValue& ret)
{
    bool fAllAccounts = (strAccount == string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("account", acentry.strAccount));
        entry.push_back(Pair("category", "move"));
        entry.push_back(Pair("time", acentry.nTime));
        entry.push_back(Pair("amount", ValueFromAmount(acentry.nCreditDebit)));
        entry.push_back(Pair("otheraccount", acentry.strOtherAccount));
        entry.push_back(Pair("comment", acentry.strComment));
        ret.push_back(entry);
    }
}

UniValue listtransactions(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 6)
        throw runtime_error(
            "listtransactions ( \"account\" count from includeWatchonly)\n"
            "\nReturns up to 'count' most recent transactions skipping the first 'from' transactions for address 'address'.\n"
            "\nArguments:\n"
            "1. \"account\"    (string, optional) DEPRECATED. The account name. Should be \"*\".\n"
            "2. count          (numeric, optional, default=10) The number of transactions to return\n"
            "3. from           (numeric, optional, default=0) The number of transactions to skip\n"
            "4. includeWatchonly (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')\n"
            "5. address (string, optional) Include only transactions involving this address\n"
            "6. includeFilteredVin (bool, optional, default=false) Meaningful only if address is specified: include also transactions involving the address as input\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"account\":\"accountname\",       (string) DEPRECATED. The account name associated with the transaction. \n"
            "                                                It will be \"\" for the default account.\n"
            "    \"address\":\"horizenaddress\",    (string) The horizen address of the transaction. Not present for \n"
            "                                                move transactions (category = move).\n"
            "    \"category\":\"send|receive|move\", (string) The transaction category. 'move' is a local (off blockchain)\n"
            "                                                transaction between accounts, and not associated with an address,\n"
            "                                                transaction id or block. 'send' and 'receive' transactions are \n"
            "                                                associated with an address, transaction id and block details\n"
            "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the\n"
            "                                         'move' category for moves outbound. It is positive for the 'receive' category,\n"
            "                                         and for the 'move' category for inbound funds.\n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the \n"
            "                                         'send' category of transactions.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and \n"
            "                                         'receive' category of transactions.\n"
            "    \"blockhash\": \"hashvalue\", (string) The block hash containing the transaction. Available for 'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"blockindex\": n,          (numeric) The block index containing the transaction. Available for 'send' and 'receive'\n"
            "                                          category of transactions.\n"
            "    \"txid\": \"transactionid\", (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (midnight Jan 1 1970 GMT). Available \n"
            "                                          for 'send' and 'receive' category of transactions.\n"
            "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
            "    \"otheraccount\": \"accountname\",  (string) For the 'move' category of transactions, the account the funds came \n"
            "                                          from (for receiving funds, positive amounts), or went to (for sending funds,\n"
            "                                          negative amounts).\n"
            "    \"size\": n,                (numeric) Transaction size in bytes\n"
            "  }\n"
            "]\n"

            "\nExamples:\n"
            "\nList the most recent 10 transactions in the systems\n"
            + HelpExampleCli("listtransactions", "") +
            "\nList transactions 100 to 120\n"
            + HelpExampleCli("listtransactions", "\"*\" 20 100") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("listtransactions", "\"*\", 20, 100")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount("*");
    if (params.size() > 0)
        strAccount = params[0].get_str();

    int nCount = 10;
    if (params.size() > 1)
        nCount = params[1].get_int();
    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");

    int nFrom = 0;
    if (params.size() > 2)
        nFrom = params[2].get_int();
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 3)
        if(params[3].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;
    string address("*");
    if (params.size()>4) {
        address=params[4].get_str();
        if (address!=("*")) {
            CBitcoinAddress baddress = CBitcoinAddress(address);
            if (!baddress.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");
        }
    }

    bool includeFilteredVin = false;
    if(params.size() > 5)
        if(params[5].get_bool())
            includeFilteredVin = true;

    UniValue ret(UniValue::VARR);

    std::list<CAccountingEntry> acentries;
    TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, strAccount, address, includeFilteredVin);

    // iterate backwards until we have nCount items to return:
    for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
#if 0
        CWalletTx *const pwtx = (*it).second.first;
#else
        CWalletTransactionBase *const pwtx = (*it).second.first;
#endif
        if (pwtx != nullptr)
            ListTransactions(*pwtx, strAccount, 0, true, ret, filter);
        CAccountingEntry *const pacentry = (*it).second.second;
        if (pacentry != nullptr)
            AcentryToJSON(*pacentry, strAccount, ret);

        if ((int)ret.size() >= (nCount+nFrom)) break;
    }

    //getting all the specific Txes requested by nCount and nFrom
    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;

    vector<UniValue> arrTmp = ret.getValues();
    vector<UniValue>::iterator first = arrTmp.begin();
    std::advance(first, nFrom);
    vector<UniValue>::iterator last = arrTmp.begin();
    std::advance(last, nFrom+nCount);

    if (last != arrTmp.end()) arrTmp.erase(last, arrTmp.end());
    if (first != arrTmp.begin()) arrTmp.erase(arrTmp.begin(), first);

    std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);
    return ret;
}

UniValue getunconfirmedtxdata(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 2)
        throw runtime_error(
            "getunconfirmedtxdata ( \"address\")\n"
            "\nReturns the server's total unconfirmed data relevanto to the input address\n"
            "\nArguments:\n"
            " \"address\"            (string, mandatory) consider transactions involving this address\n"
            " spendzeroconfchange  (boolean, optional) If provided the command will force zero confirmation change\n"
            "                         spendability as specified, otherwise the value set by zend option \'spendzeroconfchange\' \n"
            "                         will be used instead\n"

            "\nExamples:\n"
            + HelpExampleCli("getunconfirmedtxdata", "\"ztZ5M1P9ucj3P5JaW5xtY2hWTkp6JsToiHP\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string address = params[0].get_str();
    CBitcoinAddress taddr = CBitcoinAddress(address);
    if (!taddr.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");

    CWallet::eZeroConfChangeUsage zconfchangeusage = CWallet::eZeroConfChangeUsage::ZCC_UNDEF; 
    if (params.size() == 2 )
    {
        if (params[1].get_bool())
        {
            zconfchangeusage = CWallet::eZeroConfChangeUsage::ZCC_TRUE;
        }
        else
        {
            zconfchangeusage = CWallet::eZeroConfChangeUsage::ZCC_FALSE;
        }
    }

    int n = 0;
    CAmount unconfInput = 0;
    CAmount unconfOutput = 0;
    pwalletMain->GetUnconfirmedData(address, n, unconfInput, unconfOutput, zconfchangeusage);

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("unconfirmedInput", ValueFromAmount(unconfInput)));
    ret.push_back(Pair("unconfirmedOutput", ValueFromAmount(unconfOutput)));
    ret.push_back(Pair("unconfirmedTxApperances", n));

    return ret;
}

UniValue listtxesbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() == 0 || params.size() > 4)
        throw runtime_error(
            "listtxesbyaddress ( \"address\" count)\n"
            "\nReturns up to 'count' most recent transactions involving address 'address' bot for vin and vout.\n"
            "\nArguments:\n"
            "1. \"address\"     (string, mandatory) Include transactions involving this address\n"
            "2. count          (numeric, optional, default=10) The number of transactions to return\n"
            "3. from           (numeric, optional, default=0) The number of transactions to skip\n"
            "4. reverse_order  (bool, optional, default=true) sort from the most recent to the oldest\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "      TODO\n"
            "  }\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listtxesbyaddress", "\"ztZ5M1P9ucj3P5JaW5xtY2hWTkp6JsToiHP\" 20")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string address = params[0].get_str();
    CBitcoinAddress taddr = CBitcoinAddress(address);
    if (!taddr.IsValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Zen address");

    int nCount = 10;
    if (params.size() > 1)
        nCount = params[1].get_int();
    if (nCount < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative count");

    int nFrom = 0;
    if (params.size() > 2)
        nFrom = params[2].get_int();
    if (nFrom < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative from");

    bool reverse = false;
    if (params.size() > 3)
        reverse = params[3].get_bool();

    UniValue ret(UniValue::VARR);
    std::list<CAccountingEntry> unused;
    static const bool FILTER_VIN = true;
    MapTxWithInputs txOrdered = pwalletMain->OrderedTxWithInputsMap(address);

    // iterate backwards until we have nCount items to return:
    for (MapTxWithInputs::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
    {
        const CWalletTransactionBase& wtx = *((*it).second.first);
        std::vector<CWalletTransactionBase*> vtxIn = (*it).second.second;
        UniValue o(UniValue::VOBJ);
        TxExpandedToJSON(wtx, vtxIn, o);
        ret.push_back(o);

        if ((int)ret.size() >= (nCount+nFrom)) break;
    }

    //getting all the specific Txes requested by nCount and nFrom
    if (nFrom > (int)ret.size())
        nFrom = ret.size();
    if ((nFrom + nCount) > (int)ret.size())
        nCount = ret.size() - nFrom;

    vector<UniValue> arrTmp = ret.getValues();
    vector<UniValue>::iterator first = arrTmp.begin();
    std::advance(first, nFrom);
    vector<UniValue>::iterator last = arrTmp.begin();
    std::advance(last, nFrom+nCount);

    if (last != arrTmp.end()) arrTmp.erase(last, arrTmp.end());
    if (first != arrTmp.begin()) arrTmp.erase(arrTmp.begin(), first);

    if (reverse)
        std::reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);
    return ret;
}

UniValue listaccounts(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 2)
        throw runtime_error(
            "listaccounts ( minconf includeWatchonly)\n"
            "\nDEPRECATED. Returns Object that has account names as keys, account balances as values.\n"
            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) Only include transactions with at least this many confirmations\n"
            "2. includeWatchonly (bool, optional, default=false) Include balances in watchonly addresses (see 'importaddress')\n"
            "\nResult:\n"
            "{                      (json object where keys are account names, and values are numeric balances\n"
            "  \"account\": x.xxx,  (numeric) The property name is the account name, and the value is the total balance for the account.\n"
            "  ...\n"
            "}\n"
            "\nExamples:\n"
            "\nList account balances where there at least 1 confirmation\n"
            + HelpExampleCli("listaccounts", "") +
            "\nList account balances including zero confirmation transactions\n"
            + HelpExampleCli("listaccounts", "0") +
            "\nList account balances for 6 or more confirmations\n"
            + HelpExampleCli("listaccounts", "6") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("listaccounts", "6")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();
    isminefilter includeWatchonly = ISMINE_SPENDABLE;
    if(params.size() > 1)
        if(params[1].get_bool())
            includeWatchonly = includeWatchonly | ISMINE_WATCH_ONLY;

    map<string, CAmount> mapAccountBalances;
    BOOST_FOREACH(const PAIRTYPE(CTxDestination, CAddressBookData)& entry, pwalletMain->mapAddressBook) {
        if (IsMine(*pwalletMain, entry.first) & includeWatchonly) // This address belongs to me
            mapAccountBalances[entry.second.name] = 0;
    }

    for (auto it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTransactionBase& wtx = *((*it).second);

        CAmount nFee;
        string strSentAccount;
        list<COutputEntry> listReceived;
        list<COutputEntry> listSent;
        list<CScOutputEntry> listScSent;

        if (!wtx.HasMatureOutputs())
            continue;

        wtx.GetAmounts(listReceived, listSent, listScSent, nFee, strSentAccount, includeWatchonly);

        mapAccountBalances[strSentAccount] -= nFee;

        for(const COutputEntry& s: listSent)
            mapAccountBalances[strSentAccount] -= s.amount;

        for(const CScOutputEntry& s: listScSent)
            mapAccountBalances[strSentAccount] -= s.amount;

        if (wtx.GetDepthInMainChain() >= nMinDepth) {
            for(const COutputEntry& r: listReceived) {
                if (r.maturity == CCoins::outputMaturity::IMMATURE)
                    continue;

                if (pwalletMain->mapAddressBook.count(r.destination))
                    mapAccountBalances[pwalletMain->mapAddressBook[r.destination].name] += r.amount;
                else
                    mapAccountBalances[""] += r.amount;
            }
        }
    }

    list<CAccountingEntry> acentries;
    CWalletDB(pwalletMain->strWalletFile).ListAccountCreditDebit("*", acentries);
    BOOST_FOREACH(const CAccountingEntry& entry, acentries)
        mapAccountBalances[entry.strAccount] += entry.nCreditDebit;

    UniValue ret(UniValue::VOBJ);
    BOOST_FOREACH(const PAIRTYPE(string, CAmount)& accountBalance, mapAccountBalances) {
        ret.push_back(Pair(accountBalance.first, ValueFromAmount(accountBalance.second)));
    }
    return ret;
}

UniValue listsinceblock(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp)
        throw runtime_error(
            "listsinceblock ( \"blockhash\" target-confirmations includeWatchonly)\n"
            "\nGet all transactions in blocks since block [blockhash], or all transactions if omitted\n"
            "\nArguments:\n"
            "1. \"blockhash\"   (string, optional) The block hash to list transactions since\n"
            "2. target-confirmations:    (numeric, optional) The confirmations required, must be 1 or more\n"
            "3. includeWatchonly:        (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')"
            "\nResult:\n"
            "{\n"
            "  \"transactions\": [\n"
            "    \"account\":\"accountname\",       (string) DEPRECATED. The account name associated with the transaction. Will be \"\" for the default account.\n"
            "    \"address\":\"horizenaddress\",    (string) The horizen address of the transaction. Not present for move transactions (category = move).\n"
            "    \"category\":\"send|receive\",     (string) The transaction category. 'send' has negative amounts, 'receive' has positive amounts.\n"
            "    \"amount\": x.xxx,          (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the 'move' category for moves \n"
            "                                          outbound. It is positive for the 'receive' category, and for the 'move' category for inbound funds.\n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"fee\": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the 'send' category of transactions.\n"
            "    \"confirmations\": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and 'receive' category of transactions.\n"
            "    \"blockhash\": \"hashvalue\",     (string) The block hash containing the transaction. Available for 'send' and 'receive' category of transactions.\n"
            "    \"blockindex\": n,          (numeric) The block index containing the transaction. Available for 'send' and 'receive' category of transactions.\n"
            "    \"blocktime\": xxx,         (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).\n"
            "    \"txid\": \"transactionid\",  (string) The transaction id. Available for 'send' and 'receive' category of transactions.\n"
            "    \"time\": xxx,              (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT).\n"
            "    \"timereceived\": xxx,      (numeric) The time received in seconds since epoch (Jan 1 1970 GMT). Available for 'send' and 'receive' category of transactions.\n"
            "    \"comment\": \"...\",       (string) If a comment is associated with the transaction.\n"
            "    \"to\": \"...\",            (string) If a comment to is associated with the transaction.\n"
             "  ],\n"
            "  \"lastblock\": \"lastblockhash\"     (string) The hash of the last block\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("listsinceblock", "")
            + HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6")
            + HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CBlockIndex *pindex = NULL;
    int target_confirms = 1;
    isminefilter filter = ISMINE_SPENDABLE;

    if (params.size() > 0)
    {
        uint256 blockId;

        blockId.SetHex(params[0].get_str());
        BlockMap::iterator it = mapBlockIndex.find(blockId);
        if (it != mapBlockIndex.end())
            pindex = it->second;
    }

    if (params.size() > 1)
    {
        target_confirms = params[1].get_int();

        if (target_confirms < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
    }

    if(params.size() > 2)
        if(params[2].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    int depth = pindex ? (1 + chainActive.Height() - pindex->nHeight) : -1;

    UniValue transactions(UniValue::VARR);

#if 0
    for (map<uint256, CWalletTx>::iterator it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTx& tx = (*it).second;
#else
    for (auto it = pwalletMain->getMapWallet().begin(); it != pwalletMain->getMapWallet().end(); ++it)
    {
        const CWalletTransactionBase& tx = *((*it).second);
#endif
        if (depth == -1 || tx.GetDepthInMainChain() < depth)
            ListTransactions(tx, "*", 0, true, transactions, filter);
    }

    CBlockIndex *pblockLast = chainActive[chainActive.Height() + 1 - target_confirms];
    uint256 lastblock = pblockLast ? pblockLast->GetBlockHash() : uint256();

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("transactions", transactions));
    ret.push_back(Pair("lastblock", lastblock.GetHex()));

    return ret;
}

UniValue gettransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "gettransaction \"txid\" ( includeWatchonly )\n"
            "\nGet detailed information about in-wallet transaction <txid>\n"
            "\nArguments:\n"
            "1. \"txid\"    (string, required) The transaction id\n"
            "2. \"includeWatchonly\"    (bool, optional, default=false) Whether to include watchonly addresses in balance calculation and details[]\n"
            "\nResult:\n"
            "{\n"
            "  \"amount\" : x.xxx,        (numeric) The transaction amount in " + CURRENCY_UNIT + "\n"
            "  \"confirmations\" : n,     (numeric) The number of confirmations\n"
            "  \"blockhash\" : \"hash\",  (string) The block hash\n"
            "  \"blockindex\" : xx,       (numeric) The block index\n"
            "  \"blocktime\" : ttt,       (numeric) The time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"txid\" : \"transactionid\",   (string) The transaction id.\n"
            "  \"time\" : ttt,            (numeric) The transaction time in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"timereceived\" : ttt,    (numeric) The time received in seconds since epoch (1 Jan 1970 GMT)\n"
            "  \"details\" : [\n"
            "    {\n"
            "      \"account\" : \"accountname\",  (string) DEPRECATED. The account name involved in the transaction, can be \"\" for the default account.\n"
            "      \"address\" : \"horizenaddress\",   (string) The horizen address involved in the transaction\n"
            "      \"category\" : \"send|receive\",    (string) The category, either 'send' or 'receive'\n"
            "      \"amount\" : x.xxx                  (numeric) The amount in " + CURRENCY_UNIT + "\n"
            "      \"vout\" : n,                       (numeric) the vout value\n"
            "    }\n"
            "    ,...\n"
            "  ],\n"
            "  \"vjoinsplit\" : [\n"
            "    {\n"
            "      \"anchor\" : \"treestateref\",          (string) Merkle root of note commitment tree\n"
            "      \"nullifiers\" : [ string, ... ]      (string) Nullifiers of input notes\n"
            "      \"commitments\" : [ string, ... ]     (string) Note commitments for note outputs\n"
            "      \"macs\" : [ string, ... ]            (string) Message authentication tags\n"
            "      \"vpub_old\" : x.xxx                  (numeric) The amount removed from the transparent value pool\n"
            "      \"vpub_new\" : x.xxx,                 (numeric) The amount added to the transparent value pool\n"
            "    }\n"
            "    ,...\n"
            "  ],\n"
            "  \"hex\" : \"data\"         (string) Raw data for transaction\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
            + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true")
            + HelpExampleRpc("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hash;
    hash.SetHex(params[0].get_str());

    isminefilter filter = ISMINE_SPENDABLE;
    if(params.size() > 1)
        if(params[1].get_bool())
            filter = filter | ISMINE_WATCH_ONLY;

    UniValue entry(UniValue::VOBJ);
    if (!pwalletMain->getMapWallet().count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");

    const CWalletTransactionBase& wtx = *(pwalletMain->getMapWallet().at(hash));

    CAmount nCredit = wtx.GetCredit(filter);
    CAmount nDebit = wtx.GetDebit(filter);
    
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = 0;
    if (wtx.IsFromMe(filter))
    {
        nFee = -(wtx.getTxBase()->GetFeeAmount(nDebit));
    }

    entry.push_back(Pair("amount", ValueFromAmount(nNet - nFee)));
    if (wtx.IsFromMe(filter))
        entry.push_back(Pair("fee", ValueFromAmount(nFee)));

    WalletTxToJSON(wtx, entry, filter);

    UniValue details(UniValue::VARR);
    ListTransactions(wtx, "*", 0, false, details, filter);
    entry.push_back(Pair("details", details));

    string strHex = wtx.getTxBase()->EncodeHex();
    entry.push_back(Pair("hex", strHex));

    return entry;
}


UniValue backupwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
            "backupwallet \"destination\"\n"
            "\nSafely copies wallet.dat to destination filename\n"
            "\nArguments:\n"
            "1. \"destination\"   (string, required) The destination filename, saved in the directory set by -exportdir option.\n"
            "\nResult:\n"
            "\"path\"             (string) The full path of the destination file\n"
            "\nExamples:\n"
            + HelpExampleCli("backupwallet", "\"backupdata\"")
            + HelpExampleRpc("backupwallet", "\"backupdata\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    boost::filesystem::path exportdir;
    try {
        exportdir = GetExportDir();
    } catch (const std::runtime_error& e) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, e.what());
    }
    if (exportdir.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot backup wallet until the -exportdir option has been set");
    }
    std::string unclean = params[0].get_str();
    std::string clean = SanitizeFilename(unclean);
    if (clean.compare(unclean) != 0) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Filename is invalid as only alphanumeric characters are allowed.  Try '%s' instead.", clean));
    }
    boost::filesystem::path exportfilepath = exportdir / clean;

    if (!BackupWallet(*pwalletMain, exportfilepath.string()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

    return exportfilepath.string();
}


UniValue keypoolrefill(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
            "keypoolrefill ( newsize )\n"
            "\nFills the keypool."
            + HelpRequiringPassphrase() + "\n"
            "\nArguments\n"
            "1. newsize     (numeric, optional, default=100) The new keypool size\n"
            "\nExamples:\n"
            + HelpExampleCli("keypoolrefill", "")
            + HelpExampleRpc("keypoolrefill", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    unsigned int kpSize = 0;
    if (params.size() > 0) {
        if (params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = (unsigned int)params[0].get_int();
    }

    EnsureWalletIsUnlocked();
    pwalletMain->TopUpKeyPool(kpSize);

    if (pwalletMain->GetKeyPoolSize() < kpSize)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");

    return NullUniValue;
}


static void LockWallet(CWallet* pWallet)
{
    LOCK(cs_nWalletUnlockTime);
    nWalletUnlockTime = 0;
    pWallet->Lock();
}

UniValue walletpassphrase(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
            "walletpassphrase \"passphrase\" timeout\n"
            "\nStores the wallet decryption key in memory for 'timeout' seconds.\n"
            "This is needed prior to performing transactions related to private keys such as sending horizen\n"
            "\nArguments:\n"
            "1. \"passphrase\"     (string, required) The wallet passphrase\n"
            "2. timeout            (numeric, required) The time to keep the decryption key in seconds.\n"
            "\nNote:\n"
            "Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock\n"
            "time that overrides the old one.\n"
            "\nExamples:\n"
            "\nunlock the wallet for 60 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60") +
            "\nLock the wallet again (before 60 seconds)\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("walletpassphrase", "\"my pass phrase\", 60")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

    // Note that the walletpassphrase is stored in params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() > 0)
    {
        if (!pwalletMain->Unlock(strWalletPass))
            throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }
    else
        throw runtime_error(
            "walletpassphrase <passphrase> <timeout>\n"
            "Stores the wallet decryption key in memory for <timeout> seconds.");

    // No need to check return values, because the wallet was unlocked above
    pwalletMain->UpdateNullifierNoteMap();
    pwalletMain->TopUpKeyPool();

    int64_t nSleepTime = params[1].get_int64();
    LOCK(cs_nWalletUnlockTime);
    nWalletUnlockTime = GetTime() + nSleepTime;
    RPCRunLater("lockwallet", boost::bind(LockWallet, pwalletMain), nSleepTime);

    return NullUniValue;
}


UniValue walletpassphrasechange(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
            "walletpassphrasechange \"oldpassphrase\" \"newpassphrase\"\n"
            "\nChanges the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.\n"
            "\nArguments:\n"
            "1. \"oldpassphrase\"      (string) The current passphrase\n"
            "2. \"newpassphrase\"      (string) The new passphrase\n"
            "\nExamples:\n"
            + HelpExampleCli("walletpassphrasechange", "\"old one\" \"new one\"")
            + HelpExampleRpc("walletpassphrasechange", "\"old one\", \"new one\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = params[1].get_str().c_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw runtime_error(
            "walletpassphrasechange <oldpassphrase> <newpassphrase>\n"
            "Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.");

    if (!pwalletMain->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass))
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");

    return NullUniValue;
}


UniValue walletlock(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 0))
        throw runtime_error(
            "walletlock\n"
            "\nRemoves the wallet encryption key from memory, locking the wallet.\n"
            "After calling this method, you will need to call walletpassphrase again\n"
            "before being able to call any methods which require the wallet to be unlocked.\n"
            "\nExamples:\n"
            "\nSet the passphrase for 2 minutes to perform a transaction\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 120") +
            "\nPerform a send (requires passphrase set)\n"
            + HelpExampleCli("sendtoaddress", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" 1.0") +
            "\nClear the passphrase since we are done before 2 minutes is up\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs json rpc call\n"
            + HelpExampleRpc("walletlock", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");

    {
        LOCK(cs_nWalletUnlockTime);
        pwalletMain->Lock();
        nWalletUnlockTime = 0;
    }

    return NullUniValue;
}


UniValue encryptwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    auto fEnableWalletEncryption = fExperimentalMode && GetBoolArg("-developerencryptwallet", false);

    std::string strWalletEncryptionDisabledMsg = "";
    if (!fEnableWalletEncryption) {
        strWalletEncryptionDisabledMsg = "\nWARNING: Wallet encryption is DISABLED. This call always fails.\n";
    }

    if (!pwalletMain->IsCrypted() && (fHelp || params.size() != 1))
        throw runtime_error(
            "encryptwallet \"passphrase\"\n"
            + strWalletEncryptionDisabledMsg +
            "\nEncrypts the wallet with 'passphrase'. This is for first time encryption.\n"
            "After this, any calls that interact with private keys such as sending or signing \n"
            "will require the passphrase to be set prior the making these calls.\n"
            "Use the walletpassphrase call for this, and then walletlock call.\n"
            "If the wallet is already encrypted, use the walletpassphrasechange call.\n"
            "Note that this will shutdown the server.\n"
            "\nArguments:\n"
            "1. \"passphrase\"    (string) The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long.\n"
            "\nExamples:\n"
            "\nEncrypt you wallet\n"
            + HelpExampleCli("encryptwallet", "\"my pass phrase\"") +
            "\nNow set the passphrase to use the wallet, such as for signing or sending horizen\n"
            + HelpExampleCli("walletpassphrase", "\"my pass phrase\"") +
            "\nNow we can so something like sign\n"
            + HelpExampleCli("signmessage", "\"horizenaddress\" \"test message\"") +
            "\nNow lock the wallet again by removing the passphrase\n"
            + HelpExampleCli("walletlock", "") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("encryptwallet", "\"my pass phrase\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!fEnableWalletEncryption) {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: wallet encryption is disabled.");
    }
    if (pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(std::string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw runtime_error(
            "encryptwallet <passphrase>\n"
            "Encrypts the wallet with <passphrase>.");

    if (!pwalletMain->EncryptWallet(strWalletPass))
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys. So:
    StartShutdown();
    return "wallet encrypted; Horizen server stopping, restart to run with encrypted wallet. The keypool has been flushed, you need to make a new backup.";
}

UniValue lockunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "lockunspent unlock [{\"txid\":\"txid\",\"vout\":n},...]\n"
            "\nUpdates list of temporarily unspendable outputs.\n"
            "Temporarily lock (unlock=false) or unlock (unlock=true) specified transaction outputs.\n"
            "A locked transaction output will not be chosen by automatic coin selection, when spending horizen.\n"
            "Locks are stored in memory only. Nodes start with zero locked outputs, and the locked output list\n"
            "is always cleared (by virtue of process exit) when a node stops or fails.\n"
            "Also see the listunspent call\n"
            "\nArguments:\n"
            "1. unlock            (boolean, required) Whether to unlock (true) or lock (false) the specified transactions\n"
            "2. \"transactions\"  (string, required) A json array of objects. Each object the txid (string) vout (numeric)\n"
            "     [           (json array of json objects)\n"
            "       {\n"
            "         \"txid\":\"id\",    (string) The transaction id\n"
            "         \"vout\": n         (numeric) The output number\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "true|false    (boolean) Whether the command was successful or not\n"

            "\nExamples:\n"
            "\nList the unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nLock an unspent transaction\n"
            + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nList the locked transactions\n"
            + HelpExampleCli("listlockunspent", "") +
            "\nUnlock the transaction again\n"
            + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("lockunspent", "false, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (params.size() == 1)
        RPCTypeCheck(params, boost::assign::list_of(UniValue::VBOOL));
    else
        RPCTypeCheck(params, boost::assign::list_of(UniValue::VBOOL)(UniValue::VARR));

    bool fUnlock = params[0].get_bool();

    if (params.size() == 1) {
        if (fUnlock)
            pwalletMain->UnlockAllCoins();
        return true;
    }

    UniValue outputs = params[1].get_array();
    for (size_t idx = 0; idx < outputs.size(); idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM));

        string txid = find_value(o, "txid").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "vout").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);

        if (fUnlock)
            pwalletMain->UnlockCoin(outpt);
        else
            pwalletMain->LockCoin(outpt);
    }

    return true;
}

UniValue listlockunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw runtime_error(
            "listlockunspent\n"
            "\nReturns list of temporarily unspendable outputs.\n"
            "See the lockunspent call to lock and unlock transactions for spending.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\" : \"transactionid\",     (string) The transaction id locked\n"
            "    \"vout\" : n                      (numeric) The vout value\n"
            "  }\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            "\nList the unspent transactions\n"
            + HelpExampleCli("listunspent", "") +
            "\nLock an unspent transaction\n"
            + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nList the locked transactions\n"
            + HelpExampleCli("listlockunspent", "") +
            "\nUnlock the transaction again\n"
            + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("listlockunspent", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    vector<COutPoint> vOutpts;
    pwalletMain->ListLockedCoins(vOutpts);

    UniValue ret(UniValue::VARR);

    BOOST_FOREACH(COutPoint &outpt, vOutpts) {
        UniValue o(UniValue::VOBJ);

        o.push_back(Pair("txid", outpt.hash.GetHex()));
        o.push_back(Pair("vout", (int)outpt.n));
        ret.push_back(o);
    }

    return ret;
}

UniValue settxfee(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
            "settxfee amount\n"
            "\nSet the transaction fee per kB.\n"
            "\nArguments:\n"
            "1. amount         (numeric, required) The transaction fee in " + CURRENCY_UNIT + "/kB rounded to the nearest 0.00000001\n"
            "\nResult\n"
            "true|false        (boolean) Returns true if successful\n"
            "\nExamples:\n"
            + HelpExampleCli("settxfee", "0.00001")
            + HelpExampleRpc("settxfee", "0.00001")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Amount
    CAmount nAmount = AmountFromValue(params[0]);

    payTxFee = CFeeRate(nAmount, 1000);
    return true;
}

UniValue getwalletinfo(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getwalletinfo\n"
            "Returns an object containing various wallet state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total confirmed horizen balance of the wallet\n"
            "  \"unconfirmed_balance\": xxx, (numeric) the total unconfirmed horizen balance of the wallet\n"
            "  \"immature_balance\": xxxxxx, (numeric) the total immature balance of the wallet\n"
            "  \"txcount\": xxxxxxx,         (numeric) the total number of transactions in the wallet\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee configuration, set in " + CURRENCY_UNIT + "/kB\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getwalletinfo", "")
            + HelpExampleRpc("getwalletinfo", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("walletversion", pwalletMain->GetVersion()));
    obj.push_back(Pair("balance",       ValueFromAmount(pwalletMain->GetBalance())));
    obj.push_back(Pair("unconfirmed_balance", ValueFromAmount(pwalletMain->GetUnconfirmedBalance())));
    obj.push_back(Pair("immature_balance",    ValueFromAmount(pwalletMain->GetImmatureBalance())));
    obj.push_back(Pair("txcount",       (int)pwalletMain->getMapWallet().size()));
    obj.push_back(Pair("keypoololdest", pwalletMain->GetOldestKeyPoolTime()));
    obj.push_back(Pair("keypoolsize",   (int)pwalletMain->GetKeyPoolSize()));
    if (pwalletMain->IsCrypted())
        obj.push_back(Pair("unlocked_until", nWalletUnlockTime));
    obj.push_back(Pair("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK())));
    return obj;
}

UniValue resendwallettransactions(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw runtime_error(
            "resendwallettransactions\n"
            "Immediately re-broadcast unconfirmed wallet transactions to all peers.\n"
            "Intended only for testing; the wallet code periodically re-broadcasts\n"
            "automatically.\n"
            "Returns array of transaction ids that were re-broadcast.\n"
            );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::vector<uint256> txids = pwalletMain->ResendWalletTransactionsBefore(GetTime());
    UniValue result(UniValue::VARR);
    BOOST_FOREACH(const uint256& txid, txids)
    {
        result.push_back(txid.ToString());
    }
    return result;
}

UniValue listunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
            "listunspent ( minconf maxconf  [\"address\",...] )\n"
            "\nReturns array of unspent transaction outputs\n"
            "with between minconf and maxconf (inclusive) confirmations.\n"
            "Optionally filter to only include txouts paid to specified addresses.\n"
            "Results are an array of Objects, each of which has:\n"
            "{txid, vout, scriptPubKey, amount, confirmations}\n"
            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) The minimum confirmations to filter\n"
            "2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter\n"
            "3. \"addresses\"    (string) A json array of horizen addresses to filter\n"
            "    [\n"
            "      \"address\"   (string) horizen address\n"
            "      ,...\n"
            "    ]\n"
            "\nResult\n"
            "[                   (array of json object)\n"
            "  {\n"
            "    \"txid\" : \"txid\",        (string) the transaction id \n"
            "    \"vout\" : n,               (numeric) the vout value\n"
            "    \"generated\" : true|false  (boolean) true if txout is a coinbase transaction output\n"
            "    \"address\" : \"address\",  (string) the horizen address\n"
            "    \"account\" : \"account\",  (string) DEPRECATED. The associated account, or \"\" for the default account\n"
            "    \"scriptPubKey\" : \"key\", (string) the script key\n"
            "    \"amount\" : x.xxx,         (numeric) the transaction amount in " + CURRENCY_UNIT + "\n"
            "    \"confirmations\" : n       (numeric) The number of confirmations\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples\n"
            + HelpExampleCli("listunspent", "")
            + HelpExampleCli("listunspent", "6 9999999 \"[\\\"t1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"t1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
            + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"t1PGFqEzfmQch1gKD3ra4k18PNj3tTUUSqg\\\",\\\"t1LtvqCaApEdUGFkpKMM4MstjcaL4dKg8SP\\\"]\"")
        );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM)(UniValue::VNUM)(UniValue::VARR));

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    set<CBitcoinAddress> setAddress;
    if (params.size() > 2) {
        UniValue inputs = params[2].get_array();
        for (size_t idx = 0; idx < inputs.size(); idx++) {
            const UniValue& input = inputs[idx];
            CBitcoinAddress address(input.get_str());
            if (!address.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid horizen address: ")+input.get_str());
            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+input.get_str());
           setAddress.insert(address);
        }
    }

    UniValue results(UniValue::VARR);
    vector<COutput> vecOutputs;
    assert(pwalletMain != NULL);
    LOCK2(cs_main, pwalletMain->cs_wallet);
    pwalletMain->AvailableCoins(vecOutputs, false, NULL, true, true);
    for(const COutput& out: vecOutputs) {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        if (setAddress.size()) {
            CTxDestination address;
            if (!ExtractDestination(out.tx->getTxBase()->GetVout()[out.pos].scriptPubKey, address))
                continue;

            if (!setAddress.count(address))
                continue;
        }

        CAmount nValue = out.tx->getTxBase()->GetVout()[out.pos].nValue;
        const CScript& pk = out.tx->getTxBase()->GetVout()[out.pos].scriptPubKey;
        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("txid", out.tx->getTxBase()->GetHash().GetHex()));
        entry.push_back(Pair("vout", out.pos));
        if (out.tx->getTxBase()->IsCertificate() )
        {
            entry.push_back(Pair("certified", true));
        }
        else
        {
            entry.push_back(Pair("generated", out.tx->getTxBase()->IsCoinBase()));
        }
        CTxDestination address;
        if (ExtractDestination(out.tx->getTxBase()->GetVout()[out.pos].scriptPubKey, address)) {
            entry.push_back(Pair("address", CBitcoinAddress(address).ToString()));
            if (pwalletMain->mapAddressBook.count(address))
                entry.push_back(Pair("account", pwalletMain->mapAddressBook[address].name));
        }
        entry.push_back(Pair("scriptPubKey", HexStr(pk.begin(), pk.end())));
        if (pk.IsPayToScriptHash()) {
            CTxDestination address;
            if (ExtractDestination(pk, address)) {
                const CScriptID& hash = boost::get<CScriptID>(address);
                CScript redeemScript;
                if (pwalletMain->GetCScript(hash, redeemScript))
                    entry.push_back(Pair("redeemScript", HexStr(redeemScript.begin(), redeemScript.end())));
            }
        }
        entry.push_back(Pair("amount",ValueFromAmount(nValue)));
        entry.push_back(Pair("confirmations",out.nDepth));
        entry.push_back(Pair("spendable", out.fSpendable));
        results.push_back(entry);
    }

    return results;
}

UniValue fundrawtransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
                            "fundrawtransaction \"hexstring\"\n"
                            "\nAdd inputs to a transaction until it has enough in value to meet its out value.\n"
                            "This will not modify existing inputs, and will add one change output to the outputs.\n"
                            "Note that inputs which were signed may need to be resigned after completion since in/outputs have been added.\n"
                            "The inputs added will not be signed, use signrawtransaction for that.\n"
                            "\nArguments:\n"
                            "1. \"hexstring\"    (string, required) The hex string of the raw transaction\n"
                            "\nResult:\n"
                            "{\n"
                            "  \"hex\":       \"value\", (string)  The resulting raw transaction (hex-encoded string)\n"
                            "  \"fee\":       n,         (numeric) The fee added to the transaction\n"
                            "  \"changepos\": n          (numeric) The position of the added change output, or -1\n"
                            "}\n"
                            "\"hex\"             \n"
                            "\nExamples:\n"
                            "\nCreate a transaction with no inputs\n"
                            + HelpExampleCli("createrawtransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") +
                            "\nAdd sufficient unsigned inputs to meet the output value\n"
                            + HelpExampleCli("fundrawtransaction", "\"rawtransactionhex\"") +
                            "\nSign the transaction\n"
                            + HelpExampleCli("signrawtransaction", "\"fundedtransactionhex\"") +
                            "\nSend the transaction\n"
                            + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
                            );

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    // parse hex string from parameter
    CTransaction origTx;
    if (!DecodeHexTx(origTx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    CMutableTransaction tx(origTx);
    CAmount nFee;
    string strFailReason;
    int nChangePos = -1;
    if(!pwalletMain->FundTransaction(tx, nFee, nChangePos, strFailReason))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(tx)));
    result.push_back(Pair("changepos", nChangePos));
    result.push_back(Pair("fee", ValueFromAmount(nFee)));

    return result;
}

UniValue zc_sample_joinsplit(const UniValue& params, bool fHelp)
{
    if (fHelp) {
        throw runtime_error(
            "zcsamplejoinsplit\n"
            "\n"
            "Perform a joinsplit and return the JSDescription.\n"
            );
    }

    LOCK(cs_main);

    const int shieldedTxVersion = ForkManager::getInstance().getShieldedTxVersion(chainActive.Height());
    LogPrintf("shieldedTxVersion (Forkmanager): %d\n", shieldedTxVersion);

    const bool isGroth = (shieldedTxVersion == GROTH_TX_VERSION);

    uint256 pubKeyHash;
    uint256 anchor = ZCIncrementalMerkleTree().root();
    JSDescription samplejoinsplit(isGroth,
								  *pzcashParams,
                                  pubKeyHash,
                                  anchor,
                                  {JSInput(), JSInput()},
                                  {JSOutput(), JSOutput()},
                                  0,
                                  0);


    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    auto os = WithTxVersion(&ss, shieldedTxVersion);
    os << samplejoinsplit;
    return HexStr(ss.begin(), ss.end());
}

UniValue zc_benchmark(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp)) {
        return NullUniValue;
    }

    if (fHelp || params.size() < 2) {
        throw runtime_error(
            "zcbenchmark benchmarktype samplecount\n"
            "\n"
            "Runs a benchmark of the selected type samplecount times,\n"
            "returning the running times of each sample.\n"
            "\n"
            "Output: [\n"
            "  {\n"
            "    \"runningtime\": runningtime\n"
            "  },\n"
            "  {\n"
            "    \"runningtime\": runningtime\n"
            "  }\n"
            "  ...\n"
            "]\n"
            );
    }

    const int shieldedTxVersion = ForkManager::getInstance().getShieldedTxVersion(chainActive.Height());
    LogPrintf("shieldedTxVersion (Forkmanager): %d\n", shieldedTxVersion);



    LOCK(cs_main);

    std::string benchmarktype = params[0].get_str();
    int samplecount = params[1].get_int();

    if (samplecount <= 0) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid samplecount");
    }

    std::vector<double> sample_times;

    JSDescription samplejoinsplit = JSDescription::getNewInstance(shieldedTxVersion == GROTH_TX_VERSION);

    if (benchmarktype == "verifyjoinsplit") {
        CDataStream ss(ParseHexV(params[2].get_str(), "js"), SER_NETWORK, PROTOCOL_VERSION);
        auto os = WithTxVersion(&ss, shieldedTxVersion);
        os >> samplejoinsplit;
    }

    for (int i = 0; i < samplecount; i++) {
        if (benchmarktype == "sleep") {
            sample_times.push_back(benchmark_sleep());
        } else if (benchmarktype == "parameterloading") {
            sample_times.push_back(benchmark_parameter_loading());
        } else if (benchmarktype == "createjoinsplit") {
            if (params.size() < 3) {
                sample_times.push_back(benchmark_create_joinsplit());
            } else {
                int nThreads = params[2].get_int();
                std::vector<double> vals = benchmark_create_joinsplit_threaded(nThreads);
                // Divide by nThreads^2 to get average seconds per JoinSplit because
                // we are running one JoinSplit per thread.
                sample_times.push_back(std::accumulate(vals.begin(), vals.end(), 0.0) / (nThreads*nThreads));
            }
        } else if (benchmarktype == "verifyjoinsplit") {
            sample_times.push_back(benchmark_verify_joinsplit(samplejoinsplit));
#ifdef ENABLE_MINING
        } else if (benchmarktype == "solveequihash") {
            if (params.size() < 3) {
                sample_times.push_back(benchmark_solve_equihash());
            } else {
                int nThreads = params[2].get_int();
                std::vector<double> vals = benchmark_solve_equihash_threaded(nThreads);
                sample_times.insert(sample_times.end(), vals.begin(), vals.end());
            }
#endif
        } else if (benchmarktype == "verifyequihash") {
            sample_times.push_back(benchmark_verify_equihash());
        } else if (benchmarktype == "validatelargetx") {
            sample_times.push_back(benchmark_large_tx());
        } else if (benchmarktype == "trydecryptnotes") {
            int nAddrs = params[2].get_int();
            sample_times.push_back(benchmark_try_decrypt_notes(nAddrs));
        } else if (benchmarktype == "incnotewitnesses") {
            int nTxs = params[2].get_int();
            sample_times.push_back(benchmark_increment_note_witnesses(nTxs));
        } else if (benchmarktype == "connectblockslow") {
            if (Params().NetworkIDString() != "regtest") {
                throw JSONRPCError(RPC_TYPE_ERROR, "Benchmark must be run in regtest mode");
            }
            sample_times.push_back(benchmark_connectblock_slow());
        } else if (benchmarktype == "sendtoaddress") {
            if (Params().NetworkIDString() != "regtest") {
                throw JSONRPCError(RPC_TYPE_ERROR, "Benchmark must be run in regtest mode");
            }
            auto amount = AmountFromValue(params[2]);
            sample_times.push_back(benchmark_sendtoaddress(amount));
        } else if (benchmarktype == "loadwallet") {
            if (Params().NetworkIDString() != "regtest") {
                throw JSONRPCError(RPC_TYPE_ERROR, "Benchmark must be run in regtest mode");
            }
            sample_times.push_back(benchmark_loadwallet());
        } else if (benchmarktype == "listunspent") {
            sample_times.push_back(benchmark_listunspent());
        } else {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid benchmarktype");
        }
    }

    UniValue results(UniValue::VARR);
    for (auto time : sample_times) {
        UniValue result(UniValue::VOBJ);
        result.push_back(Pair("runningtime", time));
        results.push_back(result);
    }

    return results;
}

UniValue zc_raw_receive(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp)) {
        return NullUniValue;
    }

    if (fHelp || params.size() != 2) {
        throw runtime_error(
            "zcrawreceive zcsecretkey encryptednote\n"
            "\n"
            "DEPRECATED. Decrypts encryptednote and checks if the coin commitments\n"
            "are in the blockchain as indicated by the \"exists\" result.\n"
            "\n"
            "Output: {\n"
            "  \"amount\": value,\n"
            "  \"note\": noteplaintext,\n"
            "  \"exists\": exists\n"
            "}\n"
            );
    }

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VSTR));

    LOCK(cs_main);

    CZCSpendingKey spendingkey(params[0].get_str());
    SpendingKey k = spendingkey.Get();

    uint256 epk;
    unsigned char nonce;
    ZCNoteEncryption::Ciphertext ct;
    uint256 h_sig;

    {
        CDataStream ssData(ParseHexV(params[1], "encrypted_note"), SER_NETWORK, PROTOCOL_VERSION);
        try {
            ssData >> nonce;
            ssData >> epk;
            ssData >> ct;
            ssData >> h_sig;
        } catch(const std::exception &) {
            throw runtime_error(
                "encrypted_note could not be decoded"
            );
        }
    }

    ZCNoteDecryption decryptor(k.receiving_key());

    NotePlaintext npt = NotePlaintext::decrypt(
        decryptor,
        ct,
        epk,
        h_sig,
        nonce
    );
    PaymentAddress payment_addr = k.address();
    Note decrypted_note = npt.note(payment_addr);

    assert(pwalletMain != NULL);
    std::vector<boost::optional<ZCIncrementalWitness>> witnesses;
    uint256 anchor;
    uint256 commitment = decrypted_note.cm();
    pwalletMain->WitnessNoteCommitment(
        {commitment},
        witnesses,
        anchor
    );

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << npt;

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("amount", ValueFromAmount(decrypted_note.value())));
    result.push_back(Pair("note", HexStr(ss.begin(), ss.end())));
    result.push_back(Pair("exists", (bool) witnesses[0]));
    return result;
}



UniValue zc_raw_joinsplit(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp)) {
        return NullUniValue;
    }

    if (fHelp || params.size() != 5) {
        throw runtime_error(
            "zcrawjoinsplit rawtx inputs outputs vpub_old vpub_new\n"
            "  inputs: a JSON object mapping {note: zcsecretkey, ...}\n"
            "  outputs: a JSON object mapping {zcaddr: value, ...}\n"
            "\n"
            "DEPRECATED. Splices a joinsplit into rawtx. Inputs are unilaterally confidential.\n"
            "Outputs are confidential between sender/receiver. The vpub_old and\n"
            "vpub_new values are globally public and move transparent value into\n"
            "or out of the confidential value store, respectively.\n"
            "\n"
            "Note: The caller is responsible for delivering the output enc1 and\n"
            "enc2 to the appropriate recipients, as well as signing rawtxout and\n"
            "ensuring it is mined. (A future RPC call will deliver the confidential\n"
            "payments in-band on the blockchain.)\n"
            "\n"
            "Output: {\n"
            "  \"encryptednote1\": enc1,\n"
            "  \"encryptednote2\": enc2,\n"
            "  \"rawtxn\": rawtxout\n"
            "}\n"
            );
    }

    LOCK(cs_main);

    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue inputs = params[1].get_obj();
    UniValue outputs = params[2].get_obj();

    CAmount vpub_old(0);
    CAmount vpub_new(0);

    if (params[3].get_real() != 0.0)
        vpub_old = AmountFromValue(params[3]);

    if (params[4].get_real() != 0.0)
        vpub_new = AmountFromValue(params[4]);

    std::vector<JSInput> vjsin;
    std::vector<JSOutput> vjsout;
    std::vector<Note> notes;
    std::vector<SpendingKey> keys;
    std::vector<uint256> commitments;

    for (const string& name_ : inputs.getKeys()) {
        CZCSpendingKey spendingkey(inputs[name_].get_str());
        SpendingKey k = spendingkey.Get();

        keys.push_back(k);

        NotePlaintext npt;

        {
            CDataStream ssData(ParseHexV(name_, "note"), SER_NETWORK, PROTOCOL_VERSION);
            ssData >> npt;
        }

        PaymentAddress addr = k.address();
        Note note = npt.note(addr);
        notes.push_back(note);
        commitments.push_back(note.cm());
    }

    uint256 anchor;
    std::vector<boost::optional<ZCIncrementalWitness>> witnesses;
    pwalletMain->WitnessNoteCommitment(commitments, witnesses, anchor);

    assert(witnesses.size() == notes.size());
    assert(notes.size() == keys.size());

    {
        for (size_t i = 0; i < witnesses.size(); i++) {
            if (!witnesses[i]) {
                throw runtime_error(
                    "joinsplit input could not be found in tree"
                );
            }

            vjsin.push_back(JSInput(*witnesses[i], notes[i], keys[i]));
        }
    }

    while (vjsin.size() < ZC_NUM_JS_INPUTS) {
        vjsin.push_back(JSInput());
    }

    for (const string& name_ : outputs.getKeys()) {
        CZCPaymentAddress pubaddr(name_);
        PaymentAddress addrTo = pubaddr.Get();
        CAmount nAmount = AmountFromValue(outputs[name_]);

        vjsout.push_back(JSOutput(addrTo, nAmount));
    }

    while (vjsout.size() < ZC_NUM_JS_OUTPUTS) {
        vjsout.push_back(JSOutput());
    }

    // TODO
    if (vjsout.size() != ZC_NUM_JS_INPUTS || vjsin.size() != ZC_NUM_JS_OUTPUTS) {
        throw runtime_error("unsupported joinsplit input/output counts");
    }

    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);

    CMutableTransaction mtx(tx);
    const int shieldedTxVersion = ForkManager::getInstance().getShieldedTxVersion(chainActive.Height() + 1);
    mtx.nVersion = shieldedTxVersion;
    mtx.joinSplitPubKey = joinSplitPubKey;
    JSDescription jsdesc(mtx.nVersion == GROTH_TX_VERSION,
						 *pzcashParams,
                         joinSplitPubKey,
                         anchor,
                         {vjsin[0], vjsin[1]},
                         {vjsout[0], vjsout[1]},
                         vpub_old,
                         vpub_new);

    {
        auto verifier = libzcash::ProofVerifier::Strict();
        assert(jsdesc.Verify(*pzcashParams, verifier, joinSplitPubKey));
    }

    mtx.vjoinsplit.push_back(jsdesc);

    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);

    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL,
                         dataToBeSigned.begin(), 32,
                         joinSplitPrivKey
                        ) == 0);

    // Sanity check
    assert(crypto_sign_verify_detached(&mtx.joinSplitSig[0],
                                       dataToBeSigned.begin(), 32,
                                       mtx.joinSplitPubKey.begin()
                                      ) == 0);

    CTransaction rawTx(mtx);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << rawTx;

    std::string encryptedNote1;
    std::string encryptedNote2;
    {
        CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
        ss2 << ((unsigned char) 0x00);
        ss2 << jsdesc.ephemeralKey;
        ss2 << jsdesc.ciphertexts[0];
        ss2 << jsdesc.h_sig(*pzcashParams, joinSplitPubKey);

        encryptedNote1 = HexStr(ss2.begin(), ss2.end());
    }
    {
        CDataStream ss2(SER_NETWORK, PROTOCOL_VERSION);
        ss2 << ((unsigned char) 0x01);
        ss2 << jsdesc.ephemeralKey;
        ss2 << jsdesc.ciphertexts[1];
        ss2 << jsdesc.h_sig(*pzcashParams, joinSplitPubKey);

        encryptedNote2 = HexStr(ss2.begin(), ss2.end());
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("encryptednote1", encryptedNote1));
    result.push_back(Pair("encryptednote2", encryptedNote2));
    result.push_back(Pair("rawtxn", HexStr(ss.begin(), ss.end())));
    return result;
}

UniValue zc_raw_keygen(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp)) {
        return NullUniValue;
    }

    if (fHelp || params.size() != 0) {
        throw runtime_error(
            "zcrawkeygen\n"
            "\n"
            "DEPRECATED. Generate a zcaddr which can send and receive confidential values.\n"
            "\n"
            "Output: {\n"
            "  \"zcaddress\": zcaddr,\n"
            "  \"zcsecretkey\": zcsecretkey,\n"
            "  \"zcviewingkey\": zcviewingkey,\n"
            "}\n"
            );
    }

    auto k = SpendingKey::random();
    auto addr = k.address();
    auto viewing_key = k.viewing_key();

    CZCPaymentAddress pubaddr(addr);
    CZCSpendingKey spendingkey(k);
    CZCViewingKey viewingkey(viewing_key);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("zcaddress", pubaddr.ToString()));
    result.push_back(Pair("zcsecretkey", spendingkey.ToString()));
    result.push_back(Pair("zcviewingkey", viewingkey.ToString()));
    return result;
}


UniValue z_getnewaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw runtime_error(
            "z_getnewaddress\n"
            "\nReturns a new zaddr for receiving payments.\n"
            "\nArguments:\n"
            "\nResult:\n"
            "\"horizenaddress\"    (string) The new zaddr\n"
            "\nExamples:\n"
            + HelpExampleCli("z_getnewaddress", "")
            + HelpExampleRpc("z_getnewaddress", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    CZCPaymentAddress pubaddr = pwalletMain->GenerateNewZKey();
    std::string result = pubaddr.ToString();
    return result;
}


UniValue z_listaddresses(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
            "z_listaddresses ( includeWatchonly )\n"
            "\nReturns the list of zaddr belonging to the wallet.\n"
            "\nArguments:\n"
            "1. includeWatchonly (bool, optional, default=false) Also include watchonly addresses (see 'z_importviewingkey')\n"
            "\nResult:\n"
            "[                     (json array of string)\n"
            "  \"zaddr\"           (string) a zaddr belonging to the wallet\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("z_listaddresses", "")
            + HelpExampleRpc("z_listaddresses", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    bool fIncludeWatchonly = false;
    if (params.size() > 0) {
        fIncludeWatchonly = params[0].get_bool();
    }

    UniValue ret(UniValue::VARR);
    std::set<libzcash::PaymentAddress> addresses;
    pwalletMain->GetPaymentAddresses(addresses);
    for (auto addr : addresses ) {
        if (fIncludeWatchonly || pwalletMain->HaveSpendingKey(addr)) {
            ret.push_back(CZCPaymentAddress(addr).ToString());
        }
    }
    return ret;
}

CAmount getBalanceTaddr(std::string transparentAddress, int minDepth=1, bool ignoreUnspendable=true) {
    set<CBitcoinAddress> setAddress;
    vector<COutput> vecOutputs;
    CAmount balance = 0;

    if (transparentAddress.length() > 0) {
        CBitcoinAddress taddr = CBitcoinAddress(transparentAddress);
        if (!taddr.IsValid()) {
            throw std::runtime_error("invalid transparent address");
        }
        setAddress.insert(taddr);
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    pwalletMain->AvailableCoins(vecOutputs, false, NULL, true, true);

    BOOST_FOREACH(const COutput& out, vecOutputs) {
        if (out.nDepth < minDepth) {
            continue;
        }

        if (ignoreUnspendable && !out.fSpendable) {
            continue;
        }

        if (setAddress.size()) {
            CTxDestination address;
            if (!ExtractDestination(out.tx->getTxBase()->GetVout()[out.pos].scriptPubKey, address)) {
                continue;
            }

            if (!setAddress.count(address)) {
                continue;
            }
        }

        CAmount nValue = out.tx->getTxBase()->GetVout()[out.pos].nValue;
        balance += nValue;
    }
    return balance;
}

CAmount getBalanceZaddr(std::string address, int minDepth = 1, bool ignoreUnspendable=true) {
    CAmount balance = 0;
    std::vector<CNotePlaintextEntry> entries;
    LOCK2(cs_main, pwalletMain->cs_wallet);
    pwalletMain->GetFilteredNotes(entries, address, minDepth, true, ignoreUnspendable);
    for (auto & entry : entries) {
        balance += CAmount(entry.plaintext.value());
    }
    return balance;
}


UniValue z_listreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size()==0 || params.size() >2)
        throw runtime_error(
            "z_listreceivedbyaddress \"address\" ( minconf )\n"
            "\nReturn a list of amounts received by a zaddr belonging to the node’s wallet.\n"
            "\nArguments:\n"
            "1. \"address\"      (string) The private address.\n"
            "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": xxxxx,     (string) the transaction id\n"
            "  \"amount\": xxxxx,   (numeric) the amount of value in the note\n"
            "  \"memo\": xxxxx,     (string) hexademical string representation of memo field\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("z_listreceivedbyaddress", "\"ztfaW34Gj9FrnGUEf833ywDVL62NWXBM81u6EQnM6VR45eYnXhwztecW1SjxA7JrmAXKJhxhj3vDNEpVCQoSvVoSpmbhtjf\"")
            + HelpExampleRpc("z_listreceivedbyaddress", "\"ztfaW34Gj9FrnGUEf833ywDVL62NWXBM81u6EQnM6VR45eYnXhwztecW1SjxA7JrmAXKJhxhj3vDNEpVCQoSvVoSpmbhtjf\"")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 1) {
        nMinDepth = params[1].get_int();
    }
    if (nMinDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
    }

    // Check that the from address is valid.
    auto fromaddress = params[0].get_str();

    libzcash::PaymentAddress zaddr;
    CZCPaymentAddress address(fromaddress);
    try {
        zaddr = address.Get();
    } catch (const std::runtime_error&) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid zaddr.");
    }

    if (!(pwalletMain->HaveSpendingKey(zaddr) || pwalletMain->HaveViewingKey(zaddr))) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "From address does not belong to this node, zaddr spending key or viewing key not found.");
    }


    UniValue result(UniValue::VARR);
    std::vector<CNotePlaintextEntry> entries;
    pwalletMain->GetFilteredNotes(entries, fromaddress, nMinDepth, false, false);
    for (CNotePlaintextEntry & entry : entries) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("txid",entry.jsop.hash.ToString()));
        obj.push_back(Pair("amount", ValueFromAmount(CAmount(entry.plaintext.value()))));
        std::string data(entry.plaintext.memo().begin(), entry.plaintext.memo().end());
        obj.push_back(Pair("memo", HexStr(data)));
        result.push_back(obj);
    }
    return result;
}


UniValue z_getbalance(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size()==0 || params.size() >2)
        throw runtime_error(
            "z_getbalance \"address\" ( minconf )\n"
            "\nReturns the balance of a taddr or zaddr belonging to the node’s wallet.\n"
            "\nCAUTION: If address is a watch-only zaddr, the returned balance may be larger than the actual balance,"
            "\nbecause spends cannot be detected with incoming viewing keys.\n"
            "\nArguments:\n"
            "1. \"address\"      (string) The selected address. It may be a transparent or private address.\n"
            "2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.\n"
            "\nResult:\n"
            "amount              (numeric) The total amount in " + CURRENCY_UNIT + " received for this address.\n"
            "\nExamples:\n"
            "\nThe total amount received by address \"myaddress\"\n"
            + HelpExampleCli("z_getbalance", "\"myaddress\"") +
            "\nThe total amount received by address \"myaddress\" at least 5 blocks confirmed\n"
            + HelpExampleCli("z_getbalance", "\"myaddress\" 5") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("z_getbalance", "\"myaddress\", 5")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 1) {
        nMinDepth = params[1].get_int();
    }
    if (nMinDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
    }

    // Check that the from address is valid.
    auto fromaddress = params[0].get_str();
    bool fromTaddr = false;
    CBitcoinAddress taddr(fromaddress);
    fromTaddr = taddr.IsValid();
    libzcash::PaymentAddress zaddr;
    if (!fromTaddr) {
        CZCPaymentAddress address(fromaddress);
        try {
            zaddr = address.Get();
        } catch (const std::runtime_error&) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a taddr or zaddr.");
        }
        if (!(pwalletMain->HaveSpendingKey(zaddr) || pwalletMain->HaveViewingKey(zaddr))) {
             throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "From address does not belong to this node, zaddr spending key or viewing key not found.");
        }
    }

    CAmount nBalance = 0;
    if (fromTaddr) {
        nBalance = getBalanceTaddr(fromaddress, nMinDepth, false);
    } else {
        nBalance = getBalanceZaddr(fromaddress, nMinDepth, false);
    }

    return ValueFromAmount(nBalance);
}


UniValue z_gettotalbalance(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 2)
        throw runtime_error(
            "z_gettotalbalance ( minconf includeWatchonly )\n"
            "\nReturn the total value of funds stored in the node’s wallet.\n"
            "\nCAUTION: If the wallet contains watch-only zaddrs, the returned private balance may be larger than the actual balance,"
            "\nbecause spends cannot be detected with incoming viewing keys.\n"
            "\nArguments:\n"
            "1. minconf          (numeric, optional, default=1) Only include private and transparent transactions confirmed at least this many times.\n"
            "2. includeWatchonly (bool, optional, default=false) Also include balance in watchonly addresses (see 'importaddress' and 'z_importviewingkey')\n"
            "\nResult:\n"
            "{\n"
            "  \"transparent\": xxxxx,     (numeric) the total balance of transparent funds\n"
            "  \"private\": xxxxx,         (numeric) the total balance of private funds\n"
            "  \"total\": xxxxx,           (numeric) the total balance of both transparent and private funds\n"
            "}\n"
            "\nExamples:\n"
            "\nThe total amount in the wallet\n"
            + HelpExampleCli("z_gettotalbalance", "") +
            "\nThe total amount in the wallet at least 5 blocks confirmed\n"
            + HelpExampleCli("z_gettotalbalance", "5") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("z_gettotalbalance", "5")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 0) {
        nMinDepth = params[0].get_int();
    }
    if (nMinDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
    }

    bool fIncludeWatchonly = false;
    if (params.size() > 1) {
        fIncludeWatchonly = params[1].get_bool();
    }

    // getbalance and "getbalance * 1 true" should return the same number
    // but they don't because wtx.GetAmounts() does not handle tx where there are no outputs
    // pwalletMain->GetBalance() does not accept min depth parameter
    // so we use our own method to get balance of utxos.
    CAmount nBalance = getBalanceTaddr("", nMinDepth, !fIncludeWatchonly);
    CAmount nPrivateBalance = getBalanceZaddr("", nMinDepth, !fIncludeWatchonly);
    CAmount nTotalBalance = nBalance + nPrivateBalance;
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("transparent", FormatMoney(nBalance)));
    result.push_back(Pair("private", FormatMoney(nPrivateBalance)));
    result.push_back(Pair("total", FormatMoney(nTotalBalance)));
    return result;
}

UniValue z_getoperationresult(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
            "z_getoperationresult ([\"operationid\", ... ]) \n"
            "\nRetrieve the result and status of an operation which has finished, and then remove the operation from memory."
            + HelpRequiringPassphrase() + "\n"
            "\nArguments:\n"
            "1. \"operationid\"         (array, optional) A list of operation ids we are interested in.  If not provided, examine all operations known to the node.\n"
            "\nResult:\n"
            "\"    [object, ...]\"      (array) A list of JSON objects\n"
            "\nExamples:\n"
            + HelpExampleCli("z_getoperationresult", "'[\"operationid\", ... ]'")
            + HelpExampleRpc("z_getoperationresult", "'[\"operationid\", ... ]'")
        );

    // This call will remove finished operations
    return z_getoperationstatus_IMPL(params, true);
}

UniValue z_getoperationstatus(const UniValue& params, bool fHelp)
{
   if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
            "z_getoperationstatus ([\"operationid\", ... ]) \n"
            "\nGet operation status and any associated result or error data.  The operation will remain in memory."
            + HelpRequiringPassphrase() + "\n"
            "\nArguments:\n"
            "1. \"operationid\"         (array, optional) A list of operation ids we are interested in.  If not provided, examine all operations known to the node.\n"
            "\nResult:\n"
            "\"    [object, ...]\"      (array) A list of JSON objects\n"
            "\nExamples:\n"
            + HelpExampleCli("z_getoperationstatus", "'[\"operationid\", ... ]'")
            + HelpExampleRpc("z_getoperationstatus", "'[\"operationid\", ... ]'")
        );

   // This call is idempotent so we don't want to remove finished operations
   return z_getoperationstatus_IMPL(params, false);
}

UniValue z_getoperationstatus_IMPL(const UniValue& params, bool fRemoveFinishedOperations=false)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::set<AsyncRPCOperationId> filter;
    if (params.size()==1) {
        UniValue ids = params[0].get_array();
        for (const UniValue & v : ids.getValues()) {
            filter.insert(v.get_str());
        }
    }
    bool useFilter = (filter.size()>0);

    UniValue ret(UniValue::VARR);
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();

    for (auto id : ids) {
        if (useFilter && !filter.count(id))
            continue;

        std::shared_ptr<AsyncRPCOperation> operation = q->getOperationForId(id);
        if (!operation) {
            continue;
            // It's possible that the operation was removed from the internal queue and map during this loop
            // throw JSONRPCError(RPC_INVALID_PARAMETER, "No operation exists for that id.");
        }

        UniValue obj = operation->getStatus();
        std::string s = obj["status"].get_str();
        if (fRemoveFinishedOperations) {
            // Caller is only interested in retrieving finished results
            if ("success"==s || "failed"==s || "cancelled"==s) {
                ret.push_back(obj);
                q->popOperationForId(id);
            }
        } else {
            ret.push_back(obj);
        }
    }

    std::vector<UniValue> arrTmp = ret.getValues();

    // sort results chronologically by creation_time
    std::sort(arrTmp.begin(), arrTmp.end(), [](UniValue a, UniValue b) -> bool {
        const int64_t t1 = find_value(a.get_obj(), "creation_time").get_int64();
        const int64_t t2 = find_value(b.get_obj(), "creation_time").get_int64();
        return t1 < t2;
    });

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}


// Here we define the maximum number of zaddr outputs that can be included in a transaction.
// If input notes are small, we might actually require more than one joinsplit per zaddr output.
// For now though, we assume we use one joinsplit per zaddr output (and the second output note is change).
// We reduce the result by 1 to ensure there is room for non-joinsplit CTransaction data.
#define Z_SENDMANY_MAX_ZADDR_OUTPUTS(TX_VER)    ((MAX_TX_SIZE / JSDescription::getNewInstance(TX_VER == GROTH_TX_VERSION).GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION, TX_VER)) - 1)

UniValue z_sendmany(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    const int shieldedTxVersion = ForkManager::getInstance().getShieldedTxVersion(chainActive.Height() + 1);
    LogPrintf("z_sendmany shieldedTxVersion: %d\n", shieldedTxVersion);

    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            "z_sendmany \"fromaddress\" [{\"address\":... ,\"amount\":...},...] ( minconf ) ( fee )\n"
            "\nSend multiple times. Amounts are double-precision floating point numbers."
            "\nChange from a taddr flows to a new taddr address, while change from zaddr returns to itself."
            "\nWhen sending coinbase UTXOs to a zaddr, change is not allowed. The entire value of the UTXO(s) must be consumed."
            + strprintf("\nCurrently, the maximum number of zaddr outputs is %d due to transaction size limits.\n", Z_SENDMANY_MAX_ZADDR_OUTPUTS(shieldedTxVersion))
            + HelpRequiringPassphrase() + "\n"
            "\nArguments:\n"
            "1. \"fromaddress\"         (string, required) The taddr or zaddr to send the funds from.\n"
            "2. \"amounts\"             (array, required) An array of json objects representing the amounts to send.\n"
            "    [{\n"
            "      \"address\":address  (string, required) The address is a taddr or zaddr\n"
            "      \"amount\":amount    (numeric, required) The numeric amount in " + CURRENCY_UNIT + " is the value\n"
            "      \"memo\":memo        (string, optional) If the address is a zaddr, raw data represented in hexadecimal string format\n"
            "    }, ... ]\n"
            "3. minconf               (numeric, optional, default=1) Only use funds confirmed at least this many times.\n"
            "4. fee                   (numeric, optional, default="
            + strprintf("%s", FormatMoney(ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE)) + ") The fee amount to attach to this transaction.\n"
            "\nResult:\n"
            "\"operationid\"          (string) An operationid to pass to z_getoperationstatus to get the result of the operation.\n"
            "\nExamples:\n"
            + HelpExampleCli("z_sendmany", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\" '[{\"address\": \"ztfaW34Gj9FrnGUEf833ywDVL62NWXBM81u6EQnM6VR45eYnXhwztecW1SjxA7JrmAXKJhxhj3vDNEpVCQoSvVoSpmbhtjf\" ,\"amount\": 5.0}]'")
            + HelpExampleRpc("z_sendmany", "\"znnwwojWQJp1ARgbi1dqYtmnNMfihmg8m1b\", [{\"address\": \"ztfaW34Gj9FrnGUEf833ywDVL62NWXBM81u6EQnM6VR45eYnXhwztecW1SjxA7JrmAXKJhxhj3vDNEpVCQoSvVoSpmbhtjf\" ,\"amount\": 5.0}]")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Check that the from address is valid.
    auto fromaddress = params[0].get_str();
    bool fromTaddr = false;
    CBitcoinAddress taddr(fromaddress);
    fromTaddr = taddr.IsValid();
    libzcash::PaymentAddress zaddr;
    if (!fromTaddr) {
        CZCPaymentAddress address(fromaddress);
        try {
            zaddr = address.Get();
        } catch (const std::runtime_error&) {
            // invalid
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a taddr or zaddr.");
        }
    }

    // Check that we have the spending key
    if (!fromTaddr) {
        if (!pwalletMain->HaveSpendingKey(zaddr)) {
             throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "From address does not belong to this node, zaddr spending key not found.");
        }
    }

    UniValue outputs = params[1].get_array();

    if (outputs.size()==0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amounts array is empty.");

    // Keep track of addresses to spot duplicates
    set<std::string> setAddress;

    // Recipients
    std::vector<SendManyRecipient> taddrRecipients;
    std::vector<SendManyRecipient> zaddrRecipients;
    CAmount nTotalOut = 0;

    for (const UniValue& o : outputs.getValues()) {
        if (!o.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");

        // sanity check, report error if unknown key-value pairs
        for (const string& name_ : o.getKeys()) {
            std::string s = name_;
            if (s != "address" && s != "amount" && s!="memo")
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ")+s);
        }

        string address = find_value(o, "address").get_str();
        bool isZaddr = false;
        CBitcoinAddress taddr(address);
        if (!taddr.IsValid()) {
            try {
                CZCPaymentAddress zaddr(address);
                zaddr.Get();
                isZaddr = true;
            } catch (const std::runtime_error&) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ")+address );
            }
        }

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+address);
        setAddress.insert(address);

        UniValue memoValue = find_value(o, "memo");
        string memo;
        if (!memoValue.isNull()) {
            memo = memoValue.get_str();
            if (!isZaddr) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo can not be used with a taddr.  It can only be used with a zaddr.");
            } else if (!IsHex(memo)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected memo data in hexadecimal format.");
            }
            if (memo.length() > ZC_MEMO_SIZE*2) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,  strprintf("Invalid parameter, size of memo is larger than maximum allowed %d", ZC_MEMO_SIZE ));
            }
        }

        UniValue av = find_value(o, "amount");
        CAmount nAmount = AmountFromValue( av );
        if (nAmount < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount must be positive");

        if (isZaddr) {
            zaddrRecipients.push_back( SendManyRecipient(address, nAmount, memo) );
        } else {
            taddrRecipients.push_back( SendManyRecipient(address, nAmount, memo) );
        }

        nTotalOut += nAmount;
    }



    // Check the number of zaddr outputs does not exceed the limit.
    if (zaddrRecipients.size() > Z_SENDMANY_MAX_ZADDR_OUTPUTS(shieldedTxVersion))  {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, too many zaddr outputs");
    }

    // As a sanity check, estimate and verify that the size of the transaction will be valid.
    // Depending on the input notes, the actual tx size may turn out to be larger and perhaps invalid.
    size_t txsize = 0;
    CMutableTransaction mtx;
    mtx.nVersion = shieldedTxVersion;
    for (unsigned int i = 0; i < zaddrRecipients.size(); i++) {
        mtx.vjoinsplit.push_back(JSDescription::getNewInstance(mtx.nVersion == GROTH_TX_VERSION));
    }
    CTransaction tx(mtx);
    txsize += tx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    if (fromTaddr) {
        txsize += CTXIN_SPEND_DUST_SIZE;
        txsize += CTXOUT_REGULAR_SIZE;      // There will probably be taddr change
    }
    txsize += CTXOUT_REGULAR_SIZE * taddrRecipients.size();
    if (txsize > MAX_TX_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Too many outputs, size of raw transaction would be larger than limit of %d bytes", MAX_TX_SIZE ));
    }

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 2) {
        nMinDepth = params[2].get_int();
    }
    if (nMinDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
    }

    // Fee in Zatoshis, not currency format)
    CAmount nFee = ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE;
    if (params.size() > 3) {
        if (params[3].get_real() == 0.0) {
            nFee = 0;
        } else {
            nFee = AmountFromValue( params[3] );
        }

        // Check that the user specified fee is sane.
        if (nFee > nTotalOut) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Fee %s is greater than the sum of outputs %s", FormatMoney(nFee), FormatMoney(nTotalOut)));
        }
    }

    // Use input parameters as the optional context info to be returned by z_getoperationstatus and z_getoperationresult.
    UniValue o(UniValue::VOBJ);
    o.push_back(Pair("fromaddress", params[0]));
    o.push_back(Pair("amounts", params[1]));
    o.push_back(Pair("minconf", nMinDepth));
    o.push_back(Pair("fee", std::stod(FormatMoney(nFee))));
    UniValue contextInfo = o;

    CMutableTransaction contextualTx;
    bool isShielded = !fromTaddr || zaddrRecipients.size() > 0;
	contextualTx.nVersion = 1;
	if(isShielded) {
		contextualTx.nVersion = shieldedTxVersion;
	}    
    // Create operation and add to global queue
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(contextualTx, fromaddress, taddrRecipients, zaddrRecipients, nMinDepth, nFee, contextInfo) );
    q->addOperation(operation);
    AsyncRPCOperationId operationId = operation->getId();
    return operationId;
}

UniValue sc_sendmany(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "sc_sendmany [{\"address\":... ,\"amount\":...,\"scid\":,...},...]\n"
            "\nSend cross chain forward transfer of coins multiple times. Amounts are double-precision floating point numbers."
            "\nArguments:\n"
            "\"amounts\"                (array, required) An array of json objects representing the amounts to send.\n"
            "    [{\n"                     
            "      \"address\":address     (string, required) The receiver PublicKey25519Proposition in the SC\n"
            "      \"amount\":amount       (numeric, required) The numeric amount in " + CURRENCY_UNIT + " is the value\n"
            "      \"scid\":side chain ID  (string, required) The uint256 side chain ID\n"
            "    }, ... ]\n"
            "\nResult:\n"
            "\"transactionid\"          (string) The transaction id for the send. Only 1 transaction is created regardless of \n"
            "                                    the number of addresses.\n"
            "\nExamples:\n"
            + HelpExampleCli("sc_sendmany", " '[{\"address\": \"8aaddc9671dc5c8d33a3494df262883411935f4f54002fe283745fb394be508a\" ,\"amount\": 5.0 ,\"scid\": \"ea3e7ccbfd40c4e2304c4215f76d204e4de63c578ad835510f580d529516a874\"}]'")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue outputs = params[0].get_array();

    if (outputs.size()==0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, output array is empty.");

    // Recipients
    CAmount nTotalOut = 0;
    vector<CcRecipientVariant> vecSend;

    for (const UniValue& o : outputs.getValues())
    {
        if (!o.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");

        // sanity check, report error if unknown key-value pairs
        for (const string& s : o.getKeys())
        {
            if (s != "address" && s != "amount" && s != "scid")
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ") + s);
        }

        uint256 address;
        address.SetHex(find_value(o, "address").get_str() );

        UniValue av = find_value(o, "amount");
        CAmount nAmount = AmountFromValue( av );
        if (nAmount <= 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount must be positive");

        string inputString = find_value(o, "scid").get_str();
        if (inputString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");

        uint256 scId;
        scId.SetHex(inputString);

        {
            LOCK(mempool.cs);
            CCoinsViewMemPool scView(pcoinsTip, mempool);
            if (!scView.HaveSidechain(scId))
            {
                LogPrint("sc", "scid[%s] not yet created\n", scId.ToString() );
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not yet created: ") + scId.ToString());
            }
        }

        CRecipientForwardTransfer ft;
        ft.address = address;
        ft.nValue = nAmount;
        ft.scId = scId;

        vecSend.push_back(CcRecipientVariant(ft));

        nTotalOut += nAmount;
    }

    // As a sanity check, estimate and verify that the size of the transaction will be valid.
    // Depending on the input notes, the actual tx size may turn out to be larger and perhaps invalid.
    size_t txsize = 0;
    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    CTransaction tx(mtx);
    txsize += tx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
    txsize += CTXIN_SPEND_DUST_SIZE;
    txsize += CTXOUT_REGULAR_SIZE;      // There will probably be taddr change
    txsize += CTXOUT_REGULAR_SIZE * vecSend.size();
    if (txsize > MAX_TX_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Too many outputs, size of raw transaction would be larger than limit of %d bytes", MAX_TX_SIZE ));
    }

    EnsureWalletIsUnlocked();

    // Send
    CWalletTx wtx;

    ScHandleTransaction(wtx, vecSend, nTotalOut);

    return wtx.GetHash().GetHex();
}

UniValue send_certificate(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 5  )
        throw runtime_error(
            "send_certificate scid epochNumber endEpochBlockHash [{\"pubkeyhash\":... ,\"amount\":...},...] (subtractfeefromamount) (fee)\n"
            "\nSend cross chain backward transfers from SC to MC as a certificate."
            "\nArguments:\n"
            "1. \"scid\"                  (string, required) The uint256 side chain ID\n"
            "2. epochNumber             (numeric, required) The epoch number this certificate refers to, zero-based numbered\n"
            "3. \"endEpochBlockHash\"     (string, required) The block hash determining the end of the referenced epoch\n"
            "4. transfers:              (array, required) An array of json objects representing the amounts of the backward transfers. Can also be empty\n"
            "    [{\n"                     
            "      \"pubkeyhash\":\"pkh\"    (string, required) The public key hash of the receiver\n"
            "      \"amount\":amount       (numeric, required) The numeric amount in ZEN\n"
            "    }, ... ]\n"
            "5. fee                     (numeric, required) The fee of the certificate in ZEN\n"
            "\nResult:\n"
            "  \"certificateId\"   (string) The resulting certificate id.\n"
            "\nExamples:\n"
            + HelpExampleCli("send_certificate", "\"ea3e7ccbfd40c4e2304c4215f76d204e4de63c578ad835510f580d529516a874\" 12 \"04a1527384c67d9fce3d091ababfc1de325dbac9b3b14025a53722ff6c53d40e\" '[{\"pubkeyhash\":\"813551c928d41c0436ba7361850797d9b30ad4ed\" ,\"amount\": 5.0}]'")
            + HelpExampleCli("send_certificate", "\"054671870079a64a491ea68e08ed7579ec2e0bd148c51c6e2fe6385b597540f4\" 10 \"0a85efb37d1130009f1b588dcddd26626bbb159ae4a19a703715277b51033144\" '[{\"pubkeyhash\":\"76fea046133b0acc74ebabbd17b80e99816228ab\", \"amount\":33.5}]' false 0.00001")

        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // side chain id
    const string& scIdString = params[0].get_str();
    if (scIdString.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid scid format: not an hex");

    uint256 scId;
    scId.SetHex(scIdString);

    // sanity check of the side chain ID
    CCoinsViewCache scView(pcoinsTip);
    CSidechain scInfo;
    if (!scView.GetSidechain(scId,scInfo))
    {
        LogPrint("sc", "scid[%s] does not exists \n", scId.ToString() );
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("scid not exists: ") + scId.ToString());
    }

    int epochNumber = params[1].get_int(); 
    if (epochNumber < 0)
    {
        LogPrint("sc", "epochNumber can not be negative\n", epochNumber);
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid epochNumber parameter");
    }

    // epoch block hash
    const string& blockHashStr = params[2].get_str();
    if (blockHashStr.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid block hash format: not an hex");

    uint256 endEpochBlockHash;
    endEpochBlockHash.SetHex(blockHashStr);

    // sanity check of the epoch hash block: it must be a legal end epoch hash
    if (!scView.isLegalEpoch(scId, epochNumber, endEpochBlockHash) )
    {
        LogPrintf("ERROR: epochNumber[%d]/endEpochBlockHash[%s] are not legal\n", epochNumber, endEpochBlockHash.ToString() );
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("invalid epoch data"));
    }

    if (scView.isCeasedAtHeight(scId, chainActive.Height()+1)!= CSidechain::state::ALIVE) {
        LogPrintf("ERROR: certificate cannot be accepted, sidechain [%s] already ceased at active height = %d\n",
            scId.ToString(), chainActive.Height());
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("invalid cert height"));
    }

    // - there must not be another certificate for the same epoch (multiple certificates are not allowed)
    // This also checks in mempool
    {
        LOCK(mempool.cs);
        CCoinsViewMemPool viewMemPool(pcoinsTip, mempool);

        if (viewMemPool.HaveCertForEpoch(scId, epochNumber))
        {
            uint256 conflictingCertHash;
            if (mempool.mapSidechains.count(scId))
                conflictingCertHash = mempool.mapSidechains.at(scId).backwardCertificate;
            else
                conflictingCertHash.SetNull();

            LogPrintf("ERROR: certificate %s for epoch %d is already been issued\n",
                (conflictingCertHash.IsNull())?"":conflictingCertHash.ToString(), epochNumber);
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("invalid cert epoch"));
        }
    }

    // can be empty
    const UniValue& outputs = params[3].get_array();

    // Recipients
    CAmount nTotalOut = 0;

    std::vector<ScRpcCmdCert::sBwdParams> vBackwardTransfers;
    for (const UniValue& o : outputs.getValues())
    {
        if (!o.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");

        // sanity check, report error if unknown key-value pairs
        for (const string& s : o.getKeys())
        {
            if (s != "amount" && s != "pubkeyhash")
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ") + s);
        }

        const string& pkeyStr = find_value(o, "pubkeyhash").get_str();
        if (pkeyStr.find_first_not_of("0123456789abcdefABCDEF", 0) != std::string::npos)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid pkey format: not an hex");
        if (pkeyStr.length() != 40)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid pkey format: len is not 20 bytes ");

        uint160 pkeyValue;
        pkeyValue.SetHex(pkeyStr);

        CKeyID keyID(pkeyValue);
        CBitcoinAddress taddr(keyID);

        if (!taddr.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, pubkeyhash does not give a valid address");
        }

        const UniValue& av = find_value(o, "amount");
        // this throw an exception also if it is a legal value less than 1 ZAT
        CAmount nAmount = AmountFromValue(av);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount must be positive");

        vBackwardTransfers.push_back(ScRpcCmdCert::sBwdParams(GetScriptForDestination(taddr.Get(), false), nAmount));

        nTotalOut += nAmount;
    }

    // fee
    CAmount nCertFee = 0;
    if (params.size() > 4)
    {
        nCertFee = AmountFromValue(params[4]);
        if (nCertFee <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for fee, must be positive");
        // any check for upper threshold is left to cert processing
    }

    EnsureWalletIsUnlocked();

    std::string strFailReason;

    CMutableScCertificate cert;
    cert.nVersion = SC_CERT_VERSION;
    cert.scId = scId;
    cert.epochNumber = epochNumber;
    cert.endEpochBlockHash = endEpochBlockHash;

    // optional parameters (TODO to be handled since they will be probabl useful to SBH wallet)
    CBitcoinAddress fromaddress;
    CBitcoinAddress changeaddress;
    
    // allow use of unconfirmed coins
    int nMinDepth = 0; //1; 

    if (nTotalOut > scInfo.balance)
    {
        LogPrint("sc", "%s():%d - insufficent balance in scid[%s]: balance[%s], cert amount[%s]\n",
            __func__, __LINE__, scId.ToString(), FormatMoney(scInfo.balance), FormatMoney(nTotalOut) );
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "sidechain has insufficient funds");
    }

    Sidechain::ScRpcCmdCert cmd(cert, vBackwardTransfers, fromaddress, changeaddress, nMinDepth, nCertFee);

    cmd.addInputs();
    cmd.addChange();
    cmd.addBackwardTransfers();

    cmd.sign();
    cmd.send();

    return cert.GetHash().GetHex();
}


/**
When estimating the number of coinbase utxos we can shield in a single transaction:
1. Joinsplit description is 1802 bytes.
2. Transaction overhead ~ 100 bytes
3. Spending a typical P2PKH is >=148 bytes, as defined in CTXIN_SPEND_DUST_SIZE.
4. Spending a multi-sig P2SH address can vary greatly:
   https://github.com/bitcoin/bitcoin/blob/c3ad56f4e0b587d8d763af03d743fdfc2d180c9b/src/main.cpp#L517
   In real-world coinbase utxos, we consider a 3-of-3 multisig, where the size is roughly:
    (3*(33+1))+3 = 105 byte redeem script
    105 + 1 + 3*(73+1) = 328 bytes of scriptSig, rounded up to 400 based on testnet experiments.
*/
#define CTXIN_SPEND_P2SH_SIZE 400

#define SHIELD_COINBASE_DEFAULT_LIMIT 50

UniValue z_shieldcoinbase(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            "z_shieldcoinbase \"fromaddress\" \"tozaddress\" ( fee ) ( limit )\n"
            "\nShield transparent coinbase funds by sending to a shielded zaddr.  This is an asynchronous operation and utxos"
            "\nselected for shielding will be locked.  If there is an error, they are unlocked.  The RPC call `listlockunspent`"
            "\ncan be used to return a list of locked utxos.  The number of coinbase utxos selected for shielding can be limited"
            "\nby the caller.  If the limit parameter is set to zero, the -mempooltxinputlimit option will determine the number"
            "\nof uxtos.  Any limit is constrained by the consensus rule defining a maximum transaction size of "
            + strprintf("%d bytes.", MAX_TX_SIZE)
            + HelpRequiringPassphrase() + "\n"
            "\nArguments:\n"
            "1. \"fromaddress\"         (string, required) The address is a taddr or \"*\" for all taddrs belonging to the wallet.\n"
            "2. \"toaddress\"           (string, required) The address is a zaddr.\n"
            "3. fee                   (numeric, optional, default="
            + strprintf("%s", FormatMoney(SHIELD_COINBASE_DEFAULT_MINERS_FEE)) + ") The fee amount to attach to this transaction.\n"
            "4. limit                 (numeric, optional, default="
            + strprintf("%d", SHIELD_COINBASE_DEFAULT_LIMIT) + ") Limit on the maximum number of utxos to shield.  Set to 0 to use node option -mempooltxinputlimit.\n"
            "\nResult:\n"
            "{\n"
            "  \"operationid\": xxx          (string) An operationid to pass to z_getoperationstatus to get the result of the operation.\n"
            "  \"shieldedUTXOs\": xxx        (numeric) Number of coinbase utxos being shielded.\n"
            "  \"shieldedValue\": xxx        (numeric) Value of coinbase utxos being shielded.\n"
            "  \"remainingUTXOs\": xxx       (numeric) Number of coinbase utxos still available for shielding.\n"
            "  \"remainingValue\": xxx       (numeric) Value of coinbase utxos still available for shielding.\n"
            "}\n"
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Validate the from address
    auto fromaddress = params[0].get_str();
    bool isFromWildcard = fromaddress == "*";
    CBitcoinAddress taddr;
    if (!isFromWildcard) {
        taddr = CBitcoinAddress(fromaddress);
        if (!taddr.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a taddr or \"*\".");
        }
    }

    // Validate the destination address
    auto destaddress = params[1].get_str();
    try {
        CZCPaymentAddress pa(destaddress);
        /*libzcash::PaymentAddress zaddr =*/ pa.Get();
    } catch (const std::runtime_error&) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ") + destaddress );
    }

    // Convert fee from currency format to zatoshis
    CAmount nFee = SHIELD_COINBASE_DEFAULT_MINERS_FEE;
    if (params.size() > 2) {
        if (params[2].get_real() == 0.0) {
            nFee = 0;
        } else {
            nFee = AmountFromValue( params[2] );
        }
    }

    int nLimit = SHIELD_COINBASE_DEFAULT_LIMIT;
    if (params.size() > 3) {
        nLimit = params[3].get_int();
        if (nLimit < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Limit on maximum number of utxos cannot be negative");
        }
    }

    // Prepare to get coinbase utxos
    std::vector<ShieldCoinbaseUTXO> inputs;
    CAmount shieldedValue = 0;
    CAmount remainingValue = 0;
    size_t estimatedTxSize = 2000;  // 1802 joinsplit description + tx overhead + wiggle room
    size_t utxoCounter = 0;
    bool maxedOutFlag = false;
    size_t mempoolLimit = (nLimit != 0) ? nLimit : (size_t)GetArg("-mempooltxinputlimit", 0);

    // Set of addresses to filter utxos by
    set<CBitcoinAddress> setAddress = {};
    if (!isFromWildcard) {
        setAddress.insert(taddr);
    }

    // Get available utxos
    vector<COutput> vecOutputs;
    pwalletMain->AvailableCoins(vecOutputs, true, NULL, false, true);

    // Find unspent coinbase utxos and update estimated size
    BOOST_FOREACH(const COutput& out, vecOutputs) {
        if (!out.fSpendable) {
            continue;
        }

        CTxDestination address;
        if (!ExtractDestination(out.tx->getTxBase()->GetVout()[out.pos].scriptPubKey, address)) {
            continue;
        }
        // If taddr is not wildcard "*", filter utxos
        if (setAddress.size()>0 && !setAddress.count(address)) {
            continue;
        }

        if (!out.tx->getTxBase()->IsCoinBase()) {
            continue;
        }

        utxoCounter++;
        CAmount nValue = out.tx->getTxBase()->GetVout()[out.pos].nValue;

        if (!maxedOutFlag) {
            CBitcoinAddress ba(address);
            size_t increase = (ba.IsScript()) ? CTXIN_SPEND_P2SH_SIZE : CTXIN_SPEND_DUST_SIZE;
            if (estimatedTxSize + increase >= MAX_TX_SIZE ||
                (mempoolLimit > 0 && utxoCounter > mempoolLimit))
            {
                maxedOutFlag = true;
            } else {
                estimatedTxSize += increase;
                ShieldCoinbaseUTXO utxo = {out.tx->getTxBase()->GetHash(), out.pos, nValue};
                inputs.push_back(utxo);
                shieldedValue += nValue;
            }
        }

        if (maxedOutFlag) {
            remainingValue += nValue;
        }
    }

    size_t numUtxos = inputs.size();

    if (numUtxos == 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Could not find any coinbase funds to shield.");
    }

    if (shieldedValue < nFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient coinbase funds, have %s, which is less than miners fee %s",
            FormatMoney(shieldedValue), FormatMoney(nFee)));
    }

    // Check that the user specified fee is sane (if too high, it can result in error -25 absurd fee)
    CAmount netAmount = shieldedValue - nFee;
    if (nFee > netAmount) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Fee %s is greater than the net amount to be shielded %s", FormatMoney(nFee), FormatMoney(netAmount)));
    }

    // Keep record of parameters in context object
    UniValue contextInfo(UniValue::VOBJ);
    contextInfo.push_back(Pair("fromaddress", params[0]));
    contextInfo.push_back(Pair("toaddress", params[1]));
    contextInfo.push_back(Pair("fee", ValueFromAmount(nFee)));

    const int shieldedTxVersion = ForkManager::getInstance().getShieldedTxVersion(chainActive.Height() + 1);
    LogPrintf("z_shieldcoinbase shieldedTxVersion (Forkmanager): %d\n", shieldedTxVersion);

    // Contextual transaction we will build on
    // (used if no Sapling addresses are involved)
    CMutableTransaction contextualTx;
    contextualTx.nVersion = shieldedTxVersion;

    // Create operation and add to global queue
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_shieldcoinbase(contextualTx, inputs, destaddress, nFee, contextInfo) );
    q->addOperation(operation);
    AsyncRPCOperationId operationId = operation->getId();

    // Return continuation information
    UniValue o(UniValue::VOBJ);
    o.push_back(Pair("remainingUTXOs", utxoCounter - numUtxos));
    o.push_back(Pair("remainingValue", ValueFromAmount(remainingValue)));
    o.push_back(Pair("shieldingUTXOs", numUtxos));
    o.push_back(Pair("shieldingValue", ValueFromAmount(shieldedValue)));
    o.push_back(Pair("opid", operationId));
    return o;
}


UniValue z_listoperationids(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
            "z_listoperationids\n"
            "\nReturns the list of operation ids currently known to the wallet.\n"
            "\nArguments:\n"
            "1. \"status\"         (string, optional) Filter result by the operation's state state e.g. \"success\".\n"
            "\nResult:\n"
            "[                     (json array of string)\n"
            "  \"operationid\"       (string) an operation id belonging to the wallet\n"
            "  ,...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("z_listoperationids", "")
            + HelpExampleRpc("z_listoperationids", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    std::string filter;
    bool useFilter = false;
    if (params.size()==1) {
        filter = params[0].get_str();
        useFilter = true;
    }

    UniValue ret(UniValue::VARR);
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    for (auto id : ids) {
        std::shared_ptr<AsyncRPCOperation> operation = q->getOperationForId(id);
        if (!operation) {
            continue;
        }
        std::string state = operation->getStateAsString();
        if (useFilter && filter.compare(state)!=0)
            continue;
        ret.push_back(id);
    }

    return ret;
}
