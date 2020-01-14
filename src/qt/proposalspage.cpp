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
#include <QTreeWidget>
#include <QPushButton>
#include <QStringList>


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

    int columnNameWidth = 225;
    int columnDateWidth = 150;
    int columnIPFSCIDWidth = 233;
    int columnVoteRatioWidth = 200;
    int columnSummaryWidth = 225;

    //model = new QStringListModel(this);
    //QStringList List = listProposals();
    //model->setStringList(List);
    //ui->listProposals->setModel(model);

    ui->treeWidgetProposals->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetProposals->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetProposals->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetProposals->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetProposals->setColumnWidth(4, columnSummaryWidth);    
        
    ui->treeWidgetVotingRecords->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetVotingRecords->setColumnWidth(4, columnSummaryWidth);

    ui->treeWidgetApprovedRecords->setColumnWidth(0, columnDateWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(1, columnNameWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(2, columnVoteRatioWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(3, columnIPFSCIDWidth);
    ui->treeWidgetApprovedRecords->setColumnWidth(4, columnSummaryWidth);

    ui->treeWidgetProposals->setColumnCount(4);

    std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
    for (const auto& pGovObj : objs) {
        if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
            QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetProposals);
            time_t creationTime = pGovObj->GetCreationTime();
            std::string const plainData = pGovObj->GetDataAsPlainString();
            nlohmann::json jsonData = json::parse(plainData);
            QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

            nlohmann::json summaryData = jsonData["summary"];
            std::string summaryName = summaryData["name"].get<std::string>();
            std::string summaryDesc = summaryData["description"].get<std::string>();

            row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
            row1->setText(1, QString::fromStdString(summaryName));                             //Column 2 - summaryName
            row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
            row1->setText(3, QString::fromStdString(jsonData["ipfscid"].get<std::string>()));  //Column 4 - ipfscid

            //Summary child row for Row1
            QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
            row1_child->setText(0, QString::fromStdString(summaryDesc));
            row1_child->setFirstColumnSpanned(true);
        }
    }
    connect(ui->treeWidgetProposals, SIGNAL(clicked(QModelIndex)), this,
        SLOT(handleProposalClicked(QModelIndex)));

    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
    connect(ui->treeWidgetProposals, SIGNAL(clicked(QModelIndex)), this,
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
    /*
    if(model && model->getOptionsModel())
    {
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    }
*/
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

void ProposalsPage::handleProposalClicked(const QModelIndex& index)
{
    if (!clientModel || ShutdownRequested()) {
        return;
    }

    std::string addr = clientModel->getRandomValidMN();
    std::string urltemp;

    if (ui->tabWidget->currentIndex() == 0) {
        QTreeWidgetItem* item = ui->treeWidgetProposals->currentItem();
        QString ipfscid = item->text(3);
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 1) {
        QTreeWidgetItem* item = ui->treeWidgetVotingRecords->currentItem();
        QString ipfscid = item->text(3);
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } else if (ui->tabWidget->currentIndex() == 2) {
        QTreeWidgetItem* item = ui->treeWidgetApprovedRecords->currentItem();
        QString ipfscid = item->text(3);
        urltemp = "http://" + addr + "/ipfs/" + ipfscid.toUtf8().constData() + "/Index.html";
    } 
    
    QString url = QString::fromUtf8(urltemp.c_str());
    LogPrintf("ProposalsPage::handleProposalClicked %s\n", urltemp);
    QDesktopServices::openUrl(QUrl(url, QUrl::TolerantMode));

}

void ProposalsPage::tabSelected(int tabIndex)
{
    ui->treeWidgetProposals->clear();
    ui->treeWidgetVotingRecords->clear();
    ui->treeWidgetApprovedRecords->clear();
    
    if (tabIndex == 0) {
        ui->treeWidgetProposals->setColumnCount(4);

        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_PROPOSAL) {
                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetProposals);
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                nlohmann::json summaryData = jsonData["summary"];
                std::string summaryName = summaryData["name"].get<std::string>();
                std::string summaryDesc = summaryData["description"].get<std::string>();

                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, QString::fromStdString(summaryName));                             //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, QString::fromStdString(jsonData["ipfscid"].get<std::string>()));  //Column 4 - ipfscid

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, QString::fromStdString(summaryDesc));
                row1_child->setFirstColumnSpanned(true);
            }
        }
        connect(ui->treeWidgetProposals, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        
    } else if (tabIndex == 1) {
        ui->treeWidgetVotingRecords->setColumnCount(4);

        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && !pGovObj->IsSetRecordLocked()) {

                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetVotingRecords);
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                nlohmann::json summaryData = jsonData["summary"];
                std::string summaryName = summaryData["name"].get<std::string>();
                std::string summaryDesc = summaryData["description"].get<std::string>();

                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, QString::fromStdString(summaryName));                             //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, QString::fromStdString(jsonData["ipfscid"].get<std::string>()));  //Column 4 - ipfscid

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, QString::fromStdString(summaryDesc));
                row1_child->setFirstColumnSpanned(true);
            }
        }
        connect(ui->treeWidgetVotingRecords, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
        
    } else if (tabIndex == 2) {
        
        ui->treeWidgetApprovedRecords->setColumnCount(4);

        std::vector<const CGovernanceObject*> objs = governance.GetAllNewerThan(0);
        for (const auto& pGovObj : objs) {
            if (pGovObj->GetObjectType() == GOVERNANCE_OBJECT_RECORD && pGovObj->IsSetRecordLocked()) {

                QTreeWidgetItem* row1 = new QTreeWidgetItem(ui->treeWidgetApprovedRecords);
                time_t creationTime = pGovObj->GetCreationTime();
                std::string const plainData = pGovObj->GetDataAsPlainString();
                nlohmann::json jsonData = json::parse(plainData);
                QString voteRatio = QString::number(pGovObj->GetYesCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetNoCount(VOTE_SIGNAL_FUNDING)) + " / " + QString::number(pGovObj->GetAbstainCount(VOTE_SIGNAL_FUNDING));

                nlohmann::json summaryData = jsonData["summary"];
                std::string summaryName = summaryData["name"].get<std::string>();
                std::string summaryDesc = summaryData["description"].get<std::string>();

                row1->setText(0, (QDateTime::fromTime_t(creationTime).toString("MMMM dd, yyyy"))); //Column 1 - creationTime
                row1->setText(1, QString::fromStdString(summaryName));                             //Column 2 - summaryName
                row1->setText(2, voteRatio);                                                       //Column 3 - voteRatio
                row1->setText(3, QString::fromStdString(jsonData["ipfscid"].get<std::string>()));  //Column 4 - ipfscid

                //Summary child row for Row1
                QTreeWidgetItem* row1_child = new QTreeWidgetItem(row1);
                row1_child->setText(0, QString::fromStdString(summaryDesc));
                row1_child->setFirstColumnSpanned(true);
            }
        }
        connect(ui->treeWidgetApprovedRecords, SIGNAL(clicked(QModelIndex)), this,
            SLOT(handleProposalClicked(QModelIndex)));
     } 
}

