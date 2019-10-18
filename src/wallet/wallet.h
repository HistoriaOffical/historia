// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2014-2018 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLET_H
#define BITCOIN_WALLET_WALLET_H

#include "amount.h"
#include "base58.h"
#include "streams.h"
#include "tinyformat.h"
#include "ui_interface.h"
#include "util.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#include "script/ismine.h"
#include "wallet/crypter.h"
#include "wallet/walletdb.h"
#include "wallet/rpcwallet.h"

#include "privatesend.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <map>
#include <set>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <boost/shared_ptr.hpp>

extern CWallet* pwalletMain;

/**
 * Settings
 */
extern CFeeRate payTxFee;
extern unsigned int nTxConfirmTarget;
extern bool bSpendZeroConfChange;
extern bool bBIP69Enabled;

static const unsigned int DEFAULT_KEYPOOL_SIZE = 1000;
//! -paytxfee default
static const CAmount DEFAULT_TRANSACTION_FEE = 0;
//! -fallbackfee default
static const CAmount DEFAULT_FALLBACK_FEE = 1000;
//! -mintxfee default
static const CAmount DEFAULT_TRANSACTION_MINFEE = 1000;
//! minimum recommended increment for BIP 125 replacement txs
static const CAmount WALLET_INCREMENTAL_RELAY_FEE = 5000;
//! target minimum change amount
static const CAmount MIN_CHANGE = CENT;
//! final minimum change amount after paying for fees
static const CAmount MIN_FINAL_CHANGE = MIN_CHANGE/2;
//! Default for -spendzeroconfchange
static const bool DEFAULT_SPEND_ZEROCONF_CHANGE = true;
//! Default for -walletrejectlongchains
static const bool DEFAULT_WALLET_REJECT_LONG_CHAINS = false;
//! -txconfirmtarget default
static const unsigned int DEFAULT_TX_CONFIRM_TARGET = 6;
static const bool DEFAULT_WALLETBROADCAST = true;
static const bool DEFAULT_DISABLE_WALLET = false;

extern const char * DEFAULT_WALLET_DAT;

//! if set, all keys will be derived by using BIP39/BIP44
static const bool DEFAULT_USE_HD_WALLET = false;

bool AutoBackupWallet (CWallet* wallet, const std::string& strWalletFile_, std::string& strBackupWarningRet, std::string& strBackupErrorRet);

class CBlockIndex;
class CCoinControl;
class COutput;
class CReserveKey;
class CScript;
class CScheduler;
class CTxMemPool;
class CWalletTx;

/** (client) version numbers for particular wallet features */
enum WalletFeature
{
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys
    FEATURE_HD = 120200,    // Hierarchical key derivation after BIP32 (HD Wallet), BIP44 (multi-coin), BIP39 (mnemonic)
                            // which uses on-the-fly private key derivation

    FEATURE_LATEST = 61000
};

enum AvailableCoinsType
{
    ALL_COINS,
    ONLY_DENOMINATED,
    ONLY_NONDENOMINATED,
    ONLY_100, // find masternode outputs including locked ones (use with caution)
    ONLY_5000, // find masternode outputs including locked ones (use with caution)    
    ONLY_PRIVATESEND_COLLATERAL
};

struct CompactTallyItem
{
    CTxDestination txdest;
    CAmount nAmount;
    std::vector<COutPoint> vecOutPoints;
    CompactTallyItem()
    {
        nAmount = 0;
    }
};

/** A key pool entry */
class CKeyPool
{
public:
    int64_t nTime;
    CPubKey vchPubKey;
    bool fInternal; // for change outputs

    CKeyPool();
    CKeyPool(const CPubKey& vchPubKeyIn, bool fInternalIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(nTime);
        READWRITE(vchPubKey);
        if (ser_action.ForRead()) {
            try {
                READWRITE(fInternal);
            }
            catch (std::ios_base::failure&) {
                /* flag as external address if we can't read the internal boolean
                   (this will be the case for any wallet before the HD chain split version) */
                fInternal = false;
            }
        }
        else {
            READWRITE(fInternal);
        }
    }
};

/** Address book data */
class CAddressBookData
{
public:
    std::string name;
    std::string purpose;

    CAddressBookData()
    {
        purpose = "unknown";
    }

    typedef std::map<std::string, std::string> StringMap;
    StringMap destdata;
};

struct CRecipient
{
    CScript scriptPubKey;
    CAmount nAmount;
    bool fSubtractFeeFromAmount;
};

typedef std::map<std::string, std::string> mapValue_t;


static inline void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (!mapValue.count("n"))
    {
        nOrderPos = -1; // TODO: calculate elsewhere
        return;
    }
    nOrderPos = atoi64(mapValue["n"].c_str());
}


static inline void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (nOrderPos == -1)
        return;
    mapValue["n"] = i64tostr(nOrderPos);
}

struct COutputEntry
{
    CTxDestination destination;
    CAmount amount;
    int vout;
};

/** A transaction with a merkle branch linking it to the block chain. */
class CMerkleTx
{
private:
  /** Constant used in hashBlock to indicate tx has been abandoned */
    static const uint256 ABANDON_HASH;

public:
    CTransactionRef tx;
    uint256 hashBlock;

    /* An nIndex == -1 means that hashBlock (in nonzero) refers to the earliest
     * block in the chain we know this or any in-wallet dependency conflicts
     * with. Older clients interpret nIndex == -1 as unconfirmed for backward
     * compatibility.
     */
    int nIndex;

    CMerkleTx()
    {
        SetTx(MakeTransactionRef());
        Init();
    }

    CMerkleTx(CTransactionRef arg)
    {
        SetTx(std::move(arg));
        Init();
    }

    /** Helper conversion operator to allow passing CMerkleTx where CTransaction is expected.
     *  TODO: adapt callers and remove this operator. */
    operator const CTransaction&() const { return *tx; }

    void Init()
    {
        hashBlock = uint256();
        nIndex = -1;
    }

    void SetTx(CTransactionRef arg)
    {
        tx = std::move(arg);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        std::vector<uint256> vMerkleBranch; // For compatibility with older versions.
        READWRITE(tx);
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
    }

    void SetMerkleBranch(const CBlockIndex* pIndex, int posInBlock);

    /**
     * Return depth of transaction in blockchain:
     * <0  : conflicts with a transaction this deep in the blockchain
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetDepthInMainChain(const CBlockIndex* &pindexRet) const;
    int GetDepthInMainChain() const { const CBlockIndex *pindexRet; return GetDepthInMainChain(pindexRet); }
    bool IsInMainChain() const { const CBlockIndex *pindexRet; return GetDepthInMainChain(pindexRet) > 0; }
    bool IsLockedByInstantSend() const;
    bool IsLockedByLLMQInstantSend() const;
    bool IsChainLocked() const;
    int GetBlocksToMaturity() const;
    /** Pass this transaction to the mempool. Fails if absolute fee exceeds absurd fee. */
    bool AcceptToMemoryPool(const CAmount& nAbsurdFee, CValidationState& state);
    bool hashUnset() const { return (hashBlock.IsNull() || hashBlock == ABANDON_HASH); }
    bool isAbandoned() const { return (hashBlock == ABANDON_HASH); }
    void setAbandoned() { hashBlock = ABANDON_HASH; }

    const uint256& GetHash() const { return tx->GetHash(); }
    bool IsCoinBase() const { return tx->IsCoinBase(); }
};

