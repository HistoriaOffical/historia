// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "governance-object.h"
#include "core_io.h"
#include "governance-classes.h"
#include "governance-validators.h"
#include "governance-vote.h"
#include "governance.h"
#include "masternode-meta.h"
#include "masternode-sync.h"
#include "messagesigner.h"
#include "spork.h"
#include "util.h"
#include "validation.h"

#include <string>
#include <univalue.h>

CGovernanceObject::CGovernanceObject() :
    cs(),
    nObjectType(GOVERNANCE_OBJECT_UNKNOWN),
    nHashParent(),
    nRevision(0),
    nTime(0),
    nDeletionTime(0),
    nCollateralHash(),
    vchData(),
    masternodeOutpoint(),
    vchSig(),
    fCachedLocalValidity(false),
    strLocalValidityError(),
    fCachedFunding(false),
    fPermLocked(false),
    fCachedLocked(false),
    fCachedValid(true),
    fCachedDelete(false),
    fCachedEndorsed(false),
    fDirtyCache(true),
    fExpired(false),
    fUnparsable(false),
    mapCurrentMNVotes(),
    cmmapOrphanVotes(),
    fileVotes(),
    nCollateralBlockHeight(0)
{
    // PARSE JSON DATA STORAGE (VCHDATA)
    LoadData();
}

CGovernanceObject::CGovernanceObject(const uint256& nHashParentIn, int nRevisionIn, int64_t nTimeIn, const uint256& nCollateralHashIn, const std::string& strDataHexIn) :
    cs(),
    nObjectType(GOVERNANCE_OBJECT_UNKNOWN),
    nHashParent(nHashParentIn),
    nRevision(nRevisionIn),
    nTime(nTimeIn),
    nDeletionTime(0),
    nCollateralHash(nCollateralHashIn),
    vchData(ParseHex(strDataHexIn)),
    masternodeOutpoint(),
    vchSig(),
    fCachedLocalValidity(false),
    strLocalValidityError(),
    fCachedFunding(false),
    fPermLocked(false),
    fCachedLocked(false),
    fCachedValid(true),
    fCachedDelete(false),
    fCachedEndorsed(false),
    fDirtyCache(true),
    fExpired(false),
    fUnparsable(false),
    mapCurrentMNVotes(),
    cmmapOrphanVotes(),
    fileVotes(),
    nNextSuperblock(-1),
    nCollateralBlockHeight(0)
{
    // PARSE JSON DATA STORAGE (VCHDATA)
    LoadData();

    nCollateralHashBlock = governance.CollateralHashBlock(nCollateralHashIn);
}

CGovernanceObject::CGovernanceObject(const CGovernanceObject& other) :
    cs(),
    nObjectType(other.nObjectType),
    nHashParent(other.nHashParent),
    nRevision(other.nRevision),
    nTime(other.nTime),
    nDeletionTime(other.nDeletionTime),
    nCollateralHash(other.nCollateralHash),
    vchData(other.vchData),
    masternodeOutpoint(other.masternodeOutpoint),
    vchSig(other.vchSig),
    fCachedLocalValidity(other.fCachedLocalValidity),
    strLocalValidityError(other.strLocalValidityError),
    fCachedFunding(other.fCachedFunding),
    fCachedLocked(other.fCachedLocked),
    fPermLocked(other.fPermLocked),
    fCachedValid(other.fCachedValid),
    fCachedDelete(other.fCachedDelete),
    fCachedEndorsed(other.fCachedEndorsed),
    fDirtyCache(other.fDirtyCache),
    fExpired(other.fExpired),
    fUnparsable(other.fUnparsable),
    mapCurrentMNVotes(other.mapCurrentMNVotes),
    cmmapOrphanVotes(other.cmmapOrphanVotes),
    fileVotes(other.fileVotes),
    nCollateralHashBlock(other.nCollateralHashBlock),
    nNextSuperblock(other.nNextSuperblock)
{
}

