// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposalspage.h"
#include "ui_proposalspage.h"

#include "masternodelist.h"
#include "activemasternode.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "utilitydialog.h"
#include "walletmodel.h"

#include "instantx.h"
//#include "darksendconfig.h"
#include "masternode-sync.h"
#include "governance.h"
#include "transport-curl.h"
#include "client.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QObject>
#include <QStringListModel>
#include <QDesktopServices>
#include <QUrl>
#include <ctime>
#include <iomanip>

#include "json.hpp"

using json = nlohmann::json;

#define ICON_OFFSET 16
#define DECORATION_SIZE 54
#define NUM_ITEMS 5
#define NUM_ITEMS_ADV 7

/*
class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::HTA),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {

    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
*/
#include "proposalspage.moc"

ProposalsPage::ProposalsPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ProposalsPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
//    txdelegate(new TxViewDelegate(platformStyle, this)),
    timer(nullptr)
{
    ui->setupUi(this);

    int columnNameWidth = 200;
    int columnDateWidth = 100;
    int columnIPFSCIDWidth = 300;
    int columnVoteRatioWidth = 300;


    //model = new QStringListModel(this);
    //QStringList List = listProposals();
    //model->setStringList(List);
    //ui->listProposals->setModel(model);

    ui->tableWidgetApprovedRecords->setColumnWidth(0, columnNameWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(1, columnDateWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(2, columnIPFSCIDWidth);
    ui->tableWidgetApprovedRecords->setColumnWidth(3, columnVoteRatioWidth);


    ui->tableWidgetApprovedProposals->setColumnWidth(0, columnNameWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(1, columnDateWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(2, columnIPFSCIDWidth);
    ui->tableWidgetApprovedProposals->setColumnWidth(3, columnVoteRatioWidth);

    
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
    for (const auto& pGovObj : objs) {
        if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD) {
            time_t creationTime = pGovObj->GetCreationTime();
            std::string const plainData = pGovObj->GetDataAsPlainString();
            nlohmann::json jsonData = json::parse(plainData);
            QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

            QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
            QTableWidgetItem* url = new QTableWidgetItem(QString::fromStdString(jsonData["url"].get<std::string>()));
            QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
            QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);

            ui->tableWidgetApprovedRecords->insertRow(0);
            ui->tableWidgetApprovedRecords->setItem(0, 0, date);
            ui->tableWidgetApprovedRecords->setItem(0, 1, name);
            ui->tableWidgetApprovedRecords->setItem(0, 2, Vote);
            ui->tableWidgetApprovedRecords->setItem(0, 3, url);
        }
    }
    ui->tableWidgetApprovedRecords->sortItems(0, Qt::DescendingOrder);
    ui->tableWidgetApprovedProposals->sortItems(0, Qt::DescendingOrder);
    
    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
    connect(ui->tableWidgetApprovedRecords, SIGNAL(clicked(QModelIndex)), this,
        SLOT(handleProposalClicked(QModelIndex)));

}

ProposalsPage::~ProposalsPage()
{
    delete ui;
}


void ProposalsPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;

}

void ProposalsPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    }
}

QStringList ProposalsPage::listProposals() {
    QStringList List;
    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);

    for (const auto& pGovObj : objs) {
        QString proposalHash = QString::fromStdString(pGovObj->GetDataAsPlainString());
	    List << proposalHash;
	}

    return List;
}

void ProposalsPage::handleProposalClicked(const QModelIndex &index)
{
    //Add randomness here so to not choose same masternode each time
    //Curl here to verify IPFS is reachable
    //If it is then open the link and end loop
    //If not then continue loop to find valid one

    std::map<COutPoint, CDeterministicMNCPtr> mapMasternodes;

    /*
    auto mnList = clientModel->getMasternodeList();

    mnList.ForEachMN(true, [&](const CDeterministicMNCPtr& dmn) {
        mapMasternodes.emplace(dmn->collateralOutpoint, dmn);
    });
 
    int i = rand() % (int)mnList.GetValidMNsCount();
    int j = 0;
    std::string addr;
    for (const auto& p : mapMasternodes) {
        if (i >= j) {
            std::string IPFSPeerID = p.second->pdmnState->IPFSPeerID;
            //&&!p.second->pdmnState->IPFSPeerID == "0"
            if (IPFSPeerID != "0") {
                try {
                    const std::string Ipv4Gateway = "http://" + p.second->pdmnState->addr.ToString() + ":8080/ipfs/QmYwAPJzv5CZsnA625s3Xf2nemtYgPpHdWEz79ojWnPbdG/readme";
                    ipfs::http::TransportCurl curlHelper = ipfs::http::TransportCurl();
                    addr = p.second->pdmnState->addr.ToString();
                    std::stringstream response;
                    curlHelper.Fetch(Ipv4Gateway, {}, &response);
                    break;
                } catch (std::exception& e) {
                    continue;
                }
            }
        } else {
            j++;
        }
    }
    */
    QDesktopServices::openUrl(QUrl("http:/historia.network/governance", QUrl::TolerantMode));
    LogPrintf("ProposalsPage::handleProposalClicked %s\n", proposalsRow[index.row()]);

}

void ProposalsPage::tabSelected(int tabIndex)
{
    ui->tableWidgetApprovedRecords->clearContents();
    ui->tableWidgetApprovedRecords->setRowCount(0);
    ui->tableWidgetApprovedProposals->clearContents();
    ui->tableWidgetApprovedProposals->setRowCount(0);
    
    if (tabIndex == 0) {
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD) {
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
                QTableWidgetItem* url = new QTableWidgetItem(QString::fromStdString(jsonData["url"].get<std::string>()));
                QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
                QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);

                ui->tableWidgetApprovedRecords->insertRow(0);
                ui->tableWidgetApprovedRecords->setItem(0, 0, date);
                ui->tableWidgetApprovedRecords->setItem(0, 1, name);
                ui->tableWidgetApprovedRecords->setItem(0, 2, Vote);
                ui->tableWidgetApprovedRecords->setItem(0, 3, url);
            }
        }

        connect(ui->tableWidgetApprovedProposals, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
    } else if (tabIndex == 1) {
        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                QTableWidgetItem* name = new QTableWidgetItem(QString::fromStdString(jsonData["name"].get<std::string>()));
                QTableWidgetItem* url = new QTableWidgetItem(QString::fromStdString(jsonData["url"].get<std::string>()));
                QTableWidgetItem* date = new QTableWidgetItem(QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"));
                QTableWidgetItem* Vote = new QTableWidgetItem(voteRatio);

                ui->tableWidgetApprovedProposals->insertRow(0);
                ui->tableWidgetApprovedProposals->setItem(0, 0, date);
                ui->tableWidgetApprovedProposals->setItem(0, 1, name);
                ui->tableWidgetApprovedProposals->setItem(0, 2, Vote);
                ui->tableWidgetApprovedProposals->setItem(0, 3, url);
            }
        }

        connect(ui->tableWidgetApprovedProposals, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
    }


}