/** 
 * A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx : public CMerkleTx
{
private:
    const CWallet* pwallet;

public:
    /**
     * Key/value map with information about the transaction.
     *
     * The following keys can be read and written through the map and are
     * serialized in the wallet database:
     *
     *     "comment", "to"   - comment strings provided to sendtoaddress,
     *                         sendfrom, sendmany wallet RPCs
     *     "replaces_txid"   - txid (as HexStr) of transaction replaced by
     *                         bumpfee on transaction created by bumpfee
     *     "replaced_by_txid" - txid (as HexStr) of transaction created by
     *                         bumpfee on transaction replaced by bumpfee
     *     "from", "message" - obsolete fields that could be set in UI prior to
     *                         2011 (removed in commit 4d9b223)
     *
     * The following keys are serialized in the wallet database, but shouldn't
     * be read or written through the map (they will be temporarily added and
     * removed from the map during serialization):
     *
     *     "fromaccount"     - serialized strFromAccount value
     *     "n"               - serialized nOrderPos value
     *     "timesmart"       - serialized nTimeSmart value
     *     "spent"           - serialized vfSpent value that existed prior to
     *                         2014 (removed in commit 93a18a3)
     */
    mapValue_t mapValue;
    std::vector<std::pair<std::string, std::string> > vOrderForm;
    unsigned int fTimeReceivedIsTxTime;
    unsigned int nTimeReceived; //!< time received by this node
    /**
     * Stable timestamp that never changes, and reflects the order a transaction
     * was added to the wallet. Timestamp is based on the block time for a
     * transaction added as part of a block, or else the time when the
     * transaction was received if it wasn't part of a block, with the timestamp
     * adjusted in both cases so timestamp order matches the order transactions
     * were added to the wallet. More details can be found in
     * CWallet::ComputeTimeSmart().
     */
    unsigned int nTimeSmart;
    /**
     * From me flag is set to 1 for transactions that were created by the wallet
     * on this bitcoin node, and set to 0 for transactions that were created
     * externally and came in through the network or sendrawtransaction RPC.
     */
    char fFromMe;
    std::string strFromAccount;
    int64_t nOrderPos; //!< position in ordered transaction list

    // memory only
    mutable bool fDebitCached;
    mutable bool fCreditCached;
    mutable bool fImmatureCreditCached;
    mutable bool fAvailableCreditCached;
    mutable bool fAnonymizedCreditCached;
    mutable bool fDenomUnconfCreditCached;
    mutable bool fDenomConfCreditCached;
    mutable bool fWatchDebitCached;
    mutable bool fWatchCreditCached;
    mutable bool fImmatureWatchCreditCached;
    mutable bool fAvailableWatchCreditCached;
    mutable bool fChangeCached;
    mutable CAmount nDebitCached;
    mutable CAmount nCreditCached;
    mutable CAmount nImmatureCreditCached;
    mutable CAmount nAvailableCreditCached;
    mutable CAmount nAnonymizedCreditCached;
    mutable CAmount nDenomUnconfCreditCached;
    mutable CAmount nDenomConfCreditCached;
    mutable CAmount nWatchDebitCached;
    mutable CAmount nWatchCreditCached;
    mutable CAmount nImmatureWatchCreditCached;
    mutable CAmount nAvailableWatchCreditCached;
    mutable CAmount nChangeCached;

    CWalletTx()
    {
        Init(NULL);
    }

    CWalletTx(const CWallet* pwalletIn, CTransactionRef arg) : CMerkleTx(std::move(arg))
    {
        Init(pwalletIn);
    }

    void Init(const CWallet* pwalletIn)
    {
        pwallet = pwalletIn;
        mapValue.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false;
        strFromAccount.clear();
        fDebitCached = false;
        fCreditCached = false;
        fImmatureCreditCached = false;
        fAvailableCreditCached = false;
        fAnonymizedCreditCached = false;
        fDenomUnconfCreditCached = false;
        fDenomConfCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fChangeCached = false;
        nDebitCached = 0;
        nCreditCached = 0;
        nImmatureCreditCached = 0;
        nAvailableCreditCached = 0;
        nAnonymizedCreditCached = 0;
        nDenomUnconfCreditCached = 0;
        nDenomConfCreditCached = 0;
        nWatchDebitCached = 0;
        nWatchCreditCached = 0;
        nAvailableWatchCreditCached = 0;
        nImmatureWatchCreditCached = 0;
        nChangeCached = 0;
        nOrderPos = -1;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead())
            Init(NULL);
        char fSpent = false;

        if (!ser_action.ForRead())
        {
            mapValue["fromaccount"] = strFromAccount;

            WriteOrderPos(nOrderPos, mapValue);

            if (nTimeSmart)
                mapValue["timesmart"] = strprintf("%u", nTimeSmart);
        }

        READWRITE(*(CMerkleTx*)this);
        std::vector<CMerkleTx> vUnused; //!< Used to be vtxPrev
        READWRITE(vUnused);
        READWRITE(mapValue);
        READWRITE(vOrderForm);
        READWRITE(fTimeReceivedIsTxTime);
        READWRITE(nTimeReceived);
        READWRITE(fFromMe);
        READWRITE(fSpent);

        if (ser_action.ForRead())
        {
            strFromAccount = mapValue["fromaccount"];

            ReadOrderPos(nOrderPos, mapValue);

            nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(mapValue["timesmart"]) : 0;
        }

        mapValue.erase("fromaccount");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    //! make sure balances are recalculated
    void MarkDirty()
    {
        fCreditCached = false;
        fAvailableCreditCached = false;
        fImmatureCreditCached = false;
        fAnonymizedCreditCached = false;
        fDenomUnconfCreditCached = false;
        fDenomConfCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fDebitCached = false;
        fChangeCached = false;
    }

    void BindWallet(CWallet *pwalletIn)
    {
        pwallet = pwalletIn;
        MarkDirty();
    }

    //! filter decides which addresses will count towards the debit
    CAmount GetDebit(const isminefilter& filter) const;
    CAmount GetCredit(const isminefilter& filter) const;
    CAmount GetImmatureCredit(bool fUseCache=true) const;
    CAmount GetAvailableCredit(bool fUseCache=true) const;
    CAmount GetImmatureWatchOnlyCredit(const bool& fUseCache=true) const;
    CAmount GetAvailableWatchOnlyCredit(const bool& fUseCache=true) const;
    CAmount GetChange() const;

    CAmount GetAnonymizedCredit(bool fUseCache=true) const;
    CAmount GetDenominatedCredit(bool unconfirmed, bool fUseCache=true) const;

    void GetAmounts(std::list<COutputEntry>& listReceived,
                    std::list<COutputEntry>& listSent, CAmount& nFee, std::string& strSentAccount, const isminefilter& filter) const;

    void GetAccountAmounts(const std::string& strAccount, CAmount& nReceived,
                           CAmount& nSent, CAmount& nFee, const isminefilter& filter) const;

    bool IsFromMe(const isminefilter& filter) const
    {
        return (GetDebit(filter) > 0);
    }

    // True if only scriptSigs are different
    bool IsEquivalentTo(const CWalletTx& tx) const;

    bool InMempool() const;
    bool IsTrusted() const;

    int64_t GetTxTime() const;
    int GetRequestCount() const;

    bool RelayWalletTransaction(CConnman* connman, const std::string& strCommand="tx");

    std::set<uint256> GetConflicts() const;
};




class COutput
{
public:
    const CWalletTx *tx;
    int i;
    int nDepth;

    /** Whether we have the private keys to spend this output */
    bool fSpendable;

    /** Whether we know how to spend this output, ignoring the lack of keys */
    bool fSolvable;

    /**
     * Whether this output is considered safe to spend. Unconfirmed transactions
     * from outside keys and unconfirmed replacement transactions are considered
     * unsafe and will not be used to fund new spending transactions.
     */
    bool fSafe;

    COutput(const CWalletTx *txIn, int iIn, int nDepthIn, bool fSpendableIn, bool fSolvableIn, bool fSafeIn)
    {
        tx = txIn; i = iIn; nDepth = nDepthIn; fSpendable = fSpendableIn; fSolvable = fSolvableIn; fSafe = fSafeIn;
    }

    //Used with Darksend. Will return largest nondenom, then denominations, then very small inputs
    int Priority() const;

    std::string ToString() const;
};




/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey
{
public:
    CPrivKey vchPrivKey;
    int64_t nTimeCreated;
    int64_t nTimeExpires;
    std::string strComment;
    //! todo: add something to note what created it (user, getnewaddress, change)
    //!   maybe should have a map<string, string> property map

    CWalletKey(int64_t nExpires=0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPrivKey);
        READWRITE(nTimeCreated);
        READWRITE(nTimeExpires);
        READWRITE(LIMITED_STRING(strComment, 65536));
    }
};

/**
 * Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry
{
public:
    std::string strAccount;
    CAmount nCreditDebit;
    int64_t nTime;
    std::string strOtherAccount;
    std::string strComment;
    mapValue_t mapValue;
    int64_t nOrderPos; //!< position in ordered transaction list
    uint64_t nEntryNo;

    CAccountingEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        nCreditDebit = 0;
        nTime = 0;
        strAccount.clear();
        strOtherAccount.clear();
        strComment.clear();
        nOrderPos = -1;
        nEntryNo = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        //! Note: strAccount is serialized as part of the key, not here.
        READWRITE(nCreditDebit);
        READWRITE(nTime);
        READWRITE(LIMITED_STRING(strOtherAccount, 65536));

        if (!ser_action.ForRead())
        {
            WriteOrderPos(nOrderPos, mapValue);

            if (!(mapValue.empty() && _ssExtra.empty()))
            {
                CDataStream ss(s.GetType(), s.GetVersion());
                ss.insert(ss.begin(), '\0');
                ss << mapValue;
                ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
                strComment.append(ss.str());
            }
        }

        READWRITE(LIMITED_STRING(strComment, 65536));

        size_t nSepPos = strComment.find("\0", 0, 1);
        if (ser_action.ForRead())
        {
            mapValue.clear();
            if (std::string::npos != nSepPos)
            {
                CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), s.GetType(), s.GetVersion());
                ss >> mapValue;
                _ssExtra = std::vector<char>(ss.begin(), ss.end());
            }
            ReadOrderPos(nOrderPos, mapValue);
        }
        if (std::string::npos != nSepPos)
            strComment.erase(nSepPos);

        mapValue.erase("n");
    }

private:
    std::vector<char> _ssExtra;
};


/** 
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet : public CCryptoKeyStore, public CValidationInterface
{
private:
    static std::atomic<bool> fFlushScheduled;

    /**
     * Select a set of coins such that nValueRet >= nTargetValue and at least
     * all coins from coinControl are selected; Never select unconfirmed coins
     * if they are not ours
     */
    bool SelectCoins(const std::vector<COutput>& vAvailableCoins, const CAmount& nTargetValue, std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet, CAmount& nValueRet, const CCoinControl *coinControl = NULL, AvailableCoinsType nCoinType=ALL_COINS, bool fUseInstantSend = true) const;

    CWalletDB *pwalletdbEncryption;

    //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion;

    //! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion;

    int64_t nNextResend;
    int64_t nLastResend;
    bool fBroadcastTransactions;

    mutable bool fAnonymizableTallyCached;
    mutable std::vector<CompactTallyItem> vecAnonymizableTallyCached;
    mutable bool fAnonymizableTallyCachedNonDenom;
    mutable std::vector<CompactTallyItem> vecAnonymizableTallyCachedNonDenom;

    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    typedef std::multimap<COutPoint, uint256> TxSpends;
    TxSpends mapTxSpends;
    void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);
    void AddToSpends(const uint256& wtxid);

    std::set<COutPoint> setWalletUTXO;

    /* Mark a transaction (and its in-wallet descendants) as conflicting with a particular block. */
    void MarkConflicted(const uint256& hashBlock, const uint256& hashTx);

    void SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator>);

    /* HD derive new child key (on internal or external chain) */
    void DeriveNewChildKey(const CKeyMetadata& metadata, CKey& secretRet, uint32_t nAccountIndex, bool fInternal /*= false*/);

    bool fFileBacked;

    std::set<int64_t> setInternalKeyPool;
    std::set<int64_t> setExternalKeyPool;

    int64_t nTimeFirstKey;

    /**
     * Private version of AddWatchOnly method which does not accept a
     * timestamp, and which will reset the wallet's nTimeFirstKey value to 1 if
     * the watch key did not previously have a timestamp associated with it.
     * Because this is an inherited virtual method, it is accessible despite
     * being marked private, but it is marked private anyway to encourage use
     * of the other AddWatchOnly which accepts a timestamp and sets
     * nTimeFirstKey more intelligently for more efficient rescans.
     */
    bool AddWatchOnly(const CScript& dest) override;