bool CGovernanceObject::ProcessVote(CNode* pfrom,
    const CGovernanceVote& vote,
    CGovernanceException& exception,
    CConnman& connman)
{
    LOCK(cs);

    // do not process already known valid votes twice
    if (fileVotes.HasVote(vote.GetHash())) {
        // nothing to do here, not an error
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Already known valid vote";
        LogPrint("gobject", "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_NONE);
        return false;
    }

    auto mnList = deterministicMNManager->GetListAtChainTip();
    auto dmn = mnList.GetMNByCollateral(vote.GetMasternodeOutpoint());

    if (!dmn) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Masternode " << vote.GetMasternodeOutpoint().ToStringShort() << " not found";
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_WARNING);
        if (cmmapOrphanVotes.Insert(vote.GetMasternodeOutpoint(), vote_time_pair_t(vote, GetAdjustedTime() + GOVERNANCE_ORPHAN_EXPIRATION_TIME))) {
            LogPrintf("%s\n", ostr.str());
        } else {
            LogPrint("gobject", "%s\n", ostr.str());
        }
        return false;
    }

    vote_m_it it = mapCurrentMNVotes.emplace(vote_m_t::value_type(vote.GetMasternodeOutpoint(), vote_rec_t())).first;
    vote_rec_t& voteRecordRef = it->second;
    vote_signal_enum_t eSignal = vote.GetSignal();
    if (eSignal == VOTE_SIGNAL_NONE) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Vote signal: none";
        LogPrint("gobject", "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_WARNING);
        return false;
    }
    if (eSignal > MAX_SUPPORTED_VOTE_SIGNAL) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Unsupported vote signal: " << CGovernanceVoting::ConvertSignalToString(vote.GetSignal());
        LogPrintf("%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR, 20);
        return false;
    }
    vote_instance_m_it it2 = voteRecordRef.mapInstances.emplace(vote_instance_m_t::value_type(int(eSignal), vote_instance_t())).first;
    vote_instance_t& voteInstanceRef = it2->second;

    // Reject obsolete votes
    if (vote.GetTimestamp() < voteInstanceRef.nCreationTime) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Obsolete vote";
        LogPrint("gobject", "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_NONE);
        return false;
    } else if (vote.GetTimestamp() == voteInstanceRef.nCreationTime) {
        // Someone is doing smth fishy, there can be no two votes from the same masternode
        // with the same timestamp for the same object and signal and yet different hash/outcome.
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Invalid vote, same timestamp for the different outcome";
        if (vote.GetOutcome() < voteInstanceRef.eOutcome) {
            // This is an arbitrary comparison, we have to agree on some way
            // to pick the "winning" vote.
            ostr << ", rejected";
            LogPrint("gobject", "%s\n", ostr.str());
            exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_NONE);
            return false;
        }
        ostr << ", accepted";
        LogPrint("gobject", "%s\n", ostr.str());
    }

    int64_t nNow = GetAdjustedTime();
    int64_t nVoteTimeUpdate = voteInstanceRef.nTime;
    if (governance.AreRateChecksEnabled()) {
        int64_t nTimeDelta = nNow - voteInstanceRef.nTime;
        if (nTimeDelta < GOVERNANCE_UPDATE_MIN) {
            std::ostringstream ostr;
            ostr << "CGovernanceObject::ProcessVote -- Masternode voting too often"
                 << ", MN outpoint = " << vote.GetMasternodeOutpoint().ToStringShort()
                 << ", governance object hash = " << GetHash().ToString()
                 << ", time delta = " << nTimeDelta;
            LogPrint("gobject", "%s\n", ostr.str());
            exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_TEMPORARY_ERROR);
            // return false;
	    std::ostringstream error;
	    error << "You have already voted on this proposal or record. "
		  << "You must wait " << GOVERNANCE_UPDATE_MIN / 3600
		  << (GOVERNANCE_UPDATE_MIN / 3600 > 1 ? " hours." : " hour.");
	    throw std::runtime_error(error.str());
	}
        nVoteTimeUpdate = nNow;
    }

    bool onlyVotingKeyAllowed = false;
    if (nObjectType == GOVERNANCE_OBJECT_PROPOSAL || nObjectType == GOVERNANCE_OBJECT_RECORD) {
	if (vote.GetSignal() == VOTE_SIGNAL_FUNDING || vote.GetSignal() == VOTE_SIGNAL_DELETE) {
            onlyVotingKeyAllowed = true;
        } 
    }

    // Finally check that the vote is actually valid (done last because of cost of signature verification)
    if (!vote.IsValid(onlyVotingKeyAllowed)) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Invalid vote"
             << ", MN outpoint = " << vote.GetMasternodeOutpoint().ToStringShort()
             << ", governance object hash = " << GetHash().ToString()
             << ", vote hash = " << vote.GetHash().ToString();
        LogPrintf("%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR, 20);
        governance.AddInvalidVote(vote);
        return false;
    }

    if (!mmetaman.AddGovernanceVote(dmn->proTxHash, vote.GetParentHash())) {
        std::ostringstream ostr;
        ostr << "CGovernanceObject::ProcessVote -- Unable to add governance vote"
             << ", MN outpoint = " << vote.GetMasternodeOutpoint().ToStringShort()
             << ", governance object hash = " << GetHash().ToString();
        LogPrint("gobject", "%s\n", ostr.str());
        exception = CGovernanceException(ostr.str(), GOVERNANCE_EXCEPTION_PERMANENT_ERROR);
        return false;
    }

    voteInstanceRef = vote_instance_t(vote.GetOutcome(), nVoteTimeUpdate, vote.GetTimestamp());
    fileVotes.AddVote(vote);
    fDirtyCache = true;
    return true;
}

