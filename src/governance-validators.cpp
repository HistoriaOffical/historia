// Copyright (c) 2014-2018 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "governance-validators.h"

#include "base58.h"
#include "timedata.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "client.h"
#include "activemasternode.h"
#include "masternode-meta.h"
#include "ipfs-utils.h"
#include <algorithm>

const size_t MAX_DATA_SIZE = 1400;
const size_t MAX_NAME_SIZE = 40;
const size_t MAX_SUMMARY_SIZE = 1024; // 2048 in HEX or 1024 in Text

CProposalValidator::CProposalValidator(const std::string& strHexData, bool fAllowLegacyFormat) :
    objJSON(UniValue::VOBJ),
    fJSONValid(false),
    fAllowLegacyFormat(fAllowLegacyFormat),
    strErrorMessages()
{
    if (!strHexData.empty()) {
        ParseStrHexData(strHexData);
    }
}

void CProposalValidator::ParseStrHexData(const std::string& strHexData)
{
    std::vector<unsigned char> v = ParseHex(strHexData);
    if (v.size() > MAX_DATA_SIZE) {
        strErrorMessages = strprintf("data exceeds %lu characters;", MAX_DATA_SIZE);
        return;
    }
    ParseJSONData(std::string(v.begin(), v.end()));
}

bool CProposalValidator::Validate(bool fCheckExpiration)
{
    if (!fJSONValid) {
        strErrorMessages += "JSON parsing error;";
        return false;
    }
    if (!ValidateName()) {
        strErrorMessages += "Invalid name;";
        return false;
    }
    if (!ValidateStartEndEpoch(fCheckExpiration)) {
        strErrorMessages += "Invalid start:end range;";
        return false;
    }
    if (!ValidatePaymentAmount()) {
        strErrorMessages += "Invalid payment amount;";
        return false;
    }
    if (!ValidatePaymentAddress()) {
        strErrorMessages += "Invalid payment address;";
        return false;
    }
    if (!ValidateIpfsCID()) {
	    strErrorMessages += "Invalid IPFS CID or IPFS Type;";
	    return false;
    }
    if (!ValidateIpfsPID()) {
        strErrorMessages += "Invalid IPFS PID;";
        return false;
    }
    if (!ValidateSummary()) {
	    strErrorMessages += "Invalid format of Summary;";
	    return false;
    }
    
    return true;
}

bool CProposalValidator::ValidateRecord(bool fCheckExpiration)
{
    if (!fJSONValid) {
        strErrorMessages += "JSON parsing error;";
        return false;
    }
    if (!ValidateName()) {
        strErrorMessages += "Invalid name;";
        return false;
    }
    if (!ValidateStartEndEpochRecord(fCheckExpiration)) {
        strErrorMessages += "Invalid start:end range;";
        return false;
    }
    if (!ValidatePaymentAmount()) {
        strErrorMessages += "Invalid payment amount;";
        return false;
    }
    if (!ValidatePaymentAddress()) {
        strErrorMessages += "Invalid payment address;";
        return false;
    }
    if (!ValidateIpfsCID()) {
	    strErrorMessages += "Invalid IPFS CID or IPFS Type;";
        return false;
    }
    if (!ValidateIpfsPID()) {
        strErrorMessages += "Invalid IPFS PID;";
        return false;
    }
    if (!ValidateSummary()) {
        strErrorMessages += "Invalid format of Summary;";
        return false;
    }

    return true;
}

bool CProposalValidator::ValidateName()
{
    std::string strName;
    if (!GetDataValue("name", strName)) {
        strErrorMessages += "name field not found;";
        return false;
    }

    if (strName.size() > MAX_NAME_SIZE) {
        strErrorMessages += strprintf("name exceeds %lu characters;", MAX_NAME_SIZE);
        return false;
    }

    static const std::string strAllowedChars = "-_abcdefghijklmnopqrstuvwxyz0123456789";

    std::transform(strName.begin(), strName.end(), strName.begin(), ::tolower);

    if (strName.find_first_not_of(strAllowedChars) != std::string::npos) {
        strErrorMessages += "name contains invalid characters;";
        return false;
    }

    return true;
}

bool CProposalValidator::ValidateStartEndEpoch(bool fCheckExpiration)
{
    int64_t nStartEpoch = 0;
    int64_t nEndEpoch = 0;

    if (!GetDataValue("start_epoch", nStartEpoch)) {
        strErrorMessages += "start_epoch field not found;";
        return false;
    }

    if (!GetDataValue("end_epoch", nEndEpoch)) {
        strErrorMessages += "end_epoch field not found;";
        return false;
    }

    if (nEndEpoch <= nStartEpoch) {
        strErrorMessages += "end_epoch <= start_epoch;";
        return false;
    }

    if (fCheckExpiration && nEndEpoch <= GetAdjustedTime()) {
        strErrorMessages += "expired;";
        return false;
    }

    return true;
}