public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CWallet
     *   except for:
     *      fFileBacked (immutable after instantiation)
     *      strWalletFile (immutable after instantiation)
     */
    mutable CCriticalSection cs_wallet;

    const std::string strWalletFile;

    void LoadKeyPool(int nIndex, const CKeyPool &keypool)
    {
        if (keypool.fInternal) {
            setInternalKeyPool.insert(nIndex);
        } else {
            setExternalKeyPool.insert(nIndex);
        }

        // If no metadata exists yet, create a default with the pool key's
        // creation time. Note that this may be overwritten by actually
        // stored metadata for that key later, which is fine.
        CKeyID keyid = keypool.vchPubKey.GetID();
        if (mapKeyMetadata.count(keyid) == 0)
            mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
    }

    // Map from Key ID (for regular keys) or Script ID (for watch-only keys) to
    // key metadata.
    std::map<CTxDestination, CKeyMetadata> mapKeyMetadata;

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    CWallet()
    {
        SetNull();
    }

    CWallet(const std::string& strWalletFileIn)
    : strWalletFile(strWalletFileIn)
    {
        SetNull();

        fFileBacked = true;
    }

    ~CWallet()
    {
        delete pwalletdbEncryption;
        pwalletdbEncryption = NULL;
    }

    void SetNull()
    {
        nWalletVersion = FEATURE_BASE;
        nWalletMaxVersion = FEATURE_BASE;
        fFileBacked = false;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = NULL;
        nOrderPosNext = 0;
        nNextResend = 0;
        nLastResend = 0;
        nTimeFirstKey = 0;
        fBroadcastTransactions = false;
        fAnonymizableTallyCached = false;
        fAnonymizableTallyCachedNonDenom = false;
        vecAnonymizableTallyCached.clear();
        vecAnonymizableTallyCachedNonDenom.clear();
    }

    std::map<uint256, CWalletTx> mapWallet;
    std::list<CAccountingEntry> laccentries;

    typedef std::pair<CWalletTx*, CAccountingEntry*> TxPair;
    typedef std::multimap<int64_t, TxPair > TxItems;
    TxItems wtxOrdered;

    int64_t nOrderPosNext;
    std::map<uint256, int> mapRequestCount;

    std::map<CTxDestination, CAddressBookData> mapAddressBook;

    CPubKey vchDefaultKey;

    std::set<COutPoint> setLockedCoins;

    int64_t nKeysLeftSinceAutoBackup;

    std::map<CKeyID, CHDPubKey> mapHdPubKeys; //<! memory map of HD extended pubkeys

    const CWalletTx* GetWalletTx(const uint256& hash) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    bool CanSupportFeature(enum WalletFeature wf) { AssertLockHeld(cs_wallet); return nWalletMaxVersion >= wf; }

    /**
     * populate vCoins with vector of available COutputs.
     */
    void AvailableCoins(std::vector<COutput>& vCoins, bool fOnlySafe=true, const CCoinControl *coinControl = NULL, bool fIncludeZeroValue=false, AvailableCoinsType nCoinType=ALL_COINS, bool fUseInstantSend = false) const;

    /**
     * Shuffle and select coins until nTargetValue is reached while avoiding
     * small change; This method is stochastic for some inputs and upon
     * completion the coin set and corresponding actual target value is
     * assembled
     */
    bool SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, uint64_t nMaxAncestors, std::vector<COutput> vCoins, std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet, CAmount& nValueRet, AvailableCoinsType nCoinType=ALL_COINS, bool fUseInstantSend = false) const;

    // Coin selection
    bool SelectPSInOutPairsByDenominations(int nDenom, CAmount nValueMin, CAmount nValueMax, std::vector< std::pair<CTxDSIn, CTxOut> >& vecPSInOutPairsRet);
    bool GetCollateralTxDSIn(CTxDSIn& txdsinRet, CAmount& nValueRet) const;
    bool SelectPrivateCoins(CAmount nValueMin, CAmount nValueMax, std::vector<CTxIn>& vecTxInRet, CAmount& nValueRet, int nPrivateSendRoundsMin, int nPrivateSendRoundsMax) const;

    bool SelectCoinsGroupedByAddresses(std::vector<CompactTallyItem>& vecTallyRet, bool fSkipDenominated = true, bool fAnonymizable = true, bool fSkipUnconfirmed = true, int nMaxOupointsPerAddress = -1) const;

    /// Get 1000HTA output and keys which can be used for the Masternode
    bool GetMasternodeOutpointAndKeys(COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet, const std::string& strTxHash = "", const std::string& strOutputIndex = "");
    /// Extract txin information and keys from output
    bool GetOutpointAndKeysFromOutput(const COutput& out, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet);

    bool HasCollateralInputs(bool fOnlyConfirmed = true) const;
    int  CountInputsWithAmount(CAmount nInputAmount) const;

    // get the PrivateSend chain depth for a given input
    int GetRealOutpointPrivateSendRounds(const COutPoint& outpoint, int nRounds = 0) const;
    // respect current settings
    int GetCappedOutpointPrivateSendRounds(const COutPoint& outpoint) const;

    bool IsDenominated(const COutPoint& outpoint) const;

    bool IsSpent(const uint256& hash, unsigned int n) const;

    bool IsLockedCoin(uint256 hash, unsigned int n) const;
    void LockCoin(const COutPoint& output);
    void UnlockCoin(const COutPoint& output);
    void UnlockAllCoins();
    void ListLockedCoins(std::vector<COutPoint>& vOutpts);
    void ListProTxCoins(std::vector<COutPoint>& vOutpts);

    /**
     * keystore implementation
     * Generate a new key
     */
    CPubKey GenerateNewKey(uint32_t nAccountIndex, bool fInternal /*= false*/);
    //! HaveKey implementation that also checks the mapHdPubKeys
    bool HaveKey(const CKeyID &address) const override;
    //! GetPubKey implementation that also checks the mapHdPubKeys
    bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const override;
    //! GetKey implementation that can derive a HD private key on the fly
    bool GetKey(const CKeyID &address, CKey& keyOut) const override;
    //! Adds a HDPubKey into the wallet(database)
    bool AddHDPubKey(const CExtPubKey &extPubKey, bool fInternal);
    //! loads a HDPubKey into the wallets memory
    bool LoadHDPubKey(const CHDPubKey &hdPubKey);
    //! Adds a key to the store, and saves it to disk.
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey) override;
    //! Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CKey& key, const CPubKey &pubkey) { return CCryptoKeyStore::AddKeyPubKey(key, pubkey); }
    //! Load metadata (used by LoadWallet)
    bool LoadKeyMetadata(const CTxDestination& pubKey, const CKeyMetadata &metadata);

    bool LoadMinVersion(int nVersion) { AssertLockHeld(cs_wallet); nWalletVersion = nVersion; nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion); return true; }
    void UpdateTimeFirstKey(int64_t nCreateTime);

    //! Adds an encrypted key to the store, and saves it to disk.
    bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret) override;
    //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    bool AddCScript(const CScript& redeemScript) override;
    bool LoadCScript(const CScript& redeemScript);

    //! Adds a destination data tuple to the store, and saves it to disk
    bool AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Erases a destination data tuple in the store and on disk
    bool EraseDestData(const CTxDestination &dest, const std::string &key);
    //! Adds a destination data tuple to the store, without saving it to disk
    bool LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Look up a destination data tuple in the store, return true if found false otherwise
    bool GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const;

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CScript& dest, int64_t nCreateTime);

    bool RemoveWatchOnly(const CScript &dest) override;
    //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CScript &dest);

    //! Holds a timestamp at which point the wallet is scheduled (externally) to be relocked. Caller must arrange for actual relocking to occur via Lock().
    int64_t nRelockTime;

    bool Unlock(const SecureString& strWalletPassphrase, bool fForMixingOnly = false);
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
    bool EncryptWallet(const SecureString& strWalletPassphrase);

    void GetKeyBirthTimes(std::map<CTxDestination, int64_t> &mapKeyBirth) const;
    unsigned int ComputeTimeSmart(const CWalletTx& wtx) const;

    /** 
     * Increment the next transaction order id
     * @return next transaction order id
     */
    int64_t IncOrderPosNext(CWalletDB *pwalletdb = NULL);
    DBErrors ReorderTransactions();
    bool AccountMove(std::string strFrom, std::string strTo, CAmount nAmount, std::string strComment = "");
    bool GetAccountPubkey(CPubKey &pubKey, std::string strAccount, bool bForceNew = false);

    void MarkDirty();
    bool AddToWallet(const CWalletTx& wtxIn, bool fFlushOnClose=true);
    bool LoadToWallet(const CWalletTx& wtxIn);
    void SyncTransaction(const CTransaction& tx, const CBlockIndex *pindex, int posInBlock) override;
    bool AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlockIndex* pIndex, int posInBlock, bool fUpdate);
    CBlockIndex* ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false);
    void ReacceptWalletTransactions();
    void ResendWalletTransactions(int64_t nBestBlockTime, CConnman* connman) override;
    std::vector<uint256> ResendWalletTransactionsBefore(int64_t nTime, CConnman* connman);
    CAmount GetBalance() const;
    CAmount GetUnconfirmedBalance() const;
    CAmount GetImmatureBalance() const;
    CAmount GetWatchOnlyBalance() const;
    CAmount GetUnconfirmedWatchOnlyBalance() const;
    CAmount GetImmatureWatchOnlyBalance() const;

    CAmount GetAnonymizableBalance(bool fSkipDenominated = false, bool fSkipUnconfirmed = true) const;
    CAmount GetAnonymizedBalance() const;
    float GetAverageAnonymizedRounds() const;
    CAmount GetNormalizedAnonymizedBalance() const;
    CAmount GetDenominatedBalance(bool unconfirmed=false) const;

    bool GetBudgetSystemCollateralTX(CWalletTx& tx, uint256 hash, CAmount amount, bool fUseInstantSend, const COutPoint& outpoint=COutPoint()/*defaults null*/);

    /**
     * Insert additional inputs into the transaction by
     * calling CreateTransaction();
     */
    bool FundTransaction(CMutableTransaction& tx, CAmount& nFeeRet, bool overrideEstimatedFeeRate, const CFeeRate& specificFeeRate, int& nChangePosInOut, std::string& strFailReason, bool includeWatching, bool lockUnspents, const std::set<int>& setSubtractFeeFromOutputs, bool keepReserveKey = true, const CTxDestination& destChange = CNoDestination());

    /**
     * Create a new transaction paying the recipients with a set of coins
     * selected by SelectCoins(); Also create the change output, when needed
     * @note passing nChangePosInOut as -1 will result in setting a random position
     */
    bool CreateTransaction(const std::vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRet, int& nChangePosInOut,
                           std::string& strFailReason, const CCoinControl *coinControl = NULL, bool sign = true, AvailableCoinsType nCoinType=ALL_COINS, bool fUseInstantSend=false, int nExtraPayloadSize = 0);
    bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, CConnman* connman, CValidationState& state, const std::string& strCommand="tx");

    bool CreateCollateralTransaction(CMutableTransaction& txCollateral, std::string& strReason);
    bool ConvertList(std::vector<CTxIn> vecTxIn, std::vector<CAmount>& vecAmounts);

    void ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& entries);
    bool AddAccountingEntry(const CAccountingEntry&);
    bool AddAccountingEntry(const CAccountingEntry&, CWalletDB *pwalletdb);

    static CFeeRate minTxFee;
    static CFeeRate fallbackFee;
    /**
     * Estimate the minimum fee considering user set parameters
     * and the required fee
     */
    static CAmount GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool);
    /**
     * Estimate the minimum fee considering required fee and targetFee or if 0
     * then fee estimation for nConfirmTarget
     */
    static CAmount GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool, CAmount targetFee);
    /**
     * Return the minimum required fee taking into account the
     * floating relay fee and user set minimum transaction fee
     */
    static CAmount GetRequiredFee(unsigned int nTxBytes);

    bool NewKeyPool();
    size_t KeypoolCountExternalKeys();
    size_t KeypoolCountInternalKeys();
    bool TopUpKeyPool(unsigned int kpSize = 0);
    void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool, bool fInternal);
    void KeepKey(int64_t nIndex);
    void ReturnKey(int64_t nIndex, bool fInternal);
    bool GetKeyFromPool(CPubKey &key, bool fInternal /*= false*/);
    int64_t GetOldestKeyPoolTime();
    void GetAllReserveKeys(std::set<CKeyID>& setAddress) const;

    std::set< std::set<CTxDestination> > GetAddressGroupings();
    std::map<CTxDestination, CAmount> GetAddressBalances();

    CAmount GetAccountBalance(const std::string& strAccount, int nMinDepth, const isminefilter& filter, bool fAddLocked);
    CAmount GetAccountBalance(CWalletDB& walletdb, const std::string& strAccount, int nMinDepth, const isminefilter& filter, bool fAddLocked);
    std::set<CTxDestination> GetAccountAddresses(const std::string& strAccount) const;

    isminetype IsMine(const CTxIn& txin) const;
    /**
     * Returns amount of debit if the input matches the
     * filter, otherwise returns 0
     */
    CAmount GetDebit(const CTxIn& txin, const isminefilter& filter) const;
    isminetype IsMine(const CTxOut& txout) const;
    CAmount GetCredit(const CTxOut& txout, const isminefilter& filter) const;
    bool IsChange(const CTxOut& txout) const;
    CAmount GetChange(const CTxOut& txout) const;
    bool IsMine(const CTransaction& tx) const;
    /** should probably be renamed to IsRelevantToMe */
    bool IsFromMe(const CTransaction& tx) const;
    CAmount GetDebit(const CTransaction& tx, const isminefilter& filter) const;
    /** Returns whether all of the inputs match the filter */
    bool IsAllFromMe(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetCredit(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetChange(const CTransaction& tx) const;
    void SetBestChain(const CBlockLocator& loc) override;

    DBErrors LoadWallet(bool& fFirstRunRet);
    void AutoLockMasternodeCollaterals();
    DBErrors ZapWalletTx(std::vector<CWalletTx>& vWtx);
    DBErrors ZapSelectTx(std::vector<uint256>& vHashIn, std::vector<uint256>& vHashOut);

    bool SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& purpose);

    bool DelAddressBook(const CTxDestination& address);

    bool UpdatedTransaction(const uint256 &hashTx) override;

    void Inventory(const uint256 &hash) override
    {
        {
            LOCK(cs_wallet);
            std::map<uint256, int>::iterator mi = mapRequestCount.find(hash);
            if (mi != mapRequestCount.end())
                (*mi).second++;
        }
    }

    void GetScriptForMining(boost::shared_ptr<CReserveScript> &script) override;
    void ResetRequestCount(const uint256 &hash) override
    {
        LOCK(cs_wallet);
        mapRequestCount[hash] = 0;
    };
    
    unsigned int GetKeyPoolSize()
    {
        AssertLockHeld(cs_wallet); // set{Ex,In}ternalKeyPool
        return setInternalKeyPool.size() + setExternalKeyPool.size();
    }

    bool SetDefaultKey(const CPubKey &vchPubKey);

    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, CWalletDB* pwalletdbIn = NULL, bool fExplicit = false);

    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion() { LOCK(cs_wallet); return nWalletVersion; }

    //! Get wallet transactions that conflict with given transaction (spend same outputs)
    std::set<uint256> GetConflicts(const uint256& txid) const;

    //! Flush wallet (bitdb flush)
    void Flush(bool shutdown=false);

    //! Verify the wallet database and perform salvage if required
    static bool Verify();
    
    /** 
     * Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const CTxDestination
            &address, const std::string &label, bool isMine,
            const std::string &purpose,
            ChangeType status)> NotifyAddressBookChanged;

    /** 
     * Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const uint256 &hashTx,
            ChangeType status)> NotifyTransactionChanged;

    /** Show progress e.g. for rescan */
    boost::signals2::signal<void (const std::string &title, int nProgress)> ShowProgress;

    /** Watch-only address added */
    boost::signals2::signal<void (bool fHaveWatchOnly)> NotifyWatchonlyChanged;

    /** IS-lock received */
    boost::signals2::signal<void ()> NotifyISLockReceived;

    /** ChainLock received */
    boost::signals2::signal<void (int height)> NotifyChainLockReceived;

    /** Inquire whether this wallet broadcasts transactions. */
    bool GetBroadcastTransactions() const { return fBroadcastTransactions; }
    /** Set whether this wallet broadcasts transactions. */
    void SetBroadcastTransactions(bool broadcast) { fBroadcastTransactions = broadcast; }

    /* Mark a transaction (and it in-wallet descendants) as abandoned so its inputs may be respent. */
    bool AbandonTransaction(const uint256& hashTx);

    /* Returns the wallets help message */
    static std::string GetWalletHelpString(bool showDebug);

    /* Initializes the wallet, returns a new CWallet instance or a null pointer in case of an error */
    static CWallet* CreateWalletFromFile(const std::string walletFile);
    static bool InitLoadWallet();

    /**
     * Wallet post-init setup
     * Gives the wallet a chance to register repetitive tasks and complete post-init tasks
     */
    void postInitProcess(CScheduler& scheduler);

    /* Wallets parameter interaction */
    static bool ParameterInteraction();

    /* Initialize AutoBackup functionality */
    static bool InitAutoBackup();

    bool BackupWallet(const std::string& strDest);

    /**
     * HD Wallet Functions
     */

    /* Returns true if HD is enabled */
    bool IsHDEnabled();
    /* Generates a new HD chain */
    void GenerateNewHDChain();
    /* Set the HD chain model (chain child index counters) */
    bool SetHDChain(const CHDChain& chain, bool memonly);
    bool SetCryptedHDChain(const CHDChain& chain, bool memonly);
    bool GetDecryptedHDChain(CHDChain& hdChainRet);

    void NotifyTransactionLock(const CTransaction &tx) override;
    void NotifyChainLock(const CBlockIndex* pindexChainLock) override;

    std::vector<std::string> RecordProposalAddressesTestnet = {
        "TvAsYoCAdqSuG49trg2ezxuNpwuFaz83yR",
    };

    std::vector<std::string> RecordProposalAddressesMainnet = {
        "HDpWauYhaS6zpQtEcgiK1LupsHLYSPM1nL",
        "HEHCCpDVqi3Wbg1Zgjpd89UgiHJq4virXY",
        "HJ2TGMtpN9T41HmiS1Uqq9dyL8G3bB6pdk",
        "HEtqYT5Cbe9GuyqUEMacGRVUs9xb4yBzyd",
        "HF9NFN8Tbkiqrjviqq4ma9CiA3tpQy7NHc",
        "HGzGwi7D8DuLWj46w5jr16j9hCVey9tiKe",
        "HSrQxGVMTZbeD5CCZxCFAhB1nd7GN1213Q",
        "HQvWrvN3BY18LT8u3YJiAYKELyMJRrBkYm",
        "HSCSEzrxMB7x8BhmgHgAVyf1CFXV28Tfey",
        "H8wHWWndpc3YaSksUPprQ1VKEQTRX8fYtP",
        "HVfvsU3eQWzjRQ1ZWCKpGqHdqYtFZ9gR9V",
        "H9zLnsQazCWjzdQgMutpZagU7K5i7uTYCY",
        "HV8qUR12tuJxrHv2Q9KezLqxBSPF69faMK",
        "HNcu19SgmFGjuk5Ps1nw84VmG7zv3emgyK",
        "HRyq83wWdGryodnvVYjTVqidA6HtaMTLwj",
        "HKYXESXyQ5hgrQiaFaPuzA5EbZuGqZHj9R",
        "HN1nWC2jXh6MSVvohjcFCA1Dd3Wb8rHwAM",
        "HN2uZkmeZSzr4SNjzwNDtQwvAm3JrmG4X1",
        "HDxgpeDeAy9ENFwG2mgs9Hvta7dcdbPyL7",
        "HAyU7CFAr8dQM7aidYQZMiPVbE5YCj7TU2",
        "HMmTUMfDfYRMGENq1Vu2bQJ6U2FUHNxj5W",
        "H9mHYpwfu3cKevSVRZ8umrbukdH6isKTVN",
        "HHeZuqmJaV8bsrENpFXGpAgoVEZsHnRkL4",
        "H7QSpgPrbCMovj5Ny7i63xWZEDJFXcNhtm",
        "HCqz8xuP9H3JbQMoNg1fmcrMEGbM7Pn9tj",
        "HFFSZCjmUiHYxUqeh2Vjo9JjTeQ39brXE4",
        "HKt83EEK77pKaJJUFhHzdLKgoSTmtNVcy7",
        "HMtcJt8KAD9eh4bAybMWK4GRLSkFw1cife",
        "HRKCqqAJvBBtxigm9xZg6dVyvbXeESEuMf",
        "HP9w3vkuvaf9Q4rqDz2tXsJhsQ1iSUEZ64",
        "HBz2gPPDtessDAy2bzwMkznuGidxfqMmWM",
        "HTHMoCf8ZoD7AfELh4E1pX1K9avb3rT7af",
        "HQitra1zZvYxZmuKGFFJTtYa75bJ7fmCuh",
        "HUKp5CP4kXN8Ez1pnWLKg1sQ5ZhNEiaQQS",
        "HPgyr6JVecCDw7hyNJg4miL6ZfMUqyo4ip",
        "H8eCVobmNSBceZrygqRsdypDJGvEBJJb44",
        "HLdiF7v5ma9xTBjQNNqzUDvzJq5dgXbjK8",
        "HAKFaLq4x9fY4Y8TfZcvGxDM8HXNdoqMsw",
        "HTKN1su9rsk7564vanStTzL1M6XJN45W52",
        "HEutX7Tr81xGSSWEzopHxKEusrC2Fb5StT",
        "HCVmSH1bzjjaFWsZtPVRwE7eGJGgb7XShp",
        "HNVMYnznwRqD7uKRJ65DhwYVmGH5M1DYb4",
        "HBn23kBHrpu7ADSybtmLPkNuBaDtLmxRQT",
        "HQEbq2LD8eChuhP7P8UMw2z63A1sKdx8Vv",
        "HTM1biwARfFgdfuS4v8PZpfi5vAkSSuQAt",
        "HJb3TSMKdcLX8Mnyz5E5wpgwxLUvK6ZXyG",
        "H6mbnr3VzUuZF8vNgoCrrYg31ZXdfA6QZ4",
        "HTdA2z8W8xNWuGHKu9RtUNWcUCPBvFsbco",
        "H9Higd4q3KvaA81wJxkKK8bY7WCMhZZ55g",
        "HDPUPb2PpNUZDYuEN8xkyuzHYtxKHfFZNh",
        "HG4YN1nic3y8GHKPUZiDbQfxViPFjutdPf",
        "HQR83KDmqe8jYpVuYqRSUMhVGMx3asPvNu",
        "H7zo5rFLvbP8cLp52r4AxzoDgwgftqdVCA",
        "HRytN8S3hRhrqEeDiK7p4u8KHXbGhhbEhL",
        "HDDfXHinpg8JRH17eVKzHisL31MYWbmV5i",
        "HAgz79eUZWpZxc3N9yfXFCtUw41pZoW5RT",
        "HELKuho9bTi1WC5momFDjFgtrfnnQbxWfz",
        "HB7h7YrU1iu5W3BQxGCZWznaKKF41wnkuF",
        "HAqAnwyD3KME6j2HqHkhhqLnMYqKKpXR5n",
        "H9aHvUSV22Ucqr6Fm9bgwYk2CKY6ffaUbA",
        "HQPondxJ5ZysUKfFNnMs3PYGpyDitqAa73",
        "H7W3K8YbcbZqzhEfX4tdF3WGmrTMmSBC7F",
        "H7nfrXXXSq9YjytZ6Xrr28UHs8ceJMGR6H",
        "HQH8TbfLcjDDitgkUyyi3UtPmroyFMJpXS",
        "HF4supU5QYsgyM6L4qWdx2EHcuKE6KVtWR",
        "H8ypWgxoixW3kTyg1n7UwcnNtiNEPKFbEF",
        "HFe4QBJ8tJmKDudqcfJgKCwQZsdfwE8M7U",
        "H8Qe4NgEyv9rJRVtNUj1HGpijXqRsmj38w",
        "HDwz9pjnBRnQ9Wv3p1Sx1asD74Hw43HgdB",
        "HQMLMi1g1o7qB76c3J9cNLWVtE4aWULf9N",
        "HCzwXxYovzxJZ4i3wGuS7DxtRRiPy7pELx",
        "H8Ah4cRx2hziBdR8aHxqEcunUCaZd3GMnR",
        "HQPrtdi7srbFEpSUT9K8jrtHaYqGammQCc",
        "HKmsNm9NadnbxDkkCNUyr9zu1bkpcokrdM",
        "HFhQ9PZDAxMaiA9Mb8ZxkY2DFCL9TCYSnX",
        "HM1pEjpptyG42NFtCCwKECshwTHGBvsAdd",
        "H7H4uD6cPJ9NXtvaGXkZVYYNtVAL5wAtcS",
        "HViLHVYMdqyAVdtygB8v8sz72eH7W9YEiA",
        "H77Gf12mM2WWsnrNLzJEyXj1DHtksHi8Fp",
        "HQ27s5KnBbDwA52tR3AhQFxYxeWopdVNSw",
        "HBiYGzD69uwDaHKBDk5E1KKhQNXUnj2EhG",
        "HSwBr3UcajUHCHiq4sJa3XvqoBedi9HdAF",
        "HPtLxXnxhRtjM3wEZcCqEDDyK3HszkMEJf",
        "H7fvv6MrpgEWdtMPbDya1wLQhU7CDqPGVn",
        "H9JiUZzcg2Ej7VhJzUjWcHsjMrVhNcSW3e",
        "HJ3SEruY28NgdNYPGzCrttqyMWe6NftHTv",
        "HA77SXebtycrWWmw2vQpKFxtaqBKERQ3Uu",
        "HDCx6jPanZydqgjEfeRHGapjZP8AfxoAMg",
        "HTPSzr9YvsdXeU2UTnHCJ8bDnubBMjRfo4",
        "HQqfStfszcZFV25uDwnV54qAGFr7a1Z3oK",
        "HQ5bQAYfSsBry8VNy7XsUkJVFkDAhzWV1F",
        "H8sCu8Fhs4vVbjhpuErsoXgAcZmRVenGoL",
        "HEcyYVWg2TzhTRmHvzou3nWBKh5WYgDNAS",
        "HFNDwWxHdsmrJ9TGLU8HManpqca8tJSkhF",
        "H8vrTzfgveZmYC5DYgjLr8D2e4B91zuLjj",
        "H8CBHcwPpvPPNQBmQuf8UCtV8UmmZjqzTx",
        "HMgBBdGbrxWMVGBYrcQvEh9wV5838nGdf8",
        "HSt5xhc2oNgxJKwBEjFdhYuhyWnvXJwsLn",
        "HADc4ZVhTm79AJHnRr6C3ESjnVfRWVR25B",
        "HDKbYrcPdHRG7q57ygyyyJ85fRNj1sPMRy",
        "HJSwcyfUwKRhVVeyirz8zRoCZJGkAbcWGz",
        "H9wXDm8C2xswz3xrSpXCALHR5QftAgin1X",
        "HLsG8hFZR2cF6rYbudrsqQDTqwtZzyPSom",
        "HLGHpH8ticxeeMoTiXGjhrw7HT1SDULskZ",
        "HALrwi8yarph3uCKtxczeByYX1p6aw4Mg3",
        "HMu54BxuYdpGhVNZbJbPftL1Xpwr17rJWn",
        "H95rq5RAFtsJrjrG1XboNJNAzXtpKBy7bB",
        "HVgX7cjhqKtCYYL1no1CMvjVfLsRHHAnG2",
        "H7mNsXhxZeh51PmrfXpAZhPRej5EydzAfZ",
        "HGYhgc78FCzGYjHGj2j5P34XsWiDoMtBNF",
        "HH8ZhN4qU1bHAAHpgeVUS2amrjwQiBY1Qv",
        "HAMkKcxgyktKY67tNan2ZkC2f9PoPbZwgN",
        "HAuo3yfSWDwkSGQ5HqRujX7yjhF6CtAZRq",
        "HG1VJYN8wquZ3sJBAHPpsgyKzetGGCCJic",
        "HKVf9Zdw2j63VRnpBzt9jY8xA6Fd52CtQB",
        "HAZGwLSaLCqUBqPcsQYaVYoCUKG5ay2amH",
        "HNTbv7askYsjG9b5AAQ7a11TNUi4fhYQMh",
        "HShoMNTcKHdzaJzqEShrczcMdJuoBVUkZy",
        "HEBt6ZdmSLMbKECncoVkC4cscGe7vokRJZ",
        "HCtfbpAwa98pPCXbNrwGMuMrSKTqWNSPNx",
        "HA56n1nvDQT8WsWPuVTseeKSEx1vCXga1J",
        "HGvZYRGrLL7D6Cq1tW35wdzcxwPz7DLCDC",
        "H7ow3anU4jbeTvnwXmBkRn1diTQYyMxKCk",
        "HUgi9BruMufx7MkaMTk197U584KQm8yBwe",
        "HL4LEsjzyxjk5HNjFXbKjZ5ZyyD22mDL2d",
        "HAURpD9sTQHfGvvwxz9jnkLvnDe3v3oPMZ",
        "HFBoowftPLzZ7figVTS2w9FM26YFiPwJSF",
        "HUxVGEky5BW2VetbxoanfCGbDmJ5rA9qFN",
        "HUc8XxmkUrdDvrfpuj7yvDjoQ4ufv6Kmdu",
        "HARZLL8NGedgoswyGaUTmTJDVHMCPGUYdw",
        "HMGTTymbWZ1LaTXgLUiRtwZtt22WrKzmYh",
        "HVbeSepMMpdnWMEwMVaqEMeXZAxa11syM8",
        "HEHxHFEZEE8eU32X1Ukez5oRcW3fnmSyup",
        "HHCNLag1uUMjfZrkXVUSHHYLbtyYZED8Kp",
        "HL9qA3FSqEwDC7FPK2FqpUjjVtq5DKLz46",
        "HVgZbjfGSS12PgHULnz2Dqg5gycVftFfBy",
        "HU7gZTXjWyji8Hc6uPiHpQ8wmpZuD2fKcu",
        "HTdogosqm4vtp5Qffbz24cUt5TfNAqTxGG",
        "HSJPxUEamXR91ShBWksFTJN43ydYQHc6oU",
        "HFCys1igbytKeUMig5x8kNZKG7KEZquLxT",
        "HEAqf4dXCPHkzSqD9PJtBg4P8HtBkMCNkW",
        "HMop7RPVChTFsT8QqiMKN9j2do37LjmwKu",
        "HT7V8i4yq2eWwFTYJNefbqVM18KPnVi644",
        "H6ZpYFBQQpr7kqHVU7bQXxryatXpPNRiva",
        "HMXSN3nGdaq7RmKGYgc3UxeCaKcaz8jACT",
        "HT8Zrqc5ssdgTUm8KESkJcqnyio4cngoFb",
        "HTekztjXmj82HgGxNhEYLDhMNevmu7CoUJ",
        "HU82Jd8zrG1EvJ4ALd9D5soqTDCJ6PYXU4",
        "HRvPocaaFEkNZksrj3sBrqvVMRMW8HTcZt",
        "H9kuPPhzdEKwQNdXZDhWZ4rXiDWcPztX4b",
        "H6uYK91FEB7EKE5aa8NPwc9sJ32srWn5Qb",
        "H8sGUAgCJSQVnpL9WyMjobQyWEDih2a2Qr",
        "H7sPiaEqx5oSPjGfVnXXQmyLcythMxGcu3",
        "HKXEc6WVFmLNeKwWsFQDqFd9ebL2cNRmwf",
        "H9ULJX7MeQjbJ4MopoSWM6CWNZ2mE4mSZr",
        "H8AsffpaFKRu4MZwR4iDyNeHKmLXj4gs3P",
        "HJ2hjrLLE3oAEbsuYctVW1fKSfPwbh5dsG",
        "HAor9VhgBWaHvBW7r9mYv4gETkbbHXhJxK",
        "HCeCSGQky4KC6BBS2bqeEtMxvjRsy2L8DD",
        "HJwobssRo1Nc6ZGQRySVG4Y6iTeayKyCco",
        "HFKynXV1fqwrodxRWcdYcutb98FeBg7ph2",
        "HKFwGj2u4pnBXhxArXDqBE7JEKk4ChTfh3",
        "HMXSzMGx8cYAdeg5oQpUCPKaGJccMft483",
        "H8npw7gEHJm4R98LFubokxpMy46Zy4FLAv",
        "HVNjhsJacoWW6eAvLDJdEbaBHM95iHUdpT",
        "HGe7wAiSWudVsMy2cvnh6KDvtQ8LETycaY",
        "HQ8jRbav1jse3b4y5RSc9dZzNBLBjjZCPd",
        "HCVb5tMfHtu9tv8JU9bGf1aCpDVgEcA6rc",
        "H9kSX7SRU6uREnAYuEKVssSJ3hig1SKPDD",
        "HT7rrDuZPZtmCYgtnP6R4WHwUX19ujb54X",
        "HHrqFquYjY3ibrwg7gVkT19YXGB5X4y5ba",
        "H9deGJhGGjgnsAcuEweZxAiH3KcJqsf7iq",
        "HJswo43B3aKZDjPucH267SiCcuZc3C6TVC",
        "H9uLDptW3BLMGT24t5R88w1VxNV3txBPHL",
        "HTZ4DFwaqPRyUQKZ7xVCsnnSzfNa9yRahi",
        "HBxNHX9uMffCxWNPknBw5X19Y46RNHPvx9",
        "HPCr8gcNk6mvmbzbfmdqSSDTz3gxzqU6ta",
        "HFmjFcWYwTTyuZH6dJivdaGi1kD45Uiud7",
        "H6bnUp1c3m69UayRp29LERbbgaKG9Qdw3T",
        "HRWCHXxr2UXhQouqTjdXNn7LR2pXqusE26",
        "HJG76znAxL7rDPN2C4ubDhNmXKHxQeCgdz",
        "HGeJA6TewTnhrCnqiKrddAYryX81cx7XJn",
        "HUjo7FmpPf68t5bx9kibRHbNbfP9t5z9Hp",
        "HDvx8JYy42CqoT5RnZJ1im36wB6stfcn75",
        "HDmXbFWxFSbAWT9TySQSRQBvtmHVKrBfeD",
        "H9dxLxUgwLu7Uruj2hzc51xeYBaopAjJod",
        "HTbrkw5Px9dGW3CXJvw1e4XSbn3xZmQfvp",
        "HDEZyUaZ3BddsASckZqJHCi63gwJQyJGwm",
        "H9wCYSyuwLE1t3sfAnyfWdhJST8jsDgTJi",
        "HKbUvmtSerXFzWJih2UZ94MGNVyYBz8pdw",
        "HCxkf6od7hnF1yzQVL9MPdEs2xMCMPFs3F",
        "HQCrpHqjLYhW415AvTXz9RwXjHcdWU5upq",
        "H8EUYFTX8AY7Xkd9eCRJUaGyS3TJvzqoaR",
        "H73MkRJFGcfpZKe2hftrjbvuPQFomSsedc",
        "HVhYwavEbuvdNbkr2fzZdxrjpKj2FvE3P7",
        "HFaLrRobkuiA7gsjAw8FGqGAYr9qPLNRpa",
        "HVatZ6MFQQnhahVsKYTFYNwhegSw1Hnf3V",
        "HCCawwJws58dfMfRguWkNypdkBKPJ7Ezmw",
        "HCVmA8H9T66ECnuGDxPTphn1CHbwdyTn7L",
        "HFz3rP6nAWc5dTs3P7bruKnjodhDMcoo5j",
        "HUCby9pDoapUjWNUBhutTkhbkmuBw8fpaa",
        "HQiBsj21WSNV21jexbtk3jE9zwgmLDMpL9",
        "HA34eozH53B4G4mx966ZceRgNKQdgZmjBJ",
        "HR6JXCCUcnJwGsMPUSUPzncrMpNR4nmFc2",
        "HPsWJeZCP3s1xRXKeyCrDhFEttt5djwMLX",
        "HLDG3SwYx9MVFaX8dJJ7ResZgYgJLhbax3",
        "HL9KjbQiZPYuaK6X3KbTzWwqUL8Yz9pXwY",
        "HQyYpGLYwqx4gRE6yHFQUuuE5Jdv6Pybhc",
        "H8Xo8GAyt8EjaZy36GbpswVTgjFC4sQ8b6",
        "HQqZami7DCdK7riJAofV1RdSvkou8Dez2u",
        "HFgidgzMEMD2mqE6sP5jHqsfxUvpMD5DBa",
        "H9joGRYvieh1EYRebNuzQEqx3tnMGd5ULG",
        "HBLdF6VDs3hiAVoMzpMmpM5JabKhvHBmsy",
        "HEXtfJsM1zbw3zW4HcuUebuqgWCWrYki9R",
        "HV6oAutnLkdCiXCZ7bPvx9y6AyuT566J6L",
        "H9py152bb39JU18p58SQJrvxb3Zr3LXmRL",
        "H81RRFGXXmo2a2dWJc7B3SicFUBwgUVVJn",
        "HRiZu7EMifpkkYGmG68Fw4cxmBQU1USA9N",
        "H8pyrbBvNQDucQnAT8dF67tVvqp81YJtzM",
        "HNTjECvmBEoB5fvsUh1k4KHmxfnx66Gwep",
        "HKtufiqutt4RYqRbD9guLKufRMGxrutHvm",
        "HHmN3R46qEoJQq86LJQxU16H7KTAnUpxVL",
        "HP72uHdm67hZBofiaRgE7d7FXuGtNaVMyc",
        "HNBo7xaGHVnHhTDQh6f2FYQhjigF9ddpyT",
        "HPhHqh59u4WE2tAcU6Utpqi7QDb9B8guLX",
        "HC36KWieNUcQPudW2g431PpRNVQmFkfzgr",
        "H6aUZqRD1YPWyiTAE8H7s6Y4RjZfuqphbR",
        "HSAMMGVrJcdQEM8XoN4mbahE9ocJBZVzif",
        "HEujmWcWoU1B3yd3kFYxBtqDqvhVs9JnY1",
        "HAuL4XsQWxhhMfBm6VgPW1fDqZwYJ6yhDr",
        "HCWLfvc74TSqU7aHazg3wWCk5MTgfMo7Hg",
        "HRPcVLAJqWWNGaZiSkJBE62rBjRCHgCbFR",
        "HGRpd86Eiqy3FDt75nfr9uEj4QroUeqb8y",
        "HB3Zd64QwQxPq4mUWJR8AhLwKwpDQQ6F1v",
        "HGaKHftRDULVhZuiwmm3JmCEvKdYhgP7RV",
        "HJUxVb4wixGjvh3pRkzdLcYXHW4oLEF27T",
        "HPVTrzs54ZAmWvN8tQ13SUvVMkKc9hr3gf",
        "H9fAFCAB7HgY976CE9PPrDTBUTUQ87dCCP",
        "HEC2GdpnGkQP5b6d7JWaooQ3xXmAbk4Rxi",
        "HHqkbZLWhUAyCZg9QdrJ24ymUHyojzJw9a",
        "HVecFMsMH5VwcQG68CUEVt65jhEQPE84V9",
        "HUgjmiceW5kTcbB4W9Jiu8A1Gg85twkM7A",
        "HPqGPzgnYFTECeGSthafWDdjD2PSUd9WUN",
        "HVo65cBrc7XEZDkevMXE6bKay3eSyF3Byu",
        "HKP46RGJDGcBcPpRUyds3WKyWsorWCpy3W",
        "HNHiZcTVvvsVw95HN7evUTVxqATR9GiZ8E",
        "HDnt9Kdv1uJouPunMMBAxYPQmfLUGT8ZaX",
        "HPbS56GtzH88tmBshjHDfotGzBAWJRHt62",
        "HCh2AaYWWo8RCnDcLTkABAggmidZzrBMPa",
        "H7ebXrxph3T9CrrXWhdESfFHbNBQ7izLTs",
        "H95h7snhudamaUUpys64KZGSWW41AFyg2w",
        "HU96JCADwRBF3aw3y9gPgE9NL7Ex6Bj3th",
        "HDrLQvdykkkqy2niVhaXj2skX34Mctqvie",
        "HMpWET7WcH1B1AiYtQED6ihE3ixYdUky4N",
        "HVS7dHpUgvZWFbdy2AVJG8wHrsYCHyVeTX",
        "HSgfGcC7VFCRaHY39W2PJtiNJKaEwK9ygZ",
        "HGdnm6fLG5xNFRSFbZxv4dNJYKUy5tXEhV",
        "HGB3cbzdCzLrYwUVvKjg7Hekb26w1WCUDG",
        "HQDDQ2L9YxVewMTYvejteus2iaUi9ygXKJ",
        "HBS5JdnrnuMT54HuowAAGz6uQsTHAXYJXP",
        "HB65kyocnPoKUKxTv1kdkWfm2Td9u96fuJ",
        "H9ULhN55yMdxBg7mdDnYvz89E5seYTLypL",
        "HF4DcgGwptrswdSN3gVKUKEsdhHTnnNDdU",
        "H9k2M4N4g63XFjHUU5JWhx31WhZRVRwKce",
        "HBoKLTF3qcwSNhyBC88jA5wYpmnqFN6cAx",
        "HUafLGqqb7iodpPF1Uczj8RDKEKogav1yA",
        "HGszE9iqWwyLVrtjGjdtYQk8B6QdopskQj",
        "HRUJpmZ4FKNgXVvCR3nPjt1tFjm9Ca8LLM",
        "HHxhWdXdAfjGy7vgd2pJBM8mggoqgSoQef",
        "HVjaRXxQ2fxFW9RGqUrvmtThggcHvVHTU6",
        "HAvSEfS7dFAkiv6dL11hTbG5vK5c66PfnW",
        "HAmwoFVFxJQmUdRM99hw7LDFNKTUgJk5fJ",
        "HGaAE4Qm2aPqitYtiG4zjchm94C3JvBNPP",
        "HNPSsBVEa3J9wvbe39ZHEez3NGX8u2R7jd",
        "HBxxxoHzfnVfDgxKFf6M81dBuwJktWgPrS",
        "HNNXGgrahvuzxahQ8KCD1BS2Lytts92XcY",
        "HCHHBBc2DfZhohJK9pAVyormM7w1kFDsrU",
        "HAkL4H939prxPHsrwPm8UXpKDTZP98tZNn",
        "HNGebE4vRypDRU6eMyobL4Kgwb2fmW1UA5",
        "HRRLT823gqfxmMrVKaVojob7QjjNdv1pji",
        "HSKbKzgbGovLc6x8ZU3PmnNhhcqrF31pKT",
        "HN3bnexihxwWmW7e1j1ZGEFfjwj1T5LJq1",
        "HAv2dYYxAuiZFzsigRvowxuLr8GRudtnKm",
        "HKni9qVLwvy7ng2hYyzX6VTbpJpo3r9hXY",
        "HHtSaeSv9ZJgQWPArwxrM9jqYVrGbBF52N",
        "HJzRS6cgVBwf1MRtt58EjYvmfj7hbGoGGK",
        "HMUWMfNP47UYcsC8iH9h6ddSmqk1cQ1Chi",
        "HQM4TQR1CXoNXKadchDRQmdPuiHMeo1KUf",
        "HG3b32Q9SaosPpX6FFB8gW5vh76945L5k1",
        "HG6fLmuhRs6LZ8a4oC2qiRGSMCLXow3nQG",
        "HPtRbVKNqgASYx1KMUZJefDxTsEyYUogyo",
        "HSw5Xytcp995XGsGASKofCHkPNxoH4E6sT",
        "HFBUQdCgJVU6vn5qbEhHJ5wgo3fnA8zncL",
        "HFVf7vdWLNJeB3e58XzZz3cLU3H51PXkXn",
        "HKmaZWypHU7Bnh3QN6hJYqgsaZvU47F6Q2",
        "HBUUAUexE5r36HrATqGvNRFETMgQDg8wvH",
        "HAmiTrRPbs5XsRS4gXSt51udRwSwEYtwXE",
        "H8ntrzpJ5bw2XyXRnVwbrPJ37Rmo4KqnPw",
        "HS7GafFf685mNaNLABa5ofETJWTjbT9sVf",
        "HF9gxz3eePy9vBXTqr8j9Xsfca2hsxk1zD",
        "HAtFhQmnosXy9cU5dMraQcCjK7GaVvinPW",
        "HN24fLDxcrWTuXuDqCsgNpsYoYCwjgfQJ6",
        "HUER6X7RjvLR6MpGGCXk6NaLaCFjCmVugg",
        "H9nJXemcvYCcyoH2YMvXUWPwjN54CUD3ut",
        "HAgDXeeqVndrqzbwEMNht3wM3v472KUBy1",
        "HJRHgXuYZBGnuxKouPvwURkraL7bLyDkjm",
        "HUov7miEcjYwyqNTn4AePWRFQNUAG64kAP",
        "HPpwz1A72GmBdADxcsWKRyUbAU5pzdJSQ2",
        "HJ79HWz8KPCmwcBjdAVXMH79HgWAfyKsWH",
        "HCWKdc3K3owgKt4x87SuMCkU253d4Nw3ft",
        "HA6PRqjJe4aRkmBtgcKT6w1GbLsthgxhKn",
        "HH6GjFnwj4RUDR8Mh8xbKqpQHyQWgYESEh",
        "H9WjF81HdJ4icN6xhYd7eCvJMPuW8PiGtr",
        "HM5bQaYq6TnbRy9Wt5v6LN5wEFV5RpBNto",
        "HCx6X4n9eATahRctXssMtSwHwnTRh9tn5c",
        "HLPqhb6B6cL8titZs9ci484nvqkEto8hw2",
        "HRm5hRvTScRXrznsY5R8Q8e9UZH6cHoJR9",
        "HQdwqB3GXnJMc7EFn1tDrMWyzXzFhUDwTu",
        "HTebBHuUkNk8xikRbBEV1ytSsMjYnRr2Co",
        "HEL5DjsqbBJMQs6G2x1yTe7j3JsmjtgYX9",
        "HHQgQUSJ39jR2xdn1v2ihrzuXP7HKHnKgn",
        "HCKZvRsihcdByZVJNKSsrj1fMfLKMvNn4h",
        "HBiH5bqWcqxLKioEJAd1dE3KwfbrJPF1br",
        "HAwcQ8j6zFSbk1hcUHgiv9BmZVk8BS5xBr",
        "HJ3fHYWRRdCbzf1d1q1DV61NNVdaw3LWZK",
        "HPk3KYRvpCtctPBiyvGfARvgVKPomw9HxB",
        "HDsYz5qNvDczfTFQWHS2M4k3fScQaMCGpe",
        "HH5kvMHiRZMpYKEVWoV2YJR3SJY1u3e14d",
        "HQwD9GWKTwdoak3rJrzrYbdo5HEDKN7Qhj",
        "HMn75M7VXMtPwfXn5fVnHAKNaqPB88xdV6",
        "HJ92iAQ9zXhdwWHa2BX8LdppRt1jTfe37X",
        "HFaT5JmLLWqaNHUU86p32gM1hL6iUHHvd2",
        "HApsKk13Y1QrwFM1zRfQuKVX8wbaYQGY6s",
        "HD2tidLMZnEvijRVWDWsAiFdNuNDFKLvET",
        "HTi9qxhbnA541yaWL1PiZZXKWfBFCPVaBs",
        "H9q5vfEHC5tS58JHs7jj3VfhdDcKQqV8qL",
        "HL8pQkkHFwpZXFG3k2hiDnCrGLwgkfLun8",
        "HGaUxzZA8i3JLtgSdFq6pecKnGyvaHZSAc",
        "HQTakwYzvK1BWYZdgDKa7F4NhVAjb7uBSR",
        "HVmsSv6rmH87AswX7BKyErCFC4VEHXZY3F",
        "HVUfMtJoREJmcwxwx1VJWbq1GFW75o1CdR",
        "HAXvi6MfvGcAMuxBcfVNy6dR7Um37rEbut",
        "HSUQGowSEPF2Vmb7s6yGkZxSkh5gyTBTKW",
        "H9uHf7E4SobzWwba3dRResR4CR5PQst36H",
        "HLv2A1P9k6xdvorLgoUVcyD5BfmfqpdzQF",
        "HJyYJkRwFj2FhudJ7geQ972NwGGWa9LbEN",
        "HDzmowVSta2ReRYU6X2mcSfvxHVu1pjvBK",
        "HA6Sicrdu7rurup1XJmK19ZvR4BfoBgQT2",
        "HDv16qrFFPHvppjy1695RnatrpnWw6t5wT",
        "HUHF61EpsP7gH5LrYSmWXK4Po8YEXT9hnp",
        "HVJ5rK67hEoinX3nygktVaGRPvx4GGpgna",
        "HUUkXWcjzh19j9hFh1ttpwKQouT7c87rN6",
        "HT75ndRCpp9j5wBFJ6cKTo1qx97XguJQRq",
        "HNLimuZFEwA8CmiNKSJNHaHXUgYpuxvSvm",
        "HNvqp1RimC5TTbdqoebWsjsUsWpVwWoHoP",
        "HMQKbrcuyeKq18RyYjbdE2g8V2osV1rkwC",
        "HQ3r2ndsPt8MYWw6q3vh76n9MYXBowbD4D",
        "HUFQUyRpEBhieaeoAHFAa2EaDVz2tp4j5y",
        "HEZa3vZQtXXiEieDSTFzKJfaeWZUKqU9sx",
        "HGMwyuzBypPqQd4XofNcFXK4P7ZmA6QfiE",
        "HM2n55Jhv63bugrd9GJtwZDzdjtqW3CRBp",
        "HLGszcPPhE5bGkCzMTcYXBTSQk3s18Qqd2",
        "HPrKjgAxqTVzq3f9C136BrfgFELVTq8T6t",
        "HVkqjpAqrkfsnQYrHBXuxo6Biz6hEdkvzT",
        "HSrM5ktJyWgVq76Kj8vg7627ZWsCkzUvSn",
        "HF5a2xhycDkpw15TkxXZp8eWyV4rbCxwnv",
        "HTXL8dM19vMCAPRGi2YcUqdeVqkJwqQuGH",
        "HSU3BT24jG4LSrhTT9VVJVMvGTmsi2GAtm",
        "H9oiyzkcZ8PtJuJ4XevLb82e4z9xwxfaki",
        "HTQLKgzeHKobyyU3vTYgU59GTqk5BZJQam",
        "HKmbo8Kjn5S7iTGpmh9Muf43Xrh8Zpuuf9",
        "HA5v2RAVS5UuZK5nJpNas6NEdrMhy1EC1q",
        "HPskvudGDhQejJVWBxJUuUtdbDVZtgmVpV",
        "HUnn9egAvwxrGPXaQgRNfmskBSM2tiNNaN",
        "HFQLUb8c8kDQMZjMyVcrHz4yxk5np6wSGc",
        "HQTQ83hx6ovjJkge3b2TwssJFPBx4C75vv",
        "HR1GhFdJiXCaK4rRZWMqnDZvFpoUwgbCjT",
        "HG4M5A8mwR1WZB22sDxSusNbDyqTrrU4BJ",
        "HAqbaMptUhh7sHewSj6zBPVp1nPKSHmZjU",
        "H7FTwGF6Rrwpb2mMEnKP5FsbQsEbvCdhNS",
        "HAUndSiFtqJ1eGKgDG7HohbUs3XBXvPtyZ",
        "HLzDhsvwLdzTmrgq3Gb2iLwjK6rCzRYPrn",
        "HFZ6G31i8pt1H3zNgP6zDNw6628Uz7beYC",
        "HBv4ouKvAa5dkr8nfnz2nDVtyHs3J4nBsE",
        "HLVkZGTgfFYfRrbTgm5Hu3onQiUrZHAUYF",
        "H7BrxB2YrEJRrniHSch6u9gfY4sjfnVJam",
        "HK2tSV537WnuDZeHzmKBfShLKsZhKoAE9r",
        "HK2TVYf5pDNCrpDNxqXWftSY7mf2YcAmxp",
        "HJxp3jwPiSruGwS7dzBEEa8rYXCkgd9TNv",
        "H9car2yKBXqwUGdt1LCc3PUZGd29q6cjAe",
        "HPhvUG9vxNhDBBqfRo2oAeAS8oEhpNGLgK",
        "HSv2vdjAcatsckoNyY1QKVymjQvVcNN11k",
        "HB6Ryjo1jNn4iXCkJQGpBu7R2q4fLBGpxD",
        "HPSyPPypPgYF433yGgAEQjUnLnmXNfk93X",
        "HAgfaSoaMhxAaSTFdCNNKQZvKQeMpNQpKM",
        "HCLPXEDXa3YWtcJKf3UHVV3oTxUmdKnG7t",
        "HEd6b8SDmkKom6kpHcZLEcp5Uf1iXvGSMa",
        "H7EMKezLmcQQPbnqn8Y5nGyMuPouEnJiHi",
        "HHUcQrzG25zdeWgtzmpVQrCDRhv5kUGqNQ",
        "HBMkDvmN4e9A4vvZbs3kFe4m27c59cDbRr",
        "H7iQzBDyHyyLPH4sgyyuE3XSmwombng5fi",
        "H9w35Kus1Ubz5V8PHpxRW71LT1gmdmbpYw",
        "HK1RmpexYn9qg1ejZpYz1dyDQzDqVowZyR",
        "HLpKeXMPMvfC6hmJBe8HfqUsrhKav9buUv",
        "HLyjb3Mf4QNQ8HjsQ1evevuiJ3RrXme6nW",
        "HPRCcmfGuEmi3D6gktVMceB7k16CD2ns4W",
        "HCJwL1cSDWbb1is8qpRufFHiVwqSqjQZF8",
        "HMoWX7VAYabxSnVeKqhr244fAagTMZAuWF",
        "HKiyt1u7YeCwANZaULZ8x1TdakwoMC56RJ",
        "HAhaX2YShbinEyPyLk25mnHeayKEToWCPk",
        "HD2VFkaQc4mR38ADTRh6ZJpb6gSbZnRqho",
        "HGcg6rSSJEL12t8dEqAMopysxThjNT8aXf",
        "HLAugf2LfSXwTTEQEFrmFFEgdJjA5fX1Hz",
        "HVFCALFyAG58Q4zayZCtnGkXF8ci7TiZod",
        "H8aTrMvytKG3nNfXxw4SH6sgWLokggdrMU",
        "HNKWZXmSkV9gpUu8whVyCBFYRTNWgEcrRY",
        "HTRQWRioP96Bvx9zQpcC8r5JjwPuSpvVT6",
        "HTbTwwdqYys5nA3PejG9Ca389mMBvJYYJY",
        "HDejTF6MuEQnrBqTF8b8NQpo1JUWc6r6UT",
        "HLRGjHL5f2vPavphuhYdJjYYAaoiWZQhRT",
        "HQWTTAkieUbMGQBLitYf39nknVCd5HwvHB",
        "H8sWycxJoDi7HS86BxTmm9zbqykcuvuUy4",
        "H9f4miziimx3ABzkusfCZEHc21D58y2ZPR",
        "HAunDdHhjxgvXRTLN7XjmL9NLJKryBVoJp",
        "HE3Pn7MgyMS2UqfLzEhqqcN1qnD988whVZ",
        "H7uqoXowhPKTCSYuUsJqR7pFWthhYu9Tfg",
        "HBj3oBUSWjQLhMs8CoM4xoPLiu8MxtVGFr",
        "HAhBdwF22d7dRjnxa4T8wQEDa4uDJvEqGY",
        "HGvsvAKZrVuB83YtwdNRn1ti3RJNRmSHFn",
        "HRkxkopW23nsW4ZhEFpXJztcdvVoZSGJ8T",
        "HKbYbDvZjprfgv2YbjSLsg1titsW4N4JuK",
        "HK2vn9EXKfcW6VUD1TBCLh3pd8ULprhapF",
        "HPLBboaYo1DENMZ7mgSdfarGJa7sNLKx3e",
        "HGxW852XFvinDirVZ4B3FQYD4GHKPGDppf",
        "HCzjM8HRLZjTwef4fYYKPRUbAywN5JLT21",
        "H8XafRyywV3D2gwwXDtertjqpLj6j2NX4G",
        "HBcL22P3iEd566JQVSxRwFBzJamjLdjR9u",
        "HAa3jUmcJuMRqAXecZ63bhR2XA99cMWkZT",
        "HFvy7czFBF1qqxrrUkyGwSohcBxEXC7rnT",
        "HN8NpP8jTdPgjscccx138WKLXPVWiro89P",
        "HTJ6xTTim8Pfu3Aq6jj2pZ4ofEmtwXxBEg",
        "HE49dsnbgDWuccPbXzVbpW7mUDGZwX7Vv7",
        "HRm9gSbbCA7Sbd4KHHCdzPxgKXmUuoJ1ma",
        "H6kec22PKBWDMiQ4RpP3jXS63ckxPAPC9j",
        "H8AEM8nWoPgi6PE1qBUT38CuXR193kJFqN",
        "HUTWgCSAYkF6NSZsXXowAFe5XXvJni3fTE",
        "HG4nSah2aPWNRLTrR7iUKWnjudbACEyKej",
        "HAVJSEnFkj4xdvJtxbdsJXhuuf9HofJmq6",
        "H7sJwx2bHdixGWd9B4mPj6x5cViPmpmnDM",
        "HAzy77YmarMWHL7w9rDhptrVnpTfUnAR4P",
        "HEL6GGLePwiwnjT8G7TwGHFqkigz9X9iKV",
        "HPWC4bLiQTYukizFzNbVM88ES6dknW7JuQ",
        "HJLknDhbtQbLnm7aLEjPctmeMh7CRGgoq4",
        "HTxbRfGLwpAWvnEkQ8oEhUivgm41rVzNhn",
        "HS2KgvhwB9qA8F17bah63tyAqSd9oYTa6n",
        "HST9mny3Jy2mQwT1aCE6YZsNiNsATX5cDn",
        "HJ58N4RCDZhgwV7V8HN7BcDEsZPJTavQA2",
        "HLQPLf2iMSeP8FzGDujag5JswSm6nCrDHM",
        "H6kLqwDGWJUxD2tzfkRGGg5scBSXUppjcU",
        "HA88EDBm8XCN92usWCa1UwgeeQ7EeR5xWw",
        "HVSmTcPmxsnkcy59kgqRf7pTF9RZ5cX7vg",
        "HNfxXMFFd7uNuDTVuR5qpG2voteQ6DFqkB",
        "H9ayQiApYe7Ls6dhnzpz2RRQF6GxZAQZSH",
        "HF4jM4WMd4W7QPqafYJcCAYTpiGF44Z5sK",
        "HKVgeEnmwEkufUU3VuMYY6DHAA1Kx8xXo2",
        "HNPx37LV14ZkBy8CiFtHqEJLotKqArmtEo",
        "H6mFV2LZj21uNRrHZquEV5ePngEAenkHJV",
        "HKEYdk44Y3tZhHbS4Neb125J9QbMFZAXwZ",
        "HFhwJqEihAE8rJixU9FnTxCvKxXmisG4AC",
        "HBwYJHEyj1sKHCfaPbwWYfbCbHVHtp2h5u",
        "HA5ktYQR9EVqx6k7BtuXEZQQgnv6cyjZQb",
        "HJ9ZoEg4tatkmYdjivt4nZeuAPyj1LA44g",
        "HKAQ1DoxYFHHcSsiokpivcQoy42hcYvf9X",
        "HGG6XiYRFn1b5vyQwbHDZKHHVts93whdzK",
        "HQz1DcvHosRFyctRZai5LB4Gymxs29wRdh",
        "HVjJmawuvpasxNidFeDhrt4eAWm7RyN8DC",
        "HTwy2jFjX8a8HeE7HxWiBHufkeVQHa1mpo",
        "HBRxNr5Rd6hLqNuv2jJ6dRK2CuMo7bqH2r",
        "HG9YsddV9ydu1A4Mh8T7o2DRgtttvh17px",
        "HPVX58GYncV9ABBFr9nrBabn8Km43C3Ak7",
        "HE57MYwMDa5oigdgHtx7xFUeBrHZKKBwPz",
        "HTg8fHgPDV8Z3Y3bMj6oYCyusMH5HDBGCE",
        "HUg8rRgAwsFegVam2cCsCPz8ScGZzk8f55",
        "H6gGKBTcADi1vjtbmkR3mNozT2N9bpwmYS",
        "HJFxBL4rAgXBdkhcsMEpjLE7qjz7SKPnz5",
        "HMtr8Fq6hJxGToQNwi4AcykDPDcXCqYc69",
        "HBncpvSAKrFtgaDMVGZs1oRXCKJ1gpSVwA",
        "HEJMpfcUn61up7Zd3doyYV81Zq5W2Ak6jx",
        "H6t4LpDXk8AQ5tBowRQMu55wudPTvHLzoV",
        "H8jPgCDog3b2h5vxTLVNDhU7LsdPsPvUWy",
        "H8xYZCHRh9hBz9dE7AqkVjjAj5grtSztfs",
        "HTXYhZ9kUhRuCGSrbAV7uAA3HcMNk7wFVA",
        "HMt8e3RZyRsSR4tLKoKW97eBrghpv1Ljpu",
        "HNCfeqFoqY8cjJD1HUmTY1AVeuiWrNqmKa",
        "H9j634hWEV2kvmqhjiF5vvBykxXs2URSxY",
        "HNpLafi3SuAgxgLWMMHLNdXYLHuoHhHDS2",
        "HL3aigGPewfFQywvDW45knMezAdVEVXVYG",
        "HUnCeGSSu5xvzMPp2uBNF9XDq1yAkwf71R",
        "H9Ek4HhSzeJkGsuzBouZYZjKY2SXtAp28N",
        "HVg8crM4B21eQSQirw69aX8gsrsXHufR65",
    };
    
};

/** A key allocated from the key pool. */
class CReserveKey : public CReserveScript
{
protected:
    CWallet* pwallet;
    int64_t nIndex;
    CPubKey vchPubKey;
    bool fInternal;
public:
    CReserveKey(CWallet* pwalletIn)
    {
        nIndex = -1;
        pwallet = pwalletIn;
        fInternal = false;
    }

    CReserveKey() = default;
    CReserveKey(const CReserveKey&) = delete;
    CReserveKey& operator=(const CReserveKey&) = delete;

    ~CReserveKey()
    {
        ReturnKey();
    }

    void ReturnKey();
    bool GetReservedKey(CPubKey &pubkey, bool fInternalIn /*= false*/);
    void KeepKey();
    void KeepScript() override { KeepKey(); }
};


/** 
 * Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CAccount
{
public:
    CPubKey vchPubKey;

    CAccount()
    {
        SetNull();
    }

    void SetNull()
    {
        vchPubKey = CPubKey();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPubKey);
    }
};

#endif // BITCOIN_WALLET_WALLET_H