void CGovernanceObject::ClearMasternodeVotes()
{
    LOCK(cs);

    auto mnList = deterministicMNManager->GetListAtChainTip();
    int nBlockHeight = 0;
    {
        nBlockHeight = (int)chainActive.Height();
    }
    vote_m_it it = mapCurrentMNVotes.begin();
    while (it != mapCurrentMNVotes.end()) {
        if (!mnList.HasMNByCollateral(it->first)) {
            if (nObjectType != GOVERNANCE_OBJECT_RECORD) {
                fileVotes.RemoveVotesFromMasternode(it->first);
                mapCurrentMNVotes.erase(it++);
            }
        } else {
            ++it;
        }
    }
}

std::set<uint256> CGovernanceObject::RemoveInvalidVotes(const COutPoint& mnOutpoint)
{
    LOCK(cs);

    auto it = mapCurrentMNVotes.find(mnOutpoint);
    if (it == mapCurrentMNVotes.end()) {
        // don't even try as we don't have any votes from this MN
        return {};
    }

    int nBlockHeight = 0;
    {
        nBlockHeight = (int)chainActive.Height();
    }

    auto removedVotes = fileVotes.RemoveInvalidVotes(mnOutpoint, nObjectType == GOVERNANCE_OBJECT_PROPOSAL);
    if (removedVotes.empty()) {
        return {};
    }
    
    if (nObjectType == GOVERNANCE_OBJECT_RECORD && (nBlockHeight < this->GetCollateralNextSuperBlock())) {
        auto removedVotesR = fileVotes.RemoveInvalidVotes(mnOutpoint, nObjectType == GOVERNANCE_OBJECT_RECORD);
        if (removedVotesR.empty()) {
            return {};
        }
    }
    

    auto nParentHash = GetHash();
    for (auto jt = it->second.mapInstances.begin(); jt != it->second.mapInstances.end(); ) {
        CGovernanceVote tmpVote(mnOutpoint, nParentHash, (vote_signal_enum_t)jt->first, jt->second.eOutcome);
        tmpVote.SetTime(jt->second.nCreationTime);
        if (removedVotes.count(tmpVote.GetHash())) {
            jt = it->second.mapInstances.erase(jt);
        } else {
            ++jt;
        }
    }
    if (it->second.mapInstances.empty()) {
        mapCurrentMNVotes.erase(it);
    }

    if (!removedVotes.empty()) {
        std::string removedStr;
        for (auto& h : removedVotes) {
            removedStr += strprintf("  %s\n", h.ToString());
        }
        LogPrintf("CGovernanceObject::%s -- Removed %d invalid votes for %s from MN %s:\n%s", __func__, removedVotes.size(), nParentHash.ToString(), mnOutpoint.ToString(), removedStr);
        fDirtyCache = true;
    }

    return removedVotes;
}

std::string CGovernanceObject::GetSignatureMessage() const
{
    LOCK(cs);
    std::string strMessage = nHashParent.ToString() + "|" +
                             std::to_string(nRevision) + "|" +
                             std::to_string(nTime) + "|" +
                             GetDataAsHexString() + "|" +
                             masternodeOutpoint.ToStringShort() + "|" +
                             nCollateralHash.ToString();

    return strMessage;
}