bool CProposalValidator::ValidateStartEndEpochRecord(bool fCheckExpiration)
{
    int64_t nStartEpoch = 0;
    int64_t nEndEpoch = 0;

    if (!GetDataValue("start_epoch", nStartEpoch)) {
        strErrorMessages += "start_epoch field not found;";
        return false;
    }

    if (!GetDataValue("end_epoch", nEndEpoch)) {
        strErrorMessages += "end_epoch field not found;";
        return false;
    }

    if (nEndEpoch <= nStartEpoch) {
        strErrorMessages += "end_epoch <= start_epoch;";
        return false;
    }

    if (fCheckExpiration && nEndEpoch <= GetAdjustedTime()) {
        strErrorMessages += "expired;";
        return false;
    }

    if (nEndEpoch >= GetAdjustedTime() + 5184000) {
        strErrorMessages += "end_epoch greater than 60 days in the future;";
        return false;
    }

    return true;
}

bool CProposalValidator::ValidatePaymentAmount()
{
    double dValue = 0.0;

    if (!GetDataValue("payment_amount", dValue)) {
        strErrorMessages += "payment_amount field not found;";
        return false;
    }

    if (dValue <= 0.0) {
        strErrorMessages += "payment_amount is negative;";
        return false;
    }

    // TODO: Should check for an amount which exceeds the budget but this is
    // currently difficult because start and end epochs are defined in terms of
    // clock time instead of block height.

    return true;
}

bool CProposalValidator::ValidatePaymentAddress()
{
    std::string strPaymentAddress;

    if (!GetDataValue("payment_address", strPaymentAddress)) {
        strErrorMessages += "payment_address field not found;";
        return false;
    }

    if (std::find_if(strPaymentAddress.begin(), strPaymentAddress.end(), ::isspace) != strPaymentAddress.end()) {
        strErrorMessages += "payment_address can't have whitespaces;";
        return false;
    }

    CBitcoinAddress address(strPaymentAddress);
    if (!address.IsValid()) {
        strErrorMessages += "payment_address is invalid;";
        return false;
    }

    if (address.IsScript()) {
        strErrorMessages += "script addresses are not supported;";
        return false;
    }

    return true;
}

bool CProposalValidator::ValidateURL()
{
    std::string strURL;
    if (!GetDataValue("url", strURL)) {
        strErrorMessages += "url field not found;";
        return false;
    }

    if (std::find_if(strURL.begin(), strURL.end(), ::isspace) != strURL.end()) {
        strErrorMessages += "url can't have whitespaces;";
        return false;
    }

    if (strURL.size() < 4U) {
        strErrorMessages += "url too short;";
        return false;
    }

    if (!CheckURL(strURL)) {
        strErrorMessages += "url invalid;";
        return false;
    }

    return true;
}

void CProposalValidator::ParseJSONData(const std::string& strJSONData)
{
    fJSONValid = false;

    if (strJSONData.empty()) {
        return;
    }

    try {
        UniValue obj(UniValue::VOBJ);

        obj.read(strJSONData);

        if (obj.isObject()) {
            objJSON = obj;
        } else {
            if (fAllowLegacyFormat) {
                std::vector<UniValue> arr1 = obj.getValues();
                std::vector<UniValue> arr2 = arr1.at(0).getValues();
                objJSON = arr2.at(1);
            } else {
                throw std::runtime_error("Legacy proposal serialization format not allowed");
            }
        }

        fJSONValid = true;
    } catch (std::exception& e) {
        strErrorMessages += std::string(e.what()) + std::string(";");
    } catch (...) {
        strErrorMessages += "Unknown exception;";
    }
}

bool CProposalValidator::GetDataValue(const std::string& strKey, std::string& strValueRet)
{
    bool fOK = false;
    try {
        strValueRet = objJSON[strKey].get_str();
        fOK = true;
    } catch (std::exception& e) {
        strErrorMessages += std::string(e.what()) + std::string(";");
    } catch (...) {
        strErrorMessages += "Unknown exception;";
    }
    return fOK;
}

bool CProposalValidator::GetDataValue(const std::string& strKey, int64_t& nValueRet)
{
    bool fOK = false;
    try {
        const UniValue uValue = objJSON[strKey];
        switch (uValue.getType()) {
        case UniValue::VNUM:
            nValueRet = uValue.get_int64();
            fOK = true;
            break;
        default:
            break;
        }
    } catch (std::exception& e) {
        strErrorMessages += std::string(e.what()) + std::string(";");
    } catch (...) {
        strErrorMessages += "Unknown exception;";
    }
    return fOK;
}

