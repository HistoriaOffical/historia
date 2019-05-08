
#include "netbase.h"
#include "masternodeconfig.h"
#include "util.h"
#include "chainparams.h"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

CMasternodeConfig masternodeConfig;

void CMasternodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex, std::string ipv6, std::string ipfsId) {
    CMasternodeEntry cme(alias, ip, privKey, txHash, outputIndex, ipv6, ipfsId);
    entries.push_back(cme);
}

bool CMasternodeConfig::read(std::string& strErr) {
    int linenumber = 1;
    boost::filesystem::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathMasternodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathMasternodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Masternode config file\n"
                          "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index ipv6 ipfs_peer_id\n"
                          "# If Masternode Collateral 100 use example: mn1 127.0.0.2:35777 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0 0 0\n"
                          "# If Masternode Collateral 5000 use example: mn1 127.0.0.2:35777 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0 1080:0:0:0:8:800:200C:417A QmZkRv4qfXvtHot37STR8rJxKg5cDKFnkF5EMh2oP6iBVU\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty())
            continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex, ipv6, ipfsId;

        if (iss >> comment) {
            if (comment.at(0) == '#')
                continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex >> ipv6 >> ipfsId)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex >> ipv6 >> ipfsId)) {
                strErr = _("Could not parse masternode.conf") + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }
        //TODO:
        //Add IPv6 Parsing
        //Add IPFSID security checks
        int port = 0;
        std::string hostname = "";
        SplitHostPort(ip, port, hostname);
        if (port == 0 || hostname == "") {
            strErr = _("Failed to parse host:port string") + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }
        int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
        if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
            if (port != mainnetDefaultPort) {
                strErr = _("Invalid port detected in masternode.conf") + "\n" +
                         strprintf(_("Port: %d"), port) + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                         strprintf(_("(must be %d for mainnet)"), mainnetDefaultPort);
                streamConfig.close();
                return false;
            }
        } else if (port == mainnetDefaultPort) {
            strErr = _("Invalid port detected in masternode.conf") + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                     strprintf(_("(%d could be used only on mainnet)"), mainnetDefaultPort);
            streamConfig.close();
            return false;
        }

        if (port == 0 || hostname == "") {
            strErr = _("Failed to parse host:port string") + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }

        if (!(std::find_if(ipfsId.begin(), ipfsId.end(), [](char c) { return !std::isalnum(c); }) == ipfsId.end())) {
             strErr = _("Failed to parse IPFS Peer ID string") + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }
        struct sockaddr_in6 sa;
        if (inet_pton(AF_INET6, ipv6.c_str(), &(sa.sin6_addr)) == 0) {
            strErr = _("Failed to parse IPv6 address") + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
            streamConfig.close();
            return false;
        }

    add(alias, ip, privKey, txHash, outputIndex, ipv6, ipfsId);
    }

    streamConfig.close();
    return true;
}