uint256 CGovernanceObject::GetHash() const
{
    // Note: doesn't match serialization

    // CREATE HASH OF ALL IMPORTANT PIECES OF DATA

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << nHashParent;
    ss << nRevision;
    ss << nTime;
    ss << GetDataAsHexString();
    ss << masternodeOutpoint << uint8_t{} << 0xffffffff; // adding dummy values here to match old hashing
    ss << vchSig;
    // fee_tx is left out on purpose

    return ss.GetHash();
}

uint256 CGovernanceObject::GetSignatureHash() const
{
    return SerializeHash(*this);
}

void CGovernanceObject::SetMasternodeOutpoint(const COutPoint& outpoint)
{
    masternodeOutpoint = outpoint;
}

bool CGovernanceObject::Sign(const CBLSSecretKey& key)
{
    CBLSSignature sig = key.Sign(GetSignatureHash());
    if (!key.IsValid()) {
        return false;
    }
    sig.GetBuf(vchSig);
    return true;
}

bool CGovernanceObject::CheckSignature(const CBLSPublicKey& pubKey) const
{
    CBLSSignature sig;
    sig.SetBuf(vchSig);
    if (!sig.VerifyInsecure(pubKey, GetSignatureHash())) {
        LogPrintf("CGovernanceObject::CheckSignature -- VerifyInsecure() failed\n");
        return false;
    }
    return true;
}

/**
   Return the actual object from the vchData JSON structure.

   Returns an empty object on error.
 */
UniValue CGovernanceObject::GetJSONObject()
{
    UniValue obj(UniValue::VOBJ);
    if (vchData.empty()) {
        return obj;
    }

    UniValue objResult(UniValue::VOBJ);
    GetData(objResult);

    if (objResult.isObject()) {
        obj = objResult;
    } else {
        std::vector<UniValue> arr1 = objResult.getValues();
        std::vector<UniValue> arr2 = arr1.at(0).getValues();
        obj = arr2.at(1);
    }

    return obj;
}

/**
*   LoadData
*   --------------------------------------------------------
*
*   Attempt to load data from vchData
*
*/

void CGovernanceObject::LoadData()
{
    if (vchData.empty()) {
        return;
    }

    try {
        // ATTEMPT TO LOAD JSON STRING FROM VCHDATA
        UniValue objResult(UniValue::VOBJ);
        GetData(objResult);
        LogPrint("gobject", "CGovernanceObject::LoadData -- GetDataAsPlainString = %s\n", GetDataAsPlainString());
        UniValue obj = GetJSONObject();
        nObjectType = obj["type"].get_int();
    } catch (std::exception& e) {
        fUnparsable = true;
        std::ostringstream ostr;
        ostr << "CGovernanceObject::LoadData Error parsing JSON"
             << ", e.what() = " << e.what();
        LogPrintf("%s\n", ostr.str());
        return;
    } catch (...) {
        fUnparsable = true;
        std::ostringstream ostr;
        ostr << "CGovernanceObject::LoadData Unknown Error parsing JSON";
        LogPrintf("%s\n", ostr.str());
        return;
    }
}

/**
*   GetData - Example usage:
*   --------------------------------------------------------
*
*   Decode governance object data into UniValue(VOBJ)
*
*/

void CGovernanceObject::GetData(UniValue& objResult)
{
    UniValue o(UniValue::VOBJ);
    std::string s = GetDataAsPlainString();
    o.read(s);
    objResult = o;
}

/**
*   GetData - As
*   --------------------------------------------------------
*
*/

std::string CGovernanceObject::GetDataAsHexString() const
{
    return HexStr(vchData);
}

std::string CGovernanceObject::GetDataAsPlainString() const
{
    return std::string(vchData.begin(), vchData.end());
}

void CGovernanceObject::UpdateLocalValidity()
{
    LOCK(cs_main);
    // THIS DOES NOT CHECK COLLATERAL, THIS IS CHECKED UPON ORIGINAL ARRIVAL
    fCachedLocalValidity = IsValidLocally(strLocalValidityError, false);
};


bool CGovernanceObject::IsValidLocally(std::string& strError, bool fCheckCollateral) const
{
    bool fMissingMasternode = false;
    bool fMissingConfirmations = false;

    return IsValidLocally(strError, fMissingMasternode, fMissingConfirmations, fCheckCollateral);
}