bool CProposalValidator::GetDataValue(const std::string& strKey, double& dValueRet)
{
    bool fOK = false;
    try {
        const UniValue uValue = objJSON[strKey];
        switch (uValue.getType()) {
        case UniValue::VNUM:
            dValueRet = uValue.get_real();
            fOK = true;
            break;
        default:
            break;
        }
    } catch (std::exception& e) {
        strErrorMessages += std::string(e.what()) + std::string(";");
    } catch (...) {
        strErrorMessages += "Unknown exception;";
    }
    return fOK;
}

/*
  The purpose of this function is to replicate the behavior of the
  Python urlparse function used by sentinel (urlparse.py).  This function
  should return false whenever urlparse raises an exception and true
  otherwise.
 */
bool CProposalValidator::CheckURL(const std::string& strURLIn)
{
    std::string strRest(strURLIn);
    std::string::size_type nPos = strRest.find(':');

    if (nPos != std::string::npos) {
        //std::string strSchema = strRest.substr(0,nPos);

        if (nPos < strRest.size()) {
            strRest = strRest.substr(nPos + 1);
        } else {
            strRest = "";
        }
    }

    // Process netloc
    if ((strRest.size() > 2) && (strRest.substr(0, 2) == "//")) {
        static const std::string strNetlocDelimiters = "/?#";

        strRest = strRest.substr(2);

        std::string::size_type nPos2 = strRest.find_first_of(strNetlocDelimiters);

        std::string strNetloc = strRest.substr(0, nPos2);

        if ((strNetloc.find('[') != std::string::npos) && (strNetloc.find(']') == std::string::npos)) {
            return false;
        }

        if ((strNetloc.find(']') != std::string::npos) && (strNetloc.find('[') == std::string::npos)) {
            return false;
        }
    }

    return true;
}

bool CProposalValidator::ValidateIpfsCID()
{
  std::string ipfsCid, ipfsCidType;

  GetDataValue("ipfscid", ipfsCid);
  GetDataValue("ipfscidtype", ipfsCidType);

  if (!IsIpfsIdValid(ipfsCid)) {
      strErrorMessages += "invalid format;";
      return false;
  }

  if (ipfsCidType != ""){
	  if (!IsIpfsCidTypeValid(ipfsCidType)) {
		  strErrorMessages += "invalid format;";
		  return false;
	  }
  }

  
  return true;
}

bool CProposalValidator::ValidateIpfsPID()
{
    std::string ipfsPid;

    GetDataValue("ipfspid", ipfsPid);

	if (ipfsPid != "")
	{
		if (!IsIpfsIdValid(ipfsPid)) {
			strErrorMessages += "invalid format;";
			return false;
		}
	}


    return true;
}

bool CProposalValidator::IsIpfsCIDDuplicate()
{
    std::string ipfsCid;

    GetDataValue("ipfscid", ipfsCid);

    //Check for duplicate CIDs which should not happen
    if (IsIpfsIdDuplicate(ipfsCid)) {
        strErrorMessages += " duplication of record not allowed";
        return true;
    }
    return false;
}

bool CProposalValidator::ValidateSummary()
{
    std::vector<UniValue> values;
    UniValue summaryObj;
    std::string strSummary;
    static const std::string summaryAllowedChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0912345678 .,;-_/:?@()'\"";

    // It can be empty though?
    values = objJSON.getValues();
    if (!objJSON.exists("summary")) {
	strErrorMessages += "summary field missing;";
	return false;
    }
    // TODO change back to "summary"
    summaryObj = values.back().get_obj();
    // 255 chars every value and only two keys (name and description)
    if (summaryObj.size() != 2) {
	strErrorMessages += "summary format is \"name\": \"\","
	    "\"description\": \"\";";
	return false;
    }

    if (!summaryObj.exists("name") || !summaryObj.exists("description")) {
	strErrorMessages += "summary format is \"name\": \"\","
	    "\"description\": \"\";";
	return false;
    }

    values = summaryObj.getValues();
    if (values.size() > MAX_SUMMARY_SIZE) {
	strErrorMessages += "value is more than 1024 characters;";
	return false;
    }

    std::string summaryName = values[0].get_str();
    std::string summaryDesc = values[1].get_str();
    if (summaryName.find_first_not_of(summaryAllowedChars)
	!= std::string::npos) {
	strErrorMessages += "summary name contains invalid characters;";
        return false;
    }
    if (summaryDesc.find_first_not_of(summaryAllowedChars)
	!= std::string::npos) {
	strErrorMessages += "summary description contains invalid characters;";
        return false;
    }

    return true;
}
 