bool CGovernanceObject::IsValidLocally(std::string& strError, bool& fMissingMasternode, bool& fMissingConfirmations, bool fCheckCollateral) const
{
    fMissingMasternode = false;
    fMissingConfirmations = false;

    if (fUnparsable) {
        strError = "Object data unparseable";
        return false;
    }

    switch (nObjectType) {
    case GOVERNANCE_OBJECT_PROPOSAL: {
        CProposalValidator validator(GetDataAsHexString(), true);
        // Note: It's ok to have expired proposals
        // they are going to be cleared by CGovernanceManager::UpdateCachesAndClean()
        // TODO: should they be tagged as "expired" to skip vote downloading?
        if (!validator.Validate(false)) {
            strError = strprintf("Invalid proposal data, error messages: %s", validator.GetErrorMessages());
            return false;
        }
        if (fCheckCollateral && !IsCollateralValid(strError, fMissingConfirmations)) {
            strError = "Invalid proposal collateral";
            return false;
        }
        return true;
    }
    case GOVERNANCE_OBJECT_RECORD: {
        CProposalValidator validator(GetDataAsHexString(), true);
        // Note: It's ok to have expired records
        // they are going to be cleared by CGovernanceManager::UpdateCachesAndClean()
        // TODO: should they be tagged as "expired" to skip vote downloading?
        if (!validator.Validate(false)) {
            strError = strprintf("Invalid record data, error messages: %s", validator.GetErrorMessages());
            return false;
        }
        if (fCheckCollateral && !IsCollateralValid(strError, fMissingConfirmations)) {
            strError = "Invalid record collateral";
            return false;
        }
        return true;
    }
    case GOVERNANCE_OBJECT_TRIGGER: {
        if (!fCheckCollateral) {
            // nothing else we can check here (yet?)
            return true;
        }

        auto mnList = deterministicMNManager->GetListAtChainTip();

        std::string strOutpoint = masternodeOutpoint.ToStringShort();
        auto dmn = mnList.GetMNByCollateral(masternodeOutpoint);
	//NEEDS COLLATERAL CHECKING MAYBE
        if (!dmn) {
            strError = "Failed to find Masternode by UTXO, missing masternode=" + strOutpoint;
            return false;
        }

        // Check that we have a valid MN signature
        if (!CheckSignature(dmn->pdmnState->pubKeyOperator.Get())) {
            strError = "Invalid masternode signature for: " + strOutpoint + ", pubkey = " + dmn->pdmnState->pubKeyOperator.Get().ToString();
            return false;
        }

        return true;
    }
    default: {
        strError = strprintf("Invalid object type %d", nObjectType);
        return false;
    }
    }
}

CAmount CGovernanceObject::GetMinCollateralFee() const
{
    // Only 1 type has a fee for the moment but switch statement allows for future object types
    switch (nObjectType) {
    case GOVERNANCE_OBJECT_PROPOSAL:
        return sporkManager.GetSporkValue(SPORK_101_PROPOSAL_FEE_VALUE) * COIN;
    case GOVERNANCE_OBJECT_RECORD:
        return sporkManager.GetSporkValue(SPORK_100_RECORD_FEE_VALUE)*COIN;
    case GOVERNANCE_OBJECT_TRIGGER:
        return 0;
    default:
        return MAX_MONEY;
    }
}

bool CGovernanceObject::IsCollateralValid(std::string& strError, bool& fMissingConfirmations) const
{
    strError = "";
    fMissingConfirmations = false;
    CAmount nMinFee = GetMinCollateralFee();
    uint256 nExpectedHash = GetHash();

    CTransactionRef txCollateral;
    uint256 nBlockHash;

    // RETRIEVE TRANSACTION IN QUESTION

    if (!GetTransaction(nCollateralHash, txCollateral, Params().GetConsensus(), nBlockHash, true)) {
        strError = strprintf("Can't find collateral tx %s", nCollateralHash.ToString());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    if (nBlockHash == uint256()) {
        strError = strprintf("Collateral tx %s is not mined yet", txCollateral->ToString());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    if (txCollateral->vout.size() < 1) {
        strError = strprintf("tx vout size less than 1 | %d", txCollateral->vout.size());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    // LOOK FOR SPECIALIZED GOVERNANCE SCRIPT (PROOF OF BURN)

    CScript findScript;
    findScript << OP_RETURN << ToByteVector(nExpectedHash);

    LogPrint("gobject", "CGovernanceObject::IsCollateralValid -- txCollateral->vout.size() = %s, findScript = %s, nMinFee = %lld\n",
                txCollateral->vout.size(), ScriptToAsmStr(findScript, false), nMinFee);

    bool foundOpReturn = false;
    for (const auto& output : txCollateral->vout) {
        LogPrint("gobject", "CGovernanceObject::IsCollateralValid -- txout = %s, output.nValue = %lld, output.scriptPubKey = %s\n",
                    output.ToString(), output.nValue, ScriptToAsmStr(output.scriptPubKey, false));
        if (!output.scriptPubKey.IsPayToPublicKeyHash() && !output.scriptPubKey.IsUnspendable()) {
            strError = strprintf("Invalid Script %s", txCollateral->ToString());
            LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
            return false;
        }
        if (output.nValue >= nMinFee) {
        //if (output.scriptPubKey == findScript && output.nValue >= nMinFee) {
            foundOpReturn = true;
        }
    }

    if (!foundOpReturn) {
        strError = strprintf("Couldn't find opReturn %s in %s", nExpectedHash.ToString(), txCollateral->ToString());
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);
        return false;
    }

    // GET CONFIRMATIONS FOR TRANSACTION

    AssertLockHeld(cs_main);
    int nConfirmationsIn = 0;
    if (nBlockHash != uint256()) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                nConfirmationsIn += chainActive.Height() - pindex->nHeight + 1;
            }
        }
    }

    if ((nConfirmationsIn < GOVERNANCE_FEE_CONFIRMATIONS)) {
        strError = strprintf("Collateral requires at least %d confirmations to be relayed throughout the network (it has only %d)", GOVERNANCE_FEE_CONFIRMATIONS, nConfirmationsIn);
        if (nConfirmationsIn >= GOVERNANCE_MIN_RELAY_FEE_CONFIRMATIONS) {
            fMissingConfirmations = true;
            strError += ", pre-accepted -- waiting for required confirmations";
        } else {
            strError += ", rejected -- try again later";
        }
        LogPrintf("CGovernanceObject::IsCollateralValid -- %s\n", strError);

        return false;
    }

    strError = "valid";
    return true;
}

int CGovernanceObject::CountMatchingVotes(vote_signal_enum_t eVoteSignalIn, vote_outcome_enum_t eVoteOutcomeIn) const
{
    LOCK(cs);

    int nCount = 0;
    for (const auto& votepair : mapCurrentMNVotes) {
        const vote_rec_t& recVote = votepair.second;
        vote_instance_m_cit it2 = recVote.mapInstances.find(eVoteSignalIn);
        if (it2 != recVote.mapInstances.end() && it2->second.eOutcome == eVoteOutcomeIn) {
            ++nCount;
        }
    }
    return nCount;
}

/**
*   Get specific vote counts for each outcome (funding, validity, etc)
*/

int CGovernanceObject::GetAbsoluteYesCount(vote_signal_enum_t eVoteSignalIn) const
{
    return GetYesCount(eVoteSignalIn) - GetNoCount(eVoteSignalIn);
}

int CGovernanceObject::GetAbsoluteNoCount(vote_signal_enum_t eVoteSignalIn) const
{
    return GetNoCount(eVoteSignalIn) - GetYesCount(eVoteSignalIn);
}

int CGovernanceObject::GetYesCount(vote_signal_enum_t eVoteSignalIn) const
{
    return CountMatchingVotes(eVoteSignalIn, VOTE_OUTCOME_YES);
}

int CGovernanceObject::GetNoCount(vote_signal_enum_t eVoteSignalIn) const
{
    return CountMatchingVotes(eVoteSignalIn, VOTE_OUTCOME_NO);
}

int CGovernanceObject::GetAbstainCount(vote_signal_enum_t eVoteSignalIn) const
{
    return CountMatchingVotes(eVoteSignalIn, VOTE_OUTCOME_ABSTAIN);
}

bool CGovernanceObject::GetCurrentMNVotes(const COutPoint& mnCollateralOutpoint, vote_rec_t& voteRecord) const
{
    LOCK(cs);

    vote_m_cit it = mapCurrentMNVotes.find(mnCollateralOutpoint);
    if (it == mapCurrentMNVotes.end()) {
        return false;
    }
    voteRecord = it->second;
    return true;
}

void CGovernanceObject::Relay(CConnman& connman)
{
    // Do not relay until fully synced
    if (!masternodeSync.IsSynced()) {
        LogPrint("gobject", "CGovernanceObject::Relay -- won't relay until fully synced\n");
        return;
    }

    CInv inv(MSG_GOVERNANCE_OBJECT, GetHash());
    connman.RelayInv(inv, MIN_GOVERNANCE_PEER_PROTO_VERSION);
}

void CGovernanceObject::UpdateSentinelVariables()
{
    // CALCULATE MINIMUM SUPPORT LEVELS REQUIRED

    int nMnCount = (int)deterministicMNManager->GetListAtChainTip().GetValidMNsCount();
    if (nMnCount == 0) return;

    int nBlockHeight = 0;
    {
        LOCK(cs);
        nBlockHeight = (int)chainActive.Height();
    }

    // CALCULATE THE MINUMUM VOTE COUNT REQUIRED FOR FULL SIGNAL

    int nAbsVoteReq = std::max(Params().GetConsensus().nGovernanceMinQuorum, nMnCount / 10);
    int nAbsDeleteReq = std::max(Params().GetConsensus().nGovernanceMinQuorum, (2 * nMnCount) / 3);

    if(sporkManager.IsSporkActive(SPORK_103_RM_OBJECT)) {
 	nAbsDeleteReq = 3;
    }
	
    // SET SENTINEL FLAGS TO FALSE
    fCachedFunding = false;
    //DEFAULT FOR RECORDS IS LOCKED
    if (nObjectType == GOVERNANCE_OBJECT_RECORD) {
        fCachedLocked = true;
    } else {
        fCachedLocked = false;
    }
    fCachedValid = true; //default to valid
    fCachedEndorsed = false;
    fDirtyCache = false;
    fPastSuperBlock = false;
    fCachedDelete = false;
    // SET SENTINEL FLAGS TO TRUE IF MIMIMUM SUPPORT LEVELS ARE REACHED
    // ARE ANY OF THESE FLAGS CURRENTLY ACTIVATED?

    if (GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) >= nAbsVoteReq && nObjectType != GOVERNANCE_OBJECT_RECORD) fCachedFunding = true;
    int nCollateralBlockHeight = GetCollateralBlockHeight();
    int nCollateralSuperBlockHeight = GetCollateralNextSuperBlock();

    if (nCollateralBlockHeight == -1)
        LogPrintf("CGovernanceObject::UpdateSentinelVariables -- Invalid nCollateralBlockHeight\n");
    else {
        if (nObjectType == GOVERNANCE_OBJECT_RECORD) {
            // If Current RECORD with ABS YES passing, current block is greater than the superblock, record should be locked after update
            if (GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) >= nAbsVoteReq && nBlockHeight > nCollateralSuperBlockHeight && !fPermLocked) {
                fCachedFunding = false;
                fCachedLocked = true;
                fCachedDelete = false;
                fPastSuperBlock = true;
                fPermLocked = true;
            // If Old RECORD with ABS YES greater than 10, the Yes Votes are greater than the No/Abstain votes, the current block is more than the superblock of that specific voting cycle of the record, then the record should be locked after update
            // This is used to prevent old records from being deleted when masternodes/voting nodes increase/decrease dramatically over tim
            } else if (GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) >= 10 && GetYesCount(VOTE_SIGNAL_FUNDING) >= (GetNoCount(VOTE_SIGNAL_FUNDING) + GetAbstainCount(VOTE_SIGNAL_FUNDING)) && nBlockHeight > nNextSuperblock) {
                fCachedFunding = false;
                fCachedLocked = true;
                fCachedDelete = false;
                fPastSuperBlock = true;
                fPermLocked = true;
            // This is a temporary fix until all clients are sync'ed properly with all voting information. This should be removed in the future. 
            // Assume that if a received record is older than the current block height + two months of blocks then set to true.
            } else if (GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) >= 1 && nBlockHeight > nCollateralBlockHeight + 33232) {
                fCachedFunding = false;
                fCachedLocked = true;
                fCachedDelete = false;
                fPastSuperBlock = true;
                fPermLocked = true;
            // If Current RECORD with ABS YES passing, current block is less than the superblock, record should be locked after update
            } else if (GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) >= nAbsVoteReq && nBlockHeight < nCollateralSuperBlockHeight && !fPermLocked) {
                fCachedFunding = true;
                fCachedLocked = true;
                fCachedDelete = false;
                fPermLocked = false;
            // If RECORD hasn't passed and current block is less than the superblock after the collateral block, do nothing
            } else if (GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) < nAbsVoteReq && nBlockHeight < nCollateralSuperBlockHeight && !fPermLocked) {
                fCachedFunding = false;
                fCachedLocked = false;
                fCachedDelete = false;
                fPermLocked = false;
            // If RECORD hasn't passed and current block is greater than the superblock, set delete
            } else if ((GetAbsoluteYesCount(VOTE_SIGNAL_FUNDING) < nAbsVoteReq) && nBlockHeight > nNextSuperblock && !fPermLocked) {
                fCachedFunding = false;
                fCachedLocked = false;
                fCachedDelete = true;
                fPermLocked = false;
            }
        }
    }
    if (((GetAbsoluteYesCount(VOTE_SIGNAL_DELETE) >= nAbsDeleteReq) && !fCachedDelete) && !fCachedLocked && !fPermLocked) {
        fCachedDelete = true;
        if (nDeletionTime == 0) {
            nDeletionTime = GetAdjustedTime();
        }
    }

    if (GetAbsoluteYesCount(VOTE_SIGNAL_ENDORSED) >= nAbsVoteReq) fCachedEndorsed = true;

    if (GetAbsoluteNoCount(VOTE_SIGNAL_VALID) >= nAbsVoteReq) fCachedValid = false;
}

void CGovernanceObject::CheckOrphanVotes(CConnman& connman)
{
    int64_t nNow = GetAdjustedTime();
    auto mnList = deterministicMNManager->GetListAtChainTip();
    const vote_cmm_t::list_t& listVotes = cmmapOrphanVotes.GetItemList();
    vote_cmm_t::list_cit it = listVotes.begin();
    while (it != listVotes.end()) {
        bool fRemove = false;
        const COutPoint& key = it->key;
        const vote_time_pair_t& pairVote = it->value;
        const CGovernanceVote& vote = pairVote.first;
        if (pairVote.second < nNow) {
            fRemove = true;
        } else if (!mnList.HasValidMNByCollateral(vote.GetMasternodeOutpoint())) {
            ++it;
            continue;
        }
        CGovernanceException exception;
        if (!ProcessVote(nullptr, vote, exception, connman)) {
            LogPrintf("CGovernanceObject::CheckOrphanVotes -- Failed to add orphan vote: %s\n", exception.what());
        } else {
            vote.Relay(connman);
            fRemove = true;
        }
        ++it;
        if (fRemove) {
            cmmapOrphanVotes.Erase(key, pairVote);
        }
    }
}

int CGovernanceObject::GetCollateralBlockHeight()
{
    
    CTransactionRef txCollateral;
    uint256 nBlockHash;
    GetTransaction(nCollateralHash, txCollateral, Params().GetConsensus(), nBlockHash, true);
    
    if (!nBlockHash.IsNull()) {
        BlockMap::iterator mi = mapBlockIndex.find(nBlockHash);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
		       nCollateralBlockHeight = pindex->nHeight;
            } else {
                nCollateralBlockHeight = -1;
            }
        }
    } else {
        nCollateralBlockHeight = -1;
    }

    return nCollateralBlockHeight;
}


int CGovernanceObject::GetCollateralNextSuperBlock()
{
    int nLastSuperblock;
    int collateralBlockHeight = this->GetCollateralBlockHeight();
    int nSuperblockCycle = Params().GetConsensus().nSuperblockCycle;

    nLastSuperblock = collateralBlockHeight - collateralBlockHeight % nSuperblockCycle;
    this->nNextSuperblock = nLastSuperblock + nSuperblockCycle;

    //LogPrintf("CGovernanceObject::GetCollateralNextSuperBlock -- nextsuperblock: %d\n",
	//    this-nNextSuperblock);
    return this->nNextSuperblock;
}

